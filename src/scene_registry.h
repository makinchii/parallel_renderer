// Scene catalog and assembly boundary.
// Depends on geometry types only, not on render or UI code.

#pragma once

#include "scene.h"

#include <string>
#include <vector>

namespace pr {

std::vector<std::string> available_scene_names();
Scene build_scene(const std::string& name);

}  // namespace pr
