#include "benchmark_utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace kv {
namespace bench {

namespace {

double PercentileNearestRank(std::vector<double> sorted_values,
                             double percentile) {
  if (sorted_values.empty()) {
    return 0.0;
  }

  std::sort(sorted_values.begin(), sorted_values.end());
  const double rank =
      std::ceil((percentile / 100.0) * sorted_values.size());
  const std::size_t index =
      static_cast<std::size_t>(std::max(1.0, rank)) - 1;
  return sorted_values[std::min(index, sorted_values.size() - 1)];
}

}  // namespace

TempDirectory::TempDirectory(const std::string& label) {
  const auto stamp =
      Clock::now().time_since_epoch().count();
  const std::filesystem::path base = std::filesystem::temp_directory_path();

  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::filesystem::path candidate =
        base / ("kv_store_bench_" + label + "_" + std::to_string(stamp) +
                "_" + std::to_string(attempt));
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error)) {
      path_ = candidate.string();
      return;
    }
  }

  throw std::runtime_error("failed to create temporary benchmark directory");
}

TempDirectory::~TempDirectory() {
  std::error_code error;
  std::filesystem::remove_all(path_, error);
}

const std::string& TempDirectory::path() const {
  return path_;
}

std::string TempDirectory::FilePath(const std::string& filename) const {
  return (std::filesystem::path(path_) / filename).string();
}

double ElapsedSeconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double>(end - start).count();
}

double ElapsedMilliseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

double ElapsedMicroseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

double ThroughputOpsPerSecond(std::size_t operations, double elapsed_seconds) {
  if (elapsed_seconds <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(operations) / elapsed_seconds;
}

LatencyStats ComputeLatencyStats(const std::vector<double>& latencies_us) {
  if (latencies_us.empty()) {
    return {};
  }

  const double total =
      std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);

  LatencyStats stats;
  stats.average_us = total / static_cast<double>(latencies_us.size());
  stats.p50_us = PercentileNearestRank(latencies_us, 50.0);
  stats.p95_us = PercentileNearestRank(latencies_us, 95.0);
  stats.p99_us = PercentileNearestRank(latencies_us, 99.0);
  return stats;
}

std::string MakeIndexedKey(const std::string& prefix, std::size_t index) {
  return prefix + "_" + std::to_string(index);
}

std::string MakeFixedValue(std::size_t size_bytes) {
  std::string value;
  value.reserve(size_bytes);
  for (std::size_t i = 0; i < size_bytes; ++i) {
    value.push_back(static_cast<char>('a' + (i % 26)));
  }
  return value;
}

std::uintmax_t FileSizeOrZero(const std::string& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  return error ? 0 : size;
}

std::string FormatBytes(std::uintmax_t bytes) {
  constexpr double kKiB = 1024.0;
  constexpr double kMiB = kKiB * 1024.0;
  constexpr double kGiB = kMiB * 1024.0;

  std::ostringstream output;
  output << std::fixed << std::setprecision(2);

  if (bytes >= static_cast<std::uintmax_t>(kGiB)) {
    output << (static_cast<double>(bytes) / kGiB) << " GiB";
  } else if (bytes >= static_cast<std::uintmax_t>(kMiB)) {
    output << (static_cast<double>(bytes) / kMiB) << " MiB";
  } else if (bytes >= static_cast<std::uintmax_t>(kKiB)) {
    output << (static_cast<double>(bytes) / kKiB) << " KiB";
  } else {
    output << bytes << " B";
  }

  return output.str();
}

}  // namespace bench
}  // namespace kv
