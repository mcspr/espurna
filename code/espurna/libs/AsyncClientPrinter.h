// -----------------------------------------------------------------------------
// Small wrapper to allow generic methods send data to async client
// TODO: need to check .space() / .add() return result !
// -----------------------------------------------------------------------------

class AsyncClientPrinter : public Print {
    public:
        AsyncClientPrinter(AsyncClient& client) : _client(client) {}
        size_t write(uint8_t* data, size_t size) {
            return _client.add(reinterpret_cast<const char*>(data), size);
        }
        size_t write(uint8_t c) {
            return write(&c, 1);
        }
        void flush() {
            _client.send();
        }
    private:
        AsyncClient& _client;
};
