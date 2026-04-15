#pragma once

// Scene geometry and material types.
// This header must not depend on scene registry or render-engine code.

#include "math.h"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace pr {

struct Ray {
  Vec3 origin;
  Vec3 direction;

  Ray() = default;
  Ray(Vec3 o, Vec3 d) : origin(o), direction(d) {}
  Vec3 at(double t) const { return origin + t * direction; }
};

enum class MaterialKind { Lambertian, Metal, Dielectric, DiffuseLight };

struct Material {
  MaterialKind kind = MaterialKind::Lambertian;
  Vec3 albedo{0.7, 0.7, 0.7};
  double fuzz = 0.0;
  double ir = 1.5;
  Vec3 emission{0.0, 0.0, 0.0};

  static Material lambertian(Vec3 a) { return Material{MaterialKind::Lambertian, a, 0.0, 1.5}; }
  static Material metal(Vec3 a, double f) { return Material{MaterialKind::Metal, a, std::clamp(f, 0.0, 1.0), 1.5}; }
  static Material dielectric(double index) { return Material{MaterialKind::Dielectric, Vec3{1.0, 1.0, 1.0}, 0.0, index}; }
  static Material light(Vec3 e) { return Material{MaterialKind::DiffuseLight, Vec3{0.0, 0.0, 0.0}, 0.0, 1.5, e}; }
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

struct XYRect {
  double x0 = 0.0;
  double x1 = 0.0;
  double y0 = 0.0;
  double y1 = 0.0;
  double z = 0.0;
  Material material;

  bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const {
    if (std::fabs(r.direction.z) < 1e-12) return false;
    auto t = (z - r.origin.z) / r.direction.z;
    if (t < t_min || t > t_max) return false;
    auto x = r.origin.x + t * r.direction.x;
    auto y = r.origin.y + t * r.direction.y;
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    rec.t = t;
    rec.p = r.at(t);
    rec.set_face_normal(r, Vec3{0, 0, 1});
    rec.material = &material;
    return true;
  }
};

struct XZRect {
  double x0 = 0.0;
  double x1 = 0.0;
  double z0 = 0.0;
  double z1 = 0.0;
  double y = 0.0;
  Material material;

  bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const {
    if (std::fabs(r.direction.y) < 1e-12) return false;
    auto t = (y - r.origin.y) / r.direction.y;
    if (t < t_min || t > t_max) return false;
    auto x = r.origin.x + t * r.direction.x;
    auto z = r.origin.z + t * r.direction.z;
    if (x < x0 || x > x1 || z < z0 || z > z1) return false;
    rec.t = t;
    rec.p = r.at(t);
    rec.set_face_normal(r, Vec3{0, 1, 0});
    rec.material = &material;
    return true;
  }
};

struct YZRect {
  double y0 = 0.0;
  double y1 = 0.0;
  double z0 = 0.0;
  double z1 = 0.0;
  double x = 0.0;
  Material material;

  bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const {
    if (std::fabs(r.direction.x) < 1e-12) return false;
    auto t = (x - r.origin.x) / r.direction.x;
    if (t < t_min || t > t_max) return false;
    auto y = r.origin.y + t * r.direction.y;
    auto z = r.origin.z + t * r.direction.z;
    if (y < y0 || y > y1 || z < z0 || z > z1) return false;
    rec.t = t;
    rec.p = r.at(t);
    rec.set_face_normal(r, Vec3{1, 0, 0});
    rec.material = &material;
    return true;
  }
};

using SceneObject = std::variant<Sphere, XYRect, XZRect, YZRect>;

struct Scene {
  std::string name;
  std::vector<SceneObject> objects;

  bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const {
    HitRecord temp;
    bool hit_anything = false;
    auto closest = t_max;
    for (const auto& object : objects) {
      if (std::visit([&](const auto& primitive) { return primitive.hit(r, t_min, closest, temp); }, object)) {
        hit_anything = true;
        closest = temp.t;
        rec = temp;
      }
    }
    return hit_anything;
  }
};

}  // namespace pr
