#include "render_job.h"

#include "render_engine.h"

#include <utility>

namespace pr {

RenderJob::RenderJob(Scene scene, Camera camera, RenderConfig config)
    : scene_(std::move(scene)), camera_(std::move(camera)), config_(std::move(config)), viewer_state_(config_.width, config_.height) {}

RenderJob::~RenderJob() { join(); }

void RenderJob::start() {
  if (started_) return;
  started_ = true;
  start_time_ = std::chrono::steady_clock::now();
  paused_total_ = std::chrono::nanoseconds{0};
  pause_active_ = false;
  completed_elapsed_latched_ = false;
  completed_elapsed_seconds_ = 0.0;
  thread_ = std::thread([this] {
    Framebuffer& framebuffer = viewer_state_.framebuffer_ref();
    render(scene_, camera_, framebuffer, config_, &viewer_state_);
  });
}

void RenderJob::pause() {
  viewer_state_.paused.store(true);
  std::lock_guard<std::mutex> lock(timing_mutex_);
  if (!pause_active_) {
    pause_started_ = std::chrono::steady_clock::now();
    pause_active_ = true;
  }
}

void RenderJob::resume() {
  viewer_state_.paused.store(false);
  std::lock_guard<std::mutex> lock(timing_mutex_);
  if (pause_active_) {
    paused_total_ += std::chrono::steady_clock::now() - pause_started_;
    pause_active_ = false;
  }
}

void RenderJob::cancel() { viewer_state_.cancelled.store(true); }

bool RenderJob::is_complete() const { return viewer_state_.render_complete.load(); }

RenderProgress RenderJob::progress() const {
  RenderProgress progress;
  progress.tiles_completed = viewer_state_.tiles_completed.load();
  progress.total_tiles = static_cast<int>(viewer_state_.tiles.size());
  progress.paused = viewer_state_.paused.load();
  progress.cancelled = viewer_state_.cancelled.load();
  progress.complete = viewer_state_.render_complete.load();
  progress.scene_name = config_.scene_name;
  progress.schedule_mode = config_.schedule_mode;
  progress.thread_count = resolved_thread_count(config_);
  std::lock_guard<std::mutex> lock(timing_mutex_);
  auto now = std::chrono::steady_clock::now();
  auto active_elapsed = now - start_time_ - paused_total_;
  if (pause_active_) active_elapsed -= now - pause_started_;
  double elapsed_seconds = std::chrono::duration<double>(active_elapsed).count();
  if (progress.complete) {
    if (!completed_elapsed_latched_) {
      completed_elapsed_seconds_ = elapsed_seconds;
      completed_elapsed_latched_ = true;
    }
    progress.elapsed_seconds = completed_elapsed_seconds_;
  } else {
    progress.elapsed_seconds = elapsed_seconds;
  }
  return progress;
}

RenderOverlaySnapshot RenderJob::overlay_snapshot() const {
  RenderOverlaySnapshot snapshot;
  std::lock_guard<std::mutex> lock(viewer_state_.mutex);
  snapshot.tiles = viewer_state_.tiles;
  snapshot.tile_status = viewer_state_.tile_status;
  return snapshot;
}

std::vector<TileUpdate> RenderJob::consume_tile_updates() const {
  std::vector<TileUpdate> updates;
  std::lock_guard<std::mutex> lock(viewer_state_.mutex);
  while (!viewer_state_.tile_updates.empty()) {
    updates.push_back(std::move(viewer_state_.tile_updates.front()));
    viewer_state_.tile_updates.pop_front();
  }
  return updates;
}

Framebuffer RenderJob::snapshot() const {
  std::lock_guard<std::mutex> lock(viewer_state_.mutex);
  return viewer_state_.framebuffer_ref();
}

void RenderJob::join() {
  if (thread_.joinable()) thread_.join();
}

std::unique_ptr<RenderJob> RenderController::start_job(const Scene& scene, const Camera& camera, const RenderConfig& config) const {
  auto job = std::make_unique<RenderJob>(scene, camera, config);
  job->start();
  return job;
}

}  // namespace pr
