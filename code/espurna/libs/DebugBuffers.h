// -----------------------------------------------------------------------------
// Helper class for DEBUG_SUPPORT
// -----------------------------------------------------------------------------

#pragma once

// Intended to use as:
// String replacement with fixed size, without possible realloc
// char[][] replacement with flexible element size
class debug_buffer_t {

    using flush_func_t = std::function<void(const char*, size_t)>;

    private:
        char* storage;
        size_t size;
        size_t offset;
        flush_func_t flush_func;
        bool _lock;

    public:
        debug_buffer_t(size_t size) :
            storage(new char[size]),
            size(size),
            offset(0),
            flush_func(nullptr),
            _lock(false)
        {}

        const char* c_str() {
            return storage;
        }

        void attachFlush(flush_func_t func) {
            flush_func = func;
        }

        void reset() {
            offset = 0;
        }

        void flush() {
            if (!flush_func) return;
            _lock = true;
            flush_func(c_str(), offset);
            _lock = false;
            reset();
        }

        bool available() {
            return (offset > 0);
        }

        bool append(const char* data) {
            if (_lock) return false;

            const size_t data_size = strlen(data);
            if (data_size > (size - offset + 1)) {
                flush();
            }
            memcpy(storage + offset, data, data_size);
            offset += data_size;
            storage[offset] = '\0';
            return true;
        }

};

size_t debug_foreach_delim(const char* data, char delim, std::function<size_t(const char*, size_t)> callback) {
    size_t offset = 0;
    size_t res = 0;

    while (true) {
        const char* next = strchr(data + offset, delim);
        if (next == NULL) break;

        // include delim in the data
        size_t len = next - (data + offset) + 1;
        res += callback(data + offset, len);
        offset += len;
    }

    return res;
}
