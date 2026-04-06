#include "cli.h"

#include <cstdlib>
#include <iostream>

namespace pr::cli {

void usage() {
  std::cout << "Usage: renderer [options]\n"
            << "  --mode gui|viewer|benchmark|render|test\n"
            << "  --scene simple|medium|heavy|cornell|tilted\n"
            << "  --width N --height N --spp N --depth N\n"
            << "  --threads N or N,N,N --schedule serial|static|dynamic or list\n"
            << "  --tile N --seed N\n"
            << "  --save output.png\n"
            << "  --csv results.csv --runs N\n";
}

Options parse_args(int argc, char** argv) {
  Options options;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](std::string& out) {
      if (i + 1 < argc) out = argv[++i];
    };
    auto next_int = [&](int& out) {
      if (i + 1 < argc) out = std::atoi(argv[++i]);
    };
    auto next_u64 = [&](std::uint64_t& out) {
      if (i + 1 < argc) out = static_cast<std::uint64_t>(std::strtoull(argv[++i], nullptr, 10));
    };

    if (arg == "--help" || arg == "-h") {
      usage();
      std::exit(0);
    } else if (arg == "--mode") {
      next(options.mode);
    } else if (arg == "--scene") {
      next(options.config.scene_name);
    } else if (arg == "--width") {
      next_int(options.config.width);
    } else if (arg == "--height") {
      next_int(options.config.height);
    } else if (arg == "--spp") {
      next_int(options.config.samples_per_pixel);
    } else if (arg == "--depth") {
      next_int(options.config.max_depth);
    } else if (arg == "--threads") {
      next(options.threads_arg);
      auto counts = parse_int_list(options.threads_arg);
      if (!counts.empty()) options.config.thread_count = counts.front();
    } else if (arg == "--schedule") {
      next(options.schedule_arg);
      auto schedules = parse_string_list(options.schedule_arg);
      if (!schedules.empty()) options.config.schedule_mode = schedules.front();
    } else if (arg == "--tile") {
      next_int(options.config.tile_size);
    } else if (arg == "--seed") {
      next_u64(options.config.seed);
    } else if (arg == "--save") {
      next(options.config.output_filename);
      options.config.save_output = true;
    } else if (arg == "--csv") {
      next(options.config.csv_output);
    } else if (arg == "--runs") {
      next_int(options.config.benchmark_runs);
    } else if (arg == "--refresh-ms") {
      next_int(options.config.viewer_refresh_ms);
    }
  }

  if (options.mode.empty()) options.mode = "gui";

  return options;
}

}  // namespace pr::cli
