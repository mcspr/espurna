/*

NTP MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if NTP_SUPPORT

#include <Ticker.h>
#include <TimeLib.h>
#include <NtpClientLib.h>

unsigned long _ntp_start = 0;
bool _ntp_update = false;
bool _ntp_configure = false;
bool _ntp_want_sync = false;
bool _ntp_synced_once = false;

class NtpClientWrapper : public NTPClient {
public:

    bool espurna_begin(const String& ntpServerName) {
        if (!setNtpServerName(ntpServerName)) {
            return false;
        }

        _lastSyncd = 0;
        udp = new WiFiUDP();

        return true;
    }

};

NtpClientWrapper NTPc;

time_t _ntpGetTime() {
    _ntp_want_sync = true;
    return 0;
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

    _ntp_start = 0;

    NTPc.espurna_begin(getSetting("ntpServer", NTP_SERVER));
    NTPc.setInterval(NTP_SYNC_INTERVAL, NTP_UPDATE_INTERVAL);
    NTPc.setNTPTimeout(NTP_TIMEOUT);

    // XXX TimeLib has status check, but it calls sync function anyways. override locally
    setSyncProvider(_ntpGetTime);

    _ntpConfigure();

}

void _ntpConfigure() {

    _ntp_configure = false;

    int offset = getSetting("ntpOffset", NTP_TIME_OFFSET).toInt();
    int sign = offset > 0 ? 1 : -1;
    offset = abs(offset);
    int tz_hours = sign * (offset / 60);
    int tz_minutes = sign * (offset % 60);
    if (NTPc.getTimeZone() != tz_hours || NTP.getTimeZoneMinutes() != tz_minutes) {
        NTPc.setTimeZone(tz_hours, tz_minutes);
        _ntp_update = true;
    }

    bool daylight = getSetting("ntpDST", NTP_DAY_LIGHT).toInt() == 1;
    if (NTPc.getDayLight() != daylight) {
        NTPc.setDayLight(daylight);
        _ntp_update = true;
    }

    String server = getSetting("ntpServer", NTP_SERVER);
    if (!NTPc.getNtpServerName().equals(server)) {
        NTPc.setNtpServerName(server);
    }

    uint8_t dst_region = getSetting("ntpRegion", NTP_DST_REGION).toInt();
    NTPc.setDSTZone(dst_region);

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

void _ntpSyncWrapper() {
    const time_t ts = NTPc.getTime();
    if (ts) {
        setTime(ts);
        _ntp_synced_once = true;
    }
}

void _ntpLoop() {

    if (0 < _ntp_start && _ntp_start < millis()) _ntpStart();
    if (_ntp_configure) _ntpConfigure();
    if (_ntp_update) _ntpUpdate();

    if (_ntp_want_sync) {
        _ntpSyncWrapper();
        _ntp_want_sync = false;
    }

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
    return _ntp_synced_once;
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
    if (NTPc.isSummerTime()) offset += 60;
    return local - offset * 60;
}

// -----------------------------------------------------------------------------

void ntpSetup() {

    _ntpBackwards();

    NTPc.onNTPSyncEvent([](NTPSyncEvent_t error) {
        if (!error) {
            _ntp_update = true;
            _ntp_synced_once = true;
            return;
        }

        #if WEB_SUPPORT
            wsSend_P(PSTR("{\"ntpStatus\": false}"));
        #endif
        if (error == noResponse) {
            DEBUG_MSG_P(PSTR("[NTP] Error: NTP server not reachable\n"));
        } else if (error == invalidAddress) {
            DEBUG_MSG_P(PSTR("[NTP] Error: Invalid NTP server address\n"));
        } else {
            DEBUG_MSG_P(PSTR("[NTP] Unknown error\n"));
        }
    });

    wifiRegister([](justwifi_messages_t code, char * parameter) {
        if (code == MESSAGE_CONNECTED) _ntp_start = millis() + NTP_START_DELAY;
    });

    #if WEB_SUPPORT
        wsOnSendRegister(_ntpWebSocketOnSend);
        wsOnReceiveRegister(_ntpWebSocketOnReceive);
    #endif

    // Main callbacks
    espurnaRegisterLoop(_ntpLoop);
    espurnaRegisterReload([]() { _ntp_configure = true; });

}

#endif // NTP_SUPPORT
