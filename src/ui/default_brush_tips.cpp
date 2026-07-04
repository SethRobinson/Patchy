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
  // The tips carry their own spacing too; keep both in sync for callers using either.
  for (auto& spec : specs) {
    spec.tip.default_spacing = spec.spacing;
  }
  return specs;
}

}  // namespace patchy::ui
