#pragma once

#include "PeerID.hpp"
#include "peyvand/packet.pb.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/*
  ReliableTransport -- Selective Repeat over UDP + persistence (Stage 2).

  DO NUMBER (yahi poori cheez ki buniyaad):
    seq    = window token, 0..7, mod 8 -- sirf is session ki sliding-window ARQ.
    offset = absolute (uint64), kabhi wrap nahi -- disk par rehta, resume + duplicate.

  Features: sliding window, per-packet ACK, selective retransmit, reorder buffer,
  in-order delivery, duplicate detection, adaptive RTO, aur SQLite persistence
  (crash/restart ke baad delivery exact agle offset se resume).

  Persistence optional: Store ke callbacks null hon to pure in-memory chalta.
*/
class ReliableTransport {
  public:
    static constexpr int SEQUENCE_SPACE = 8;              // seq 0..7
    static constexpr int WINDOW_SIZE = SEQUENCE_SPACE / 2; // 4  (window <= space/2 -> wrap-safe)
    static constexpr long START_TIMEOUT_MS = 500;
    static constexpr int RESUME_RETRIES = 5;              // Resume kho jaye to kitni dafa dobara

    // wire/app I/O ka boundary (Peer yeh deta hai)
    using SendData = std::function<void(const PeerID&, const peyvand::Data&)>;
    using SendAck = std::function<void(const PeerID&, const peyvand::Ack&)>;
    using SendResume = std::function<void(const PeerID&, const peyvand::Resume&)>;
    using Deliver = std::function<void(const PeerID&, const std::string&)>;

    // durable store (asli mein SQLite; test mein in-memory). Har callback ek chhota DB op.
    struct Store {
        std::function<uint64_t(const PeerID&)> reserveSendOffset;                   // agla offset do (persist)
        std::function<void(const PeerID&, uint64_t, const std::string&)> putOutbox; // {offset,payload} save
        std::function<void(const PeerID&, uint64_t)> dropOutboxUpTo;                // offset <= arg hata do
        std::function<std::vector<std::pair<uint64_t, std::string>>(const PeerID&, uint64_t)>
            loadOutboxAbove;                                                        // offset > arg (resend)
        std::function<uint64_t(const PeerID&)> loadDelivered;                       // delivered_offset load
        std::function<void(const PeerID&, uint64_t)> saveDelivered;                 // delivered_offset save
    };

    ReliableTransport(SendData sendData, SendAck sendAck, SendResume sendResume, Deliver deliver,
                      Store store = {});

    void send(const PeerID& to, const std::string& payload); // app -> queue + bhejo

    void onData(const PeerID& from, const peyvand::Data& data);       // DATA aaya
    void onAck(const PeerID& from, const peyvand::Ack& ack);          // ACK aaya
    void onResume(const PeerID& from, const peyvand::Resume& resume); // RESUME aaya

    void onPeerEstablished(const PeerID& peer); // (re)connect
    void onPeerLost(const PeerID& peer);        // leave/timeout -> park
    void tick();                                // periodic (har ~100ms)

  private:
    struct Connection {
        bool active = false; // established hai? (park par false)

        // ---- SEND side ----
        std::deque<std::pair<uint64_t, std::string>> outbox; // window ka intezaar karte {offset,payload}
        int sendBase = 0;           // window ka pehla un-acked seq
        int nextSeq = 0;            // agla naya seq
        std::string slotPayload[SEQUENCE_SPACE]; // slot ka payload   (resend ke liye)
        uint64_t slotOffset[SEQUENCE_SPACE] = {0}; // slot ka absolute offset
        bool slotAcked[SEQUENCE_SPACE] = {false};
        bool slotResent[SEQUENCE_SPACE] = {false}; // RTT naapna hai ya nahi (Karn)
        long slotSentAt[SEQUENCE_SPACE] = {0};     // kab bheja (timeout)

        // ---- RECEIVE side ----
        int recvBase = 0;                        // agla expected seq
        bool recvHas[SEQUENCE_SPACE] = {false};  // slot bhara?
        std::string recvPayload[SEQUENCE_SPACE]; // out-of-order buffer
        uint64_t recvOffset[SEQUENCE_SPACE] = {0};

        // ---- durable cursor ----
        uint64_t deliveredOffset = 0; // is peer SE ab tak in-order delivered

        // ---- resume state ----
        bool resumeDone = false;
        uint64_t lastResumedD = 0;
        int resumeRetriesLeft = 0;

        // ---- adaptive timeout ----
        double avgRtt = 300;
        double timeout = START_TIMEOUT_MS;
    };

    Connection& conn(const PeerID& id);
    void pump(const PeerID& to, Connection& c);         // window bharo + bhejo
    void retransmit(const PeerID& to, Connection& c);   // timed-out un-acked dobara
    void emitData(const PeerID& to, Connection& c, int seq); // slot se Data banao + bhejo
    void sendResume(const PeerID& to, Connection& c);   // Resume{deliveredOffset} bhejo

    void resetSendWindow(Connection& c);
    void resetRecvWindow(Connection& c);
    void reloadOutbox(const PeerID& peer, Connection& c, uint64_t aboveOffset);

    static long nowMs();
    static int dist(int from, int to); // wrap-safe aage ka faasla

    std::unordered_map<PeerID, Connection> conns_;
    SendData sendData_;
    SendAck sendAck_;
    SendResume sendResume_;
    Deliver deliver_;
    Store store_;
};
