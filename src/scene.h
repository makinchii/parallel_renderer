#pragma once

// Scene geometry and material types.
// This header must not depend on scene registry or render-engine code.

#include "math.h"

#include <algorithm>
#include <string>
#include <vector>

namespace pr {

struct Ray {
  Vec3 origin;
  Vec3 direction;

  Ray() = default;
  Ray(Vec3 o, Vec3 d) : origin(o), direction(d) {}
  Vec3 at(double t) const { return origin + t * direction; }
};

enum class MaterialKind { Lambertian, Metal, Dielectric };

struct Material {
  MaterialKind kind = MaterialKind::Lambertian;
  Vec3 albedo{0.7, 0.7, 0.7};
  double fuzz = 0.0;
  double ir = 1.5;

  static Material lambertian(Vec3 a) { return Material{MaterialKind::Lambertian, a, 0.0, 1.5}; }
  static Material metal(Vec3 a, double f) { return Material{MaterialKind::Metal, a, std::clamp(f, 0.0, 1.0), 1.5}; }
  static Material dielectric(double index) { return Material{MaterialKind::Dielectric, Vec3{1.0, 1.0, 1.0}, 0.0, index}; }
};

struct HitRecord {
  Vec3 p;
  Vec3 normal;
  double t = 0.0;
  bool front_face = true;
  const Material* material = nullptr;

  void set_face_normal(const Ray& r, const Vec3& outward_normal) {
    front_face = dot(r.direction, outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

struct Sphere {
  Vec3 center;
  double radius = 0.5;
  Material material;

  bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const {
    Vec3 oc = r.origin - center;
    auto a = r.direction.length_squared();
    auto half_b = dot(oc, r.direction);
    auto c = oc.length_squared() - radius * radius;
    auto discriminant = half_b * half_b - a * c;
    if (discriminant < 0) return false;
    auto sqrtd = std::sqrt(discriminant);

    auto root = (-half_b - sqrtd) / a;
    if (root < t_min || root > t_max) {
      root = (-half_b + sqrtd) / a;
      if (root < t_min || root > t_max) return false;
    }

    rec.t = root;
    rec.p = r.at(rec.t);
    Vec3 outward_normal = (rec.p - center) / radius;
    rec.set_face_normal(r, outward_normal);
    rec.material = &material;
    return true;
  }
};

struct Scene {
  std::string name;
  std::vector<Sphere> objects;

  bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const {
    HitRecord temp;
    bool hit_anything = false;
    auto closest = t_max;
    for (const auto& object : objects) {
      if (object.hit(r, t_min, closest, temp)) {
        hit_anything = true;
        closest = temp.t;
        rec = temp;
      }
    }
    return hit_anything;
  }
};

}  // namespace pr
