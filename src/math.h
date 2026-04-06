#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pr {

inline constexpr double kPi = 3.141592653589793238462643383279502884;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  Vec3() = default;
  Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

  Vec3 operator-() const { return Vec3{-x, -y, -z}; }
  Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  Vec3& operator*=(double s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }
  Vec3& operator/=(double s) { return *this *= (1.0 / s); }

  double length() const { return std::sqrt(length_squared()); }
  double length_squared() const { return x * x + y * y + z * z; }
  bool near_zero() const {
    constexpr double e = 1e-12;
    return std::fabs(x) < e && std::fabs(y) < e && std::fabs(z) < e;
  }
};

inline Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
inline Vec3 operator-(Vec3 a, const Vec3& b) { return a += -b; }
inline Vec3 operator*(Vec3 a, double s) { return a *= s; }
inline Vec3 operator*(double s, Vec3 a) { return a *= s; }
inline Vec3 operator/(Vec3 a, double s) { return a /= s; }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 unit_vector(Vec3 v) { return v / v.length(); }

struct Rng {
  std::uint64_t state = 0x9e3779b97f4a7c15ull;

  explicit Rng(std::uint64_t seed = 1) : state(seed ? seed : 1) {}

  std::uint64_t next_u64() {
    std::uint64_t x = state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state = x;
    return x * 2685821657736338717ull;
  }

  double uniform01() { return (next_u64() >> 11) * (1.0 / 9007199254740992.0); }
  double range(double lo, double hi) { return lo + (hi - lo) * uniform01(); }
};

inline Vec3 random_in_unit_sphere(Rng& rng) {
  for (;;) {
    Vec3 p{rng.range(-1.0, 1.0), rng.range(-1.0, 1.0), rng.range(-1.0, 1.0)};
    if (p.length_squared() < 1.0) return p;
  }
}

inline Vec3 random_unit_vector(Rng& rng) { return unit_vector(random_in_unit_sphere(rng)); }

inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - 2.0 * dot(v, n) * n; }

inline Vec3 refract(const Vec3& uv, const Vec3& n, double etai_over_etat) {
  double cos_theta = std::min(dot(-uv, n), 1.0);
  Vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
  Vec3 r_out_parallel = -std::sqrt(std::fabs(1.0 - r_out_perp.length_squared())) * n;
  return r_out_perp + r_out_parallel;
}

inline bool approx_equal(Vec3 a, Vec3 b, double eps = 1e-9) {
  return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

}  // namespace pr
