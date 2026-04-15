#include "scene_registry.h"

#include <cstdlib>

namespace {

void add_open_stage(pr::Scene& scene, const pr::Vec3& floor_color, const pr::Vec3& back_wall_color, const pr::Vec3& light_color, double light_half_extent) {
  scene.objects.push_back(pr::XZRect{-5.0, 0.0, -5.0, 0.0, 0.0, pr::Material::lambertian(floor_color * pr::Vec3{0.97, 0.96, 0.95})});
  scene.objects.push_back(pr::XZRect{0.0, 5.0, -5.0, 0.0, 0.0, pr::Material::lambertian(floor_color * pr::Vec3{0.99, 0.98, 0.97})});
  scene.objects.push_back(pr::XZRect{-5.0, 0.0, 0.0, 5.0, 0.0, pr::Material::lambertian(floor_color * pr::Vec3{0.96, 0.97, 0.98})});
  scene.objects.push_back(pr::XZRect{0.0, 5.0, 0.0, 5.0, 0.0, pr::Material::lambertian(floor_color * pr::Vec3{0.98, 0.99, 0.97})});
  scene.objects.push_back(pr::XYRect{-4.8, 4.8, 0.0, 4.8, -4.8, pr::Material::lambertian(back_wall_color)});
  scene.objects.push_back(pr::XZRect{-light_half_extent, light_half_extent, -light_half_extent, light_half_extent, 4.4, pr::Material::light(light_color)});
}

}  // namespace

namespace pr {

std::vector<std::string> available_scene_names() {
  return {"simple", "medium", "heavy"};
}

Scene build_scene(const std::string& name) {
  Scene scene;
  scene.name = name;

  if (name == "simple") {
    scene.objects.push_back(XZRect{-4.0, 4.0, -4.0, 4.0, 0.0, Material::lambertian(Vec3{0.74, 0.75, 0.78})});
    scene.objects.push_back(XZRect{-1.2, 1.2, -1.2, 1.2, 4.0, Material::light(Vec3{7.0, 6.5, 6.0})});
    scene.objects.push_back(Sphere{Vec3{-1.1, 0.8, -1.0}, 0.8, Material::lambertian(Vec3{0.80, 0.30, 0.24})});
    scene.objects.push_back(Sphere{Vec3{1.0, 0.55, -0.3}, 0.55, Material::metal(Vec3{0.90, 0.85, 0.82}, 0.03)});
    scene.objects.push_back(Sphere{Vec3{0.0, 0.45, 1.0}, 0.45, Material::dielectric(1.45)});
  } else if (name == "medium") {
    add_open_stage(scene, Vec3{0.80, 0.78, 0.75}, Vec3{0.24, 0.26, 0.31}, Vec3{11.0, 10.4, 9.8}, 2.0);
    scene.objects.push_back(Sphere{Vec3{-1.5, 0.85, -0.8}, 0.85, Material::lambertian(Vec3{0.88, 0.34, 0.26})});
    scene.objects.push_back(Sphere{Vec3{-0.3, 0.55, -1.8}, 0.55, Material::metal(Vec3{0.84, 0.86, 0.88}, 0.05)});
    scene.objects.push_back(Sphere{Vec3{1.0, 0.72, -1.0}, 0.75, Material::dielectric(1.42)});
    scene.objects.push_back(Sphere{Vec3{0.3, 0.42, 0.35}, 0.42, Material::lambertian(Vec3{0.20, 0.68, 0.54})});
    scene.objects.push_back(Sphere{Vec3{-0.8, 0.28, 1.1}, 0.28, Material::lambertian(Vec3{0.80, 0.64, 0.22})});
  } else if (name == "heavy") {
    add_open_stage(scene, Vec3{0.75, 0.74, 0.72}, Vec3{0.21, 0.23, 0.28}, Vec3{13.8, 12.9, 12.0}, 2.1);
    scene.objects.push_back(Sphere{Vec3{-1.9, 0.76, -1.0}, 0.78, Material::dielectric(1.52)});
    scene.objects.push_back(Sphere{Vec3{-0.6, 0.56, -1.8}, 0.58, Material::metal(Vec3{0.88, 0.72, 0.48}, 0.07)});
    scene.objects.push_back(Sphere{Vec3{0.8, 0.89, -1.1}, 0.92, Material::lambertian(Vec3{0.24, 0.52, 0.84})});
    scene.objects.push_back(Sphere{Vec3{1.7, 0.46, -0.3}, 0.48, Material::lambertian(Vec3{0.72, 0.26, 0.58})});
    for (int i = 0; i < 18; ++i) {
      double x = -2.3 + 0.27 * static_cast<double>(i);
      double y = 0.10 + 0.03 * static_cast<double>(i % 4);
      double z = -1.9 + 0.25 * static_cast<double>((i * 3) % 8);
      Vec3 base{0.18 + 0.03 * static_cast<double>(i % 5), 0.26 + 0.05 * static_cast<double>(i % 4), 0.40 + 0.04 * static_cast<double>(i % 3)};
      Vec3 tint{0.96 + 0.01 * static_cast<double>(i % 3), 0.96 + 0.008 * static_cast<double>((i + 1) % 3), 0.97 + 0.006 * static_cast<double>((i + 2) % 3)};
      double radius = 0.08 + 0.015 * static_cast<double>(i % 3);
      if (i % 6 == 0) {
        scene.objects.push_back(Sphere{Vec3{x, y, z}, radius, Material::metal(base * tint * Vec3{1.06, 1.00, 0.92}, 0.10)});
      } else {
        scene.objects.push_back(Sphere{Vec3{x, y, z}, radius, Material::lambertian(base * tint)});
      }
    }
  } else {
    add_open_stage(scene, Vec3{0.76, 0.74, 0.71}, Vec3{0.20, 0.23, 0.28}, Vec3{13.8, 12.9, 12.0}, 2.1);
    scene.objects.push_back(Sphere{Vec3{-1.4, 0.82, -0.9}, 0.82, Material::lambertian(Vec3{0.92, 0.34, 0.26})});
    scene.objects.push_back(Sphere{Vec3{-0.1, 0.62, -1.7}, 0.62, Material::metal(Vec3{0.88, 0.82, 0.76}, 0.04)});
    scene.objects.push_back(Sphere{Vec3{1.1, 0.95, -1.1}, 0.95, Material::dielectric(1.5)});
    scene.objects.push_back(Sphere{Vec3{1.9, 0.52, -0.2}, 0.52, Material::lambertian(Vec3{0.24, 0.62, 0.40})});
    scene.objects.push_back(Sphere{Vec3{-0.8, 0.34, 0.9}, 0.34, Material::lambertian(Vec3{0.72, 0.52, 0.18})});
    scene.objects.push_back(Sphere{Vec3{0.7, 1.65, -0.2}, 0.26, Material::light(Vec3{14.8, 13.8, 12.5})});
    scene.objects.push_back(Sphere{Vec3{-2.0, 0.22, 1.5}, 0.22, Material::lambertian(Vec3{0.18, 0.36, 0.72})});
    scene.objects.push_back(Sphere{Vec3{0.0, 0.16, 1.8}, 0.16, Material::lambertian(Vec3{0.80, 0.28, 0.62})});
    for (int i = 0; i < 28; ++i) {
      double x = -2.5 + 0.20 * static_cast<double>(i);
      double y = 0.08 + 0.025 * static_cast<double>(i % 5);
      double z = -2.0 + 0.22 * static_cast<double>((i * 7) % 9);
      Vec3 base{0.15 + 0.03 * static_cast<double>(i % 6), 0.23 + 0.04 * static_cast<double>(i % 5), 0.34 + 0.05 * static_cast<double>(i % 4)};
      Vec3 tint{0.95 + 0.01 * static_cast<double>(i % 4), 0.96 + 0.008 * static_cast<double>((i + 2) % 4), 0.97 + 0.006 * static_cast<double>((i + 1) % 4)};
      double radius = 0.06 + 0.01 * static_cast<double>(i % 4);
      if (i % 7 == 0) {
        scene.objects.push_back(Sphere{Vec3{x, y, z}, radius, Material::metal(base * tint * Vec3{1.04, 1.01, 0.92}, 0.14)});
      } else {
        scene.objects.push_back(Sphere{Vec3{x, y, z}, radius, Material::lambertian(base * tint)});
      }
    }
  }
  return scene;
}

}  // namespace pr
