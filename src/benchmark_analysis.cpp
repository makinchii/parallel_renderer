#include "benchmark_analysis.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace pr {
namespace {

int schedule_rank(const std::string& schedule) {
  if (schedule == "serial") return 0;
  if (schedule == "static") return 1;
  if (schedule == "dynamic") return 2;
  return 3;
}

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  std::stringstream ss(line);
  while (std::getline(ss, field, ',')) fields.push_back(field);
  return fields;
}

std::optional<BenchmarkRecord> parse_row(const std::string& row, const std::filesystem::path& source) {
  auto fields = split_csv_line(row);
  if (fields.size() < 11) return std::nullopt;
  try {
    BenchmarkRecord rec;
    rec.scene = fields[0];
    rec.width = std::stoi(fields[1]);
    rec.height = std::stoi(fields[2]);
    rec.spp = std::stoi(fields[3]);
    rec.depth = std::stoi(fields[4]);
    rec.threads = std::stoi(fields[5]);
    rec.schedule = fields[6];
    rec.tile_size = std::stoi(fields[7]);
    rec.run = std::stoi(fields[8]);
    rec.milliseconds = std::stod(fields[9]);
    rec.pixels_per_sec = std::stod(fields[10]);
    rec.source_file = source.string();
    rec.raw_row = row;
    return rec;
  } catch (...) {
    return std::nullopt;
  }
}

double compute_median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t mid = values.size() / 2;
  return values.size() % 2 == 0 ? (values[mid - 1] + values[mid]) * 0.5 : values[mid];
}

double compute_stddev(const std::vector<double>& values, double mean) {
  if (values.empty()) return 0.0;
  double variance = 0.0;
  for (double v : values) variance += (v - mean) * (v - mean);
  return std::sqrt(variance / static_cast<double>(values.size()));
}

}  // namespace

bool BenchmarkConfigKey::operator<(const BenchmarkConfigKey& other) const {
  return std::make_tuple(threads, schedule_rank(schedule), schedule) <
         std::make_tuple(other.threads, schedule_rank(other.schedule), other.schedule);
}

std::string benchmark_config_label(const BenchmarkConfigKey& key) {
  std::ostringstream oss;
  oss << key.threads << " threads | " << key.schedule;
  return oss.str();
}

std::string benchmark_workload_label(const BenchmarkWorkloadSummary& summary) {
  std::ostringstream oss;
  oss << summary.scene << " | " << summary.width << "x" << summary.height << " | spp " << summary.spp << " | depth " << summary.depth << " | tile " << summary.tile_size;
  return oss.str();
}

bool load_benchmark_csv(const std::filesystem::path& path, BenchmarkDataset& dataset, std::string& error) {
  dataset = BenchmarkDataset{};
  dataset.csv_path = path;
  std::ifstream in(path);
  if (!in) {
    error = "failed to open csv";
    return false;
  }

  std::string line;
  if (!std::getline(in, line)) {
    error = "empty csv";
    return false;
  }
  dataset.header_text = line;

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    auto parsed = parse_row(line, path);
    if (parsed) dataset.rows.push_back(*parsed);
  }

  if (dataset.rows.empty()) {
    error = "no data rows";
    return false;
  }

  std::sort(dataset.rows.begin(), dataset.rows.end(), [](const BenchmarkRecord& a, const BenchmarkRecord& b) {
    return std::make_tuple(a.scene, a.width, a.height, a.spp, a.depth, a.tile_size, a.threads, schedule_rank(a.schedule), a.schedule, a.run) <
           std::make_tuple(b.scene, b.width, b.height, b.spp, b.depth, b.tile_size, b.threads, schedule_rank(b.schedule), b.schedule, b.run);
  });

  const auto& first = dataset.rows.front();
  dataset.workload.scene = first.scene;
  dataset.workload.width = first.width;
  dataset.workload.height = first.height;
  dataset.workload.spp = first.spp;
  dataset.workload.depth = first.depth;
  dataset.workload.tile_size = first.tile_size;
  dataset.workload.row_count = dataset.rows.size();

  std::map<BenchmarkConfigKey, std::vector<const BenchmarkRecord*>> grouped;
  for (const auto& row : dataset.rows) grouped[BenchmarkConfigKey{row.threads, row.schedule}].push_back(&row);

  dataset.configs.clear();
  dataset.configs.reserve(grouped.size());
  for (auto& [key, config_rows] : grouped) {
    BenchmarkConfigSummary summary;
    summary.key = key;
    summary.row_count = config_rows.size();
    if (!config_rows.empty()) {
      std::vector<double> samples;
      double total_ms = 0.0;
      double total_pps = 0.0;
      summary.min_ms = config_rows.front()->milliseconds;
      summary.max_ms = config_rows.front()->milliseconds;
      for (const auto* row : config_rows) {
        summary.min_ms = std::min(summary.min_ms, row->milliseconds);
        summary.max_ms = std::max(summary.max_ms, row->milliseconds);
        total_ms += row->milliseconds;
        total_pps += row->pixels_per_sec;
        samples.push_back(row->milliseconds);
      }
      summary.avg_ms = total_ms / static_cast<double>(config_rows.size());
      summary.avg_pixels_per_sec = total_pps / static_cast<double>(config_rows.size());
      summary.median_ms = compute_median(samples);
      summary.stddev_ms = compute_stddev(samples, summary.avg_ms);
    }
    dataset.configs.push_back(summary);
  }

  if (!dataset.configs.empty()) {
    dataset.workload.config_count = dataset.configs.size();
    const auto baseline = std::find_if(dataset.configs.begin(), dataset.configs.end(), [](const BenchmarkConfigSummary& cfg) {
      return cfg.key.threads == 1 && cfg.key.schedule == "serial";
    });
    const auto best = std::min_element(dataset.configs.begin(), dataset.configs.end(), [](const BenchmarkConfigSummary& a, const BenchmarkConfigSummary& b) {
      return a.median_ms < b.median_ms;
    });
    if (baseline != dataset.configs.end()) {
      dataset.workload.baseline_config = baseline->key;
      dataset.workload.baseline_serial_ms = baseline->median_ms;
    } else {
      dataset.workload.baseline_config = dataset.configs.front().key;
      dataset.workload.baseline_serial_ms = dataset.configs.front().median_ms;
    }
    dataset.workload.ideal_linear_speedup = std::max(1, std::max_element(dataset.rows.begin(), dataset.rows.end(), [](const BenchmarkRecord& a, const BenchmarkRecord& b) {
      return a.threads < b.threads;
    })->threads);
    dataset.workload.ideal_runtime_ms = dataset.workload.baseline_serial_ms / static_cast<double>(dataset.workload.ideal_linear_speedup);
    if (best != dataset.configs.end() && dataset.workload.baseline_serial_ms > 0.0) {
      dataset.workload.best_config = best->key;
      dataset.workload.best_observed_ms = best->median_ms;
      dataset.workload.best_observed_speedup = dataset.workload.baseline_serial_ms / best->median_ms;
    }
    for (auto& cfg : dataset.configs) {
      if (cfg.key.threads > 0 && cfg.median_ms > 0.0) cfg.efficiency = (dataset.workload.baseline_serial_ms / cfg.median_ms) / static_cast<double>(cfg.key.threads);
      if (cfg.key.threads > 0 && cfg.median_ms > 0.0) cfg.observed_speedup = dataset.workload.baseline_serial_ms / cfg.median_ms;
    }
  }

  return true;
}

}  // namespace pr
