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
- Avoid external dependencies and keep the build portable.

## Features

- In-memory `KVStore` backed by `std::unordered_map`
- Write-ahead log persistence in `kv_store.wal`
- Startup WAL replay so data survives process restarts
- Basic CLI with `SET`, `GET`, `DEL`, `HELP`, and `EXIT`
- `DELETE` is accepted as an alias for `DEL`
- Small parser module isolated from storage logic
- Simple Makefile-based build
- Docker-friendly project layout
- No external libraries

## Persistence

Mutating commands are persisted to `kv_store.wal` before they are applied to memory.
The WAL uses a compact binary record format instead of command text:

```text
[record_length][op][key_size][key][value_size][value]  // SET
[record_length][op][key_size][key]                     // DELETE
```

On startup, the program replays `kv_store.wal` into the in-memory map before
accepting commands. Malformed bounded records are skipped during replay, and an
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
├── Dockerfile
├── DESIGN.md
├── Makefile
├── README.md
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
│   ├── build.sh
│   └── run.sh
├── src
│   ├── common
│   │   └── string_utils.cpp
│   ├── main.cpp
│   ├── parser
│   │   └── command_parser.cpp
│   ├── persistence
│   │   └── wal.cpp
│   ├── server
│   │   └── cli_server.cpp
│   └── store
│       └── kv_store.cpp
└── tests
    └── test_kv_store.cpp
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

```bash
make test
```

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
