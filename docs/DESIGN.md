# Design Notes

This project is a single-node C++ key-value store focused on a clean,
durable baseline before adding concurrency, networking, or a larger storage
engine. The current implementation keeps the active data structure in memory
and uses disk for write-ahead logging, snapshots, and crash recovery.

## Storage Model

`KVStore` owns the authoritative in-memory map:

```cpp
std::unordered_map<std::string, std::string>
```

Reads are served directly from memory. Writes are persisted first, then applied
to the map. This keeps the steady-state API simple while making recovery
possible after process restarts.

The store can run without persistence for unit tests and in-memory use, or with
configured `WriteAheadLog` and `Snapshot` objects for durable operation.

## Module Boundaries

- `store`: Owns key/value state, write path ordering, snapshot triggers, and
  recovery application.
- `persistence`: Owns binary WAL and snapshot formats, bounded parsing, file
  replacement, and replay.
- `parser`: Parses raw JSON requests and validates only the shared request
  shape.
- `command`: Interprets validated actions, maps them to KV keys and values,
  and calls the generic store.
- `common`: Holds small shared string utilities.

The parser and command layer do not know the persistence format. Recovery applies
directly to the store's backing map so replay does not append recovered
operations back into the WAL.

## Write-Ahead Log

The WAL is append-only and stores binary framed records. Each record starts
with a 32-bit payload length followed by an opcode-specific payload.

```text
SET:
[record_length][op=1][key_size][key][value_size][value]

DELETE:
[record_length][op=2][key_size][key]
```

Write ordering:

1. Append the WAL record.
2. Flush the WAL stream.
3. Apply the mutation to the in-memory map.
4. Trigger an automatic snapshot if the write interval has been reached.

`DELETE` operations are logged even when the key does not exist. This keeps the
log as an ordered command history and makes replay idempotent.

Replay validates each bounded frame before applying it. Malformed records are
skipped when they are fully framed, while incomplete trailing records stop
replay at the last valid operation. Impossible record lengths stop replay
without allocating the payload.

## Snapshots

Snapshots materialize the full in-memory map. They are separate from the WAL:
the snapshot stores state, while the WAL stores ordered mutations after that
state.

Snapshot file layout:

```text
[magic="KVS1"][version][wal_offset][entry_count]
[key_size][key][value_size][value]
...
```

The snapshot writer saves to a temporary file and then renames it over the
committed snapshot. This prevents a failed snapshot write from replacing the
last complete snapshot.

Snapshots are created in two ways:

- Automatically after every 1,000 write commands.
- Explicitly through `KVStore::SaveSnapshot()`.

Each snapshot records the current WAL byte offset. That offset marks the point
through the log already represented by the snapshot.

## Recovery Flow

Startup recovery follows the same sequence as the application entry point:

1. Open the WAL and snapshot handles.
2. Load `kv_store.snapshot` if it exists.
3. Use the snapshot's recorded WAL offset when available.
4. Replay `kv_store.wal` from that offset into the in-memory map.
5. Hand validated JSON commands to the command layer for execution.

In the current application entry point, those files are stored under `data/`
by default, or under the directory supplied with `--db`.

If no snapshot exists, recovery replays the WAL from offset zero. If a snapshot
exists but the WAL has newer operations, those operations override or delete
snapshot values during tail replay.

## Tradeoffs

- Simplicity vs performance: The store uses one in-memory hash map and a direct
  append-only WAL. This keeps correctness easy to reason about, but every write
  pays for a WAL flush.
- Replay cost vs snapshot frequency: More frequent snapshots reduce WAL replay
  time but add periodic full-map snapshot cost to the write path.
- Binary compactness vs portability: Current binary formats use fixed-width
  fields and host byte order. A future portable format should define explicit
  endianness.
- Single-threaded baseline vs concurrency: The current design avoids locks and
  request interleavings so persistence ordering is clear. Concurrency should be
  added around this contract, not before it is stable.
- Full snapshots vs incremental checkpoints: Full snapshots are simple and
  robust for this phase. Larger datasets may need segmented WAL files,
  incremental checkpointing, or compaction.

## Current Limits

- Single process and single-threaded request handling
- No transactions or multi-key atomic operations
- No WAL checksums yet
- No WAL segmentation or garbage collection after snapshot coverage
- No networking or replication layer

These limits are intentional for the current phase. They define the boundary
for Phase 3 storage-engine work and later concurrency/networking phases.
