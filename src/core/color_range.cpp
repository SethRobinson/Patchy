#include "core/color_range.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace patchy {
namespace {

constexpr std::array<std::pair<ColorRangeMode, std::string_view>, kColorRangeModeCount> kModeTokens{{
    {ColorRangeMode::SampledColors, "sampled"},
    {ColorRangeMode::Reds, "reds"},
    {ColorRangeMode::Yellows, "yellows"},
    {ColorRangeMode::Greens, "greens"},
    {ColorRangeMode::Cyans, "cyans"},
    {ColorRangeMode::Blues, "blues"},
    {ColorRangeMode::Magentas, "magentas"},
    {ColorRangeMode::Highlights, "highlights"},
    {ColorRangeMode::Midtones, "midtones"},
    {ColorRangeMode::Shadows, "shadows"},
}};

// Hue runs 0..1535 around the hexcone: 256 steps per 60 degrees.
constexpr int kHueTurn = 1536;
constexpr int kHueSector = 256;
// Luma in 1/256 levels (77 + 150 + 29 = 256): the thirds of 0..255.
constexpr int kLowerThird = 85 * 256;
constexpr int kUpperThird = 170 * 256;

// floor(sqrt(value)), bit by bit: no floating point, so the ramp is the same everywhere.
std::uint32_t integer_sqrt(std::uint32_t value) noexcept {
  std::uint32_t result = 0;
  std::uint32_t bit = 1U << 30U;
  while (bit > value) {
    bit >>= 2U;
  }
  while (bit != 0U) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1U) + bit;
    } else {
      result >>= 1U;
    }
    bit >>= 2U;
  }
  return result;
}

// 255 while value <= full_until, 0 from full_until + width on, linear between (rounded).
int ramp_down(int value, int full_until, int width) noexcept {
  if (value <= full_until) {
    return 255;
  }
  if (width <= 0 || value >= full_until + width) {
    return 0;
  }
  return (255 * (full_until + width - value) + width / 2) / width;
}

// 0 while value <= full_from - width, 255 from full_from on.
int ramp_up(int value, int full_from, int width) noexcept {
  if (value >= full_from) {
    return 255;
  }
  if (width <= 0 || value <= full_from - width) {
    return 0;
  }
  return (255 * (value - (full_from - width)) + width / 2) / width;
}

int rounded_quotient(int numerator, int denominator) noexcept {
  return numerator >= 0 ? (numerator + denominator / 2) / denominator
                        : -((-numerator + denominator / 2) / denominator);
}

int scale_255(int value, int factor) noexcept {
  return (value * factor + 127) / 255;
}

}  // namespace

std::string_view color_range_mode_token(ColorRangeMode mode) noexcept {
  for (const auto& [candidate, token] : kModeTokens) {
    if (candidate == mode) {
      return token;
    }
  }
  return kModeTokens.front().second;
}

std::optional<ColorRangeMode> color_range_mode_from_token(std::string_view token) noexcept {
  for (const auto& [mode, candidate] : kModeTokens) {
    if (candidate == token) {
      return mode;
    }
  }
  return std::nullopt;
}

ColorRangeClassifier::ColorRangeClassifier(const ColorRangeOptions& options)
    : mode_(options.mode),
      invert_(options.invert),
      fuzziness_(std::clamp(options.fuzziness, kMinColorRangeFuzziness, kMaxColorRangeFuzziness)) {
  if (mode_ != ColorRangeMode::SampledColors) {
    return;
  }
  for (const auto& sample : options.samples) {
    (sample.subtract ? subtract_samples_ : add_samples_).push_back(sample);
  }
  // Distances in sixteenths of a level: full coverage up to half the fuzziness, none at it.
  const auto outer = fuzziness_ * 16;
  const auto inner = fuzziness_ * 8;
  distance_ramp_.resize(static_cast<std::size_t>(fuzziness_) * static_cast<std::size_t>(fuzziness_) + 1U);
  for (std::size_t squared = 0; squared < distance_ramp_.size(); ++squared) {
    const auto distance = static_cast<int>(integer_sqrt(static_cast<std::uint32_t>(squared) * 256U));
    distance_ramp_[squared] = static_cast<std::uint8_t>(squared == 0U ? 255 : ramp_down(distance, inner, outer - inner));
  }
}

std::uint8_t ColorRangeClassifier::sampled_coverage(int red, int green, int blue) const noexcept {
  const auto strongest = [&](const std::vector<ColorRangeSample>& samples) {
    int best = 0;
    for (const auto& sample : samples) {
      const auto dr = red - static_cast<int>(sample.red);
      const auto dg = green - static_cast<int>(sample.green);
      const auto db = blue - static_cast<int>(sample.blue);
      const auto squared = static_cast<std::size_t>(dr * dr + dg * dg + db * db);
      if (squared < distance_ramp_.size()) {
        best = std::max(best, static_cast<int>(distance_ramp_[squared]));
      }
    }
    return best;
  };
  const auto added = strongest(add_samples_);
  if (added == 0 || subtract_samples_.empty()) {
    return static_cast<std::uint8_t>(added);
  }
  // Scaled, never subtracted: docs/legal-constraints-selection.md ("Color Range") keeps the
  // combine apart from a difference of two color models.
  return static_cast<std::uint8_t>(scale_255(added, 255 - strongest(subtract_samples_)));
}

std::uint8_t ColorRangeClassifier::hue_band_coverage(int red, int green, int blue) const noexcept {
  const auto highest = std::max({red, green, blue});
  const auto chroma = highest - std::min({red, green, blue});
  if (chroma == 0) {
    return 0;
  }
  // Ties go to red, then green, then blue.
  int hue = 0;
  if (highest == red) {
    hue = rounded_quotient((green - blue) * kHueSector, chroma);
  } else if (highest == green) {
    hue = 2 * kHueSector + rounded_quotient((blue - red) * kHueSector, chroma);
  } else {
    hue = 4 * kHueSector + rounded_quotient((red - green) * kHueSector, chroma);
  }
  hue = ((hue % kHueTurn) + kHueTurn) % kHueTurn;
  const auto band = static_cast<int>(mode_) - static_cast<int>(ColorRangeMode::Reds);
  auto distance = std::abs(hue - band * kHueSector);
  distance = std::min(distance, kHueTurn - distance);
  const auto soft_edge = (fuzziness_ * (kHueSector / 2) + kMaxColorRangeFuzziness / 2) / kMaxColorRangeFuzziness;
  const auto hue_weight = ramp_down(distance, kHueSector / 2, soft_edge);
  return static_cast<std::uint8_t>(scale_255(hue_weight, std::min(255, chroma * 2)));
}

std::uint8_t ColorRangeClassifier::tone_coverage(int red, int green, int blue) const noexcept {
  const auto luma = red * 77 + green * 150 + blue * 29;
  const auto soft_edge = fuzziness_ * kLowerThird / kMaxColorRangeFuzziness;
  switch (mode_) {
    case ColorRangeMode::Shadows:
      return static_cast<std::uint8_t>(ramp_down(luma, kLowerThird, soft_edge));
    case ColorRangeMode::Highlights:
      return static_cast<std::uint8_t>(ramp_up(luma, kUpperThird, soft_edge));
    default:
      return static_cast<std::uint8_t>(
          std::min(ramp_up(luma, kLowerThird, soft_edge), ramp_down(luma, kUpperThird, soft_edge)));
  }
}

std::uint8_t ColorRangeClassifier::classify(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                            std::uint8_t alpha) const noexcept {
  int coverage = 0;
  if (alpha != 0U) {
    switch (mode_) {
      case ColorRangeMode::SampledColors:
        coverage = sampled_coverage(red, green, blue);
        break;
      case ColorRangeMode::Highlights:
      case ColorRangeMode::Midtones:
      case ColorRangeMode::Shadows:
        coverage = tone_coverage(red, green, blue);
        break;
      default:
        coverage = hue_band_coverage(red, green, blue);
        break;
    }
    if (alpha != 255U) {
      coverage = scale_255(coverage, alpha);
    }
  }
  return static_cast<std::uint8_t>(invert_ ? 255 - coverage : coverage);
}

void ColorRangeClassifier::classify_row(const std::uint8_t* rgba, std::size_t count,
                                        std::uint8_t* coverage) const noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    const auto* pixel = rgba + index * 4U;
    coverage[index] = classify(pixel[0], pixel[1], pixel[2], pixel[3]);
  }
}

PixelBuffer color_range_mask(const PixelBuffer& source, const ColorRangeOptions& options) {
  if (source.empty() || source.format().channels < 3U || source.format().color_mode != ColorMode::RGB) {
    return {};
  }
  PixelBuffer mask(source.width(), source.height(), PixelFormat::gray8());
  const ColorRangeClassifier classifier(options);
  const auto width = static_cast<std::size_t>(source.width());
  const bool rgba8 = source.format().bit_depth == BitDepth::UInt8 && source.format().channels == 4U;
  std::vector<std::uint8_t> scratch(rgba8 ? 0U : width * 4U);
  for (std::int32_t y = 0; y < source.height(); ++y) {
    auto out = mask.row(y);
    if (rgba8) {
      classifier.classify_row(source.row(y).data(), width, out.data());
      continue;
    }
    if (source.format().bit_depth == BitDepth::UInt8) {
      const auto channels = static_cast<std::size_t>(source.format().channels);
      const auto in = source.row(y);
      for (std::size_t x = 0; x < width; ++x) {
        const auto* pixel = in.data() + x * channels;
        scratch[x * 4U] = pixel[0];
        scratch[x * 4U + 1U] = pixel[1];
        scratch[x * 4U + 2U] = pixel[2];
        scratch[x * 4U + 3U] = channels >= 4U ? pixel[3] : std::uint8_t{255};
      }
    } else {
      for (std::size_t x = 0; x < width; ++x) {
        const auto bytes = display_rgba8_at(source, static_cast<std::int32_t>(x), y);
        std::copy(bytes.begin(), bytes.end(), scratch.begin() + static_cast<std::ptrdiff_t>(x * 4U));
      }
    }
    classifier.classify_row(scratch.data(), width, out.data());
  }
  return mask;
}

}  // namespace patchy
