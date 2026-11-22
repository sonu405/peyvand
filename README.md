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