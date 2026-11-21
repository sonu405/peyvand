#pragma once

#include "Dispatcher.hpp"
#include "PeerID.hpp"
#include "peyvand/packet.pb.h"
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <iostream>
#include <spdlog/spdlog.h>

using asio::awaitable;
using asio::ip::udp;

using asio::awaitable;

class Peer {
  public:
    Peer(asio::io_context& ctx, uint16_t port, SQLite::Database& db);
    awaitable<void> handleReq();

    template <std::derived_from<google::protobuf::Message> Msg>
    void sendPacket(const asio::ip::udp::endpoint& to, peyvand::MessageType type, const Msg& body) {
        peyvand::Packet pkt;
        pkt.set_id(nextId_++);
        pkt.set_sender_id(std::string(reinterpret_cast<const char*>(peerId_.bytes().data()),
                                      peerId_.bytes().size()));
        pkt.set_type(type);

        if (!body.SerializeToString(pkt.mutable_payload())) {
            spdlog::error("failed to serialize payload");
            return;
        }

        auto wire = std::make_shared<std::string>();
        if (!pkt.SerializeToString(wire.get())) {
            spdlog::error("failed to serialize packet");
            return;
        }

        socket_.async_send_to(asio::buffer(*wire), to,
                              [wire](const asio::error_code& ec, std::size_t) {
                                  if (ec)
                                      spdlog::warn("send failed: {}", ec.message());
                              });
    }

  private:
    SQLite::Database& db_;
    PeerID peerId_;
    udp::socket socket_;
    Dispatcher dispatcher_;

    // Packet id's
    uint64_t nextId_ = 1;

    PeerID load_or_create_identity(SQLite::Database& db);
};
