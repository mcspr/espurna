#include <Print.h>

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
