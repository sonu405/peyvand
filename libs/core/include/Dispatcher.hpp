#pragma once

#include "PeerID.hpp"
#include "peyvand/packet.pb.h"
#include <asio.hpp>
#include <functional>
#include <spdlog/spdlog.h>
#include <unordered_map>

/*
Dispatcher maps a MessageType to a callback function in its maintained underordered_map named
handlers_.

These callbacks are registered for each MessageType using Dispatch.on().

Everytime we get some packet, we hand it to the dispatch function, which is responsible for
calling the correct handler based on the message type..
*/
class Dispatcher {
  public:
    // Handler is basically a function that takes in the udp endpoint from where packet came
    using Handler = std::function<void(const asio::ip::udp::endpoint& from, const PeerID& sender,
                                       const peyvand::Packet& pkt)>;

    void on(peyvand::MessageType t, Handler h);

    void dispatch(const asio::ip::udp::endpoint& from, const peyvand::Packet& pkt) {
        const auto& s = pkt.sender_id();
        auto sender = PeerID::from_bytes({reinterpret_cast<const uint8_t*>(s.data()), s.size()});

        if (!sender) {
            spdlog::warn("dropped packet: bad sender_id from {}", from.address().to_string());
            return;
        }

        auto it = handlers_.find(pkt.type());
        if (it == handlers_.end()) {
            spdlog::debug("no handler for type {}", static_cast<int>(pkt.type()));
            return;
        }

        // *sender --> unwaps the optional<PeerID>  --> PeerID
        it->second(from, *sender, pkt);
    }

  private:
    std::unordered_map<peyvand::MessageType, Handler> handlers_;
};
