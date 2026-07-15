#include "core/pixel_tools.hpp"

#include "core/pixel_tools_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace patchy {

// Shared painting/geometry helpers with external linkage; declared in
// core/pixel_tools_internal.hpp and also used by document_geometry.cpp.
[[nodiscard]] Rect canvas_rect(const Document& document) noexcept {
  return Rect{0, 0, document.width(), document.height()};
}

[[nodiscard]] Rect normalized_rect(Rect rect) noexcept {
  if (rect.width < 0) {
    rect.x += rect.width;
    rect.width = -rect.width;
  }
  if (rect.height < 0) {
    rect.y += rect.height;
    rect.height = -rect.height;
  }
  return rect;
}

namespace {

[[nodiscard]] float selection_coverage(const EditOptions& options, std::int32_t x, std::int32_t y) noexcept {
  if (options.selection_coverage) {
    return std::clamp(options.selection_coverage(x, y), 0.0F, 1.0F);
  }
  if (options.selection_mask) {
    return options.selection_mask(x, y) ? 1.0F : 0.0F;
  }
  return !options.selection.has_value() || options.selection->contains(x, y) ? 1.0F : 0.0F;
}

[[nodiscard]] bool selection_allows(const EditOptions& options, std::int32_t x, std::int32_t y) noexcept {
  return selection_coverage(options, x, y) > 0.0F;
}

void report_edit_progress(const EditOptions& options) {
  if (options.progress_callback) {
    options.progress_callback();
  }
}

void report_edit_progress_periodically(const EditOptions& options, std::size_t& counter,
                                       std::size_t interval = 4096U) {
  if (!options.progress_callback) {
    return;
  }
  ++counter;
  if (counter % interval == 0U) {
    options.progress_callback();
  }
}

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

float brush_coverage(double distance_squared, int radius, int softness) {
  if (radius <= 0) {
    return distance_squared <= 0.0 ? 1.0F : 0.0F;
  }

  const auto radius_squared = static_cast<double>(radius) * static_cast<double>(radius);
  if (distance_squared > radius_squared) {
    return 0.0F;
  }

  softness = std::clamp(softness, 0, 100);
  if (softness <= 0) {
    return 1.0F;
  }

  const auto edge_width = std::max(1.0, static_cast<double>(radius) * static_cast<double>(softness) / 100.0);
  const auto inner_radius = std::max(0.0, static_cast<double>(radius) - edge_width);
  const auto distance = std::sqrt(distance_squared);
  if (distance <= inner_radius) {
    return 1.0F;
  }
  const auto t = std::clamp((distance - inner_radius) / edge_width, 0.0, 1.0);
  const auto smooth = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  return static_cast<float>(1.0 - smooth);
}

int brush_roundness_percent(const EditOptions& options) noexcept {
  return std::clamp(options.brush_roundness, 1, 100);
}

bool brush_shape_is_round(const EditOptions& options) noexcept {
  return brush_roundness_percent(options) >= 99;
}

double degrees_to_radians(double degrees) noexcept {
  return degrees * 3.14159265358979323846 / 180.0;
}

float brush_shape_coverage(double distance_x, double distance_y, int radius, const EditOptions& options) {
  const auto roundness = brush_roundness_percent(options);
  if (radius <= 0 || roundness >= 99) {
    return brush_coverage(distance_x * distance_x + distance_y * distance_y, radius, options.brush_softness);
  }

  const auto major_radius = static_cast<double>(radius);
  const auto minor_radius = std::max(0.5, major_radius * static_cast<double>(roundness) / 100.0);
  const auto angle = degrees_to_radians(options.brush_angle_degrees);
  const auto c = std::cos(angle);
  const auto s = std::sin(angle);
  const auto major_axis = c * distance_x + s * distance_y;
  const auto minor_axis = -s * distance_x + c * distance_y;
  const auto normalized_distance_squared =
      (major_axis * major_axis) / (major_radius * major_radius) +
      (minor_axis * minor_axis) / (minor_radius * minor_radius);
  return brush_coverage(normalized_distance_squared * major_radius * major_radius, radius,
                        options.brush_softness);
}

// --- Shape (rectangle / ellipse) signed-distance rendering -------------------------------------
// Shapes are rendered by computing a signed distance from each pixel center to the shape boundary
// (negative inside) and converting it to a single coverage value, which is composited exactly once
// via write_pixel. Visiting each pixel once eliminates the brush-stamp buildup that made thick
// outlines scratchy, and routing through a smooth coverage profile gives antialiased edges and lets
// the Soft setting feather both outlines and fills.

// Smoothstep-based edge profile. `sd` is a signed distance to the target edge (negative = covered
// side); `band` is the total transition width (px) centered on the edge. Returns coverage in [0,1].
// Matches brush_coverage's smoothstep so soft shapes look like the soft brush.
[[nodiscard]] float shape_edge_coverage(double sd, double band) noexcept {
  band = std::max(band, 1e-3);
  const auto t = std::clamp(0.5 - sd / band, 0.0, 1.0);
  return static_cast<float>(t * t * t * (t * (t * 6.0 - 15.0) + 10.0));
}

// Cheap first-order signed distance to an axis-aligned ellipse (negative inside). Accurate near the
// boundary; used for fills (where small error is invisible) and as a culling estimate for outlines.
[[nodiscard]] double ellipse_distance_estimate(double px, double py, double cx, double cy, double rx,
                                               double ry) noexcept {
  const auto dx = px - cx;
  const auto dy = py - cy;
  const auto nx = dx / rx;
  const auto ny = dy / ry;
  const auto f = nx * nx + ny * ny;
  const auto gx = dx / (rx * rx);
  const auto gy = dy / (ry * ry);
  const auto grad = 2.0 * std::sqrt(gx * gx + gy * gy);
  if (grad <= 1e-12) {
    return -std::min(rx, ry);  // at the center, deep inside
  }
  return (f - 1.0) / grad;
}

// Eberly's robust point-on-ellipse root finder (bisection). Requires r0 = (e0/e1)^2 with e0 >= e1.
[[nodiscard]] double ellipse_get_root(double r0, double z0, double z1, double g) noexcept {
  const auto n0 = r0 * z0;
  auto s0 = z1 - 1.0;
  auto s1 = (g < 0.0) ? 0.0 : std::sqrt(n0 * n0 + z1 * z1) - 1.0;
  auto s = 0.0;
  for (int i = 0; i < 60; ++i) {
    s = 0.5 * (s0 + s1);
    if (s == s0 || s == s1) {
      break;
    }
    const auto ratio0 = n0 / (s + r0);
    const auto ratio1 = z1 / (s + 1.0);
    const auto gg = ratio0 * ratio0 + ratio1 * ratio1 - 1.0;
    if (gg > 0.0) {
      s0 = s;
    } else if (gg < 0.0) {
      s1 = s;
    } else {
      break;
    }
  }
  return s;
}

// Unsigned distance from (y0,y1) in the first quadrant to an ellipse with semi-axes e0 >= e1 > 0.
[[nodiscard]] double ellipse_quadrant_distance(double e0, double e1, double y0, double y1) noexcept {
  if (y1 > 0.0) {
    if (y0 > 0.0) {
      const auto z0 = y0 / e0;
      const auto z1 = y1 / e1;
      const auto g = z0 * z0 + z1 * z1 - 1.0;
      if (g != 0.0) {
        const auto r0 = (e0 * e0) / (e1 * e1);
        const auto sbar = ellipse_get_root(r0, z0, z1, g);
        const auto x0 = r0 * y0 / (sbar + r0);
        const auto x1 = y1 / (sbar + 1.0);
        return std::hypot(x0 - y0, x1 - y1);
      }
      return 0.0;
    }
    return std::abs(y1 - e1);
  }
  const auto numer0 = e0 * y0;
  const auto denom0 = e0 * e0 - e1 * e1;
  if (denom0 > 0.0 && numer0 < denom0) {
    const auto xde0 = numer0 / denom0;
    const auto x1 = e1 * std::sqrt(std::max(0.0, 1.0 - xde0 * xde0));
    return std::hypot(e0 * xde0 - y0, x1);
  }
  return std::abs(y0 - e0);
}

// Exact Euclidean signed distance to an axis-aligned ellipse (negative inside). Gives the uniform
// ring thickness a thick outline needs, even on elongated ellipses.
[[nodiscard]] double ellipse_distance_exact(double px, double py, double cx, double cy, double rx,
                                            double ry) noexcept {
  const auto dx = px - cx;
  const auto dy = py - cy;
  const auto distance = (rx >= ry) ? ellipse_quadrant_distance(rx, ry, std::abs(dx), std::abs(dy))
                                   : ellipse_quadrant_distance(ry, rx, std::abs(dy), std::abs(dx));
  const auto nx = dx / rx;
  const auto ny = dy / ry;
  const auto inside = (nx * nx + ny * ny) <= 1.0;
  return inside ? -distance : distance;
}

// Exact signed distance to an axis-aligned rounded box (negative inside). r == 0 -> sharp rectangle.
[[nodiscard]] double rounded_box_distance(double px, double py, double cx, double cy, double hx,
                                          double hy, double r) noexcept {
  r = std::clamp(r, 0.0, std::min(hx, hy));
  const auto qx = std::abs(px - cx) - (hx - r);
  const auto qy = std::abs(py - cy) - (hy - r);
  const auto ax = std::max(qx, 0.0);
  const auto ay = std::max(qy, 0.0);
  const auto outside = std::sqrt(ax * ax + ay * ay);
  const auto inside = std::min(std::max(qx, qy), 0.0);
  return outside + inside - r;
}

// Inward edge-feather factors for a solid fill, computed over `region` (document coords). Pixels are
// "inside" when their selection coverage is >= 0.5; a two-pass chamfer distance transform measures
// each inside pixel's distance to the nearest outside pixel, which a smoothstep maps to a factor that
// is 0 at the selection edge and 1 once `band` pixels inside. Lets the Fill command feather its edges
// by the brush Soft setting (band scales with brush size). Returns w*h factors, row-major.
[[nodiscard]] std::vector<float> compute_fill_feather(const EditOptions& options, Rect region, double band) {
  const auto w = region.width;
  const auto h = region.height;
  std::vector<float> dist(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0F);
  constexpr float kInf = 1e9F;
  for (std::int32_t yy = 0; yy < h; ++yy) {
    for (std::int32_t xx = 0; xx < w; ++xx) {
      const auto cov = selection_coverage(options, region.x + xx, region.y + yy);
      dist[static_cast<std::size_t>(yy) * w + xx] = (cov >= 0.5F) ? kInf : 0.0F;
    }
  }
  constexpr float kOrtho = 1.0F;
  constexpr float kDiag = 1.41421356F;
  const auto at = [&](std::int32_t xx, std::int32_t yy) -> float& {
    return dist[static_cast<std::size_t>(yy) * w + xx];
  };
  for (std::int32_t yy = 0; yy < h; ++yy) {
    for (std::int32_t xx = 0; xx < w; ++xx) {
      auto& d = at(xx, yy);
      if (d == 0.0F) {
        continue;
      }
      if (xx > 0) d = std::min(d, at(xx - 1, yy) + kOrtho);
      if (yy > 0) d = std::min(d, at(xx, yy - 1) + kOrtho);
      if (xx > 0 && yy > 0) d = std::min(d, at(xx - 1, yy - 1) + kDiag);
      if (xx < w - 1 && yy > 0) d = std::min(d, at(xx + 1, yy - 1) + kDiag);
    }
  }
  for (std::int32_t yy = h - 1; yy >= 0; --yy) {
    for (std::int32_t xx = w - 1; xx >= 0; --xx) {
      auto& d = at(xx, yy);
      if (d == 0.0F) {
        continue;
      }
      if (xx < w - 1) d = std::min(d, at(xx + 1, yy) + kOrtho);
      if (yy < h - 1) d = std::min(d, at(xx, yy + 1) + kOrtho);
      if (xx < w - 1 && yy < h - 1) d = std::min(d, at(xx + 1, yy + 1) + kDiag);
      if (xx > 0 && yy < h - 1) d = std::min(d, at(xx - 1, yy + 1) + kDiag);
    }
  }
  for (auto& value : dist) {
    const auto t = std::clamp(value / static_cast<float>(band), 0.0F, 1.0F);
    value = t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
  }
  return dist;
}

Rect brush_dab_rect(double x, double y, int radius, const EditOptions& options, Rect bounds) {
  if (radius <= 0) {
    const auto px = static_cast<std::int32_t>(std::floor(x));
    const auto py = static_cast<std::int32_t>(std::floor(y));
    return intersect_rect(Rect{px, py, 1, 1}, bounds);
  }

  const auto roundness = brush_roundness_percent(options);
  double half_width = static_cast<double>(radius);
  double half_height = static_cast<double>(radius);
  if (roundness < 99) {
    const auto major_radius = static_cast<double>(radius);
    const auto minor_radius = std::max(0.5, major_radius * static_cast<double>(roundness) / 100.0);
    const auto angle = degrees_to_radians(options.brush_angle_degrees);
    const auto c = std::cos(angle);
    const auto s = std::sin(angle);
    half_width = std::sqrt((major_radius * c) * (major_radius * c) + (minor_radius * s) * (minor_radius * s));
    half_height = std::sqrt((major_radius * s) * (major_radius * s) + (minor_radius * c) * (minor_radius * c));
  }

  const auto left = static_cast<std::int32_t>(std::floor(x - half_width));
  const auto top = static_cast<std::int32_t>(std::floor(y - half_height));
  const auto right = static_cast<std::int32_t>(std::ceil(x + half_width)) + 1;
  const auto bottom = static_cast<std::int32_t>(std::ceil(y + half_height)) + 1;
  return intersect_rect(Rect{left, top, right - left, bottom - top}, bounds);
}

// Bitmap-tip stamping ---------------------------------------------------------------------------
// A tip dab samples the pre-scaled tip mask with an inverse map per pixel: document offset →
// rotate by -brush_angle_degrees → divide the minor axis by roundness (squash) → grow by the
// per-dab inverse scale / flip signs → translate by the tip anchor → bilinear sample. Keeping
// rotation/roundness/scale/subpixel in the inverse map means the scaled mask only ever depends
// on brush size, so pen rotation, tilt, and per-dab jitter never force a rescale. Per-dab size
// jitter only shrinks (Photoshop semantics), so the inverse scale is always >= 1 and the stamp
// is only ever minified.

struct TipDabTransform {
  double cos_angle{1.0};
  double sin_angle{0.0};
  double inverse_roundness{1.0};
  double inverse_scale{1.0};
  double flip_x_sign{1.0};
  double flip_y_sign{1.0};
};

[[nodiscard]] TipDabTransform tip_dab_transform(const EditOptions& options) noexcept {
  TipDabTransform transform;
  const auto angle = degrees_to_radians(options.brush_angle_degrees);
  transform.cos_angle = std::cos(angle);
  transform.sin_angle = std::sin(angle);
  transform.inverse_roundness = 100.0 / static_cast<double>(brush_roundness_percent(options));
  return transform;
}

[[nodiscard]] TipDabTransform tip_dab_transform(const EditOptions& options,
                                                const BrushDabVariation& variation) noexcept {
  TipDabTransform transform;
  const auto angle = degrees_to_radians(options.brush_angle_degrees + variation.angle_offset_degrees);
  transform.cos_angle = std::cos(angle);
  transform.sin_angle = std::sin(angle);
  const auto roundness_fraction =
      std::clamp((static_cast<double>(brush_roundness_percent(options)) / 100.0) *
                     std::clamp(variation.roundness_multiplier, 0.01, 1.0),
                 0.01, 1.0);
  transform.inverse_roundness = 1.0 / roundness_fraction;
  transform.inverse_scale = 1.0 / std::clamp(variation.scale, 0.01, 1.0);
  transform.flip_x_sign = variation.flip_x ? -1.0 : 1.0;
  transform.flip_y_sign = variation.flip_y ? -1.0 : 1.0;
  return transform;
}

[[nodiscard]] float tip_dab_coverage(const ScaledBrushTip& tip, const TipDabTransform& transform,
                                     double offset_x, double offset_y) noexcept {
  const auto u = (transform.cos_angle * offset_x + transform.sin_angle * offset_y) *
                 transform.inverse_scale * transform.flip_x_sign;
  const auto v = (-transform.sin_angle * offset_x + transform.cos_angle * offset_y) *
                 transform.inverse_roundness * transform.inverse_scale * transform.flip_y_sign;
  const auto sample_x = tip.anchor_x + u - 0.5;
  const auto sample_y = tip.anchor_y + v - 0.5;
  const auto x0 = static_cast<std::int32_t>(std::floor(sample_x));
  const auto y0 = static_cast<std::int32_t>(std::floor(sample_y));
  if (x0 < -1 || y0 < -1 || x0 >= tip.width || y0 >= tip.height) {
    return 0.0F;
  }
  const auto tx = sample_x - static_cast<double>(x0);
  const auto ty = sample_y - static_cast<double>(y0);
  const auto sample = [&tip](std::int32_t x, std::int32_t y) -> double {
    if (x < 0 || y < 0 || x >= tip.width || y >= tip.height) {
      return 0.0;
    }
    return static_cast<double>(tip.mask[static_cast<std::size_t>(y) * tip.width + x]);
  };
  const auto top = sample(x0, y0) * (1.0 - tx) + sample(x0 + 1, y0) * tx;
  const auto bottom = sample(x0, y0 + 1) * (1.0 - tx) + sample(x0 + 1, y0 + 1) * tx;
  return static_cast<float>((top * (1.0 - ty) + bottom * ty) / 255.0);
}

[[nodiscard]] std::uint32_t brush_texture_hash(std::int32_t x, std::int32_t y,
                                               std::uint32_t seed) noexcept {
  auto hash = static_cast<std::uint32_t>(x) * 0x8DA6B343U ^
              static_cast<std::uint32_t>(y) * 0xD8163841U ^ seed;
  hash ^= hash >> 15U;
  hash *= 0x7FEB352DU;
  hash ^= hash >> 13U;
  return hash;
}

// Patent boundary: this is one static grayscale function of document coordinates and saved
// brush settings. It never receives pressure, direction, velocity, tilt, rotation, or a color
// channel, and never assembles a runtime texture from weighted channels.
[[nodiscard]] float static_brush_texture(const BrushDynamics& dynamics, std::int32_t x,
                                         std::int32_t y) noexcept {
  const auto scale = std::clamp(dynamics.texture_scale, 0.01, 10.0);
  float value = 1.0F;
  switch (dynamics.texture_style) {
    case BrushTextureStyle::FineGrain: {
      const auto cell = std::max(1.0, 3.0 * scale);
      const auto gx = static_cast<std::int32_t>(std::floor(static_cast<double>(x) / cell));
      const auto gy = static_cast<std::int32_t>(std::floor(static_cast<double>(y) / cell));
      value = static_cast<float>(brush_texture_hash(gx, gy, dynamics.texture_seed) & 0xFFFFU) /
              65535.0F;
      break;
    }
    case BrushTextureStyle::Canvas: {
      const auto period = std::max(2, static_cast<int>(std::lround(6.0 * scale)));
      const auto positive_mod = [period](std::int32_t coordinate) {
        const auto mod = coordinate % period;
        return mod < 0 ? mod + period : mod;
      };
      const auto warp = positive_mod(x) <= std::max(1, period / 4);
      const auto weft = positive_mod(y) <= std::max(1, period / 4);
      value = warp || weft ? 0.95F : 0.28F;
      break;
    }
    case BrushTextureStyle::Speckle: {
      const auto cell = std::max(1.0, 2.0 * scale);
      const auto gx = static_cast<std::int32_t>(std::floor(static_cast<double>(x) / cell));
      const auto gy = static_cast<std::int32_t>(std::floor(static_cast<double>(y) / cell));
      const auto random = brush_texture_hash(gx, gy, dynamics.texture_seed) & 0xFFU;
      value = random < 82U ? 0.18F : 1.0F;
      break;
    }
  }
  return dynamics.texture_invert ? 1.0F - value : value;
}

[[nodiscard]] float dual_brush_coverage(const BrushDynamics& dynamics,
                                        const TipDabTransform& transform, double offset_x,
                                        double offset_y, int brush_size) noexcept {
  const auto local_x = (transform.cos_angle * offset_x + transform.sin_angle * offset_y) *
                       transform.inverse_scale * transform.flip_x_sign;
  const auto local_y = (-transform.sin_angle * offset_x + transform.cos_angle * offset_y) *
                       transform.inverse_roundness * transform.inverse_scale *
                       transform.flip_y_sign;
  const auto diameter = std::max(
      1.0, static_cast<double>(std::max(1, brush_size)) *
               std::clamp(dynamics.dual_brush_size, 0.05, 4.0));
  const auto period = std::max(1.0, diameter * std::clamp(dynamics.dual_brush_spacing, 0.1, 10.0));
  // A half-cell phase keeps the secondary mask visible even when its diameter exceeds the
  // primary tip. The same fixed lattice is evaluated for every dab; there is no component
  // hierarchy or input-driven switching.
  const auto nearest = [period](double coordinate) {
    const auto cell = std::floor(coordinate / period);
    return (cell + 0.5) * period;
  };
  const auto dx = local_x - nearest(local_x);
  const auto dy = local_y - nearest(local_y);
  const auto radius = diameter * 0.5;
  const auto distance = std::sqrt(dx * dx + dy * dy);
  const auto hardness = std::clamp(dynamics.dual_brush_hardness, 0.0, 1.0);
  const auto inner = radius * hardness;
  if (distance <= inner) {
    return 1.0F;
  }
  if (distance >= radius) {
    return 0.0F;
  }
  return static_cast<float>(1.0 - (distance - inner) / std::max(0.001, radius - inner));
}

[[nodiscard]] EditColor color_dynamics_color(const EditOptions& options,
                                             const BrushDabVariation& variation) noexcept {
  const auto mix = std::clamp(variation.foreground_background_mix, 0.0, 1.0);
  const auto lerp_channel = [mix](std::uint8_t foreground, std::uint8_t background) {
    return static_cast<double>(foreground) +
           (static_cast<double>(background) - static_cast<double>(foreground)) * mix;
  };
  auto red = lerp_channel(options.primary.r, options.secondary.r) / 255.0;
  auto green = lerp_channel(options.primary.g, options.secondary.g) / 255.0;
  auto blue = lerp_channel(options.primary.b, options.secondary.b) / 255.0;

  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;
  auto hue = 0.0;
  if (delta > 1e-12) {
    if (maximum == red) {
      hue = std::fmod((green - blue) / delta, 6.0);
    } else if (maximum == green) {
      hue = (blue - red) / delta + 2.0;
    } else {
      hue = (red - green) / delta + 4.0;
    }
    hue /= 6.0;
    if (hue < 0.0) {
      hue += 1.0;
    }
  }
  auto saturation = maximum <= 1e-12 ? 0.0 : delta / maximum;
  auto brightness = maximum;
  hue = std::fmod(hue + variation.hue_shift + 1.0, 1.0);
  saturation = std::clamp(saturation + variation.saturation_shift, 0.0, 1.0);
  brightness = std::clamp(brightness + variation.brightness_shift, 0.0, 1.0);
  const auto purity = std::clamp(options.brush_dynamics.purity, -1.0, 1.0);
  saturation = purity >= 0.0 ? saturation + (1.0 - saturation) * purity
                             : saturation * (1.0 + purity);

  const auto sector = hue * 6.0;
  const auto index = static_cast<int>(std::floor(sector)) % 6;
  const auto fraction = sector - std::floor(sector);
  const auto p = brightness * (1.0 - saturation);
  const auto q = brightness * (1.0 - saturation * fraction);
  const auto t = brightness * (1.0 - saturation * (1.0 - fraction));
  switch (index) {
    case 0: red = brightness; green = t; blue = p; break;
    case 1: red = q; green = brightness; blue = p; break;
    case 2: red = p; green = brightness; blue = t; break;
    case 3: red = p; green = q; blue = brightness; break;
    case 4: red = t; green = p; blue = brightness; break;
    default: red = brightness; green = p; blue = q; break;
  }
  const auto byte = [](double value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
  };
  return EditColor{byte(red), byte(green), byte(blue),
                   byte(lerp_channel(options.primary.a, options.secondary.a) / 255.0)};
}

[[nodiscard]] Rect tip_dab_rect(double x, double y, const ScaledBrushTip& tip,
                                const TipDabTransform& transform, Rect bounds) {
  const auto half_u = (static_cast<double>(tip.width) / 2.0) / transform.inverse_scale;
  const auto half_v =
      (static_cast<double>(tip.height) / 2.0) / (transform.inverse_roundness * transform.inverse_scale);
  const auto half_width = std::abs(transform.cos_angle) * half_u + std::abs(transform.sin_angle) * half_v;
  const auto half_height = std::abs(transform.sin_angle) * half_u + std::abs(transform.cos_angle) * half_v;
  const auto left = static_cast<std::int32_t>(std::floor(x - half_width)) - 1;
  const auto top = static_cast<std::int32_t>(std::floor(y - half_height)) - 1;
  const auto right = static_cast<std::int32_t>(std::ceil(x + half_width)) + 2;
  const auto bottom = static_cast<std::int32_t>(std::ceil(y + half_height)) + 2;
  return intersect_rect(Rect{left, top, right - left, bottom - top}, bounds);
}

}  // namespace

[[nodiscard]] Layer* editable_layer(Document& document, LayerId layer_id) noexcept {
  auto* layer = document.find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return nullptr;
  }
  auto& pixels = layer->pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return nullptr;
  }
  return layer;
}

[[nodiscard]] const Layer* editable_layer(const Document& document, LayerId layer_id) noexcept {
  const auto* layer = document.find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return nullptr;
  }
  const auto& pixels = layer->pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return nullptr;
  }
  return layer;
}

namespace {

[[nodiscard]] Rect clear_affected_rect(const Document& document, const Layer& layer, Rect rect,
                                       const EditOptions& options) noexcept {
  auto affected = intersect_rect(intersect_rect(normalized_rect(rect), canvas_rect(document)), layer.bounds());
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  return affected;
}

[[nodiscard]] bool clear_pixel_would_change(const PixelBuffer& pixels, const std::uint8_t* px,
                                            const EditOptions& options, float coverage) noexcept {
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  if (coverage <= 0.0F) {
    return false;
  }

  const auto channels = pixels.format().channels;
  if (options.lock_transparent_pixels) {
    return false;
  }

  const auto erase_alpha = (static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F) * coverage;
  if (channels >= 4) {
    return clamp_byte(static_cast<float>(px[3]) * (1.0F - erase_alpha)) != px[3];
  }
  return clamp_byte(255.0F * (1.0F - erase_alpha)) != 255U;
}

template <typename Callback>
void for_each_clear_candidate(const Layer& layer, Rect affected, const EditOptions& options, Callback callback) {
  if (affected.empty()) {
    return;
  }

  const auto& pixels = layer.pixels();
  const auto bounds = layer.bounds();
  const auto channels = pixels.format().channels;
  const auto scan_rect = [&](Rect rect) {
    rect = intersect_rect(rect, affected);
    if (rect.empty()) {
      return;
    }
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      const auto row = pixels.row(y - bounds.y);
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        const auto coverage = options.selection_scan_rects.empty() ? selection_coverage(options, x, y) : 1.0F;
        if (coverage <= 0.0F) {
          continue;
        }
        const auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
        callback(x, y, px, coverage);
      }
      report_edit_progress(options);
    }
  };

  if (options.selection_scan_rects.empty()) {
    scan_rect(affected);
    return;
  }
  for (const auto& rect : options.selection_scan_rects) {
    scan_rect(rect);
  }
}

bool write_pixel_blend(PixelBuffer& pixels, std::uint8_t* px, const EditOptions& options, bool erase,
                       float coverage) {
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  if (coverage <= 0.0F) {
    return false;
  }

  const auto channels = pixels.format().channels;
  std::array<std::uint8_t, 4> before{};
  for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
    before[channel] = px[channel];
  }
  const auto changed = [&]() {
    for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
      if (px[channel] != before[channel]) {
        return true;
      }
    }
    return false;
  };

  const auto locked_alpha = options.lock_transparent_pixels && channels >= 4;
  if (locked_alpha && px[3] == 0) {
    return true;
  }
  if (erase) {
    const auto erase_alpha = (static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F) * coverage;
    if (channels >= 4) {
      if (locked_alpha) {
        return false;
      }
      px[3] = clamp_byte(static_cast<float>(px[3]) * (1.0F - erase_alpha));
    } else {
      px[0] =
          clamp_byte(static_cast<float>(options.secondary.r) * erase_alpha + static_cast<float>(px[0]) * (1.0F - erase_alpha));
      px[1] =
          clamp_byte(static_cast<float>(options.secondary.g) * erase_alpha + static_cast<float>(px[1]) * (1.0F - erase_alpha));
      px[2] =
          clamp_byte(static_cast<float>(options.secondary.b) * erase_alpha + static_cast<float>(px[2]) * (1.0F - erase_alpha));
    }
    return changed();
  }

  const auto source_alpha = (static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F) * coverage;
  if (channels >= 4 && !locked_alpha && px[3] == 0) {
    px[0] = options.primary.r;
    px[1] = options.primary.g;
    px[2] = options.primary.b;
    px[3] = std::max<std::uint8_t>(1, clamp_byte(source_alpha * 255.0F));
    return changed();
  }
  if (channels >= 4) {
    if (locked_alpha) {
      if (source_alpha >= 0.999F) {
        px[0] = options.primary.r;
        px[1] = options.primary.g;
        px[2] = options.primary.b;
      } else {
        px[0] = clamp_byte(static_cast<float>(options.primary.r) * source_alpha +
                           static_cast<float>(px[0]) * (1.0F - source_alpha));
        px[1] = clamp_byte(static_cast<float>(options.primary.g) * source_alpha +
                           static_cast<float>(px[1]) * (1.0F - source_alpha));
        px[2] = clamp_byte(static_cast<float>(options.primary.b) * source_alpha +
                           static_cast<float>(px[2]) * (1.0F - source_alpha));
      }
      return changed();
    }
    const auto destination_alpha = static_cast<float>(px[3]) / 255.0F;
    const auto out_alpha = source_alpha + destination_alpha * (1.0F - source_alpha);
    if (out_alpha <= 0.0F) {
      return false;
    }
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      const auto destination_premultiplied = static_cast<float>(px[channel]) * destination_alpha;
      const auto source_value = channel == 0 ? static_cast<float>(options.primary.r)
                                             : channel == 1 ? static_cast<float>(options.primary.g)
                                                            : static_cast<float>(options.primary.b);
      px[channel] =
          clamp_byte((source_value * source_alpha + destination_premultiplied * (1.0F - source_alpha)) / out_alpha);
    }
    px[3] = clamp_byte(out_alpha * 255.0F);
    return changed();
  }
  if (source_alpha >= 0.999F) {
    px[0] = options.primary.r;
    px[1] = options.primary.g;
    px[2] = options.primary.b;
  } else {
    px[0] = clamp_byte(static_cast<float>(options.primary.r) * source_alpha + static_cast<float>(px[0]) * (1.0F - source_alpha));
    px[1] = clamp_byte(static_cast<float>(options.primary.g) * source_alpha + static_cast<float>(px[1]) * (1.0F - source_alpha));
    px[2] = clamp_byte(static_cast<float>(options.primary.b) * source_alpha + static_cast<float>(px[2]) * (1.0F - source_alpha));
  }
  return changed();
}

// Every tool write funnels through here. With no palette constraint this forwards
// to the historical blend untouched; in palette mode coverage is binarized (hard
// aliased edges), the blend runs at full strength, and the result snaps to the
// palette with 0/255 alpha. See core/palette.hpp.
bool write_pixel(PixelBuffer& pixels, std::uint8_t* px, const EditOptions& options, bool erase,
                 float coverage = 1.0F) {
  const auto* snap = options.palette_snap;
  if (snap == nullptr || snap->lut == nullptr || snap->lut->empty()) {
    return write_pixel_blend(pixels, px, options, erase, coverage);
  }
  if (coverage < snap->coverage_threshold) {
    return false;
  }

  const auto channels = pixels.format().channels;
  std::array<std::uint8_t, 4> before{};
  for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
    before[channel] = px[channel];
  }
  (void)write_pixel_blend(pixels, px, options, erase, 1.0F);
  const auto blend_wrote = [&]() {
    for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
      if (px[channel] != before[channel]) {
        return true;
      }
    }
    return false;
  }();
  if (!blend_wrote) {
    // The blend refused the pixel (transparency lock, no-op write): snapping it
    // anyway would mutate pixels the tool never touched.
    return false;
  }
  snap_pixel_to_palette(px, channels, *snap);
  for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
    if (px[channel] != before[channel]) {
      return true;
    }
  }
  return false;
}

void ensure_alpha_for_erase(Layer& layer) {
  auto& source = layer.pixels();
  if (source.format().channels >= 4) {
    return;
  }

  const auto old_bounds = layer.bounds();
  PixelBuffer rgba(source.width(), source.height(), PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto* src = source.pixel(x, y);
      auto* dst = rgba.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = 255;
    }
  }
  layer.set_pixels(std::move(rgba));
  layer.set_bounds(old_bounds);
}

EditColor lerp_color(EditColor a, EditColor b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  const auto lerp = [t](std::uint8_t lhs, std::uint8_t rhs) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<double>(lhs) +
                                                            (static_cast<double>(rhs) - static_cast<double>(lhs)) * t),
                                                0L, 255L));
  };
  return EditColor{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}

std::array<std::uint8_t, 4> sample_layer_rgba(const Layer& layer, std::int32_t document_x, std::int32_t document_y) {
  const auto bounds = layer.bounds();
  if (!bounds.contains(document_x, document_y)) {
    return {0, 0, 0, 0};
  }

  const auto& pixels = layer.pixels();
  const auto* px = pixels.pixel(document_x - bounds.x, document_y - bounds.y);
  return {px[0], px[1], px[2],
          pixels.format().channels >= 4 ? px[3] : static_cast<std::uint8_t>(255)};
}

void ensure_smudge_sample(SmudgeState& state, int diameter) {
  diameter = std::max(1, diameter);
  const auto required_size = static_cast<std::size_t>(diameter) * static_cast<std::size_t>(diameter) * 4U;
  if (state.diameter != diameter || state.sample_rgba.size() != required_size) {
    state.diameter = diameter;
    state.sample_rgba.assign(required_size, 0);
    state.initialized = false;
  }
}

void capture_smudge_sample(SmudgeState& state, const Layer& layer, std::int32_t center_x, std::int32_t center_y,
                           int radius) {
  ensure_smudge_sample(state, radius * 2 + 1);
  for (int y = 0; y < state.diameter; ++y) {
    for (int x = 0; x < state.diameter; ++x) {
      const auto color = sample_layer_rgba(layer, center_x + x - radius, center_y + y - radius);
      auto* dst = state.sample_rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(state.diameter) +
                                              static_cast<std::size_t>(x)) *
                                                 4U;
      std::copy(color.begin(), color.end(), dst);
    }
  }
  state.initialized = true;
}

bool same_pixel(const std::uint8_t* px, const std::vector<std::uint8_t>& target, std::uint16_t channels) {
  for (std::uint16_t channel = 0; channel < channels; ++channel) {
    if (px[channel] != target[channel]) {
      return false;
    }
  }
  return true;
}

template <typename Callback>
void visit_pixel_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Callback&& callback) {
  const auto dx = std::abs(x1 - x0);
  const auto sx = x0 < x1 ? 1 : -1;
  const auto dy = -std::abs(y1 - y0);
  const auto sy = y0 < y1 ? 1 : -1;
  auto error = dx + dy;

  while (true) {
    callback(x0, y0);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const auto doubled_error = error * 2;
    if (doubled_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

}  // namespace

void begin_mixer_brush_stroke(MixerBrushState& state, EditColor initial_sample) noexcept {
  state = {};
  state.initialized = true;
  state.initial_sample = initial_sample;
}

EditColor mixer_brush_dab_color(MixerBrushState& state, double x, double y, int brush_size,
                                EditColor loaded_color, int wet, int load, int mix) noexcept {
  if (!state.initialized) {
    begin_mixer_brush_stroke(state, loaded_color);
  }
  if (state.has_last_dab) {
    state.distance += std::hypot(x - state.last_dab_x, y - state.last_dab_y);
  }
  state.last_dab_x = x;
  state.last_dab_y = y;
  state.has_last_dab = true;

  const auto wet_fraction = static_cast<double>(std::clamp(wet, 0, 100)) / 100.0;
  const auto load_fraction = static_cast<double>(std::clamp(load, 1, 100)) / 100.0;
  const auto mix_fraction = static_cast<double>(std::clamp(mix, 0, 100)) / 100.0;
  const auto diameter = static_cast<double>(std::max(1, brush_size));

  // One mouse-down sample fades along the stroke. It is deliberately never refreshed from the
  // canvas: this is a direct weighted RGBA interpolation, not dynamic paint pickup, a bristle
  // reservoir, a fill-channel simulation, or a 3D RGB curve (see docs/brushes.md).
  const auto pickup_length = diameter * (0.5 + wet_fraction * 6.0);
  const auto pickup_decay = wet_fraction <= 0.0 ? 0.0 : std::exp(-state.distance / pickup_length);
  const auto load_length = diameter * (0.25 + load_fraction * 6.0);
  const auto load_decay = load_fraction >= 0.995
                              ? 1.0
                              : std::exp(-state.distance / load_length);
  const auto sample_alpha = static_cast<double>(state.initial_sample.a) / 255.0;
  const auto sample_weight = wet_fraction * mix_fraction * sample_alpha;
  const auto canvas_influence = std::clamp(sample_weight * pickup_decay, 0.0, 1.0);
  const auto alpha_factor = std::clamp(canvas_influence + (1.0 - sample_weight) * load_decay,
                                       0.0, 1.0);
  const auto mix_channel = [canvas_influence](std::uint8_t loaded, std::uint8_t sampled) {
    return clamp_byte(static_cast<float>(static_cast<double>(loaded) * (1.0 - canvas_influence) +
                                         static_cast<double>(sampled) * canvas_influence));
  };
  return {mix_channel(loaded_color.r, state.initial_sample.r),
          mix_channel(loaded_color.g, state.initial_sample.g),
          mix_channel(loaded_color.b, state.initial_sample.b),
          clamp_byte(static_cast<float>(static_cast<double>(loaded_color.a) * alpha_factor))};
}

[[nodiscard]] PixelFormat canvas_resized_format_for_layer(const Layer& layer, const PixelBuffer& source,
                                                          EditColor extension_color = EditColor{255, 255, 255,
                                                                                                 255}) noexcept {
  if (source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return source.format();
  }
  if (source.format().channels >= 4 || layer.name() != "Background" || extension_color.a < 255) {
    return PixelFormat::rgba8();
  }
  return source.format();
}

void fill_resized_layer_background(PixelBuffer& pixels, const Layer& layer,
                                   EditColor extension_color = EditColor{255, 255, 255, 255}) {
  pixels.clear(0);
  if (layer.name() != "Background" || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3 ||
      pixels.empty()) {
    return;
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = extension_color.r;
      px[1] = extension_color.g;
      px[2] = extension_color.b;
      if (pixels.format().channels >= 4) {
        px[3] = extension_color.a;
      }
    }
  }
}

void copy_resized_layer_pixel(const PixelBuffer& source, PixelBuffer& destination, std::int32_t sx, std::int32_t sy,
                              std::int32_t dx, std::int32_t dy) {
  const auto* src = source.pixel(sx, sy);
  auto* dst = destination.pixel(dx, dy);
  if (source.format().bit_depth == BitDepth::UInt8 && destination.format().bit_depth == BitDepth::UInt8 &&
      source.format().channels >= 3 && destination.format().channels >= 3) {
    const auto channel_count = std::min(source.format().channels, destination.format().channels);
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      dst[channel] = src[channel];
    }
    if (destination.format().channels >= 4 && source.format().channels < 4) {
      dst[3] = 255;
    }
    return;
  }

  const auto bytes = std::min(bytes_per_pixel(source.format()), bytes_per_pixel(destination.format()));
  std::copy(src, src + bytes, dst);
}

namespace {

// Single-pass shape renderer shared by rectangle and ellipse, fill and outline, draw and erase.
Rect render_shape(Document& document, LayerId layer_id, Rect rect, const EditOptions& options, bool erase,
                  ShapeKind kind) {
  rect = normalized_rect(rect);
  if (rect.empty()) {
    return {};
  }
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }
  if (erase) {
    ensure_alpha_for_erase(*layer);
  }

  const auto params = make_shape_coverage_params(rect, options, kind);
  const auto margin = params.fill ? (params.band * 0.5 + 1.0)
                                  : (params.half_thickness + params.band * 0.5 + 1.0);
  const auto m = static_cast<std::int32_t>(std::ceil(margin));
  Rect shape_bbox{rect.x - m, rect.y - m, rect.width + 2 * m, rect.height + 2 * m};

  auto affected = intersect_rect(shape_bbox, canvas_rect(document));
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (!erase && !options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, affected);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  affected = intersect_rect(affected, bounds);
  if (affected.empty()) {
    return {};
  }

  bool wrote = false;
  for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
    auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
      const auto selected_coverage = selection_coverage(options, x, y);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      const auto coverage = shape_pixel_coverage(params, x, y) * selected_coverage;
      if (coverage <= 0.0F) {
        continue;
      }
      auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
      wrote = write_pixel(pixels, px, options, erase, coverage) || wrote;
    }
    report_edit_progress(options);
  }
  return wrote ? affected : Rect{};
}

}  // namespace

std::vector<GradientStop> normalized_gradient_stops(const std::vector<GradientStop>& stops) {
  std::vector<GradientStop> normalized;
  normalized.reserve(stops.size());
  for (const auto& stop : stops) {
    auto copy = stop;
    copy.location = std::clamp(copy.location, 0.0F, 1.0F);
    normalized.push_back(copy);
  }
  if (normalized.empty()) {
    normalized.push_back(GradientStop{0.0F, EditColor{0, 0, 0, 255}});
    normalized.push_back(GradientStop{1.0F, EditColor{255, 255, 255, 255}});
  } else if (normalized.size() == 1U) {
    normalized.push_back(GradientStop{1.0F, normalized.front().color});
  }
  std::sort(normalized.begin(), normalized.end(), [](const GradientStop& lhs, const GradientStop& rhs) {
    return lhs.location < rhs.location;
  });
  return normalized;
}

EditColor gradient_color_at(const std::vector<GradientStop>& sorted_stops, float opacity, bool reverse,
                            double position) {
  if (sorted_stops.empty()) {
    return EditColor{};
  }
  position = std::clamp(reverse ? 1.0 - position : position, 0.0, 1.0);
  opacity = std::clamp(opacity, 0.0F, 1.0F);
  const auto apply_opacity = [opacity](EditColor color) {
    color.a = static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<float>(color.a) * opacity), 0L, 255L));
    return color;
  };
  if (position <= sorted_stops.front().location) {
    return apply_opacity(sorted_stops.front().color);
  }
  if (position >= sorted_stops.back().location) {
    return apply_opacity(sorted_stops.back().color);
  }
  for (std::size_t index = 1; index < sorted_stops.size(); ++index) {
    const auto& right = sorted_stops[index];
    const auto& left = sorted_stops[index - 1U];
    if (position <= right.location) {
      const auto span = std::max(0.0001F, right.location - left.location);
      const auto t = (position - left.location) / static_cast<double>(span);
      return apply_opacity(lerp_color(left.color, right.color, t));
    }
  }
  return apply_opacity(sorted_stops.back().color);
}

void expand_layer_to_include_rect(Layer& layer, Rect document_rect) {
  document_rect = normalized_rect(document_rect);
  if (document_rect.empty()) {
    return;
  }

  auto& source = layer.pixels();
  if (source.empty()) {
    PixelBuffer expanded(document_rect.width, document_rect.height, PixelFormat::rgba8());
    expanded.clear(0);
    layer.set_pixels(std::move(expanded));
    layer.set_bounds(document_rect);
    return;
  }

  const auto old_bounds = layer.bounds();
  const auto new_bounds = unite_rect(old_bounds, document_rect);
  if (new_bounds.x == old_bounds.x && new_bounds.y == old_bounds.y && new_bounds.width == old_bounds.width &&
      new_bounds.height == old_bounds.height) {
    return;
  }

  const auto destination_format = canvas_resized_format_for_layer(layer, source);
  PixelBuffer expanded(new_bounds.width, new_bounds.height, destination_format);
  fill_resized_layer_background(expanded, layer);

  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      copy_resized_layer_pixel(source, expanded, x, y, old_bounds.x - new_bounds.x + x, old_bounds.y - new_bounds.y + y);
    }
  }

  layer.set_pixels(std::move(expanded));
  layer.set_bounds(new_bounds);
}

Rect paint_tip_dab(Document& document, LayerId layer_id, double x, double y, const EditOptions& options,
                   bool erase, const TipDabTransform& transform, float opacity_multiplier,
                   const BrushDabVariation* variation = nullptr) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr || options.brush_tip == nullptr || options.brush_tip->empty()) {
    return {};
  }
  if (erase) {
    ensure_alpha_for_erase(*layer);
  }

  const auto& tip = *options.brush_tip;
  const auto dab_rect = tip_dab_rect(x, y, tip, transform, canvas_rect(document));
  if (dab_rect.empty()) {
    return {};
  }
  if (!erase && !options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, dab_rect);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  auto dab_options = options;
  if (!erase && variation != nullptr && options.brush_dynamics.color_dynamics_enabled) {
    dab_options.primary = color_dynamics_color(options, *variation);
  }
  if (!erase && options.dab_primary_provider) {
    dab_options.primary = options.dab_primary_provider(x, y, dab_options.primary);
  }
  Rect dirty;

  for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
    const auto local_y = py - bounds.y;
    if (local_y < 0 || local_y >= pixels.height()) {
      continue;
    }

    auto row = pixels.row(local_y);
    for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
      const auto offset_x = static_cast<double>(px_doc) - x;
      const auto offset_y = static_cast<double>(py) - y;
      auto coverage = tip_dab_coverage(tip, transform, offset_x, offset_y);
      if (coverage <= 0.0F) {
        continue;
      }
      if (options.brush_dynamics.dual_brush_enabled) {
        coverage *= dual_brush_coverage(options.brush_dynamics, transform, offset_x, offset_y,
                                        options.brush_size);
      }
      if (options.brush_dynamics.texture_enabled && options.brush_dynamics.texture_depth > 0.0) {
        const auto grain = static_brush_texture(options.brush_dynamics, px_doc, py);
        const auto depth = static_cast<float>(
            std::clamp(options.brush_dynamics.texture_depth, 0.0, 1.0));
        coverage *= 1.0F - depth * (1.0F - grain);
      }
      if (coverage <= 0.0F) {
        continue;
      }
      const auto selected_coverage = selection_coverage(options, px_doc, py);
      if (selected_coverage <= 0.0F) {
        continue;
      }

      const auto local_x = px_doc - bounds.x;
      if (local_x < 0 || local_x >= pixels.width()) {
        continue;
      }
      const auto effective_coverage = coverage * selected_coverage * opacity_multiplier;
      if (options.stroke_coverage_observer) {
        options.stroke_coverage_observer(px_doc, py, effective_coverage, dab_options.primary);
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        continue;
      }

      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      const auto changed =
          options.stroke_pixel_writer
              ? options.stroke_pixel_writer(px_doc, py, px, channels, effective_coverage,
                                            dab_options.primary)
              : write_pixel(pixels, px, dab_options, erase, effective_coverage);
      if (changed) {
        dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
      }
    }
    report_edit_progress(options);
  }
  return dirty;
}

Rect paint_brush_dab(Document& document, LayerId layer_id, double x, double y, const EditOptions& options,
                     bool erase) {
  if (options.brush_tip != nullptr) {
    return paint_tip_dab(document, layer_id, x, y, options, erase, tip_dab_transform(options), 1.0F);
  }

  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }
  if (erase) {
    ensure_alpha_for_erase(*layer);
  }

  const auto radius = std::max(1, options.brush_size) / 2;
  const auto dab_rect = brush_dab_rect(x, y, radius, options, canvas_rect(document));
  if (!erase && !options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, dab_rect);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  auto dab_options = options;
  if (!erase && options.dab_primary_provider) {
    dab_options.primary = options.dab_primary_provider(x, y, dab_options.primary);
  }
  Rect dirty;

  for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
    const auto local_y = py - bounds.y;
    if (local_y < 0 || local_y >= pixels.height()) {
      continue;
    }

    auto row = pixels.row(local_y);
    for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
      const auto selected_coverage = selection_coverage(options, px_doc, py);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      const auto local_x = px_doc - bounds.x;
      if (local_x < 0 || local_x >= pixels.width()) {
        continue;
      }
      const auto effective_coverage =
          brush_shape_coverage(static_cast<double>(px_doc) - x, static_cast<double>(py) - y, radius, options) *
          selected_coverage;
      if (effective_coverage <= 0.0F) {
        continue;
      }
      if (options.stroke_coverage_observer) {
        options.stroke_coverage_observer(px_doc, py, effective_coverage, dab_options.primary);
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        continue;
      }

      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      const auto changed =
          options.stroke_pixel_writer
              ? options.stroke_pixel_writer(px_doc, py, px, channels, effective_coverage,
                                             dab_options.primary)
              : write_pixel(pixels, px, dab_options, erase, effective_coverage);
      if (changed) {
        dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
      }
    }
    report_edit_progress(options);
  }
  return dirty;
}

Rect paint_stationary_airbrush_dab(Document& document, LayerId layer_id, double x, double y,
                                   const EditOptions& options, BrushTipStrokeState& state) {
  if (options.brush_tip == nullptr || options.brush_tip->empty() ||
      !options.brush_dynamics.active()) {
    return paint_brush_dab(document, layer_id, x, y, options, false);
  }

  if (!state.initialized) {
    state.initialized = true;
    state.rng.seed(options.brush_dynamics.seed);
  }

  // Patent/design boundary: a stationary Airbrush tick is one current flat stamp. Shape and
  // Transfer dynamics still advance (including Fade), but Scattering and Count are movement-
  // stroke features here. Omitting their offsets, extra dabs, and RNG draws prevents a held
  // pointer from becoming a simulated spray-particle burst. The regular segment path retains
  // the full Photoshop-compatible dynamics behavior while the pointer moves.
  auto stationary_dynamics = options.brush_dynamics;
  stationary_dynamics.scatter = 0.0;
  const auto variation =
      sample_dab_variation(stationary_dynamics, state.rng, state.dynamics, options.brush_size);
  const auto transform = tip_dab_transform(options, variation);
  const auto dirty = paint_tip_dab(
      document, layer_id, x, y, options, false, transform,
      static_cast<float>(variation.opacity_multiplier * variation.flow_multiplier), &variation);
  ++state.dynamics.step_index;
  return dirty;
}

// Places tip dabs along [x0,y0]→[x1,y1] every brush_size * brush_tip_spacing pixels, resuming
// from state.residual_distance so chained segments keep a uniform dab cadence. With active
// brush dynamics each spacing step stamps `count` independently varied dabs (scatter offsets,
// per-dab transform, opacity/flow jitter); the RNG is seeded from options.brush_dynamics.seed on the
// stroke's first dab and its draw order is the contract documented in brush_dynamics.hpp.
// Scatter and count never perturb the spacing walk or residual_distance.
Rect paint_tip_segment(Document& document, LayerId layer_id, double x0, double y0, double x1, double y1,
                       const EditOptions& options, bool erase, BrushTipStrokeState& state) {
  const auto spacing =
      std::max(1.0, static_cast<double>(std::max(1, options.brush_size)) *
                        std::clamp(options.brush_tip_spacing, 0.01, 10.0));
  const auto& dynamics = options.brush_dynamics;
  const auto dynamic = dynamics.active();

  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  const auto distance = std::sqrt(dx * dx + dy * dy);
  const auto moved = distance > std::numeric_limits<double>::epsilon();

  Rect dirty;
  const auto stamp_step = [&](double x, double y) {
    if (!dynamic) {
      dirty = unite_rect(
          dirty, paint_tip_dab(document, layer_id, x, y, options, erase, tip_dab_transform(options), 1.0F));
    } else {
      const auto dab_count = sample_dab_count(dynamics, state.rng, state.dynamics);
      for (auto i = 0; i < dab_count; ++i) {
        const auto variation = sample_dab_variation(dynamics, state.rng, state.dynamics, options.brush_size);
        const auto transform = tip_dab_transform(options, variation);
        dirty = unite_rect(dirty, paint_tip_dab(document, layer_id, x + variation.offset_x,
                                                y + variation.offset_y, options, erase, transform,
                                                static_cast<float>(variation.opacity_multiplier *
                                                                   variation.flow_multiplier),
                                                &variation));
      }
    }
    ++state.dynamics.step_index;
  };

  if (!state.initialized) {
    state.initialized = true;
    if (dynamic) {
      state.rng.seed(dynamics.seed);
    }
    if (moved) {
      advance_stroke_direction(state.dynamics, dx, dy);
    }
    stamp_step(x0, y0);
    state.residual_distance = spacing;
  } else if (moved) {
    advance_stroke_direction(state.dynamics, dx, dy);
  }

  if (!moved) {
    return dirty;
  }

  auto position = state.residual_distance;
  while (position <= distance) {
    const auto t = position / distance;
    stamp_step(x0 + dx * t, y0 + dy * t);
    position += spacing;
  }
  state.residual_distance = position - distance;
  return dirty;
}

Rect paint_brush(Document& document, LayerId layer_id, std::int32_t x, std::int32_t y, const EditOptions& options,
                 bool erase) {
  return paint_brush_dab(document, layer_id, static_cast<double>(x), static_cast<double>(y), options, erase);
}

Rect paint_brush_segment(Document& document, LayerId layer_id, double x0, double y0, double x1, double y1,
                         const EditOptions& options, bool erase, BrushTipStrokeState& state) {
  if (options.brush_tip != nullptr && !options.brush_tip->empty()) {
    return paint_tip_segment(document, layer_id, x0, y0, x1, y1, options, erase, state);
  }
  return paint_brush_segment(document, layer_id, x0, y0, x1, y1, options, erase);
}

Rect paint_brush_segment(Document& document, LayerId layer_id, double x0, double y0, double x1, double y1,
                         const EditOptions& options, bool erase) {
  if (options.brush_tip != nullptr && !options.brush_tip->empty()) {
    BrushTipStrokeState state;
    return paint_tip_segment(document, layer_id, x0, y0, x1, y1, options, erase, state);
  }

  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }
  if (erase) {
    ensure_alpha_for_erase(*layer);
  }

  const auto radius = std::max(1, options.brush_size) / 2;
  if (!brush_shape_is_round(options) && radius > 0) {
    const auto dx = x1 - x0;
    const auto dy = y1 - y0;
    const auto distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= std::numeric_limits<double>::epsilon()) {
      return paint_brush_dab(document, layer_id, x0, y0, options, erase);
    }

    const auto spacing = std::max(1.0, static_cast<double>(std::max(1, options.brush_size)) * 0.125);
    const auto steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
    Rect dirty;
    for (int step = 0; step <= steps; ++step) {
      const auto t = static_cast<double>(step) / static_cast<double>(steps);
      dirty = unite_rect(dirty, paint_brush_dab(document, layer_id, x0 + dx * t, y0 + dy * t, options, erase));
    }
    return dirty;
  }

  if (radius == 0) {
    const auto start_x = static_cast<std::int32_t>(std::floor(x0));
    const auto start_y = static_cast<std::int32_t>(std::floor(y0));
    const auto end_x = static_cast<std::int32_t>(std::floor(x1));
    const auto end_y = static_cast<std::int32_t>(std::floor(y1));
    const auto path_rect = intersect_rect(Rect{std::min(start_x, end_x),
                                               std::min(start_y, end_y),
                                               std::abs(end_x - start_x) + 1,
                                               std::abs(end_y - start_y) + 1},
                                          canvas_rect(document));
    if (path_rect.empty()) {
      return {};
    }

    if (!erase && !options.lock_transparent_pixels) {
      expand_layer_to_include_rect(*layer, path_rect);
    }

    auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto channels = pixels.format().channels;
    Rect dirty;
    visit_pixel_line(start_x, start_y, end_x, end_y, [&](std::int32_t px_doc, std::int32_t py) {
      if (!canvas_rect(document).contains(px_doc, py)) {
        return;
      }
      const auto effective_coverage = selection_coverage(options, px_doc, py);
      if (effective_coverage <= 0.0F) {
        return;
      }
      const auto local_x = px_doc - bounds.x;
      const auto local_y = py - bounds.y;
      if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
        return;
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        return;
      }

      auto row = pixels.row(local_y);
      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      const auto changed =
          options.stroke_pixel_writer
              ? options.stroke_pixel_writer(px_doc, py, px, channels, effective_coverage,
                                            options.primary)
              : write_pixel(pixels, px, options, erase, effective_coverage);
      if (changed) {
        dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
      }
    });
    return dirty;
  }

  const auto left = static_cast<std::int32_t>(std::floor(std::min(x0, x1) - static_cast<double>(radius)));
  const auto top = static_cast<std::int32_t>(std::floor(std::min(y0, y1) - static_cast<double>(radius)));
  const auto right =
      static_cast<std::int32_t>(std::ceil(std::max(x0, x1) + static_cast<double>(radius))) + 1;
  const auto bottom =
      static_cast<std::int32_t>(std::ceil(std::max(y0, y1) + static_cast<double>(radius))) + 1;
  const auto stroke_rect = intersect_rect(Rect{left, top, right - left, bottom - top}, canvas_rect(document));
  if (stroke_rect.empty()) {
    return {};
  }

  if (!erase && !options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, stroke_rect);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  const auto segment_length_squared = dx * dx + dy * dy;
  Rect dirty;

  for (std::int32_t py = stroke_rect.y; py < stroke_rect.y + stroke_rect.height; ++py) {
    const auto local_y = py - bounds.y;
    if (local_y < 0 || local_y >= pixels.height()) {
      continue;
    }

    auto row = pixels.row(local_y);
    for (std::int32_t px_doc = stroke_rect.x; px_doc < stroke_rect.x + stroke_rect.width; ++px_doc) {
      const auto along =
          segment_length_squared <= std::numeric_limits<double>::epsilon()
              ? 0.0
              : std::clamp(((static_cast<double>(px_doc) - x0) * dx + (static_cast<double>(py) - y0) * dy) /
                                segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = x0 + dx * along;
      const auto closest_y = y0 + dy * along;
      const auto distance_x = static_cast<double>(px_doc) - closest_x;
      const auto distance_y = static_cast<double>(py) - closest_y;
      const auto coverage = brush_shape_coverage(distance_x, distance_y, radius, options);
      if (coverage <= 0.0F) {
        continue;
      }
      const auto selected_coverage = selection_coverage(options, px_doc, py);
      if (selected_coverage <= 0.0F) {
        continue;
      }

      const auto local_x = px_doc - bounds.x;
      if (local_x < 0 || local_x >= pixels.width()) {
        continue;
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        continue;
      }
      const auto effective_coverage = coverage * selected_coverage;

      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      const auto changed =
          options.stroke_pixel_writer
              ? options.stroke_pixel_writer(px_doc, py, px, channels, effective_coverage,
                                            options.primary)
              : write_pixel(pixels, px, options, erase, effective_coverage);
      if (changed) {
        dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
      }
    }
    report_edit_progress(options);
  }
  return dirty;
}

Rect paint_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                         std::int32_t y1, const EditOptions& options, bool erase) {
  return paint_brush_segment(document, layer_id, static_cast<double>(x0), static_cast<double>(y0),
                             static_cast<double>(x1), static_cast<double>(y1), options, erase);
}

Rect smudge_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1, const EditOptions& options) {
  SmudgeState state;
  return smudge_brush_segment(document, layer_id, x0, y0, x1, y1, options, state);
}

Rect smudge_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1, const EditOptions& options, SmudgeState& state) {
  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  if (dx == 0 && dy == 0) {
    auto* layer = editable_layer(document, layer_id);
    if (layer != nullptr) {
      const auto radius = std::max(1, options.brush_size) / 2;
      capture_smudge_sample(state, *layer, x0, y0, radius);
    }
    return {};
  }

  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  const auto radius = std::max(1, options.brush_size) / 2;
  const auto diameter = radius * 2 + 1;
  const auto left = std::min(x0, x1) - radius;
  const auto top = std::min(y0, y1) - radius;
  const auto right = std::max(x0, x1) + radius + 1;
  const auto bottom = std::max(y0, y1) + radius + 1;
  auto stroke_rect = intersect_rect(Rect{left, top, right - left, bottom - top}, canvas_rect(document));
  if (options.selection.has_value()) {
    stroke_rect = intersect_rect(stroke_rect, *options.selection);
  }
  if (stroke_rect.empty()) {
    return {};
  }

  if (!options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, stroke_rect);
  }

  stroke_rect = intersect_rect(stroke_rect, layer->bounds());
  if (stroke_rect.empty()) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  const auto color_channels = std::min<std::uint16_t>(channels, 3);
  const auto strength = static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F;
  if (!state.initialized || state.diameter != diameter) {
    capture_smudge_sample(state, *layer, x0, y0, radius);
  }

  Rect dirty;
  const auto stamp_at = [&](std::int32_t center_x, std::int32_t center_y) {
    const auto dab_rect = intersect_rect(
        Rect{center_x - radius, center_y - radius, diameter, diameter}, stroke_rect);
    if (dab_rect.empty()) {
      return;
    }

    for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
      auto row = pixels.row(py - bounds.y);
      for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
        const auto sample_x = px_doc - (center_x - radius);
        const auto sample_y = py - (center_y - radius);
        const auto distance_x = px_doc - center_x;
        const auto distance_y = py - center_y;
        auto coverage = brush_coverage(static_cast<double>(distance_x * distance_x + distance_y * distance_y),
                                       radius, options.brush_softness);
        coverage *= selection_coverage(options, px_doc, py);
        if (coverage <= 0.0F) {
          continue;
        }
        const auto* snap = options.palette_snap;
        if (snap != nullptr) {
          if (coverage < snap->coverage_threshold) {
            continue;
          }
          coverage = 1.0F;
        }

        if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
          continue;
        }

        auto* dst = row.data() + static_cast<std::size_t>(px_doc - bounds.x) * channels;
        if (options.lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
          continue;
        }

        std::array<std::uint8_t, 4> pre_snap{};
        if (snap != nullptr) {
          for (std::uint16_t channel = 0; channel < channels && channel < pre_snap.size(); ++channel) {
            pre_snap[channel] = dst[channel];
          }
        }
        const auto* src =
            state.sample_rgba.data() +
            (static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(state.diameter) +
             static_cast<std::size_t>(sample_x)) *
                4U;
        const auto amount = std::clamp(strength * coverage, 0.0F, 1.0F);
        bool changed = false;
        for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
          const auto value = clamp_byte(static_cast<float>(src[channel]) * amount +
                                        static_cast<float>(dst[channel]) * (1.0F - amount));
          changed = changed || value != dst[channel];
          dst[channel] = value;
        }
        if (channels >= 4 && !options.lock_transparent_pixels) {
          const auto alpha = clamp_byte(static_cast<float>(src[3]) * amount +
                                        static_cast<float>(dst[3]) * (1.0F - amount));
          changed = changed || alpha != dst[3];
          dst[3] = alpha;
        }
        if (snap != nullptr) {
          snap_pixel_to_palette(dst, channels, *snap);
          changed = false;
          for (std::uint16_t channel = 0; channel < channels && channel < pre_snap.size(); ++channel) {
            changed = changed || dst[channel] != pre_snap[channel];
          }
        }
        if (changed) {
          dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
        }
      }
    }

    const auto pickup = std::clamp(1.0F - strength, 0.0F, 1.0F);
    if (pickup <= 0.0F) {
      return;
    }
    for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
      for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
        const auto sample_x = px_doc - (center_x - radius);
        const auto sample_y = py - (center_y - radius);
        const auto distance_x = px_doc - center_x;
        const auto distance_y = py - center_y;
        auto coverage = brush_coverage(static_cast<double>(distance_x * distance_x + distance_y * distance_y),
                                       radius, options.brush_softness);
        coverage *= selection_coverage(options, px_doc, py);
        if (coverage <= 0.0F) {
          continue;
        }

        const auto current = sample_layer_rgba(*layer, px_doc, py);
        auto* dst =
            state.sample_rgba.data() +
            (static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(state.diameter) +
             static_cast<std::size_t>(sample_x)) *
                4U;
        const auto amount = std::clamp(pickup * coverage, 0.0F, 1.0F);
        for (std::size_t channel = 0; channel < 4U; ++channel) {
          dst[channel] = clamp_byte(static_cast<float>(current[channel]) * amount +
                                    static_cast<float>(dst[channel]) * (1.0F - amount));
        }
      }
    }
  };

  const auto distance = std::sqrt(static_cast<double>(dx) * static_cast<double>(dx) +
                                  static_cast<double>(dy) * static_cast<double>(dy));
  const auto spacing = std::max(1.0, static_cast<double>(radius) * 0.2);
  const auto steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
  auto last_center_x = std::numeric_limits<std::int32_t>::min();
  auto last_center_y = std::numeric_limits<std::int32_t>::min();
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    const auto center_x = static_cast<std::int32_t>(std::lround(static_cast<double>(x0) + static_cast<double>(dx) * t));
    const auto center_y = static_cast<std::int32_t>(std::lround(static_cast<double>(y0) + static_cast<double>(dy) * t));
    if (last_center_x == center_x && last_center_y == center_y) {
      continue;
    }
    stamp_at(center_x, center_y);
    last_center_x = center_x;
    last_center_y = center_y;
  }
  return dirty;
}

Rect draw_line(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
               const EditOptions& options, bool erase) {
  return paint_brush_segment(document, layer_id, x0, y0, x1, y1, options, erase);
}

ShapeCoverageParams make_shape_coverage_params(Rect rect, const EditOptions& options, ShapeKind kind) {
  rect = normalized_rect(rect);
  const auto rx = std::max(0.5, static_cast<double>(rect.width) / 2.0);
  const auto ry = std::max(0.5, static_cast<double>(rect.height) / 2.0);
  ShapeCoverageParams params;
  params.kind = kind;
  params.fill = options.fill_shapes;
  params.cx = static_cast<double>(rect.x) + static_cast<double>(rect.width) / 2.0;
  params.cy = static_cast<double>(rect.y) + static_cast<double>(rect.height) / 2.0;
  params.rx = rx;
  params.ry = ry;
  params.half_thickness = std::max(1, options.brush_size) / 2.0;
  if (kind == ShapeKind::Rectangle) {
    params.corner_radius = std::clamp(static_cast<double>(options.shape_corner_radius), 0.0, std::min(rx, ry));
  }
  const auto softness = static_cast<double>(std::clamp(options.brush_softness, 0, 100)) / 100.0;
  params.band = std::max(params.half_thickness * softness, 1.0);
  return params;
}

float shape_pixel_coverage(const ShapeCoverageParams& params, std::int32_t x, std::int32_t y) noexcept {
  const auto px = static_cast<double>(x) + 0.5;
  const auto py = static_cast<double>(y) + 0.5;

  double signed_distance = 0.0;  // distance to shape boundary, negative inside
  if (params.kind == ShapeKind::Rectangle) {
    signed_distance = rounded_box_distance(px, py, params.cx, params.cy, params.rx, params.ry,
                                           params.corner_radius);
  } else {
    const auto estimate = ellipse_distance_estimate(px, py, params.cx, params.cy, params.rx, params.ry);
    if (params.fill) {
      signed_distance = estimate;
    } else {
      // Outline: refine only near the ring; the cheap estimate culls the vast interior/exterior.
      if (std::abs(estimate) - params.half_thickness > params.band * 0.5 + 4.0) {
        return 0.0F;
      }
      signed_distance = ellipse_distance_exact(px, py, params.cx, params.cy, params.rx, params.ry);
    }
  }

  const auto sd = params.fill ? signed_distance : (std::abs(signed_distance) - params.half_thickness);
  return shape_edge_coverage(sd, params.band);
}

Rect draw_rectangle(Document& document, LayerId layer_id, Rect rect, const EditOptions& options, bool erase) {
  // A 1px outline is drawn with crisp Bresenham edges: a unit-width stroke centered on an integer
  // edge would otherwise split 50/50 across two pixel columns under the (symmetric) coverage profile.
  if (!options.fill_shapes && options.shape_corner_radius <= 0 && std::max(1, options.brush_size) <= 1) {
    rect = normalized_rect(rect);
    if (rect.empty()) {
      return {};
    }
    Rect dirty;
    dirty = unite_rect(dirty,
                       draw_line(document, layer_id, rect.x, rect.y, rect.x + rect.width - 1, rect.y, options, erase));
    dirty = unite_rect(dirty, draw_line(document, layer_id, rect.x + rect.width - 1, rect.y,
                                        rect.x + rect.width - 1, rect.y + rect.height - 1, options, erase));
    dirty = unite_rect(dirty, draw_line(document, layer_id, rect.x + rect.width - 1, rect.y + rect.height - 1, rect.x,
                                        rect.y + rect.height - 1, options, erase));
    dirty = unite_rect(dirty,
                       draw_line(document, layer_id, rect.x, rect.y + rect.height - 1, rect.x, rect.y, options, erase));
    return dirty;
  }
  return render_shape(document, layer_id, rect, options, erase, ShapeKind::Rectangle);
}

Rect draw_ellipse(Document& document, LayerId layer_id, Rect rect, const EditOptions& options, bool erase) {
  // See draw_rectangle: keep the crisp legacy path for a 1px outline.
  if (!options.fill_shapes && std::max(1, options.brush_size) <= 1) {
    rect = normalized_rect(rect);
    if (rect.empty()) {
      return {};
    }
    constexpr int kSamples = 720;
    const auto rx = std::max(1.0, static_cast<double>(rect.width) / 2.0);
    const auto ry = std::max(1.0, static_cast<double>(rect.height) / 2.0);
    const auto cx = static_cast<double>(rect.x) + rx;
    const auto cy = static_cast<double>(rect.y) + ry;
    auto previous_x = static_cast<std::int32_t>(std::round(cx + rx));
    auto previous_y = static_cast<std::int32_t>(std::round(cy));
    Rect dirty;
    for (int i = 1; i <= kSamples; ++i) {
      const auto angle = (static_cast<double>(i) / static_cast<double>(kSamples)) * 2.0 * 3.14159265358979323846;
      const auto current_x = static_cast<std::int32_t>(std::round(cx + std::cos(angle) * rx));
      const auto current_y = static_cast<std::int32_t>(std::round(cy + std::sin(angle) * ry));
      dirty = unite_rect(dirty,
                         draw_line(document, layer_id, previous_x, previous_y, current_x, current_y, options, erase));
      previous_x = current_x;
      previous_y = current_y;
    }
    return dirty;
  }
  return render_shape(document, layer_id, rect, options, erase, ShapeKind::Ellipse);
}

Rect flood_fill(Document& document, LayerId layer_id, std::int32_t x, std::int32_t y, const EditOptions& options) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr || !canvas_rect(document).contains(x, y) || !selection_allows(options, x, y)) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto local_start_x = x - bounds.x;
  const auto local_start_y = y - bounds.y;
  if (local_start_x < 0 || local_start_y < 0 || local_start_x >= pixels.width() || local_start_y >= pixels.height()) {
    return {};
  }

  const auto channels = pixels.format().channels;
  std::vector<std::uint8_t> target(channels);
  const auto* start_pixel = pixels.pixel(local_start_x, local_start_y);
  for (std::uint16_t channel = 0; channel < channels; ++channel) {
    target[channel] = start_pixel[channel];
  }
  if (options.lock_transparent_pixels && channels >= 4 && target[3] == 0) {
    return {};
  }

  std::vector<std::uint8_t> replacement{options.primary.r, options.primary.g, options.primary.b};
  if (channels >= 4) {
    replacement.push_back(options.lock_transparent_pixels ? target[3] : std::max<std::uint8_t>(1, options.primary.a));
  }
  if (options.palette_snap != nullptr) {
    // Flood writes replacement bytes verbatim, so snapping it once up front keeps
    // the whole filled region palette-exact.
    snap_pixel_to_palette(replacement.data(), channels, *options.palette_snap);
  }
  if (same_pixel(replacement.data(), target, channels)) {
    return {};
  }

  std::queue<std::pair<std::int32_t, std::int32_t>> queue;
  queue.emplace(local_start_x, local_start_y);
  Rect dirty;
  std::size_t progress_counter = 0;
  while (!queue.empty()) {
    report_edit_progress_periodically(options, progress_counter);
    const auto [local_x, local_y] = queue.front();
    queue.pop();
    if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
      continue;
    }

    const auto doc_x = local_x + bounds.x;
    const auto doc_y = local_y + bounds.y;
    if (!selection_allows(options, doc_x, doc_y)) {
      continue;
    }

    auto* px = pixels.pixel(local_x, local_y);
    if (!same_pixel(px, target, channels)) {
      continue;
    }

    for (std::uint16_t channel = 0; channel < channels; ++channel) {
      px[channel] = replacement[channel];
    }
    dirty = unite_rect(dirty, Rect{doc_x, doc_y, 1, 1});
    queue.emplace(local_x + 1, local_y);
    queue.emplace(local_x - 1, local_y);
    queue.emplace(local_x, local_y + 1);
    queue.emplace(local_x, local_y - 1);
  }
  return dirty;
}

Rect fill_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  auto affected = intersect_rect(normalized_rect(rect), canvas_rect(document));
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (!options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, affected);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  affected = intersect_rect(affected, bounds);
  if (affected.empty()) {
    return {};
  }

  const auto band = options.fill_softness_feather;
  const bool feather = band >= 1.0;
  std::vector<float> feather_factors;
  Rect feather_rect;
  if (feather) {
    const auto pad = static_cast<std::int32_t>(std::ceil(band)) + 1;
    feather_rect = Rect{affected.x - pad, affected.y - pad, affected.width + 2 * pad, affected.height + 2 * pad};
    feather_factors = compute_fill_feather(options, feather_rect, band);
  }

  for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
    auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
      auto coverage = selection_coverage(options, x, y);
      if (coverage <= 0.0F) {
        continue;
      }
      if (feather) {
        const auto fx = x - feather_rect.x;
        const auto fy = y - feather_rect.y;
        coverage = std::min(coverage, feather_factors[static_cast<std::size_t>(fy) * feather_rect.width + fx]);
        if (coverage <= 0.0F) {
          continue;
        }
      }
      auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
      write_pixel(pixels, px, options, false, coverage);
    }
    report_edit_progress(options);
  }
  return affected;
}

Rect clear_rect_change_bounds(const Document& document, LayerId layer_id, Rect rect, const EditOptions& options) {
  const auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  const auto affected = clear_affected_rect(document, *layer, rect, options);
  if (affected.empty()) {
    return {};
  }

  Rect changed;
  for_each_clear_candidate(*layer, affected, options,
                           [&layer, &options, &changed](std::int32_t x, std::int32_t y, const std::uint8_t* px,
                                                        float coverage) {
                             if (clear_pixel_would_change(layer->pixels(), px, options, coverage)) {
                               changed = unite_rect(changed, Rect{x, y, 1, 1});
                             }
                           });
  return changed;
}

Rect clear_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  const auto changed = clear_rect_change_bounds(document, layer_id, rect, options);
  if (changed.empty()) {
    return {};
  }

  if (layer->pixels().format().channels < 4) {
    ensure_alpha_for_erase(*layer);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  const auto affected = clear_affected_rect(document, *layer, changed, options);
  if (affected.empty()) {
    return {};
  }

  const auto scan_rect = [&](Rect rect) {
    rect = intersect_rect(rect, affected);
    if (rect.empty()) {
      return;
    }
    for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
      auto row = pixels.row(y - bounds.y);
      for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
        const auto selected_coverage = options.selection_scan_rects.empty() ? selection_coverage(options, x, y) : 1.0F;
        if (selected_coverage <= 0.0F) {
          continue;
        }
        auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
        write_pixel(pixels, px, options, true, selected_coverage);
      }
      report_edit_progress(options);
    }
  };

  if (options.selection_scan_rects.empty()) {
    scan_rect(affected);
  } else {
    for (const auto& scan : options.selection_scan_rects) {
      scan_rect(scan);
    }
  }
  return changed;
}

Rect draw_gradient(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                   std::int32_t y1, const EditOptions& options, const GradientOptions& gradient) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  auto affected = intersect_rect(bounds, canvas_rect(document));
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (affected.empty()) {
    return {};
  }

  const auto dx = static_cast<double>(x1 - x0);
  const auto dy = static_cast<double>(y1 - y0);
  const auto length_squared = dx * dx + dy * dy;
  const auto radius = std::sqrt(length_squared);
  auto stops = normalized_gradient_stops(gradient.stops.empty()
                                             ? std::vector<GradientStop>{
                                                   GradientStop{0.0F, options.primary},
                                                   GradientStop{1.0F, options.secondary}}
                                             : gradient.stops);
  auto gradient_options = options;
  for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
    auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
      const auto selected_coverage = selection_coverage(options, x, y);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      double t = 0.0;
      switch (gradient.method) {
        case GradientMethod::Radial:
          t = radius <= 0.0 ? 0.0
                            : std::sqrt(static_cast<double>(x - x0) * static_cast<double>(x - x0) +
                                        static_cast<double>(y - y0) * static_cast<double>(y - y0)) /
                                  radius;
          break;
        case GradientMethod::Linear:
          t = length_squared <= 0.0
                  ? 0.0
                  : (((static_cast<double>(x - x0) * dx) + (static_cast<double>(y - y0) * dy)) / length_squared);
          break;
      }
      const auto color = gradient_color_at(stops, gradient.opacity, gradient.reverse, t);
      if (color.a == 0) {
        continue;
      }
      gradient_options.primary = color;
      auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
      write_pixel(pixels, px, gradient_options, false, selected_coverage);
    }
    report_edit_progress(options);
  }
  return affected;
}

Rect draw_linear_gradient(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1, const EditOptions& options) {
  GradientOptions gradient;
  gradient.method = GradientMethod::Linear;
  gradient.opacity = 1.0F;
  gradient.stops.push_back(GradientStop{0.0F, options.primary});
  gradient.stops.push_back(GradientStop{1.0F, options.secondary});
  return draw_gradient(document, layer_id, x0, y0, x1, y1, options, gradient);
}

}  // namespace patchy
