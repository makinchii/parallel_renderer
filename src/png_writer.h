// PNG output boundary.
// Depends on core framebuffer types, but not on benchmark or UI code.

#pragma once

#include "render_core.h"

namespace pr {

bool save_png(const Framebuffer& framebuffer, const std::string& path);

}  // namespace pr
