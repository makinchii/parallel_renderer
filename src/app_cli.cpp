#include "app.h"

#include "cli.h"
#include "benchmark_runner.h"
#include "camera.h"
#include "csv_writer.h"
#include "render_engine.h"
#include "png_writer.h"
#include "scene_registry.h"
#include "viewer.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

namespace pr {
namespace {

bool run_self_tests() {
  if (!approx_equal(Vec3{1, 2, 3} + Vec3{2, 3, 4}, Vec3{3, 5, 7})) return false;
  if (!approx_equal(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1})) return false;

  Scene scene = build_scene("simple");
  Ray ray{Vec3{0, 1, 3}, unit_vector(Vec3{0, -0.2, -1})};
  HitRecord rec;
  if (!scene.hit(ray, 0.001, 1000.0, rec)) return false;

  Camera camera = make_camera(32, 18);
  auto center = camera.get_ray(0.5, 0.5);
  if (center.direction.length() <= 0.0) return false;

  RenderConfig cfg;
  cfg.width = 32;
  cfg.height = 18;
  cfg.samples_per_pixel = 2;
  cfg.max_depth = 4;
  cfg.thread_count = 1;
  cfg.schedule_mode = "serial";
  cfg.scene_name = "simple";
  cfg.seed = 42;
  Framebuffer fb1(cfg.width, cfg.height);
  Framebuffer fb2(cfg.width, cfg.height);
  auto stats1 = render(scene, camera, fb1, cfg, nullptr);
  cfg.schedule_mode = "dynamic";
  cfg.thread_count = 4;
  auto stats2 = render(scene, camera, fb2, cfg, nullptr);
  (void)stats1;
  (void)stats2;
  return fb1.pixels == fb2.pixels;
}

int run_benchmark_mode(const cli::Options& options) {
  const RenderConfig& config = options.config;
  Scene scene = build_scene(config.scene_name);
  Camera camera = make_camera(config.width, config.height);

  std::vector<int> threads = options.threads_arg.empty() ? (config.thread_count > 0 ? std::vector<int>{config.thread_count} : std::vector<int>{resolved_thread_count(config)}) : parse_int_list(options.threads_arg);
  if (threads.empty()) threads = config.thread_count > 0 ? std::vector<int>{config.thread_count} : std::vector<int>{resolved_thread_count(config)};
  std::vector<std::string> schedules = options.schedule_arg.empty() ? std::vector<std::string>{config.schedule_mode} : parse_string_list(options.schedule_arg);
  if (schedules.empty()) schedules = {config.schedule_mode};
  if (schedules.size() == 1 && schedules.front() == "all") schedules = {"serial", "static", "dynamic"};

  auto rows = run_benchmarks(scene, camera, config, threads, schedules);
  if (!write_benchmark_csv(config.csv_output, rows)) {
    std::cerr << "failed to write CSV: " << config.csv_output << '\n';
    return 1;
  }
  std::cout << "wrote " << rows.size() << " rows to " << config.csv_output << '\n';
  return 0;
}

int run_render_mode(const cli::Options& options) {
  const RenderConfig& config = options.config;
  Scene scene = build_scene(config.scene_name);
  Camera camera = make_camera(config.width, config.height);
  Framebuffer framebuffer(config.width, config.height);

  auto stats = render(scene, camera, framebuffer, config, nullptr);
  std::cout << format_stats_line(stats, config.width, config.height, config.samples_per_pixel, config.max_depth) << '\n';
  if (config.save_output && !save_png(framebuffer, config.output_filename)) {
    std::cerr << "failed to save image\n";
    return 1;
  }
  return 0;
}

}  // namespace

int run_cli_app(int argc, char** argv) {
  auto options = cli::parse_args(argc, argv);

  if (options.mode.empty() || options.mode == "render") {
    return run_render_mode(options);
  }

  if (options.mode == "benchmark") {
    return run_benchmark_mode(options);
  }

  if (options.mode == "viewer" || options.config.viewer_enabled) {
    return run_viewer_mode(options);
  }

  if (options.mode == "test") {
    return run_self_tests() ? 0 : 1;
  }

  std::cerr << "unknown mode: " << options.mode << '\n';
  cli::usage();
  return 1;
}

}  // namespace pr
