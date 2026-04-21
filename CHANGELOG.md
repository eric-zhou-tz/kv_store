# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
### Added
- GoogleTest-based unit, integration, and stress test suites for Phase 2 persistence
- Makefile targets for `make test`, `make test_verbose`, and `make test_stress`
- Temporary test directory and binary file helpers for isolated persistence tests
- GoogleTest bootstrap script for vendoring into `external/googletest`

### Changed
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
