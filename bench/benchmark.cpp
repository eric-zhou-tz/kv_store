#include "benchmark_utils.h"
#include "workloads.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t kDefaultOperations = 20000;
constexpr std::size_t kValueSizeBytes = 128;
constexpr std::size_t kMixedKeyspaceSize = 10000;
constexpr std::uint32_t kMixedReadPercent = 70;
constexpr std::uint32_t kRandomSeed = 12345;

std::size_t ParseOperations(int argc, char* argv[]) {
  if (argc == 1) {
    return kDefaultOperations;
  }

  if (argc != 2 || std::string(argv[1]) == "--help" ||
      std::string(argv[1]) == "-h") {
    throw std::invalid_argument("usage: ./benchmark [operation_count]");
  }

  std::string input(argv[1]);
  if (input.empty() || input[0] == '-') {
    throw std::invalid_argument("operation_count must be a positive integer");
  }

  std::size_t parsed_characters = 0;
  const unsigned long long operations =
      std::stoull(input, &parsed_characters);
  if (parsed_characters != input.size() || operations == 0) {
    throw std::invalid_argument("operation_count must be a positive integer");
  }
  if (operations > std::numeric_limits<std::size_t>::max()) {
    throw std::invalid_argument("operation_count is too large");
  }

  return static_cast<std::size_t>(operations);
}

void PrintLatencyStats(const kv::bench::LatencyStats& stats) {
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "avg_latency_us: " << stats.average_us << "\n";
  std::cout << "p50_latency_us: " << stats.p50_us << "\n";
  std::cout << "p95_latency_us: " << stats.p95_us << "\n";
  std::cout << "p99_latency_us: " << stats.p99_us << "\n";
}

void PrintOperationBenchmark(
    const std::string& title,
    const kv::bench::OperationBenchmarkResult& result) {
  std::cout << "\n[" << title << "]\n";
  std::cout << "operations: " << result.operations << "\n";
  std::cout << std::fixed << std::setprecision(6);
  std::cout << "elapsed_sec: " << result.elapsed_sec << "\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "throughput_ops_sec: " << result.throughput_ops_sec << "\n";
  PrintLatencyStats(result.latency);
}

void PrintMixedBenchmark(const kv::bench::MixedBenchmarkResult& result) {
  PrintOperationBenchmark("MIXED BENCHMARK", result.overall);
  std::cout << "read_operations: " << result.read_operations << "\n";
  std::cout << "write_operations: " << result.write_operations << "\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "read_avg_latency_us: "
            << result.read_latency.average_us << "\n";
  std::cout << "read_p50_latency_us: " << result.read_latency.p50_us << "\n";
  std::cout << "read_p95_latency_us: " << result.read_latency.p95_us << "\n";
  std::cout << "read_p99_latency_us: " << result.read_latency.p99_us << "\n";
  std::cout << "write_avg_latency_us: "
            << result.write_latency.average_us << "\n";
  std::cout << "write_p50_latency_us: " << result.write_latency.p50_us
            << "\n";
  std::cout << "write_p95_latency_us: " << result.write_latency.p95_us
            << "\n";
  std::cout << "write_p99_latency_us: " << result.write_latency.p99_us
            << "\n";
}

void PrintRecoveryBenchmark(
    const kv::bench::RecoveryBenchmarkResult& result) {
  std::cout << "\n[RECOVERY BENCHMARK]\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "recovery_time_ms: " << result.recovery_time_ms << "\n";
  std::cout << "entries_restored: " << result.entries_restored << "\n";
  std::cout << "recovery_path: "
            << kv::bench::RecoveryPathDescription(result) << "\n";
  std::cout << "snapshot_loaded: "
            << (result.snapshot_loaded ? "true" : "false") << "\n";
  std::cout << "snapshot_entries: " << result.snapshot_entries << "\n";
  std::cout << "wal_operations_replayed: "
            << result.wal_operations_replayed << "\n";
  std::cout << "snapshot_wal_offset: " << result.snapshot_wal_offset
            << "\n";
  std::cout << "snapshot_file_bytes: " << result.snapshot_file_bytes
            << "\n";
  std::cout << "snapshot_file_human: "
            << kv::bench::FormatBytes(result.snapshot_file_bytes) << "\n";
  std::cout << "wal_file_bytes: " << result.wal_file_bytes << "\n";
  std::cout << "wal_file_human: "
            << kv::bench::FormatBytes(result.wal_file_bytes) << "\n";
}

void PrintSnapshotBenchmark(
    const kv::bench::SnapshotBenchmarkResult& result) {
  std::cout << "\n[SNAPSHOT BENCHMARK]\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "snapshot_time_ms: " << result.snapshot_time_ms << "\n";
  std::cout << "records: " << result.records << "\n";
  std::cout << "snapshot_written: "
            << (result.snapshot_written ? "true" : "false") << "\n";
  std::cout << "snapshot_file_bytes: " << result.snapshot_file_bytes
            << "\n";
  std::cout << "snapshot_file_human: "
            << kv::bench::FormatBytes(result.snapshot_file_bytes) << "\n";
  std::cout << "wal_file_bytes: " << result.wal_file_bytes << "\n";
  std::cout << "wal_file_human: "
            << kv::bench::FormatBytes(result.wal_file_bytes) << "\n";
}

void PrintConfig(const kv::bench::WorkloadConfig& config) {
  std::cout << "[BENCHMARK CONFIG]\n";
  std::cout << "operations: " << config.operations << "\n";
  std::cout << "value_size_bytes: " << config.value_size_bytes << "\n";
  std::cout << "mixed_read_percent: " << config.mixed_read_percent << "\n";
  std::cout << "mixed_write_percent: "
            << (100U - config.mixed_read_percent) << "\n";
  std::cout << "mixed_keyspace_size: "
            << std::min(config.operations, config.mixed_keyspace_size)
            << "\n";
  std::cout << "random_seed: " << config.random_seed << "\n";
  std::cout << "persistence: wal+snapshot\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    kv::bench::WorkloadConfig config;
    config.operations = ParseOperations(argc, argv);
    config.value_size_bytes = kValueSizeBytes;
    config.mixed_keyspace_size = kMixedKeyspaceSize;
    config.mixed_read_percent = kMixedReadPercent;
    config.random_seed = kRandomSeed;

    PrintConfig(config);

    PrintOperationBenchmark("WRITE BENCHMARK",
                            kv::bench::RunWriteBenchmark(config));
    PrintOperationBenchmark("READ BENCHMARK",
                            kv::bench::RunReadBenchmark(config));
    PrintMixedBenchmark(kv::bench::RunMixedBenchmark(config));
    PrintRecoveryBenchmark(kv::bench::RunRecoveryBenchmark(config));
    PrintSnapshotBenchmark(kv::bench::RunSnapshotBenchmark(config));

    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}
