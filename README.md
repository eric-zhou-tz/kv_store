# KV Store

A clean C++ foundation for a single-threaded key-value store with simple
write-ahead log persistence.

This repository is intentionally small, but it is structured to evolve into a
production-quality system with snapshotting, networking, and concurrency in
later phases.

## Goals

- Keep the Phase 1 implementation simple and readable.
- Maintain clean boundaries between parsing, storage, and interface layers.
- Make future extensions possible without rewriting the core store logic.
- Keep dependencies explicit and easy to bootstrap.

## Features

- In-memory `KVStore` backed by `std::unordered_map`
- Write-ahead log persistence in `kv_store.wal`
- Snapshot support in `kv_store.snapshot`
- Startup snapshot load plus WAL replay so data survives process restarts
- Basic CLI with `SET`, `GET`, `DEL`, `HELP`, and `EXIT`
- `DELETE` is accepted as an alias for `DEL`
- Small parser module isolated from storage logic
- Simple Makefile-based build
- Docker-friendly project layout
- GoogleTest-based persistence tests

## Persistence

Mutating commands are persisted to `kv_store.wal` before they are applied to memory.
The WAL uses a compact binary record format instead of command text:

```text
[record_length][op][key_size][key][value_size][value]  // SET
[record_length][op][key_size][key]                     // DELETE
```

On startup, the program loads `kv_store.snapshot` when present, then replays
`kv_store.wal` from the snapshot's covered byte offset before accepting
commands. Malformed bounded WAL records are skipped during replay, and an
incomplete trailing record stops recovery after the last valid operation.

Example startup output:

```text
Replaying WAL...
Recovered 3 operation(s)
kv-store>
```

The WAL is intentionally simple:

- `SET <key> <value>` appends a binary set record, flushes it, then updates
  memory.
- `DEL <key>` or `DELETE <key>` appends a delete record, flushes it, then
  removes the key from memory.
- `GET <key>` reads from memory. The WAL is only used for recovery.

## Directory Layout

```text
.
├── bench
│   ├── benchmark.cpp
│   ├── benchmark_utils.cpp
│   ├── benchmark_utils.h
│   ├── workloads.cpp
│   └── workloads.h
├── Dockerfile
├── DESIGN.md
├── Makefile
├── README.md
├── benchmark.md
├── include
│   ├── common
│   │   └── string_utils.h
│   ├── parser
│   │   └── command_parser.h
│   ├── persistence
│   │   └── wal.h
│   ├── server
│   │   └── cli_server.h
│   └── store
│       └── kv_store.h
├── scripts
│   ├── bootstrap_gtest.sh
│   ├── build.sh
│   └── run.sh
├── src
│   ├── common
│   │   └── string_utils.cpp
│   ├── main.cpp
│   ├── parser
│   │   └── command_parser.cpp
│   ├── persistence
│   │   ├── snapshot.cpp
│   │   └── wal.cpp
│   ├── server
│   │   └── cli_server.cpp
│   └── store
│       └── kv_store.cpp
└── tests
    ├── helpers
    ├── integration
    ├── stress
    ├── test_main.cpp
    └── unit
```

## Build

```bash
make
```

## Run

```bash
./bin/kv_store
```

Example session:

```text
kv-store> SET language cpp
OK
kv-store> GET language
cpp
kv-store> DELETE language
1
kv-store> EXIT
Bye
```

## Test

Tests use GoogleTest. The Makefile first looks for `external/googletest` or
`vendor/googletest`, then falls back to common system installs. To bootstrap a
local copy:

```bash
./scripts/bootstrap_gtest.sh
```

Run the normal unit and integration suite:

```bash
make test
```

Run with GoogleTest timing output:

```bash
make test_verbose
```

Run the bounded stress suite:

```bash
make test_stress
```

## Benchmarks

This benchmark suite establishes the baseline performance of the current
persisted KV store implementation. It measures steady-state read/write
throughput, latency percentiles, mixed workload behavior, recovery time, and
snapshot creation cost. These results provide the comparison point for later
Phase 3 storage-engine and performance optimization work.

Build and run the benchmark executable:

```bash
make benchmark
./benchmark
```

You can pass a smaller or larger operation count when collecting local results:

```bash
./benchmark 100000
```

Do not treat the README as a source of benchmark numbers. Capture fresh output
from the current machine and build when documenting results.

See [benchmark.md](benchmark.md) for the baseline benchmark methodology,
results, and comparison guidance.

## Docker

```bash
docker build -t kv-store .
docker run --rm -it kv-store
```

## Next Phases

- Add a storage engine abstraction for persistent backends
- Add snapshotting and WAL compaction
- Add TCP request handling
- Add worker/threading model around the server layer
- Add richer test coverage and benchmarking
