#include "math.h"
#include "camera.h"
#include "benchmark_runner.h"
#include "render_core.h"
#include "render_engine.h"
#include "csv_writer.h"
#include "png_writer.h"
#include "scene.h"
#include "scene_registry.h"
#include "viewer_state.h"

#include <cstdint>
#include <iostream>

using namespace pr;

int main() {
  bool ok = true;
  ok = ok && approx_equal(Vec3{1, 2, 3} + Vec3{4, 5, 6}, Vec3{5, 7, 9});
  ok = ok && approx_equal(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1});

  const auto scenes = available_scene_names();
  ok = ok && scenes.size() >= 5;

  for (const auto& name : {std::string("simple"), std::string("medium"), std::string("heavy")}) {
    Scene scene = build_scene(name);
    ok = ok && scene.name == name;
    ok = ok && !scene.objects.empty();
  }

  Scene scene = build_scene("simple");
  Ray ray{Vec3{0, 1, 3}, unit_vector(Vec3{0, -0.2, -1})};
  HitRecord rec;
  ok = ok && scene.hit(ray, 0.001, 1000.0, rec);

  Camera camera = make_camera(16, 9);
  ok = ok && camera.get_ray(0.5, 0.5).direction.length() > 0.0;

  RenderConfig cfg;
  cfg.width = 24;
  cfg.height = 16;
  cfg.samples_per_pixel = 2;
  cfg.max_depth = 4;
  cfg.thread_count = 1;
  cfg.schedule_mode = "serial";
  cfg.scene_name = "simple";
  cfg.seed = 123;

  Framebuffer serial_fb(cfg.width, cfg.height);
  Framebuffer parallel_fb(cfg.width, cfg.height);
  render(scene, camera, serial_fb, cfg, nullptr);
  cfg.schedule_mode = "dynamic";
  cfg.thread_count = 4;
  render(scene, camera, parallel_fb, cfg, nullptr);
  ok = ok && serial_fb.pixels == parallel_fb.pixels;

  ok = ok && parse_int_list("1,2,4").size() == 3;
  ok = ok && parse_string_list("serial,dynamic").size() == 2;

  cfg.benchmark_runs = 1;
  auto bench_rows = run_benchmarks(scene, camera, cfg, std::vector<int>{1, 2}, std::vector<std::string>{"serial", "dynamic"});
  ok = ok && bench_rows.size() == 4;

  RenderConfig checksum_cfg;
  checksum_cfg.width = 8;
  checksum_cfg.height = 4;
  checksum_cfg.samples_per_pixel = 1;
  checksum_cfg.max_depth = 2;
  checksum_cfg.thread_count = 1;
  checksum_cfg.schedule_mode = "serial";
  checksum_cfg.scene_name = "simple";
  checksum_cfg.seed = 1234;

  Camera checksum_camera = make_camera(checksum_cfg.width, checksum_cfg.height);
  Framebuffer checksum_fb(checksum_cfg.width, checksum_cfg.height);
  render(scene, checksum_camera, checksum_fb, checksum_cfg, nullptr);

  std::uint64_t hash = 1469598103934665603ull;
  for (auto px : checksum_fb.pixels) {
    hash ^= px;
    hash *= 1099511628211ull;
  }
  ok = ok && hash == 3448531474989380987ull;

  for (const auto& name : {std::string("cornell"), std::string("tilted")}) {
    Scene extra = build_scene(name);
    ok = ok && !extra.objects.empty();
  }

  std::cout << (ok ? "ok" : "fail") << '\n';
  return ok ? 0 : 1;
}
