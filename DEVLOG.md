# Devlog

## 2026-04-21 — Persisted Baseline Benchmark Suite

What I did:
- Added a dedicated benchmark harness under `bench/`
- Implemented write, read, mixed, recovery, and snapshot benchmarks
- Captured throughput, elapsed time, average latency, p50, p95, and p99 latency
- Reported WAL and snapshot artifact sizes where relevant
- Added a lightweight operation-count argument to `./benchmark`
- Added `make benchmark` and `make run_benchmark` without changing the main executable or test targets
- Added `KVStore::SaveSnapshot()` so benchmarks can time explicit snapshot creation through the store API
- Documented the captured persisted-baseline results in `benchmark.md`

Challenges:
- Keeping the benchmark honest by measuring the current WAL + snapshot path instead of inventing a separate in-memory baseline
- Timing snapshot creation without reaching into private store internals
- Making recovery measurements reflect the application startup path: load snapshot first, then replay the WAL tail
- Keeping output structured enough for documentation while avoiding a heavyweight benchmark framework

Key insights:
- Write latency reflects both WAL flush cost and automatic snapshot work, so percentile reporting is more useful than averages alone
- Read throughput is much higher because steady-state reads stay on the in-memory map and do not touch persistence files
- Separating mixed read and write latency makes the overall mixed workload easier to interpret
- Benchmark results are only useful if future Phase 3 comparisons preserve workload shape, seed, value size, compiler settings, and machine context

Next steps:
- Add optional CSV or JSON output if benchmark results start getting tracked over time
- Consider a release-build benchmark target with explicit optimization flags
- Add automatic environment metadata capture for compiler, OS, CPU, and git commit
- Keep benchmarks single-threaded until the project reaches the concurrency phase

---

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
