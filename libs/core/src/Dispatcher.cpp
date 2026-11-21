#include "Dispatcher.hpp"

void Dispatcher::on(peyvand::MessageType t, Handler h) {
    handlers_[t] = std::move(h);
}