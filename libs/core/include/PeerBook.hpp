#pragma once
#include "PeerID.hpp"
#include "spdlog/spdlog.h"
#include <asio.hpp>
#include <asio/detail/chrono.hpp>
#include <chrono>
#include <optional>
#include <vector>

namespace PeerBook {

using asio::chrono::duration;
using Endpoint = asio::ip::udp::endpoint;
using std::vector;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

struct Candidate {
    Endpoint endpoint;
    std::optional<PeerID> peerID;
    enum class State { Idle, SynSent } state = State::Idle;
    std::chrono::steady_clock::time_point nextRetry{};
    int attempts;
};

struct Known {
    Endpoint endpoint;
    TimePoint last_success;
    int fail_count;
};

struct Established {
    Endpoint endpoint;
    TimePoint last_seen;
};

class Book {
  public:
    bool addCandidate(const Endpoint& ep, std::optional<PeerID> id = std::nullopt);
    bool isEstablished(const PeerID& id) const;
    bool isEstablished(const Endpoint& ep) const;

    // Returns the session if present, so the dispatcher can also check the endpoint.
    const Established* session(const PeerID& id) const;

    void updateToEstablished(const PeerID& id, const Endpoint& ep, TimePoint now);

    void updateToKnown(const PeerID& id, const Endpoint& ep, TimePoint now);
    std::vector<Endpoint> dueCandidates(TimePoint now, Duration retry, int maxAttempts);

    bool completeCandidate(const Endpoint& ep, const PeerID& id);
    void removeCandidate(const Endpoint& ep);
    void removeEstablished(const PeerID& id);

    void flushKnown(SQLite::Database& db) const;
    size_t loadKnownAsCandidates(SQLite::Database& db, size_t maxRows);

    // Getters
    std::unordered_map<PeerID, Established>& established();

  private:
    vector<Candidate> candidates_;
    std::unordered_map<PeerID, Known> known_;
    std::unordered_map<PeerID, Established> established_;
};

} // namespace PeerBook
