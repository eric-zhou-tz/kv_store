#include "workloads.h"

#include "persistence/snapshot.h"
#include "persistence/wal.h"
#include "store/kv_store.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace kv {
namespace bench {

namespace {

struct MixedOperation {
  bool is_read = true;
  std::size_t key_index = 0;
};

std::vector<std::string> MakeKeys(const std::string& prefix,
                                  std::size_t count) {
  std::vector<std::string> keys;
  keys.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    keys.push_back(MakeIndexedKey(prefix, i));
  }
  return keys;
}

OperationBenchmarkResult BuildOperationResult(std::size_t operations,
                                              double elapsed_sec,
                                              const std::vector<double>&
                                                  latencies_us) {
  OperationBenchmarkResult result;
  result.operations = operations;
  result.elapsed_sec = elapsed_sec;
  result.throughput_ops_sec =
      ThroughputOpsPerSecond(operations, elapsed_sec);
  result.latency = ComputeLatencyStats(latencies_us);
  return result;
}

std::string WalPath(const TempDirectory& directory) {
  return directory.FilePath("wal.log");
}

std::string SnapshotPath(const TempDirectory& directory) {
  return directory.FilePath("store.snapshot");
}

void PreloadStore(kv::store::KVStore& store,
                  const std::vector<std::string>& keys,
                  const std::string& value) {
  for (const std::string& key : keys) {
    store.Set(key, value);
  }
}

std::size_t MixedKeyspaceSize(const WorkloadConfig& config) {
  return std::max<std::size_t>(
      1, std::min(config.operations, config.mixed_keyspace_size));
}

std::vector<MixedOperation> BuildMixedPlan(const WorkloadConfig& config,
                                           std::size_t keyspace_size) {
  std::mt19937 generator(config.random_seed);
  std::vector<MixedOperation> plan;
  plan.reserve(config.operations);

  for (std::size_t i = 0; i < config.operations; ++i) {
    const bool is_read = (generator() % 100U) < config.mixed_read_percent;
    const std::size_t key_index = generator() % keyspace_size;
    plan.push_back(MixedOperation{is_read, key_index});
  }

  return plan;
}

std::size_t RecoveryTailOperations(const WorkloadConfig& config) {
  return std::max<std::size_t>(
      1, std::min<std::size_t>(999, config.operations / 20));
}

}  // namespace

OperationBenchmarkResult RunWriteBenchmark(const WorkloadConfig& config) {
  TempDirectory directory("write");
  kv::persistence::WriteAheadLog wal(WalPath(directory));
  kv::persistence::Snapshot snapshot(SnapshotPath(directory));
  kv::store::KVStore store(&wal, &snapshot);

  const std::vector<std::string> keys = MakeKeys("key", config.operations);
  const std::string value = MakeFixedValue(config.value_size_bytes);
  std::vector<double> latencies_us;
  latencies_us.reserve(config.operations);

  // Measures the persisted SET path, including WAL flushes and any automatic
  // snapshots triggered by the store's configured snapshot interval.
  const auto benchmark_start = Clock::now();
  for (const std::string& key : keys) {
    const auto operation_start = Clock::now();
    store.Set(key, value);
    const auto operation_end = Clock::now();
    latencies_us.push_back(ElapsedMicroseconds(operation_start,
                                               operation_end));
  }
  const auto benchmark_end = Clock::now();

  return BuildOperationResult(
      config.operations, ElapsedSeconds(benchmark_start, benchmark_end),
      latencies_us);
}

OperationBenchmarkResult RunReadBenchmark(const WorkloadConfig& config) {
  TempDirectory directory("read");
  kv::persistence::WriteAheadLog wal(WalPath(directory));
  kv::persistence::Snapshot snapshot(SnapshotPath(directory));
  kv::store::KVStore store(&wal, &snapshot);

  const std::vector<std::string> keys = MakeKeys("key", config.operations);
  const std::string value = MakeFixedValue(config.value_size_bytes);
  PreloadStore(store, keys, value);

  std::vector<double> latencies_us;
  latencies_us.reserve(config.operations);

  // Measures steady-state in-memory GET performance after durable preload.
  const auto benchmark_start = Clock::now();
  for (const std::string& key : keys) {
    const auto operation_start = Clock::now();
    const auto result = store.Get(key);
    const auto operation_end = Clock::now();
    if (!result.has_value()) {
      throw std::runtime_error("read benchmark expected preloaded key");
    }
    latencies_us.push_back(ElapsedMicroseconds(operation_start,
                                               operation_end));
  }
  const auto benchmark_end = Clock::now();

  return BuildOperationResult(
      config.operations, ElapsedSeconds(benchmark_start, benchmark_end),
      latencies_us);
}

MixedBenchmarkResult RunMixedBenchmark(const WorkloadConfig& config) {
  TempDirectory directory("mixed");
  kv::persistence::WriteAheadLog wal(WalPath(directory));
  kv::persistence::Snapshot snapshot(SnapshotPath(directory));
  kv::store::KVStore store(&wal, &snapshot);

  const std::size_t keyspace_size = MixedKeyspaceSize(config);
  const std::vector<std::string> keys = MakeKeys("mixed_key", keyspace_size);
  const std::string value = MakeFixedValue(config.value_size_bytes);
  PreloadStore(store, keys, value);

  const std::vector<MixedOperation> plan = BuildMixedPlan(config, keyspace_size);

  std::vector<double> overall_latencies_us;
  std::vector<double> read_latencies_us;
  std::vector<double> write_latencies_us;
  overall_latencies_us.reserve(config.operations);
  read_latencies_us.reserve(config.operations);
  write_latencies_us.reserve(config.operations);

  std::size_t read_operations = 0;
  std::size_t write_operations = 0;

  // Measures a deterministic 70/30-style GET/SET workload over an existing
  // bounded keyspace, so most reads are hits and writes are overwrites.
  const auto benchmark_start = Clock::now();
  for (const MixedOperation& operation : plan) {
    const std::string& key = keys[operation.key_index];
    const auto operation_start = Clock::now();
    if (operation.is_read) {
      const auto result = store.Get(key);
      const auto operation_end = Clock::now();
      if (!result.has_value()) {
        throw std::runtime_error("mixed benchmark expected read hit");
      }
      const double latency_us =
          ElapsedMicroseconds(operation_start, operation_end);
      overall_latencies_us.push_back(latency_us);
      read_latencies_us.push_back(latency_us);
      ++read_operations;
    } else {
      store.Set(key, value);
      const auto operation_end = Clock::now();
      const double latency_us =
          ElapsedMicroseconds(operation_start, operation_end);
      overall_latencies_us.push_back(latency_us);
      write_latencies_us.push_back(latency_us);
      ++write_operations;
    }
  }
  const auto benchmark_end = Clock::now();

  MixedBenchmarkResult result;
  result.overall = BuildOperationResult(
      config.operations, ElapsedSeconds(benchmark_start, benchmark_end),
      overall_latencies_us);
  result.read_operations = read_operations;
  result.write_operations = write_operations;
  result.read_latency = ComputeLatencyStats(read_latencies_us);
  result.write_latency = ComputeLatencyStats(write_latencies_us);
  return result;
}

RecoveryBenchmarkResult RunRecoveryBenchmark(const WorkloadConfig& config) {
  TempDirectory directory("recovery");
  const std::string wal_path = WalPath(directory);
  const std::string snapshot_path = SnapshotPath(directory);
  const std::string value = MakeFixedValue(config.value_size_bytes);
  const std::size_t tail_operations = RecoveryTailOperations(config);

  {
    kv::persistence::WriteAheadLog wal(wal_path);
    kv::persistence::Snapshot snapshot(snapshot_path);
    kv::store::KVStore store(&wal, &snapshot);

    const std::vector<std::string> base_keys =
        MakeKeys("recovery_base_key", config.operations);
    PreloadStore(store, base_keys, value);

    if (!store.SaveSnapshot()) {
      throw std::runtime_error("recovery benchmark failed to save snapshot");
    }

    const std::vector<std::string> tail_keys =
        MakeKeys("recovery_tail_key", tail_operations);
    PreloadStore(store, tail_keys, value);
  }

  kv::persistence::WriteAheadLog wal(wal_path);
  kv::persistence::Snapshot snapshot(snapshot_path);
  kv::store::KVStore recovered(&wal, &snapshot);

  // Measures the same startup shape as the application: load snapshot first,
  // then replay the WAL tail after the snapshot's covered byte offset.
  const auto recovery_start = Clock::now();
  const kv::persistence::SnapshotLoadResult snapshot_result =
      recovered.LoadSnapshot(snapshot);
  const std::size_t wal_operations =
      recovered.ReplayFromWal(wal, snapshot_result.wal_offset);
  const auto recovery_end = Clock::now();

  RecoveryBenchmarkResult result;
  result.recovery_time_ms = ElapsedMilliseconds(recovery_start, recovery_end);
  result.entries_restored = recovered.Size();
  result.snapshot_entries = snapshot_result.entry_count;
  result.wal_operations_replayed = wal_operations;
  result.snapshot_loaded = snapshot_result.loaded;
  result.snapshot_wal_offset = snapshot_result.wal_offset;
  result.snapshot_file_bytes = FileSizeOrZero(snapshot_path);
  result.wal_file_bytes = FileSizeOrZero(wal_path);
  return result;
}

SnapshotBenchmarkResult RunSnapshotBenchmark(const WorkloadConfig& config) {
  TempDirectory directory("snapshot");
  const std::string wal_path = WalPath(directory);
  const std::string snapshot_path = SnapshotPath(directory);

  kv::persistence::WriteAheadLog wal(wal_path);
  kv::persistence::Snapshot snapshot(snapshot_path);
  kv::store::KVStore store(&wal, &snapshot);

  const std::vector<std::string> keys =
      MakeKeys("snapshot_key", config.operations);
  const std::string value = MakeFixedValue(config.value_size_bytes);
  PreloadStore(store, keys, value);

  // Measures a full point-in-time snapshot of the current in-memory map.
  const auto snapshot_start = Clock::now();
  const bool snapshot_written = store.SaveSnapshot();
  const auto snapshot_end = Clock::now();

  SnapshotBenchmarkResult result;
  result.snapshot_time_ms =
      ElapsedMilliseconds(snapshot_start, snapshot_end);
  result.records = store.Size();
  result.snapshot_written = snapshot_written;
  result.snapshot_file_bytes = FileSizeOrZero(snapshot_path);
  result.wal_file_bytes = FileSizeOrZero(wal_path);
  return result;
}

std::string RecoveryPathDescription(const RecoveryBenchmarkResult& result) {
  if (result.snapshot_loaded && result.wal_operations_replayed > 0) {
    return "snapshot+wal";
  }
  if (result.snapshot_loaded) {
    return "snapshot";
  }
  if (result.wal_operations_replayed > 0) {
    return "wal";
  }
  return "empty";
}

}  // namespace bench
}  // namespace kv
