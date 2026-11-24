#include "Peer.hpp"
#include "peyvand/packet.pb.h"
#include <asio/as_tuple.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <spdlog/spdlog.h>

Peer::Peer(asio::io_context& ctx, uint16_t port, SQLite::Database& db) :
    db_(db), peerId_(load_or_create_identity(db)), socket_(ctx, udp::endpoint(udp::v4(), port)) {

    spdlog::info("ID: {} Handling requests on {}:{}!!", peerId_.short_tag(),
                 socket_.local_endpoint().address().to_string(), socket_.local_endpoint().port());

    // true --> bypass gate, go to dispatch handler
    dispatcher_.setGate(
        [this](const udp::endpoint& from, const PeerID& sender, const peyvand::Packet& pkt) {
            const auto type = pkt.type();

            if (type == peyvand::MESSAGE_TYPE_SYN || type == peyvand::MESSAGE_TYPE_SYN_ACK)
                return true; // handshake bypasses the gate

            // touch validates that the pkt came from established connection
            // It also updates the last_seen.
            if (peerBook.touch(sender, from, pkt.session_id())) { return true; }

            // Not established. Never answer a RESET with a RESET.
            if (type != peyvand::MESSAGE_TYPE_RESET)
                sendPacket(from, peyvand::MESSAGE_TYPE_RESET, peyvand::Reset{});

            return false;
        });

    // register handlers
    dispatcher_.on(peyvand::MESSAGE_TYPE_PING,
                   [this](const auto& from, const PeerID& sender, const peyvand::Packet& pkt) {
                       peyvand::Ping ping;
                       if (!ping.ParseFromString(pkt.payload())) return;

                       spdlog::info("PING from {} nonce={}", sender.to_string(), ping.nonce());
                       peyvand::Pong pong;
                       pong.set_nonce(ping.nonce());
                       sendPacket(from, peyvand::MESSAGE_TYPE_PONG, pong);
                   });

    dispatcher_.on(peyvand::MESSAGE_TYPE_PONG,
                   [](const auto& /*from*/, const PeerID& sender, const peyvand::Packet& pkt) {
                       peyvand::Pong pong;
                       if (!pong.ParseFromString(pkt.payload())) return;

                       spdlog::info("PONG from {} nonce={}", sender.to_string(), pong.nonce());
                   });

    dispatcher_.on(peyvand::MESSAGE_TYPE_SYN, [this](const auto& from, const PeerID& sender,
                                                     const peyvand::Packet& /*pkt*/) {
        if (sender == peerId_) return; // peer finding itself in bootstrap file

        // adding to known peers
        auto now = std::chrono::steady_clock::now();

        peerBook.removeCandidate(from); // remove if one sending the SYN is in our candidate list.
        peerBook.updateToKnown(sender, from, now);
        peerBook.updateToEstablished(sender, from, now);

        spdlog::info("established with {}", sender.short_tag());

        peyvand::SynAck synAck;

        sendPacket(from, peyvand::MESSAGE_TYPE_SYN_ACK, synAck);
    });

    dispatcher_.on(peyvand::MESSAGE_TYPE_SYN_ACK,
                   [this](const auto& from, const PeerID& sender, const peyvand::Packet& /*pkt*/) {
                       if (!peerBook.completeCandidate(from, sender)) {
                           spdlog::debug("unsolicited SYN_ACK from {}", from.address().to_string());
                           return;
                       }

                       // Sending PEX request
                       peyvand::PexRequest pexReq;
                       sendPacket(from, peyvand::MESSAGE_TYPE_PEX_REQUEST, pexReq);
                       spdlog::info("sending PEX request to: {}", sender.short_tag());

                       const auto now = std::chrono::steady_clock::now();
                       peerBook.updateToKnown(sender, from, now);
                       peerBook.updateToEstablished(sender, from, now);
                       spdlog::info("established with {}", sender.short_tag());
                   });

    dispatcher_.on(peyvand::MESSAGE_TYPE_LEAVE, [this](const auto& /*from*/, const PeerID& sender,
                                                       const peyvand::Packet& /*pkt*/) {
        peerBook.removeEstablished(sender);
        spdlog::info("{} left", sender.short_tag());
    });

    dispatcher_.on(peyvand::MESSAGE_TYPE_RESET,
                   [this](const auto& from, const PeerID& sender, const peyvand::Packet& /*pkt*/) {
                       peerBook.removeEstablished(sender);
                       peerBook.addCandidate(from, sender);
                       spdlog::info("RESET from {}, re-handshaking", sender.short_tag());
                   });

    dispatcher_.on(peyvand::MESSAGE_TYPE_PEX_REQUEST, [this](const auto& from, const PeerID& sender,
                                                             const peyvand::Packet& /*pkt*/) {
        peyvand::PexResponse resp;
        for (const auto& [id, s] : peerBook.established()) {
            if (id == sender) continue;
            if (resp.peers_size() >= static_cast<int>(kPexMax)) break;

            auto* r = resp.add_peers();
            r->set_peer_id(
                std::string(reinterpret_cast<const char*>(id.bytes().data()), id.bytes().size()));
            r->set_ipv4(s.endpoint.address().to_v4().to_uint());
            r->set_port(s.endpoint.port());

            sendPacket(from, peyvand::MESSAGE_TYPE_PEX_RESPONSE, resp);
        }
    });

    dispatcher_.on(peyvand::MESSAGE_TYPE_PEX_RESPONSE, [this](const auto& /*from*/,
                                                              const PeerID& /*sender*/,
                                                              const peyvand::Packet& pkt) {
        peyvand::PexResponse resp;
        if (!resp.ParseFromString(pkt.payload())) return;

        int n = 0;
        for (const auto& r : resp.peers()) {

            // bound what we accept
            if (++n > static_cast<int>(kPexMax)) break;

            const auto& s = r.peer_id();
            auto id = PeerID::from_bytes({reinterpret_cast<const uint8_t*>(s.data()), s.size()});

            if (!id || *id == peerId_) continue;
            if (r.port() < 1 || r.port() > 65535) continue;

            const auto addr = asio::ip::make_address_v4(r.ipv4());
            if (addr.is_unspecified()) continue;

            if (peerBook.isEstablished(*id)) continue;

            peerBook.addCandidate(udp::endpoint(addr, static_cast<uint16_t>(r.port())), *id);
        }
    });
}

awaitable<void> Peer::handleReq() {

    std::array<uint8_t, 1500> buf;
    asio::ip::udp::endpoint from;

    for (;;) {
        auto [ec, n] = co_await socket_.async_receive_from(asio::buffer(buf), from,
                                                           asio::as_tuple(asio::use_awaitable));

        if (ec) {
            if (ec == asio::error::operation_aborted) co_return; // socket closed or ctx stopping
            spdlog::warn("receive error: {}", ec.message());
            continue;
        }

        peyvand::Packet pkt;

        if (!pkt.ParseFromArray(buf.data(), static_cast<int>(n))) {
            spdlog::warn("dropped malformed packet ({} bytes) from {}", n,
                         from.address().to_string());
            continue;
        }

        dispatcher_.dispatch(from, pkt); // checks if from established peer
    }
}

asio::awaitable<void> Peer::probeLoop() {
    asio::steady_timer timer(co_await asio::this_coro::executor);

    for (;;) {
        timer.expires_after(kTick);
        auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec) co_return; // cancelled on shutdown

        for (const auto& id : peerBook.expireEstablished(Peer::kSessionTimeout)) {
            spdlog::info("{} timed out", id.short_tag());
        }

        const auto now = PeerBook::Clock::now();
        for (const auto& ep : peerBook.dueCandidates(now, kRetryDelay, kMaxAttempts)) {
            sendPacket(ep, peyvand::MESSAGE_TYPE_SYN, peyvand::Syn{});
        }
    }
}

awaitable<void> Peer::heartBeatsLoop() {
    asio::steady_timer timer(co_await asio::this_coro::executor);

    for (;;) {
        timer.expires_after(kHeartbeatInterval);
        auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec) co_return;

        peyvand::Heartbeat heartbeat;
        for (const auto& [id, peer] : peerBook.established()) {
            sendPacket(peer.endpoint, peyvand::MESSAGE_TYPE_HEARTBEAT, heartbeat);
        }
    }
}

static std::chrono::milliseconds jitter() {
    thread_local std::mt19937 rng{std::random_device{}()};
    return std::chrono::milliseconds(std::uniform_int_distribution<int>(-5000, 5000)(rng));
}

awaitable<void> Peer::pexLoop() {
    asio::steady_timer timer(co_await asio::this_coro::executor);

    for (;;) {
        // jitter so peers don't request in lockstep: kPexInterval +/- 5 s
        timer.expires_after(kPexInterval + jitter());
        auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (ec) co_return;

        peyvand::PexRequest pexReq;
        for (const auto& [id, peer] : peerBook.established()) {
            sendPacket(peer.endpoint, peyvand::MESSAGE_TYPE_PEX_REQUEST, pexReq);
        }
    }
}

PeerID Peer::load_or_create_identity(SQLite::Database& db) {
    SQLite::Transaction tx(db, SQLite::TransactionBehavior::IMMEDIATE);
    SQLite::Statement sel(db, "SELECT peer_id FROM identity WHERE id = 1");

    if (sel.executeStep()) {
        const auto col = sel.getColumn(0);
        const std::span<const uint8_t> blob{static_cast<const uint8_t*>(col.getBlob()),
                                            static_cast<std::size_t>(col.getBytes())};
        auto id = PeerID::from_bytes(blob); // validates length

        if (!id) { throw std::runtime_error("stored peer_id is corrupt"); }
        tx.commit();
        return *id;
    }

    const PeerID id = PeerID::generate(); // only reached on first run
    SQLite::Statement ins(db, "INSERT INTO identity (id, peer_id) VALUES (1, ?1)");
    ins.bind(1, id.bytes().data(), static_cast<int>(id.bytes().size()));
    ins.exec();
    tx.commit();
    return id;
}

std::optional<asio::ip::udp::endpoint> Peer::parse_bootstrap_line(std::string_view line) {
    if (auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);

    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return std::nullopt; // blank or comment
    line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);

    const auto colon = line.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;

    asio::error_code ec;
    const auto addr = asio::ip::make_address_v4(std::string(line.substr(0, colon)), ec);
    if (ec) return std::nullopt;

    const auto port_str = line.substr(colon + 1);
    unsigned port = 0;
    const auto [ptr, err] =
        std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
    if (err != std::errc{} || ptr != port_str.data() + port_str.size() || port < 1 || port > 65535)
        return std::nullopt;

    return asio::ip::udp::endpoint(addr, static_cast<uint16_t>(port));
}

std::vector<asio::ip::udp::endpoint> Peer::load_bootstrap_file(const std::filesystem::path& path) {
    std::vector<asio::ip::udp::endpoint> out;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open bootstrap file: " + path.string());

    constexpr std::size_t kMaxEntries = 256;
    std::string line;
    std::size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (out.size() >= kMaxEntries) {
            spdlog::warn("bootstrap file capped at {}", kMaxEntries);
            break;
        }

        if (auto ep = parse_bootstrap_line(line)) {
            out.push_back(*ep);
        } else if (line.find_first_not_of(" \t\r") != std::string::npos &&
                   line.find_first_not_of(" \t\r") != line.find('#')) {
            spdlog::warn("{}:{}: skipping invalid entry", path.string(), lineno);
        }
    }
    spdlog::info("loaded {} bootstrap peers from {}", out.size(), path.string());
    return out;
}

void Peer::GoodbyeAll() {
    peyvand::Leave leave;

    for (const auto& [id, session] : peerBook.established())
        sendPacketSync(session.endpoint, peyvand::MESSAGE_TYPE_LEAVE, leave);
}

SQLite::Database& Peer::db() {
    return db_;
};

void Peer::saveKnownPeers() {
    try {
        peerBook.flushKnown(db_);
        spdlog::info("saved known peers");
    } catch (const std::exception& e) { spdlog::error("failed to save known peers: {}", e.what()); }
}

void Peer::loadKnownPeers() {
    try {
        const auto n = peerBook.loadKnownAsCandidates(db_, 64);
        spdlog::info("loaded {} known peers as candidates", n);
    } catch (const std::exception& e) { spdlog::error("failed to load known peers: {}", e.what()); }
}
