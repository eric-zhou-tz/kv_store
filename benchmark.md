# KV Store Benchmark Report

This document records the initial persisted-baseline performance of the current
KV store implementation. These results are intended to serve as the comparison
point for future Phase 3 storage-engine and performance work.

The benchmark target is the current production code path: `KVStore` backed by
the write-ahead log and snapshot persistence components. No legacy Phase 1
implementation, alternate in-memory-only implementation, or synthetic storage
backend is used for comparison.

## Benchmark Scope

The suite measures the following characteristics:

- Write throughput and latency for persisted `SET` operations
- Read throughput and latency for successful `GET` operations
- Mixed workload throughput and latency using a deterministic 70/30 read/write
  profile
- Recovery time through snapshot load plus WAL replay
- Snapshot creation time
- WAL and snapshot artifact sizes

The current suite is intentionally single-threaded. It measures the baseline
behavior of the current implementation before introducing Phase 3 storage-engine
optimizations.

## How To Run

Build the benchmark executable:

```bash
make benchmark
```

Run with the default workload size:

```bash
./benchmark
```

Run with a custom operation count:

```bash
./benchmark 100000
```

The benchmark prints structured results to stdout. To persist a run:

```bash
./benchmark 100000 > benchmark_results.txt
```

## Reproducibility Controls

The benchmark harness uses deterministic inputs so future runs can be compared
against this baseline:

| Parameter | Value |
| --- | ---: |
| Operations | 20,000 |
| Value size | 128 bytes |
| Mixed workload read ratio | 70% |
| Mixed workload write ratio | 30% |
| Mixed workload keyspace | 10,000 keys |
| Random seed | 12,345 |
| Persistence mode | WAL + snapshot |

All benchmark data is stored in temporary directories created by the benchmark
process and removed after each workload completes.

## Environment Metadata

Record the following environment details with any official benchmark run. They
are not inferred by the benchmark executable today.

| Field | Value |
| --- | --- |
| Machine | To be recorded |
| CPU | To be recorded |
| Memory | To be recorded |
| Operating system | To be recorded |
| Compiler | To be recorded |
| Build flags | To be recorded |
| Storage medium | To be recorded |
| Git commit | To be recorded |

## Baseline Results

The following results were captured from the current persisted baseline using
20,000 operations and 128-byte values.

### Summary

| Workload | Operations | Elapsed | Throughput |
| --- | ---: | ---: | ---: |
| Write | 20,000 | 0.089527 sec | 223,395.57 ops/sec |
| Read | 20,000 | 0.004474 sec | 4,470,689.38 ops/sec |
| Mixed | 20,000 | 0.022163 sec | 902,398.15 ops/sec |

### Latency

All latency values are reported in microseconds.

| Workload | Avg | P50 | P95 | P99 |
| --- | ---: | ---: | ---: | ---: |
| Write | 4.40 | 1.54 | 2.71 | 5.50 |
| Read | 0.16 | 0.17 | 0.21 | 0.21 |
| Mixed overall | 1.03 | 0.21 | 1.25 | 2.58 |
| Mixed reads | 0.19 | 0.21 | 0.25 | 0.29 |
| Mixed writes | 3.01 | 1.12 | 1.50 | 3.21 |

### Mixed Workload Composition

| Operation type | Count |
| --- | ---: |
| Reads | 14,067 |
| Writes | 5,933 |

### Recovery

| Metric | Value |
| --- | ---: |
| Recovery time | 8.38 ms |
| Entries restored | 20,999 |
| Recovery path | snapshot + WAL |
| Snapshot loaded | true |
| Snapshot entries | 20,000 |
| WAL operations replayed | 999 |
| Snapshot WAL offset | 3,268,890 bytes |
| Snapshot file size | 3,168,910 bytes |
| Snapshot file size, human | 3.02 MiB |
| WAL file size | 3,430,618 bytes |
| WAL file size, human | 3.27 MiB |

### Snapshot Creation

| Metric | Value |
| --- | ---: |
| Snapshot time | 4.21 ms |
| Records | 20,000 |
| Snapshot written | true |
| Snapshot file size | 3,068,910 bytes |
| Snapshot file size, human | 2.93 MiB |
| WAL file size | 3,168,890 bytes |
| WAL file size, human | 3.02 MiB |

## Interpretation

The write benchmark measures the persisted mutation path, including WAL append
and flush behavior. Because the store is configured with snapshots, write
latency can include automatic snapshot work when the snapshot interval is
reached.

The read benchmark measures successful in-memory lookups after a durable
preload. Reads do not consult the WAL or snapshot files on the steady-state
read path.

The mixed benchmark uses a deterministic bounded keyspace so reads are expected
to hit existing keys and writes mostly update existing keys. The overall
latency distribution combines inexpensive reads with more expensive persisted
writes, while the read and write latency rows show the two paths separately.

The recovery benchmark measures the application startup pattern: load the latest
snapshot, then replay WAL records after the snapshot's covered byte offset. This
run restored 20,000 snapshot entries and replayed 999 WAL operations.

The snapshot benchmark measures explicit creation of a full point-in-time
snapshot of the current in-memory map.

## Comparison Guidance

Future Phase 3 changes should compare against this baseline using the same
operation count, value size, random seed, compiler settings, and machine
environment whenever possible. Any benchmark report for a future change should
include:

- The old and new git commits
- The exact command used
- The environment metadata listed above
- Throughput deltas for write, read, and mixed workloads
- Latency deltas for average, P50, P95, and P99
- Recovery and snapshot timing deltas
- WAL and snapshot size changes

Performance improvements should be evaluated alongside correctness tests. A
faster result is not meaningful unless the normal unit, integration, stress, and
recovery tests still pass.

## Known Limitations

This benchmark suite is a baseline harness, not a full production performance
lab. Current limitations include:

- Single-threaded workloads only
- No warm-up phase beyond workload-specific preload
- No CPU, memory, or disk I/O profiling
- No long-duration soak testing
- No automated environment capture
- No CSV or JSON output mode

These limitations are acceptable for the current phase because the goal is to
establish a clear, reproducible baseline before larger storage-engine changes.
