/*

NTP MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if NTP_SUPPORT

#include <TimeLib.h>
#include <NtpClientLib.h>
#include <WiFiClient.h>
#include <Ticker.h>

unsigned long _ntp_start = 0;
bool _ntp_update = false;
bool _ntp_configure = false;
bool _ntp_ready = false;

String _ntp_server;

void _ntpSetServer(const char* server) {
    _ntp_server = server;
    _ntp_ready = true;
}

// -----------------------------------------------------------------------------
// NTP
// -----------------------------------------------------------------------------

#if WEB_SUPPORT

bool _ntpWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "ntp", 3) == 0);
}

void _ntpWebSocketOnSend(JsonObject& root) {
    root["ntpVisible"] = 1;
    root["ntpStatus"] = (timeStatus() == timeSet);
    root["ntpServer"] = getSetting("ntpServer", NTP_SERVER);
    root["ntpOffset"] = getSetting("ntpOffset", NTP_TIME_OFFSET).toInt();
    root["ntpDST"] = getSetting("ntpDST", NTP_DAY_LIGHT).toInt() == 1;
    root["ntpRegion"] = getSetting("ntpRegion", NTP_DST_REGION).toInt();
    if (ntpSynced()) root["now"] = now();
}

#endif

void _ntpStart() {

    if (!_ntp_ready) {
        _ntpConfigure();
        _ntp_start = millis() + NTP_START_DELAY;
        return;
    }

    NTP.begin(_ntp_server);

    NTP.setInterval(NTP_SYNC_INTERVAL, NTP_UPDATE_INTERVAL);
    NTP.setNTPTimeout(NTP_TIMEOUT);

    _ntp_start = 0;

}

void _ntpConfigure() {

    String server = getSetting("ntpServer", NTP_SERVER);
    #if DNS_RESOLVER
        if (!_ntp_ready) {
            dnsResolve(server.c_str(), &_ntpSetServer);
            return;
        }
    #else
        _ntpSetServer(server.c_str());
    #endif

    if (!_ntp_server.equals(NTP.getNtpServerName())) {
        NTP.setNtpServerName(_ntp_server);
        _ntp_update = true;
    }

    int offset = getSetting("ntpOffset", NTP_TIME_OFFSET).toInt();
    int sign = offset > 0 ? 1 : -1;
    offset = abs(offset);
    int tz_hours = sign * (offset / 60);
    int tz_minutes = sign * (offset % 60);
    if (NTP.getTimeZone() != tz_hours || NTP.getTimeZoneMinutes() != tz_minutes) {
        NTP.setTimeZone(tz_hours, tz_minutes);
        _ntp_update = true;
    }

    bool daylight = getSetting("ntpDST", NTP_DAY_LIGHT).toInt() == 1;
    if (NTP.getDayLight() != daylight) {
        NTP.setDayLight(daylight);
        _ntp_update = true;
    }

    uint8_t dst_region = getSetting("ntpRegion", NTP_DST_REGION).toInt();
    NTP.setDSTZone(dst_region);

    _ntp_configure = false;
    _ntp_ready = true;

}

void _ntpUpdate() {

    _ntp_update = false;

    #if WEB_SUPPORT
        wsSend(_ntpWebSocketOnSend);
    #endif

    if (ntpSynced()) {
        time_t t = now();
        DEBUG_MSG_P(PSTR("[NTP] UTC Time  : %s\n"), (char *) ntpDateTime(ntpLocal2UTC(t)).c_str());
        DEBUG_MSG_P(PSTR("[NTP] Local Time: %s\n"), (char *) ntpDateTime(t).c_str());
    }

}

void _ntpLoop() {

    if (0 < _ntp_start && _ntp_start < millis()) _ntpStart();
    if (_ntp_configure) _ntpConfigure();
    if (_ntp_update) _ntpUpdate();

    now();

    #if BROKER_SUPPORT
        static unsigned char last_minute = 60;
        if (ntpSynced() && (minute() != last_minute)) {
            last_minute = minute();
            brokerPublish(MQTT_TOPIC_DATETIME, ntpDateTime().c_str());
        }
    #endif

}

void _ntpBackwards() {
    moveSetting("ntpServer1", "ntpServer");
    delSetting("ntpServer2");
    delSetting("ntpServer3");
    int offset = getSetting("ntpOffset", NTP_TIME_OFFSET).toInt();
    if (-30 < offset && offset < 30) {
        offset *= 60;
        setSetting("ntpOffset", offset);
    }
}

// -----------------------------------------------------------------------------

bool ntpSynced() {
    return (year() > 2017);
}

String ntpDateTime(time_t t) {
    char buffer[20];
    snprintf_P(buffer, sizeof(buffer),
        PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
        year(t), month(t), day(t), hour(t), minute(t), second(t)
    );
    return String(buffer);
}

String ntpDateTime() {
    if (ntpSynced()) return ntpDateTime(now());
    return String();
}

time_t ntpLocal2UTC(time_t local) {
    int offset = getSetting("ntpOffset", NTP_TIME_OFFSET).toInt();
    if (NTP.isSummerTime()) offset += 60;
    return local - offset * 60;
}

// -----------------------------------------------------------------------------

void ntpSetup() {

    _ntpBackwards();

    NTP.onNTPSyncEvent([](NTPSyncEvent_t error) {
        if (error) {
            #if WEB_SUPPORT
                wsSend_P(PSTR("{\"ntpStatus\": false}"));
            #endif
            if (error == noResponse) {
                DEBUG_MSG_P(PSTR("[NTP] Error: NTP server not reachable\n"));
            } else if (error == invalidAddress) {
                DEBUG_MSG_P(PSTR("[NTP] Error: Invalid NTP server address\n"));
            }
        } else {
            _ntp_update = true;
        }
    });

    wifiRegister([](justwifi_messages_t code, char * parameter) {
        if (code == MESSAGE_CONNECTED) _ntp_start = millis() + NTP_START_DELAY;
    });

    #if WEB_SUPPORT
        wsOnSendRegister(_ntpWebSocketOnSend);
        wsOnReceiveRegister(_ntpWebSocketOnReceive);
    #endif

    #if TERMINAL_SUPPORT
        settingsRegisterCommand(F("NTP"), [](Embedis* e) {
            if (!_ntp_ready) {
                DEBUG_MSG_P(PSTR("[NTP] Not ready yet"));
                return;
            }

            DEBUG_MSG_P(PSTR("[NTP] Server: %s\n"), NTP.getNtpServerNamePtr());
            DEBUG_MSG_P(PSTR("[NTP] Synced: %s\n"), ntpSynced() ? "yes" : "no");
            if (ntpSynced()) {
                time_t t = now();
                DEBUG_MSG_P(PSTR("[NTP] UTC Time  : %s\n"), (char *) ntpDateTime(ntpLocal2UTC(t)).c_str());
                DEBUG_MSG_P(PSTR("[NTP] Local Time: %s\n"), (char *) ntpDateTime(t).c_str());
            }
        });
    #endif

    // Main callbacks
    espurnaRegisterLoop(_ntpLoop);
    espurnaRegisterReload([]() {
        _ntp_configure = true;
        _ntp_ready = false;
    });

}

#endif // NTP_SUPPORT
