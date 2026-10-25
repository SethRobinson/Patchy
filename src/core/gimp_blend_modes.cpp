#include "core/gimp_blend_modes.hpp"

#include <algorithm>
#include <cmath>

// The formulas are the textbook color models, fitted against GIMP 3.2's own renders of
// every mode (local-test-fixtures/xcf/modes*.xcf, scripts/dev/xcf/make_xcf_fixtures.py):
// HSV and HSL as in Python's colorsys, CIE Lab with a D50 white and the Bradford-adapted
// sRGB primaries, and luminance from the same matrix's Y row in linear light.

namespace patchy {

namespace {

using Rgb = std::array<double, 3>;

[[nodiscard]] double decode(double encoded) noexcept {
  return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double encode(double linear) noexcept {
  linear = std::clamp(linear, 0.0, 1.0);
  return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

[[nodiscard]] Rgb decode(const Rgb& c) noexcept {
  return {decode(c[0]), decode(c[1]), decode(c[2])};
}

[[nodiscard]] Rgb encode(const Rgb& c) noexcept {
  return {encode(c[0]), encode(c[1]), encode(c[2])};
}

[[nodiscard]] Rgb clamped(const Rgb& c) noexcept {
  return {std::clamp(c[0], 0.0, 1.0), std::clamp(c[1], 0.0, 1.0), std::clamp(c[2], 0.0, 1.0)};
}

// --- HSV and HSL ---------------------------------------------------------------------------

struct Hsv {
  double h{0.0};  // 0..1
  double s{0.0};
  double v{0.0};
};

[[nodiscard]] Hsv to_hsv(const Rgb& c) noexcept {
  const double maximum = std::max({c[0], c[1], c[2]});
  const double minimum = std::min({c[0], c[1], c[2]});
  Hsv hsv;
  hsv.v = maximum;
  if (maximum == minimum) {
    return hsv;
  }
  const double range = maximum - minimum;
  hsv.s = range / maximum;
  const double rc = (maximum - c[0]) / range;
  const double gc = (maximum - c[1]) / range;
  const double bc = (maximum - c[2]) / range;
  double h = 0.0;
  if (c[0] == maximum) {
    h = bc - gc;
  } else if (c[1] == maximum) {
    h = 2.0 + rc - bc;
  } else {
    h = 4.0 + gc - rc;
  }
  h /= 6.0;
  hsv.h = h - std::floor(h);
  return hsv;
}

[[nodiscard]] Rgb from_hsv(const Hsv& hsv) noexcept {
  if (hsv.s == 0.0) {
    return {hsv.v, hsv.v, hsv.v};
  }
  const double scaled = hsv.h * 6.0;
  const int sector = static_cast<int>(std::floor(scaled)) % 6;
  const double f = scaled - std::floor(scaled);
  const double p = hsv.v * (1.0 - hsv.s);
  const double q = hsv.v * (1.0 - hsv.s * f);
  const double t = hsv.v * (1.0 - hsv.s * (1.0 - f));
  switch (sector) {
    case 0:
      return {hsv.v, t, p};
    case 1:
      return {q, hsv.v, p};
    case 2:
      return {p, hsv.v, t};
    case 3:
      return {p, q, hsv.v};
    case 4:
      return {t, p, hsv.v};
    default:
      return {hsv.v, p, q};
  }
}

struct Hls {
  double h{0.0};
  double l{0.0};
  double s{0.0};
};

[[nodiscard]] Hls to_hls(const Rgb& c) noexcept {
  const double maximum = std::max({c[0], c[1], c[2]});
  const double minimum = std::min({c[0], c[1], c[2]});
  Hls hls;
  hls.l = (minimum + maximum) / 2.0;
  if (maximum == minimum) {
    return hls;
  }
  const double sum = maximum + minimum;
  const double range = maximum - minimum;
  hls.s = hls.l <= 0.5 ? range / sum : range / (2.0 - sum);
  const double rc = (maximum - c[0]) / range;
  const double gc = (maximum - c[1]) / range;
  const double bc = (maximum - c[2]) / range;
  double h = 0.0;
  if (c[0] == maximum) {
    h = bc - gc;
  } else if (c[1] == maximum) {
    h = 2.0 + rc - bc;
  } else {
    h = 4.0 + gc - rc;
  }
  h /= 6.0;
  hls.h = h - std::floor(h);
  return hls;
}

[[nodiscard]] double hls_value(double m1, double m2, double hue) noexcept {
  hue -= std::floor(hue);
  if (hue < 1.0 / 6.0) {
    return m1 + (m2 - m1) * hue * 6.0;
  }
  if (hue < 0.5) {
    return m2;
  }
  if (hue < 2.0 / 3.0) {
    return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0;
  }
  return m1;
}

[[nodiscard]] Rgb from_hls(const Hls& hls) noexcept {
  if (hls.s == 0.0) {
    return {hls.l, hls.l, hls.l};
  }
  const double m2 = hls.l <= 0.5 ? hls.l * (1.0 + hls.s) : hls.l + hls.s - hls.l * hls.s;
  const double m1 = 2.0 * hls.l - m2;
  return {hls_value(m1, m2, hls.h + 1.0 / 3.0), hls_value(m1, m2, hls.h), hls_value(m1, m2, hls.h - 1.0 / 3.0)};
}

// --- CIE Lab / LCh(ab), D50 ------------------------------------------------------------------

constexpr double kToXyz[3][3] = {{0.4360747, 0.3850649, 0.1430804},
                                 {0.2225045, 0.7168786, 0.0606169},
                                 {0.0139322, 0.0971045, 0.7141733}};
constexpr double kFromXyz[3][3] = {{3.133856367792, -1.616866770291, -0.490614772798},
                                   {-0.978768563127, 1.916141552624, 0.033454116303},
                                   {0.071945168282, -0.228991277299, 1.405242674154}};
constexpr double kWhite[3] = {0.96422, 1.0, 0.82521};
constexpr double kDelta = 6.0 / 29.0;

[[nodiscard]] double lab_f(double t) noexcept {
  return t > kDelta * kDelta * kDelta ? std::cbrt(t) : t / (3.0 * kDelta * kDelta) + 4.0 / 29.0;
}

[[nodiscard]] double lab_f_inverse(double t) noexcept {
  return t > kDelta ? t * t * t : 3.0 * kDelta * kDelta * (t - 4.0 / 29.0);
}

struct Lch {
  double l{0.0};
  double c{0.0};
  double h{0.0};  // radians
};

[[nodiscard]] Lch to_lch(const Rgb& encoded) noexcept {
  const auto linear = decode(encoded);
  double xyz[3];
  for (int i = 0; i < 3; ++i) {
    xyz[i] = (kToXyz[i][0] * linear[0] + kToXyz[i][1] * linear[1] + kToXyz[i][2] * linear[2]) / kWhite[i];
  }
  const double fx = lab_f(xyz[0]);
  const double fy = lab_f(xyz[1]);
  const double fz = lab_f(xyz[2]);
  const double a = 500.0 * (fx - fy);
  const double b = 200.0 * (fy - fz);
  return Lch{116.0 * fy - 16.0, std::hypot(a, b), std::atan2(b, a)};
}

[[nodiscard]] Rgb from_lch(const Lch& lch) noexcept {
  const double a = lch.c * std::cos(lch.h);
  const double b = lch.c * std::sin(lch.h);
  const double fy = (lch.l + 16.0) / 116.0;
  const double fx = fy + a / 500.0;
  const double fz = fy - b / 200.0;
  const double xyz[3] = {lab_f_inverse(fx) * kWhite[0], lab_f_inverse(fy) * kWhite[1], lab_f_inverse(fz) * kWhite[2]};
  Rgb linear{};
  for (int i = 0; i < 3; ++i) {
    linear[static_cast<std::size_t>(i)] = kFromXyz[i][0] * xyz[0] + kFromXyz[i][1] * xyz[1] + kFromXyz[i][2] * xyz[2];
  }
  return encode(linear);
}

[[nodiscard]] double luminance(const Rgb& linear) noexcept {
  return kToXyz[1][0] * linear[0] + kToXyz[1][1] * linear[1] + kToXyz[1][2] * linear[2];
}

[[nodiscard]] double linear_op(double s, double d, BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::LinearMultiply:
      return s * d;
    case BlendMode::LinearAddition:
      return s + d;
    case BlendMode::LinearSubtract:
      return d - s;
    case BlendMode::LinearDivide:
      return s <= 0.0 ? (d > 0.0 ? 1.0 : 0.0) : d / s;
    default:
      return s;
  }
}

}  // namespace

bool blend_mode_is_gimp_color_model(BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::HsvHue:
    case BlendMode::HsvSaturation:
    case BlendMode::HslColor:
    case BlendMode::HsvValue:
    case BlendMode::LchHue:
    case BlendMode::LchChroma:
    case BlendMode::LchColor:
    case BlendMode::LchLightness:
    case BlendMode::Luminance:
    case BlendMode::LumaDarkenOnly:
    case BlendMode::LumaLightenOnly:
    case BlendMode::LinearMultiply:
    case BlendMode::LinearAddition:
    case BlendMode::LinearSubtract:
    case BlendMode::LinearDivide:
      return true;
    default:
      return false;
  }
}

std::array<double, 3> gimp_blend_unit(const std::array<double, 3>& source, const std::array<double, 3>& destination,
                                      BlendMode mode) noexcept {
  const auto s = clamped(source);
  const auto d = clamped(destination);
  switch (mode) {
    case BlendMode::HsvHue: {
      const auto src = to_hsv(s);
      if (src.s == 0.0) {
        return d;
      }
      auto dst = to_hsv(d);
      dst.h = src.h;
      return clamped(from_hsv(dst));
    }
    case BlendMode::HsvSaturation: {
      auto dst = to_hsv(d);
      dst.s = to_hsv(s).s;
      return clamped(from_hsv(dst));
    }
    case BlendMode::HsvValue: {
      auto dst = to_hsv(d);
      dst.v = to_hsv(s).v;
      return clamped(from_hsv(dst));
    }
    case BlendMode::HslColor: {
      const auto src = to_hls(s);
      auto dst = to_hls(d);
      dst.h = src.h;
      dst.s = src.s;
      return clamped(from_hls(dst));
    }
    case BlendMode::LchHue: {
      const auto src = to_lch(s);
      if (src.c <= 0.0) {
        return d;
      }
      auto dst = to_lch(d);
      dst.h = src.h;
      return from_lch(dst);
    }
    case BlendMode::LchChroma: {
      auto dst = to_lch(d);
      dst.c = to_lch(s).c;
      return from_lch(dst);
    }
    case BlendMode::LchColor: {
      const auto src = to_lch(s);
      auto dst = to_lch(d);
      dst.c = src.c;
      dst.h = src.h;
      return from_lch(dst);
    }
    case BlendMode::LchLightness: {
      auto dst = to_lch(d);
      dst.l = to_lch(s).l;
      return from_lch(dst);
    }
    case BlendMode::Luminance: {
      const auto ls = decode(s);
      const auto ld = decode(d);
      const double ys = luminance(ls);
      const double yd = luminance(ld);
      const double k = yd > 0.0 ? ys / yd : 0.0;
      return encode(Rgb{ld[0] * k, ld[1] * k, ld[2] * k});
    }
    case BlendMode::LumaDarkenOnly:
      return luminance(decode(s)) <= luminance(decode(d)) ? s : d;
    case BlendMode::LumaLightenOnly:
      return luminance(decode(s)) >= luminance(decode(d)) ? s : d;
    case BlendMode::LinearMultiply:
    case BlendMode::LinearAddition:
    case BlendMode::LinearSubtract:
    case BlendMode::LinearDivide: {
      const auto ls = decode(s);
      const auto ld = decode(d);
      return encode(Rgb{linear_op(ls[0], ld[0], mode), linear_op(ls[1], ld[1], mode), linear_op(ls[2], ld[2], mode)});
    }
    default:
      return s;
  }
}

}  // namespace patchy
