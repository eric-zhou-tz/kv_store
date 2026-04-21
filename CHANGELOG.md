# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [0.2.0] - 2026-04-21
### Added
- Write-ahead logging (WAL) with append-only persistence
- Binary record format: [length][op][key_size][key]
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