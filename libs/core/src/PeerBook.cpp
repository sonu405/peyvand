#include "PeerBook.hpp"
#include <algorithm>

bool PeerBook::Book::addCandidate(const Endpoint& ep, std::optional<PeerID> id) {
    // have we established conection with this candidate already?
    if (isEstablished(ep))
        return false;

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
    established_.insert_or_assign(id, Established{ep, now});
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
        if (c.nextRetry > now)
            continue;
        if (isEstablished(c.endpoint))
            continue; // simultaneous-open guard
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

    if (it == candidates_.end())
        return false;

    // SynAck sender has different id than the candiddate that was sent Syn.
    if (it->peerID && *it->peerID != id)
        return false;

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

std::unordered_map<PeerID, PeerBook::Established>& PeerBook::Book::established() {
    return established_;
}