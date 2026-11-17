#include "core/color_match_stroke.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace patchy {

namespace {

// The anti-alias fringe: a match falls to 0 this much past the tolerance distance.
constexpr float kFringeFactor = 1.25F;

[[nodiscard]] float squared_distance(const ColorMatchRgb& a, const ColorMatchRgb& b) noexcept {
  const auto dr = a[0] - b[0];
  const auto dg = a[1] - b[1];
  const auto db = a[2] - b[2];
  return dr * dr + dg * dg + db * db;
}

[[nodiscard]] Rect intersect(Rect a, Rect b) noexcept {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  if (right <= left || bottom <= top) {
    return {};
  }
  return {left, top, right - left, bottom - top};
}

}  // namespace

std::string_view color_match_sampling_token(ColorMatchSampling sampling) noexcept {
  switch (sampling) {
    case ColorMatchSampling::Continuous:
      return "continuous";
    case ColorMatchSampling::Once:
      return "once";
    case ColorMatchSampling::BackgroundSwatch:
      return "background";
  }
  return "continuous";
}

std::optional<ColorMatchSampling> color_match_sampling_from_token(std::string_view token) noexcept {
  for (const auto sampling :
       {ColorMatchSampling::Continuous, ColorMatchSampling::Once, ColorMatchSampling::BackgroundSwatch}) {
    if (token == color_match_sampling_token(sampling)) {
      return sampling;
    }
  }
  return std::nullopt;
}

std::string_view color_match_limits_token(ColorMatchLimits limits) noexcept {
  switch (limits) {
    case ColorMatchLimits::Discontiguous:
      return "discontiguous";
    case ColorMatchLimits::Contiguous:
      return "contiguous";
    case ColorMatchLimits::FindEdges:
      return "findEdges";
  }
  return "contiguous";
}

std::optional<ColorMatchLimits> color_match_limits_from_token(std::string_view token) noexcept {
  for (const auto limits :
       {ColorMatchLimits::Discontiguous, ColorMatchLimits::Contiguous, ColorMatchLimits::FindEdges}) {
    if (token == color_match_limits_token(limits)) {
      return limits;
    }
  }
  return std::nullopt;
}

int color_match_wand_tolerance(int tolerance_percent) noexcept {
  const auto percent = std::clamp(tolerance_percent, kMinColorMatchTolerance, kMaxColorMatchTolerance);
  return (percent * 255 + 100) / 200;
}

float color_match_strength(const ColorMatchRgb& pixel, const ColorMatchRgb& sample, int tolerance_percent,
                           bool anti_alias) noexcept {
  if (tolerance_percent >= kMaxColorMatchTolerance) {
    return 1.0F;  // 100% takes every color
  }
  const auto distance_squared = squared_distance(pixel, sample);
  const auto tolerance = static_cast<float>(color_match_wand_tolerance(tolerance_percent));
  if (tolerance <= 0.0F) {
    return distance_squared == 0.0F ? 1.0F : 0.0F;
  }
  // color_within_tolerance: the sum of squared differences within 4 * tolerance^2.
  const auto limit_squared = 4.0F * tolerance * tolerance;
  if (distance_squared <= limit_squared) {
    return 1.0F;
  }
  if (!anti_alias) {
    return 0.0F;
  }
  const auto limit = 2.0F * tolerance;
  const auto fringe_end = limit * kFringeFactor;
  const auto distance = std::sqrt(distance_squared);
  if (distance >= fringe_end) {
    return 0.0F;
  }
  return (fringe_end - distance) / (fringe_end - limit);
}

float color_match_protection_scale(const ColorMatchRgb& pixel, const ColorMatchRgb& sample,
                                   const ColorMatchRgb& protected_color, int tolerance_percent,
                                   bool anti_alias) noexcept {
  const auto to_sample = std::sqrt(squared_distance(pixel, sample));
  const auto to_protected = std::sqrt(squared_distance(pixel, protected_color));
  if (to_protected <= to_sample) {
    return 0.0F;  // nearer to the protected color, or a tie: kept
  }
  // The ramp is as wide as the match's fringe: a quarter of the tolerance distance.
  const auto limit = 2.0F * static_cast<float>(color_match_wand_tolerance(tolerance_percent));
  const auto ramp = anti_alias ? limit * (kFringeFactor - 1.0F) : 0.0F;
  if (ramp <= 0.0F) {
    return 1.0F;
  }
  return std::min(1.0F, (to_protected - to_sample) / ramp);
}

ColorMatchPixel color_match_pixel_at(const PixelBuffer& pixels, std::int32_t x, std::int32_t y) {
  const auto format = pixels.format();
  const auto* px = pixels.pixel(x, y);
  if (format.bit_depth == BitDepth::UInt8) {
    return {{static_cast<float>(px[0]), static_cast<float>(px[1]), static_cast<float>(px[2])},
            format.channels >= 4 ? static_cast<float>(px[3]) / 255.0F : 1.0F};
  }
  const auto values = load_pixel(format, px);
  ColorMatchPixel sample;
  sample.alpha = values[3] / 255.0F;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    sample.rgb[channel] = format.bit_depth == BitDepth::Float32
                              ? static_cast<float>(srgb_encode(static_cast<double>(values[channel]) / 255.0) * 255.0)
                              : values[channel];
  }
  return sample;
}

std::vector<float> color_match_dab_weights(const PixelBuffer& source, Rect rect, std::int32_t seed_x,
                                           std::int32_t seed_y, const ColorMatchRgb& sample,
                                           const ColorMatchOptions& options, std::span<const float> footprint) {
  const auto width = static_cast<std::size_t>(std::max(0, rect.width));
  const auto height = static_cast<std::size_t>(std::max(0, rect.height));
  std::vector<float> weights(width * height, 0.0F);
  if (weights.empty() || footprint.size() < weights.size() || source.empty() || source.format().channels < 3 ||
      rect.x < 0 || rect.y < 0 || rect.x + rect.width > source.width() || rect.y + rect.height > source.height()) {
    return weights;
  }

  // Each pixel's match with the sample (0 for transparent pixels) and, for Find Edges, its
  // color. The whole box is compared, so a tip with gaps (a scatter, a ring) still joins up.
  const bool find_edges = options.limits == ColorMatchLimits::FindEdges;
  // Find Edges gives no partly matching fringe, so it compares without one.
  const bool fringe = options.anti_alias && !find_edges;
  std::vector<float> match(weights.size(), 0.0F);
  std::vector<ColorMatchRgb> colors;
  if (find_edges) {
    colors.resize(weights.size());
  }
  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      const auto index = row * width + column;
      const auto pixel = color_match_pixel_at(source, rect.x + static_cast<std::int32_t>(column),
                                              rect.y + static_cast<std::int32_t>(row));
      if (pixel.alpha <= 0.0F) {
        continue;
      }
      match[index] = color_match_strength(pixel.rgb, sample, options.tolerance, fringe);
      if (find_edges) {
        colors[index] = pixel.rgb;
      }
    }
  }

  if (options.limits == ColorMatchLimits::Discontiguous) {
    for (std::size_t index = 0; index < weights.size(); ++index) {
      weights[index] = footprint[index] * match[index];
    }
    return weights;
  }

  if (!rect.contains(seed_x, seed_y)) {
    return weights;
  }
  const auto seed_index = static_cast<std::size_t>(seed_y - rect.y) * width + static_cast<std::size_t>(seed_x - rect.x);
  if (match[seed_index] < 1.0F) {
    return weights;
  }

  // The area joined to the seed through fully matching pixels. Find Edges also needs each step
  // between neighbours to stay within half the tolerance distance, so the result is the set
  // reachable through such steps, whatever order the walk takes.
  const auto step_limit = static_cast<float>(color_match_wand_tolerance(options.tolerance));
  const auto step_limit_squared = step_limit * step_limit;
  std::vector<std::uint8_t> joined(weights.size(), 0U);
  std::vector<std::size_t> queue;
  queue.reserve(std::min<std::size_t>(weights.size(), 65536U));
  joined[seed_index] = 1U;
  queue.push_back(seed_index);
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const auto index = queue[head];
    const auto column = index % width;
    const auto row = index / width;
    const auto visit = [&](std::size_t neighbour) {
      if (joined[neighbour] != 0U || match[neighbour] < 1.0F) {
        return;
      }
      if (find_edges && squared_distance(colors[index], colors[neighbour]) > step_limit_squared) {
        return;
      }
      joined[neighbour] = 1U;
      queue.push_back(neighbour);
    };
    if (column > 0U) {
      visit(index - 1U);
    }
    if (column + 1U < width) {
      visit(index + 1U);
    }
    if (row > 0U) {
      visit(index - width);
    }
    if (row + 1U < height) {
      visit(index + width);
    }
  }

  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      const auto index = row * width + column;
      if (joined[index] != 0U) {
        weights[index] = footprint[index];
        continue;
      }
      if (!fringe || match[index] <= 0.0F || match[index] >= 1.0F) {
        continue;
      }
      // A partly matching pixel takes its share only when it touches the joined area.
      bool touches = false;
      for (int offset_y = -1; offset_y <= 1 && !touches; ++offset_y) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto neighbour_column = static_cast<std::ptrdiff_t>(column) + offset_x;
          const auto neighbour_row = static_cast<std::ptrdiff_t>(row) + offset_y;
          if (neighbour_column < 0 || neighbour_row < 0 || neighbour_column >= static_cast<std::ptrdiff_t>(width) ||
              neighbour_row >= static_cast<std::ptrdiff_t>(height)) {
            continue;
          }
          if (joined[static_cast<std::size_t>(neighbour_row) * width + static_cast<std::size_t>(neighbour_column)] !=
              0U) {
            touches = true;
            break;
          }
        }
      }
      if (touches) {
        weights[index] = footprint[index] * match[index];
      }
    }
  }
  return weights;
}

ColorMatchRoundTip color_match_round_tip(double center_x, double center_y, int diameter, int softness) {
  ColorMatchRoundTip tip;
  const auto radius = std::max(1, diameter) / 2;
  tip.hot_x = static_cast<std::int32_t>(std::floor(center_x));
  tip.hot_y = static_cast<std::int32_t>(std::floor(center_y));
  tip.rect = Rect{tip.hot_x - radius - 1, tip.hot_y - radius - 1, radius * 2 + 3, radius * 2 + 3};
  tip.footprint.resize(static_cast<std::size_t>(tip.rect.width) * static_cast<std::size_t>(tip.rect.height));
  for (std::int32_t row = 0; row < tip.rect.height; ++row) {
    for (std::int32_t column = 0; column < tip.rect.width; ++column) {
      const auto dx = static_cast<double>(tip.rect.x + column) - center_x;
      const auto dy = static_cast<double>(tip.rect.y + row) - center_y;
      tip.footprint[static_cast<std::size_t>(row) * static_cast<std::size_t>(tip.rect.width) +
                    static_cast<std::size_t>(column)] =
          radius <= 0 ? ((tip.rect.x + column == tip.hot_x && tip.rect.y + row == tip.hot_y) ? 1.0F : 0.0F)
                      : brush_coverage(dx * dx + dy * dy, radius, softness);
    }
  }
  return tip;
}

ColorMatchStroke::ColorMatchStroke(const PixelBuffer& source, const ColorMatchOptions& options,
                                   ColorMatchSampling sampling, EditColor background)
    : source_(source), options_(options), sampling_(sampling) {
  options_.tolerance = std::clamp(options_.tolerance, kMinColorMatchTolerance, kMaxColorMatchTolerance);
  if (sampling_ == ColorMatchSampling::BackgroundSwatch) {
    sample_ = ColorMatchRgb{static_cast<float>(background.r), static_cast<float>(background.g),
                            static_cast<float>(background.b)};
  }
}

float& ColorMatchStroke::cap_at(std::int32_t x, std::int32_t y) {
  const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y >> kTileShift)) << 32U) |
                   static_cast<std::uint64_t>(static_cast<std::uint32_t>(x >> kTileShift));
  auto& tile = caps_[key];
  if (tile == nullptr) {
    tile = std::make_unique<Tile>();
    tile->fill(0.0F);
  }
  return (*tile)[static_cast<std::size_t>(y & (kTileSize - 1)) * kTileSize +
                 static_cast<std::size_t>(x & (kTileSize - 1))];
}

ColorMatchStroke::Dab ColorMatchStroke::dab(std::int32_t center_x, std::int32_t center_y, Rect rect,
                                            std::span<const float> footprint, const SelectionCoverage& selection) {
  if (source_.empty() || source_.format().channels < 3 || rect.empty() ||
      footprint.size() < static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height)) {
    return {};
  }
  const auto clipped = intersect(rect, Rect::from_size(source_.width(), source_.height()));
  if (clipped.empty()) {
    return {};
  }

  // The sample: Continuous reads the stroke-start pixel under every dab's hot spot, Once keeps
  // the first one a dab finds, Background Swatch was set at the start.
  const bool center_inside = clipped.contains(center_x, center_y);
  if (sampling_ == ColorMatchSampling::Continuous || !sample_.has_value()) {
    if (!center_inside) {
      return {};
    }
    const auto center = color_match_pixel_at(source_, center_x, center_y);
    if (center.alpha <= 0.0F) {
      return {};
    }
    sample_ = center.rgb;
  }

  const auto* tip = footprint.data();
  if (clipped != rect) {
    clipped_footprint_.resize(static_cast<std::size_t>(clipped.width) * static_cast<std::size_t>(clipped.height));
    for (std::int32_t row = 0; row < clipped.height; ++row) {
      const auto* from = footprint.data() +
                         static_cast<std::size_t>(clipped.y - rect.y + row) * static_cast<std::size_t>(rect.width) +
                         static_cast<std::size_t>(clipped.x - rect.x);
      std::copy_n(from, clipped.width,
                  clipped_footprint_.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(clipped.width));
    }
    tip = clipped_footprint_.data();
  }
  weights_ = color_match_dab_weights(
      source_, clipped, center_x, center_y, *sample_, options_,
      std::span<const float>(tip, static_cast<std::size_t>(clipped.width) * static_cast<std::size_t>(clipped.height)));

  const bool deep = source_.format().bit_depth != BitDepth::UInt8;
  // The protected color's step is soft when the dab's own edge is (no fringe with Find Edges).
  const bool protect_fringe = options_.anti_alias && options_.limits != ColorMatchLimits::FindEdges;
  for (std::int32_t row = 0; row < clipped.height; ++row) {
    const auto y = clipped.y + row;
    for (std::int32_t column = 0; column < clipped.width; ++column) {
      auto& weight = weights_[static_cast<std::size_t>(row) * static_cast<std::size_t>(clipped.width) +
                              static_cast<std::size_t>(column)];
      if (weight <= 0.0F) {
        continue;
      }
      const auto x = clipped.x + column;
      if (protected_color_.has_value()) {
        // A pixel at least as near to the protected color as to the sample is kept.
        weight *= color_match_protection_scale(color_match_pixel_at(source_, x, y).rgb, *sample_, *protected_color_,
                                               options_.tolerance, protect_fringe);
        if (weight <= 0.0F) {
          weight = 0.0F;
          continue;
        }
      }
      if (selection) {
        weight *= std::clamp(selection(x, y), 0.0F, 1.0F);
        if (weight <= 0.0F) {
          weight = 0.0F;
          continue;
        }
      }
      if (palette_ != nullptr && !deep) {
        if (weight < palette_->coverage_threshold) {
          weight = 0.0F;
          continue;
        }
        weight = 1.0F;
      }
      auto& cap = cap_at(x, y);
      if (weight <= cap) {
        weight = 0.0F;  // an earlier dab already took this pixel at least this strongly
        continue;
      }
      cap = weight;
    }
  }
  return {clipped, std::span<const float>(weights_)};
}

}  // namespace patchy
