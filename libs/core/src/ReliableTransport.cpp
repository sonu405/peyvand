#include "ReliableTransport.hpp"

#include <chrono>
#include <utility>

ReliableTransport::ReliableTransport(SendData sendData, SendAck sendAck, SendResume sendResume,
                                     Deliver deliver, Store store) :
    sendData_(std::move(sendData)), sendAck_(std::move(sendAck)),
    sendResume_(std::move(sendResume)), deliver_(std::move(deliver)), store_(std::move(store)) {}

long ReliableTransport::nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// base se seq tak wrap-safe faasla
int ReliableTransport::dist(int from, int to) {
    return (to - from + SEQUENCE_SPACE) % SEQUENCE_SPACE;
}

ReliableTransport::Connection& ReliableTransport::conn(const PeerID& id) {
    return conns_[id]; // nahi hai to fresh ban jata
}

// slot ka Data (seq + offset + payload) banao aur bhejo. pump aur retransmit dono isko use karte.
void ReliableTransport::emitData(const PeerID& to, Connection& c, int seq) {
    peyvand::Data data;
    data.set_seq(static_cast<uint32_t>(seq));
    data.set_offset(c.slotOffset[seq]); // absolute offset -- resume ki jaan
    data.set_payload(c.slotPayload[seq]);
    c.slotSentAt[seq] = nowMs();
    sendData_(to, data);
}

// =============================== SEND ===============================

// app -> message ko offset do, disk par likho, queue karo, bhejne ki koshish
void ReliableTransport::send(const PeerID& to, const std::string& payload) {
    Connection& c = conn(to);

    uint64_t offset;
    if (store_.reserveSendOffset) {
        offset = store_.reserveSendOffset(to);                       // disk: next_send_offset++
        if (store_.putOutbox) store_.putOutbox(to, offset, payload); // disk: un-delivered queue
    } else {
        offset = c.outbox.empty() ? 1 : c.outbox.back().first + 1;   // no-store fallback
    }

    c.outbox.push_back({offset, payload});
    pump(to, c);
}

// window mein jagah hai aur outbox mein message hai -> naya packet bhejo
void ReliableTransport::pump(const PeerID& to, Connection& c) {
    while (dist(c.sendBase, c.nextSeq) < WINDOW_SIZE && !c.outbox.empty()) {
        int seq = c.nextSeq;
        auto [offset, payload] = c.outbox.front();
        c.outbox.pop_front();

        c.slotPayload[seq] = payload; // resend ke liye yaad
        c.slotOffset[seq] = offset;
        c.slotAcked[seq] = false;
        c.slotResent[seq] = false; // pehli koshish

        emitData(to, c, seq);
        c.nextSeq = (c.nextSeq + 1) % SEQUENCE_SPACE;
    }
}

// timeout nikal jaye to SIRF un-acked outstanding dobara (selective)
void ReliableTransport::retransmit(const PeerID& to, Connection& c) {
    int outstanding = dist(c.sendBase, c.nextSeq);
    long now = nowMs();

    for (int i = 0; i < outstanding; i++) {
        int seq = (c.sendBase + i) % SEQUENCE_SPACE;
        if (!c.slotAcked[seq] && now - c.slotSentAt[seq] >= static_cast<long>(c.timeout)) {
            emitData(to, c, seq);
            c.slotResent[seq] = true; // ab iska RTT mat naapna (Karn)
        }
    }
}

// ACK aaya -> mark + window slide + RTT update + durable prune
void ReliableTransport::onAck(const PeerID& from, const peyvand::Ack& ack) {
    Connection& c = conn(from);
    int seq = static_cast<int>(ack.seq());

    // valid ACK? (window ke andar)
    if (dist(c.sendBase, seq) < dist(c.sendBase, c.nextSeq)) {
        if (!c.slotAcked[seq]) {
            c.slotAcked[seq] = true;

            if (!c.slotResent[seq]) { // Karn: sirf first-try se RTT
                double sample = static_cast<double>(nowMs() - c.slotSentAt[seq]);
                c.avgRtt = 0.9 * c.avgRtt + 0.1 * sample;
                c.timeout = 2.0 * c.avgRtt;
                if (c.timeout < 100) c.timeout = 100;
                if (c.timeout > 2000) c.timeout = 2000;
            }
        }

        while (c.sendBase != c.nextSeq && c.slotAcked[c.sendBase]) { // contiguous slide
            c.slotAcked[c.sendBase] = false;
            c.slotResent[c.sendBase] = false;
            c.sendBase = (c.sendBase + 1) % SEQUENCE_SPACE;
        }

        pump(from, c); // jagah bani -> aur bhejo
    }

    // receiver ne bataya kitne tak deliver kiya -> us tak ke disk se hata do
    if (store_.dropOutboxUpTo && ack.delivered_offset() > 0)
        store_.dropOutboxUpTo(from, ack.delivered_offset());
}

// =============================== RECEIVE ===============================

// DATA aaya -> duplicate guard -> buffer -> in-order deliver -> persist -> ACK
void ReliableTransport::onData(const PeerID& from, const peyvand::Data& data) {
    Connection& c = conn(from);
    int seq = static_cast<int>(data.seq());
    uint64_t offset = data.offset();

    // pehle hi deliver ho chuka (is ya pichle session mein)? -> drop, bas ACK
    if (offset != 0 && offset <= c.deliveredOffset) {
        peyvand::Ack ack;
        ack.set_seq(static_cast<uint32_t>(seq));
        ack.set_delivered_offset(c.deliveredOffset);
        sendAck_(from, ack);
        return;
    }

    if (dist(c.recvBase, seq) < WINDOW_SIZE) { // receive window ke andar
        if (!c.recvHas[seq]) {
            c.recvHas[seq] = true;
            c.recvPayload[seq] = data.payload();
            c.recvOffset[seq] = offset;
        }
        while (c.recvHas[c.recvBase]) { // jitne in-order ready hain, deliver
            deliver_(from, c.recvPayload[c.recvBase]);
            c.deliveredOffset = c.recvOffset[c.recvBase];
            if (store_.saveDelivered) store_.saveDelivered(from, c.deliveredOffset);
            c.recvHas[c.recvBase] = false;
            c.recvBase = (c.recvBase + 1) % SEQUENCE_SPACE;
        }
    }

    peyvand::Ack ack; // taaza delivered_offset ke saath ACK
    ack.set_seq(static_cast<uint32_t>(seq));
    ack.set_delivered_offset(c.deliveredOffset);
    sendAck_(from, ack);
}

// =============================== RESUME / LIFECYCLE ===============================

// Resume{deliveredOffset} bhejo -- peer ko batao hum kahan tak le chuke
void ReliableTransport::sendResume(const PeerID& to, Connection& c) {
    peyvand::Resume r;
    r.set_delivered_offset(c.deliveredOffset);
    sendResume_(to, r);
}

// RESUME aaya: peer ne bataya "main D tak deliver kar chuka" -> D+1 se aligned resend.
// (resend sirf yahan, onPeerEstablished mein nahi -- taake D pata ho aur seq0 == D+1 bane.)
void ReliableTransport::onResume(const PeerID& from, const peyvand::Resume& resume) {
    uint64_t D = resume.delivered_offset();
    Connection& c = conn(from);

    if (store_.dropOutboxUpTo && D > 0) store_.dropOutboxUpTo(from, D); // deliver ho chuke hata do

    if (!c.resumeDone) {            // is session ka pehla resume
        resetSendWindow(c);         // fresh seq 0 se
        reloadOutbox(from, c, D);   // disk se offset > D wale load
        pump(from, c);              // resend (seq0 == D+1)
        c.resumeDone = true;
        c.lastResumedD = D;
    } else if (D > c.lastResumedD) { // baad ka higher-D -> sirf prune (window reset nahi)
        c.lastResumedD = D;
    }
    // purana/duplicate Resume -> ignore (idempotent)
}

// (re)connect: window reset + Resume bhejo. Resend onResume karega.
void ReliableTransport::onPeerEstablished(const PeerID& peer) {
    Connection& c = conn(peer);
    c.active = true;

    if (store_.loadDelivered) c.deliveredOffset = store_.loadDelivered(peer); // disk se bahaal

    resetSendWindow(c);
    resetRecvWindow(c);
    c.outbox.clear(); // disk hi sach hai (onResume reload karega)

    c.resumeDone = false;
    c.lastResumedD = 0;
    c.resumeRetriesLeft = RESUME_RETRIES;

    sendResume(peer, c);
}

// leave/timeout: state phenko mat, sirf park (tick skip karega)
void ReliableTransport::onPeerLost(const PeerID& peer) {
    conn(peer).active = false;
}

// periodic: Resume-retry + timed-out resend + pump (sirf active peers)
void ReliableTransport::tick() {
    for (auto& [id, c] : conns_) {
        if (!c.active) continue;

        if (c.resumeRetriesLeft > 0) { // Resume kho sakta -> dobara (onResume dup ignore karta)
            sendResume(id, c);
            c.resumeRetriesLeft--;
        }
        retransmit(id, c);
        pump(id, c);
    }
}

// =============================== helpers ===============================

void ReliableTransport::resetSendWindow(Connection& c) {
    c.sendBase = 0;
    c.nextSeq = 0;
    for (int i = 0; i < SEQUENCE_SPACE; i++) {
        c.slotAcked[i] = false;
        c.slotResent[i] = false;
        c.slotOffset[i] = 0;
        c.slotSentAt[i] = 0;
    }
}

void ReliableTransport::resetRecvWindow(Connection& c) {
    c.recvBase = 0;
    for (int i = 0; i < SEQUENCE_SPACE; i++) {
        c.recvHas[i] = false;
        c.recvOffset[i] = 0;
    }
}

// disk par pare un-delivered (offset > aboveOffset) ko in-memory outbox mein (resend ke liye)
void ReliableTransport::reloadOutbox(const PeerID& peer, Connection& c, uint64_t aboveOffset) {
    if (!store_.loadOutboxAbove) return;
    c.outbox.clear();
    for (auto& [offset, payload] : store_.loadOutboxAbove(peer, aboveOffset))
        c.outbox.push_back({offset, payload});
}
