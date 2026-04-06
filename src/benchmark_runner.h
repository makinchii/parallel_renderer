// Benchmark orchestration boundary.
// Depends on render engine and core types, but not on UI or output writers.

#pragma once

#include "render_engine.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace pr {

struct BenchmarkEvent {
  enum class Kind {
    ConfigurationStarted,
    RunStarted,
    RunCompleted,
    ConfigurationCompleted,
    Completed,
    Cancelled,
  };

  Kind kind = Kind::RunStarted;
  std::string scene_name;
  std::string schedule_mode;
  int thread_count = 0;
  int run_index = 0;
  int run_total = 0;
  int configuration_index = 0;
  int configuration_total = 0;
  RenderStats stats;
  std::string row;
};

using BenchmarkCallback = std::function<void(const BenchmarkEvent&)>;

std::vector<std::string> run_benchmarks(const Scene& scene, const Camera& camera, const RenderConfig& config, const std::vector<int>& thread_counts,
                                        const std::vector<std::string>& schedules, const BenchmarkCallback& on_progress = {}, std::atomic<bool>* cancel_token = nullptr,
                                        bool emit_stdout = true);

}  // namespace pr
