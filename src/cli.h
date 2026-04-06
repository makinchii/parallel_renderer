#pragma once

#include "render_core.h"

#include <string>

namespace pr::cli {

struct Options {
  RenderConfig config;
  std::string mode = "gui";
  std::string threads_arg;
  std::string schedule_arg;
};

void usage();
Options parse_args(int argc, char** argv);

}  // namespace pr::cli
