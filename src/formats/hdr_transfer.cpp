#include "formats/hdr_transfer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace patchy::hdr {

namespace {

using Matrix = std::array<double, 9>;

Matrix multiply(const Matrix& a, const Matrix& b) {
  Matrix out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    }
  }
  return out;
}

Matrix invert(const Matrix& m) {
  const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
  const double co0 = e * i - f * h;
  const double co1 = f * g - d * i;
  const double co2 = d * h - e * g;
  const double det = a * co0 + b * co1 + c * co2;
  const double s = det != 0.0 ? 1.0 / det : 0.0;
  return {co0 * s, (c * h - b * i) * s, (b * f - c * e) * s,
          co1 * s, (a * i - c * g) * s, (c * d - a * f) * s,
          co2 * s, (b * g - a * h) * s, (a * e - b * d) * s};
}

std::array<double, 3> apply(const Matrix& m, double x, double y, double z) {
  return {m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z, m[6] * x + m[7] * y + m[8] * z};
}

std::array<double, 3> xy_to_xyz(double x, double y) {
  return {x / y, 1.0, (1.0 - x - y) / y};
}

// RGB to XYZ for a set of primaries (the standard construction: columns scaled so RGB
// white lands on the white point at Y = 1).
Matrix rgb_to_xyz(const Chromaticities& p) {
  const auto r = xy_to_xyz(p.red_x, p.red_y);
  const auto g = xy_to_xyz(p.green_x, p.green_y);
  const auto b = xy_to_xyz(p.blue_x, p.blue_y);
  const auto w = xy_to_xyz(p.white_x, p.white_y);
  const Matrix columns{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
  const auto s = apply(invert(columns), w[0], w[1], w[2]);
  return {r[0] * s[0], g[0] * s[1], b[0] * s[2], r[1] * s[0], g[1] * s[1], b[1] * s[2],
          r[2] * s[0], g[2] * s[1], b[2] * s[2]};
}

// Bradford chromatic adaptation from one white point to another.
Matrix bradford(const Chromaticities& from, const Chromaticities& to) {
  const Matrix cone{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
  const auto ws = xy_to_xyz(from.white_x, from.white_y);
  const auto wd = xy_to_xyz(to.white_x, to.white_y);
  const auto s = apply(cone, ws[0], ws[1], ws[2]);
  const auto d = apply(cone, wd[0], wd[1], wd[2]);
  const Matrix scale{d[0] / s[0], 0.0, 0.0, 0.0, d[1] / s[1], 0.0, 0.0, 0.0, d[2] / s[2]};
  return multiply(invert(cone), multiply(scale, cone));
}

bool same_white(const Chromaticities& a, const Chromaticities& b) {
  return std::abs(a.white_x - b.white_x) < 1e-6 && std::abs(a.white_y - b.white_y) < 1e-6;
}

}  // namespace

double pq_signal_to_nits(double signal) {
  constexpr double m1 = 2610.0 / 16384.0;
  constexpr double m2 = 2523.0 / 4096.0 * 128.0;
  constexpr double c1 = 3424.0 / 4096.0;
  constexpr double c2 = 2413.0 / 4096.0 * 32.0;
  constexpr double c3 = 2392.0 / 4096.0 * 32.0;
  const double e = std::pow(std::clamp(signal, 0.0, 1.0), 1.0 / m2);
  const double y = std::max(e - c1, 0.0) / (c2 - c3 * e);
  return 10000.0 * std::pow(y, 1.0 / m1);
}

double hlg_signal_to_scene(double signal) {
  constexpr double a = 0.17883277;
  constexpr double b = 1.0 - 4.0 * a;
  const double c = 0.5 - a * std::log(4.0 * a);
  const double e = std::clamp(signal, 0.0, 1.0);
  return e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
}

std::array<double, 9> linear_to_srgb_matrix(const Chromaticities& source) {
  const auto to_xyz = rgb_to_xyz(source);
  const auto from_xyz = invert(rgb_to_xyz(kBt709Primaries));
  if (same_white(source, kBt709Primaries)) {
    return multiply(from_xyz, to_xyz);
  }
  return multiply(from_xyz, multiply(bradford(source, kBt709Primaries), to_xyz));
}

void signal_rgba16_to_linear_srgb(const std::uint16_t* rgba16, std::size_t pixel_count, Transfer transfer,
                                  const Chromaticities& source, float* rgba_float) {
  // Every sample is one of 65536 codes, so the transfer runs once per code: PQ straight to
  // linear relative to SDR white, HLG to scene light for the OOTF below.
  std::vector<float> table(65536);
  for (std::size_t code = 0; code < table.size(); ++code) {
    const double signal = static_cast<double>(code) / 65535.0;
    table[code] = static_cast<float>(transfer == Transfer::Pq ? pq_signal_to_nits(signal) / kSdrReferenceWhiteNits
                                                              : hlg_signal_to_scene(signal));
  }
  const auto matrix = linear_to_srgb_matrix(source);
  const auto luma = rgb_to_xyz(source);  // its middle row weights scene light into luminance
  // BT.2100 reference OOTF gamma for the nominal peak: 1.2 at 1000 nits.
  const double gamma = 1.2 + 0.42 * std::log10(kHlgNominalPeakNits / 1000.0);
  const double hlg_scale = kHlgNominalPeakNits / kSdrReferenceWhiteNits;
  for (std::size_t i = 0; i < pixel_count; ++i) {
    const std::uint16_t* in = rgba16 + i * 4U;
    double r = table[in[0]];
    double g = table[in[1]];
    double b = table[in[2]];
    if (transfer == Transfer::Hlg) {
      const double ys = luma[3] * r + luma[4] * g + luma[5] * b;
      const double gain = ys > 0.0 ? hlg_scale * std::pow(ys, gamma - 1.0) : 0.0;
      r *= gain;
      g *= gain;
      b *= gain;
    }
    const auto out = apply(matrix, r, g, b);
    float* dst = rgba_float + i * 4U;
    dst[0] = static_cast<float>(out[0]);
    dst[1] = static_cast<float>(out[1]);
    dst[2] = static_cast<float>(out[2]);
    dst[3] = static_cast<float>(in[3]) / 65535.0F;
  }
}

}  // namespace patchy::hdr
