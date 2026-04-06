#include "scene_registry.h"

#include <cstdlib>

namespace pr {

std::vector<std::string> available_scene_names() {
  return {"simple", "medium", "heavy", "cornell", "tilted"};
}

Scene build_scene(const std::string& name) {
  Scene scene;
  scene.name = name;
  scene.objects.push_back(Sphere{Vec3{0, -1000, 0}, 1000.0, Material::lambertian(Vec3{0.5, 0.5, 0.5})});

  if (name == "heavy") {
    for (int a = -11; a < 11; ++a) {
      for (int b = -11; b < 11; ++b) {
        Vec3 center{a + 0.9 * (0.15 * ((a * 17 + b * 31) % 7)), 0.2, b + 0.9 * (0.11 * ((a * 13 + b * 19) % 9))};
        if ((center - Vec3{4, 0.2, 0}).length() > 0.9) {
          int choice = std::abs((a * 97 + b * 57) % 3);
          if (choice == 0) {
            scene.objects.push_back(Sphere{center, 0.2, Material::lambertian(Vec3{0.3 + 0.7 * ((a + 11) / 22.0), 0.2 + 0.7 * ((b + 11) / 22.0), 0.4})});
          } else if (choice == 1) {
            scene.objects.push_back(Sphere{center, 0.2, Material::metal(Vec3{0.8, 0.8, 0.8}, 0.05 * ((a + b + 22) % 5))});
          } else {
            scene.objects.push_back(Sphere{center, 0.2, Material::dielectric(1.5)});
          }
        }
      }
    }
    scene.objects.push_back(Sphere{Vec3{0, 1, 0}, 1.0, Material::dielectric(1.5)});
    scene.objects.push_back(Sphere{Vec3{-4, 1, 0}, 1.0, Material::lambertian(Vec3{0.4, 0.2, 0.1})});
    scene.objects.push_back(Sphere{Vec3{4, 1, 0}, 1.0, Material::metal(Vec3{0.7, 0.6, 0.5}, 0.0)});
  } else if (name == "medium") {
    scene.objects.push_back(Sphere{Vec3{0, 1, 0}, 1.0, Material::dielectric(1.5)});
    scene.objects.push_back(Sphere{Vec3{-4, 1, 0}, 1.0, Material::lambertian(Vec3{0.4, 0.2, 0.1})});
    scene.objects.push_back(Sphere{Vec3{4, 1, 0}, 1.0, Material::metal(Vec3{0.7, 0.6, 0.5}, 0.1)});
    scene.objects.push_back(Sphere{Vec3{-2, 0.25, -1}, 0.25, Material::lambertian(Vec3{0.2, 0.8, 0.3})});
    scene.objects.push_back(Sphere{Vec3{2, 0.35, -1.5}, 0.35, Material::metal(Vec3{0.9, 0.8, 0.7}, 0.0)});
    scene.objects.push_back(Sphere{Vec3{0, 0.5, -2}, 0.5, Material::lambertian(Vec3{0.2, 0.3, 0.8})});
  } else if (name == "cornell") {
    scene.objects.push_back(Sphere{Vec3{-1.2, 0.75, -1.2}, 0.75, Material::lambertian(Vec3{0.95, 0.25, 0.25})});
    scene.objects.push_back(Sphere{Vec3{1.0, 0.8, -0.8}, 0.8, Material::metal(Vec3{0.85, 0.85, 0.9}, 0.02)});
    scene.objects.push_back(Sphere{Vec3{0.0, 1.4, -2.5}, 1.4, Material::dielectric(1.35)});
  } else if (name == "tilted") {
    scene.objects.push_back(Sphere{Vec3{-2.0, 0.9, -1.5}, 0.9, Material::lambertian(Vec3{0.8, 0.7, 0.2})});
    scene.objects.push_back(Sphere{Vec3{0.0, 0.6, -1.0}, 0.6, Material::lambertian(Vec3{0.2, 0.7, 0.8})});
    scene.objects.push_back(Sphere{Vec3{2.0, 0.7, -1.8}, 0.7, Material::metal(Vec3{0.9, 0.6, 0.4}, 0.15)});
  } else {
    scene.objects.push_back(Sphere{Vec3{0, 1, 0}, 1.0, Material::dielectric(1.5)});
    scene.objects.push_back(Sphere{Vec3{-4, 1, 0}, 1.0, Material::lambertian(Vec3{0.4, 0.2, 0.1})});
    scene.objects.push_back(Sphere{Vec3{4, 1, 0}, 1.0, Material::metal(Vec3{0.7, 0.6, 0.5}, 0.0)});
  }
  return scene;
}

}  // namespace pr
