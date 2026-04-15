#include "sweep_runner.h"

#include "camera.h"
#include "csv_writer.h"
#include "scene_registry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <cmath>

namespace pr {
namespace {

std::string join_strings(const std::vector<std::string>& values) {
  std::ostringstream oss;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) oss << ',';
    oss << values[i];
  }
  return oss.str();
}

std::string join_ints(const std::vector<int>& values) {
  std::ostringstream oss;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) oss << ',';
    oss << values[i];
  }
  return oss.str();
}

std::string join_resolutions(const std::vector<std::pair<int, int>>& values) {
  std::ostringstream oss;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) oss << ',';
    oss << values[i].first << 'x' << values[i].second;
  }
  return oss.str();
}

std::vector<std::pair<int, int>> parse_resolution_list_impl(const std::string& text) {
  std::vector<std::pair<int, int>> values;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty()) continue;
    auto pos = item.find('x');
    if (pos == std::string::npos) pos = item.find('X');
    if (pos == std::string::npos) continue;
    int w = std::stoi(item.substr(0, pos));
    int h = std::stoi(item.substr(pos + 1));
    values.emplace_back(w, h);
  }
  return values;
}

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  std::stringstream ss(line);
  while (std::getline(ss, field, ',')) fields.push_back(field);
  return fields;
}

std::string read_file_text(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

std::optional<BenchmarkRecord> parse_benchmark_row(const std::string& row, const std::filesystem::path& source) {
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

SweepCaseKey make_case_key(const BenchmarkRecord& row) {
  return SweepCaseKey{row.scene, row.width, row.height, row.spp, row.depth, row.tile_size};
}

}  // namespace

bool SweepCaseKey::operator<(const SweepCaseKey& other) const {
  return std::tie(scene, width, height, spp, depth, tile_size) < std::tie(other.scene, other.width, other.height, other.spp, other.depth, other.tile_size);
}

bool SweepConfigKey::operator<(const SweepConfigKey& other) const {
  return std::tie(threads, schedule) < std::tie(other.threads, other.schedule);
}

std::vector<std::pair<int, int>> parse_resolution_list(const std::string& text) {
  return parse_resolution_list_impl(text);
}

std::vector<SweepCase> expand_sweep(const SweepConfig& sweep) {
  std::vector<SweepCase> cases;
  for (const auto& scene : sweep.scenes) {
    for (const auto& [width, height] : sweep.resolutions) {
      for (int spp : sweep.spp_values) {
        for (int tile_size : sweep.tile_sizes) {
          cases.push_back(SweepCase{scene, width, height, spp, tile_size, sweep.thread_counts, sweep.schedules, sweep.runs});
        }
      }
    }
  }
  return cases;
}

static std::vector<SweepConfigSummary> summarize_configs(const std::vector<BenchmarkRecord>& rows) {
  std::map<SweepConfigKey, std::vector<const BenchmarkRecord*>> grouped;
  for (const auto& row : rows) {
    grouped[SweepConfigKey{row.threads, row.schedule}].push_back(&row);
  }

  std::vector<SweepConfigSummary> summaries;
  summaries.reserve(grouped.size());
  for (auto& [key, config_rows] : grouped) {
    SweepConfigSummary summary;
    summary.key = key;
    summary.row_count = config_rows.size();
    if (!config_rows.empty()) {
      std::vector<double> samples;
      samples.reserve(config_rows.size());
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
      std::sort(samples.begin(), samples.end());
      const std::size_t mid = samples.size() / 2;
      summary.median_ms = samples.size() % 2 == 0 ? (samples[mid - 1] + samples[mid]) * 0.5 : samples[mid];
      double variance = 0.0;
      for (double ms : samples) variance += (ms - summary.avg_ms) * (ms - summary.avg_ms);
      summary.stddev_ms = std::sqrt(variance / static_cast<double>(samples.size()));
    }
    summaries.push_back(summary);
  }
  return summaries;
}

static std::string case_label(const SweepCaseSummary& summary) {
  std::ostringstream oss;
  oss << summary.key.scene << " | " << summary.key.width << "x" << summary.key.height << " | spp " << summary.key.spp << " | depth " << summary.key.depth << " | tile " << summary.key.tile_size;
  return oss.str();
}

bool write_sweep_manifest(const SweepConfig& sweep, const std::string& path) {
  std::ofstream out(path);
  if (!out) return false;
  out << "scenes=" << join_strings(sweep.scenes) << '\n';
  out << "resolutions=" << join_resolutions(sweep.resolutions) << '\n';
  out << "spp=" << join_ints(sweep.spp_values) << '\n';
  out << "tile_sizes=" << join_ints(sweep.tile_sizes) << '\n';
  out << "threads=" << join_ints(sweep.thread_counts) << '\n';
  out << "schedules=" << join_strings(sweep.schedules) << '\n';
  out << "runs=" << sweep.runs << '\n';
  out << "output_dir=" << sweep.output_dir << '\n';
  return static_cast<bool>(out);
}

bool run_sweep(const SweepConfig& sweep, const RenderConfig& base_config, const SweepCallback& on_progress, std::atomic<bool>* cancel_token, bool* cancelled, std::size_t start_case_index) {
  std::filesystem::create_directories(sweep.output_dir);
  const std::string manifest_path = sweep.output_dir + "/manifest.txt";
  if (!write_sweep_manifest(sweep, manifest_path)) return false;

  const auto cases = expand_sweep(sweep);
  for (std::size_t i = start_case_index; i < cases.size(); ++i) {
    if (cancel_token && cancel_token->load()) {
      if (cancelled) *cancelled = true;
      return false;
    }
    const auto& sweep_case = cases[i];
    Scene scene = build_scene(sweep_case.scene);
    Camera camera = make_camera(sweep_case.width, sweep_case.height);
    RenderConfig cfg = base_config;
    cfg.scene_name = sweep_case.scene;
    cfg.width = sweep_case.width;
    cfg.height = sweep_case.height;
    cfg.samples_per_pixel = sweep_case.spp;
    cfg.tile_size = sweep_case.tile_size;
    cfg.benchmark_runs = sweep_case.runs;
    cfg.csv_output = sweep.output_dir + "/" + sweep_case.scene + "_" + std::to_string(sweep_case.width) + "x" + std::to_string(sweep_case.height) + "_spp" + std::to_string(sweep_case.spp) + ".csv";
    auto rows = run_benchmarks(scene, camera, cfg, sweep_case.thread_counts, sweep_case.schedules, {}, nullptr, false);
    if (!write_benchmark_csv(cfg.csv_output, rows)) return false;
    if (on_progress) {
      on_progress(SweepProgress{static_cast<int>(i) + 1, static_cast<int>(cases.size()), cfg.csv_output});
    }
  }
  if (cancelled) *cancelled = false;
  return true;
}

bool load_sweep_dataset(const std::filesystem::path& root_dir, SweepDataset& dataset, std::string& error) {
  dataset = SweepDataset{};
  dataset.root_dir = root_dir;
  const auto manifest_path = root_dir / "manifest.txt";
  dataset.manifest_text = read_file_text(manifest_path);
  if (dataset.manifest_text.empty()) {
    error = "missing manifest.txt";
    return false;
  }

  try {
    for (const auto& entry : std::filesystem::directory_iterator(root_dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".csv") continue;
      dataset.csv_files.push_back(entry.path());
      std::ifstream in(entry.path());
      if (!in) continue;
      std::string line;
      bool first = true;
      while (std::getline(in, line)) {
        if (first) {
          first = false;
          continue;
        }
        if (line.empty()) continue;
        auto parsed = parse_benchmark_row(line, entry.path());
        if (parsed) dataset.rows.push_back(*parsed);
      }
    }
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }

  if (dataset.csv_files.empty()) {
    error = "no CSV files found";
    return false;
  }

  std::map<SweepCaseKey, SweepCaseGroup> groups;
  for (const auto& row : dataset.rows) {
    auto key = make_case_key(row);
    auto& group = groups[key];
    group.key = key;
    group.rows.push_back(row);
  }
  dataset.cases.clear();
  dataset.summaries.clear();
  for (auto& [key, group] : groups) {
    std::sort(group.rows.begin(), group.rows.end(), [](const BenchmarkRecord& a, const BenchmarkRecord& b) {
      return std::tie(a.threads, a.schedule, a.run) < std::tie(b.threads, b.schedule, b.run);
    });
    group.configs = summarize_configs(group.rows);
    SweepCaseSummary summary;
    summary.key = key;
    summary.row_count = group.rows.size();
    summary.config_count = group.configs.size();
    if (!group.rows.empty()) {
      summary.min_ms = group.rows.front().milliseconds;
      summary.max_ms = group.rows.front().milliseconds;
      double total_ms = 0.0;
      double total_pps = 0.0;
      std::vector<double> samples;
      for (const auto& row : group.rows) {
        summary.min_ms = std::min(summary.min_ms, row.milliseconds);
        summary.max_ms = std::max(summary.max_ms, row.milliseconds);
        total_ms += row.milliseconds;
        total_pps += row.pixels_per_sec;
        samples.push_back(row.milliseconds);
      }
      summary.avg_ms = total_ms / static_cast<double>(group.rows.size());
      summary.avg_pixels_per_sec = total_pps / static_cast<double>(group.rows.size());
      std::sort(samples.begin(), samples.end());
      const std::size_t mid = samples.size() / 2;
      summary.median_ms = samples.size() % 2 == 0 ? (samples[mid - 1] + samples[mid]) * 0.5 : samples[mid];
      double variance = 0.0;
      for (double ms : samples) variance += (ms - summary.avg_ms) * (ms - summary.avg_ms);
      summary.stddev_ms = std::sqrt(variance / static_cast<double>(samples.size()));

      const auto serial = std::find_if(group.configs.begin(), group.configs.end(), [](const SweepConfigSummary& cfg) {
        return cfg.key.threads == 1 && cfg.key.schedule == "serial";
      });
      if (serial != group.configs.end()) {
        summary.baseline_serial_ms = serial->median_ms;
      } else if (!group.configs.empty()) {
        summary.baseline_serial_ms = group.configs.front().median_ms;
      }
      const auto best = std::min_element(group.configs.begin(), group.configs.end(), [](const SweepConfigSummary& a, const SweepConfigSummary& b) {
        return a.median_ms < b.median_ms;
      });
      if (best != group.configs.end() && summary.baseline_serial_ms > 0.0) {
        summary.best_config = best->key;
        summary.best_config_median_ms = best->median_ms;
        summary.best_speedup = summary.baseline_serial_ms / best->median_ms;
      }
      int max_threads = 1;
      for (const auto& cfg : group.configs) max_threads = std::max(max_threads, cfg.key.threads);
      summary.ideal_linear_speedup = static_cast<double>(max_threads);
      summary.ideal_runtime_ms = summary.baseline_serial_ms > 0.0 ? summary.baseline_serial_ms / static_cast<double>(max_threads) : 0.0;
    }
    dataset.cases.push_back(std::move(group));
    dataset.summaries.push_back(summary);
  }
  return true;
}

}  // namespace pr
