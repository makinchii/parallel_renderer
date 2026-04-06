#pragma once

#include "camera.h"
#include "render_core.h"

#include <string>

#if __has_include(<SDL3/SDL.h>)
#define PR_HAS_SDL3 1
#include <SDL3/SDL.h>
#else
#define PR_HAS_SDL3 0
#endif

namespace pr {

struct SdlViewerResult {
  bool success = false;
  bool cancelled = false;
  std::string fallback_reason;
};

SdlViewerResult run_sdl_viewer(const Scene& scene, const Camera& camera, const RenderConfig& config, Framebuffer& framebuffer);

}  // namespace pr
