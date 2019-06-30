/*

THINGSPEAK MODULE

Copyright (C) 2019 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if THINGSPEAK_SUPPORT

#if THINGSPEAK_USE_ASYNC
#include <ESPAsyncTCP.h>
#else
#include <ESP8266WiFi.h>
#endif

#include "libs/AsyncClientPrinter.h"

const char THINGSPEAK_REQUEST_TEMPLATE[] PROGMEM =
    "POST %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: ESPurna\r\n"
    "Connection: close\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: %d\r\n\r\n";

#if THINGSPEAK_USE_ASYNC

AsyncClient * _tspk_client;
bool _tspk_connecting = false;
bool _tspk_connected = false;

#endif

struct thingspeak_retry_t {

    thingspeak_retry_t(uint8_t limit) :
        _limit(limit),
        _times(limit)
    {}

    operator bool() {
        if (_times) return _times--;
        return false;
    }

    void reset() {
        _times = _limit;
    }

    uint8_t times() {
        return _times;
    }

private:

    const uint8_t _limit;
    uint8_t _times;
};


class thingspeak_queue_t {

private:

    std::bitset<THINGSPEAK_FIELDS> _used;
    std::vector<String> _fields;
    String _apiKey;
    bool _do_clear;

public:

    thingspeak_queue_t(const String&& apiKey, bool do_clear) :
        _fields(THINGSPEAK_FIELDS),
        _apiKey(apiKey),
        _do_clear(do_clear)
    {
        // XXX: based on maximum sensor buffer size
        for (auto& field : _fields) {
            field.reserve(10);
        }
    }

    uint8_t fields() {
        return _used.count();
    }

    bool place(unsigned char index, const char* data) {
        if (index >= _fields.size()) return false;

        _used.set(index);
        _fields[index] = "";
        _fields[index].concat(data);

        return true;
    }

    void setClear(bool clear) {
        _do_clear = clear;
    }

    // XXX: maybeClear() ?
    void clear() {
        if (!_do_clear) return;
        for (unsigned char n=0; n < _fields.size(); ++n) {
            if (!_fields[n].length()) continue;
            _fields[n] = "";
            _used.reset(n);
        }
    }

    bool empty() {
        for (auto& field : _fields) {
            if (field.length()) return false;
        }
        return true;
    }

    size_t estimate() {

        size_t size = 0;

        for (unsigned char id=0; id<_fields.size(); ++id) {
            if (!_fields[id].length()) continue;
            size += _fields[id].length();
            size += strlen("field=&");
            if (id < 10) {
                size += 1;
            } else if (id < 100) {
                size += 2;
            }
        }

        if (!size) return size;

        size += _apiKey.length();
        size += strlen("api_key=");

        return size;

    }

    void sendPayload(Print& output) {

        unsigned char join = 0;
        for (unsigned char id=0; id<_fields.size(); ++id) {
            if (!_fields[id].length()) continue;
            if (join) output.write("&");
            char buf[24] = {0};
            snprintf_P(buf, sizeof(buf), PSTR("field%u=%s"), (id + 1), _fields[id].c_str());
            output.write(buf);
            ++join;
        }

        char buf[32] = {0};
        snprintf_P(buf, sizeof(buf), PSTR("&api_key=%s"), _apiKey.c_str());
        output.write(buf);

    }

    void sendPayload(const Print& output) {
        sendPayload((Print&) output);
    }

};

thingspeak_retry_t _tspk_retry(THINGSPEAK_TRIES);
thingspeak_queue_t* _tspk_queue = nullptr;
bool _tspk_flush = false;
bool _tspk_enabled = false;
unsigned long _tspk_last_flush = 0;
unsigned char _tspk_tries = 0;

// -----------------------------------------------------------------------------

#if BROKER_SUPPORT
void _tspkBrokerCallback(const unsigned char type, const char * topic, unsigned char id, const char * payload) {

    // Process status messages
    if (BROKER_MSG_TYPE_STATUS == type) {
        tspkEnqueueRelay(id, payload);
        tspkFlush();
    }

    // Porcess sensor messages
    if (BROKER_MSG_TYPE_SENSOR == type) {
        //tspkEnqueueMeasurement(id, (char *) payload);
        //tspkFlush();
    }

}
#endif // BROKER_SUPPORT


#if WEB_SUPPORT

bool _tspkWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "tspk", 4) == 0);
}

void _tspkWebSocketOnSend(JsonObject& root) {

    unsigned char visible = 0;

    root["tspkEnabled"] = getSetting("tspkEnabled", THINGSPEAK_ENABLED).toInt() == 1;
    root["tspkKey"] = getSetting("tspkKey");
    root["tspkClear"] = getSetting("tspkClear", THINGSPEAK_CLEAR_CACHE).toInt() == 1;

    JsonArray& relays = root.createNestedArray("tspkRelays");
    for (byte i=0; i<relayCount(); i++) {
        relays.add(getSetting("tspkRelay", i, 0).toInt());
    }
    if (relayCount() > 0) visible = 1;

    #if SENSOR_SUPPORT
        _sensorWebSocketMagnitudes(root, "tspk");
        visible = visible || (magnitudeCount() > 0);
    #endif

    root["tspkVisible"] = visible;

}

#endif

#if THINGSPEAK_USE_ASYNC
void _tspkInitAsyncClient() {

    _tspk_client = new AsyncClient();

    // Normal disconnection routine
    _tspk_client->onDisconnect([](void *s, AsyncClient *c) {
        DEBUG_MSG_P(PSTR("[THINGSPEAK] Disconnected\n"));
        _tspk_connected = false;
        _tspk_connecting = false;
    }, nullptr);

    _tspk_client->onTimeout([](void *s, AsyncClient *c, uint32_t time) {
        DEBUG_MSG_P(PSTR("[THINGSPEAK] No response %ums\n"), time);
        c->close(true);
    }, nullptr);

    _tspk_client->onData([](void * arg, AsyncClient * c, void * response, size_t len) {

        char* data = reinterpret_cast<char*>(response);
        data[len] = 0;

        char * p = strstr(data, "\r\n\r\n");
        unsigned int code = (p != NULL) ? atoi(&p[4]) : 0;
        DEBUG_MSG_P(PSTR("[THINGSPEAK] Response value: %d\n"), code);

        _tspk_last_flush = millis();
        if (0 == code) {
            _tspk_flush = true;
            DEBUG_MSG_P(PSTR("[THINGSPEAK] Re-enqueuing %u more time(s)\n"), _tspk_retry.times());
        } else {
            _tspk_retry.reset();
            _tspk_queue->clear();
        }

        c->close(true);

    }, nullptr);

    _tspk_client->onConnect([](void * arg, AsyncClient * c) {

        _tspk_connected = true;
        _tspk_connecting = false;

        DEBUG_MSG_P(PSTR("[THINGSPEAK] Connected to %s:%d\n"), THINGSPEAK_HOST, THINGSPEAK_PORT);

        if (!_tspk_queue->estimate()) {
            DEBUG_MSG_P(PSTR("[THINGSPEAK] No payload, aborting\n"));
            c->close(true);
            return;
        }

        #if THINGSPEAK_USE_SSL
            uint8_t fp[20] = {0};
            sslFingerPrintArray(THINGSPEAK_FINGERPRINT, fp);
            SSL * ssl = _tspk_client->getSSL();
            if (ssl_match_fingerprint(ssl, fp) != SSL_OK) {
                DEBUG_MSG_P(PSTR("[THINGSPEAK] Warning: certificate doesn't match\n"));
            }
        #endif

        DEBUG_MSG_P(PSTR("[THINGSPEAK] Sending %u field(s)\n"), _tspk_queue->fields());
        char headers[strlen_P(THINGSPEAK_REQUEST_TEMPLATE) + strlen(THINGSPEAK_URL) + strlen(THINGSPEAK_HOST)];

        snprintf_P(headers, sizeof(headers),
            THINGSPEAK_REQUEST_TEMPLATE,
            THINGSPEAK_URL,
            THINGSPEAK_HOST,
            _tspk_queue->estimate()
        );

        Serial.println(headers);
        _tspk_queue->sendPayload(Serial);
        Serial.println();

        c->write(headers);
        _tspk_queue->sendPayload(AsyncClientPrinter(*c));

        c->send();

    }, NULL);

}

#endif // THINGSPEAK_USE_ASYNC

void _tspkConfigure() {
    _tspk_enabled = getSetting("tspkEnabled", THINGSPEAK_ENABLED).toInt() == 1;
    String apiKey = getSetting("tspkKey");
    if (_tspk_enabled && !apiKey.length()) {
        _tspk_enabled = false;
        setSetting("tspkEnabled", 0);
    }

    if (_tspk_enabled) {
        bool clear = getSetting("tspkClear", THINGSPEAK_CLEAR_CACHE).toInt() == 1;
        if (!_tspk_queue) {
            _tspk_queue = new thingspeak_queue_t(std::move(apiKey), clear);
        } else {
            _tspk_queue->setClear(clear);
        }
        #if THINGSPEAK_USE_ASYNC
            if (!_tspk_client) _tspkInitAsyncClient();
        #endif
    }

}

#if THINGSPEAK_USE_ASYNC

void _tspkPost() {

    if (!_tspk_client) return;
    if (_tspk_connecting || _tspk_connected) return;
    if (!_tspk_queue->estimate()) return;

    _tspk_connecting = true;

    #if ASYNC_TCP_SSL_ENABLED
        _tspk_client->connect(THINGSPEAK_HOST, THINGSPEAK_PORT, THINGSPEAK_USE_SSL);
    #else
        _tspk_client->connect(THINGSPEAK_HOST, THINGSPEAK_PORT);
    #endif

}

#else // THINGSPEAK_USE_ASYNC

void _tspkPost() {

    #if THINGSPEAK_USE_SSL
        WiFiClientSecure _tspk_client;
    #else
        WiFiClient _tspk_client;
    #endif

    if (_tspk_client.connect(THINGSPEAK_HOST, THINGSPEAK_PORT)) {

        DEBUG_MSG_P(PSTR("[THINGSPEAK] Connected to %s:%d\n"), THINGSPEAK_HOST, THINGSPEAK_PORT);

        if (!_tspk_client.verify(THINGSPEAK_FINGERPRINT, THINGSPEAK_HOST)) {
            DEBUG_MSG_P(PSTR("[THINGSPEAK] Warning: certificate doesn't match\n"));
        }

        DEBUG_MSG_P(PSTR("[THINGSPEAK] Sending %u field(s)\n"), _tspk_queue->fields());
        char buffer[strlen_P(THINGSPEAK_REQUEST_TEMPLATE) + strlen(THINGSPEAK_URL) + strlen(THINGSPEAK_HOST) + 1];
        snprintf_P(buffer, sizeof(buffer),
            THINGSPEAK_REQUEST_TEMPLATE,
            THINGSPEAK_URL,
            THINGSPEAK_HOST,
            _tspk_queue->estimate()
        );
        _tspk_client.print(buffer);
        _tspk_queue->sendPayload(_tspk_client);

        nice_delay(100);

        String response = _tspk_client.readString();
        int pos = response.indexOf("\r\n\r\n");
        unsigned int code = (pos > 0) ? response.substring(pos + 4).toInt() : 0;
        DEBUG_MSG_P(PSTR("[THINGSPEAK] Response value: %d\n"), code);
        _tspk_client.stop();

        _tspk_last_flush = millis();
        if (0 == code) {
            _tspk_flush = true;
            DEBUG_MSG_P(PSTR("[THINGSPEAK] Re-enqueuing %u more time(s)\n"), _tspk_retry.times());
        } else {
            _tspk_retry.reset();
            _tspk_queue->clear();
        }

        return;

    }

    DEBUG_MSG_P(PSTR("[THINGSPEAK] Connection failed\n"));

}

#endif // THINGSPEAK_USE_ASYNC

void _tspkEnqueue(unsigned char index, const char * payload) {
    DEBUG_MSG_P(PSTR("[THINGSPEAK] Enqueuing field #%u with value %s\n"), index, payload);
    --index;
    if (_tspk_queue) _tspk_queue->place(index, payload);
}

void _tspkFlush() {

    if (!_tspk_queue) return;
    if (!_tspk_flush) return;

    _tspk_flush = false;

    if (_tspk_queue->empty()) return;
    if (_tspk_retry) {
        _tspkPost();
    } else {
        _tspk_queue->clear();
        _tspk_retry.reset();
    }

}

// -----------------------------------------------------------------------------

bool tspkEnqueueRelay(unsigned char index, const char * payload) {
    if (!_tspk_enabled) return true;
    if (!_tspk_queue) return true;
    unsigned char id = getSetting("tspkRelay", index, 0).toInt();
    if (id > 0) {
        _tspkEnqueue(id, payload);
        return true;
    }
    return false;
}

bool tspkEnqueueMeasurement(unsigned char index, const char * payload) {
    if (!_tspk_enabled) return true;
    if (!_tspk_queue) return true;
    unsigned char id = getSetting("tspkMagnitude", index, 0).toInt();
    if (id > 0) {
        _tspkEnqueue(id, payload);
        return true;
    }
    return false;
}

void tspkFlush() {
    _tspk_flush = true;
}

bool tspkEnabled() {
    return _tspk_enabled;
}

void tspkSetup() {

    _tspkConfigure();

    #if WEB_SUPPORT
        wsOnSendRegister(_tspkWebSocketOnSend);
        wsOnReceiveRegister(_tspkWebSocketOnReceive);
    #endif

    #if BROKER_SUPPORT
        brokerRegister(_tspkBrokerCallback);
    #endif

    DEBUG_MSG_P(PSTR("[THINGSPEAK] Async %s, SSL %s\n"),
        THINGSPEAK_USE_ASYNC ? "ENABLED" : "DISABLED",
        THINGSPEAK_USE_SSL ? "ENABLED" : "DISABLED"
    );

    // Main callbacks
    espurnaRegisterLoop(tspkLoop);
    espurnaRegisterReload(_tspkConfigure);

}

void tspkLoop() {
    if (!_tspk_enabled) return;
    if (!wifiConnected() || (WiFi.getMode() != WIFI_STA)) return;
    if (_tspk_flush && (millis() - _tspk_last_flush > THINGSPEAK_MIN_INTERVAL)) {
        _tspkFlush();
    }
}

#endif
