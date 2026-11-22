#pragma once

#include "Dispatcher.hpp"
#include "PeerID.hpp"
#include "peyvand/packet.pb.h"
#include <PeerBook.hpp>
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <optional>
#include <spdlog/spdlog.h>

using asio::awaitable;
using asio::ip::udp;

using asio::awaitable;

class Peer {
  public:
    static constexpr auto kTick = std::chrono::milliseconds(500);
    static constexpr auto kRetryDelay = std::chrono::seconds(2);
    static constexpr int kMaxAttempts = 5;

    Peer(asio::io_context& ctx, uint16_t port, SQLite::Database& db);
    awaitable<void> handleReq();
    awaitable<void> probeLoop();

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

    template <std::derived_from<google::protobuf::Message> Msg>
    bool sendPacketSync(const asio::ip::udp::endpoint& to, peyvand::MessageType type,
                        const Msg& body) {
        peyvand::Packet pkt;
        pkt.set_id(nextId_++);
        pkt.set_sender_id(std::string(reinterpret_cast<const char*>(peerId_.bytes().data()),
                                      peerId_.bytes().size()));
        pkt.set_type(type);

        if (!body.SerializeToString(pkt.mutable_payload())) {
            spdlog::error("failed to serialize payload");
            return false;
        }

        std::string wire;
        if (!pkt.SerializeToString(&wire)) {
            spdlog::error("failed to serialize packet");
            return false;
        }

        asio::error_code ec;
        socket_.send_to(asio::buffer(wire), to, 0, ec);
        if (ec) {
            spdlog::warn("send to {}:{} failed: {}", to.address().to_string(), to.port(),
                         ec.message());
            return false;
        }
        return true;
    }

    std::vector<asio::ip::udp::endpoint> load_bootstrap_file(const std::filesystem::path& path);
    std::optional<asio::ip::udp::endpoint> parse_bootstrap_line(std::string_view line);
    void GoodbyeAll();

    PeerBook::Book peerBook;

  private:
    SQLite::Database& db_;
    PeerID peerId_;
    udp::socket socket_;
    Dispatcher dispatcher_;

    // Packet id's
    uint64_t nextId_ = 1;

    PeerID load_or_create_identity(SQLite::Database& db);
};
