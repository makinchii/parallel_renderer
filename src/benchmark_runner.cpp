#include "benchmark_runner.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace pr {

std::vector<std::string> run_benchmarks(const Scene& scene, const Camera& camera, const RenderConfig& config, const std::vector<int>& thread_counts,
                                        const std::vector<std::string>& schedules, const BenchmarkCallback& on_progress, std::atomic<bool>* cancel_token,
                                        bool emit_stdout) {
  std::vector<std::string> rows;
  const int run_total = std::max(1, config.benchmark_runs);
  const int configuration_total = static_cast<int>(std::max<std::size_t>(1, thread_counts.size() * schedules.size()));
  int configuration_index = 0;
  for (const auto schedule : schedules) {
    for (const auto threads : thread_counts) {
      ++configuration_index;
      if (cancel_token && cancel_token->load()) {
        if (on_progress) {
          on_progress(BenchmarkEvent{BenchmarkEvent::Kind::Cancelled, scene.name, schedule, threads, 0, run_total, configuration_index - 1, configuration_total, {}, {}});
        }
        return rows;
      }
      if (on_progress) {
        on_progress(BenchmarkEvent{BenchmarkEvent::Kind::ConfigurationStarted, scene.name, schedule, threads, 0, run_total, configuration_index, configuration_total, {}, {}});
      }
      for (int run = 1; run <= run_total; ++run) {
        if (cancel_token && cancel_token->load()) {
          if (on_progress) {
            on_progress(BenchmarkEvent{BenchmarkEvent::Kind::Cancelled, scene.name, schedule, threads, run, run_total, configuration_index, configuration_total, {}, {}});
          }
          return rows;
        }
        if (on_progress) {
          on_progress(BenchmarkEvent{BenchmarkEvent::Kind::RunStarted, scene.name, schedule, threads, run, run_total, configuration_index, configuration_total, {}, {}});
        }
        RenderConfig cfg = config;
        cfg.schedule_mode = schedule;
        cfg.thread_count = threads;
        Framebuffer framebuffer(cfg.width, cfg.height);
        auto stats = render(scene, camera, framebuffer, cfg, nullptr);
        double pps = stats.milliseconds > 0.0 ? (1000.0 * static_cast<double>(stats.pixels) / stats.milliseconds) : 0.0;
        std::ostringstream row;
        row << scene.name << ',' << cfg.width << ',' << cfg.height << ',' << cfg.samples_per_pixel << ',' << cfg.max_depth << ','
            << threads << ',' << schedule << ',' << cfg.tile_size << ',' << run << ',' << std::fixed << std::setprecision(3) << stats.milliseconds << ','
            << std::fixed << std::setprecision(1) << pps;
        auto row_text = row.str();
        rows.push_back(row_text);
        if (emit_stdout) std::cout << format_stats_line(stats, cfg.width, cfg.height, cfg.samples_per_pixel, cfg.max_depth) << '\n';
        if (on_progress) {
          on_progress(BenchmarkEvent{BenchmarkEvent::Kind::RunCompleted, scene.name, schedule, threads, run, run_total, configuration_index, configuration_total, stats, row_text});
        }
      }
      if (on_progress) {
        on_progress(BenchmarkEvent{BenchmarkEvent::Kind::ConfigurationCompleted, scene.name, schedule, threads, 0, run_total, configuration_index, configuration_total, {}, {}});
      }
    }
  }
  if (on_progress) {
    on_progress(BenchmarkEvent{BenchmarkEvent::Kind::Completed, scene.name, {}, 0, 0, run_total, configuration_index, configuration_total, {}, {}});
  }
  return rows;
}

}  // namespace pr
