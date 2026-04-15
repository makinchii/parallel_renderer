#include "gui_app.h"

#include "benchmark_runner.h"
#include "camera.h"
#include "cli.h"
#include "csv_writer.h"
#include "render_job.h"
#include "png_writer.h"
#include "scene_registry.h"
#include "benchmark_analysis.h"
#include "viewer_sdl.h"
#include "viewer_state.h"

#include <SDL3/SDL.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <cmath>
#include <deque>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace pr {
namespace {

struct GuiAppState {
  enum class Page { Home, Render, Benchmark, Results, Charts, Diagnostics };

  RenderController controller;
  RenderConfig config;
  Page page = Page::Home;
  std::vector<std::string> scene_names;
  std::array<std::string, 3> schedules{{"serial", "static", "dynamic"}};
  int selected_scene = 0;
  std::unique_ptr<RenderJob> job;
  bool job_joined = false;
  bool completed_elapsed_latched = false;
  double completed_elapsed_seconds = 0.0;
  bool show_overlay = true;
  bool show_controls = true;
  bool fullscreen = false;
  SDL_FRect windowed_bounds{0.0f, 0.0f, 1280.0f, 800.0f};
  RenderProgress last_progress;
  Framebuffer viewport_framebuffer;
  SDL_Texture* viewport_texture = nullptr;
  bool viewport_dirty = true;
  bool needs_full_upload = true;
  int last_uploaded_tiles_completed = -1;
  RenderOverlaySnapshot overlay_cache;
  bool overlay_cache_valid = false;
  std::vector<TileUpdate> pending_tile_updates;
  bool benchmark_running = false;
  bool benchmark_cancelled = false;
  std::string benchmark_csv_path = "results.csv";
  std::string benchmark_thread_counts = "1,2,4,8";
  std::string benchmark_schedules = "serial,dynamic";
  std::deque<std::string> benchmark_rows;
  std::deque<std::string> benchmark_log;
  std::string results_csv_path = "";
  std::string results_status = "No benchmark CSV selected";
  std::string results_browser_dir = "./results";
  BenchmarkDataset results_dataset;
  int results_compare_a = -1;
  int results_compare_b = -1;
  std::thread results_worker;
  std::atomic<bool> results_loading{false};
  std::mutex results_mutex;
  std::string diagnostics_text;
  std::thread benchmark_worker;
  std::mutex benchmark_mutex;
  std::string benchmark_status = "Idle";
  bool benchmark_done = false;
  std::atomic<bool> benchmark_cancel_token{false};
  std::chrono::steady_clock::time_point home_start_time = std::chrono::steady_clock::now();
  float ui_scale = 1.0f;
  float last_applied_ui_scale = 1.0f;
  ImGuiStyle base_style{};
  ImFont* ui_font = nullptr;
  ImFont* home_ascii_font = nullptr;
  SDL_Texture* home_banner_texture = nullptr;
  int home_banner_texture_width = 0;
  int home_banner_texture_height = 0;
};

static void commit_tile(Framebuffer& framebuffer, const Tile& tile, const std::vector<std::uint32_t>& pixels) {
  for (int y = tile.y0; y < tile.y1; ++y) {
    for (int x = tile.x0; x < tile.x1; ++x) {
      framebuffer.at(x, y) = pixels[static_cast<size_t>(y - tile.y0) * static_cast<size_t>(tile.x1 - tile.x0) + static_cast<size_t>(x - tile.x0)];
    }
  }
}

static const char* render_state_label(const GuiAppState& state) {
  if (state.last_progress.cancelled) return "Cancelled";
  if (state.last_progress.complete) return "Complete";
  if (!state.job) return "Idle";
  if (state.last_progress.paused) return "Paused";
  return "Running";
}

static constexpr const char* kHomeBanner =
    "███████████                                ████  ████           ████     ███████████                           █████\n"
    "▒▒███▒▒▒▒▒███                              ▒▒███ ▒▒███          ▒▒███    ▒▒███▒▒▒▒▒███                         ▒▒███\n"
    " ▒███    ▒███  ██████   ████████   ██████   ▒███  ▒███   ██████  ▒███     ▒███    ▒███   ██████  ████████    ███████   ██████  ████████   ██████  ████████\n"
    " ▒██████████  ▒▒▒▒▒███ ▒▒███▒▒███ ▒▒▒▒▒███  ▒███  ▒███  ███▒▒███ ▒███     ▒██████████   ███▒▒███▒▒███▒▒███  ███▒▒███  ███▒▒███▒▒███▒▒███ ███▒▒███▒▒███▒▒███\n"
    " ▒███▒▒▒▒▒▒    ███████  ▒███ ▒▒▒   ███████  ▒███  ▒███ ▒███████  ▒███     ▒███▒▒▒▒▒███ ▒███████  ▒███ ▒███ ▒███ ▒███ ▒███████  ▒███ ▒▒▒ ▒███████  ▒███ ▒▒▒\n"
    " ▒███         ███▒▒███  ▒███      ███▒▒███  ▒███  ▒███ ▒███▒▒▒   ▒███     ▒███    ▒███ ▒███▒▒▒   ▒███ ▒███ ▒███ ▒███ ▒███▒▒▒   ▒███     ▒███▒▒▒   ▒███\n"
    " █████       ▒▒████████ █████    ▒▒████████ █████ █████▒▒██████  █████    █████   █████▒▒██████  ████ █████▒▒████████▒▒██████  █████    ▒▒██████  █████\n"
    "▒▒▒▒▒         ▒▒▒▒▒▒▒▒ ▒▒▒▒▒      ▒▒▒▒▒▒▒▒ ▒▒▒▒▒ ▒▒▒▒▒  ▒▒▒▒▒▒  ▒▒▒▒▒    ▒▒▒▒▒   ▒▒▒▒▒  ▒▒▒▒▒▒  ▒▒▒▒ ▒▒▒▒▒  ▒▒▒▒▒▒▒▒  ▒▒▒▒▒▒  ▒▒▒▒▒      ▒▒▒▒▒▒  ▒▒▒▒▒";

static std::vector<std::string> home_banner_lines() {
  static const std::vector<std::string> lines = [] {
    std::vector<std::string> out;
    std::istringstream iss(kHomeBanner);
    for (std::string line; std::getline(iss, line);) out.push_back(line);
    return out;
  }();
  return lines;
}

static float compute_ui_scale() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (!viewport) return 1.0f;
  float scale_x = viewport->WorkSize.x / 1280.0f;
  float scale_y = viewport->WorkSize.y / 800.0f;
  float raw_scale = std::min(scale_x, scale_y);
  return std::clamp(raw_scale, 1.0f, 1.85f);
}

static void apply_ui_scale_if_needed(GuiAppState& state) {
  state.ui_scale = compute_ui_scale();
  if (std::abs(state.ui_scale - state.last_applied_ui_scale) < 0.02f) return;

  ImGui::GetStyle() = state.base_style;
  ImGui::GetStyle().ScaleAllSizes(state.ui_scale);
  ImGui::GetStyle().FontSizeBase = (state.ui_font ? state.ui_font->LegacySize : 13.0f) * state.ui_scale;
  state.last_applied_ui_scale = state.ui_scale;
}

static ImFont* load_home_ascii_font(ImGuiIO& io) {
  static const ImWchar home_title_ranges[] = {
      0x20, 0x7E,
      0x2588, 0x2588,
      0x2592, 0x2592,
      0,
  };

  static const std::array<const char*, 5> kFontCandidates{{
      "assets/fonts/DejaVuSansMono.ttf",
      "../assets/fonts/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
  }};

  for (const char* path : kFontCandidates) {
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(path, 32.0f, nullptr, home_title_ranges)) return font;
  }
  return nullptr;
}

static bool ensure_home_banner_texture(SDL_Renderer* renderer, GuiAppState& state, int width, int height) {
  if (!renderer || width <= 0 || height <= 0) return false;
  if (state.home_banner_texture && state.home_banner_texture_width == width && state.home_banner_texture_height == height) return true;
  if (state.home_banner_texture) {
    SDL_DestroyTexture(state.home_banner_texture);
    state.home_banner_texture = nullptr;
  }
  state.home_banner_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!state.home_banner_texture) return false;
  state.home_banner_texture_width = width;
  state.home_banner_texture_height = height;

  auto lines = home_banner_lines();
  std::size_t source_w = 0;
  for (const auto& line : lines) source_w = std::max(source_w, line.size());
  if (source_w == 0 || lines.empty()) return false;

  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u);
  float source_aspect = static_cast<float>(source_w) / static_cast<float>(lines.size());
  float target_aspect = static_cast<float>(width) / static_cast<float>(height);
  int draw_w = width;
  int draw_h = height;
  if (target_aspect > source_aspect) {
    draw_h = std::max(1, static_cast<int>(height * 0.88f));
    draw_w = std::max(1, static_cast<int>(draw_h * source_aspect));
  } else {
    draw_w = std::max(1, static_cast<int>(width * 0.96f));
    draw_h = std::max(1, static_cast<int>(draw_w / source_aspect));
  }
  int offset_x = (width - draw_w) / 2;
  int offset_y = (height - draw_h) / 2;
  for (int y = 0; y < draw_h; ++y) {
    std::size_t src_y = std::min<std::size_t>(lines.size() - 1, static_cast<std::size_t>(y) * lines.size() / static_cast<std::size_t>(draw_h));
    for (int x = 0; x < draw_w; ++x) {
      std::size_t src_x = std::min<std::size_t>(source_w - 1, static_cast<std::size_t>(x) * source_w / static_cast<std::size_t>(draw_w));
      if (src_x >= lines[src_y].size()) continue;
      if (lines[src_y][src_x] == ' ') continue;
      float row_t = lines.size() > 1 ? static_cast<float>(src_y) / static_cast<float>(lines.size() - 1) : 0.0f;
      std::uint32_t r = static_cast<std::uint32_t>(220.0f + 25.0f * (1.0f - row_t));
      std::uint32_t g = static_cast<std::uint32_t>(205.0f + 18.0f * (1.0f - row_t));
      std::uint32_t b = static_cast<std::uint32_t>(170.0f + 12.0f * (1.0f - row_t));
      pixels[static_cast<std::size_t>(offset_y + y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(offset_x + x)] = (255u << 24) | (r << 16) | (g << 8) | b;
    }
  }

  void* dst = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(state.home_banner_texture, nullptr, &dst, &pitch)) return false;
  auto* out = static_cast<std::uint32_t*>(dst);
  int pitch_pixels = pitch / static_cast<int>(sizeof(std::uint32_t));
  for (int y = 0; y < height; ++y) {
    std::copy_n(pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width), width, out + static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch_pixels));
  }
  SDL_UnlockTexture(state.home_banner_texture);
  return true;
}

static void draw_home_page(SDL_Renderer* renderer, GuiAppState& state) {
  (void)renderer;

  const auto lines = home_banner_lines();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  const char* credit = "Developed by An";
  const ImU32 title_col = ImGui::GetColorU32(ImVec4(0.96f, 0.96f, 0.94f, 1.0f));
  const ImU32 credit_col = ImGui::GetColorU32(ImVec4(0.88f, 0.89f, 0.93f, 1.0f));

  if (state.home_ascii_font && !lines.empty()) {
    const float base_px = state.home_ascii_font->LegacySize > 0.0f ? state.home_ascii_font->LegacySize : 32.0f;
    float base_block_w = 0.0f;
    for (const auto& line : lines) {
      base_block_w = std::max(base_block_w, state.home_ascii_font->CalcTextSizeA(base_px, 1000000.0f, 0.0f, line.c_str()).x);
    }
    const float base_line_h = base_px;
    const float base_block_h = base_line_h * static_cast<float>(lines.size());

    const float scale_x = (avail.x * 0.94f) / std::max(base_block_w, 1.0f);
    const float scale_y = (avail.y * 0.62f) / std::max(base_block_h, 1.0f);
    const float scale = std::clamp(std::min(scale_x, scale_y), 0.35f, 4.0f);
    const float font_px = base_px * scale;
    const float line_h = font_px;

    float block_w = 0.0f;
    for (const auto& line : lines) {
      block_w = std::max(block_w, state.home_ascii_font->CalcTextSizeA(font_px, 1000000.0f, 0.0f, line.c_str()).x);
    }
    const float block_h = line_h * static_cast<float>(lines.size());
    const float credit_font_px = std::clamp(font_px * 0.13f, 18.0f * state.ui_scale, 40.0f * state.ui_scale);
    const float credit_gap = std::clamp(font_px * 0.18f, 12.0f * state.ui_scale, 30.0f * state.ui_scale);

    const ImVec2 origin{
        start.x + std::max(0.0f, (avail.x - block_w) * 0.5f),
        start.y + std::max(24.0f * state.ui_scale, (avail.y - (block_h + credit_gap + credit_font_px)) * 0.30f),
    };

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
      const ImVec2 p{origin.x, origin.y + static_cast<float>(i) * line_h};
      draw_list->AddText(state.home_ascii_font, font_px, p, title_col, lines[static_cast<std::size_t>(i)].c_str());
    }

    const ImVec2 credit_size = state.ui_font ? state.ui_font->CalcTextSizeA(credit_font_px, 1000000.0f, 0.0f, credit)
                                             : ImGui::CalcTextSize(credit);
    const ImVec2 credit_pos{
        start.x + std::max(0.0f, (avail.x - credit_size.x) * 0.5f),
        origin.y + block_h + credit_gap,
    };
    draw_list->AddText(state.ui_font, credit_font_px, credit_pos, credit_col, credit);
    ImGui::Dummy(ImVec2(avail.x, block_h + credit_size.y + credit_gap + 32.0f * state.ui_scale));
    return;
  }

  const char* fallback_title = "Parallel Renderer";
  const float title_font_px = 32.0f * state.ui_scale;
  const float credit_font_px = std::clamp(title_font_px * 0.13f, 18.0f * state.ui_scale, 40.0f * state.ui_scale);
  const float credit_gap = std::clamp(title_font_px * 0.18f, 12.0f * state.ui_scale, 30.0f * state.ui_scale);
  const ImVec2 title_size = state.ui_font ? state.ui_font->CalcTextSizeA(title_font_px, 1000000.0f, 0.0f, fallback_title)
                                          : ImGui::CalcTextSize(fallback_title);
  const ImVec2 credit_size = state.ui_font ? state.ui_font->CalcTextSizeA(credit_font_px, 1000000.0f, 0.0f, credit)
                                           : ImGui::CalcTextSize(credit);
  const float total_h = title_size.y + credit_gap + credit_size.y;
  const ImVec2 title_pos{
      start.x + std::max(0.0f, (avail.x - title_size.x) * 0.5f),
      start.y + std::max(24.0f * state.ui_scale, (avail.y - (total_h + 24.0f * state.ui_scale)) * 0.35f),
  };
  const ImVec2 credit_pos{
      start.x + std::max(0.0f, (avail.x - credit_size.x) * 0.5f),
      title_pos.y + title_size.y + credit_gap,
  };
  draw_list->AddText(state.ui_font, title_font_px, title_pos, title_col, fallback_title);
  draw_list->AddText(state.ui_font, credit_font_px, credit_pos, credit_col, credit);
  ImGui::Dummy(ImVec2(avail.x, total_h + 32.0f * state.ui_scale));
}

static void print_sdl_video_diagnostics() {
  auto print_env = [](const char* name) {
    const char* value = std::getenv(name);
    std::cerr << name << '=' << (value ? value : "<unset>") << '\n';
  };
  print_env("DISPLAY");
  print_env("WAYLAND_DISPLAY");
  print_env("XDG_RUNTIME_DIR");
  print_env("SDL_VIDEODRIVER");
}

static bool try_sdl_video_init_with_driver(const char* driver_name) {
  if (driver_name) SDL_setenv_unsafe("SDL_VIDEODRIVER", driver_name, 1);
  if (SDL_Init(SDL_INIT_VIDEO)) return true;
  std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
  SDL_Quit();
  return false;
}

static void destroy_texture(SDL_Texture*& texture) {
  if (texture) {
    SDL_DestroyTexture(texture);
    texture = nullptr;
  }
}

static bool ensure_texture(SDL_Renderer* renderer, SDL_Texture*& texture, int width, int height) {
  if (width <= 0 || height <= 0) return false;
  if (texture) {
    float tex_w = 0.0f;
    float tex_h = 0.0f;
    if (SDL_GetTextureSize(texture, &tex_w, &tex_h) && static_cast<int>(tex_w) == width && static_cast<int>(tex_h) == height) return true;
    destroy_texture(texture);
  }
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture) {
    std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
    return false;
  }
  return true;
}

static bool upload_framebuffer(SDL_Texture* texture, const Framebuffer& framebuffer) {
  if (!texture || framebuffer.width <= 0 || framebuffer.height <= 0) return false;
  void* pixels = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
    std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << '\n';
    return false;
  }
  auto* dst = static_cast<std::uint32_t*>(pixels);
  int dst_pitch = pitch / static_cast<int>(sizeof(std::uint32_t));
  for (int y = 0; y < framebuffer.height; ++y) {
    auto* row = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_pitch);
    for (int x = 0; x < framebuffer.width; ++x) row[x] = 0xff000000u | framebuffer.at(x, y);
  }
  SDL_UnlockTexture(texture);
  return true;
}

static bool upload_tile_pixels(SDL_Texture* texture, const Tile& tile, const std::vector<std::uint32_t>& pixels) {
  if (!texture || pixels.empty()) return false;

  SDL_Rect rect{tile.x0, tile.y0, tile.x1 - tile.x0, tile.y1 - tile.y0};
  void* dst_pixels = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(texture, &rect, &dst_pixels, &pitch)) {
    std::cerr << "SDL_LockTexture(tile) failed: " << SDL_GetError() << '\n';
    return false;
  }

  auto* dst = static_cast<std::uint32_t*>(dst_pixels);
  int dst_pitch = pitch / static_cast<int>(sizeof(std::uint32_t));
  int tile_w = rect.w;
  int tile_h = rect.h;
  for (int y = 0; y < tile_h; ++y) {
    for (int x = 0; x < tile_w; ++x) {
      dst[static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_pitch) + static_cast<std::size_t>(x)] =
          0xff000000u | pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(tile_w) + static_cast<std::size_t>(x)];
    }
  }

  SDL_UnlockTexture(texture);
  return true;
}

static void start_render(GuiAppState& state) {
  if (state.job) {
    state.job->cancel();
    state.job->join();
    state.job.reset();
  }

  if (state.selected_scene < 0 || state.selected_scene >= static_cast<int>(state.scene_names.size())) state.selected_scene = 0;
  state.config.scene_name = state.scene_names[static_cast<std::size_t>(state.selected_scene)];
  Scene scene = build_scene(state.config.scene_name);
  Camera camera = make_camera(state.config.width, state.config.height);
  state.viewport_framebuffer.resize(state.config.width, state.config.height);
  state.viewport_dirty = true;
  state.needs_full_upload = true;
  state.last_uploaded_tiles_completed = -1;
  state.last_progress = {};
  state.overlay_cache = {};
  state.overlay_cache_valid = false;
  state.pending_tile_updates.clear();
  state.job = state.controller.start_job(scene, camera, state.config);
  state.job_joined = false;
  state.completed_elapsed_latched = false;
  state.completed_elapsed_seconds = 0.0;
}

static void refresh_progress(GuiAppState& state) {
  if (!state.job) return;

  RenderProgress progress = state.job->progress();
  if (progress.complete) {
    if (!state.completed_elapsed_latched) {
      state.completed_elapsed_seconds = progress.elapsed_seconds;
      state.completed_elapsed_latched = true;
    }
    progress.elapsed_seconds = state.completed_elapsed_seconds;
  }
  state.last_progress = progress;
}

static void update_job_output(GuiAppState& state, SDL_Renderer* renderer) {
  auto refresh_texture = [&]() {
    if (state.viewport_framebuffer.width <= 0 || state.viewport_framebuffer.height <= 0) return;
    if (ensure_texture(renderer, state.viewport_texture, state.viewport_framebuffer.width, state.viewport_framebuffer.height)) {
      if (upload_framebuffer(state.viewport_texture, state.viewport_framebuffer)) {
        state.needs_full_upload = false;
        state.viewport_dirty = false;
      }
    } else {
      state.needs_full_upload = true;
    }
  };

  auto refresh_overlay_cache = [&]() -> bool {
    if (!state.job) return false;
    state.overlay_cache = state.job->overlay_snapshot();
    state.overlay_cache_valid = !state.overlay_cache.tiles.empty() && state.overlay_cache.tiles.size() == state.overlay_cache.tile_status.size();
    return state.overlay_cache_valid;
  };

  auto refresh_overlay_states = [&]() {
    if (!state.job) return;
    refresh_overlay_cache();
  };

  auto consume_tile_updates = [&]() {
    if (!state.job) return;
    auto updates = state.job->consume_tile_updates();
    if (!updates.empty()) {
      state.pending_tile_updates.insert(state.pending_tile_updates.end(),
                                       std::make_move_iterator(updates.begin()),
                                       std::make_move_iterator(updates.end()));
    }
  };

  auto process_tile_updates = [&]() {
    if (!state.overlay_cache_valid) return;
    std::vector<TileUpdate> remaining;
    remaining.reserve(state.pending_tile_updates.size());
    for (auto& update : state.pending_tile_updates) {
      if (update.id < 0 || update.id >= static_cast<int>(state.overlay_cache.tiles.size()) ||
          update.id >= static_cast<int>(state.overlay_cache.tile_status.size())) {
        remaining.push_back(std::move(update));
        continue;
      }
      state.overlay_cache.tile_status[static_cast<std::size_t>(update.id)] = update.status;
      if (!update.pixels.empty()) {
        const Tile& tile = state.overlay_cache.tiles[static_cast<std::size_t>(update.id)];
        commit_tile(state.viewport_framebuffer, tile, update.pixels);
        if (state.viewport_texture) {
          if (!upload_tile_pixels(state.viewport_texture, tile, update.pixels)) {
            state.needs_full_upload = true;
          }
        } else {
          state.needs_full_upload = true;
        }
      }
    }
    state.pending_tile_updates = std::move(remaining);
  };

  if (state.job) {
    RenderProgress progress = state.job->progress();
    if (state.needs_full_upload || state.viewport_dirty) refresh_texture();

    refresh_overlay_states();
    if (!state.overlay_cache_valid) {
      consume_tile_updates();
      state.last_uploaded_tiles_completed = progress.tiles_completed;
      if (state.job->is_complete() && !state.job_joined) {
        state.job->join();
        state.job_joined = true;
        state.last_progress = state.job->progress();
        state.job.reset();
        state.viewport_dirty = true;
        state.needs_full_upload = true;
      }
      return;
    }

    consume_tile_updates();
    process_tile_updates();
    state.last_uploaded_tiles_completed = progress.tiles_completed;

    if (state.job->is_complete() && !state.job_joined) {
      state.job->join();
      state.job_joined = true;
      state.last_progress = state.job->progress();
      state.job.reset();
      state.viewport_dirty = true;
      state.needs_full_upload = true;
      state.overlay_cache_valid = false;
      state.pending_tile_updates.clear();
    }
  } else if ((state.needs_full_upload || state.viewport_dirty) && state.viewport_framebuffer.width > 0 && state.viewport_framebuffer.height > 0) {
    refresh_texture();
  }
}

static void draw_viewport_overlay(const GuiAppState& state, const RenderOverlaySnapshot& overlay, const ImVec2& image_min, const ImVec2& image_size) {
  if (!state.show_overlay || overlay.tiles.empty() || state.config.width <= 0 || state.config.height <= 0) return;

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  float scale_x = image_size.x / static_cast<float>(state.config.width);
  float scale_y = image_size.y / static_cast<float>(state.config.height);
  for (const auto& tile : overlay.tiles) {
    if (tile.id < 0 || tile.id >= static_cast<int>(overlay.tile_status.size())) continue;
    const auto& status = overlay.tile_status[static_cast<std::size_t>(tile.id)];
    ImU32 color = IM_COL32(120, 120, 120, 90);
    if (status.state == TileState::InProgress) color = IM_COL32(255, 170, 0, 70);
    if (status.state == TileState::Completed) color = IM_COL32(80, 220, 120, 70);
    ImVec2 p0{image_min.x + static_cast<float>(tile.x0) * scale_x, image_min.y + static_cast<float>(tile.y0) * scale_y};
    ImVec2 p1{image_min.x + static_cast<float>(tile.x1) * scale_x, image_min.y + static_cast<float>(tile.y1) * scale_y};
    draw_list->AddRectFilled(p0, p1, color);
    draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 180));
  }
}

static void add_hover_tooltip(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", text);
  }
}

static void table_header_cell(const char* label, const char* tooltip) {
  ImGui::TextUnformatted(label);
  add_hover_tooltip(tooltip);
}

static std::string benchmark_summary_label(const BenchmarkConfigSummary& summary) {
  std::ostringstream oss;
  oss << summary.key.threads << " threads | " << summary.key.schedule;
  return oss.str();
}

static void draw_control_panel(SDL_Window* window, GuiAppState& state);

static const char* page_name(GuiAppState::Page page) {
  switch (page) {
    case GuiAppState::Page::Home: return "Home";
    case GuiAppState::Page::Render: return "Render";
    case GuiAppState::Page::Benchmark: return "Benchmark";
    case GuiAppState::Page::Results: return "Results";
    case GuiAppState::Page::Charts: return "Charts";
    case GuiAppState::Page::Diagnostics: return "Diagnostics";
  }
  return "Home";
}

static void draw_navigation(GuiAppState& state) {
  ImGui::TextUnformatted("Sections");
  ImGui::Separator();
  const struct {
    GuiAppState::Page page;
    const char* label;
  } items[] = {
      {GuiAppState::Page::Home, "Home"},
      {GuiAppState::Page::Render, "Render"},
      {GuiAppState::Page::Benchmark, "Benchmark"},
      {GuiAppState::Page::Results, "Results"},
      {GuiAppState::Page::Charts, "Charts"},
      {GuiAppState::Page::Diagnostics, "Diagnostics"},
  };
  for (const auto& item : items) {
    if (ImGui::Selectable(item.label, state.page == item.page)) state.page = item.page;
  }
}

static void draw_render_page(SDL_Window* window, GuiAppState& state);

static void start_benchmark(GuiAppState& state) {
  if (state.benchmark_running) return;
  state.benchmark_running = true;
  state.benchmark_cancelled = false;
  state.benchmark_rows.clear();
  state.benchmark_log.clear();
  state.benchmark_status = "Running benchmark...";
  state.benchmark_done = false;
  state.benchmark_cancel_token.store(false);

  if (state.benchmark_worker.joinable()) state.benchmark_worker.join();
  state.benchmark_worker = std::thread([&state] {
    RenderConfig config = state.config;
    Scene scene = build_scene(config.scene_name);
    Camera camera = make_camera(config.width, config.height);
    auto thread_counts = parse_int_list(state.benchmark_thread_counts);
    if (thread_counts.empty()) thread_counts = {resolved_thread_count(config)};
    auto schedules = parse_string_list(state.benchmark_schedules);
    if (schedules.empty()) schedules = {config.schedule_mode};
    auto on_progress = [&state](const BenchmarkEvent& event) {
      std::lock_guard<std::mutex> lock(state.benchmark_mutex);
      std::ostringstream oss;
      switch (event.kind) {
        case BenchmarkEvent::Kind::ConfigurationStarted:
          oss << "config " << event.configuration_index << "/" << event.configuration_total << " started: schedule=" << event.schedule_mode
              << " threads=" << event.thread_count;
          break;
        case BenchmarkEvent::Kind::RunStarted:
          oss << "run " << event.run_index << "/" << event.run_total << " started";
          break;
        case BenchmarkEvent::Kind::RunCompleted:
          state.benchmark_rows.push_back(event.row);
          oss << "run " << event.run_index << "/" << event.run_total << " complete: " << event.stats.milliseconds << " ms";
          break;
        case BenchmarkEvent::Kind::ConfigurationCompleted:
          oss << "config " << event.configuration_index << "/" << event.configuration_total << " complete";
          break;
        case BenchmarkEvent::Kind::Completed:
          state.benchmark_status = "Benchmark complete";
          state.benchmark_done = true;
          oss << "benchmark complete";
          break;
        case BenchmarkEvent::Kind::Cancelled:
          state.benchmark_status = "Benchmark cancelled";
          state.benchmark_done = true;
          oss << "benchmark cancelled";
          break;
      }
      if (!oss.str().empty()) state.benchmark_log.push_back(oss.str());
      while (state.benchmark_log.size() > 200) state.benchmark_log.pop_front();
    };
    run_benchmarks(scene, camera, config, thread_counts, schedules, on_progress, &state.benchmark_cancel_token, false);
    {
      std::lock_guard<std::mutex> lock(state.benchmark_mutex);
      state.benchmark_running = false;
      if (!state.benchmark_done) {
        state.benchmark_status = state.benchmark_cancel_token.load() ? "Benchmark cancelled" : "Benchmark complete";
        state.benchmark_done = true;
      }
    }
  });
}

static void draw_benchmark_page(GuiAppState& state) {
  ImGui::TextUnformatted("Benchmark Mode");
  ImGui::Separator();
  float full_width = ImGui::GetContentRegionAvail().x;
  float left_width = std::max(360.0f * state.ui_scale, full_width * 0.48f);
  float right_width = std::max(360.0f * state.ui_scale, full_width - left_width - ImGui::GetStyle().ItemSpacing.x);

  ImGui::BeginChild("BenchmarkConfig", ImVec2(left_width, 0), true);
  if (ImGui::BeginCombo("Scene", state.scene_names[static_cast<std::size_t>(state.selected_scene)].c_str())) {
    for (int i = 0; i < static_cast<int>(state.scene_names.size()); ++i) {
      bool selected = (i == state.selected_scene);
      if (ImGui::Selectable(state.scene_names[static_cast<std::size_t>(i)].c_str(), selected)) {
        state.selected_scene = i;
        state.config.scene_name = state.scene_names[static_cast<std::size_t>(i)];
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  add_hover_tooltip("Choose the scene to benchmark.");

  ImGui::InputInt("Width", &state.config.width);
  add_hover_tooltip("Benchmark image width in pixels.");
  ImGui::InputInt("Height", &state.config.height);
  add_hover_tooltip("Benchmark image height in pixels.");
  ImGui::InputInt("SPP", &state.config.samples_per_pixel);
  add_hover_tooltip("Samples per pixel: higher costs more time.");
  ImGui::InputInt("Depth", &state.config.max_depth);
  add_hover_tooltip("Maximum ray bounce depth.");
  ImGui::InputInt("Threads", &state.config.thread_count);
  add_hover_tooltip("Thread count list is entered below; this field is the default thread count.");
  ImGui::InputInt("Tile Size", &state.config.tile_size);
  add_hover_tooltip("Tile size used to split the workload.");
  ImGui::InputInt("Runs", &state.config.benchmark_runs);
  add_hover_tooltip("How many times each benchmark configuration runs.");
  ImGui::InputScalar("Seed", ImGuiDataType_U64, &state.config.seed);
  add_hover_tooltip("Seed used for reproducible benchmark runs.");
  ImGui::InputText("Thread Counts", &state.benchmark_thread_counts);
  add_hover_tooltip("Comma-separated thread counts, for example 1,2,4,8.");
  ImGui::InputText("Schedules", &state.benchmark_schedules);
  add_hover_tooltip("Comma-separated schedules, for example serial,dynamic.");
  ImGui::InputText("CSV Path", &state.benchmark_csv_path);
  add_hover_tooltip("Where benchmark CSV output should be saved.");

  ImGui::Separator();
  if (ImGui::Button(state.benchmark_running ? "Running..." : "Start Benchmark")) start_benchmark(state);
  ImGui::SameLine();
  if (ImGui::Button("Export CSV")) {
    std::lock_guard<std::mutex> lock(state.benchmark_mutex);
    write_benchmark_csv(state.benchmark_csv_path, std::vector<std::string>(state.benchmark_rows.begin(), state.benchmark_rows.end()));
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) state.benchmark_cancel_token.store(true);

  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("BenchmarkResults", ImVec2(0, 0), true);
  ImGui::TextUnformatted("Live Log / Results");
  ImGui::Separator();
  std::lock_guard<std::mutex> lock(state.benchmark_mutex);
  ImGui::Text("Status: %s", state.benchmark_status.c_str());
  ImGui::Text("Rows: %zu", state.benchmark_rows.size());
  ImGui::BeginChild("BenchmarkLog", ImVec2(0, 160.0f * state.ui_scale), true, ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& line : state.benchmark_log) ImGui::TextWrapped("%s", line.c_str());
  ImGui::EndChild();
  ImGui::Separator();
  ImGui::BeginChild("BenchmarkRows", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& row : state.benchmark_rows) ImGui::TextWrapped("%s", row.c_str());
  ImGui::EndChild();
  ImGui::EndChild();
}

static std::vector<std::string> load_csv_rows(const std::string& path) {
  std::vector<std::string> rows;
  std::ifstream in(path);
  if (!in) return rows;
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (first) {
      first = false;
      continue;
    }
    if (!line.empty()) rows.push_back(line);
  }
  return rows;
}

static void load_results_csv(GuiAppState& state, const std::string& path) {
  if (path.empty()) return;
  state.results_csv_path = path;
  state.results_status = "Loading CSV...";
  state.results_loading.store(true);
  if (state.results_worker.joinable()) state.results_worker.join();
  state.results_worker = std::thread([&state, path] {
    BenchmarkDataset dataset;
    std::string error;
    bool ok = load_benchmark_csv(path, dataset, error);
    {
      std::lock_guard<std::mutex> guard(state.results_mutex);
      if (!ok) {
        state.results_status = error.empty() ? "CSV load failed" : error;
        state.results_dataset = BenchmarkDataset{};
        state.results_compare_a = -1;
        state.results_compare_b = -1;
      } else {
        state.results_dataset = std::move(dataset);
        state.results_status = "CSV loaded";
        auto find_config_index = [&](const BenchmarkConfigKey& key) -> int {
          for (int i = 0; i < static_cast<int>(state.results_dataset.configs.size()); ++i) {
            const auto& cfg = state.results_dataset.configs[static_cast<std::size_t>(i)];
            if (cfg.key.threads == key.threads && cfg.key.schedule == key.schedule) return i;
          }
          return -1;
        };
        state.results_compare_a = find_config_index(state.results_dataset.workload.baseline_config);
        state.results_compare_b = find_config_index(state.results_dataset.workload.best_config);
        if (state.results_compare_a < 0 && !state.results_dataset.configs.empty()) state.results_compare_a = 0;
        if (state.results_compare_b < 0) {
          state.results_compare_b = state.results_dataset.configs.size() > 1 ? 1 : state.results_compare_a;
        }
        if (state.results_compare_a == state.results_compare_b && state.results_dataset.configs.size() > 1) {
          state.results_compare_b = (state.results_compare_a + 1) % static_cast<int>(state.results_dataset.configs.size());
        }
      }
      state.results_loading.store(false);
    }
  });
}

static void draw_benchmark_dataset_panel(SDL_Window*, GuiAppState& state) {
  (void)state;
  ImGui::TextUnformatted("Benchmark CSV Source");
  ImGui::Separator();

  ImGui::InputText("CSV Path", &state.results_csv_path);
  add_hover_tooltip("Type or paste a benchmark CSV path, or choose one from the browser below.");
  if (ImGui::Button(state.results_loading.load() ? "Loading..." : "Load CSV")) {
    load_results_csv(state, state.results_csv_path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Up")) {
    std::filesystem::path current = state.results_browser_dir;
    state.results_browser_dir = current.parent_path().empty() ? "." : current.parent_path().string();
  }

  ImGui::Text("Browse: %s", state.results_browser_dir.c_str());
  ImGui::Text("CSV Path: %s", state.results_csv_path.c_str());
  ImGui::BeginChild("ResultsBrowser", ImVec2(0, 140.0f * state.ui_scale), true, ImGuiWindowFlags_HorizontalScrollbar);
  try {
    for (const auto& entry : std::filesystem::directory_iterator(state.results_browser_dir)) {
      const auto name = entry.path().filename().string();
      const bool is_dir = entry.is_directory();
      const bool is_csv = entry.is_regular_file() && entry.path().extension() == ".csv";
      if (!is_dir && !is_csv) continue;
      std::string label = is_dir ? "[DIR] " + name : name;
      if (ImGui::Selectable(label.c_str(), false)) {
        if (is_dir) {
          state.results_browser_dir = entry.path().string();
        } else {
          state.results_csv_path = entry.path().string();
          load_results_csv(state, state.results_csv_path);
        }
      }
    }
  } catch (...) {
    ImGui::TextUnformatted("Unable to open directory.");
  }
  ImGui::EndChild();

  {
    std::lock_guard<std::mutex> lock(state.results_mutex);
    ImGui::Text("Status: %s", state.results_status.c_str());
    ImGui::Text("CSV: %s", state.results_dataset.csv_path.string().c_str());
    ImGui::Text("Rows: %zu", state.results_dataset.rows.size());
    ImGui::Text("Config groups: %zu", state.results_dataset.configs.size());
    ImGui::Text("Workload: %s", benchmark_workload_label(state.results_dataset.workload).c_str());
    if (!state.results_dataset.configs.empty()) {
      ImGui::Text("Baseline config: %s", benchmark_config_label(state.results_dataset.workload.baseline_config).c_str());
      ImGui::Text("Baseline serial ms: %.2f", state.results_dataset.workload.baseline_serial_ms);
      ImGui::Text("Speedup basis: baseline serial median / config median");
      ImGui::TextWrapped("Values below 1.0x are slower than the baseline. Serial schedule ignores extra threads, so serial rows with higher thread counts should stay near 1.0x.");
      ImGui::Text("Ideal speedup @ max threads: %.2fx", state.results_dataset.workload.ideal_linear_speedup);
      ImGui::Text("Ideal runtime @ max threads: %.2f ms", state.results_dataset.workload.ideal_runtime_ms);
      ImGui::Text("Best config: %s", benchmark_config_label(state.results_dataset.workload.best_config).c_str());
      ImGui::Text("Best observed speedup: %.2fx", state.results_dataset.workload.best_observed_speedup);
      ImGui::Text("Fastest observed ms: %.2f ms", state.results_dataset.workload.best_observed_ms);
    }
  }
}

static void draw_results_page(SDL_Window* window, GuiAppState& state) {
  ImGui::TextUnformatted("Results");
  ImGui::Separator();
  draw_benchmark_dataset_panel(window, state);

  {
    std::lock_guard<std::mutex> lock(state.results_mutex);
    if (!state.results_dataset.configs.empty()) {
      ImGui::Separator();
      ImGui::TextUnformatted("Config Comparison");
      ImGui::Separator();
      const char* preview_items = "Select two configs";
      if (ImGui::BeginCombo("Config A", state.results_compare_a >= 0 && state.results_compare_a < static_cast<int>(state.results_dataset.configs.size()) ? benchmark_config_label(state.results_dataset.configs[static_cast<std::size_t>(state.results_compare_a)].key).c_str() : preview_items)) {
        for (int i = 0; i < static_cast<int>(state.results_dataset.configs.size()); ++i) {
          const auto& summary = state.results_dataset.configs[static_cast<std::size_t>(i)];
          std::string label = benchmark_config_label(summary.key);
          if (ImGui::Selectable(label.c_str(), state.results_compare_a == i)) state.results_compare_a = i;
        }
        ImGui::EndCombo();
      }
      if (ImGui::BeginCombo("Config B", state.results_compare_b >= 0 && state.results_compare_b < static_cast<int>(state.results_dataset.configs.size()) ? benchmark_config_label(state.results_dataset.configs[static_cast<std::size_t>(state.results_compare_b)].key).c_str() : preview_items)) {
        for (int i = 0; i < static_cast<int>(state.results_dataset.configs.size()); ++i) {
          const auto& summary = state.results_dataset.configs[static_cast<std::size_t>(i)];
          std::string label = benchmark_config_label(summary.key);
          if (ImGui::Selectable(label.c_str(), state.results_compare_b == i)) state.results_compare_b = i;
        }
        ImGui::EndCombo();
      }

      const BenchmarkConfigSummary* a = nullptr;
      const BenchmarkConfigSummary* b = nullptr;
      if (state.results_compare_a >= 0 && state.results_compare_a < static_cast<int>(state.results_dataset.configs.size())) a = &state.results_dataset.configs[static_cast<std::size_t>(state.results_compare_a)];
      if (state.results_compare_b >= 0 && state.results_compare_b < static_cast<int>(state.results_dataset.configs.size())) b = &state.results_dataset.configs[static_cast<std::size_t>(state.results_compare_b)];
      if (a && b) {
        ImGui::BeginChild("ResultsComparison", ImVec2(0, 150.0f * state.ui_scale), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::Text("%-14s %-16s %-16s", "Metric", "Config A", "Config B");
        ImGui::Separator();
        ImGui::Text("%-14s %-16.2f %-16.2f", "Config avg ms", a->avg_ms, b->avg_ms);
        ImGui::Text("%-14s %-16.2f %-16.2f", "Median ms", a->median_ms, b->median_ms);
        ImGui::Text("%-14s %-16.2f %-16.2f", "Stddev ms", a->stddev_ms, b->stddev_ms);
        ImGui::Text("%-14s %-16.1f %-16.1f", "Avg pps", a->avg_pixels_per_sec, b->avg_pixels_per_sec);
        ImGui::Text("%-14s %-16.2fx %-16.2fx", "Observed speedup", a->observed_speedup, b->observed_speedup);
        ImGui::Text("%-14s %-16.2f %-16.2f", "Efficiency", a->efficiency, b->efficiency);
        ImGui::EndChild();
      }
    }

    ImGui::BeginChild("ResultsSummary", ImVec2(0, 220.0f * state.ui_scale), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted("Config Summary");
    ImGui::Separator();
    if (ImGui::BeginTable("ConfigSummaryTable", 11, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY)) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Threads");
      ImGui::TableSetupColumn("Schedule");
      ImGui::TableSetupColumn("Rows");
      ImGui::TableSetupColumn("Min ms");
      ImGui::TableSetupColumn("Max ms");
      ImGui::TableSetupColumn("Avg ms");
      ImGui::TableSetupColumn("Median ms");
      ImGui::TableSetupColumn("Stddev ms");
      ImGui::TableSetupColumn("Avg pps");
      ImGui::TableSetupColumn("Speedup vs baseline");
      ImGui::TableSetupColumn("Efficiency");
      ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
      ImGui::TableNextColumn(); table_header_cell("Threads", "Thread count used by this configuration group.");
      ImGui::TableNextColumn(); table_header_cell("Schedule", "Scheduling mode used by this configuration group.");
      ImGui::TableNextColumn(); table_header_cell("Rows", "Benchmark rows in this configuration group.");
      ImGui::TableNextColumn(); table_header_cell("Min ms", "Fastest run time in this group.");
      ImGui::TableNextColumn(); table_header_cell("Max ms", "Slowest run time in this group.");
      ImGui::TableNextColumn(); table_header_cell("Avg ms", "Average run time in this group.");
      ImGui::TableNextColumn(); table_header_cell("Median ms", "Median run time in this group.");
      ImGui::TableNextColumn(); table_header_cell("Stddev ms", "Standard deviation of run times in this group.");
      ImGui::TableNextColumn(); table_header_cell("Avg pps", "Average pixels per second in this group.");
      ImGui::TableNextColumn(); table_header_cell("Speedup vs baseline", "Baseline serial time divided by this group's median time.");
      ImGui::TableNextColumn(); table_header_cell("Efficiency", "Observed speedup divided by the thread count.");

      for (const auto& summary : state.results_dataset.configs) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%d", summary.key.threads);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(summary.key.schedule.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%zu", summary.row_count);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", summary.min_ms);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", summary.max_ms);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", summary.avg_ms);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", summary.median_ms);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", summary.stddev_ms);
        ImGui::TableNextColumn(); ImGui::Text("%.1f", summary.avg_pixels_per_sec);
        ImGui::TableNextColumn(); ImGui::Text("%.2fx", summary.observed_speedup);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", summary.efficiency);
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();
  }
}

static void draw_charts_page(SDL_Window* window, GuiAppState& state) {
  ImGui::TextUnformatted("Charts");
  ImGui::Separator();

  draw_benchmark_dataset_panel(window, state);

  BenchmarkDataset snapshot;
  {
    std::lock_guard<std::mutex> lock(state.results_mutex);
    snapshot = state.results_dataset;
  }

  if (snapshot.configs.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("Load a benchmark CSV to view charts.");
    return;
  }

  if (snapshot.configs.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("No chartable config groups found.");
    return;
  }

  auto show_point_tooltip = [&](auto value_getter, const char* metric_label) {
    if (!ImPlot::IsPlotHovered()) return;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    const BenchmarkConfigSummary* nearest = nullptr;
    double nearest_value = 0.0;
    double nearest_dist = 12.0;
    for (const auto& summary : snapshot.configs) {
      const double x = static_cast<double>(summary.key.threads);
      const double y = static_cast<double>(value_getter(summary));
      const ImVec2 p = ImPlot::PlotToPixels(x, y);
      const double dx = static_cast<double>(mouse.x - p.x);
      const double dy = static_cast<double>(mouse.y - p.y);
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (dist < nearest_dist) {
        nearest_dist = dist;
        nearest = &summary;
        nearest_value = y;
      }
    }

    if (!nearest) return;
    std::string label = benchmark_config_label(nearest->key);
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(label.c_str());
    ImGui::Text("Threads: %d", nearest->key.threads);
    ImGui::Text("Schedule: %s", nearest->key.schedule.c_str());
    ImGui::Text("%s: %.3f", metric_label, nearest_value);
    ImGui::Text("Median ms: %.2f", nearest->median_ms);
    ImGui::Text("Observed speedup: %.2fx", nearest->observed_speedup);
    ImGui::Text("Efficiency: %.2f", nearest->efficiency);
    ImGui::EndTooltip();
  };

  ImGui::Separator();
  if (ImPlot::BeginPlot("Observed Speedup", ImVec2(-1, 260.0f * state.ui_scale))) {
    ImPlot::SetupAxes("Threads", "Speedup");
    ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
    std::map<std::string, std::vector<const BenchmarkConfigSummary*>> grouped;
    for (const auto& summary : snapshot.configs) grouped[summary.key.schedule].push_back(&summary);
    for (auto& [schedule, configs] : grouped) {
      std::sort(configs.begin(), configs.end(), [](const BenchmarkConfigSummary* a, const BenchmarkConfigSummary* b) {
        return a->key.threads < b->key.threads;
      });
      std::vector<double> xs, ys;
      xs.reserve(configs.size());
      ys.reserve(configs.size());
      for (const auto* cfg : configs) {
        xs.push_back(static_cast<double>(cfg->key.threads));
        ys.push_back(cfg->observed_speedup);
      }
      ImU32 color = schedule == "serial" ? IM_COL32(223, 165, 91, 255) : schedule == "static" ? IM_COL32(102, 179, 255, 255) : IM_COL32(124, 214, 142, 255);
      ImPlot::PlotLine(schedule.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), ImPlotSpec{ImPlotProp_LineColor, color, ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 5.0f});
    }
    double max_threads = 1.0;
    for (const auto& summary : snapshot.configs) max_threads = std::max(max_threads, static_cast<double>(summary.key.threads));
    std::vector<double> ideal_x{1.0, max_threads};
    std::vector<double> ideal_y{1.0, max_threads};
    ImPlot::PlotLine("Ideal", ideal_x.data(), ideal_y.data(), 2, ImPlotSpec{ImPlotProp_LineColor, IM_COL32(255, 210, 90, 180), ImPlotProp_LineWeight, 1.5f});
    show_point_tooltip([](const BenchmarkConfigSummary& summary) { return summary.observed_speedup; }, "Observed speedup");
    ImPlot::EndPlot();
  }

  if (ImPlot::BeginPlot("Median Runtime", ImVec2(-1, 260.0f * state.ui_scale))) {
    ImPlot::SetupAxes("Threads", "Milliseconds");
    ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
    std::map<std::string, std::vector<const BenchmarkConfigSummary*>> grouped;
    for (const auto& summary : snapshot.configs) grouped[summary.key.schedule].push_back(&summary);
    std::vector<double> thread_samples;
    thread_samples.reserve(snapshot.configs.size());
    for (const auto& summary : snapshot.configs) thread_samples.push_back(static_cast<double>(summary.key.threads));
    for (auto& [schedule, configs] : grouped) {
      std::sort(configs.begin(), configs.end(), [](const BenchmarkConfigSummary* a, const BenchmarkConfigSummary* b) {
        return a->key.threads < b->key.threads;
      });
      std::vector<double> xs, ys;
      xs.reserve(configs.size());
      ys.reserve(configs.size());
      for (const auto* cfg : configs) {
        xs.push_back(static_cast<double>(cfg->key.threads));
        ys.push_back(cfg->median_ms);
      }
      ImU32 color = schedule == "serial" ? IM_COL32(223, 165, 91, 255) : schedule == "static" ? IM_COL32(102, 179, 255, 255) : IM_COL32(124, 214, 142, 255);
      ImPlot::PlotLine(schedule.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), ImPlotSpec{ImPlotProp_LineColor, color, ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 5.0f});
    }
    const double baseline_ms = snapshot.workload.baseline_serial_ms;
    std::vector<double> base_y(thread_samples.size(), baseline_ms);
    std::vector<double> ideal_y;
    ideal_y.reserve(thread_samples.size());
    for (double x : thread_samples) ideal_y.push_back(baseline_ms / std::max(1.0, x));
    ImPlot::PlotLine("Baseline", thread_samples.data(), base_y.data(), static_cast<int>(thread_samples.size()), ImPlotSpec{ImPlotProp_LineColor, IM_COL32(255, 255, 255, 120), ImPlotProp_LineWeight, 1.5f});
    ImPlot::PlotLine("Ideal", thread_samples.data(), ideal_y.data(), static_cast<int>(ideal_y.size()), ImPlotSpec{ImPlotProp_LineColor, IM_COL32(102, 179, 255, 180), ImPlotProp_LineWeight, 1.5f});
    show_point_tooltip([](const BenchmarkConfigSummary& summary) { return summary.median_ms; }, "Median runtime (ms)");
    ImPlot::EndPlot();
  }

  if (ImPlot::BeginPlot("Efficiency", ImVec2(-1, 260.0f * state.ui_scale))) {
    ImPlot::SetupAxes("Threads", "Efficiency");
    ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
    std::map<std::string, std::vector<const BenchmarkConfigSummary*>> grouped;
    for (const auto& summary : snapshot.configs) grouped[summary.key.schedule].push_back(&summary);
    std::vector<double> thread_samples;
    thread_samples.reserve(snapshot.configs.size());
    for (const auto& summary : snapshot.configs) thread_samples.push_back(static_cast<double>(summary.key.threads));
    for (auto& [schedule, configs] : grouped) {
      std::sort(configs.begin(), configs.end(), [](const BenchmarkConfigSummary* a, const BenchmarkConfigSummary* b) {
        return a->key.threads < b->key.threads;
      });
      std::vector<double> xs, ys;
      xs.reserve(configs.size());
      ys.reserve(configs.size());
      for (const auto* cfg : configs) {
        xs.push_back(static_cast<double>(cfg->key.threads));
        ys.push_back(cfg->efficiency);
      }
      ImU32 color = schedule == "serial" ? IM_COL32(223, 165, 91, 255) : schedule == "static" ? IM_COL32(102, 179, 255, 255) : IM_COL32(124, 214, 142, 255);
      ImPlot::PlotLine(schedule.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), ImPlotSpec{ImPlotProp_LineColor, color, ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 5.0f});
    }
    std::vector<double> ideal_y(thread_samples.size(), 1.0);
    ImPlot::PlotLine("Ideal", thread_samples.data(), ideal_y.data(), static_cast<int>(ideal_y.size()), ImPlotSpec{ImPlotProp_LineColor, IM_COL32(255, 255, 255, 120), ImPlotProp_LineWeight, 1.5f});
    show_point_tooltip([](const BenchmarkConfigSummary& summary) { return summary.efficiency; }, "Efficiency");
    ImPlot::EndPlot();
  }
}

static void draw_diagnostics_page(GuiAppState& state) {
  ImGui::TextUnformatted("Diagnostics / Self-Test");
  ImGui::Separator();
  if (ImGui::Button("Run Self-Test")) {
    auto self_test = []() {
      if (!approx_equal(Vec3{1, 2, 3} + Vec3{2, 3, 4}, Vec3{3, 5, 7})) return false;
      if (!approx_equal(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1})) return false;
      Scene scene = build_scene("simple");
      Ray ray{Vec3{0, 1, 3}, unit_vector(Vec3{0, -0.2, -1})};
      HitRecord rec;
      if (!scene.hit(ray, 0.001, 1000.0, rec)) return false;
      return true;
    };
    state.diagnostics_text = self_test() ? "Self-test passed" : "Self-test failed";
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh Environment")) {
    std::ostringstream oss;
    oss << "SDL_VIDEODRIVER=" << (std::getenv("SDL_VIDEODRIVER") ? std::getenv("SDL_VIDEODRIVER") : "<unset>") << '\n';
    oss << "DISPLAY=" << (std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "<unset>") << '\n';
    oss << "WAYLAND_DISPLAY=" << (std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "<unset>") << '\n';
    state.diagnostics_text = oss.str();
  }
  ImGui::TextWrapped("%s", state.diagnostics_text.c_str());
}

static void draw_page(GuiAppState& state, SDL_Window* window, SDL_Renderer* renderer) {
  float nav_w = std::clamp(180.0f * state.ui_scale, 170.0f, 260.0f);
  ImGui::BeginChild("Nav", ImVec2(nav_w, 0), true);
  draw_navigation(state);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("Main", ImVec2(0, 0), false);
  switch (state.page) {
    case GuiAppState::Page::Home: draw_home_page(renderer, state); break;
    case GuiAppState::Page::Render: draw_render_page(window, state); break;
    case GuiAppState::Page::Benchmark: draw_benchmark_page(state); break;
    case GuiAppState::Page::Results: draw_results_page(window, state); break;
    case GuiAppState::Page::Charts: draw_charts_page(window, state); break;
    case GuiAppState::Page::Diagnostics: draw_diagnostics_page(state); break;
  }
  ImGui::EndChild();
}

static void apply_fullscreen(SDL_Window* window, GuiAppState& state, bool fullscreen) {
  if (!window) return;
  if (state.fullscreen == fullscreen) return;
  state.fullscreen = fullscreen;
  if (state.fullscreen) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    state.windowed_bounds = SDL_FRect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    SDL_SetWindowFullscreen(window, true);
  } else {
    SDL_SetWindowFullscreen(window, false);
    SDL_SetWindowPosition(window, static_cast<int>(state.windowed_bounds.x), static_cast<int>(state.windowed_bounds.y));
    SDL_SetWindowSize(window, static_cast<int>(state.windowed_bounds.w), static_cast<int>(state.windowed_bounds.h));
  }
  state.viewport_dirty = true;
}

static void draw_viewport_status(const GuiAppState& state) {
  ImGui::Separator();
  ImGui::Text("Status: %s", render_state_label(state));
  if (state.job) {
    const RenderProgress& progress = state.last_progress;
    float pct = progress.total_tiles > 0 ? static_cast<float>(progress.tiles_completed) / static_cast<float>(progress.total_tiles) : 0.0f;
    ImGui::ProgressBar(pct, ImVec2(-1.0f, 0.0f));
    ImGui::Text("%d / %d tiles | %.3f s", progress.tiles_completed, progress.total_tiles, progress.elapsed_seconds);
  } else if (state.viewport_framebuffer.width > 0 && state.viewport_framebuffer.height > 0) {
    ImGui::TextUnformatted("Render output available. Save PNG is enabled.");
  } else {
    ImGui::TextUnformatted("No image yet. Press Render to start a job.");
  }
}

static void draw_control_panel(SDL_Window* window, GuiAppState& state) {
  float panel_w = ImGui::GetContentRegionAvail().x;
  bool compact = panel_w < 320.0f * state.ui_scale;
  bool very_compact = panel_w < 260.0f * state.ui_scale;
  float button_spacing = ImGui::GetStyle().ItemSpacing.x;

  ImGui::TextUnformatted("Controls");
  ImGui::Separator();

  auto draw_int_field = [&](const char* label, const char* id, int* value, const char* tooltip) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt(id, value);
    add_hover_tooltip(tooltip);
  };

  ImGui::TextUnformatted("Scene");
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##scene", state.scene_names[static_cast<std::size_t>(state.selected_scene)].c_str())) {
    for (int i = 0; i < static_cast<int>(state.scene_names.size()); ++i) {
      bool selected = (i == state.selected_scene);
      if (ImGui::Selectable(state.scene_names[static_cast<std::size_t>(i)].c_str(), selected)) {
        state.selected_scene = i;
        state.config.scene_name = state.scene_names[static_cast<std::size_t>(i)];
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  add_hover_tooltip("Choose the built-in scene to render.");

  draw_int_field("Width", "##width", &state.config.width, "Output image width in pixels.");
  draw_int_field("Height", "##height", &state.config.height, "Output image height in pixels.");
  draw_int_field("SPP", "##spp", &state.config.samples_per_pixel, "Samples per pixel: higher improves quality and costs more time.");
  draw_int_field("Depth", "##depth", &state.config.max_depth, "Bounce depth: limits recursive light transport.");
  draw_int_field("Threads", "##threads", &state.config.thread_count, "Worker thread count; 0 uses the renderer's default.");
  draw_int_field("Tile Size", "##tile_size", &state.config.tile_size, "Tile dimensions used to split the render workload.");

  ImGui::TextUnformatted("Seed");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputScalar("##seed", ImGuiDataType_U64, &state.config.seed);
  add_hover_tooltip("Deterministic seed for reproducible randomness.");

  if (state.config.width < 1) state.config.width = 1;
  if (state.config.height < 1) state.config.height = 1;
  if (state.config.samples_per_pixel < 1) state.config.samples_per_pixel = 1;
  if (state.config.max_depth < 1) state.config.max_depth = 1;
  if (state.config.thread_count < 0) state.config.thread_count = 0;
  if (state.config.tile_size < 1) state.config.tile_size = 1;

  int schedule_index = 0;
  for (int i = 0; i < static_cast<int>(state.schedules.size()); ++i) {
    if (state.config.schedule_mode == state.schedules[static_cast<std::size_t>(i)]) {
      schedule_index = i;
      break;
    }
  }
  ImGui::TextUnformatted("Schedule");
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##schedule", state.schedules[static_cast<std::size_t>(schedule_index)].c_str())) {
    for (int i = 0; i < static_cast<int>(state.schedules.size()); ++i) {
      bool selected = (i == schedule_index);
      if (ImGui::Selectable(state.schedules[static_cast<std::size_t>(i)].c_str(), selected)) state.config.schedule_mode = state.schedules[static_cast<std::size_t>(i)];
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  add_hover_tooltip("Choose how worker threads are scheduled: serial, static, or dynamic.");

  ImGui::TextUnformatted("Overlay");
  ImGui::Checkbox("##overlay", &state.show_overlay);
  add_hover_tooltip("Show or hide the tile overlay on top of the image.");
  if (!compact) ImGui::SameLine();
  if (ImGui::Button(state.fullscreen ? "Windowed" : "Fullscreen", ImVec2(-1.0f, 0.0f))) {
    apply_fullscreen(window, state, !state.fullscreen);
  }
  add_hover_tooltip("Toggle fullscreen mode while preserving the previous window bounds.");
  ImGui::Separator();

  bool has_job = static_cast<bool>(state.job);
  bool paused = has_job && state.last_progress.paused;
  bool complete = has_job && state.last_progress.complete;

  auto render_button_width = [&](const char* label, bool disabled, auto&& on_click, float width, const char* tooltip) {
    ImGui::BeginDisabled(disabled);
    if (ImGui::Button(label, ImVec2(width, 0.0f))) on_click();
    ImGui::EndDisabled();
    add_hover_tooltip(tooltip);
  };

  auto save_current_image = [&] {
    Framebuffer save_framebuffer = state.viewport_framebuffer;
    if (state.job) {
      save_framebuffer = state.job->snapshot();
      if (state.job->is_complete() && !state.job_joined) {
        state.job->join();
        state.job_joined = true;
      }
    }
    if (!save_png(save_framebuffer, state.config.output_filename)) {
      std::cerr << "failed to save image\n";
    } else {
      std::cerr << "saved " << state.config.output_filename << '\n';
    }
  };

  ImGui::PushID("lifecycle");
  const bool can_pair_buttons = !very_compact && panel_w >= 320.0f * state.ui_scale;
  const float full_button_w = std::max(0.0f, panel_w);
  const float half_button_w = std::max(0.0f, (panel_w - button_spacing) * 0.5f);
  bool can_save = state.viewport_framebuffer.width > 0 && state.viewport_framebuffer.height > 0;

  if (can_pair_buttons) {
    render_button_width("Render", false, [&] { start_render(state); }, half_button_w, "Start rendering with the current settings.");
    ImGui::SameLine();
    render_button_width("Pause", !has_job || paused || complete, [&] { if (state.job) state.job->pause(); }, half_button_w, "Pause the active render.");
    render_button_width("Resume", !has_job || !paused || complete, [&] { if (state.job) state.job->resume(); }, half_button_w, "Resume a paused render.");
    ImGui::SameLine();
    render_button_width("Cancel", !has_job || complete, [&] { if (state.job) state.job->cancel(); }, half_button_w, "Cancel the active render.");
    render_button_width("Save PNG", !can_save, save_current_image, full_button_w, "Save the current image to the configured PNG file.");
  } else {
    render_button_width("Render", false, [&] { start_render(state); }, full_button_w, "Start rendering with the current settings.");
    render_button_width("Pause", !has_job || paused || complete, [&] { if (state.job) state.job->pause(); }, full_button_w, "Pause the active render.");
    render_button_width("Resume", !has_job || !paused || complete, [&] { if (state.job) state.job->resume(); }, full_button_w, "Resume a paused render.");
    render_button_width("Cancel", !has_job || complete, [&] { if (state.job) state.job->cancel(); }, full_button_w, "Cancel the active render.");
    render_button_width("Save PNG", !can_save, save_current_image, full_button_w, "Save the current image to the configured PNG file.");
  }
  ImGui::PopID();

  ImGui::Separator();
  if (state.job) {
    RenderProgress progress = state.last_progress;
    ImGui::Text("Job: %s", progress.complete ? (progress.cancelled ? "complete (cancelled)" : "complete") : (progress.paused ? "paused" : "running"));
    ImGui::Text("Tiles: %d / %d", progress.tiles_completed, progress.total_tiles);
    ImGui::Text("Elapsed: %.3f s", progress.elapsed_seconds);
  } else if (state.last_progress.complete || state.last_progress.cancelled) {
    RenderProgress progress = state.last_progress;
    ImGui::Text("Job: %s", progress.cancelled ? "complete (cancelled)" : "complete");
    ImGui::Text("Tiles: %d / %d", progress.tiles_completed, progress.total_tiles);
    ImGui::Text("Elapsed: %.3f s", progress.elapsed_seconds);
  } else {
    ImGui::TextUnformatted("Job: idle");
  }
}

static void draw_render_page(SDL_Window* window, GuiAppState& state) {
  float full_w = ImGui::GetContentRegionAvail().x;
  float full_h = ImGui::GetContentRegionAvail().y;
  if (full_w <= 0.0f) full_w = 1.0f;
  if (full_h <= 0.0f) full_h = 1.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  float controls_w = std::clamp(full_w * 0.24f, 300.0f * state.ui_scale, 420.0f * state.ui_scale);
  const float min_viewport_w = 520.0f * state.ui_scale;
  const bool side_by_side = full_w >= (controls_w + min_viewport_w + spacing);

  auto draw_viewport_child = [&] {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (state.viewport_texture && state.viewport_framebuffer.width > 0 && state.viewport_framebuffer.height > 0) {
      float scale = std::min(avail.x / static_cast<float>(state.viewport_framebuffer.width), avail.y / static_cast<float>(state.viewport_framebuffer.height));
      if (scale > 0.0f) {
        ImVec2 image_size{static_cast<float>(state.viewport_framebuffer.width) * scale, static_cast<float>(state.viewport_framebuffer.height) * scale};
        ImVec2 image_pos = ImGui::GetCursorScreenPos();
        image_pos.x += std::max(0.0f, (avail.x - image_size.x) * 0.5f);
        image_pos.y += std::max(0.0f, (avail.y - image_size.y) * 0.5f);
        ImGui::SetCursorScreenPos(image_pos);
        ImGui::Image((ImTextureID)(intptr_t)state.viewport_texture, image_size);
        ImVec2 image_min = ImGui::GetItemRectMin();
        ImVec2 image_max = ImGui::GetItemRectMax();
        RenderOverlaySnapshot overlay = state.overlay_cache_valid ? state.overlay_cache : RenderOverlaySnapshot{};
        if (state.job && !state.overlay_cache_valid) overlay = state.job->overlay_snapshot();
        draw_viewport_overlay(state, overlay, image_min, image_max - image_min);
      } else {
        ImGui::TextUnformatted("Viewport too small.");
      }
    } else {
      ImGui::TextWrapped(state.job ? "Rendering started. The first frame will appear as soon as tiles complete." : "Press Render to start a job using the settings on the right.");
    }
    draw_viewport_status(state);
  };

  if (side_by_side) {
    float viewport_w = std::max(0.0f, full_w - controls_w - spacing);
    ImGui::BeginChild("ViewportChild", ImVec2(viewport_w, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    draw_viewport_child();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("ControlsChild", ImVec2(0, 0), true);
    draw_control_panel(window, state);
    ImGui::EndChild();
  } else {
    float controls_h = std::clamp(full_h * 0.34f, 220.0f * state.ui_scale, 360.0f * state.ui_scale);
    float viewport_h = std::max(0.0f, full_h - controls_h - spacing);

    ImGui::BeginChild("ViewportChild", ImVec2(0, viewport_h), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    draw_viewport_child();
    ImGui::EndChild();

    ImGui::BeginChild("ControlsChild", ImVec2(0, 0), true);
    draw_control_panel(window, state);
    ImGui::EndChild();
  }
}

}  // namespace

bool gui_support_available() {
#if PR_HAS_SDL3
  return true;
#else
  return false;
#endif
}

int run_gui_app(int argc, char** argv) {
  auto options = cli::parse_args(argc, argv);
  GuiAppState state;
  state.config = options.config;
  state.scene_names = available_scene_names();
  if (!state.scene_names.empty()) {
    auto it = std::find(state.scene_names.begin(), state.scene_names.end(), state.config.scene_name);
    state.selected_scene = (it != state.scene_names.end()) ? static_cast<int>(std::distance(state.scene_names.begin(), it)) : 0;
    state.config.scene_name = state.scene_names[static_cast<std::size_t>(state.selected_scene)];
  }
  if (state.config.scene_name.empty()) state.config.scene_name = "simple";

  std::cerr << "gui backend=SDL3 + Dear ImGui\n";
  print_sdl_video_diagnostics();

  const char* original_driver = SDL_getenv("SDL_VIDEODRIVER");
  if (!try_sdl_video_init_with_driver(nullptr)) {
    if (!try_sdl_video_init_with_driver("wayland")) {
      if (!try_sdl_video_init_with_driver("x11")) {
        if (original_driver) SDL_setenv_unsafe("SDL_VIDEODRIVER", original_driver, 1);
        else SDL_unsetenv_unsafe("SDL_VIDEODRIVER");
        return 1;
      }
    }
  }

  SDL_Window* window = SDL_CreateWindow("Parallel Renderer", 1280, 800, SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = nullptr;
  const char* renderer_order[] = {nullptr, "gpu", "opengl", "opengles2", "software"};
  for (const char* driver : renderer_order) {
    renderer = SDL_CreateRenderer(window, driver);
    if (renderer) {
      std::cerr << "selected renderer=" << SDL_GetRendererName(renderer) << '\n';
      break;
    }
  }
  if (!renderer) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui::StyleColorsDark();
  state.base_style = ImGui::GetStyle();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  state.ui_font = io.Fonts->AddFontDefault();
  io.FontDefault = state.ui_font;
  state.home_ascii_font = load_home_ascii_font(io);
  state.last_applied_ui_scale = 0.0f;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  bool running = true;
  while (running) {
    bool had_job = static_cast<bool>(state.job);
    bool received_event = false;
    SDL_Event event;
    if (had_job ? SDL_WaitEventTimeout(&event, 16) : SDL_WaitEvent(&event)) {
      received_event = true;
      do {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
          state.viewport_dirty = true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
          const bool alt_enter = event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT);
          if (event.key.key == SDLK_F11 || alt_enter) {
            apply_fullscreen(window, state, !state.fullscreen);
          }
        }
        if (event.type == SDL_EVENT_QUIT) running = false;
      } while (SDL_PollEvent(&event));
    }

    if (!received_event && !had_job) continue;

    update_job_output(state, renderer);
    refresh_progress(state);
    if (state.benchmark_worker.joinable() && state.benchmark_done) {
      state.benchmark_worker.join();
      state.benchmark_done = false;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    apply_ui_scale_if_needed(state);
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::Begin("Parallel Renderer", nullptr, root_flags)) {
      ImGui::BeginChild("Shell", ImVec2(0, 0), false);
      draw_page(state, window, renderer);
      ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  destroy_texture(state.viewport_texture);
  if (state.job) {
    state.job->cancel();
    state.job->join();
  }
  if (state.results_worker.joinable()) {
    state.results_worker.join();
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}  // namespace pr
