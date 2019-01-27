/*

DEBUG MODULE

Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if DEBUG_SUPPORT

#if DEBUG_UDP_SUPPORT
#include <WiFiUdp.h>
WiFiUDP _udp_debug;
#if DEBUG_UDP_PORT == 514
char _udp_syslog_header[40] = {0};
#endif
#endif

bool _debug_flush = false;

class debug_buffer_t {

    using flush_func_t = std::function<void(const char*, size_t)>;

    private:
        char* storage;
        size_t size;
        size_t pos;
        flush_func_t flush_func;

    public:
        debug_buffer_t(size_t size) :
            storage(new char[size]),
            size(size),
            pos(0),
            flush_func(nullptr)
        {}

        const char* c_str() {
            return storage;
        }

        void attachFlush(flush_func_t func) {
            flush_func = func;
        }

        void reset() {
            pos = 0;
        }

        void flush() {
            if (!flush_func) return;
            flush_func(c_str(), pos);
            reset();
        }

        bool available() {
            return (pos > 0);
        }

        bool append(const char* data) {
            const size_t data_size = strlen(data);
            if (data_size > (size - pos + 1)) {
                flush();
            }
            memcpy(storage + pos, data, data_size);
            pos += data_size;
            storage[pos] = '\0';
            return true;
        }

};

debug_buffer_t _debug_buffer(1280);

void _debugFlushLoop() {
    if (!_debug_flush) return;
    if (!_debug_buffer.available()) return;

    _debug_buffer.flush();
}

void _debugFlush(const char* data, size_t size) {

    bool pause = false;

    #if DEBUG_SERIAL_SUPPORT
        DEBUG_PORT.write(reinterpret_cast<const uint8_t*>(data), size);
    #endif

    #if DEBUG_WEB_SUPPORT
        pause = wsDebugSend(data);
    #endif

    if (pause) optimistic_yield(100); // 0.0001s
}


void _debugSend(char * message) {

    bool pause = false;

    #if DEBUG_ADD_TIMESTAMP
        static bool add_timestamp = true;
        char timestamp[10] = {0};
        if (add_timestamp) {
            snprintf_P(timestamp, sizeof(timestamp), PSTR("[%06lu] "), millis() % 1000000);
            _debug_buffer.append(timestamp);
        }
        add_timestamp = (message[strlen(message)-1] == 10) || (message[strlen(message)-1] == 13);
    #endif

    #if DEBUG_UDP_SUPPORT
        #if SYSTEM_CHECK_ENABLED
        if (systemCheck()) {
        #endif
            _udp_debug.beginPacket(DEBUG_UDP_IP, DEBUG_UDP_PORT);
            #if DEBUG_UDP_PORT == 514
                _udp_debug.write(_udp_syslog_header);
            #endif
            _udp_debug.write(_debug_buffer.c_str());
            _udp_debug.endPacket();
            pause = true;
        #if SYSTEM_CHECK_ENABLED
        }
        #endif
    #endif

    #if DEBUG_TELNET_SUPPORT
        #if DEBUG_ADD_TIMESTAMP
            _telnetWrite(timestamp, 9);
        #endif
        _telnetWrite(message, strlen(message));
        pause = true;
    #endif

    if (pause) optimistic_yield(100); // 0.0001s

    _debug_buffer.append(message);
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
