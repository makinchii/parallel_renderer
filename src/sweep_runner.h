#pragma once

#include "benchmark_runner.h"

#include <filesystem>
#include <atomic>
#include <functional>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace pr {

struct SweepCase {
  std::string scene;
  int width = 0;
  int height = 0;
  int spp = 0;
  int tile_size = 0;
  std::vector<int> thread_counts;
  std::vector<std::string> schedules;
  int runs = 0;
};

struct SweepConfig {
  std::vector<std::string> scenes;
  std::vector<std::pair<int, int>> resolutions;
  std::vector<int> spp_values;
  std::vector<int> tile_sizes;
  std::vector<int> thread_counts;
  std::vector<std::string> schedules;
  int runs = 1;
  std::string output_dir = "results/phase_b";
};

struct SweepProgress {
  int configuration_index = 0;
  int configuration_total = 0;
  std::string message;
};

using SweepCallback = std::function<void(const SweepProgress&)>;

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

struct SweepCaseKey {
  std::string scene;
  int width = 0;
  int height = 0;
  int spp = 0;
  int depth = 0;
  int tile_size = 0;

  bool operator<(const SweepCaseKey& other) const;
};

struct SweepCaseGroup {
  SweepCaseKey key;
  std::vector<BenchmarkRecord> rows;
  std::vector<struct SweepConfigSummary> configs;
};

struct SweepConfigKey {
  int threads = 0;
  std::string schedule;

  bool operator<(const SweepConfigKey& other) const;
};

struct SweepConfigSummary {
  SweepConfigKey key;
  std::size_t row_count = 0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  double avg_ms = 0.0;
  double median_ms = 0.0;
  double stddev_ms = 0.0;
  double avg_pixels_per_sec = 0.0;
  double ideal_linear_speedup = 0.0;
  double ideal_runtime_ms = 0.0;
};

struct SweepCaseSummary {
  SweepCaseKey key;
  std::size_t row_count = 0;
  std::size_t config_count = 0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  double avg_ms = 0.0;
  double median_ms = 0.0;
  double stddev_ms = 0.0;
  double avg_pixels_per_sec = 0.0;
  double baseline_serial_ms = 0.0;
  double ideal_linear_speedup = 0.0;
  double ideal_runtime_ms = 0.0;
  double best_speedup = 0.0;
  double best_config_median_ms = 0.0;
  SweepConfigKey best_config;
};

struct SweepDataset {
  std::filesystem::path root_dir;
  std::string manifest_text;
  std::vector<std::filesystem::path> csv_files;
  std::vector<BenchmarkRecord> rows;
  std::vector<SweepCaseGroup> cases;
  std::vector<SweepCaseSummary> summaries;
};

std::vector<std::pair<int, int>> parse_resolution_list(const std::string& text);
std::vector<SweepCase> expand_sweep(const SweepConfig& sweep);
bool write_sweep_manifest(const SweepConfig& sweep, const std::string& path);
bool run_sweep(const SweepConfig& sweep, const RenderConfig& base_config, const SweepCallback& on_progress = {}, std::atomic<bool>* cancel_token = nullptr, bool* cancelled = nullptr, std::size_t start_case_index = 0);
bool load_sweep_dataset(const std::filesystem::path& root_dir, SweepDataset& dataset, std::string& error);

}  // namespace pr
