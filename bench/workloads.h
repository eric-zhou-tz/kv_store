#ifndef KV_STORE_BENCH_WORKLOADS_H_
#define KV_STORE_BENCH_WORKLOADS_H_

#include "benchmark_utils.h"

#include <cstdint>
#include <string>

namespace kv {
namespace bench {

struct WorkloadConfig {
  std::size_t operations = 20000;
  std::size_t value_size_bytes = 128;
  std::size_t mixed_keyspace_size = 10000;
  std::uint32_t mixed_read_percent = 70;
  std::uint32_t random_seed = 12345;
};

struct OperationBenchmarkResult {
  std::size_t operations = 0;
  double elapsed_sec = 0.0;
  double throughput_ops_sec = 0.0;
  LatencyStats latency;
};

struct MixedBenchmarkResult {
  OperationBenchmarkResult overall;
  std::size_t read_operations = 0;
  std::size_t write_operations = 0;
  LatencyStats read_latency;
  LatencyStats write_latency;
};

struct RecoveryBenchmarkResult {
  double recovery_time_ms = 0.0;
  std::size_t entries_restored = 0;
  std::size_t snapshot_entries = 0;
  std::size_t wal_operations_replayed = 0;
  bool snapshot_loaded = false;
  std::uint64_t snapshot_wal_offset = 0;
  std::uintmax_t snapshot_file_bytes = 0;
  std::uintmax_t wal_file_bytes = 0;
};

struct SnapshotBenchmarkResult {
  double snapshot_time_ms = 0.0;
  std::size_t records = 0;
  bool snapshot_written = false;
  std::uintmax_t snapshot_file_bytes = 0;
  std::uintmax_t wal_file_bytes = 0;
};

OperationBenchmarkResult RunWriteBenchmark(const WorkloadConfig& config);
OperationBenchmarkResult RunReadBenchmark(const WorkloadConfig& config);
MixedBenchmarkResult RunMixedBenchmark(const WorkloadConfig& config);
RecoveryBenchmarkResult RunRecoveryBenchmark(const WorkloadConfig& config);
SnapshotBenchmarkResult RunSnapshotBenchmark(const WorkloadConfig& config);

std::string RecoveryPathDescription(const RecoveryBenchmarkResult& result);

}  // namespace bench
}  // namespace kv

#endif  // KV_STORE_BENCH_WORKLOADS_H_
