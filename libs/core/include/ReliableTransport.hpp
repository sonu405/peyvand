#pragma once

#include "PeerID.hpp"
#include "peyvand/packet.pb.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

// Selective Repeat over UDP
class ReliableTransport {
  public:
    // Selective Repeat rule: WINDOW <= SEQUENCE_SPACE / 2  (wraparound-safe)
    static constexpr int SEQUENCE_SPACE = 8;
    static constexpr int WINDOW_SIZE = SEQUENCE_SPACE / 2;
    static constexpr long START_TIMEOUT_MS = 500;

    // Peer yeh teen cheezein deta hai:
    using SendData = std::function<void(const PeerID& to, const peyvand::Data& data)>;
    using SendAck = std::function<void(const PeerID& to, const peyvand::Ack& ack)>;
    using Deliver = std::function<void(const PeerID& from, const std::string& payload)>;

    ReliableTransport(SendData sendData, SendAck sendAck, Deliver deliver);

    void send(const PeerID& to, const std::string& payload);

    // dispatcher -> incoming
    void onData(const PeerID& from, const peyvand::Data& data);
    void onAck(const PeerID& from, const peyvand::Ack& ack);

    void tick();

  private:
    // ---- per-peer state ----
    struct Connection {
        // send side
        std::deque<std::string> outbox;
        int sendBase = 0;
        int nextSequenceNumber = 0;

        std::string savedPayloads[SEQUENCE_SPACE];
        bool acknowledged[SEQUENCE_SPACE] = {false};
        bool packetWasRetransmitted[SEQUENCE_SPACE] = {false};
        long lastSentTime[SEQUENCE_SPACE] = {0};

        // receive side
        int receiveBase = 0;
        bool packetReceived[SEQUENCE_SPACE] = {false};
        std::string receivedPacketData[SEQUENCE_SPACE];

        // adaptive timeout
        double averageRTT = 300;
        double currentTimeout = START_TIMEOUT_MS;
    };

    Connection& conn(const PeerID& id);
    void pump(const PeerID& to, Connection& c);
    void retransmit(const PeerID& to, Connection& c);

    static long nowMs();
    static int dist(int from, int to);

    std::unordered_map<PeerID, Connection> conns_;
    SendData sendData_;
    SendAck sendAck_;
    Deliver deliver_;
};
