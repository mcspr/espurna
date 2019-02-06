#pragma once

#include <vector>

template <typename Event>
class Broker {

    public:

        virtual ~Broker() {}
        using handler_t = std::function<void (const Broker<Event>& broker, const Event&)>;

    private:
        class Runner {
            public:
                virtual ~Runner() {}

                void subscribe(handler_t handler) {
                    handlers.push_back(handler);
                }

                void publish(const Broker<Event>& broker, const Event& event) {
                    for (auto handler : handlers) {
                        handler(broker, event);
                    }
                }

            private:
                std::vector<handler_t> handlers;
            
        };

        static Runner& getRunner() {
            static Runner runner;
            return runner;
        }

    public:
        void subscribe(handler_t handler) {
            Broker<Event>::getRunner().subscribe(handler);
        }

        void publish(const Event& event) {
            Broker<Event>::getRunner().publish(*this, event);
        }

        std::uintptr_t getId() const {
            return reinterpret_cast<std::uintptr_t>(this);
        }
};
