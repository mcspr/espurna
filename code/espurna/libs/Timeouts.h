// -----------------------------------------------------------------------------
// Really basic helper objects to simplify setup and reset steps
// for periodically called actions
// -----------------------------------------------------------------------------

#pragma once

// Track always increasing value
class counter_t {

    public:
        counter_t() : 
            current(0),
            start(0),
            stop(0)
        {}

        counter_t(size_t start, size_t stop) :
            current(start),
            start(start),
            stop(stop)
        {}

        void reset() {
            current = start;
        }

        void next() {
            if (current < stop) {
                ++current;
            }
        }

        bool done() {
            return (current >= stop);
        }

        size_t current;
        size_t start;
        size_t stop;
};

// Like esp8266::polledTimeout, but shorter and with some new methods
// TODO: the same, but for clockcycles
class timeout_t {

    using ts_unit_t = decltype(millis());

    public:
        timeout_t() :
            _start(0),
            _timeout(0),
            _active(false)
        {}
        timeout_t(ts_unit_t start, ts_unit_t timeout) :
            _start(start),
            _timeout(timeout),
            _active(true)
        {}

        timeout_t(ts_unit_t timeout) :
            timeout_t(millis(), timeout)
        {}

        ICACHE_RAM_ATTR
        operator bool() {
            return ((_active) && (millis() - _start >= _timeout));
        }

        void feed() {
            _start = millis();
            _active = true;
        }

        void reset(ts_unit_t timeout) {
            feed();
            _timeout = timeout;
        }

        void deactivate() {
            _active = false;
            _timeout = 0;
        }

        const ts_unit_t timeout() const {
            return _timeout;
        }

        const ts_unit_t start() const {
            return _start;
        }

    protected:
        ts_unit_t _start;
        ts_unit_t _timeout;
        bool _active;
};
