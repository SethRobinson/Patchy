#include "core/color_replace.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace patchy {

namespace {

// The anti-alias fringe: a match falls to 0 this much past the tolerance distance.
constexpr float kFringeFactor = 1.25F;

[[nodiscard]] float squared_distance(const ColorReplaceRgb& a, const ColorReplaceRgb& b) noexcept {
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

[[nodiscard]] Rect unite(Rect a, Rect b) noexcept {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  const auto left = std::min(a.x, b.x);
  const auto top = std::min(a.y, b.y);
  const auto right = std::max(a.x + a.width, b.x + b.width);
  const auto bottom = std::max(a.y + a.height, b.y + b.height);
  return {left, top, right - left, bottom - top};
}

}  // namespace

std::string_view color_replace_mode_token(ColorReplaceMode mode) noexcept {
  switch (mode) {
    case ColorReplaceMode::Hue:
      return "hue";
    case ColorReplaceMode::Saturation:
      return "saturation";
    case ColorReplaceMode::Color:
      return "color";
    case ColorReplaceMode::Luminosity:
      return "luminosity";
  }
  return "color";
}

std::optional<ColorReplaceMode> color_replace_mode_from_token(std::string_view token) noexcept {
  for (const auto mode : {ColorReplaceMode::Hue, ColorReplaceMode::Saturation, ColorReplaceMode::Color,
                          ColorReplaceMode::Luminosity}) {
    if (token == color_replace_mode_token(mode)) {
      return mode;
    }
  }
  return std::nullopt;
}

std::string_view color_replace_sampling_token(ColorReplaceSampling sampling) noexcept {
  switch (sampling) {
    case ColorReplaceSampling::Continuous:
      return "continuous";
    case ColorReplaceSampling::Once:
      return "once";
    case ColorReplaceSampling::BackgroundSwatch:
      return "background";
  }
  return "continuous";
}

std::optional<ColorReplaceSampling> color_replace_sampling_from_token(std::string_view token) noexcept {
  for (const auto sampling : {ColorReplaceSampling::Continuous, ColorReplaceSampling::Once,
                              ColorReplaceSampling::BackgroundSwatch}) {
    if (token == color_replace_sampling_token(sampling)) {
      return sampling;
    }
  }
  return std::nullopt;
}

std::string_view color_replace_limits_token(ColorReplaceLimits limits) noexcept {
  switch (limits) {
    case ColorReplaceLimits::Discontiguous:
      return "discontiguous";
    case ColorReplaceLimits::Contiguous:
      return "contiguous";
    case ColorReplaceLimits::FindEdges:
      return "findEdges";
  }
  return "contiguous";
}

std::optional<ColorReplaceLimits> color_replace_limits_from_token(std::string_view token) noexcept {
  for (const auto limits : {ColorReplaceLimits::Discontiguous, ColorReplaceLimits::Contiguous,
                            ColorReplaceLimits::FindEdges}) {
    if (token == color_replace_limits_token(limits)) {
      return limits;
    }
  }
  return std::nullopt;
}

BlendMode color_replace_blend_mode(ColorReplaceMode mode) noexcept {
  switch (mode) {
    case ColorReplaceMode::Hue:
      return BlendMode::Hue;
    case ColorReplaceMode::Saturation:
      return BlendMode::Saturation;
    case ColorReplaceMode::Color:
      return BlendMode::Color;
    case ColorReplaceMode::Luminosity:
      return BlendMode::Luminosity;
  }
  return BlendMode::Color;
}

int color_replace_wand_tolerance(int tolerance_percent) noexcept {
  const auto percent = std::clamp(tolerance_percent, kMinColorReplaceTolerance, kMaxColorReplaceTolerance);
  return (percent * 255 + 100) / 200;
}

float color_replace_match(const ColorReplaceRgb& pixel, const ColorReplaceRgb& sample, int tolerance_percent,
                          bool anti_alias) noexcept {
  if (tolerance_percent >= kMaxColorReplaceTolerance) {
    return 1.0F;  // 100% takes every color
  }
  const auto distance_squared = squared_distance(pixel, sample);
  const auto tolerance = static_cast<float>(color_replace_wand_tolerance(tolerance_percent));
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

ColorReplaceSample color_replace_sample_at(const PixelBuffer& pixels, std::int32_t x, std::int32_t y) {
  const auto format = pixels.format();
  const auto* px = pixels.pixel(x, y);
  if (format.bit_depth == BitDepth::UInt8) {
    return {{static_cast<float>(px[0]), static_cast<float>(px[1]), static_cast<float>(px[2])},
            format.channels >= 4 ? static_cast<float>(px[3]) / 255.0F : 1.0F};
  }
  const auto values = load_pixel(format, px);
  ColorReplaceSample sample;
  sample.alpha = values[3] / 255.0F;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    sample.rgb[channel] = format.bit_depth == BitDepth::Float32
                              ? static_cast<float>(srgb_encode(static_cast<double>(values[channel]) / 255.0) * 255.0)
                              : values[channel];
  }
  return sample;
}

std::vector<float> color_replace_dab_weights(const PixelBuffer& source, Rect rect, std::int32_t seed_x,
                                             std::int32_t seed_y, const ColorReplaceRgb& sample,
                                             const ColorReplaceOptions& options, std::span<const float> footprint) {
  const auto width = static_cast<std::size_t>(std::max(0, rect.width));
  const auto height = static_cast<std::size_t>(std::max(0, rect.height));
  std::vector<float> weights(width * height, 0.0F);
  if (weights.empty() || footprint.size() < weights.size() || source.empty() || source.format().channels < 3 ||
      rect.x < 0 || rect.y < 0 || rect.x + rect.width > source.width() || rect.y + rect.height > source.height()) {
    return weights;
  }

  // Each pixel's match with the sample (0 for transparent pixels) and, for Find Edges, its
  // color. The whole box is compared, so a tip with gaps (a scatter, a ring) still joins up.
  const bool find_edges = options.limits == ColorReplaceLimits::FindEdges;
  // Find Edges gives no partly matching fringe, so it compares without one.
  const bool fringe = options.anti_alias && !find_edges;
  std::vector<float> match(weights.size(), 0.0F);
  std::vector<ColorReplaceRgb> colors;
  if (find_edges) {
    colors.resize(weights.size());
  }
  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      const auto index = row * width + column;
      const auto pixel = color_replace_sample_at(source, rect.x + static_cast<std::int32_t>(column),
                                                 rect.y + static_cast<std::int32_t>(row));
      if (pixel.alpha <= 0.0F) {
        continue;
      }
      match[index] = color_replace_match(pixel.rgb, sample, options.tolerance, fringe);
      if (find_edges) {
        colors[index] = pixel.rgb;
      }
    }
  }

  if (options.limits == ColorReplaceLimits::Discontiguous) {
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
  const auto step_limit = static_cast<float>(color_replace_wand_tolerance(options.tolerance));
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

std::array<std::uint8_t, 4> color_replace_pixel_rgba8(std::array<std::uint8_t, 4> original, EditColor foreground,
                                                      ColorReplaceMode mode, float weight) noexcept {
  weight = std::clamp(weight, 0.0F, 1.0F);
  if (weight <= 0.0F) {
    return original;
  }
  const auto blended = blend_rgb({foreground.r, foreground.g, foreground.b}, {original[0], original[1], original[2]},
                                 color_replace_blend_mode(mode));
  auto result = original;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = weight >= 1.0F
                          ? blended[channel]
                          : clamp_byte(static_cast<float>(original[channel]) +
                                       (static_cast<float>(blended[channel]) - static_cast<float>(original[channel])) *
                                           weight);
  }
  return result;
}

std::array<float, 4> color_replace_pixel_deep(std::array<float, 4> original, DeepChannels paint,
                                              ColorReplaceMode mode, float weight, DeepDomain domain) noexcept {
  weight = std::clamp(weight, 0.0F, 1.0F);
  if (weight <= 0.0F) {
    return original;
  }
  const auto blended =
      blend_rgb_deep(paint, {original[0], original[1], original[2]}, color_replace_blend_mode(mode), domain);
  auto result = original;
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    result[channel] = original[channel] + (blended[channel] - original[channel]) * weight;
  }
  return result;
}

ColorReplaceStroke::ColorReplaceStroke(const PixelBuffer& source, const ColorReplaceOptions& options,
                                       ColorReplaceSampling sampling, EditColor foreground, EditColor background,
                                       float foreground_gain)
    : source_(source), options_(options), sampling_(sampling), foreground_(foreground) {
  options_.tolerance = std::clamp(options_.tolerance, kMinColorReplaceTolerance, kMaxColorReplaceTolerance);
  domain_ = source_.format().bit_depth == BitDepth::Float32 ? DeepDomain::Linear : DeepDomain::Encoded;
  const auto paint_channel = [&](std::uint8_t value) {
    return domain_ == DeepDomain::Linear
               ? static_cast<float>(srgb_decode(static_cast<double>(value) / 255.0) * 255.0) * foreground_gain
               : static_cast<float>(value);
  };
  deep_paint_ = {paint_channel(foreground.r), paint_channel(foreground.g), paint_channel(foreground.b)};
  if (sampling_ == ColorReplaceSampling::BackgroundSwatch) {
    sample_ = ColorReplaceRgb{static_cast<float>(background.r), static_cast<float>(background.g),
                              static_cast<float>(background.b)};
  }
}

float& ColorReplaceStroke::cap_at(std::int32_t x, std::int32_t y) {
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

Rect ColorReplaceStroke::dab(PixelBuffer& target, std::int32_t center_x, std::int32_t center_y, Rect rect,
                             std::span<const float> footprint, const SelectionCoverage& selection) {
  if (source_.empty() || source_.format().channels < 3 || target.width() != source_.width() ||
      target.height() != source_.height() || target.format() != source_.format() || rect.empty() ||
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
  if (sampling_ == ColorReplaceSampling::Continuous || !sample_.has_value()) {
    if (!center_inside) {
      return {};
    }
    const auto center = color_replace_sample_at(source_, center_x, center_y);
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
  const auto weights = color_replace_dab_weights(
      source_, clipped, center_x, center_y, *sample_, options_,
      std::span<const float>(tip, static_cast<std::size_t>(clipped.width) * static_cast<std::size_t>(clipped.height)));

  const auto format = source_.format();
  const bool deep = format.bit_depth != BitDepth::UInt8;
  const bool has_alpha = format.channels >= 4;
  std::uint8_t* target_bytes = nullptr;  // fetched on the first write: data() detaches shared storage
  const auto stride = target.stride_bytes();
  const auto pixel_bytes = bytes_per_pixel(format);
  Rect dirty;
  for (std::int32_t row = 0; row < clipped.height; ++row) {
    const auto y = clipped.y + row;
    std::int32_t dirty_left = clipped.width;
    std::int32_t dirty_right = -1;
    for (std::int32_t column = 0; column < clipped.width; ++column) {
      auto weight = weights[static_cast<std::size_t>(row) * static_cast<std::size_t>(clipped.width) +
                            static_cast<std::size_t>(column)];
      if (weight <= 0.0F) {
        continue;
      }
      const auto x = clipped.x + column;
      if (selection) {
        weight *= std::clamp(selection(x, y), 0.0F, 1.0F);
        if (weight <= 0.0F) {
          continue;
        }
      }
      if (palette_ != nullptr && !deep) {
        if (weight < palette_->coverage_threshold) {
          continue;
        }
        weight = 1.0F;
      }
      auto& cap = cap_at(x, y);
      if (weight <= cap) {
        continue;  // an earlier dab already painted this pixel at least this strongly
      }
      cap = weight;
      if (target_bytes == nullptr) {
        target_bytes = target.data().data();
      }
      auto* destination = target_bytes + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * pixel_bytes;
      const auto* original = source_.pixel(x, y);
      bool changed = false;
      if (deep) {
        const auto before = load_pixel(format, destination);
        store_pixel(format, destination,
                    color_replace_pixel_deep(load_pixel(format, original), deep_paint_, options_.mode, weight, domain_));
        changed = load_pixel(format, destination) != before;
      } else {
        const std::array<std::uint8_t, 4> start{original[0], original[1], original[2],
                                                has_alpha ? original[3] : std::uint8_t{255}};
        auto painted = color_replace_pixel_rgba8(start, foreground_, options_.mode, weight);
        if (palette_ != nullptr) {
          // RGB only: the tool never changes alpha, and the palette threshold would.
          snap_pixel_to_palette(painted.data(), 3, *palette_);
        }
        changed = destination[0] != painted[0] || destination[1] != painted[1] || destination[2] != painted[2];
        destination[0] = painted[0];
        destination[1] = painted[1];
        destination[2] = painted[2];
      }
      if (changed) {
        dirty_left = std::min(dirty_left, column);
        dirty_right = std::max(dirty_right, column);
      }
    }
    if (dirty_left <= dirty_right) {
      dirty = unite(dirty, Rect{clipped.x + dirty_left, y, dirty_right - dirty_left + 1, 1});
    }
  }
  return dirty;
}

Rect ColorReplaceStroke::round_dab(PixelBuffer& target, double center_x, double center_y, int diameter, int softness,
                                   const SelectionCoverage& selection) {
  const auto radius = std::max(1, diameter) / 2;
  const auto hot_x = static_cast<std::int32_t>(std::floor(center_x));
  const auto hot_y = static_cast<std::int32_t>(std::floor(center_y));
  const Rect rect{hot_x - radius - 1, hot_y - radius - 1, radius * 2 + 3, radius * 2 + 3};
  std::vector<float> footprint(static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height));
  for (std::int32_t row = 0; row < rect.height; ++row) {
    for (std::int32_t column = 0; column < rect.width; ++column) {
      const auto dx = static_cast<double>(rect.x + column) - center_x;
      const auto dy = static_cast<double>(rect.y + row) - center_y;
      footprint[static_cast<std::size_t>(row) * static_cast<std::size_t>(rect.width) +
                static_cast<std::size_t>(column)] =
          radius <= 0 ? ((rect.x + column == hot_x && rect.y + row == hot_y) ? 1.0F : 0.0F)
                      : brush_coverage(dx * dx + dy * dy, radius, softness);
    }
  }
  return dab(target, hot_x, hot_y, rect, footprint, selection);
}

}  // namespace patchy
