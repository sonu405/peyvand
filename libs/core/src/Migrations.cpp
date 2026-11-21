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
    // v2: next entries below.
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
