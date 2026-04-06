#include "render_engine.h"

#include "viewer_state.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

namespace pr {
namespace {

inline std::uint64_t hash64(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

inline std::uint64_t sample_seed(std::uint64_t base, int x, int y, int sample) {
  return hash64(base ^ (static_cast<std::uint64_t>(x) << 1) ^ (static_cast<std::uint64_t>(y) << 17) ^ (static_cast<std::uint64_t>(sample) << 33));
}

inline std::uint32_t pack_color(const Vec3& c, int samples_per_pixel) {
  auto scale = 1.0 / std::max(1, samples_per_pixel);
  auto r = std::sqrt(std::clamp(c.x * scale, 0.0, 0.999));
  auto g = std::sqrt(std::clamp(c.y * scale, 0.0, 0.999));
  auto b = std::sqrt(std::clamp(c.z * scale, 0.0, 0.999));
  auto ir = static_cast<std::uint32_t>(256.0 * r);
  auto ig = static_cast<std::uint32_t>(256.0 * g);
  auto ib = static_cast<std::uint32_t>(256.0 * b);
  return (ir << 16) | (ig << 8) | ib;
}

inline std::vector<Tile> build_tiles(int width, int height, int tile_size) {
  std::vector<Tile> tiles;
  int id = 0;
  for (int y = 0; y < height; y += tile_size) {
    for (int x = 0; x < width; x += tile_size) {
      tiles.push_back(Tile{id++, x, y, std::min(x + tile_size, width), std::min(y + tile_size, height)});
    }
  }
  return tiles;
}

inline Vec3 ray_color(const Ray& r, const Scene& world, int depth, Rng& rng) {
  if (depth <= 0) return Vec3{0, 0, 0};

  HitRecord rec;
  if (world.hit(r, 0.001, std::numeric_limits<double>::infinity(), rec)) {
    const Material& m = *rec.material;
    switch (m.kind) {
      case MaterialKind::Lambertian: {
        Vec3 scatter_dir = rec.normal + random_unit_vector(rng);
        if (scatter_dir.near_zero()) scatter_dir = rec.normal;
        return m.albedo * ray_color(Ray{rec.p, scatter_dir}, world, depth - 1, rng);
      }
      case MaterialKind::Metal: {
        Vec3 reflected = reflect(unit_vector(r.direction), rec.normal);
        Ray scattered{rec.p, reflected + m.fuzz * random_in_unit_sphere(rng)};
        if (dot(scattered.direction, rec.normal) > 0) {
          return m.albedo * ray_color(scattered, world, depth - 1, rng);
        }
        return Vec3{0, 0, 0};
      }
      case MaterialKind::Dielectric: {
        double refraction_ratio = rec.front_face ? (1.0 / m.ir) : m.ir;
        Vec3 unit_dir = unit_vector(r.direction);
        double cos_theta = std::min(dot(-unit_dir, rec.normal), 1.0);
        double sin_theta = std::sqrt(1.0 - cos_theta * cos_theta);
        bool cannot_refract = refraction_ratio * sin_theta > 1.0;
        auto schlick = [&](double cosine, double ref_idx) {
          auto r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
          r0 = r0 * r0;
          return r0 + (1.0 - r0) * std::pow(1.0 - cosine, 5.0);
        };
        Vec3 direction;
        if (cannot_refract || schlick(cos_theta, refraction_ratio) > rng.uniform01()) {
          direction = reflect(unit_dir, rec.normal);
        } else {
          direction = refract(unit_dir, rec.normal, refraction_ratio);
        }
        return ray_color(Ray{rec.p, direction}, world, depth - 1, rng);
      }
    }
  }

  Vec3 unit_direction = unit_vector(r.direction);
  auto t = 0.5 * (unit_direction.y + 1.0);
  return (1.0 - t) * Vec3{1.0, 1.0, 1.0} + t * Vec3{0.5, 0.7, 1.0};
}

inline Vec3 render_pixel(const Scene& scene, const Camera& camera, const RenderConfig& config, int x, int y, ViewerState* viewer_state) {
  Vec3 color{0, 0, 0};
  for (int s = 0; s < config.samples_per_pixel; ++s) {
    while (viewer_state && viewer_state->paused.load()) {
      if (viewer_state->cancelled.load()) return Vec3{0, 0, 0};
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (viewer_state && viewer_state->cancelled.load()) return Vec3{0, 0, 0};
    Rng rng(sample_seed(config.seed, x, y, s));
    auto u = (x + rng.uniform01()) / (config.width - 1.0);
    auto v = ((config.height - 1 - y) + rng.uniform01()) / (config.height - 1.0);
    auto r = camera.get_ray(u, v);
    color += ray_color(r, scene, config.max_depth, rng);
  }
  return color;
}

inline void commit_tile(Framebuffer& framebuffer, const Tile& tile, const std::vector<std::uint32_t>& pixels) {
  for (int y = tile.y0; y < tile.y1; ++y) {
    for (int x = tile.x0; x < tile.x1; ++x) {
      framebuffer.at(x, y) = pixels[static_cast<size_t>(y - tile.y0) * static_cast<size_t>(tile.x1 - tile.x0) + static_cast<size_t>(x - tile.x0)];
    }
  }
}

inline void push_log(ViewerState* state, const std::string& msg) {
  if (!state) return;
  std::lock_guard<std::mutex> lock(state->mutex);
  state->logs.push_back(msg);
  while (state->logs.size() > 8) state->logs.pop_front();
}

inline void clear_viewer_state(ViewerState& state, int width, int height, const std::vector<Tile>& tiles) {
  std::lock_guard<std::mutex> lock(state.mutex);
  state.framebuffer_ref().resize(width, height);
  state.tiles = tiles;
  state.tile_status.assign(tiles.size(), TileStatus{});
  state.tile_updates.clear();
  state.tiles_completed.store(0);
  state.paused.store(false);
  state.cancelled.store(false);
  state.render_complete.store(false);
  state.logs.clear();
}

}  // namespace

RenderStats render(const Scene& scene, const Camera& camera, Framebuffer& framebuffer, const RenderConfig& config, ViewerState* viewer_state) {
  framebuffer.resize(config.width, config.height);
  auto tiles = build_tiles(config.width, config.height, std::max(1, config.tile_size));
  if (viewer_state) {
    clear_viewer_state(*viewer_state, config.width, config.height, tiles);
    push_log(viewer_state, "render started");
  }

  auto start = std::chrono::steady_clock::now();
  auto render_tile = [&](const Tile& tile, int worker_id) {
    std::vector<std::uint32_t> tile_pixels(static_cast<size_t>(tile.x1 - tile.x0) * static_cast<size_t>(tile.y1 - tile.y0));
    for (int y = tile.y0; y < tile.y1; ++y) {
      if (viewer_state && viewer_state->cancelled.load()) return;
      for (int x = tile.x0; x < tile.x1; ++x) {
        if (viewer_state && viewer_state->cancelled.load()) return;
        Vec3 color = render_pixel(scene, camera, config, x, y, viewer_state);
        tile_pixels[static_cast<size_t>(y - tile.y0) * static_cast<size_t>(tile.x1 - tile.x0) + static_cast<size_t>(x - tile.x0)] = pack_color(color, config.samples_per_pixel);
      }
    }
    if (viewer_state) {
      std::lock_guard<std::mutex> lock(viewer_state->mutex);
      commit_tile(framebuffer, tile, tile_pixels);
      viewer_state->tile_status[static_cast<size_t>(tile.id)].state = TileState::Completed;
      viewer_state->tile_status[static_cast<size_t>(tile.id)].worker_id = worker_id;
      viewer_state->tile_status[static_cast<size_t>(tile.id)].end_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      viewer_state->tile_updates.push_back(TileUpdate{tile.id, viewer_state->tile_status[static_cast<size_t>(tile.id)], std::move(tile_pixels)});
      viewer_state->tiles_completed.fetch_add(1);
    } else {
      commit_tile(framebuffer, tile, tile_pixels);
    }
  };

  auto run_worker_range = [&](int begin, int end, int worker_id) {
    for (int i = begin; i < end; ++i) {
      if (viewer_state && viewer_state->cancelled.load()) return;
      if (viewer_state && viewer_state->paused.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        --i;
        continue;
      }
      if (viewer_state) {
        std::lock_guard<std::mutex> lock(viewer_state->mutex);
        viewer_state->tile_status[static_cast<size_t>(i)].state = TileState::InProgress;
        viewer_state->tile_status[static_cast<size_t>(i)].worker_id = worker_id;
        viewer_state->tile_status[static_cast<size_t>(i)].start_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      }
      render_tile(tiles[static_cast<size_t>(i)], worker_id);
    }
  };

  auto run_dynamic = [&](int worker_id, std::atomic<int>& next_tile) {
    for (;;) {
      if (viewer_state && viewer_state->cancelled.load()) return;
      if (viewer_state && viewer_state->paused.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      int i = next_tile.fetch_add(1);
      if (i >= static_cast<int>(tiles.size())) return;
      if (viewer_state) {
        std::lock_guard<std::mutex> lock(viewer_state->mutex);
        viewer_state->tile_status[static_cast<size_t>(i)].state = TileState::InProgress;
        viewer_state->tile_status[static_cast<size_t>(i)].worker_id = worker_id;
        viewer_state->tile_status[static_cast<size_t>(i)].start_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      }
      render_tile(tiles[static_cast<size_t>(i)], worker_id);
    }
  };

  auto worker_count = resolved_thread_count(config);
  if (config.schedule_mode == "serial" || worker_count == 1) {
    if (!tiles.empty()) run_worker_range(0, static_cast<int>(tiles.size()), 0);
  } else if (config.schedule_mode == "static") {
    std::vector<std::thread> workers;
    int per = static_cast<int>((tiles.size() + static_cast<size_t>(worker_count) - 1) / static_cast<size_t>(worker_count));
    for (int w = 0; w < worker_count; ++w) {
      int begin = w * per;
      int end = std::min<int>(static_cast<int>(tiles.size()), begin + per);
      if (begin >= end) break;
      workers.emplace_back(run_worker_range, begin, end, w);
    }
    for (auto& t : workers) t.join();
  } else {
    std::atomic<int> next_tile{0};
    std::vector<std::thread> workers;
    for (int w = 0; w < worker_count; ++w) {
      workers.emplace_back(run_dynamic, w, std::ref(next_tile));
    }
    for (auto& t : workers) t.join();
  }

  auto end = std::chrono::steady_clock::now();
  RenderStats stats;
  stats.milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
  stats.total_tiles = tiles.size();
  stats.tiles_completed = tiles.size();
  stats.pixels = static_cast<size_t>(config.width) * static_cast<size_t>(config.height);
  stats.thread_count = static_cast<size_t>(worker_count);
  stats.schedule_mode = config.schedule_mode;
  stats.scene_name = scene.name;
  if (viewer_state) {
    viewer_state->render_complete.store(true);
    push_log(viewer_state, "render complete");
  }
  return stats;
}

}  // namespace pr
