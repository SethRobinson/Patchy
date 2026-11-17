#pragma once

#include "core/blend_math.hpp"
#include "core/layer.hpp"
#include "core/pattern_resource.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// Pattern tile sampling shared by the layer-style renderer (pattern overlays,
// bevel texture) and the vector shape rasterizer (pattern fills/strokes).
// Pure move from render/layer_compositor.hpp (July 2026): the sampling rules
// were calibrated against Photoshop and must not fork - see the class comment.
namespace patchy {

struct PatternSampleRgba {
  RgbColor color{};
  float alpha{0.0F};
};

// Exact per-channel texel sums of a tile over any rectangle of the endlessly
// tiled plane (a summed-area table). Minification box-averages every texel an
// output pixel covers, so at 1% Scale one sample reads 100x100 texels: with a
// photo-sized tile a Bevel Texture preview took seconds (desk card local-060).
// The table answers any rectangle with a few lookups instead. Sums are kept
// modulo 2^32: a rectangle's true sum is below 2^32 whenever it covers fewer
// than 2^32 / 255 texels, which every footprint the sampler asks for does
// (Scale >= 1% is at most 101x101 texels), so the differences come out exact
// for tiles of any size.
class PatternTileSums {
public:
  explicit PatternTileSums(const PixelBuffer& tile);

  // 3 when every texel is opaque (alpha is then exactly 1), else 4.
  [[nodiscard]] int channels() const noexcept { return channels_; }

  // S(x, y) per channel, modulo 2^32: the sum over tiled texels [0, x) x [0, y),
  // extended to negative coordinates so that S(x1,y1) - S(x0,y1) - S(x1,y0) +
  // S(x0,y0) is the sum over [x0, x1) x [y0, y1) for any integers.
  void prefix(std::int64_t x, std::int64_t y, std::array<std::uint32_t, 4>& sums) const noexcept;

private:
  [[nodiscard]] const std::uint32_t* entry(std::int32_t x, std::int32_t y) const noexcept {
    return table_.data() +
           (static_cast<std::size_t>(y) * (static_cast<std::size_t>(width_) + 1U) + static_cast<std::size_t>(x)) *
               static_cast<std::size_t>(channels_);
  }

  std::int32_t width_{0};
  std::int32_t height_{0};
  int channels_{3};
  std::vector<std::uint32_t> table_;  // (width+1) x (height+1) x channels
};

// The tile's sums, built once and shared while the tile's pixels stay the same
// (a small cache keyed on the tile's shared storage: the preview renders of a
// slider drag and the strips of one render reuse one table).
[[nodiscard]] std::shared_ptr<const PatternTileSums> shared_pattern_tile_sums(const PixelBuffer& tile);

// Wrap-tiled pattern sampler in document space. Anchoring follows the Photoshop
// probes: "Link with Layer" anchors the grid at the layer's fxrp effects
// reference point (updated by PS when a layer moves), unlinked at the document
// origin; the descriptor phase adds on top in both cases. Filtering was fitted
// against PS 2026 scale probes: 100% tiles nearest (byte-crisp), magnification
// interpolates linearly between texels on the INTEGER grid, and minification
// box-averages the source footprint each output pixel covers. Rotation was
// pinned by the July 2026 full-canvas probes (probe-pat-rot30 /
// pat-rot30-phase / pat-order-a/b, 100% cell-classification fits): PS maps
// document to tile space as R(angle) @ (p - anchor) / scale with
// R = [[cos, -sin], [sin, cos]] - the pattern APPEARS rotated
// counterclockwise for a positive angle, matching the dial - and the phase
// subtracts in DOCUMENT space before the rotation, unscaled.
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
    // Footprints wider than kTileSumsMinFootprint texels read the summed-area
    // table; smaller ones stay on the texel loop, which is as fast there.
    if (box_filter_ && inverse_scale_ > kTileSumsMinFootprint) {
      sums_ = shared_pattern_tile_sums(tile);
    }
  }

  // Minification footprints (texels per output pixel per axis) above this use
  // the summed-area table.
  static constexpr double kTileSumsMinFootprint = 4.0;

  [[nodiscard]] bool uses_tile_sums() const noexcept { return sums_ != nullptr; }

  [[nodiscard]] PatternSampleRgba sample(std::int32_t x, std::int32_t y) const noexcept {
    if (box_filter_) {
      return sums_ != nullptr ? sample_box_from_sums(x, y) : sample_box_direct(x, y);
    }
    return sample_point(x, y);
  }

  // Blend-If-calibrated integer luminance weights (299/590/111), 0..1.
  [[nodiscard]] float sample_luminance(std::int32_t x, std::int32_t y) const noexcept {
    const auto value = sample(x, y);
    const auto weighted = 299L * value.color.red + 590L * value.color.green + 111L * value.color.blue;
    return static_cast<float>(weighted) / 255000.0F;
  }

  // The texel-by-texel box average (the calibrated minification reference);
  // the summed-area path must give the same bytes. Public for tests.
  [[nodiscard]] PatternSampleRgba sample_box_direct(std::int32_t x, std::int32_t y) const noexcept {
    const auto width = tile_->width();
    const auto height = tile_->height();
    {
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
  }

private:
  // The same weighted average as sample_box_direct, from the summed-area
  // table: the footprint splits into a 3x3 grid of cells (partial first
  // column/row, whole texels, partial last column/row) whose exact integer
  // sums come from 16 prefix lookups, weighted by the same cover fractions.
  [[nodiscard]] PatternSampleRgba sample_box_from_sums(std::int32_t x, std::int32_t y) const noexcept {
    const auto u0 = (static_cast<double>(x) - anchor_x_) * inverse_scale_;
    const auto u1 = (static_cast<double>(x) + 1.0 - anchor_x_) * inverse_scale_;
    const auto v0 = (static_cast<double>(y) - anchor_y_) * inverse_scale_;
    const auto v1 = (static_cast<double>(y) + 1.0 - anchor_y_) * inverse_scale_;
    const auto first_x = static_cast<std::int64_t>(std::floor(u0));
    const auto last_x = static_cast<std::int64_t>(std::ceil(u1)) - 1;
    const auto first_y = static_cast<std::int64_t>(std::floor(v0));
    const auto last_y = static_cast<std::int64_t>(std::ceil(v1)) - 1;
    const std::array<double, 3> weight_x{
        std::min(u1, static_cast<double>(first_x) + 1.0) - std::max(u0, static_cast<double>(first_x)), 1.0,
        std::min(u1, static_cast<double>(last_x) + 1.0) - std::max(u0, static_cast<double>(last_x))};
    const std::array<double, 3> weight_y{
        std::min(v1, static_cast<double>(first_y) + 1.0) - std::max(v0, static_cast<double>(first_y)), 1.0,
        std::min(v1, static_cast<double>(last_y) + 1.0) - std::max(v0, static_cast<double>(last_y))};
    const std::array<std::int64_t, 4> edges_x{first_x, first_x + 1, last_x, last_x + 1};
    const std::array<std::int64_t, 4> edges_y{first_y, first_y + 1, last_y, last_y + 1};
    std::array<std::array<std::array<std::uint32_t, 4>, 4>, 4> grid{};
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        sums_->prefix(edges_x[column], edges_y[row], grid[row][column]);
      }
    }
    const auto channels = static_cast<std::size_t>(sums_->channels());
    std::array<double, 4> sum{};
    for (std::size_t cell_y = 0; cell_y < 3; ++cell_y) {
      std::array<double, 4> row_sum{};
      for (std::size_t cell_x = 0; cell_x < 3; ++cell_x) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
          // Unsigned wraparound: the cell's true sum is below 2^32.
          const std::uint32_t cell = grid[cell_y + 1][cell_x + 1][channel] - grid[cell_y][cell_x + 1][channel] -
                                     grid[cell_y + 1][cell_x][channel] + grid[cell_y][cell_x][channel];
          row_sum[channel] += weight_x[cell_x] * static_cast<double>(cell);
        }
      }
      for (std::size_t channel = 0; channel < channels; ++channel) {
        sum[channel] += weight_y[cell_y] * row_sum[channel];
      }
    }
    const auto total = (weight_x[0] + static_cast<double>(last_x - first_x - 1) + weight_x[2]) *
                       (weight_y[0] + static_cast<double>(last_y - first_y - 1) + weight_y[2]);
    PatternSampleRgba result;
    if (total <= 0.0) {
      return result;
    }
    result.color.red = static_cast<std::uint8_t>(std::clamp(std::lround(sum[0] / total), 0L, 255L));
    result.color.green = static_cast<std::uint8_t>(std::clamp(std::lround(sum[1] / total), 0L, 255L));
    result.color.blue = static_cast<std::uint8_t>(std::clamp(std::lround(sum[2] / total), 0L, 255L));
    result.alpha = channels >= 4 ? clamp_unit(static_cast<float>(sum[3] / 255.0 / total)) : 1.0F;
    return result;
  }

  [[nodiscard]] PatternSampleRgba sample_point(std::int32_t x, std::int32_t y) const noexcept {
    const auto width = tile_->width();
    const auto height = tile_->height();
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
      // R(angle) @ (p - anchor): the PS-probed document-to-tile mapping.
      const auto ru = u * cosine_ - v * sine_;
      const auto rv = u * sine_ + v * cosine_;
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
  std::shared_ptr<const PatternTileSums> sums_{};
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
