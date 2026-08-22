#pragma once

#include <array>
#include <cmath>

// The 2D affine every vector format writes as six numbers: SVG's transform lists,
// PDF's `cm` and `Tm`, and Affinity's Xfrm all use this layout, in this order.
// Header-only and dependency-free so any formats module can share one definition.
//
//   x' = a*x + c*y + e
//   y' = b*x + d*y + f

namespace patchy::formats {

struct Affine {
  double a{1.0};
  double b{0.0};
  double c{0.0};
  double d{1.0};
  double e{0.0};
  double f{0.0};
};

// Applies `inner` first, then `outer`.
inline Affine multiply(const Affine& outer, const Affine& inner) noexcept {
  return {outer.a * inner.a + outer.c * inner.b,
          outer.b * inner.a + outer.d * inner.b,
          outer.a * inner.c + outer.c * inner.d,
          outer.b * inner.c + outer.d * inner.d,
          outer.a * inner.e + outer.c * inner.f + outer.e,
          outer.b * inner.e + outer.d * inner.f + outer.f};
}

inline std::array<double, 2> map_point(const Affine& matrix, double x, double y) noexcept {
  return {matrix.a * x + matrix.c * y + matrix.e, matrix.b * x + matrix.d * y + matrix.f};
}

inline double determinant(const Affine& matrix) noexcept {
  return matrix.a * matrix.d - matrix.b * matrix.c;
}

inline bool positive_axis_scale_translate(const Affine& matrix) noexcept {
  constexpr double epsilon = 1e-10;
  return matrix.a > 0.0 && matrix.d > 0.0 && std::abs(matrix.b) < epsilon && std::abs(matrix.c) < epsilon;
}

// The uniform scale a transform applies to lengths, used wherever a scalar such as
// a stroke width has to survive a matrix that could scale each axis differently.
inline double average_scale(const Affine& matrix) noexcept {
  return std::sqrt(std::abs(determinant(matrix)));
}

}  // namespace patchy::formats
