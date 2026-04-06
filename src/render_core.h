#pragma once

#include "scene.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace pr {

struct Framebuffer {
  int width = 0;
  int height = 0;
  std::vector<std::uint32_t> pixels;

  Framebuffer() = default;
  Framebuffer(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * static_cast<size_t>(h), 0u) {}

  void resize(int w, int h) {
    width = w;
    height = h;
    pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0u);
  }

  std::uint32_t& at(int x, int y) { return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
  std::uint32_t at(int x, int y) const { return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
};

struct RenderConfig {
  int width = 800;
  int height = 450;
  int samples_per_pixel = 16;
  int max_depth = 8;
  int thread_count = 0;
  int tile_size = 32;
  std::string schedule_mode = "dynamic";
  std::string scene_name = "simple";
  std::uint64_t seed = 1;
  bool viewer_enabled = false;
  bool save_output = false;
  std::string output_filename = "output.ppm";
  int viewer_refresh_ms = 100;
  int benchmark_runs = 5;
  std::string csv_output = "results.csv";
};

struct RenderStats {
  double milliseconds = 0.0;
  std::size_t total_tiles = 0;
  std::size_t tiles_completed = 0;
  std::size_t pixels = 0;
  std::size_t thread_count = 0;
  std::string schedule_mode;
  std::string scene_name;
};

inline std::array<std::uint8_t, 4> unpack_rgba(std::uint32_t p) {
  return {static_cast<std::uint8_t>((p >> 16) & 0xff), static_cast<std::uint8_t>((p >> 8) & 0xff), static_cast<std::uint8_t>(p & 0xff), 255u};
}

inline int resolved_thread_count(const RenderConfig& config) {
  if (config.thread_count > 0) return config.thread_count;
  return std::max(1u, std::thread::hardware_concurrency());
}

inline std::string human_time(double ms) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << ms << " ms";
  return oss.str();
}

inline std::string format_stats_line(const RenderStats& stats, int width, int height, int spp, int depth) {
  std::ostringstream oss;
  double pixels_per_sec = stats.milliseconds > 0.0 ? (1000.0 * static_cast<double>(stats.pixels) / stats.milliseconds) : 0.0;
  oss << "scene=" << stats.scene_name
      << " mode=" << stats.schedule_mode
      << " threads=" << stats.thread_count
      << " size=" << width << 'x' << height
      << " spp=" << spp
      << " depth=" << depth
      << " tiles=" << stats.tiles_completed << '/' << stats.total_tiles
      << " time=" << human_time(stats.milliseconds)
      << " px/s=" << std::fixed << std::setprecision(0) << pixels_per_sec;
  return oss.str();
}

inline std::vector<int> parse_int_list(const std::string& text) {
  std::vector<int> values;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) values.push_back(std::stoi(item));
  }
  return values;
}

inline std::vector<std::string> parse_string_list(const std::string& text) {
  std::vector<std::string> values;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) values.push_back(item);
  }
  return values;
}

}  // namespace pr
