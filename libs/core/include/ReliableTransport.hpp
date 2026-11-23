#pragma once

#include "PeerID.hpp"
#include "peyvand/packet.pb.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

/*
  ReliableTransport -- Selective Repeat over UDP (Stage 2).

  Core logic (window, acked[], slide, RTT, reorder buffer, in-order deliver) is
  EXACTLY the standalone version -- sirf I/O boundary badla:
    - text packets  -> protobuf Data / Ack
    - raw socket    -> callbacks (Peer deta hai: kaise bhejna, kahan deliver karna)
    - ek global     -> ek Connection PER PeerID (map)

  Dispatcher MESSAGE_TYPE_DATA/ACK ko onData()/onAck() mein bhejta hai.
  Ek coroutine periodically tick() call karti hai (timeouts/retransmit).
*/
class ReliableTransport {
  public:
    // Selective Repeat rule: WINDOW <= SEQUENCE_SPACE / 2  (wraparound-safe)
    static constexpr int SEQUENCE_SPACE = 8;
    static constexpr int WINDOW_SIZE = SEQUENCE_SPACE / 2;
    static constexpr long START_TIMEOUT_MS = 500;

    // Peer yeh teen cheezein deta hai:
    using SendData = std::function<void(const PeerID& to, const peyvand::Data& data)>; // DATA bhejo
    using SendAck = std::function<void(const PeerID& to, const peyvand::Ack& ack)>;    // ACK bhejo
    using Deliver = std::function<void(const PeerID& from, const std::string& payload)>; // in-order deliver

    ReliableTransport(SendData sendData, SendAck sendAck, Deliver deliver);

    // app -> reliable send (message ko is peer ke liye queue karo)
    void send(const PeerID& to, const std::string& payload);

    // dispatcher -> incoming
    void onData(const PeerID& from, const peyvand::Data& data);
    void onAck(const PeerID& from, const peyvand::Ack& ack);

    // periodic (jaise har 100ms): timed-out packets resend + window pump
    void tick();

  private:
    // ---- per-peer state (wahi fields jo standalone Connection mein the) ----
    struct Connection {
        // send side
        std::deque<std::string> outbox; // app messages jo window mein jaane ka intezaar kar rahe
        int sendBase = 0;               // window ka pehla un-acked
        int nextSequenceNumber = 0;     // agla naya seq

        std::string savedPayloads[SEQUENCE_SPACE];        // har slot ka payload (resend ke liye)
        bool acknowledged[SEQUENCE_SPACE] = {false};      // kaunsा acked
        bool packetWasRetransmitted[SEQUENCE_SPACE] = {false}; // RTT naapne/na naapne ke liye
        long lastSentTime[SEQUENCE_SPACE] = {0};          // har slot kab bheja (timeout)

        // receive side
        int receiveBase = 0;                              // receive window ka start
        bool packetReceived[SEQUENCE_SPACE] = {false};    // kaunsा mila
        std::string receivedPacketData[SEQUENCE_SPACE];   // out-of-order buffer

        // adaptive timeout
        double averageRTT = 300;
        double currentTimeout = START_TIMEOUT_MS;
    };

    Connection& conn(const PeerID& id);
    void pump(const PeerID& to, Connection& c);       // window bharo outbox se + bhejo
    void retransmit(const PeerID& to, Connection& c); // timeout pe un-acked dobara

    static long nowMs();
    static int dist(int from, int to); // wrap-safe faasla

    std::unordered_map<PeerID, Connection> conns_;
    SendData sendData_;
    SendAck sendAck_;
    Deliver deliver_;
};
