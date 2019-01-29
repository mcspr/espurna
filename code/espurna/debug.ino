/*

DEBUG MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if DEBUG_SUPPORT

#include "libs/DebugBuffers.h"

#if DEBUG_UDP_SUPPORT

#include <WiFiUdp.h>
WiFiUDP _udp_debug;

#if DEBUG_UDP_PORT == 514
char _udp_syslog_header[40] = {0};
#endif

size_t _debugUdpSend(const char* data, size_t size) {
    size_t res = 0;

    _udp_debug.beginPacket(DEBUG_UDP_IP, DEBUG_UDP_PORT);
    #if DEBUG_UDP_PORT == 514
        _udp_debug.write(_udp_syslog_header);
    #endif
    res = _udp_debug.write(data, size);
    _udp_debug.endPacket();

    delay(0);

    return res;
}

#endif // DEBUG_UDP_SEND

// hide overload ambiguity for std::function + do yield
#if DEBUG_TELNET_SUPPORT
size_t _debugTelnetSend(const char* data, size_t size) {
    size_t res = _telnetWrite(data, size);
    delay(0);
    return res;
}
#endif

bool _debug_flush = false;
debug_buffer_t _debug_buffer(1280);

void _debugFlushLoop() {
    if (!_debug_flush) return;
    if (!_debug_buffer.available()) return;

    _debug_buffer.flush();
    yield();
}

void _debugFlush(const char* data, size_t size) {
    #if DEBUG_SERIAL_SUPPORT
        DEBUG_PORT.write(reinterpret_cast<const uint8_t*>(data), size);
    #endif

    #if DEBUG_UDP_SUPPORT
        #if SYSTEM_CHECK_ENABLED
        if (systemCheck()) {
        #endif
            debug_foreach_delim(data, '\n', _debugUdpSend);
        #if SYSTEM_CHECK_ENABLED
        }
        #endif
    #endif

    #if DEBUG_TELNET_SUPPORT
        debug_foreach_delim(data, '\n', _debugTelnetSend);
    #endif

    #if DEBUG_WEB_SUPPORT
        wsDebugSend(data);
    #endif
}


void _debugSend(char * message) {

    size_t msg_len = strlen(message);

    #if DEBUG_ADD_TIMESTAMP
        constexpr size_t TIMESTAMP_LENGTH = 10;
        std::unique_ptr<char[]> _buffer(new char[msg_len + TIMESTAMP_LENGTH]);
    #else
        std::unique_ptr<char[]> _buffer(new char[msg_len + 1]);
    #endif

    size_t offset = 0;
    char* buffer = _buffer.get();

    #if DEBUG_ADD_TIMESTAMP
        static bool add_timestamp = true;
        if (add_timestamp) {
            snprintf_P(buffer, TIMESTAMP_LENGTH, PSTR("[%06lu] "), millis() % 1000000);
            offset = TIMESTAMP_LENGTH - 1;
        }
        add_timestamp = (message[msg_len - 1] == 10) || (message[msg_len - 1] == 13);
    #endif

    memcpy(buffer + offset, message, msg_len);
    buffer[msg_len + offset] = '\0';

    _debug_buffer.append(buffer);
    _debug_flush = true;
}

// -----------------------------------------------------------------------------

void debugSend(const char * format, ...) {

    va_list args;
    va_start(args, format);
    char test[1];
    int len = ets_vsnprintf(test, 1, format, args) + 1;
    char * buffer = new char[len];
    ets_vsnprintf(buffer, len, format, args);
    va_end(args);

    _debugSend(buffer);

    delete[] buffer;

}

void debugSend_P(PGM_P format_P, ...) {

    char format[strlen_P(format_P)+1];
    memcpy_P(format, format_P, sizeof(format));

    va_list args;
    va_start(args, format_P);
    char test[1];
    int len = ets_vsnprintf(test, 1, format, args) + 1;
    char * buffer = new char[len];
    ets_vsnprintf(buffer, len, format, args);
    va_end(args);

    _debugSend(buffer);

    delete[] buffer;

}

#if DEBUG_WEB_SUPPORT

void debugWebSetup() {

    wsOnSendRegister([](JsonObject& root) {
        root["dbgVisible"] = 1;
    });

    wsOnActionRegister([](uint32_t client_id, const char * action, JsonObject& data) {

        #if TERMINAL_SUPPORT
            if (strcmp(action, "dbgcmd") == 0) {
                const char* command = data.get<const char*>("command");
                char buffer[strlen(command) + 2];
                snprintf(buffer, sizeof(buffer), "%s\n", command);
                terminalInject((void*) buffer, strlen(buffer));
            }
        #endif
        
    });

    #if DEBUG_UDP_SUPPORT
    #if DEBUG_UDP_PORT == 514
        snprintf_P(_udp_syslog_header, sizeof(_udp_syslog_header), PSTR("<%u>%s ESPurna[0]: "), DEBUG_UDP_FAC_PRI, getSetting("hostname").c_str());
    #endif
    #endif


}

#endif // DEBUG_WEB_SUPPORT

// -----------------------------------------------------------------------------

void debugSetup() {

    #if DEBUG_SERIAL_SUPPORT
        DEBUG_PORT.begin(SERIAL_BAUDRATE);
        #if DEBUG_ESP_WIFI
            DEBUG_PORT.setDebugOutput(true);
        #endif
    #endif

    _debug_buffer.attachFlush(_debugFlush);
    espurnaRegisterLoop(_debugFlushLoop);

}

#endif // DEBUG_SUPPORT
