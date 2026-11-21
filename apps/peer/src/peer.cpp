#include "Peer.hpp"

Peer::Peer(asio::io_context& ctx, uint16_t port, SQLite::Database& db) :
    ctx_(ctx), port_(port), db_(db), peerId_(load_or_create_identity(db)) {}

Peer::~Peer() {
    ctx_.stop();
}

awaitable<void> Peer::handleReq() {
    auto ex = co_await asio::this_coro::executor;
    udp::socket socket(ex, udp::endpoint(udp::v4(), port_));

    spdlog::info("My ID is {}", peerId_.to_string());
    for (;;) {
        // Accept connections and handle them
        co_await socket.async_send(asio::buffer("Hello, World!"), asio::use_awaitable);
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

        if (!id) {
            throw std::runtime_error("stored peer_id is corrupt");
        }
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