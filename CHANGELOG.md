# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
### Added
- Comprehensive benchmark suite for the current persisted KV store baseline
- Benchmark workloads for write, read, mixed, recovery, and snapshot timing
- Benchmark latency statistics including average, p50, p95, and p99
- Persistence artifact size reporting for WAL and snapshot files
- Makefile benchmark target via `make benchmark` and `./benchmark`
- Formal `benchmark.md` report with baseline results, methodology, and Phase 3 comparison guidance
- Public `KVStore::SaveSnapshot()` checkpoint API for explicit snapshot timing and operational checkpointing
- GoogleTest-based unit, integration, and stress test suites for Phase 2 persistence
- Makefile targets for `make test`, `make test_verbose`, and `make test_stress`
- Temporary test directory and binary file helpers for isolated persistence tests
- GoogleTest bootstrap script for vendoring into `external/googletest`

### Changed
- Updated README benchmark instructions to point to the dedicated benchmark report
- Ignored the generated benchmark executable as a build artifact
- Replaced the old assert-based test executable with organized GoogleTest files
- Updated README testing instructions for GoogleTest setup and stress tests
- Ignored vendored GoogleTest sources under `external/googletest`

### Fixed
- Bounded snapshot key/value lengths during load so corrupted snapshots cannot request unbounded allocations

## [0.2.0] - 2026-04-21
### Added
- Write-ahead logging (WAL) with append-only persistence
- Binary record format for SET and DELETE records
- WAL replay on startup for crash recovery

### Changed
- Integrated persistence layer into KV store write path
- Preserved simple store API while introducing durability

### Fixed
- Handling of partial/corrupted WAL records during replay
- Safe recovery from incomplete trailing WAL writes

---

## [0.1.0] - 2026-04-18
### Added
- In-memory key-value store (put/get/delete)
- CLI interface for interacting with store
- Initial separation of parser, server, and storage layers
