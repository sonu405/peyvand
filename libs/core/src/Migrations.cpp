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

    // -------- Stage 2 persistence (resume-from-exact-offset) --------
    // Per-peer cursors. Yeh row hi "hum is peer ke saath kahan the" ko disk pe yaad rakhti hai,
    // taake process band ho ke dobara khule to delivery 0 se nahi, exact agle offset se chale.
    //   next_send_offset = agla ABSOLUTE offset jo main is peer ko bhejne wale message ko dunga
    //   delivered_offset = is peer SE maine ab tak jitne offset tak IN-ORDER deliver kiye
    std::string_view{R"sql(
        CREATE TABLE rt_cursor (
            peer_id          BLOB PRIMARY KEY CHECK (length(peer_id) = 16),
            next_send_offset INTEGER NOT NULL DEFAULT 1,  -- 0 ka matlab "kuch nahi", isliye 1 se shuru
            delivered_offset INTEGER NOT NULL DEFAULT 0
        ) STRICT;
        )sql"},

    // Durable send-queue: har bheja hua message jab tak receiver IN-ORDER deliver na kar de,
    // yahan disk pe rehta hai. Reconnect par yahin se un-delivered messages dobara load+resend hote.
    // deliver confirm hone par (ACK ka delivered_offset) yahan se hat jate hain -- DB chhota rehta.
    std::string_view{R"sql(
        CREATE TABLE rt_outbox (
            peer_id    BLOB    NOT NULL CHECK (length(peer_id) = 16),
            abs_offset INTEGER NOT NULL,          -- absolute offset (NOT window seq)
            payload    BLOB    NOT NULL,
            PRIMARY KEY (peer_id, abs_offset)
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
