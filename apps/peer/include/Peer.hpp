#pragma once

#include "PeerID.hpp"
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
    ~Peer();
    awaitable<void> handleReq();

  private:
    asio::io_context& ctx_;
    uint16_t port_;
    SQLite::Database& db_;
    PeerID peerId_;

    PeerID load_or_create_identity(SQLite::Database& db);
};
