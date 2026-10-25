// Krita's own blend modes (docs/blend-modes.md, "Krita modes"). Written from Krita's renders
// alone: scripts/dev/kra/make_kra_fixtures.py draws a 256 x 256 table per mode (the layer's
// gray level along x, the backdrop's along y), and each formula below reproduces Krita
// 5.3's 8-bit output exactly or within one level. Krita's source was never read.

#include "core/krita_blend_modes.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy {

namespace {

[[nodiscard]] double unit(std::uint8_t value) noexcept {
  return value / 255.0;
}

[[nodiscard]] std::uint8_t rounded(double value) noexcept {
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

// The W3C soft light: Photoshop's below a 50% layer, a cubic instead of the square root
// for dark backdrops above it.
[[nodiscard]] double soft_light_svg(double s, double d) noexcept {
  if (s <= 0.5) {
    return d - (1.0 - 2.0 * s) * d * (1.0 - d);
  }
  const double lifted = d <= 0.25 ? ((16.0 * d - 12.0) * d + 4.0) * d : std::sqrt(d);
  return d + (2.0 * s - 1.0) * (lifted - d);
}

[[nodiscard]] double arc_tangent(double s, double d) noexcept {
  if (d <= 0.0) {
    return s <= 0.0 ? 0.0 : 1.0;
  }
  return 2.0 * std::atan(s / d) / std::numbers::pi;
}

[[nodiscard]] double parallel(double s, double d) noexcept {
  if (s <= 0.0 || d <= 0.0) {
    return 0.0;
  }
  return 2.0 / (1.0 / s + 1.0 / d);
}

[[nodiscard]] double hard_overlay(double s, double d) noexcept {
  if (s >= 1.0) {
    return d > 0.0 ? 1.0 : 0.0;
  }
  if (s > 0.5) {
    return std::min(1.0, d / (2.0 - 2.0 * s));
  }
  return 2.0 * s * d;
}

[[nodiscard]] double interpolation(double s, double d) noexcept {
  if (s <= 0.0 && d <= 0.0) {
    return 0.0;
  }
  return 0.5 - 0.25 * std::cos(std::numbers::pi * s) - 0.25 * std::cos(std::numbers::pi * d);
}

[[nodiscard]] double easy_dodge(double s, double d) noexcept {
  if (s >= 1.0) {
    return 1.0;
  }
  // Krita's exponent factor is the float nearest 1.04.
  return std::pow(d, (1.0 - s) * 1.0399999618530273);
}

[[nodiscard]] double formula(double s, double d, BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::SoftLightSvg:
      return soft_light_svg(s, d);
    case BlendMode::AdditiveSubtractive:
      return std::abs(std::sqrt(d) - std::sqrt(s));
    case BlendMode::ArcTangent:
      return arc_tangent(s, d);
    case BlendMode::GeometricMean:
      return std::sqrt(s * d);
    case BlendMode::Allanon:
      return (s + d) / 2.0;
    case BlendMode::Parallel:
      return parallel(s, d);
    case BlendMode::HardOverlay:
      return hard_overlay(s, d);
    case BlendMode::Interpolation:
      return interpolation(s, d);
    case BlendMode::EasyDodge:
      return easy_dodge(s, d);
    default:
      return s;
  }
}

}  // namespace

bool blend_mode_is_krita_only(BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::SoftLightSvg:
    case BlendMode::AdditiveSubtractive:
    case BlendMode::ArcTangent:
    case BlendMode::GeometricMean:
    case BlendMode::Allanon:
    case BlendMode::Parallel:
    case BlendMode::HardOverlay:
    case BlendMode::Interpolation:
    case BlendMode::EasyDodge:
      return true;
    default:
      return false;
  }
}

std::uint8_t krita_blend_channel8(std::uint8_t src, std::uint8_t dst, BlendMode mode) noexcept {
  const int s = src;
  const int d = dst;
  switch (mode) {
    case BlendMode::Allanon:
      // Krita halves with 127/255 on 8-bit samples: white over white is 254.
      return static_cast<std::uint8_t>((s + d) * 127 / 255);
    case BlendMode::Parallel: {
      // Integer reciprocals (255^2 / x, rounded), then a floored quotient: exact on all
      // 65536 pairs.
      if (s == 0 || d == 0) {
        return 0;
      }
      const int inverse_s = (255 * 255 + s / 2) / s;
      const int inverse_d = (255 * 255 + d / 2) / d;
      return static_cast<std::uint8_t>(std::min(255, 2 * 255 * 255 / (inverse_s + inverse_d)));
    }
    case BlendMode::HardOverlay: {
      // Floored rather than rounded; the small bias keeps exact quotients from flooring
      // one level low.
      const double value = hard_overlay(unit(src), unit(dst));
      return static_cast<std::uint8_t>(std::clamp(std::floor(value * 255.0 + 1e-9), 0.0, 255.0));
    }
    default:
      return rounded(formula(unit(src), unit(dst), mode));
  }
}

float krita_blend_unit(float s, float d, BlendMode mode) noexcept {
  const double a = std::clamp(static_cast<double>(s), 0.0, 1.0);
  const double b = std::clamp(static_cast<double>(d), 0.0, 1.0);
  return static_cast<float>(std::clamp(formula(a, b, mode), 0.0, 1.0));
}

}  // namespace patchy
