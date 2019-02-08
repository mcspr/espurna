#pragma once

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

// implement by the program
extern "C" {
     void custom_crash_callback(struct rst_info*, uint32_t, uint32_t);
}

void _debugSend(char*);

// common debugging symbols
// TODO combine MSG and MSG_P under a single method, using ONLY PSTR
template<typename... Args>
void DEBUG_MSG(const char * format, Args... args) {

    const size_t args_len = sizeof...(args);
    if (args_len == 0) {
        _debugSend(const_cast<char*>(format));
        return;
    }

    char test[1];
    int len = snprintf(test, 1, format, args...) + 1;
    char * buffer = new char[len];
    snprintf(buffer, len, format, args...);

    _debugSend(buffer);

    delete[] buffer;

}

template<typename... Args>
void DEBUG_MSG_P(PGM_P format_P, Args... args) {

    char format[strlen_P(format_P)+1];
    memcpy_P(format, format_P, sizeof(format));

    const size_t args_len = sizeof...(args);
    if (args_len == 0) {
        _debugSend(const_cast<char*>(format));
        return;
    }

    char test[1];
    int len = snprintf(test, 1, format, args...) + 1;
    char * buffer = new char[len];
    snprintf(buffer, len, format, args...);

    _debugSend(buffer);

    delete[] buffer;

}
