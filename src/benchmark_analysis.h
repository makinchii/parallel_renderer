#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pr {

struct BenchmarkRecord {
  std::string scene;
  int width = 0;
  int height = 0;
  int spp = 0;
  int depth = 0;
  int threads = 0;
  std::string schedule;
  int tile_size = 0;
  int run = 0;
  double milliseconds = 0.0;
  double pixels_per_sec = 0.0;
  std::string source_file;
  std::string raw_row;
};

struct BenchmarkConfigKey {
  int threads = 0;
  std::string schedule;

  bool operator<(const BenchmarkConfigKey& other) const;
};

struct BenchmarkConfigSummary {
  BenchmarkConfigKey key;
  std::size_t row_count = 0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  double avg_ms = 0.0;
  double median_ms = 0.0;
  double stddev_ms = 0.0;
  double avg_pixels_per_sec = 0.0;
  double observed_speedup = 0.0;
  double efficiency = 0.0;
};

struct BenchmarkWorkloadSummary {
  std::string scene;
  int width = 0;
  int height = 0;
  int spp = 0;
  int depth = 0;
  int tile_size = 0;
  std::size_t row_count = 0;
  std::size_t config_count = 0;
  BenchmarkConfigKey baseline_config;
  double baseline_serial_ms = 0.0;
  double ideal_linear_speedup = 0.0;
  double ideal_runtime_ms = 0.0;
  double best_observed_ms = 0.0;
  double best_observed_speedup = 0.0;
  BenchmarkConfigKey best_config;
};

struct BenchmarkDataset {
  std::filesystem::path csv_path;
  std::string header_text;
  std::vector<BenchmarkRecord> rows;
  std::vector<BenchmarkConfigSummary> configs;
  BenchmarkWorkloadSummary workload;
};

bool load_benchmark_csv(const std::filesystem::path& path, BenchmarkDataset& dataset, std::string& error);
std::string benchmark_config_label(const BenchmarkConfigKey& key);
std::string benchmark_workload_label(const BenchmarkWorkloadSummary& summary);

}  // namespace pr
