#include "camera.h"

namespace pr {

Camera make_camera(int width, int height) {
  Camera cam;
  cam.origin = Vec3{8.5, 2.8, 8.0};
  Vec3 lookat{0, 1.2, 0.2};
  Vec3 vup{0, 1, 0};
  auto aspect_ratio = static_cast<double>(width) / static_cast<double>(height);
  auto theta = 34.0 * kPi / 180.0;
  auto h = std::tan(theta / 2.0);
  auto viewport_height = 2.0 * h;
  auto viewport_width = aspect_ratio * viewport_height;
  auto w = unit_vector(cam.origin - lookat);
  auto u = unit_vector(cross(vup, w));
  auto v = cross(w, u);

  cam.horizontal = viewport_width * u;
  cam.vertical = viewport_height * v;
  cam.lower_left_corner = cam.origin - cam.horizontal / 2.0 - cam.vertical / 2.0 - w;
  return cam;
}

}  // namespace pr
