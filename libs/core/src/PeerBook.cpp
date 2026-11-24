#include "PeerBook.hpp"
#include "PeerID.hpp"
#include "spdlog/spdlog.h"
#include <SQLiteCpp/SQLiteCpp.h>
#include <algorithm>
#include <random>

bool PeerBook::Book::addCandidate(const Endpoint& ep, std::optional<PeerID> id) {
    // have we established conection with this candidate already?
    if (isEstablished(ep)) return false;

    // Do we have already the incoming ep on some candidate?
    auto it =
        std::ranges::find_if(candidates_, [&](const Candidate& c) { return c.endpoint == ep; });

    if (it != candidates_.end()) {
        if (!it->peerID && id) {
            // update our stored candidate's ID
            it->peerID = id; // learned an ID for a seed: pin it
        }
        return false; // already a candidate
    }
    candidates_.push_back(Candidate{ep, id, Candidate::State::Idle, {}, 0});
    return true;
}

bool PeerBook::Book::isEstablished(const PeerID& id) const {
    return established_.contains(id);
}

bool PeerBook::Book::isEstablished(const Endpoint& ep) const {
    return std::ranges::any_of(established_,
                               [&](const auto& kv) { return kv.second.endpoint == ep; });
}

// Returns the session if present, so the dispatcher can also check the endpoint.
const PeerBook::Established* PeerBook::Book::session(const PeerID& id) const {
    auto it = established_.find(id);
    return it == established_.end() ? nullptr : &it->second;
}

void PeerBook::Book::updateToEstablished(const PeerID& id, const Endpoint& ep, TimePoint now) {
    static std::random_device rd;
    uint32_t session_id;
    do {
        session_id = rd();
    } while (session_id == 0); // 0 = "no session"

    established_.insert_or_assign(id, Established{ep, now, session_id});
}

void PeerBook::Book::updateToKnown(const PeerID& id, const Endpoint& ep, TimePoint now) {
    known_.insert_or_assign(id, Known{ep, now, 0});
}

std::vector<PeerBook::Endpoint> PeerBook::Book::dueCandidates(TimePoint now, Duration retry,
                                                              int maxAttempts) {

    // drop exhausted candidates
    std::erase_if(candidates_, [&](const Candidate& c) {
        return c.attempts >= maxAttempts && c.nextRetry <= now;
    });

    std::vector<Endpoint> out;
    for (auto& c : candidates_) {
        if (c.nextRetry > now) continue;
        if (isEstablished(c.endpoint)) continue; // simultaneous-open guard
        c.state = Candidate::State::SynSent;
        ++c.attempts;
        c.nextRetry = now + retry;
        out.push_back(c.endpoint);
    }

    return out;
}

// Role: is this SYN_ACK a reply to a SYN I actually sent? If so, retire that candidate

// Removes the SynSent candidate for this endpoint. Returns false if none exists,
// or if the candidate had a pinned PeerID that doesn't match.
bool PeerBook::Book::completeCandidate(const Endpoint& ep, const PeerID& id) {

    // We got SYN_ACK. Find candidate that sent it. That candidate must be in SynSent state.
    auto it = std::ranges::find_if(candidates_, [&](const Candidate& c) {
        return c.endpoint == ep && c.state == Candidate::State::SynSent;
    });

    if (it == candidates_.end()) return false;

    // SynAck sender has different id than the candiddate that was sent Syn.
    if (it->peerID && *it->peerID != id) return false;

    // valid candidate. No more a candidate.
    candidates_.erase(it);
    return true;
}

void PeerBook::Book::removeCandidate(const Endpoint& ep) {
    std::erase_if(candidates_, [&](const Candidate& c) { return c.endpoint == ep; });
}

void PeerBook::Book::removeEstablished(const PeerID& id) {
    established_.erase(id);
}

std::vector<PeerID> PeerBook::Book::expireEstablished(Duration timeout) {
    const auto now = Clock::now(); // one timestamp for the whole sweep
    std::vector<PeerID> dead;

    for (auto it = established_.begin(); it != established_.end();) {
        if (now - it->second.last_seen > timeout) {
            dead.push_back(it->first);

            // increase failcount;
            if (auto k = known_.find(it->first); k != known_.end()) { k->second.fail_count++; }

            it = established_.erase(it); // returns the next valid iterator
        } else {
            ++it;
        }
    }
    return dead;
}

std::unordered_map<PeerID, PeerBook::Established>& PeerBook::Book::established() {
    return established_;
}

void PeerBook::Book::flushKnown(SQLite::Database& db) const {
    const auto steadyNow = Clock::now();
    const auto sysNow = std::chrono::system_clock::now();

    SQLite::Transaction tx(db, SQLite::TransactionBehavior::IMMEDIATE);
    SQLite::Statement upsert(db, "INSERT INTO known_peers (peer_id, address, port, first_seen, "
                                 "last_success, fail_count, source) "
                                 "VALUES (?1, ?2, ?3, ?4, ?4, ?5, 0) "
                                 "ON CONFLICT(peer_id) DO UPDATE SET "
                                 "  address = excluded.address, "
                                 "  port = excluded.port, "
                                 "  last_success = excluded.last_success, "
                                 "  fail_count = excluded.fail_count");

    for (const auto& [id, k] : known_) {
        const auto unixSecs = std::chrono::duration_cast<std::chrono::seconds>(
                                  (sysNow - (steadyNow - k.last_success)).time_since_epoch())
                                  .count();

        upsert.bind(1, id.bytes().data(), static_cast<int>(id.bytes().size()));
        upsert.bind(2, k.endpoint.address().to_string());
        upsert.bind(3, static_cast<int>(k.endpoint.port()));
        upsert.bind(4, static_cast<int64_t>(unixSecs));
        upsert.bind(5, k.fail_count);
        upsert.exec();
        upsert.reset();
        upsert.clearBindings();
    }
    tx.commit();
}

// PeerBook.cpp
size_t PeerBook::Book::loadKnownAsCandidates(SQLite::Database& db, size_t maxRows) {
    SQLite::Statement q(db, "SELECT peer_id, address, port FROM known_peers "
                            "ORDER BY fail_count ASC, last_success DESC LIMIT ?1");
    q.bind(1, static_cast<int64_t>(maxRows));

    size_t added = 0;
    while (q.executeStep()) {
        const auto idCol = q.getColumn(0);
        const std::span<const uint8_t> blob{static_cast<const uint8_t*>(idCol.getBlob()),
                                            static_cast<std::size_t>(idCol.getBytes())};
        const auto id = PeerID::from_bytes(blob);
        if (!id) {
            spdlog::warn("known_peers: skipping row with bad peer_id");
            continue;
        }

        asio::error_code ec;
        const auto addr = asio::ip::make_address_v4(q.getColumn(1).getString(), ec);
        const int port = q.getColumn(2).getInt();
        if (ec || addr.is_unspecified() || port < 1 || port > 65535) {
            spdlog::warn("known_peers: skipping row with bad address");
            continue;
        }

        if (addCandidate(Endpoint(addr, static_cast<uint16_t>(port)), *id)) ++added;
    }
    return added;
}

bool PeerBook::Book::touch(const PeerID& id, const Endpoint& ep, const uint32_t session_id) {
    auto it = established_.find(id);
    if (it == established_.end() || it->second.endpoint != ep) return false;
    it->second.last_seen = Clock::now();

    // session_id == 0 -- not allowed or a knwon peer with different session id.
    // It would be forced to be dropped.
    // WE can optionally send it a RESET.
    if (session_id == 0 || session_id != it->second.session_id) return false;

    return true;
}