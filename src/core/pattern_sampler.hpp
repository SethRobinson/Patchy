#pragma once

#include "core/blend_math.hpp"
#include "core/layer.hpp"
#include "core/pattern_resource.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Pattern tile sampling shared by the layer-style renderer (pattern overlays,
// bevel texture) and the vector shape rasterizer (pattern fills/strokes).
// Pure move from render/layer_compositor.hpp (July 2026): the sampling rules
// were calibrated against Photoshop and must not fork - see the class comment.
namespace patchy {

struct PatternSampleRgba {
  RgbColor color{};
  float alpha{0.0F};
};

// Wrap-tiled pattern sampler in document space. Anchoring follows the Photoshop
// probes: "Link with Layer" anchors the grid at the layer's fxrp effects
// reference point (updated by PS when a layer moves), unlinked at the document
// origin; the descriptor phase adds on top in both cases. Filtering was fitted
// against PS 2026 scale probes: 100% tiles nearest (byte-crisp), magnification
// interpolates linearly between texels on the INTEGER grid, and minification
// box-averages the source footprint each output pixel covers.
class PatternTileSampler {
public:
  PatternTileSampler(const PixelBuffer& tile, const Layer& layer, float scale, float angle_degrees,
                     bool link_with_layer, float phase_x, float phase_y)
      : tile_(&tile) {
    double anchor_x = phase_x;
    double anchor_y = phase_y;
    if (link_with_layer) {
      const auto reference = layer_effects_reference_point(layer);
      anchor_x += reference[0];
      anchor_y += reference[1];
    }
    anchor_x_ = anchor_x;
    anchor_y_ = anchor_y;
    const auto clamped_scale = std::max(0.01, static_cast<double>(scale));
    inverse_scale_ = 1.0 / clamped_scale;
    constexpr double kPi = 3.14159265358979323846;
    const auto radians = static_cast<double>(angle_degrees) * kPi / 180.0;
    rotated_ = std::abs(radians) > 1e-9;
    cosine_ = std::cos(radians);
    sine_ = std::sin(radians);
    nearest_ = !rotated_ && std::abs(clamped_scale - 1.0) < 1e-6;
    // Rotated minification falls back to the linear tap (rare; box footprints
    // do not stay axis-aligned under rotation).
    box_filter_ = !nearest_ && !rotated_ && clamped_scale < 1.0;
  }

  [[nodiscard]] PatternSampleRgba sample(std::int32_t x, std::int32_t y) const noexcept {
    const auto width = tile_->width();
    const auto height = tile_->height();
    if (box_filter_) {
      // Output pixel [x, x+1) covers source [(x - a) / s, (x + 1 - a) / s):
      // average texels with their covered fractions (PS's minification fit).
      const auto u0 = (static_cast<double>(x) - anchor_x_) * inverse_scale_;
      const auto u1 = (static_cast<double>(x) + 1.0 - anchor_x_) * inverse_scale_;
      const auto v0 = (static_cast<double>(y) - anchor_y_) * inverse_scale_;
      const auto v1 = (static_cast<double>(y) + 1.0 - anchor_y_) * inverse_scale_;
      double sum_r = 0.0;
      double sum_g = 0.0;
      double sum_b = 0.0;
      double sum_a = 0.0;
      double total = 0.0;
      const auto first_x = static_cast<std::int64_t>(std::floor(u0));
      const auto last_x = static_cast<std::int64_t>(std::ceil(u1)) - 1;
      const auto first_y = static_cast<std::int64_t>(std::floor(v0));
      const auto last_y = static_cast<std::int64_t>(std::ceil(v1)) - 1;
      for (auto ty = first_y; ty <= last_y; ++ty) {
        const auto cover_y = std::min(v1, static_cast<double>(ty) + 1.0) - std::max(v0, static_cast<double>(ty));
        if (cover_y <= 0.0) {
          continue;
        }
        const auto row = wrap_index(ty, height);
        for (auto tx = first_x; tx <= last_x; ++tx) {
          const auto cover_x =
              std::min(u1, static_cast<double>(tx) + 1.0) - std::max(u0, static_cast<double>(tx));
          if (cover_x <= 0.0) {
            continue;
          }
          const auto weight = cover_x * cover_y;
          const auto value = texel(wrap_index(tx, width), row);
          sum_r += weight * value.color.red;
          sum_g += weight * value.color.green;
          sum_b += weight * value.color.blue;
          sum_a += weight * value.alpha;
          total += weight;
        }
      }
      PatternSampleRgba result;
      if (total <= 0.0) {
        return result;
      }
      result.color.red = static_cast<std::uint8_t>(std::clamp(std::lround(sum_r / total), 0L, 255L));
      result.color.green = static_cast<std::uint8_t>(std::clamp(std::lround(sum_g / total), 0L, 255L));
      result.color.blue = static_cast<std::uint8_t>(std::clamp(std::lround(sum_b / total), 0L, 255L));
      result.alpha = clamp_unit(static_cast<float>(sum_a / total));
      return result;
    }

    if (nearest_) {
      // 100%: the tile grid lands on pixel cells directly (P1/P2b byte-exact).
      const auto ix = wrap_index(
          static_cast<std::int64_t>(std::floor(static_cast<double>(x) + 0.5 - anchor_x_)), width);
      const auto iy = wrap_index(
          static_cast<std::int64_t>(std::floor(static_cast<double>(y) + 0.5 - anchor_y_)), height);
      return texel(ix, iy);
    }
    // Magnification sample positions fitted against the PS scale probe:
    // src = (x - anchor) / scale, texel grid at integer coordinates.
    auto u = static_cast<double>(x) - anchor_x_;
    auto v = static_cast<double>(y) - anchor_y_;
    if (rotated_) {
      const auto ru = u * cosine_ + v * sine_;  // inverse rotation into tile space
      const auto rv = -u * sine_ + v * cosine_;
      u = ru;
      v = rv;
    }
    u *= inverse_scale_;
    v *= inverse_scale_;
    // Magnification: linear taps between texels on the integer grid (PS fit).
    const auto floor_u = std::floor(u);
    const auto floor_v = std::floor(v);
    const auto fraction_x = static_cast<float>(u - floor_u);
    const auto fraction_y = static_cast<float>(v - floor_v);
    const auto x0 = wrap_index(static_cast<std::int64_t>(floor_u), width);
    const auto y0 = wrap_index(static_cast<std::int64_t>(floor_v), height);
    const auto x1 = x0 + 1 == width ? 0 : x0 + 1;
    const auto y1 = y0 + 1 == height ? 0 : y0 + 1;
    const auto top_left = texel(x0, y0);
    const auto top_right = texel(x1, y0);
    const auto bottom_left = texel(x0, y1);
    const auto bottom_right = texel(x1, y1);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const auto blend_channel = [&](float tl, float tr, float bl, float br) {
      return lerp(lerp(tl, tr, fraction_x), lerp(bl, br, fraction_x), fraction_y);
    };
    PatternSampleRgba result;
    result.color.red = static_cast<std::uint8_t>(std::clamp(
        std::lround(blend_channel(top_left.color.red, top_right.color.red, bottom_left.color.red,
                                  bottom_right.color.red)),
        0L, 255L));
    result.color.green = static_cast<std::uint8_t>(std::clamp(
        std::lround(blend_channel(top_left.color.green, top_right.color.green, bottom_left.color.green,
                                  bottom_right.color.green)),
        0L, 255L));
    result.color.blue = static_cast<std::uint8_t>(std::clamp(
        std::lround(blend_channel(top_left.color.blue, top_right.color.blue, bottom_left.color.blue,
                                  bottom_right.color.blue)),
        0L, 255L));
    result.alpha = clamp_unit(blend_channel(top_left.alpha, top_right.alpha, bottom_left.alpha,
                                            bottom_right.alpha));
    return result;
  }

  // Blend-If-calibrated integer luminance weights (299/590/111), 0..1.
  [[nodiscard]] float sample_luminance(std::int32_t x, std::int32_t y) const noexcept {
    const auto value = sample(x, y);
    const auto weighted = 299L * value.color.red + 590L * value.color.green + 111L * value.color.blue;
    return static_cast<float>(weighted) / 255000.0F;
  }

private:
  [[nodiscard]] static std::int32_t wrap_index(std::int64_t value, std::int32_t extent) noexcept {
    const auto wrapped = value % extent;
    return static_cast<std::int32_t>(wrapped < 0 ? wrapped + extent : wrapped);
  }

  [[nodiscard]] PatternSampleRgba texel(std::int32_t x, std::int32_t y) const noexcept {
    const auto* px = tile_->pixel(x, y);
    PatternSampleRgba result;
    result.color = RgbColor{px[0], px[1], px[2]};
    result.alpha = tile_->format().channels >= 4 ? static_cast<float>(px[3]) / 255.0F : 1.0F;
    return result;
  }

  const PixelBuffer* tile_{nullptr};
  double anchor_x_{0.0};
  double anchor_y_{0.0};
  double inverse_scale_{1.0};
  double cosine_{1.0};
  double sine_{0.0};
  bool rotated_{false};
  bool nearest_{true};
  bool box_filter_{false};
};

}  // namespace patchy
