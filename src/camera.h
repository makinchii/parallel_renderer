// Camera construction boundary.
// Depends on scene geometry only, not on registry or render/UI code.

#pragma once

#include "scene.h"

namespace pr {

struct Camera {
  Vec3 origin;
  Vec3 lower_left_corner;
  Vec3 horizontal;
  Vec3 vertical;

  Ray get_ray(double u, double v) const { return Ray{origin, lower_left_corner + u * horizontal + v * vertical - origin}; }
};

Camera make_camera(int width, int height);

}  // namespace pr
