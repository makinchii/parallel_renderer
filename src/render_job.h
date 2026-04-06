#pragma once

#include "camera.h"
#include "render_core.h"
#include "viewer_state.h"

#include <memory>
#include <mutex>

namespace pr {

struct RenderProgress {
  int tiles_completed = 0;
  int total_tiles = 0;
  double elapsed_seconds = 0.0;
  bool paused = false;
  bool cancelled = false;
  bool complete = false;
  std::string scene_name;
  std::string schedule_mode;
  int thread_count = 0;
};

struct RenderOverlaySnapshot {
  std::vector<Tile> tiles;
  std::vector<TileStatus> tile_status;
};

class RenderJob {
 public:
  RenderJob(Scene scene, Camera camera, RenderConfig config);
  ~RenderJob();

  void start();
  void pause();
  void resume();
  void cancel();

  bool is_complete() const;
  RenderProgress progress() const;
  RenderOverlaySnapshot overlay_snapshot() const;
  std::vector<TileUpdate> consume_tile_updates() const;
  Framebuffer snapshot() const;
  void join();

 private:
  Scene scene_;
  Camera camera_;
  RenderConfig config_;
  mutable ViewerState viewer_state_;
  mutable std::thread thread_;
  mutable std::mutex timing_mutex_;
  std::chrono::steady_clock::time_point start_time_{};
  std::chrono::steady_clock::time_point pause_started_{};
  std::chrono::nanoseconds paused_total_{0};
  bool pause_active_ = false;
  mutable double completed_elapsed_seconds_ = 0.0;
  mutable bool completed_elapsed_latched_ = false;
  bool started_ = false;
};

class RenderController {
 public:
  std::unique_ptr<RenderJob> start_job(const Scene& scene, const Camera& camera, const RenderConfig& config) const;
};

}  // namespace pr
