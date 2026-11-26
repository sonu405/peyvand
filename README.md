# Projects

## List Presets
cmake --list-presets

## Configure Debug
cmake --preset debug

## Build Debug
cmake --build --preset debug


./build/debug/apps/peer/peyvand_peer --data-dir build/data/peer_1 --port 9001


# For Bootstrapping + PEX 
We need to store three tiers of peer knowledge

Tier	      Where	Meaning
Candidate	  memory (and optionally disk)	  An address learned from bootstrap or PEX, never handshaken. Unverified.
Known	      SQLite	                      A peer you completed a handshake with at least once. The persistent address book.
Established	  memory only (sessions_)	      A live session right now. Gone on restart.

A peer moves candidate → known on its first successful handshake, and known → established on every new session.

# Handshakes

We are doing 2 way for now. [TODO]: later 3 way

Why 3 way and not 2 way.

With 2 way:
A --SYN--> B      B: "A is at this endpoint" → Known + Established
A <-SYN_ACK- B

- Identity hijack. An attacker sends a SYN with a real peer's sender_id from its
own endpoint. With insert_or_assign, B overwrites that peer's endpoint.

- **Poisoned address book**: Known is persisted and later shared via PEX, so a
forged entry gets saved and gossiped to others.

A --SYN-------------> B    B: half-open entry (id, endpoint, challenge), NOT Known
A <-SYN_ACK(challenge)- B
A --ACK(echo)--------> B    B: challenge matches → Known + Established

B commits only after the peer proves it receives at that address. A attacker who
forges the source address never sees the SYN_ACK, so it can't echo the challenge

The challenge is what does the work. Without an unguessable value in the SYN_ACK
that the ACK must echo, the third packet proves nothing, because an attacker can
forge it as easily as the SYN. So 3-way without a nonce is mostly ceremony.

Message	Purpose
SYN	"I'm peer X, let's talk." Carries nothing beyond the packet's sender_id, for now.
SYN_ACK	Reply carrying B's identity. Name it differently from the spec's transport ACK so the two layers don't blur.
RESET	"I don't know you, re-handshake." Sent in reply to any non-SYN packet from a non-established sender.
LEAVE	Graceful end of session.

# Bootstrap file Fomrat
```
# peyvand bootstrap list
# one entry per line: [peer_id@]host:port

127.0.0.1:9001
192.168.1.20:9000
[::1]:9002
seed.example.org:9000
a3f9c1d2-7b4e-4c1a-9f20-5d8e1b6a0c33@10.0.0.5:9000   # pinned identity
```

# PEX
Two triggers: 
1.After a handshake completes, in both the SYN and SYN_ACK handlers: sendPacket(from, MESSAGE_TYPE_PEX_REQUEST, peyvand::PexRequest{});

2. Periodically, a coroutine like heartBeatsLoop with kPexInterval, sending a request to every established peer.


# NOTES
1. If we get a packet from a user with the same endpoint and peer_id but a different session_id, we don't cater its requests so that its forced to restart. 

WE can optionally send a reset.

# Selective Repeat
Scheme	            In flight   	On a loss
Stop-and-wait	    1	            Resend that packet. The link sits idle while waiting.
Go-Back-N	        up to N	        Resend the lost packet and everything after it, even packets that arrived.
Selective Repeat	up to N	        Resend only the lost packet.

Drawback of Selective Repeat: receiver must buffer out of order packets and sender much traack each packet individually.

For implementation, both the sender and receiver need to keep a window of sequence numbers.

### Sender state: 
- `base`: the oldest unacknowledged sequence number
- `next_seq`: the next number to assign
- For each packet in `[base, base + W)`: its payload, send time, retry count, and an acked flag

The sender may transmit while next_seq < base + W. When the window is full, new messages wait in a queue.


### Receiver State
- `rcv_base`: the next sequence number it expects to be delivered
- A buffer for packets in `[rcv_base, rcv_base + W)` that arrived early

### Simple Rules:
1. The sender transmits packet `n` and starts a timer for that packet.
2. The receiver gets packet n:
    - If n is inside the window, store it and send ACK(n).
    - If n < rcv_base (a duplicate of something already delivered), drop it but still send ACK(n), because the first ACK may have been the lost one.
    - If n is beyond the window, drop it.
    - While the buffer holds rcv_base, deliver it to the application and advance rcv_base. This is how a gap closing releases a run of buffered packets in order.

3. The sender gets ACK(n): mark n acknowledged. If n == base, slide base forward to the next unacknowledged packet.
4. If a packet's timer expires, retransmit only that packet and restart its timer. After a retry limit, report failure upward.


### Implementation Details
Details that matter in a real implementation
- ACK format. Per-packet ACKs are the textbook form. A common refinement is a
cumulative ACK plus a bitmap: "I have everything up to $a$, and also these
packets after it". One ACK can then cover many packets, and a lost ACK is
covered by the next one.

- Timers. Don't create one timer per packet. Keep one timer per peer and flow,
aimed at the earliest deadline.

- Timeout value. Start with a fixed value, then move to the RFC 6298 estimator:
$$\text{RTO} = \text{SRTT} + 4\,\text{RTTVAR}$$

with exponential backoff on each timeout, and no RTT samples from retransmitted packets.

- Flow control. The receiver's window size acts as flow control, since a slow
receiver simply stops sliding. Congestion control is out of scope for your
project.

- Bounded buffers. Cap both the send queue and the reorder buffer, and fail the

send when the queue is full.
- Session lifetime. Tie sequence spaces to a session (reset on each handshake),
so a restarted peer's sequence 1 isn't mistaken for a duplicate.
