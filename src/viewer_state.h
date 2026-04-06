// Viewer-shared state and tile update types.
// This boundary owns presentation state only and must not depend on render engine internals.

#pragma once

#include "render_core.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace pr {

enum class TileState { Queued, InProgress, Completed };

struct Tile {
  int id = 0;
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
};

struct TileStatus {
  TileState state = TileState::Queued;
  int worker_id = -1;
  double start_ms = 0.0;
  double end_ms = 0.0;
};

struct TileUpdate {
  int id = 0;
  TileStatus status;
  std::vector<std::uint32_t> pixels;
};

struct ViewerState {
  Framebuffer framebuffer;
  std::vector<Tile> tiles;
  std::vector<TileStatus> tile_status;
  std::deque<TileUpdate> tile_updates;
  std::atomic<int> tiles_completed{0};
  std::atomic<bool> paused{false};
  std::atomic<bool> cancelled{false};
  std::atomic<bool> render_complete{false};
  std::mutex mutex;
  std::deque<std::string> logs;

  ViewerState();
  explicit ViewerState(int w, int h);

  Framebuffer& framebuffer_ref();
  const Framebuffer& framebuffer_ref() const;
};

}  // namespace pr
