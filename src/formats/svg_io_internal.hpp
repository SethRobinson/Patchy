#pragma once

#include "core/layer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace patchy::svg::detail {

struct Affine {
  double a{1.0};
  double b{0.0};
  double c{0.0};
  double d{1.0};
  double e{0.0};
  double f{0.0};
};

inline Affine multiply(const Affine& outer, const Affine& inner) noexcept {
  return {outer.a * inner.a + outer.c * inner.b,
          outer.b * inner.a + outer.d * inner.b,
          outer.a * inner.c + outer.c * inner.d,
          outer.b * inner.c + outer.d * inner.d,
          outer.a * inner.e + outer.c * inner.f + outer.e,
          outer.b * inner.e + outer.d * inner.f + outer.f};
}

inline std::array<double, 2> map_point(const Affine& matrix, double x, double y) noexcept {
  return {matrix.a * x + matrix.c * y + matrix.e,
          matrix.b * x + matrix.d * y + matrix.f};
}

inline double determinant(const Affine& matrix) noexcept {
  return matrix.a * matrix.d - matrix.b * matrix.c;
}

inline bool positive_axis_scale_translate(const Affine& matrix) noexcept {
  constexpr double epsilon = 1e-10;
  return matrix.a > 0.0 && matrix.d > 0.0 && std::abs(matrix.b) < epsilon &&
         std::abs(matrix.c) < epsilon;
}

// Locale-independent number I/O. Deliberately classic-locale iostreams, not
// <charconv>: Apple's libc++ marks the floating-point from_chars/to_chars
// overloads unavailable. %.15g digits are correctly rounded on every
// mainstream toolchain, so output stays deterministic cross-platform.
inline std::string format_number(double value) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("SVG cannot encode a non-finite number");
  }
  if (std::abs(value) < 5e-13) {
    value = 0.0;
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(15) << value;
  return stream.str();
}

// Parses a double from the FRONT of `text` (trailing content like CSS "px"
// units is left unconsumed; a bare "e" that starts a unit like "5em" is not
// eaten as an exponent); false when no number leads the text. Hand-written
// on purpose: no locale machinery, identical behavior on every toolchain,
// and exact results for the <= 15-significant-digit numbers format_number
// emits (a < 2^53 mantissa scaled by an exactly-representable power of ten
// is correctly rounded).
inline bool parse_double_prefix(std::string_view text, std::size_t& consumed, double& value) {
  std::size_t i = 0;
  bool negative = false;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  std::uint64_t mantissa = 0;
  int mantissa_digits = 0;
  int scale_exponent = 0;
  bool any_digits = false;
  while (i < text.size() && is_digit(text[i])) {
    any_digits = true;
    if (mantissa_digits < 19) {
      mantissa = mantissa * 10U + static_cast<std::uint64_t>(text[i] - '0');
      ++mantissa_digits;
    } else {
      ++scale_exponent;  // digits beyond 19 only shift the magnitude
    }
    ++i;
  }
  if (i < text.size() && text[i] == '.') {
    const auto after_dot = i + 1;
    std::size_t j = after_dot;
    while (j < text.size() && is_digit(text[j])) {
      any_digits = true;
      if (mantissa_digits < 19) {
        mantissa = mantissa * 10U + static_cast<std::uint64_t>(text[j] - '0');
        ++mantissa_digits;
        --scale_exponent;
      }
      ++j;
    }
    if (j > after_dot || any_digits) {
      i = j;  // a trailing lone '.' after digits is consumed ("5." == 5)
    }
  }
  if (!any_digits) {
    consumed = 0;
    return false;
  }
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    std::size_t j = i + 1;
    bool exponent_negative = false;
    if (j < text.size() && (text[j] == '+' || text[j] == '-')) {
      exponent_negative = text[j] == '-';
      ++j;
    }
    int exponent = 0;
    bool exponent_digits = false;
    while (j < text.size() && is_digit(text[j])) {
      exponent_digits = true;
      exponent = std::min(exponent * 10 + (text[j] - '0'), 100000);
      ++j;
    }
    if (exponent_digits) {  // otherwise leave the 'e' for whatever follows
      scale_exponent += exponent_negative ? -exponent : exponent;
      i = j;
    }
  }
  double result = static_cast<double>(mantissa);
  // Powers of ten through 1e22 are exact doubles: one multiply/divide stays
  // correctly rounded. Larger magnitudes step in exact 1e22 chunks.
  static constexpr double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                      1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                      1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
  int remaining = scale_exponent;
  while (remaining > 22) {
    result *= 1e22;
    remaining -= 22;
    if (!std::isfinite(result)) {
      break;
    }
  }
  while (remaining < -22) {
    result /= 1e22;
    remaining += 22;
    if (result == 0.0) {
      break;
    }
  }
  if (remaining > 0) {
    result *= kPow10[remaining];
  } else if (remaining < 0) {
    result /= kPow10[-remaining];
  }
  if (!std::isfinite(result)) {
    consumed = 0;
    return false;
  }
  value = negative ? -result : result;
  consumed = i;
  return true;
}

inline std::string_view blend_mode_css(BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::Normal: return "normal";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Screen: return "screen";
    case BlendMode::Overlay: return "overlay";
    case BlendMode::Darken: return "darken";
    case BlendMode::Lighten: return "lighten";
    case BlendMode::ColorDodge: return "color-dodge";
    case BlendMode::ColorBurn: return "color-burn";
    case BlendMode::HardLight: return "hard-light";
    case BlendMode::SoftLight: return "soft-light";
    case BlendMode::Difference: return "difference";
    case BlendMode::Exclusion: return "exclusion";
    case BlendMode::Hue: return "hue";
    case BlendMode::Saturation: return "saturation";
    case BlendMode::Color: return "color";
    case BlendMode::Luminosity: return "luminosity";
    case BlendMode::LinearDodge: return "plus-lighter";
    default: return {};
  }
}

inline BlendMode blend_mode_from_css(std::string_view value) noexcept {
  if (value == "multiply") return BlendMode::Multiply;
  if (value == "screen") return BlendMode::Screen;
  if (value == "overlay") return BlendMode::Overlay;
  if (value == "darken") return BlendMode::Darken;
  if (value == "lighten") return BlendMode::Lighten;
  if (value == "color-dodge") return BlendMode::ColorDodge;
  if (value == "color-burn") return BlendMode::ColorBurn;
  if (value == "hard-light") return BlendMode::HardLight;
  if (value == "soft-light") return BlendMode::SoftLight;
  if (value == "difference") return BlendMode::Difference;
  if (value == "exclusion") return BlendMode::Exclusion;
  if (value == "hue") return BlendMode::Hue;
  if (value == "saturation") return BlendMode::Saturation;
  if (value == "color") return BlendMode::Color;
  if (value == "luminosity") return BlendMode::Luminosity;
  if (value == "plus-lighter") return BlendMode::LinearDodge;
  return BlendMode::Normal;
}

}  // namespace patchy::svg::detail
