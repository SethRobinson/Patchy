#include "ui/default_brush_tips.hpp"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

// Every tip below is pure math over a float coverage buffer: signed-distance shapes, value-noise
// grain, and seeded PRNG scatter. Fixed seeds keep the output identical between runs and
// machines, so the seeded library files are reproducible.
namespace patchy::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;

class CoverageBuffer {
public:
  CoverageBuffer(std::int32_t width, std::int32_t height)
      : width_(width), height_(height), values_(static_cast<std::size_t>(width) * height, 0.0F) {}

  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }

  [[nodiscard]] float& at(std::int32_t x, std::int32_t y) {
    return values_[static_cast<std::size_t>(y) * width_ + x];
  }

  void add(std::int32_t x, std::int32_t y, float value) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
      return;
    }
    auto& px = at(x, y);
    px = std::min(1.0F, px + value);
  }

  [[nodiscard]] patchy::BrushTip to_tip(double spacing) const {
    patchy::BrushTip tip;
    tip.width = width_;
    tip.height = height_;
    tip.default_spacing = spacing;
    tip.mask.resize(values_.size());
    for (std::size_t index = 0; index < values_.size(); ++index) {
      tip.mask[index] =
          static_cast<std::uint8_t>(std::clamp(std::lround(values_[index] * 255.0F), 0L, 255L));
    }
    return tip;
  }

private:
  std::int32_t width_;
  std::int32_t height_;
  std::vector<float> values_;
};

// Deterministic lattice hash → [0,1).
[[nodiscard]] float hash01(std::int32_t x, std::int32_t y, std::uint32_t seed) noexcept {
  auto h = static_cast<std::uint32_t>(x) * 0x8DA6B343U + static_cast<std::uint32_t>(y) * 0xD8163841U +
           seed * 0xCB1AB31FU;
  h ^= h >> 13U;
  h *= 0x7FEB352DU;
  h ^= h >> 15U;
  return static_cast<float>(h & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

[[nodiscard]] float smooth01(float t) noexcept {
  return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float value_noise(float x, float y, std::uint32_t seed) noexcept {
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto tx = smooth01(x - static_cast<float>(x0));
  const auto ty = smooth01(y - static_cast<float>(y0));
  const auto top = hash01(x0, y0, seed) * (1.0F - tx) + hash01(x0 + 1, y0, seed) * tx;
  const auto bottom = hash01(x0, y0 + 1, seed) * (1.0F - tx) + hash01(x0 + 1, y0 + 1, seed) * tx;
  return top * (1.0F - ty) + bottom * ty;
}

// Fractal value noise in [0,1] (approximately), `octaves` layers.
[[nodiscard]] float fbm(float x, float y, std::uint32_t seed, int octaves) noexcept {
  float sum = 0.0F;
  float amplitude = 0.5F;
  float frequency = 1.0F;
  for (int octave = 0; octave < octaves; ++octave) {
    sum += amplitude * value_noise(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(octave));
    amplitude *= 0.5F;
    frequency *= 2.0F;
  }
  return sum;
}

// 1px-band antialiasing for signed distances (negative = inside).
[[nodiscard]] float sdf_coverage(float signed_distance) noexcept {
  return std::clamp(0.5F - signed_distance, 0.0F, 1.0F);
}

[[nodiscard]] float rounded_rect_sdf(float x, float y, float half_width, float half_height,
                                     float corner_radius) noexcept {
  const auto qx = std::abs(x) - (half_width - corner_radius);
  const auto qy = std::abs(y) - (half_height - corner_radius);
  const auto ax = std::max(qx, 0.0F);
  const auto ay = std::max(qy, 0.0F);
  return std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0F) - corner_radius;
}

// Soft round dab (used as a building block for droplets and dots).
void stamp_dot(CoverageBuffer& buffer, float cx, float cy, float radius, float alpha, float softness = 0.35F) {
  const auto extent = static_cast<std::int32_t>(std::ceil(radius + 1.0F));
  const auto x_begin = static_cast<std::int32_t>(std::floor(cx)) - extent;
  const auto y_begin = static_cast<std::int32_t>(std::floor(cy)) - extent;
  for (std::int32_t y = y_begin; y <= y_begin + 2 * extent; ++y) {
    for (std::int32_t x = x_begin; x <= x_begin + 2 * extent; ++x) {
      const auto dx = static_cast<float>(x) - cx;
      const auto dy = static_cast<float>(y) - cy;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      const auto inner = radius * (1.0F - softness);
      float coverage = 1.0F;
      if (distance > radius) {
        coverage = std::clamp(1.0F - (distance - radius), 0.0F, 1.0F);  // 1px AA skirt
      } else if (distance > inner) {
        coverage = 1.0F - smooth01((distance - inner) / std::max(0.001F, radius - inner));
      }
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage * alpha);
      }
    }
  }
}

// Gradient-normalized approximate ellipse SDF (axis-aligned; pre-rotate coordinates for tilted
// petals). Exact enough for 1px-band antialiased masks.
[[nodiscard]] float ellipse_sdf(float x, float y, float rx, float ry) noexcept {
  const auto kx = x / rx;
  const auto ky = y / ry;
  const auto k0 = std::sqrt(kx * kx + ky * ky);
  const auto gx = x / (rx * rx);
  const auto gy = y / (ry * ry);
  const auto k1 = std::sqrt(gx * gx + gy * gy);
  if (k1 < 1e-6F) {
    return -std::min(rx, ry);
  }
  return k0 * (k0 - 1.0F) / k1;
}

// Signed distance to the line through A-B, sign fixed so the reference point is negative.
[[nodiscard]] float half_plane_sd(float px, float py, float ax, float ay, float bx, float by,
                                  float ref_x, float ref_y) noexcept {
  const auto ex = bx - ax;
  const auto ey = by - ay;
  const auto length = std::max(0.0001F, std::sqrt(ex * ex + ey * ey));
  const auto nx = ey / length;
  const auto ny = -ex / length;
  const auto d = (px - ax) * nx + (py - ay) * ny;
  const auto ref = (ref_x - ax) * nx + (ref_y - ay) * ny;
  return ref > 0.0F ? -d : d;
}

// Solid triangle SDF (orientation-agnostic; exact inside, chamfered outside corners).
[[nodiscard]] float triangle_sdf(float px, float py, float x0, float y0, float x1, float y1,
                                 float x2, float y2) noexcept {
  const auto cx = (x0 + x1 + x2) / 3.0F;
  const auto cy = (y0 + y1 + y2) / 3.0F;
  const auto d0 = half_plane_sd(px, py, x0, y0, x1, y1, cx, cy);
  const auto d1 = half_plane_sd(px, py, x1, y1, x2, y2, cx, cy);
  const auto d2 = half_plane_sd(px, py, x2, y2, x0, y0, cx, cy);
  return std::max({d0, d1, d2});
}

// Solid axis-aligned ellipse (paw pads, shoe soles, flower petals via pre-rotated coords).
void stamp_ellipse(CoverageBuffer& buffer, float cx, float cy, float rx, float ry, float alpha) {
  const auto x_begin = static_cast<std::int32_t>(std::floor(cx - rx - 1.0F));
  const auto x_end = static_cast<std::int32_t>(std::ceil(cx + rx + 1.0F));
  const auto y_begin = static_cast<std::int32_t>(std::floor(cy - ry - 1.0F));
  const auto y_end = static_cast<std::int32_t>(std::ceil(cy + ry + 1.0F));
  for (std::int32_t y = y_begin; y <= y_end; ++y) {
    for (std::int32_t x = x_begin; x <= x_end; ++x) {
      const auto coverage =
          sdf_coverage(ellipse_sdf(static_cast<float>(x) - cx, static_cast<float>(y) - cy, rx, ry));
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage * alpha);
      }
    }
  }
}

// Solid rounded rectangle (bars, dashes, confetti squares, shoe heels).
void stamp_rounded_rect(CoverageBuffer& buffer, float cx, float cy, float half_width,
                        float half_height, float corner_radius, float alpha) {
  const auto x_begin = static_cast<std::int32_t>(std::floor(cx - half_width - 1.0F));
  const auto x_end = static_cast<std::int32_t>(std::ceil(cx + half_width + 1.0F));
  const auto y_begin = static_cast<std::int32_t>(std::floor(cy - half_height - 1.0F));
  const auto y_end = static_cast<std::int32_t>(std::ceil(cy + half_height + 1.0F));
  for (std::int32_t y = y_begin; y <= y_end; ++y) {
    for (std::int32_t x = x_begin; x <= x_end; ++x) {
      const auto coverage = sdf_coverage(rounded_rect_sdf(static_cast<float>(x) - cx,
                                                          static_cast<float>(y) - cy, half_width,
                                                          half_height, corner_radius));
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage * alpha);
      }
    }
  }
}

// Knockout counterparts of stamp_dot/stamp_capsule: multiply existing coverage by
// (1 - carve coverage). Used for leaf midribs, bubble highlights, and the logo's negative-space R.
void carve_dot(CoverageBuffer& buffer, float cx, float cy, float radius, float strength = 1.0F) {
  const auto extent = static_cast<std::int32_t>(std::ceil(radius + 1.0F));
  const auto x_begin = std::max(0, static_cast<std::int32_t>(std::floor(cx)) - extent);
  const auto y_begin = std::max(0, static_cast<std::int32_t>(std::floor(cy)) - extent);
  const auto x_end = std::min(buffer.width() - 1, static_cast<std::int32_t>(std::floor(cx)) + extent);
  const auto y_end = std::min(buffer.height() - 1, static_cast<std::int32_t>(std::floor(cy)) + extent);
  for (std::int32_t y = y_begin; y <= y_end; ++y) {
    for (std::int32_t x = x_begin; x <= x_end; ++x) {
      const auto dx = static_cast<float>(x) - cx;
      const auto dy = static_cast<float>(y) - cy;
      const auto coverage = sdf_coverage(std::sqrt(dx * dx + dy * dy) - radius);
      if (coverage > 0.0F) {
        buffer.at(x, y) *= 1.0F - coverage * strength;
      }
    }
  }
}

void carve_capsule(CoverageBuffer& buffer, float x0, float y0, float x1, float y1, float radius0,
                   float radius1, float strength = 1.0F) {
  const auto pad = std::max(radius0, radius1) + 1.0F;
  const auto min_x = std::max(0, static_cast<std::int32_t>(std::floor(std::min(x0, x1) - pad)));
  const auto max_x = std::min(buffer.width() - 1, static_cast<std::int32_t>(std::ceil(std::max(x0, x1) + pad)));
  const auto min_y = std::max(0, static_cast<std::int32_t>(std::floor(std::min(y0, y1) - pad)));
  const auto max_y = std::min(buffer.height() - 1, static_cast<std::int32_t>(std::ceil(std::max(y0, y1) + pad)));
  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  const auto length_squared = std::max(0.0001F, dx * dx + dy * dy);
  for (std::int32_t y = min_y; y <= max_y; ++y) {
    for (std::int32_t x = min_x; x <= max_x; ++x) {
      const auto px = static_cast<float>(x) - x0;
      const auto py = static_cast<float>(y) - y0;
      const auto t = std::clamp((px * dx + py * dy) / length_squared, 0.0F, 1.0F);
      const auto closest_x = px - dx * t;
      const auto closest_y = py - dy * t;
      const auto distance = std::sqrt(closest_x * closest_x + closest_y * closest_y);
      const auto radius = radius0 + (radius1 - radius0) * t;
      const auto coverage = sdf_coverage(distance - radius);
      if (coverage > 0.0F) {
        buffer.at(x, y) *= 1.0F - coverage * strength;
      }
    }
  }
}

// 4x4-supersampled boolean fill for silhouettes without a convenient SDF (the heart curve).
template <typename InsideFn>
void stamp_filled_region(CoverageBuffer& buffer, InsideFn&& inside, float alpha = 1.0F) {
  constexpr int kSub = 4;  // 17 coverage levels; plenty for masks that get mip-minified anyway
  for (std::int32_t y = 0; y < buffer.height(); ++y) {
    for (std::int32_t x = 0; x < buffer.width(); ++x) {
      int hits = 0;
      for (int sy = 0; sy < kSub; ++sy) {
        for (int sx = 0; sx < kSub; ++sx) {
          if (inside(static_cast<float>(x) + (static_cast<float>(sx) + 0.5F) / kSub,
                     static_cast<float>(y) + (static_cast<float>(sy) + 0.5F) / kSub)) {
            ++hits;
          }
        }
      }
      if (hits > 0) {
        buffer.add(x, y, alpha * static_cast<float>(hits) / (kSub * kSub));
      }
    }
  }
}

// Capsule (line segment with round caps and per-end radii); used for arms, blades, streaks.
void stamp_capsule(CoverageBuffer& buffer, float x0, float y0, float x1, float y1, float radius0,
                   float radius1, float alpha) {
  const auto min_x = static_cast<std::int32_t>(std::floor(std::min(x0, x1) - std::max(radius0, radius1) - 1));
  const auto max_x = static_cast<std::int32_t>(std::ceil(std::max(x0, x1) + std::max(radius0, radius1) + 1));
  const auto min_y = static_cast<std::int32_t>(std::floor(std::min(y0, y1) - std::max(radius0, radius1) - 1));
  const auto max_y = static_cast<std::int32_t>(std::ceil(std::max(y0, y1) + std::max(radius0, radius1) + 1));
  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  const auto length_squared = std::max(0.0001F, dx * dx + dy * dy);
  for (std::int32_t y = min_y; y <= max_y; ++y) {
    for (std::int32_t x = min_x; x <= max_x; ++x) {
      const auto px = static_cast<float>(x) - x0;
      const auto py = static_cast<float>(y) - y0;
      const auto t = std::clamp((px * dx + py * dy) / length_squared, 0.0F, 1.0F);
      const auto closest_x = px - dx * t;
      const auto closest_y = py - dy * t;
      const auto distance = std::sqrt(closest_x * closest_x + closest_y * closest_y);
      const auto radius = radius0 + (radius1 - radius0) * t;
      const auto coverage = sdf_coverage(distance - radius);
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage * alpha);
      }
    }
  }
}

[[nodiscard]] patchy::BrushTip make_chalk_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 101;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 64.0F;
      // Block shape whose boundary is eaten away by noise, plus coarse interior grain.
      const auto wobble = (fbm(static_cast<float>(x) * 0.10F, static_cast<float>(y) * 0.10F, kSeed, 3) - 0.5F) * 22.0F;
      const auto sdf = rounded_rect_sdf(fx, fy, 46.0F, 46.0F, 14.0F) + wobble;
      const auto base = sdf_coverage(sdf / 2.5F);
      if (base <= 0.0F) {
        continue;
      }
      const auto grain = fbm(static_cast<float>(x) * 0.22F, static_cast<float>(y) * 0.22F, kSeed + 7, 3);
      const auto chalky = std::clamp((grain - 0.18F) * 1.9F, 0.0F, 1.0F);
      buffer.at(x, y) = std::clamp(base * (0.35F + 0.65F * chalky), 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.08);
}

[[nodiscard]] patchy::BrushTip make_charcoal_tip() {
  constexpr std::int32_t kWidth = 156;
  constexpr std::int32_t kHeight = 96;
  CoverageBuffer buffer(kWidth, kHeight);
  constexpr std::uint32_t kSeed = 202;
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      const auto fx = static_cast<float>(x) - kWidth / 2.0F;
      const auto fy = static_cast<float>(y) - kHeight / 2.0F;
      const auto wobble = (fbm(static_cast<float>(x) * 0.09F, static_cast<float>(y) * 0.09F, kSeed, 3) - 0.5F) * 16.0F;
      const auto sdf = rounded_rect_sdf(fx, fy, 66.0F, 34.0F, 12.0F) + wobble;
      const auto base = sdf_coverage(sdf / 2.0F);
      if (base <= 0.0F) {
        continue;
      }
      // Streaks running along the bar: low-frequency bands in y, broken up by grain.
      const auto band = value_noise(static_cast<float>(x) * 0.035F, static_cast<float>(y) * 0.30F, kSeed + 3);
      const auto streak = std::clamp((band - 0.22F) * 2.4F, 0.0F, 1.0F);
      const auto grain = fbm(static_cast<float>(x) * 0.30F, static_cast<float>(y) * 0.30F, kSeed + 9, 2);
      buffer.at(x, y) = std::clamp(base * streak * (0.45F + 0.55F * grain) * 1.35F, 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.06);
}

[[nodiscard]] patchy::BrushTip make_pastel_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 303;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 64.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy);
      const auto wobble = (fbm(static_cast<float>(x) * 0.075F, static_cast<float>(y) * 0.075F, kSeed, 3) - 0.5F) * 24.0F;
      const auto base = sdf_coverage((distance - 52.0F + wobble) / 3.0F);
      if (base <= 0.0F) {
        continue;
      }
      // Heavy tooth: the paper grain must survive stroke accumulation, so push contrast hard.
      const auto grain = fbm(static_cast<float>(x) * 0.20F, static_cast<float>(y) * 0.20F, kSeed + 5, 3);
      const auto toothy = std::clamp((grain - 0.30F) * 2.2F, 0.0F, 1.0F);
      buffer.at(x, y) = std::clamp(base * (0.12F + 0.88F * toothy), 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.08);
}

[[nodiscard]] patchy::BrushTip make_bristle_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  std::mt19937 rng(404);
  std::uniform_real_distribution<float> jitter(-3.0F, 3.0F);
  std::uniform_real_distribution<float> alpha_pick(0.35F, 1.0F);
  std::uniform_real_distribution<float> radius_pick(1.2F, 3.4F);
  // A vertical row of bristle dots: stroking horizontally streaks each dot into its own line.
  constexpr int kBristles = 14;
  for (int bristle = 0; bristle < kBristles; ++bristle) {
    const auto t = (static_cast<float>(bristle) + 0.5F) / kBristles;
    const auto y = 10.0F + t * 108.0F + jitter(rng);
    const auto x = 64.0F + jitter(rng) * 2.2F;
    stamp_dot(buffer, x, y, radius_pick(rng), alpha_pick(rng), 0.45F);
  }
  return buffer.to_tip(0.03);
}

[[nodiscard]] patchy::BrushTip make_sponge_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 505;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 64.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy);
      const auto base = sdf_coverage((distance - 54.0F) / 2.0F);
      if (base <= 0.0F) {
        continue;
      }
      // High-contrast cellular holes.
      const auto cells = fbm(static_cast<float>(x) * 0.13F, static_cast<float>(y) * 0.13F, kSeed, 3);
      const auto porous = std::clamp((cells - 0.34F) * 3.4F, 0.0F, 1.0F);
      buffer.at(x, y) = std::clamp(base * porous, 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.15);
}

[[nodiscard]] patchy::BrushTip make_canvas_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 606;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 64.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy);
      const auto inner = 44.0F;
      float base = 1.0F;
      if (distance > 56.0F) {
        base = 0.0F;
      } else if (distance > inner) {
        base = 1.0F - smooth01((distance - inner) / (56.0F - inner));
      }
      if (base <= 0.0F) {
        continue;
      }
      // Woven warp/weft: deep gaps between threads so the weave survives accumulation.
      const auto warp = 0.5F + 0.5F * std::sin(static_cast<float>(x) * 0.65F);
      const auto weft = 0.5F + 0.5F * std::sin(static_cast<float>(y) * 0.65F);
      const auto thread = std::max(warp * warp, weft * weft);
      const auto weave = std::clamp((thread - 0.35F) * 1.9F, 0.0F, 1.0F);
      const auto irregular = 0.75F + 0.5F * value_noise(static_cast<float>(x) * 0.11F,
                                                        static_cast<float>(y) * 0.11F, kSeed);
      buffer.at(x, y) = std::clamp(base * weave * irregular, 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.10);
}

[[nodiscard]] patchy::BrushTip make_smoke_tip() {
  constexpr std::int32_t kSize = 144;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 707;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 72.0F;
      const auto fy = static_cast<float>(y) - 72.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy) / 68.0F;
      if (distance >= 1.0F) {
        continue;
      }
      const auto falloff = 1.0F - smooth01(distance);
      const auto cloud = fbm(static_cast<float>(x) * 0.055F, static_cast<float>(y) * 0.055F, kSeed, 4);
      // Deliberately translucent: builds up like soft smoke instead of flooding to black.
      buffer.at(x, y) = std::clamp(falloff * cloud * 0.62F, 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.12);
}

[[nodiscard]] patchy::BrushTip make_spray_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  std::mt19937 rng(808);
  std::normal_distribution<float> radial(0.0F, 24.0F);
  std::uniform_real_distribution<float> angle_pick(0.0F, static_cast<float>(2.0 * kPi));
  std::uniform_real_distribution<float> alpha_pick(0.25F, 0.9F);
  std::uniform_real_distribution<float> radius_pick(0.6F, 1.6F);
  for (int droplet = 0; droplet < 900; ++droplet) {
    const auto angle = angle_pick(rng);
    const auto distance = std::min(std::abs(radial(rng)), 58.0F);
    const auto x = 64.0F + std::cos(angle) * distance;
    const auto y = 64.0F + std::sin(angle) * distance;
    stamp_dot(buffer, x, y, radius_pick(rng), alpha_pick(rng), 0.5F);
  }
  return buffer.to_tip(0.05);
}

[[nodiscard]] patchy::BrushTip make_spatter_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  std::mt19937 rng(909);
  std::normal_distribution<float> radial(0.0F, 23.0F);
  std::uniform_real_distribution<float> angle_pick(0.0F, static_cast<float>(2.0 * kPi));
  std::uniform_real_distribution<float> radius_pick(1.0F, 6.5F);
  std::uniform_real_distribution<float> alpha_pick(0.7F, 1.0F);
  // Enough droplets that repeated stamps read as texture, not as a recognizable cluster.
  for (int blob = 0; blob < 42; ++blob) {
    const auto angle = angle_pick(rng);
    const auto distance = std::min(std::abs(radial(rng)), 56.0F);
    const auto x = 64.0F + std::cos(angle) * distance;
    const auto y = 64.0F + std::sin(angle) * distance;
    const auto radius = radius_pick(rng) * (1.0F - distance / 110.0F);  // smaller near the rim
    stamp_dot(buffer, x, y, std::max(0.8F, radius), alpha_pick(rng), 0.25F);
  }
  return buffer.to_tip(0.4);
}

[[nodiscard]] patchy::BrushTip make_stipple_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  std::mt19937 rng(1013);
  std::uniform_real_distribution<float> angle_pick(0.0F, static_cast<float>(2.0 * kPi));
  std::uniform_real_distribution<float> distance_pick(0.0F, 1.0F);
  std::uniform_real_distribution<float> radius_pick(3.2F, 6.8F);
  // Isotropic scatter inside the disc (sqrt keeps density uniform) so repeats do not read as a
  // recognizable constellation.
  for (int dot = 0; dot < 13; ++dot) {
    const auto angle = angle_pick(rng);
    const auto distance = std::sqrt(distance_pick(rng)) * 50.0F;
    stamp_dot(buffer, 64.0F + std::cos(angle) * distance, 64.0F + std::sin(angle) * distance,
              radius_pick(rng), 1.0F, 0.2F);
  }
  return buffer.to_tip(0.6);
}

[[nodiscard]] patchy::BrushTip make_ink_splat_tip() {
  constexpr std::int32_t kSize = 160;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 1111;
  // Irregular core blob.
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 80.0F;
      const auto fy = static_cast<float>(y) - 80.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy);
      const auto wobble = (fbm(static_cast<float>(x) * 0.08F, static_cast<float>(y) * 0.08F, kSeed, 3) - 0.5F) * 18.0F;
      const auto coverage = sdf_coverage((distance - 30.0F + wobble) / 1.5F);
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage);
      }
    }
  }
  // Radial arms with droplets flying off the ends.
  std::mt19937 rng(kSeed);
  std::uniform_real_distribution<float> angle_jitter(-0.25F, 0.25F);
  std::uniform_real_distribution<float> length_pick(34.0F, 66.0F);
  std::uniform_real_distribution<float> width_pick(2.6F, 5.4F);
  constexpr int kArms = 12;
  for (int arm = 0; arm < kArms; ++arm) {
    const auto angle = static_cast<float>(arm) / kArms * static_cast<float>(2.0 * kPi) + angle_jitter(rng);
    const auto length = length_pick(rng);
    const auto x0 = 80.0F + std::cos(angle) * 16.0F;
    const auto y0 = 80.0F + std::sin(angle) * 16.0F;
    const auto x1 = 80.0F + std::cos(angle) * (16.0F + length);
    const auto y1 = 80.0F + std::sin(angle) * (16.0F + length);
    stamp_capsule(buffer, x0, y0, x1, y1, width_pick(rng), 0.8F, 1.0F);
    if ((arm % 3) != 2) {
      const auto drop_distance = 16.0F + length + 5.0F + 6.0F * hash01(arm, 7, kSeed);
      stamp_dot(buffer, 80.0F + std::cos(angle) * drop_distance, 80.0F + std::sin(angle) * drop_distance,
                1.8F + 2.2F * hash01(arm, 11, kSeed), 1.0F, 0.25F);
    }
  }
  return buffer.to_tip(1.5);
}

[[nodiscard]] patchy::BrushTip make_grunge_tip() {
  constexpr std::int32_t kSize = 144;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 1212;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 72.0F;
      const auto fy = static_cast<float>(y) - 72.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy);
      // Circular mask with a wide organic fade so stamped patches show no geometric outline.
      const auto edge_wobble =
          (fbm(static_cast<float>(x) * 0.06F, static_cast<float>(y) * 0.06F, kSeed + 11, 3) - 0.5F) * 30.0F;
      const auto mask = std::clamp(1.0F - (distance + edge_wobble - 40.0F) / 26.0F, 0.0F, 1.0F);
      if (mask <= 0.0F) {
        continue;
      }
      const auto texture = fbm(static_cast<float>(x) * 0.05F, static_cast<float>(y) * 0.05F, kSeed, 4);
      const auto detail = fbm(static_cast<float>(x) * 0.21F, static_cast<float>(y) * 0.21F, kSeed + 3, 2);
      const auto combined = std::clamp((texture - 0.34F) * 2.4F, 0.0F, 1.0F) * (0.45F + 0.55F * detail);
      buffer.at(x, y) = std::clamp(mask * combined * 1.25F, 0.0F, 1.0F);
    }
  }
  return buffer.to_tip(0.8);
}

[[nodiscard]] patchy::BrushTip make_square_tip() {
  constexpr std::int32_t kSize = 96;
  CoverageBuffer buffer(kSize, kSize);
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 48.0F;
      const auto fy = static_cast<float>(y) - 48.0F;
      buffer.at(x, y) = sdf_coverage(rounded_rect_sdf(fx, fy, 42.0F, 42.0F, 0.5F));
    }
  }
  return buffer.to_tip(0.05);  // tight spacing keeps diagonal stroke edges from scalloping
}

[[nodiscard]] patchy::BrushTip make_calligraphy_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  // Thin flat nib at 45°: strokes get thick/thin contrast depending on direction.
  const auto c = std::cos(kPi / 4.0);
  const auto s = std::sin(kPi / 4.0);
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 64.0F;
      const auto u = static_cast<float>(c) * fx + static_cast<float>(s) * fy;
      const auto v = -static_cast<float>(s) * fx + static_cast<float>(c) * fy;
      buffer.at(x, y) = sdf_coverage(rounded_rect_sdf(u, v, 54.0F, 7.0F, 6.0F));
    }
  }
  return buffer.to_tip(0.04);
}

[[nodiscard]] patchy::BrushTip make_star_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr float kOuterRadius = 58.0F;
  constexpr float kInnerRadius = 24.0F;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 64.0F;
      const auto distance = std::sqrt(fx * fx + fy * fy);
      // 5-point star: radius limit oscillates between outer (points) and inner (notches).
      auto angle = std::atan2(fy, fx) + static_cast<float>(kPi) / 2.0F;  // a point faces up
      const auto sector = static_cast<float>(2.0 * kPi) / 5.0F;
      angle = std::fmod(std::fmod(angle, sector) + sector, sector);
      const auto blend = std::abs(angle - sector / 2.0F) / (sector / 2.0F);  // 1 at point, 0 at notch
      const auto limit = kInnerRadius + (kOuterRadius - kInnerRadius) * std::pow(blend, 1.6F);
      buffer.at(x, y) = sdf_coverage((distance - limit) / 1.5F);
    }
  }
  return buffer.to_tip(1.3);
}

[[nodiscard]] patchy::BrushTip make_grass_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  std::mt19937 rng(1414);
  std::uniform_real_distribution<float> base_pick(34.0F, 94.0F);
  std::uniform_real_distribution<float> lean_pick(-26.0F, 26.0F);
  std::uniform_real_distribution<float> height_pick(58.0F, 96.0F);
  std::uniform_real_distribution<float> width_pick(1.8F, 3.2F);
  constexpr int kBlades = 11;
  for (int blade = 0; blade < kBlades; ++blade) {
    const auto base_x = base_pick(rng);
    const auto lean = lean_pick(rng);
    const auto height = height_pick(rng);
    const auto width = width_pick(rng);
    // Approximate a curved blade with three tapering capsule segments.
    const auto base_y = 120.0F;
    float previous_x = base_x;
    float previous_y = base_y;
    float previous_width = width;
    for (int segment = 1; segment <= 3; ++segment) {
      const auto t = static_cast<float>(segment) / 3.0F;
      const auto x = base_x + lean * t * t;
      const auto y = base_y - height * t;
      const auto segment_width = width * (1.0F - 0.78F * t);
      stamp_capsule(buffer, previous_x, previous_y, x, y, previous_width, segment_width, 1.0F);
      previous_x = x;
      previous_y = y;
      previous_width = segment_width;
    }
  }
  return buffer.to_tip(0.6);
}

// ---- Path / line stamps. Direction-control convention: bitmap +X = direction of travel ----

[[nodiscard]] patchy::BrushTip make_dotted_line_tip() {
  // A plain hard disc; the big default spacing is the whole point (the procedural Round brush
  // renders capsules and has no spacing control, so it cannot make dotted lines).
  constexpr std::int32_t kSize = 96;
  CoverageBuffer buffer(kSize, kSize);
  stamp_dot(buffer, 48.0F, 48.0F, 40.0F, 1.0F, 0.0F);
  return buffer.to_tip(2.5);
}

[[nodiscard]] patchy::BrushTip make_dashed_line_tip() {
  constexpr std::int32_t kWidth = 144;
  constexpr std::int32_t kHeight = 48;
  CoverageBuffer buffer(kWidth, kHeight);
  stamp_rounded_rect(buffer, 72.0F, 24.0F, 62.0F, 14.0F, 12.0F, 1.0F);  // bar along travel
  return buffer.to_tip(2.0);
}

[[nodiscard]] patchy::BrushTip make_stitches_tip() {
  constexpr std::int32_t kWidth = 48;
  constexpr std::int32_t kHeight = 128;
  CoverageBuffer buffer(kWidth, kHeight);
  stamp_capsule(buffer, 24.0F, 14.0F, 24.0F, 114.0F, 9.0F, 9.0F, 1.0F);  // tick across travel
  return buffer.to_tip(1.5);
}

[[nodiscard]] patchy::BrushTip make_chain_tip() {
  constexpr std::int32_t kWidth = 128;
  constexpr std::int32_t kHeight = 88;
  CoverageBuffer buffer(kWidth, kHeight);
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto fy = static_cast<float>(y) - 44.0F;
      // Elongated ring; overlapping stamps read as interlocked links.
      buffer.at(x, y) = sdf_coverage(std::abs(ellipse_sdf(fx, fy, 52.0F, 30.0F)) - 9.0F);
    }
  }
  return buffer.to_tip(0.85);
}

[[nodiscard]] patchy::BrushTip make_rope_tip() {
  // X-periodic band of diagonal strands; spacing 1.0 butts consecutive stamps into a
  // continuous rope (the strand family repeats exactly every 128px of travel).
  constexpr std::int32_t kWidth = 128;
  constexpr std::int32_t kHeight = 64;
  CoverageBuffer buffer(kWidth, kHeight);
  // Strands lean 36px right over the 64px height (a rope-like twist); four per tile.
  constexpr float kNormalX = 0.87157F;  // normal of the strand direction (36, -64)/|.|
  constexpr float kNormalY = 0.49026F;
  constexpr float kPitch = 128.0F * kNormalX / 4.0F;  // strand spacing along the normal
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      const auto s = static_cast<float>(x) * kNormalX + static_cast<float>(y) * kNormalY;
      const auto t = s - std::floor(s / kPitch) * kPitch;
      const auto line_distance = std::min(t, kPitch - t);
      const auto strand = sdf_coverage(line_distance - (kPitch - 7.0F) * 0.5F);
      // Round the band's top/bottom silhouette a touch.
      const auto band = sdf_coverage(std::abs(static_cast<float>(y) - 31.5F) - 31.0F);
      buffer.at(x, y) = strand * band;
    }
  }
  return buffer.to_tip(1.0);
}

[[nodiscard]] patchy::BrushTip make_arrow_tip() {
  constexpr std::int32_t kWidth = 128;
  constexpr std::int32_t kHeight = 96;
  CoverageBuffer buffer(kWidth, kHeight);
  stamp_capsule(buffer, 14.0F, 48.0F, 74.0F, 48.0F, 8.0F, 8.0F, 1.0F);  // shaft
  for (std::int32_t y = 10; y < 88; ++y) {
    for (std::int32_t x = 62; x < kWidth; ++x) {
      const auto coverage = sdf_coverage(triangle_sdf(static_cast<float>(x), static_cast<float>(y),
                                                      120.0F, 48.0F, 68.0F, 14.0F, 68.0F, 82.0F));
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage);
      }
    }
  }
  return buffer.to_tip(2.2);
}

[[nodiscard]] patchy::BrushTip make_brick_road_tip() {
  // Square running-bond tile: dab advance is brush_size * spacing and the scaled stamp's larger
  // dimension equals brush_size, so at spacing 1.0 consecutive stamps butt exactly. The joint
  // phases (16 / 48) keep every mortar joint away from the x = 0/128 edges: the tile boundary
  // cuts through full-coverage brick faces, so the butt seam blends invisibly. Brick shading is
  // hashed from the WRAPPED brick index so a brick spanning the seam keeps one value.
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 2107;
  constexpr float kPeriod = 64.0F;    // brick length; two bricks per course per tile
  constexpr float kHalfJoint = 3.0F;  // ~6px transparent mortar
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto course = y < 64 ? 0 : 1;
      const auto shift = course == 0 ? 16.0F : 48.0F;  // half-brick running bond, edge-safe
      const auto shifted = static_cast<float>(x) + shift;
      const auto phase = std::fmod(shifted, kPeriod);
      const auto vertical_joint = std::min(phase, kPeriod - phase);
      const auto horizontal_joint = std::abs(static_cast<float>(y) - 64.0F);
      const auto joint = std::min(vertical_joint, horizontal_joint);
      const auto coverage = std::clamp((joint - kHalfJoint) * 0.9F, 0.0F, 1.0F);
      if (coverage <= 0.0F) {
        continue;
      }
      const auto brick = static_cast<std::int32_t>(std::floor(shifted / kPeriod)) % 2;
      buffer.at(x, y) = coverage * (0.80F + 0.20F * hash01(brick, course, kSeed));
    }
  }
  return buffer.to_tip(1.0);
}

[[nodiscard]] patchy::BrushTip make_cobblestone_tip() {
  // 4x4 stones on a jittered grid. Stone identity hashes the WRAPPED column index and the edge
  // columns are also evaluated one tile over, so the pattern is X-periodic and spacing ~1 lays
  // a continuous stone road. The radius wobble is stone-local (angular harmonics), which keeps
  // the tile periodic where fbm over absolute x would not be.
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 2108;
  constexpr int kCells = 3;
  constexpr float kCell = 128.0F / 3.0F;
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      float best = 0.0F;
      for (int row = 0; row < kCells; ++row) {
        for (int column = -1; column <= kCells; ++column) {
          const auto wrapped = (column % kCells + kCells) % kCells;
          const auto jitter_x = (hash01(wrapped, row * 3, kSeed) - 0.5F) * 7.0F;
          const auto jitter_y = (hash01(wrapped, row * 3 + 1, kSeed) - 0.5F) * 7.0F;
          const auto cx = (static_cast<float>(column) + 0.5F) * kCell + jitter_x;
          const auto cy = (static_cast<float>(row) + 0.5F) * kCell + jitter_y;
          const auto dx = static_cast<float>(x) - cx;
          const auto dy = static_cast<float>(y) - cy;
          if (std::abs(dx) > 24.0F || std::abs(dy) > 24.0F) {
            continue;
          }
          const auto rx = 14.5F + hash01(wrapped, row * 3 + 2, kSeed) * 3.0F;
          const auto ry = 14.5F + hash01(wrapped + 17, row * 3 + 2, kSeed) * 3.0F;
          const auto theta = std::atan2(dy, dx);
          const auto phase = hash01(wrapped, row + 9, kSeed) * 2.0F * static_cast<float>(kPi);
          const auto wobble = 1.0F + 0.09F * std::sin(3.0F * theta + phase) +
                              0.05F * std::sin(5.0F * theta + 2.0F * phase);
          const auto sd = ellipse_sdf(dx, dy, rx * wobble, ry * wobble);
          const auto value = 0.84F + 0.15F * hash01(wrapped + 31, row, kSeed);
          best = std::max(best, sdf_coverage(sd) * value);
        }
      }
      buffer.at(x, y) = best;
    }
  }
  return buffer.to_tip(0.95);
}

// ---- Nature scatter stamps ----

[[nodiscard]] patchy::BrushTip make_leaf_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  // Vesica body: intersection of two discs, pointed tips on the X axis.
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 64.0F;
      const auto d1 = std::sqrt(fx * fx + (static_cast<float>(y) - 16.0F) * (static_cast<float>(y) - 16.0F));
      const auto d2 = std::sqrt(fx * fx + (static_cast<float>(y) - 112.0F) * (static_cast<float>(y) - 112.0F));
      buffer.at(x, y) = sdf_coverage(std::max(d1, d2) - 70.0F);
    }
  }
  stamp_capsule(buffer, 6.0F, 64.0F, 17.0F, 64.0F, 2.0F, 3.2F, 1.0F);  // stem
  carve_capsule(buffer, 20.0F, 64.0F, 104.0F, 64.0F, 2.2F, 1.2F, 0.80F);  // midrib
  for (const auto base_x : {40.0F, 62.0F, 84.0F}) {  // staggered side veins
    carve_capsule(buffer, base_x, 64.0F, base_x + 16.0F, 51.0F, 1.2F, 0.7F, 0.55F);
    carve_capsule(buffer, base_x + 9.0F, 64.0F, base_x + 25.0F, 77.0F, 1.2F, 0.7F, 0.55F);
  }
  return buffer.to_tip(1.2);
}

[[nodiscard]] patchy::BrushTip make_snowflake_tip() {
  constexpr std::int32_t kSize = 144;
  CoverageBuffer buffer(kSize, kSize);
  constexpr float kCx = 72.0F;
  constexpr float kCy = 72.0F;
  for (int arm = 0; arm < 6; ++arm) {
    const auto angle = static_cast<float>(arm) * static_cast<float>(kPi) / 3.0F;
    const auto ax = std::cos(angle);
    const auto ay = std::sin(angle);
    stamp_capsule(buffer, kCx, kCy, kCx + 62.0F * ax, kCy + 62.0F * ay, 5.0F, 2.0F, 1.0F);
    // Two pairs of branchlets per arm.
    constexpr float kBranchFractions[] = {0.55F, 0.80F};
    constexpr float kBranchLengths[] = {16.0F, 10.0F};
    for (int branch = 0; branch < 2; ++branch) {
      const auto bx = kCx + 62.0F * kBranchFractions[branch] * ax;
      const auto by = kCy + 62.0F * kBranchFractions[branch] * ay;
      for (const auto side : {-1.0F, 1.0F}) {
        const auto branch_angle = angle + side * 55.0F * static_cast<float>(kPi) / 180.0F;
        stamp_capsule(buffer, bx, by, bx + kBranchLengths[branch] * std::cos(branch_angle),
                      by + kBranchLengths[branch] * std::sin(branch_angle), 2.4F, 1.1F, 1.0F);
      }
    }
  }
  stamp_dot(buffer, kCx, kCy, 8.0F, 1.0F, 0.25F);
  return buffer.to_tip(2.0);
}

[[nodiscard]] patchy::BrushTip make_rain_tip() {
  // One streak leaning ~12 degrees off vertical, thin head to fat tail; the curated dynamics
  // scatter parallel copies (no angle jitter, so a whole stroke reads as one shower).
  constexpr std::int32_t kWidth = 64;
  constexpr std::int32_t kHeight = 128;
  CoverageBuffer buffer(kWidth, kHeight);
  stamp_capsule(buffer, 44.0F, 8.0F, 20.0F, 120.0F, 2.0F, 4.5F, 1.0F);
  return buffer.to_tip(1.5);
}

[[nodiscard]] patchy::BrushTip make_bubbles_tip() {
  constexpr std::int32_t kSize = 112;
  CoverageBuffer buffer(kSize, kSize);
  for (std::int32_t y = 0; y < kSize; ++y) {
    for (std::int32_t x = 0; x < kSize; ++x) {
      const auto fx = static_cast<float>(x) - 56.0F;
      const auto fy = static_cast<float>(y) - 56.0F;
      buffer.at(x, y) = sdf_coverage(std::abs(std::sqrt(fx * fx + fy * fy) - 40.0F) - 7.0F);
    }
  }
  carve_dot(buffer, 85.0F, 27.0F, 14.0F, 1.0F);  // upper-right highlight gap
  return buffer.to_tip(1.4);
}

[[nodiscard]] patchy::BrushTip make_flower_tip() {
  constexpr std::int32_t kSize = 144;
  CoverageBuffer buffer(kSize, kSize);
  for (int petal = 0; petal < 5; ++petal) {
    const auto angle = (static_cast<float>(petal) * 72.0F - 90.0F) * static_cast<float>(kPi) / 180.0F;
    const auto ax = std::cos(angle);
    const auto ay = std::sin(angle);
    const auto cx = 72.0F + 38.0F * ax;
    const auto cy = 72.0F + 38.0F * ay;
    for (std::int32_t y = 0; y < kSize; ++y) {
      for (std::int32_t x = 0; x < kSize; ++x) {
        const auto dx = static_cast<float>(x) - cx;
        const auto dy = static_cast<float>(y) - cy;
        // Rotate into the petal frame: radial axis long, tangential axis short.
        const auto radial = dx * ax + dy * ay;
        const auto tangential = -dx * ay + dy * ax;
        const auto coverage = sdf_coverage(ellipse_sdf(radial, tangential, 34.0F, 20.0F));
        if (coverage > 0.0F) {
          buffer.add(x, y, coverage);
        }
      }
    }
  }
  carve_dot(buffer, 72.0F, 72.0F, 14.0F, 1.0F);  // hollow center
  return buffer.to_tip(1.7);
}

// ---- Fun / design stamps ----

[[nodiscard]] patchy::BrushTip make_sparkle_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr float kC = 64.0F;
  for (const auto direction : {-1.0F, 1.0F}) {
    stamp_capsule(buffer, kC, kC, kC + direction * 54.0F, kC, 7.0F, 0.8F, 1.0F);
    stamp_capsule(buffer, kC, kC, kC, kC + direction * 54.0F, 7.0F, 0.8F, 1.0F);
  }
  for (const auto dx : {-1.0F, 1.0F}) {  // short diagonal glints
    for (const auto dy : {-1.0F, 1.0F}) {
      stamp_capsule(buffer, kC, kC, kC + dx * 19.0F, kC + dy * 19.0F, 3.0F, 0.6F, 0.9F);
    }
  }
  stamp_dot(buffer, kC, kC, 12.0F, 0.6F, 0.8F);  // soft core glow
  return buffer.to_tip(1.8);
}

[[nodiscard]] patchy::BrushTip make_heart_tip() {
  constexpr std::int32_t kWidth = 128;
  constexpr std::int32_t kHeight = 120;
  CoverageBuffer buffer(kWidth, kHeight);
  // Classic implicit heart (x^2 + y^2 - 1)^3 <= x^2 * y^3 (y up), scaled to ~105px and flipped
  // into bitmap space so the lobes sit at the top and the point at the bottom.
  stamp_filled_region(buffer, [](float px, float py) {
    const auto x = (px - 64.0F) / 46.0F;
    const auto y = (50.0F - py) / 46.0F + 0.25F;
    const auto a = x * x + y * y - 1.0F;
    return a * a * a - x * x * y * y * y <= 0.0F;
  });
  return buffer.to_tip(1.5);
}

[[nodiscard]] patchy::BrushTip make_confetti_tip() {
  constexpr std::int32_t kSize = 64;
  CoverageBuffer buffer(kSize, kSize);
  stamp_rounded_rect(buffer, 32.0F, 32.0F, 18.0F, 18.0F, 5.0F, 1.0F);
  return buffer.to_tip(1.0);
}

void stamp_paw(CoverageBuffer& buffer, float cx, float cy) {
  stamp_ellipse(buffer, cx, cy, 12.0F, 13.5F, 1.0F);  // main pad
  for (const auto toe_angle : {-54.0F, -18.0F, 18.0F, 54.0F}) {
    const auto radians = toe_angle * static_cast<float>(kPi) / 180.0F;
    const auto inner = std::abs(toe_angle) < 30.0F;  // inner toes sit a touch farther out
    const auto distance = inner ? 21.0F : 19.0F;
    stamp_dot(buffer, cx + distance * std::cos(radians), cy + distance * std::sin(radians),
              inner ? 5.5F : 5.0F, 1.0F, 0.2F);
  }
}

[[nodiscard]] patchy::BrushTip make_paw_prints_tip() {
  // A trotting pair baked into one stamp (the dynamics engine has no deterministic left/right
  // alternation); with the Direction control the toes lead along the stroke.
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  stamp_paw(buffer, 36.0F, 86.0F);
  stamp_paw(buffer, 92.0F, 42.0F);
  return buffer.to_tip(2.0);
}

void stamp_shoe_print(CoverageBuffer& buffer, float cx, float cy) {
  stamp_ellipse(buffer, cx + 8.0F, cy, 17.0F, 10.0F, 1.0F);              // sole, toe toward +X
  stamp_rounded_rect(buffer, cx - 19.0F, cy, 6.0F, 7.5F, 4.0F, 1.0F);   // heel behind a 4px gap
}

[[nodiscard]] patchy::BrushTip make_footprints_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  stamp_shoe_print(buffer, 44.0F, 40.0F);   // left foot
  stamp_shoe_print(buffer, 86.0F, 88.0F);   // right foot, half a stride ahead
  return buffer.to_tip(2.2);
}

[[nodiscard]] patchy::BrushTip make_crosshatch_tip() {
  constexpr std::int32_t kSize = 128;
  CoverageBuffer buffer(kSize, kSize);
  constexpr std::uint32_t kSeed = 2101;
  constexpr float kDiag = 0.70711F;
  for (int line = 0; line < 5; ++line) {
    const auto offset = (static_cast<float>(line) - 2.0F) * 22.0F;
    const auto cx = 64.0F + offset * kDiag;
    const auto cy = 64.0F - offset * kDiag;
    const auto head = 42.0F + (hash01(line, 0, kSeed) - 0.5F) * 12.0F;
    const auto tail = 42.0F + (hash01(line, 1, kSeed) - 0.5F) * 12.0F;
    stamp_capsule(buffer, cx - head * kDiag, cy - head * kDiag, cx + tail * kDiag,
                  cy + tail * kDiag, 3.5F, 3.5F, 0.9F);
  }
  return buffer.to_tip(0.5);
}

[[nodiscard]] patchy::BrushTip make_rtsoft_logo_tip() {
  // The RTsoft mark, matched to the official 1-bit reduction (RTsoft_Logo_512_512_2tone.png):
  // a SOLID mega-R silhouette — full-height stem slab, a huge bowl disc whose top meets the
  // R's top edge and whose bottom point forms the waist notch, and a straight leg wedge from
  // the waist to the bottom-right corner — plus the small bar floating above, the full-width
  // bar floating below, and the short dash etched inside the bowl (the 1-bit mark keeps only
  // the horizontal stroke of the color logo's "+"). Reference coordinates are 512-space,
  // mapped here via (v - origin) * 0.4.
  constexpr std::int32_t kWidth = 120;
  constexpr std::int32_t kHeight = 198;
  CoverageBuffer buffer(kWidth, kHeight);
  stamp_rounded_rect(buffer, 58.0F, 3.2F, 10.8F, 2.4F, 1.0F, 1.0F);    // small bar above
  stamp_rounded_rect(buffer, 36.0F, 95.2F, 34.0F, 86.4F, 1.0F, 1.0F);  // stem slab
  stamp_dot(buffer, 70.0F, 55.6F, 46.8F, 1.0F, 0.0F);                  // bowl disc
  for (std::int32_t y = 100; y <= 184; ++y) {                          // leg wedge to the corner
    for (std::int32_t x = 40; x < kWidth; ++x) {
      const auto coverage =
          sdf_coverage(triangle_sdf(static_cast<float>(x), static_cast<float>(y), 70.0F, 102.4F,
                                    112.8F, 181.6F, 52.0F, 181.6F));
      if (coverage > 0.0F) {
        buffer.add(x, y, coverage);
      }
    }
  }
  stamp_rounded_rect(buffer, 58.4F, 192.4F, 57.6F, 2.8F, 1.0F, 1.0F);   // full-width bar below
  carve_capsule(buffer, 48.8F, 51.4F, 68.4F, 51.4F, 2.0F, 2.0F, 1.0F);  // etched dash
  return buffer.to_tip(1.3);
}

}  // namespace

QString default_brush_tips_folder_name() {
  return QObject::tr("Patchy Defaults");
}

std::vector<DefaultBrushTipSpec> generate_default_brush_tips() {
  using patchy::BrushDynamicControl;
  // Curated dynamics, tuned per tip: media tips get subtle per-dab variation so dense strokes
  // do not band; particle tips (spray/spatter/stipple/star) scatter like their Photoshop
  // counterparts; bristle and grass follow the stroke direction (the bristle dot-row must stay
  // perpendicular to travel to streak in any direction). Canvas keeps its weave aligned, Square
  // stays stable for hard-edge work, and Calligraphy's 45° nib is baked into the bitmap.
  // The v3 path stamps (dashes, chain, bricks, footprints, ...) use the Direction control with
  // their art authored bitmap +X = direction of travel; Dotted Line and RTsoft Logo ship
  // deliberately plain so they stamp clean.
  std::vector<DefaultBrushTipSpec> specs;
  specs.push_back({QObject::tr("Chalk"), 0.08, make_chalk_tip(),
                   {.angle_jitter = 0.08, .opacity_jitter = 0.10}});
  specs.push_back({QObject::tr("Charcoal"), 0.06, make_charcoal_tip(),
                   {.angle_jitter = 0.06, .opacity_jitter = 0.15}});
  specs.push_back({QObject::tr("Pastel"), 0.08, make_pastel_tip(),
                   {.angle_jitter = 0.10, .opacity_jitter = 0.10}});
  specs.push_back({QObject::tr("Bristle"), 0.03, make_bristle_tip(),
                   {.angle_control = BrushDynamicControl::Direction}});
  specs.push_back({QObject::tr("Sponge"), 0.15, make_sponge_tip(),
                   {.size_jitter = 0.15, .angle_jitter = 1.0, .opacity_jitter = 0.15}});
  specs.push_back({QObject::tr("Canvas"), 0.10, make_canvas_tip()});
  specs.push_back({QObject::tr("Smoke"), 0.12, make_smoke_tip(),
                   {.size_jitter = 0.25, .angle_jitter = 1.0, .scatter = 0.35, .opacity_jitter = 0.35}});
  specs.push_back({QObject::tr("Spray"), 0.05, make_spray_tip(),
                   {.size_jitter = 0.50,
                    .angle_jitter = 1.0,
                    .scatter = 0.50,
                    .scatter_both_axes = true,
                    .opacity_jitter = 0.30}});
  specs.push_back({QObject::tr("Spatter"), 0.40, make_spatter_tip(),
                   {.size_jitter = 0.50,
                    .angle_jitter = 1.0,
                    .scatter = 1.50,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.25}});
  specs.push_back({QObject::tr("Stipple"), 0.60, make_stipple_tip(),
                   {.size_jitter = 0.35,
                    .angle_jitter = 1.0,
                    .scatter = 1.20,
                    .scatter_both_axes = true,
                    .count = 3,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.30}});
  specs.push_back({QObject::tr("Ink Splat"), 1.50, make_ink_splat_tip(),
                   {.size_jitter = 0.60, .angle_jitter = 1.0, .scatter = 0.80, .opacity_jitter = 0.20}});
  specs.push_back({QObject::tr("Grunge"), 0.80, make_grunge_tip(),
                   {.size_jitter = 0.25, .angle_jitter = 1.0, .opacity_jitter = 0.25}});
  specs.push_back({QObject::tr("Square"), 0.05, make_square_tip()});
  specs.push_back({QObject::tr("Calligraphy"), 0.04, make_calligraphy_tip()});
  specs.push_back({QObject::tr("Star"), 1.30, make_star_tip(),
                   {.size_jitter = 0.60,
                    .angle_jitter = 1.0,
                    .scatter = 2.00,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.15}});
  specs.push_back({QObject::tr("Grass"), 0.60, make_grass_tip(),
                   {.size_jitter = 0.40,
                    .angle_jitter = 0.06,
                    .angle_control = BrushDynamicControl::Direction,
                    .scatter = 0.50,
                    .count = 2,
                    .count_jitter = 0.50}});

  // ---- v3 additions (since_version 3): path stamps, scatter stamps, and the logo ----
  specs.push_back({QObject::tr("Dotted Line"), 2.5, make_dotted_line_tip(), {}, 3});
  specs.push_back({QObject::tr("Dashed Line"), 2.0, make_dashed_line_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Stitches"), 1.5, make_stitches_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Chain"), 0.85, make_chain_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Rope"), 1.0, make_rope_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Arrow"), 2.2, make_arrow_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Brick Road"), 1.0, make_brick_road_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Cobblestone"), 0.95, make_cobblestone_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Leaf"), 1.2, make_leaf_tip(),
                   {.size_jitter = 0.40,
                    .minimum_diameter = 0.30,
                    .angle_jitter = 1.0,
                    .flip_x_jitter = true,
                    .scatter = 1.50,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.15},
                   3});
  specs.push_back({QObject::tr("Snowflake"), 2.0, make_snowflake_tip(),
                   {.size_jitter = 0.60,
                    .minimum_diameter = 0.20,
                    .angle_jitter = 1.0,
                    .scatter = 3.00,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.30},
                   3});
  specs.push_back({QObject::tr("Rain"), 1.5, make_rain_tip(),
                   {.size_jitter = 0.40,
                    .minimum_diameter = 0.40,
                    .scatter = 2.50,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.40},
                   3});
  specs.push_back({QObject::tr("Bubbles"), 1.4, make_bubbles_tip(),
                   {.size_jitter = 0.60,
                    .minimum_diameter = 0.15,
                    .scatter = 2.50,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.25},
                   3});
  specs.push_back({QObject::tr("Flower"), 1.7, make_flower_tip(),
                   {.size_jitter = 0.50,
                    .minimum_diameter = 0.25,
                    .angle_jitter = 1.0,
                    .scatter = 2.00,
                    .scatter_both_axes = true,
                    .opacity_jitter = 0.15},
                   3});
  specs.push_back({QObject::tr("Sparkle"), 1.8, make_sparkle_tip(),
                   {.size_jitter = 0.70,
                    .minimum_diameter = 0.10,
                    .angle_jitter = 0.05,
                    .scatter = 2.00,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.40},
                   3});
  specs.push_back({QObject::tr("Heart"), 1.5, make_heart_tip(),
                   {.size_jitter = 0.40,
                    .minimum_diameter = 0.30,
                    .angle_jitter = 0.08,
                    .scatter = 1.50,
                    .scatter_both_axes = true,
                    .opacity_jitter = 0.10},
                   3});
  specs.push_back({QObject::tr("Confetti"), 1.0, make_confetti_tip(),
                   {.size_jitter = 0.50,
                    .minimum_diameter = 0.20,
                    .angle_jitter = 1.0,
                    .scatter = 4.00,
                    .scatter_both_axes = true,
                    .count = 3,
                    .count_jitter = 0.50,
                    .opacity_jitter = 0.30},
                   3});
  specs.push_back({QObject::tr("Paw Prints"), 2.0, make_paw_prints_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Footprints"), 2.2, make_footprints_tip(),
                   {.angle_control = BrushDynamicControl::Direction}, 3});
  specs.push_back({QObject::tr("Crosshatch"), 0.5, make_crosshatch_tip(),
                   {.size_jitter = 0.15,
                    .angle_jitter = 0.03,
                    .flip_x_jitter = true,
                    .flip_y_jitter = true,
                    .opacity_jitter = 0.20},
                   3});
  specs.push_back({QObject::tr("RTsoft Logo"), 1.3, make_rtsoft_logo_tip(), {}, 3});

  // ---- v4 additions (since_version 4): presets demonstrating the expanded brush engine ----
  specs.push_back({QObject::tr("Textured Chalk"), 0.08, make_chalk_tip(),
                   {.angle_jitter = 0.08,
                    .opacity_jitter = 0.10,
                    .texture_enabled = true,
                    .texture_style = patchy::BrushTextureStyle::FineGrain,
                    .texture_scale = 0.65,
                    .texture_depth = 0.55},
                   4});
  specs.push_back({QObject::tr("Dual Brush Dots"), 0.35, make_square_tip(),
                   {.dual_brush_enabled = true,
                    .dual_brush_size = 0.18,
                    .dual_brush_hardness = 0.90,
                    .dual_brush_spacing = 2.0},
                   4});
  specs.push_back({QObject::tr("Color Scatter"), 0.75, make_spatter_tip(),
                   {.size_jitter = 0.35,
                    .angle_jitter = 1.0,
                    .scatter = 1.25,
                    .scatter_both_axes = true,
                    .count = 2,
                    .count_jitter = 0.35,
                    .color_dynamics_enabled = true,
                    .foreground_background_jitter = 0.70,
                    .hue_jitter = 0.08,
                    .saturation_jitter = 0.12},
                   4});
  specs.push_back({QObject::tr("Wet Edge Wash"), 0.12, make_smoke_tip(),
                   {.opacity_jitter = 0.10, .wet_edges = true}, 4});

  // The tips carry their own spacing too; keep both in sync for callers using either.
  for (auto& spec : specs) {
    spec.tip.default_spacing = spec.spacing;
  }
  return specs;
}

}  // namespace patchy::ui
