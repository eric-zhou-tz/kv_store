# Devlog

## 2026-04-21 — GoogleTest Phase 2 Persistence Tests

What I did:
- Replaced the assert-based test executable with GoogleTest
- Added Makefile targets for normal, verbose, and bounded stress test runs
- Organized tests into unit, integration, stress, and reusable helper files
- Covered KV store behavior, WAL replay, binary corruption cases, snapshot load/save, and recovery flows
- Added a bootstrap script for vendoring GoogleTest into `external/googletest`

Challenges:
- Keeping GoogleTest integration Make-friendly without migrating the project to CMake
- Avoiding `src/main.cpp` in test binaries to prevent duplicate entry points
- Testing malformed WAL and snapshot inputs without polluting the repository
- Keeping stress tests useful while still fast enough for local development

Key insights:
- Path-injected WAL and snapshot objects made persistence tests straightforward
- Temp directories are enough for isolated recovery tests; no mocks were needed
- Corruption tests are most useful when they assert both behavior and preservation of prior valid state
- Snapshot parsing needed the same bounded-allocation mindset already present in WAL replay

Next steps:
- Consider adding checksum validation to WAL records
- Add coverage reporting only if it stays lightweight
- Keep future persistence tests close to concrete recovery behavior

---

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
