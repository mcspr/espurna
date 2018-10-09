/*

NTP MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if NTP_SUPPORT

#include <ctime>

bool _ntp_update = false;
bool _ntp_configure = false;

bool cbtime_set = false;

// -----------------------------------------------------------------------------
// NTP
// -----------------------------------------------------------------------------

#if WEB_SUPPORT

bool _ntpWebSocketOnReceive(const char * key, JsonVariant& value) {
    return (strncmp(key, "ntp", 3) == 0);
}

void _ntpWebSocketOnSend(JsonObject& root) {
    root["ntpVisible"] = 1;
    root["ntpServer"] = getSetting("ntpServer", NTP_SERVER);
    root["ntpTZ"] = getSetting("ntpTZ", NTP_TIMEZONE);
    if (ntpSynced()) root["now"] = time(nullptr);
}

#endif

void _ntpInitialSyncCb(void) {
  cbtime_set = true;
}

void _ntpConfigure() {

// TODO 2.3.0 does not support tzset();
#ifndef ARDUINO_ESP8266_RELEASE_2_3_0
    setenv("TZ", getSetting("ntpTZ", NTP_TIMEZONE).c_str(), 1);
    tzset();
#endif

    configTime(0, 0, getSetting("ntpServer", NTP_SERVER).c_str());

    _ntp_configure = false;

}

void _ntpUpdate() {

    _ntp_update = false;

    #if WEB_SUPPORT
        wsSend(_ntpWebSocketOnSend);
    #endif

    if (ntpSynced()) {
        time_t ts = time(nullptr);

        DEBUG_MSG_P(PSTR("[NTP] UTC Time  : %s\n"), std::asctime(std::gmtime(&ts)));
        DEBUG_MSG_P(PSTR("[NTP] Local Time: %s\n"), std::asctime(std::localtime(&ts)));
    }

}

void _ntpLoop() {

    if (_ntp_configure) _ntpConfigure();
    if (_ntp_update) _ntpUpdate();

}

// -----------------------------------------------------------------------------

inline bool ntpSynced() {
    return cbtime_set;
}

String ntpDateTime() {
    time_t ts = time(nullptr);
    return String(std::asctime(localtime(&ts)));
}

// -----------------------------------------------------------------------------

void ntpSetup() {

    // TODO multiple servers. revert _ntpBackwards?
    // TODO dst / tz on 2.3.0 ?
    configTime(0, 0, getSetting("ntpServer", NTP_SERVER).c_str());
    #ifndef ARDUINO_ESP8266_RELEASE_2_3_0
        settimeofday_cb(_ntpInitialSyncCb);
    #endif

    #if WEB_SUPPORT
        wsOnSendRegister(_ntpWebSocketOnSend);
        wsOnReceiveRegister(_ntpWebSocketOnReceive);
    #endif

    // Main callbacks
    espurnaRegisterLoop(_ntpLoop);
    espurnaRegisterReload([]() { _ntp_configure = true; });

}

#endif // NTP_SUPPORT
