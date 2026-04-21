#ifndef KV_STORE_BENCH_BENCHMARK_UTILS_H_
#define KV_STORE_BENCH_BENCHMARK_UTILS_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace kv {
namespace bench {

using Clock = std::chrono::steady_clock;

struct LatencyStats {
  double average_us = 0.0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double p99_us = 0.0;
};

class TempDirectory {
 public:
  explicit TempDirectory(const std::string& label);
  ~TempDirectory();

  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;

  const std::string& path() const;
  std::string FilePath(const std::string& filename) const;

 private:
  std::string path_;
};

double ElapsedSeconds(Clock::time_point start, Clock::time_point end);
double ElapsedMilliseconds(Clock::time_point start, Clock::time_point end);
double ElapsedMicroseconds(Clock::time_point start, Clock::time_point end);

double ThroughputOpsPerSecond(std::size_t operations, double elapsed_seconds);
LatencyStats ComputeLatencyStats(const std::vector<double>& latencies_us);

std::string MakeIndexedKey(const std::string& prefix, std::size_t index);
std::string MakeFixedValue(std::size_t size_bytes);

std::uintmax_t FileSizeOrZero(const std::string& path);
std::string FormatBytes(std::uintmax_t bytes);

}  // namespace bench
}  // namespace kv

#endif  // KV_STORE_BENCH_BENCHMARK_UTILS_H_
