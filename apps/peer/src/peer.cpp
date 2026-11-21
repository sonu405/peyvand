#include "Peer.hpp"

Peer::Peer(asio::io_context& ctx, uint16_t port, SQLite::Database& db) :
    db_(db), peerId_(load_or_create_identity(db)), socket_(ctx, udp::endpoint(udp::v4(), port)) {

    spdlog::info("ID: {}, listening on port {}", peerId_.to_string(),
                 socket_.local_endpoint().port());

    // register handlers
    dispatcher_.on(peyvand::MESSAGE_TYPE_PING,
                   [this](const auto& from, const PeerID& sender, const peyvand::Packet& pkt) {
                       peyvand::Ping ping;
                       if (!ping.ParseFromString(pkt.payload()))
                           return;

                       spdlog::info("PING from {} nonce={}", sender.to_string(), ping.nonce());
                       peyvand::Pong pong;
                       pong.set_nonce(ping.nonce());
                       sendPacket(from, peyvand::MESSAGE_TYPE_PONG, pong);
                   });

    dispatcher_.on(peyvand::MESSAGE_TYPE_PONG,
                   [this](const auto& /*from*/, const PeerID& sender, const peyvand::Packet& pkt) {
                       peyvand::Pong pong;
                       if (!pong.ParseFromString(pkt.payload()))
                           return;

                       spdlog::info("PONG from {} nonce={}", sender.to_string(), pong.nonce());
                   });
}

awaitable<void> Peer::handleReq() {

    std::array<uint8_t, 1500> buf;
    asio::ip::udp::endpoint from;

    /*
    Testing start
    */
    asio::ip::udp::endpoint tar_epp(socket_.local_endpoint().address(), 9001);

    peyvand::Ping ping;
    ping.set_nonce(ping.nonce());
    sendPacket(socket_.local_endpoint(), peyvand::MESSAGE_TYPE_PONG, ping);
    spdlog::info("SEND THE PING PACKET to {} on!!", tar_epp.address().to_string());
    /*
    Testing end
    */

    for (;;) {
        auto [ec, n] = co_await socket_.async_receive_from(asio::buffer(buf), from,
                                                           asio::as_tuple(asio::use_awaitable));

        if (ec) {
            if (ec == asio::error::operation_aborted)
                co_return; // socket closed or ctx stopping
            spdlog::warn("receive error: {}", ec.message());
            continue;
        }

        peyvand::Packet pkt;

        if (!pkt.ParseFromArray(buf.data(), static_cast<int>(n))) {
            spdlog::warn("dropped malformed packet ({} bytes) from {}", n,
                         from.address().to_string());
            continue;
        }

        dispatcher_.dispatch(from, pkt);
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