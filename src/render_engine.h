// Tile-based render engine entry point.
// Depends on core types and geometry, but not on UI code.

#pragma once

#include "camera.h"
#include "render_core.h"
#include "scene.h"

namespace pr {

struct ViewerState;

RenderStats render(const Scene& scene, const Camera& camera, Framebuffer& framebuffer, const RenderConfig& config, ViewerState* viewer_state);

}  // namespace pr
