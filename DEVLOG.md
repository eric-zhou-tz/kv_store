# Devlog

## 2026-04-21 — WAL Implementation

What I did:
- Implemented WAL append and replay logic
- Designed a deterministic binary record format
- Added crash recovery via replay on startup

Challenges:
- Replay failures due to malformed or partial records
- Ensuring deterministic parsing across writes and reads
- Handling incomplete trailing writes safely

Key insights:
- Binary formats must be strictly defined; small inconsistencies break replay
- EOF handling is critical for crash safety
- Persistence logic should remain decoupled from request handling

Next steps:
- Add record validation (checksums or length verification)
- Improve replay performance
- Integrate snapshot + WAL recovery flow

---

## 2026-04-18 — Initial KV Store

What I did:
- Built in-memory KV store using a hash map
- Implemented CLI interface for interaction
- Structured code into parser, server, and store layers

Challenges:
- Choosing boundaries between components early on
- Keeping the design simple without blocking future persistence

Key insights:
- Early separation of concerns makes adding persistence significantly easier
- A minimal, stable API simplifies introducing durability later

Next steps:
- Implement persistence layer (WAL)