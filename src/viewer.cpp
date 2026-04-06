#include "viewer.h"

#include "camera.h"
#include "render_engine.h"
#include "scene_registry.h"
#include "png_writer.h"
#include "viewer_sdl.h"
#include "viewer_terminal.h"

#include <iostream>

namespace pr {

int run_viewer_mode(const cli::Options& options) {
  const RenderConfig& config = options.config;
  Scene scene = build_scene(config.scene_name);
  Camera camera = make_camera(config.width, config.height);
  Framebuffer framebuffer(config.width, config.height);

  #if PR_HAS_SDL3
  auto viewer_result = run_sdl_viewer(scene, camera, config, framebuffer);
  if (!viewer_result.success && !viewer_result.cancelled) {
    if (!viewer_result.fallback_reason.empty()) {
      std::cerr << viewer_result.fallback_reason << '\n';
    } else {
      std::cerr << "SDL viewer unavailable, falling back to terminal viewer\n";
    }
    run_viewer_terminal(scene, camera, config, framebuffer);
  } else if (!viewer_result.success) {
    return 1;
  }
  #else
  std::cerr << "SDL support unavailable; using terminal viewer\n";
  run_viewer_terminal(scene, camera, config, framebuffer);
  #endif

  if (config.save_output) {
    if (!save_png(framebuffer, config.output_filename)) {
      std::cerr << "failed to save image\n";
      return 1;
    }
  }
  return 0;
}

}  // namespace pr
