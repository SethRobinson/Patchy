#pragma once

// The Color Replacement tool's fixed rules (GitHub issue 720, docs/color-replacement.md).
//
// A stroke is a row of brush dabs. Each dab compares the stroke-start pixels under the tip
// with ONE sampled color (the Magic Wand's metric on display RGB), optionally keeps only the
// matching pixels connected to the dab's center, and paints the foreground color over those
// pixels with one of four blend modes; alpha never changes. The pixels are written at once.
// The match itself is core/color_match_stroke.hpp, shared with the Background Eraser.
//
// Legal boundary (docs/legal-constraints-selection.md, "Color Replacement tool"): the sample stays one
// point in display RGB with a hard tolerance and a short linear fringe. Never a Lab point or
// area model, a Gaussian or other smoothing function around the sample, histogram or range
// normalization before the comparison, a stored spatial or range mask, or anything that can
// be adjusted after the dab is painted.

#include "core/blend_math_deep.hpp"
#include "core/color_match_stroke.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_tools.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace patchy {

// The sampling, the limits and the match are the shared ones in core/color_match_stroke.hpp
// (the Background Eraser uses them too, GitHub issue 727); these names are the tool's own.
// Append-only: the tokens below are script and settings identifiers.
enum class ColorReplaceMode : std::uint8_t { Hue, Saturation, Color, Luminosity };
using ColorReplaceSampling = ColorMatchSampling;
using ColorReplaceLimits = ColorMatchLimits;

inline constexpr int kMinColorReplaceTolerance = kMinColorMatchTolerance;
inline constexpr int kMaxColorReplaceTolerance = kMaxColorMatchTolerance;
inline constexpr int kDefaultColorReplaceTolerance = kDefaultColorMatchTolerance;

// "hue", "saturation", "color", "luminosity".
[[nodiscard]] std::string_view color_replace_mode_token(ColorReplaceMode mode) noexcept;
[[nodiscard]] std::optional<ColorReplaceMode> color_replace_mode_from_token(std::string_view token) noexcept;
// "continuous", "once", "background".
[[nodiscard]] inline std::string_view color_replace_sampling_token(ColorReplaceSampling sampling) noexcept {
  return color_match_sampling_token(sampling);
}
[[nodiscard]] inline std::optional<ColorReplaceSampling> color_replace_sampling_from_token(
    std::string_view token) noexcept {
  return color_match_sampling_from_token(token);
}
// "discontiguous", "contiguous", "findEdges".
[[nodiscard]] inline std::string_view color_replace_limits_token(ColorReplaceLimits limits) noexcept {
  return color_match_limits_token(limits);
}
[[nodiscard]] inline std::optional<ColorReplaceLimits> color_replace_limits_from_token(
    std::string_view token) noexcept {
  return color_match_limits_from_token(token);
}

// The layer blend mode whose math a Mode paints with (core/blend_math).
[[nodiscard]] BlendMode color_replace_blend_mode(ColorReplaceMode mode) noexcept;

struct ColorReplaceOptions {
  ColorReplaceMode mode{ColorReplaceMode::Color};
  ColorReplaceLimits limits{ColorReplaceLimits::Contiguous};
  int tolerance{kDefaultColorReplaceTolerance};  // percent, 0 = exact matches only
  bool anti_alias{true};

  // The part of the options the shared match reads.
  [[nodiscard]] ColorMatchOptions match() const noexcept { return {limits, tolerance, anti_alias}; }

  friend bool operator==(const ColorReplaceOptions&, const ColorReplaceOptions&) = default;
};

// Display-encoded RGB on the 0..255 scale (unrounded for 16 and 32-bit pixels).
using ColorReplaceRgb = ColorMatchRgb;

// The Magic Wand tolerance (0..255) a percent stands for. The wand accepts a color distance of
// twice its tolerance, so the percent is that distance as a share of one channel's full range:
// percent * 255 / 200, rounded (30% is wand 38, a distance of 76).
[[nodiscard]] inline int color_replace_wand_tolerance(int tolerance_percent) noexcept {
  return color_match_wand_tolerance(tolerance_percent);
}

// How strongly `pixel` matches `sample`. 1 when the Magic Wand at
// color_replace_wand_tolerance(tolerance) would accept it (the sum of squared RGB differences
// is within 4 * tolerance^2; tolerance 0 is an exact match), and for every color at 100%.
// With anti_alias the match falls in a straight line to 0 over a further quarter of that
// distance; without it, it is 0.
[[nodiscard]] inline float color_replace_match(const ColorReplaceRgb& pixel, const ColorReplaceRgb& sample,
                                               int tolerance_percent, bool anti_alias) noexcept {
  return color_match_strength(pixel, sample, tolerance_percent, anti_alias);
}

// One pixel of a color buffer (3 or 4 channels, any depth) as display-encoded RGB, and its
// alpha as 0..1 (1 without an alpha channel). A 32-bit linear pixel is sRGB-encoded first, so
// the comparison sees the color as it looks.
using ColorReplaceSample = ColorMatchPixel;
[[nodiscard]] inline ColorReplaceSample color_replace_sample_at(const PixelBuffer& pixels, std::int32_t x,
                                                                std::int32_t y) {
  return color_match_pixel_at(pixels, x, y);
}

// One dab's weights over `rect` (buffer coordinates, inside `source`), row-major: the tip
// coverage in `footprint` times how well each pixel of `source` matches `sample`, limited by
// `options.limits` (color_match_dab_weights has the rules for each Limits choice).
[[nodiscard]] inline std::vector<float> color_replace_dab_weights(const PixelBuffer& source, Rect rect,
                                                                  std::int32_t seed_x, std::int32_t seed_y,
                                                                  const ColorReplaceRgb& sample,
                                                                  const ColorReplaceOptions& options,
                                                                  std::span<const float> footprint) {
  return color_match_dab_weights(source, rect, seed_x, seed_y, sample, options.match(), footprint);
}

// `foreground` painted over one stroke-start pixel with `mode` at `weight` (0..1): the blend
// mode's color mixed in by weight, alpha untouched.
[[nodiscard]] std::array<std::uint8_t, 4> color_replace_pixel_rgba8(std::array<std::uint8_t, 4> original,
                                                                    EditColor foreground, ColorReplaceMode mode,
                                                                    float weight) noexcept;
// The same on deep-scale values in `domain` (16 and 32-bit pixels); `paint` is already in it.
[[nodiscard]] std::array<float, 4> color_replace_pixel_deep(std::array<float, 4> original, DeepChannels paint,
                                                            ColorReplaceMode mode, float weight,
                                                            DeepDomain domain) noexcept;

// One stroke of the tool on one pixel buffer. The shared ColorMatchStroke holds the
// stroke-start copy of the buffer, the sample for Once and Background Swatch, and the largest
// weight each pixel has taken, so overlapping dabs never compound: every write is the
// stroke-start pixel repainted at its largest weight so far.
class ColorReplaceStroke {
 public:
  // Selection coverage (0..1) of a buffer pixel; empty means everything is selected.
  using SelectionCoverage = ColorMatchStroke::SelectionCoverage;

  // `foreground_gain` scales the paint in linear light on a 32-bit buffer (the foreground
  // swatch's Intensity); 1 otherwise.
  ColorReplaceStroke(const PixelBuffer& source, const ColorReplaceOptions& options, ColorReplaceSampling sampling,
                     EditColor foreground, EditColor background, float foreground_gain = 1.0F);

  // Indexed documents: weights under the context's coverage threshold write nothing, the rest
  // paint in full, and every written pixel snaps to the palette. Not owned; may be null.
  void set_palette_snap(const PaletteSnapContext* palette) noexcept { match_.set_palette_snap(palette); }

  // One dab on `target` (the buffer the stroke was made from, same size and format). `rect`
  // is the tip's box in buffer coordinates and `footprint` its coverage, row-major; the part
  // outside the buffer is ignored. (center_x, center_y) is the hot spot: where Continuous
  // samples and where the contiguous limits start. Returns the changed box (empty if none).
  Rect dab(PixelBuffer& target, std::int32_t center_x, std::int32_t center_y, Rect rect,
           std::span<const float> footprint, const SelectionCoverage& selection = {});

  // A procedural round dab (core brush_coverage): `softness` is 0..100 as on the options bar.
  Rect round_dab(PixelBuffer& target, double center_x, double center_y, int diameter, int softness,
                 const SelectionCoverage& selection = {});

  // The color the stroke compares against, once it has one (Continuous: the last dab's).
  [[nodiscard]] const std::optional<ColorReplaceRgb>& sample() const noexcept { return match_.sample(); }

 private:
  ColorMatchStroke match_;
  ColorReplaceMode mode_;
  EditColor foreground_;
  DeepChannels deep_paint_{};
  DeepDomain domain_{DeepDomain::Encoded};
};

}  // namespace patchy
