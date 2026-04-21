# Devlog

Engineering notes for major project milestones. This file captures intent,
tradeoffs, and follow-up work that does not belong in the user-facing README.

## 2026-04-21 - Documentation Presentation Refresh

Summary:
- Reworked the README as a recruiter-facing project overview.
- Updated the design notes to reflect the completed Phase 2 persistence model.
- Added a demo GIF placeholder and clarified how future benchmark results
  should be recorded.

Notes:
- Kept benchmark numbers tied to the checked-in baseline report rather than
  presenting them as universal performance claims.
- Kept the repository technical and direct by avoiding unsupported badges and
  decorative content.

Next:
- Add a real terminal demo GIF showing writes, restart, and recovery.
- Capture official benchmark environment metadata with the next benchmark run.

---

## 2026-04-21 - Persisted Baseline Benchmark Suite

Summary:
- Added a dedicated benchmark harness under `bench/`.
- Implemented write, read, mixed, recovery, and snapshot workloads.
- Reported throughput, elapsed time, average latency, p50, p95, and p99.
- Reported WAL and snapshot artifact sizes where relevant.
- Added `make benchmark`, `make run_benchmark`, and an optional operation-count
  argument for `./benchmark`.
- Added `KVStore::SaveSnapshot()` so benchmarks can time explicit checkpoint
  creation through the store API.
- Documented baseline methodology and results in `benchmark.md`.

Design notes:
- The benchmark uses the current WAL plus snapshot path instead of a separate
  in-memory-only baseline.
- Recovery timing follows the application startup path: load snapshot first,
  then replay the WAL tail.
- Snapshot timing is exposed through the store API instead of reaching into
  private state.

Next:
- Add optional CSV or JSON benchmark output if results are tracked over time.
- Add automatic environment metadata capture for compiler, OS, CPU, and commit.
- Keep benchmarks single-threaded until the concurrency phase.

---

## 2026-04-21 - GoogleTest Phase 2 Persistence Tests

Summary:
- Replaced the assert-based test executable with GoogleTest.
- Organized tests into unit, integration, stress, and helper files.
- Added Makefile targets for normal, verbose, and bounded stress test runs.
- Covered core KV behavior, WAL replay, binary corruption cases, snapshot
  load/save, and recovery flows.
- Added a bootstrap script for vendoring GoogleTest into
  `external/googletest`.

Design notes:
- Path-injected WAL and snapshot objects keep persistence tests isolated.
- Temporary directories are sufficient for recovery tests; mocks were not
  needed.
- Corruption tests assert both the recovery result and preservation of prior
  valid state.

Next:
- Consider WAL record checksums.
- Add coverage reporting only if it stays lightweight.
- Keep future persistence tests close to concrete recovery behavior.

---

## 2026-04-21 - WAL Persistence and Recovery

Summary:
- Implemented append-only WAL persistence for `SET` and `DELETE`.
- Added a deterministic binary record format.
- Added startup crash recovery through WAL replay.
- Integrated snapshot-aware replay using a covered WAL byte offset.

Design notes:
- Binary formats must be strictly framed; small inconsistencies can break
  replay.
- EOF handling matters for crash safety because a process may stop after a
  partial final write.
- Persistence remains decoupled from request handling.

Next:
- Add record validation with checksums.
- Improve replay performance.
- Add WAL segmentation and garbage collection after snapshot coverage.

---

## 2026-04-18 - Initial In-Memory Store

Summary:
- Built the in-memory key-value store using `std::unordered_map`.
- Implemented the CLI interface.
- Separated parser, server, and store layers.

Design notes:
- Early module boundaries made it easier to add persistence without rewriting
  the CLI or parser.
- A small, stable storage API simplified the later durability work.

Next:
- Keep storage semantics simple while adding persistence and recovery.
