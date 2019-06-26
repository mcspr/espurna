#pragma once

#include <Print.h>

#if defined(ARDUINO_ESP8266_RELEASE_2_3_0)

class PrintWrap {

    public:
        
        PrintWrap(Print& target)
            : target(target)
        {}

    size_t printf_P(PGM_P format, ...) {
        va_list arg;
        va_start(arg, format);
        char temp[64];
        char* buffer = temp;
        size_t len = vsnprintf_P(temp, sizeof(temp), format, arg);
        va_end(arg);
        if (len > sizeof(temp) - 1) {
            buffer = new char[len + 1];
            if (!buffer) {
                return 0;
            }
            va_start(arg, format);
            vsnprintf_P(buffer, len + 1, format, arg);
            va_end(arg);
        }
        len = target.write((const uint8_t*) buffer, len);
        if (buffer != temp) {
            delete[] buffer;
        }
        return len;
    }

    private:
        Print& target;

};

#endif

class DebugPrinter : public Print, public String {

    public:

        ~DebugPrinter() {
            flush();
        }

        void setFlush(size_t size) {
            flush_size = size;
        }

        size_t write(uint8_t data) {
            if (length() >= flush_size) flush();
            return concat((char) data);
        }

        void flush() {
            debugSend(c_str());
            copy("", 0);
        }

    private:

        size_t flush_size = 256;

};
