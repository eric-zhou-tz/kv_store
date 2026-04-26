# ContextKV ⚡️🗄️

## 🚀 One-Sentence Description

High-performance C++ key-value store with in-memory reads, append-only WAL
persistence, snapshotting, and crash recovery.

## Key Highlights

- Single-threaded C++17 key-value engine backed by `std::unordered_map`
- Durable write path using an append-only binary write-ahead log
- Snapshot checkpoints that record the covered WAL byte offset
- Crash recovery through snapshot load plus WAL tail replay
- GoogleTest coverage for core storage, WAL replay, snapshots, and recovery
- Benchmark harness for throughput, latency, recovery, and snapshot timing

## Demo

![KV Store Demo](docs/demo.gif)

## Features

- JSON-driven agent command pipeline: parse raw request, validate shape,
  dispatch action, and execute against the generic KV store
- Supported command actions: `put`, `get`, `delete`, `log_step`,
  `save_memory`, `get_memory`, `save_run_state`, and `get_run_state`
- In-memory storage engine for steady-state reads and writes
- Append-only WAL for durable `SET` and `DELETE` operations
- Binary, length-prefixed persistence records with bounded replay parsing
- Full-state snapshots saved automatically every 1,000 writes or explicitly via
  `KVStore::SaveSnapshot()`
- Startup recovery that loads a snapshot first, then replays WAL records after
  the snapshot's recorded byte offset
- GoogleTest unit, integration, and stress tests
- Single-threaded benchmark suite for persisted write, read, mixed, recovery,
  and snapshot workloads

## Persistence Model

The store keeps the active dataset in memory and uses disk only for durability
and recovery.

1. Mutating commands append a WAL record and flush it before updating memory.
2. `SET` records store an opcode, key length, key bytes, value length, and
   value bytes.
3. `DELETE` records store an opcode, key length, and key bytes. Delete attempts
   are logged even when the key is absent so replay preserves command order.
4. Snapshots write the full in-memory map to a temporary file, then replace the
   committed snapshot file. Each snapshot stores the WAL byte offset it covers.
5. Startup recovery loads `kv_store.snapshot` when present, then replays
   `kv_store.wal` from the saved offset. If no snapshot exists, recovery falls
   back to WAL replay from offset zero. By default these files live under
   `data/`, or under the directory passed with `--db`.

Replay skips malformed bounded records, stops safely at an incomplete trailing
record, and avoids unbounded allocations for corrupted lengths.

## Benchmarks

The repository includes a benchmark executable under `bench/`. It measures the
current persisted code path: `KVStore` with WAL and snapshot persistence.

```bash
make benchmark
./benchmark
./benchmark 100000
```

The checked-in baseline report is in [docs/benchmark.md](docs/benchmark.md). Treat those
numbers as a local baseline, not portable performance claims; rerun the suite on
the target machine before publishing new results.

| Workload | Operations | Throughput | Notes |
| --- | ---: | ---: | --- |
| Write | 20,000 | 223,395.57 ops/sec | Persisted `SET` path with WAL flushes |
| Read | 20,000 | 4,470,689.38 ops/sec | In-memory successful `GET` after preload |
| Mixed | 20,000 | 902,398.15 ops/sec | Deterministic 70/30 read/write workload |
| Recovery | 20,000 base + 999 tail | 8.38 ms | Snapshot load plus WAL tail replay |
| Snapshot | 20,000 records | 4.21 ms | Explicit full-state snapshot |

Benchmark conditions to record for future runs:

| Field | Value |
| --- | --- |
| Machine | MacBook Pro 14 inch 2024 |
| CPU | Apple M4 |
| Memory | 16 GB unified memory   |
| Operating system | 26.2 (25C56) |
| Compiler | clang++ (Apple Clang 17.0) |
| Build flags | -O3 -march=native -std=c++17  |
| Storage medium | Internal NVMe SSD |
| Git commit | 7808a016c160c8b0167c395443920b65153afe25 |

## Testing

Tests are organized by behavior rather than implementation detail:

- Core store tests for `SET`, `GET`, `DELETE`, overwrites, missing keys, and
  large values
- WAL tests for replay order, malformed records, partial trailing records, and
  replay from offsets
- Snapshot tests for save/load, legacy snapshot loading, corruption handling,
  and bounded field sizes
- Recovery integration tests for WAL-only recovery, snapshot-only recovery,
  snapshot plus WAL tail recovery, and persistence reset behavior
- Stress tests for deterministic mixed workloads, hot-key overwrites, many
  distinct keys, large values, and large snapshot-plus-WAL recovery

GoogleTest is required for the test targets. The Makefile can use a vendored
copy, a system install, or `GTEST_ROOT`.

```bash
./scripts/bootstrap_gtest.sh
make test
make test_verbose
make test_stress
```

## Quick Start

```bash
git clone <repo-url>
cd KV_Store
make
echo '{"action":"put","params":{"key":"x","value":"y"}}' | ./bin/kv_store
echo '{"action":"get","params":{"key":"x"}}' | ./bin/kv_store
```

Conceptual agent integration:

```cpp
std::string raw = R"({"action":"put","params":{"key":"language","value":"cpp"}})";
kv::parser::Json request = kv::parser::parse_agent_request(raw);
kv::command::Json response = kv::command::execute_command(request, store);
std::cout << response.dump() << std::endl;
```

`main` now runs a single-request pipeline. When `--db` is omitted, persistence
is stored in `data/kv_store.wal` and `data/kv_store.snapshot`. Passing
`--db <path>` moves both files under that directory.

Useful targets:

```bash
make run
make test
make test_stress
make benchmark
make run_benchmark
make clean
```

Docker is also supported:

```bash
docker build -t kv-store .
docker run --rm -it kv-store
```

## Project Structure

```text
src/command/        Action-to-KV mapping for validated JSON requests
src/store/          Core in-memory KV store and persistence integration
src/persistence/    WAL, snapshot, and binary I/O implementations
src/parser/         JSON request parsing and shape validation
include/            Public headers for command, store, persistence, and parser
tests/              GoogleTest unit, integration, stress, and helper code
bench/              Single-threaded benchmark harness and workloads
scripts/            Build, run, and GoogleTest bootstrap helpers
docs/               Design notes, changelog, devlog, and benchmark report
```

## Design Decisions

- Use `std::unordered_map` for average O(1) in-memory lookups and updates.
- Keep the core single-threaded while persistence semantics are being hardened.
- Append WAL records before memory mutation so successful writes have durable
  replay records.
- Use length-prefixed binary records for compact WAL and snapshot formats.
- Bound record and field sizes during replay to keep corrupted files from
  causing unbounded allocations.
- Store the covered WAL offset in each snapshot so recovery can replay only the
  post-snapshot tail.
- Save snapshots with a temp-file-plus-rename flow so a failed snapshot write
  does not replace the last complete snapshot.

## Roadmap

- [x] Phase 1: In-memory single-threaded store
- [x] Phase 2: WAL persistence, crash recovery, and snapshots
- [ ] Phase 3: Benchmarking polish, storage-engine evolution, compaction,
      SSTables, and LSM direction
- [ ] Phase 4: Concurrency and safe multi-threaded request handling
- [ ] Phase 5: Networking and request protocol
- [ ] Phase 6: Replication and distributed extensions
