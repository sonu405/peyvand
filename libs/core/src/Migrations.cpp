#include "Migrations.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::array kMigrations = {
    std::string_view{R"sql(
        CREATE TABLE identity (
            id      INTEGER PRIMARY KEY CHECK (id = 1),
            peer_id BLOB NOT NULL CHECK (length(peer_id) = 16)
        ) STRICT;
    )sql"},

    // last_success and fail_count are for bootstrap. On startup,
    // we sort by recent success and few failures, and evict entries that
    // keep failing.
    std::string_view{R"sql(
        CREATE TABLE known_peers (
            peer_id      BLOB PRIMARY KEY CHECK (length(peer_id) = 16),
            address      TEXT    NOT NULL,
            port         INTEGER NOT NULL CHECK (port BETWEEN 1 AND 65535),
            first_seen   INTEGER NOT NULL,   -- unix seconds
            last_success INTEGER NOT NULL,   -- last completed handshake or valid packet
            fail_count   INTEGER NOT NULL DEFAULT 0,
            source       INTEGER NOT NULL    -- 0 inbound, 1 bootstrap, 2 pex
        ) STRICT;
        )sql"},
};

} // namespace

void migrate(SQLite::Database& db) {
    SQLite::Transaction tx(db, SQLite::TransactionBehavior::IMMEDIATE);

    const int current = db.execAndGet("PRAGMA user_version").getInt();
    const int target = static_cast<int>(kMigrations.size());

    if (current > target)
        throw std::runtime_error("database schema v" + std::to_string(current) +
                                 " is newer than this binary supports (v" + std::to_string(target) +
                                 ")");

    for (int v = current; v < target; ++v)
        db.exec(std::string(kMigrations[static_cast<std::size_t>(v)]));

    if (current != target)
        db.exec("PRAGMA user_version = " + std::to_string(target));

    tx.commit();
}
