/*

ALEXA MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if ALEXA_SUPPORT

#include <fauxmoESP.h>
fauxmoESP alexa;

Broker<RelayStatus> alexaRelaysBroker;

#include <queue>
typedef struct {
    unsigned char device_id;
    bool state;
    unsigned char value;
} alexa_queue_element_t;
static std::queue<alexa_queue_element_t> _alexa_queue;

// -----------------------------------------------------------------------------
// ALEXA
// -----------------------------------------------------------------------------

bool _alexaWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "alexa", 5) == 0);
}

void _alexaWebSocketOnSend(JsonObject& root) {
    root["alexaVisible"] = 1;
    root["alexaEnabled"] = alexaEnabled();
}

void _alexaConfigure() {
    alexa.enable(wifiConnected() && alexaEnabled());
}

#if WEB_SUPPORT
    bool _alexaBodyCallback(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        return alexa.process(request->client(), request->method() == HTTP_GET, request->url(), String((char *)data));
    }

    bool _alexaRequestCallback(AsyncWebServerRequest *request) {
        String body = (request->hasParam("body", true)) ? request->getParam("body", true)->value() : String();
        return alexa.process(request->client(), request->method() == HTTP_GET, request->url(), body);
    }
#endif

#if BROKER_SUPPORT

#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
Broker<LightStatus> alexaLightsBroker;

void _alexaLightBrokerCallback(const Broker<LightStatus>& broker, const LightStatus& event) {
    const size_t channels = lightGetChannels(event.type);
    for (size_t id = 0; id < channels; ++id) {
        alexa.setState(event.id + 1, event.get(id) > 0, event.get(id));
    }
}
#endif

void _alexaRelayBrokerCallback(const Broker<RelayStatus>& broker, const RelayStatus& event) {
#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
        if (id > 0) return;
#endif
    alexa.setState(event.id, event.status, event.status ? 255 : 0);
}
#endif // BROKER_SUPPORT

// -----------------------------------------------------------------------------

bool alexaEnabled() {
    return (getSetting("alexaEnabled", ALEXA_ENABLED).toInt() == 1);
}

void alexaSetup() {

    // Backwards compatibility
    moveSetting("fauxmoEnabled", "alexaEnabled");

    // Basic fauxmoESP configuration
    alexa.createServer(!WEB_SUPPORT);
    alexa.setPort(80);

    // Uses hostname as base name for all devices
    // TODO: use custom switch name when available
    String hostname = getSetting("hostname");

    // Lights
    #if RELAY_PROVIDER == RELAY_PROVIDER_LIGHT

        // Global switch
        alexa.addDevice(hostname.c_str());

        // For each channel
        for (unsigned char i = 1; i <= lightChannels(); i++) {
            alexa.addDevice((hostname + " " + i).c_str());
        }

    // Relays
    #else

        unsigned int relays = relayCount();
        if (relays == 1) {
            alexa.addDevice(hostname.c_str());
        } else {
            for (unsigned int i=1; i<=relays; i++) {
                alexa.addDevice((hostname + " " + i).c_str());
            }
        }

    #endif

    // Load & cache settings
    _alexaConfigure();

    // Websockets
    #if WEB_SUPPORT
        webBodyRegister(_alexaBodyCallback);
        webRequestRegister(_alexaRequestCallback);
        wsOnSendRegister(_alexaWebSocketOnSend);
        wsOnReceiveRegister(_alexaWebSocketOnReceive);
    #endif

    // Register wifi callback
    wifiRegister([](justwifi_messages_t code, char * parameter) {
        if ((MESSAGE_CONNECTED == code) || (MESSAGE_DISCONNECTED == code)) {
            _alexaConfigure();
        }
    });

    // Callback
    alexa.onSetState([&](unsigned char device_id, const char * name, bool state, unsigned char value) {
        alexa_queue_element_t element;
        element.device_id = device_id;
        element.state = state;
        element.value = value;
        _alexa_queue.push(element);
    });

    // Register main callbacks
    #if BROKER_SUPPORT
        alexaRelaysBroker.subscribe(_alexaRelayBrokerCallback);
        #if RELAY_PROVIDER == RELAY_PROVIDER_LIGHT
            alexaLightsBroker.subscribe(_alexaLightBrokerCallback);
        #endif
    #endif
    espurnaRegisterReload(_alexaConfigure);
    espurnaRegisterLoop(alexaLoop);

}

void alexaLoop() {

    alexa.handle();

    while (!_alexa_queue.empty()) {

        alexa_queue_element_t element = _alexa_queue.front();
        DEBUG_MSG_P(PSTR("[ALEXA] Device #%u state: %s value: %d\n"), element.device_id, element.state ? "ON" : "OFF", element.value);

        #if RELAY_PROVIDER == RELAY_PROVIDER_LIGHT
            if (0 == element.device_id) {
                relayStatus(0, element.state);
            } else {
                lightState(element.device_id - 1, element.state);
                lightChannel(element.device_id - 1, element.value);
                lightUpdate(true, true);
            }
        #else
            relayStatus(element.device_id, element.state);
        #endif

        _alexa_queue.pop();
    }

}

#endif
