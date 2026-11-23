#include "ReliableTransport.hpp"

#include <chrono>
#include <utility>

ReliableTransport::ReliableTransport(SendData sendData, SendAck sendAck, Deliver deliver) :
    sendData_(std::move(sendData)), sendAck_(std::move(sendAck)), deliver_(std::move(deliver)) {}

long ReliableTransport::nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// base se seq tak wrap-safe faasla (== getSequenceDistance)
int ReliableTransport::dist(int from, int to) {
    return (to - from + SEQUENCE_SPACE) % SEQUENCE_SPACE;
}

ReliableTransport::Connection& ReliableTransport::conn(const PeerID& id) {
    return conns_[id]; // nahi hai to default-construct ho jata
}

// --- app -> message queue karo, phir bhejne ki koshish ---
void ReliableTransport::send(const PeerID& to, const std::string& payload) {
    Connection& c = conn(to);
    c.outbox.push_back(payload);
    pump(to, c);
}

// --- window mein jagah + outbox mein message -> naya DATA bhejo ---
// (== sendNewAndRetryPackets ka "naye bhejo" wala hissa)
void ReliableTransport::pump(const PeerID& to, Connection& c) {
    while (dist(c.sendBase, c.nextSequenceNumber) < WINDOW_SIZE && !c.outbox.empty()) {
        int seq = c.nextSequenceNumber;
        std::string payload = c.outbox.front();
        c.outbox.pop_front();

        c.savedPayloads[seq] = payload;          // slot mein yaad (resend ke liye)
        c.acknowledged[seq] = false;
        c.packetWasRetransmitted[seq] = false;
        c.lastSentTime[seq] = nowMs();

        peyvand::Data data;
        data.set_seq(static_cast<uint32_t>(seq));
        data.set_payload(payload);
        sendData_(to, data);                     // network pe daalo (ya kho jaye)

        c.nextSequenceNumber = (c.nextSequenceNumber + 1) % SEQUENCE_SPACE;
    }
}

// --- timeout pe sirf un-acked outstanding dobara bhejo (SELECTIVE) ---
// (== sendNewAndRetryPackets ka "retry" wala hissa)
void ReliableTransport::retransmit(const PeerID& to, Connection& c) {
    int packetsInWindow = dist(c.sendBase, c.nextSequenceNumber);
    long now = nowMs();

    for (int offset = 0; offset < packetsInWindow; offset++) {
        int seq = (c.sendBase + offset) % SEQUENCE_SPACE;

        if (!c.acknowledged[seq]) {
            long waitingTime = now - c.lastSentTime[seq];
            if (waitingTime >= static_cast<long>(c.currentTimeout)) {
                peyvand::Data data;
                data.set_seq(static_cast<uint32_t>(seq));
                data.set_payload(c.savedPayloads[seq]);
                sendData_(to, data);                       // *** sirf yahi dobara ***
                c.lastSentTime[seq] = now;
                c.packetWasRetransmitted[seq] = true;      // RTT mat naapna is ka
            }
        }
    }
}

// --- ACK aaya -> mark + window slide + RTT update  (== handleAcknowledgement) ---
void ReliableTransport::onAck(const PeerID& from, const peyvand::Ack& ack) {
    Connection& c = conn(from);
    int acknowledgedSequence = static_cast<int>(ack.seq());

    int packetsInWindow = dist(c.sendBase, c.nextSequenceNumber);
    int distanceFromBase = dist(c.sendBase, acknowledgedSequence);

    if (distanceFromBase < packetsInWindow) { // window ke andar valid ACK
        if (!c.acknowledged[acknowledgedSequence]) {
            c.acknowledged[acknowledgedSequence] = true;

            if (!c.packetWasRetransmitted[acknowledgedSequence]) { // Karn: sirf first-try se RTT
                double measuredRTT = static_cast<double>(nowMs() - c.lastSentTime[acknowledgedSequence]);
                c.averageRTT = 0.9 * c.averageRTT + 0.1 * measuredRTT;
                c.currentTimeout = 2.0 * c.averageRTT;
                if (c.currentTimeout < 100) c.currentTimeout = 100;
                if (c.currentTimeout > 2000) c.currentTimeout = 2000;
            }
        }

        while (c.sendBase != c.nextSequenceNumber && c.acknowledged[c.sendBase]) { // slide
            c.acknowledged[c.sendBase] = false;
            c.packetWasRetransmitted[c.sendBase] = false;
            c.sendBase = (c.sendBase + 1) % SEQUENCE_SPACE;
        }

        pump(from, c); // window mein jagah bani -> outbox se aur bhejo
    }
    // warna: purana/stale ACK -> ignore
}

// --- DATA aaya -> ACK bhejo + buffer + in-order deliver  (== handleDataPacket) ---
void ReliableTransport::onData(const PeerID& from, const peyvand::Data& data) {
    Connection& c = conn(from);
    int seq = static_cast<int>(data.seq());

    peyvand::Ack ack; // ACK hamesha bhejo (chahe duplicate ho)
    ack.set_seq(static_cast<uint32_t>(seq));
    sendAck_(from, ack);

    if (dist(c.receiveBase, seq) < WINDOW_SIZE) { // receive window ke andar
        if (!c.packetReceived[seq]) {
            c.packetReceived[seq] = true;
            c.receivedPacketData[seq] = data.payload();
        }
        while (c.packetReceived[c.receiveBase]) { // in-order deliver + slide
            deliver_(from, c.receivedPacketData[c.receiveBase]);
            c.packetReceived[c.receiveBase] = false;
            c.receiveBase = (c.receiveBase + 1) % SEQUENCE_SPACE;
        }
    }
    // warna: window-bahar duplicate -> sirf ACK (upar bhej diya)
}

// --- periodic: har connection ka timeout-check + pump ---
void ReliableTransport::tick() {
    for (auto& [id, c] : conns_) {
        retransmit(id, c);
        pump(id, c);
    }
}
