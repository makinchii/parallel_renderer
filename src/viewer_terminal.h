#pragma once

#include "camera.h"
#include "render_core.h"
#include "viewer_state.h"

namespace pr {

void print_viewer_frame(ViewerState& state, const RenderStats& stats, const RenderConfig& config);
void run_viewer_terminal(const Scene& scene, const Camera& camera, const RenderConfig& config, Framebuffer& framebuffer);

}  // namespace pr
