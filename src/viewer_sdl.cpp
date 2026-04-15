#include "viewer_sdl.h"

#include "render_job.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace pr {
#if PR_HAS_SDL3
static void print_sdl_video_diagnostics() {
  auto print_env = [](const char* name) {
    const char* value = std::getenv(name);
    std::cerr << name << '=' << (value ? value : "<unset>") << '\n';
  };
  print_env("DISPLAY");
  print_env("WAYLAND_DISPLAY");
  print_env("XDG_RUNTIME_DIR");
  print_env("SDL_VIDEODRIVER");

  std::cerr << "compiled SDL video drivers:";
  int count = SDL_GetNumVideoDrivers();
  for (int i = 0; i < count; ++i) {
    if (const char* driver = SDL_GetVideoDriver(i)) {
      std::cerr << ' ' << driver;
    }
  }
  std::cerr << '\n';
}

static bool try_sdl_video_init_with_driver(const char* driver_name) {
  if (driver_name) {
    std::cerr << "trying SDL_VIDEODRIVER=" << driver_name << '\n';
    SDL_setenv_unsafe("SDL_VIDEODRIVER", driver_name, 1);
  } else {
    std::cerr << "trying SDL_VIDEODRIVER=<default>\n";
  }
  if (SDL_Init(SDL_INIT_VIDEO)) {
    return true;
  }
    std::cerr << "SDL_Init(" << (driver_name ? driver_name : "default") << ") failed: " << SDL_GetError() << '\n';
  SDL_Quit();
  return false;
}

static bool sdl_set_texture_from_framebuffer(SDL_Texture* texture, const Framebuffer& framebuffer) {
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
    for (int x = 0; x < framebuffer.width; ++x) {
      row[x] = 0xff000000u | framebuffer.at(x, y);
    }
  }
  SDL_UnlockTexture(texture);
  return true;
}

static bool sdl_set_texture_tile(SDL_Texture* texture, const Tile& tile, const std::vector<std::uint32_t>& pixels) {
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

static void print_sdl_render_diagnostics() {
  std::cerr << "available SDL render drivers:";
  int count = SDL_GetNumRenderDrivers();
  for (int i = 0; i < count; ++i) {
    if (const char* driver = SDL_GetRenderDriver(i)) {
      std::cerr << ' ' << driver;
    }
  }
  std::cerr << '\n';
}

static void draw_sdl_overlay(SDL_Renderer* renderer, const RenderProgress& progress, const std::vector<Tile>& tiles, const std::vector<TileStatus>& tile_status,
                             const RenderConfig& config, double elapsed_ms) {
  if (!renderer) return;

  int output_w = config.width;
  int output_h = config.height;
  SDL_GetCurrentRenderOutputSize(renderer, &output_w, &output_h);
  float scale_x = output_w > 0 ? static_cast<float>(output_w) / static_cast<float>(std::max(1, config.width)) : 1.0f;
  float scale_y = output_h > 0 ? static_cast<float>(output_h) / static_cast<float>(std::max(1, config.height)) : 1.0f;

  auto draw_box = [&](float x, float y, float w, float h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_FRect rect{x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
  };

  auto draw_text = [&](float x, float y, const std::string& text, float scale, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    float cx = x;
    for (char ch : text) {
      if (ch == ' ') {
        cx += 4.0f * scale;
        continue;
      }
      auto glyph = [&](char c, int row, int col) {
        switch (c) {
          case 'A': return (row == 0 && col == 2) || (row == 1 && (col == 1 || col == 3)) || (row == 2 && col == 0) || (row == 2 && col == 4) || (row == 3 && col <= 4) || (row >= 4 && (col == 0 || col == 4));
          case 'B': return (col == 0) || (row == 0 && col < 4) || (row == 2 && col < 4) || (row == 4 && col < 4) || (row == 6 && col < 4) || (col == 4 && row > 0 && row < 6 && row != 3);
          case 'C': return (row == 0 && col > 0) || (row == 6 && col > 0) || (col == 0 && row > 0 && row < 6);
          case 'D': return (col == 0) || (row == 0 && col < 4) || (row == 6 && col < 4) || (col == 4 && row > 0 && row < 6);
          case 'E': return (col == 0) || (row == 0) || (row == 3 && col < 4) || (row == 6) ;
          case 'F': return (col == 0) || (row == 0) || (row == 3 && col < 4);
          case 'G': return (row == 0 && col > 0) || (row == 6 && col > 0) || (col == 0 && row > 0 && row < 6) || (row == 3 && col >= 2) || (col == 4 && row >= 3);
          case 'H': return (col == 0) || (col == 4) || (row == 3);
          case 'I': return (row == 0) || (row == 6) || (col == 2);
          case 'J': return (row == 0) || (col == 2 && row < 6) || (row == 6 && col < 2) || (col == 0 && row >= 5);
          case 'K': return (col == 0) || (row + col == 4) || (row == 3 && col == 2) || (row + (4 - col) == 4);
          case 'L': return (col == 0) || (row == 6);
          case 'M': return (col == 0) || (col == 4) || (row == col && row < 3) || (row + col == 4 && row < 3);
          case 'N': return (col == 0) || (col == 4) || (row == col);
          case 'O': return (row == 0 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 6) || (col == 4 && row > 0 && row < 6);
          case 'P': return (col == 0) || (row == 0 && col < 4) || (row == 3 && col < 4) || (col == 4 && row > 0 && row < 3);
          case 'Q': return (row == 0 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 6) || (col == 4 && row > 0 && row < 6) || (row == 5 && col == 3) || (row == 4 && col == 4);
          case 'R': return (col == 0) || (row == 0 && col < 4) || (row == 3 && col < 4) || (col == 4 && row > 0 && row < 3) || (row == col && row > 2);
          case 'S': return (row == 0 && col > 0) || (row == 3) || (row == 6 && col < 4) || (col == 0 && row > 0 && row < 3) || (col == 4 && row > 3 && row < 6);
          case 'T': return (row == 0) || (col == 2);
          case 'U': return (col == 0 && row < 6) || (col == 4 && row < 6) || (row == 6 && col > 0 && col < 4);
          case 'V': return (col == 0 && row < 5) || (col == 4 && row < 5) || (row == 5 && (col == 1 || col == 3)) || (row == 6 && col == 2);
          case 'W': return (col == 0) || (col == 4) || (row == col && row > 3) || (row + col == 4 && row > 3);
          case 'X': return row == col || row + col == 4;
          case 'Y': return (row == col && row < 3) || (row + col == 4 && row < 3) || (col == 2 && row >= 2);
          case 'Z': return (row == 0) || (row == 6) || (row + col == 4);
          case '0': return (row == 0 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 6) || (col == 4 && row > 0 && row < 6);
          case '1': return (col == 2) || (row == 6 && col > 0 && col < 4) || (row == 1 && col == 1) || (row == 0 && col == 2);
          case '2': return (row == 0 && col > 0 && col < 4) || (row == 3) || (row == 6 && col < 4) || (col == 4 && row > 0 && row < 3) || (col == 0 && row > 3 && row < 6);
          case '3': return (row == 0 && col > 0 && col < 4) || (row == 3 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 4 && row > 0 && row < 6);
          case '4': return (col == 0 && row < 3) || (col == 4) || (row == 3 && col < 5);
          case '5': return (row == 0 && col < 4) || (row == 3 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 3) || (col == 4 && row > 3 && row < 6);
          case '6': return (row == 0 && col > 0 && col < 4) || (row == 3 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 6) || (col == 4 && row > 3 && row < 6);
          case '7': return (row == 0) || (col == 4 && row < 6);
          case '8': return (row == 0 && col > 0 && col < 4) || (row == 3 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 6) || (col == 4 && row > 0 && row < 6);
          case '9': return (row == 0 && col > 0 && col < 4) || (row == 3 && col > 0 && col < 4) || (row == 6 && col > 0 && col < 4) || (col == 0 && row > 0 && row < 3) || (col == 4 && row > 0 && row < 6);
          case ':': return (row == 2 || row == 4) && col == 2;
          case '/': return row + col == 4;
          case '=': return (row == 2 || row == 4) && col > 0 && col < 4;
          case '-': return row == 3 && col > 0 && col < 4;
          case '_': return row == 6 && col < 5;
          default: return false;
        }
      };

      for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
          if (!glyph(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))), row, col)) continue;
          SDL_FRect dot{cx + col * scale, y + row * scale, scale, scale};
          SDL_RenderFillRect(renderer, &dot);
        }
      }
      cx += 6.0f * scale;
    }
  };

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const auto& tile : tiles) {
    if (tile.id < 0 || tile.id >= static_cast<int>(tile_status.size())) continue;
    const auto& status = tile_status[static_cast<std::size_t>(tile.id)];
    SDL_FRect rect{static_cast<float>(tile.x0) * scale_x, static_cast<float>(tile.y0) * scale_y,
                   static_cast<float>(tile.x1 - tile.x0) * scale_x, static_cast<float>(tile.y1 - tile.y0) * scale_y};
    switch (status.state) {
      case TileState::Queued:
        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 90);
        SDL_RenderRect(renderer, &rect);
        break;
      case TileState::InProgress:
        SDL_SetRenderDrawColor(renderer, 255, 170, 0, 70);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 255, 210, 80, 220);
        SDL_RenderRect(renderer, &rect);
        break;
      case TileState::Completed:
        SDL_SetRenderDrawColor(renderer, 80, 220, 120, 70);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 120, 255, 160, 220);
        SDL_RenderRect(renderer, &rect);
        break;
    }
  }

  auto completed = progress.tiles_completed;
  bool paused = progress.paused;
  bool done = progress.complete;
  double elapsed_s = elapsed_ms / 1000.0;
  std::ostringstream elapsed_oss;
  elapsed_oss << std::fixed << std::setprecision(3) << elapsed_s << " s";
  auto title = std::string("Parallel Renderer | scene=") + config.scene_name + " mode=" + config.schedule_mode + " threads=" +
               std::to_string(progress.thread_count) + " tiles=" + std::to_string(completed) + "/" +
               std::to_string(progress.total_tiles) + " elapsed=" + elapsed_oss.str();
  if (SDL_Window* window = SDL_GetRenderWindow(renderer)) {
    SDL_SetWindowTitle(window, title.c_str());
  }

  const float text_scale = 1.5f;
  const float glyph_advance = 6.0f * text_scale;
  const float line_height = 9.0f * text_scale;
  const float padding = 10.0f;
  const std::vector<std::string> lines = {
      "scene: " + config.scene_name,
      "mode: " + config.schedule_mode,
      "threads: " + std::to_string(std::max(1, config.thread_count)),
      "tiles: " + std::to_string(completed) + "/" + std::to_string(progress.total_tiles),
      "elapsed: " + elapsed_oss.str(),
      std::string("state: ") + (paused ? "paused" : (done ? "done" : "running")),
  };

  float content_w = 0.0f;
  for (const auto& line : lines) content_w = std::max(content_w, static_cast<float>(line.size()) * glyph_advance);
  float panel_w = content_w + padding * 2.0f;
  float panel_h = static_cast<float>(lines.size()) * line_height + padding * 2.0f;
  panel_w = std::min(panel_w, static_cast<float>(std::max(0, output_w - 16)));
  panel_h = std::min(panel_h, static_cast<float>(std::max(0, output_h - 16)));
  float panel_x = 8.0f;
  float panel_y = 8.0f;
  draw_box(panel_x, panel_y, panel_w, panel_h, 0, 0, 0, 150);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_FRect outline{panel_x, panel_y, panel_w, panel_h};
  SDL_RenderRect(renderer, &outline);
  for (std::size_t i = 0; i < lines.size(); ++i) {
      draw_text(panel_x + padding, panel_y + padding + static_cast<float>(i) * line_height, lines[i], text_scale, 255, 255, 255);
  }
}

SdlViewerResult run_sdl_viewer(const Scene& scene, const Camera& camera, const RenderConfig& config, Framebuffer& framebuffer) {
  SdlViewerResult result;
  RenderController controller;
  auto job = controller.start_job(scene, camera, config);
  std::cerr << "viewer backend=SDL3\n";
  print_sdl_video_diagnostics();

  const char* original_driver = SDL_getenv("SDL_VIDEODRIVER");
  if (!try_sdl_video_init_with_driver(nullptr)) {
    if (!try_sdl_video_init_with_driver("wayland")) {
      if (!try_sdl_video_init_with_driver("x11")) {
        if (original_driver) SDL_setenv_unsafe("SDL_VIDEODRIVER", original_driver, 1);
        else SDL_unsetenv_unsafe("SDL_VIDEODRIVER");
        result.fallback_reason = "SDL video init failed; using terminal fallback";
        return result;
      }
    }
  }

  std::cerr << "chosen video driver=" << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "<unknown>") << '\n';
  SDL_Window* window = SDL_CreateWindow("Parallel Renderer", config.width, config.height, SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    result.fallback_reason = "SDL_CreateWindow failed; using terminal fallback";
    SDL_Quit();
    return result;
  }

  print_sdl_render_diagnostics();
  SDL_Renderer* renderer = nullptr;
  const char* renderer_order[] = {nullptr, "gpu", "opengl", "opengles2", "software"};
  for (const char* driver : renderer_order) {
    renderer = SDL_CreateRenderer(window, driver);
    if (renderer) {
      std::cerr << "selected renderer=" << SDL_GetRendererName(renderer) << " (requested " << (driver ? driver : "default") << ")\n";
      break;
    }
    std::cerr << "SDL_CreateRenderer(" << (driver ? driver : "default") << ") failed: " << SDL_GetError() << '\n';
  }

  if (!renderer) {
    result.fallback_reason = "SDL_CreateRenderer failed; using terminal fallback";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
  }

  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, config.width, config.height);
  if (!texture) {
    std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
    result.fallback_reason = "SDL_CreateTexture failed; using terminal fallback";
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
  }

  Framebuffer viewer_framebuffer(config.width, config.height);

  if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)) {
    std::cerr << "SDL_SetRenderDrawBlendMode failed: " << SDL_GetError() << '\n';
  }

  std::cerr << "texture pixel format=" << SDL_GetPixelFormatName(SDL_PIXELFORMAT_ARGB8888) << '\n';

  auto last_stats_print = std::chrono::steady_clock::now();
  int refresh_ms = std::clamp(config.viewer_refresh_ms, 16, 250);
  bool running = true;
  auto last_draw = std::chrono::steady_clock::now();
  bool render_joined = false;
  RenderOverlaySnapshot overlay_layout = job->overlay_snapshot();
  auto apply_tile_updates = [&]() {
    auto updates = job->consume_tile_updates();
    bool needs_full_refresh = false;
    for (auto& update : updates) {
      if (update.id < 0) continue;
      if (update.id >= static_cast<int>(overlay_layout.tiles.size())) {
        continue;
      }
      if (!update.pixels.empty()) {
        const auto& tile = overlay_layout.tiles[static_cast<std::size_t>(update.id)];
        for (int y = tile.y0; y < tile.y1; ++y) {
          for (int x = tile.x0; x < tile.x1; ++x) {
            viewer_framebuffer.at(x, y) = update.pixels[static_cast<std::size_t>(y - tile.y0) * static_cast<std::size_t>(tile.x1 - tile.x0) + static_cast<std::size_t>(x - tile.x0)];
          }
        }
        if (!sdl_set_texture_tile(texture, tile, update.pixels)) {
          needs_full_refresh = true;
        }
      }
    }
    if (needs_full_refresh && !sdl_set_texture_from_framebuffer(texture, viewer_framebuffer)) {
      std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
      job->cancel();
      running = false;
      result.cancelled = true;
    }
  };
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
        job->cancel();
        running = false;
        result.cancelled = true;
      } else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
          job->cancel();
          running = false;
          result.cancelled = true;
        } else if (event.key.key == SDLK_SPACE) {
          if (job->progress().paused) job->resume(); else job->pause();
        }
      }
    }

    auto now = std::chrono::steady_clock::now();
    apply_tile_updates();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw).count() >= refresh_ms) {
      auto progress = job->progress();
      double elapsed_ms = progress.elapsed_seconds * 1000.0;
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
      auto overlay = job->overlay_snapshot();
      draw_sdl_overlay(renderer, progress, overlay.tiles, overlay.tile_status, config, elapsed_ms);
      SDL_RenderPresent(renderer);
      last_draw = now;
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_print).count() >= 500) {
      auto progress = job->progress();
      std::cout << "[viewer] scene=" << progress.scene_name << " mode=" << progress.schedule_mode << " threads=" << progress.thread_count
                << " tiles=" << progress.tiles_completed << '/' << progress.total_tiles << " elapsed=" << std::fixed << std::setprecision(3)
                << progress.elapsed_seconds << " s\n";
      last_stats_print = now;
    }

    if (job->is_complete() && !render_joined) {
      job->join();
      render_joined = true;
      apply_tile_updates();
      if (config.save_output) framebuffer = viewer_framebuffer;
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
      auto overlay = job->overlay_snapshot();
      auto complete_progress = job->progress();
      draw_sdl_overlay(renderer, complete_progress, overlay.tiles, overlay.tile_status, config, complete_progress.elapsed_seconds * 1000.0);
      SDL_RenderPresent(renderer);
      std::cerr << "viewer complete, waiting for close or escape\n";
    }
    SDL_Delay(8);
  }

  if (!render_joined) job->join();
  apply_tile_updates();
  if (config.save_output) framebuffer = viewer_framebuffer;
  SDL_RenderClear(renderer);
  SDL_RenderTexture(renderer, texture, nullptr, nullptr);
  {
    auto overlay = job->overlay_snapshot();
    auto complete_progress = job->progress();
    draw_sdl_overlay(renderer, complete_progress, overlay.tiles, overlay.tile_status, config, complete_progress.elapsed_seconds * 1000.0);
  }
  SDL_RenderPresent(renderer);
  result.success = true;

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}
#endif

}  // namespace pr
