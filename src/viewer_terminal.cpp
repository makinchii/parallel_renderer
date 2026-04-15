#include "viewer_terminal.h"

#include "viewer_state.h"

#include "render_engine.h"

#include <iostream>

namespace pr {

static std::string ansi_rgb_fg(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  std::ostringstream oss;
  oss << "\033[38;2;" << static_cast<int>(r) << ';' << static_cast<int>(g) << ';' << static_cast<int>(b) << 'm';
  return oss.str();
}

static std::string ansi_rgb_bg(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  std::ostringstream oss;
  oss << "\033[48;2;" << static_cast<int>(r) << ';' << static_cast<int>(g) << ';' << static_cast<int>(b) << 'm';
  return oss.str();
}

static std::string color_preview(const Framebuffer& framebuffer, int out_w = 80) {
  if (framebuffer.width <= 0 || framebuffer.height <= 0) return {};
  std::ostringstream oss;
  int out_h = std::max(1, out_w * framebuffer.height / std::max(1, framebuffer.width * 2));
  for (int oy = 0; oy < out_h; ++oy) {
    int y0 = std::min(framebuffer.height - 1, (oy * 2) * framebuffer.height / std::max(1, out_h * 2));
    int y1 = std::min(framebuffer.height - 1, y0 + 1);
    for (int ox = 0; ox < out_w; ++ox) {
      int x = std::min(framebuffer.width - 1, ox * framebuffer.width / out_w);
      auto top = unpack_rgba(framebuffer.at(x, y0));
      auto bottom = unpack_rgba(framebuffer.at(x, y1));
      oss << ansi_rgb_fg(top[0], top[1], top[2]) << ansi_rgb_bg(bottom[0], bottom[1], bottom[2]) << "▀";
    }
    oss << "\033[0m\n";
  }
  return oss.str();
}

static std::string tile_state_char(TileState state) {
  switch (state) {
    case TileState::Queued: return ".";
    case TileState::InProgress: return "*";
    case TileState::Completed: return "#";
  }
  return "?";
}

static std::string draw_tile_overlay(const ViewerState& state, int columns = 24) {
  if (state.tiles.empty()) return {};
  int rows = static_cast<int>((state.tiles.size() + columns - 1) / columns);
  std::ostringstream oss;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < columns; ++c) {
      int index = r * columns + c;
      if (index >= static_cast<int>(state.tiles.size())) break;
      oss << tile_state_char(state.tile_status[index].state);
    }
    oss << '\n';
  }
  return oss.str();
}

void print_viewer_frame(ViewerState& state, const RenderStats& stats, const RenderConfig& config) {
  std::lock_guard<std::mutex> lock(state.mutex);
  std::lock_guard<std::mutex> framebuffer_lock(state.framebuffer_mutex);
  const Framebuffer& framebuffer = state.framebuffer_ref();
  std::cout << "\033[2J\033[H";
  std::cout << "Parallel Renderer Viewer\n";
  std::cout << format_stats_line(stats, config.width, config.height, config.samples_per_pixel, config.max_depth) << "\n\n";
  std::cout << draw_tile_overlay(state) << "\n";
  std::cout << color_preview(framebuffer) << "\n";
  std::cout << "Logs:\n";
  for (const auto& line : state.logs) std::cout << "- " << line << "\n";
  std::cout.flush();
}

void run_viewer_terminal(const Scene& scene, const Camera& camera, const RenderConfig& config, Framebuffer& framebuffer) {
  ViewerState state(config.width, config.height);
  RenderStats stats;
  std::thread render_thread([&] { stats = render(scene, camera, framebuffer, config, &state); });
  stats.scene_name = config.scene_name;
  stats.schedule_mode = config.schedule_mode;
  stats.thread_count = static_cast<std::size_t>(resolved_thread_count(config));

  int refresh_ms = std::clamp(config.viewer_refresh_ms, 16, 250);
  auto last_draw = std::chrono::steady_clock::now() - std::chrono::milliseconds(refresh_ms);
  while (!state.render_complete.load()) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw).count() >= refresh_ms) {
      print_viewer_frame(state, stats, config);
      last_draw = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  render_thread.join();
  print_viewer_frame(state, stats, config);
}

}  // namespace pr
