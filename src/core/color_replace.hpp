#pragma once

// The Color Replacement tool's fixed rules (GitHub issue 720, docs/color-replacement.md).
//
// A stroke is a row of brush dabs. Each dab compares the stroke-start pixels under the tip
// with ONE sampled color (the Magic Wand's metric on display RGB), optionally keeps only the
// matching pixels connected to the dab's center, and paints the foreground color over those
// pixels with one of four blend modes; alpha never changes. The pixels are written at once.
//
// Legal boundary (docs/legal-constraints-selection.md, "Color Replacement tool"): the sample stays one
// point in display RGB with a hard tolerance and a short linear fringe. Never a Lab point or
// area model, a Gaussian or other smoothing function around the sample, histogram or range
// normalization before the comparison, a stored spatial or range mask, or anything that can
// be adjusted after the dab is painted.

#include "core/blend_math_deep.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_tools.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace patchy {

// Append-only: the tokens below are script and settings identifiers.
enum class ColorReplaceMode : std::uint8_t { Hue, Saturation, Color, Luminosity };
enum class ColorReplaceSampling : std::uint8_t { Continuous, Once, BackgroundSwatch };
enum class ColorReplaceLimits : std::uint8_t { Discontiguous, Contiguous, FindEdges };

inline constexpr int kMinColorReplaceTolerance = 0;
inline constexpr int kMaxColorReplaceTolerance = 100;
inline constexpr int kDefaultColorReplaceTolerance = 30;

// "hue", "saturation", "color", "luminosity".
[[nodiscard]] std::string_view color_replace_mode_token(ColorReplaceMode mode) noexcept;
[[nodiscard]] std::optional<ColorReplaceMode> color_replace_mode_from_token(std::string_view token) noexcept;
// "continuous", "once", "background".
[[nodiscard]] std::string_view color_replace_sampling_token(ColorReplaceSampling sampling) noexcept;
[[nodiscard]] std::optional<ColorReplaceSampling> color_replace_sampling_from_token(std::string_view token) noexcept;
// "discontiguous", "contiguous", "findEdges".
[[nodiscard]] std::string_view color_replace_limits_token(ColorReplaceLimits limits) noexcept;
[[nodiscard]] std::optional<ColorReplaceLimits> color_replace_limits_from_token(std::string_view token) noexcept;

// The layer blend mode whose math a Mode paints with (core/blend_math).
[[nodiscard]] BlendMode color_replace_blend_mode(ColorReplaceMode mode) noexcept;

struct ColorReplaceOptions {
  ColorReplaceMode mode{ColorReplaceMode::Color};
  ColorReplaceLimits limits{ColorReplaceLimits::Contiguous};
  int tolerance{kDefaultColorReplaceTolerance};  // percent, 0 = exact matches only
  bool anti_alias{true};

  friend bool operator==(const ColorReplaceOptions&, const ColorReplaceOptions&) = default;
};

// Display-encoded RGB on the 0..255 scale (unrounded for 16 and 32-bit pixels).
using ColorReplaceRgb = std::array<float, 3>;

// The Magic Wand tolerance (0..255) a percent stands for. The wand accepts a color distance of
// twice its tolerance, so the percent is that distance as a share of one channel's full range:
// percent * 255 / 200, rounded (30% is wand 38, a distance of 76).
[[nodiscard]] int color_replace_wand_tolerance(int tolerance_percent) noexcept;

// How strongly `pixel` matches `sample`. 1 when the Magic Wand at
// color_replace_wand_tolerance(tolerance) would accept it (the sum of squared RGB differences
// is within 4 * tolerance^2; tolerance 0 is an exact match), and for every color at 100%.
// With anti_alias the match falls in a straight line to 0 over a further quarter of that
// distance; without it, it is 0.
[[nodiscard]] float color_replace_match(const ColorReplaceRgb& pixel, const ColorReplaceRgb& sample,
                                        int tolerance_percent, bool anti_alias) noexcept;

// One pixel of a color buffer (3 or 4 channels, any depth) as display-encoded RGB, and its
// alpha as 0..1 (1 without an alpha channel). A 32-bit linear pixel is sRGB-encoded first, so
// the comparison sees the color as it looks.
struct ColorReplaceSample {
  ColorReplaceRgb rgb{};
  float alpha{0.0F};
};
[[nodiscard]] ColorReplaceSample color_replace_sample_at(const PixelBuffer& pixels, std::int32_t x, std::int32_t y);

// One dab's weights over `rect` (buffer coordinates, inside `source`), row-major: the tip
// coverage in `footprint` (same layout, 0 outside the tip) times how well each pixel of
// `source` matches `sample`, limited by `options.limits`:
// - Discontiguous: every matching pixel under the tip.
// - Contiguous: matching pixels joined to (seed_x, seed_y) through fully matching pixels of
//   the box (4-connected, so a tip with gaps still joins up); with anti_alias, partly matching
//   pixels touching that area too. Nothing when the seed itself does not fully match.
// - FindEdges: as Contiguous, but the area also stops where two neighbours differ by more than
//   half the tolerance distance (an abrupt step), and it gets no partly matching fringe.
// Fully transparent pixels never match.
[[nodiscard]] std::vector<float> color_replace_dab_weights(const PixelBuffer& source, Rect rect,
                                                           std::int32_t seed_x, std::int32_t seed_y,
                                                           const ColorReplaceRgb& sample,
                                                           const ColorReplaceOptions& options,
                                                           std::span<const float> footprint);

// `foreground` painted over one stroke-start pixel with `mode` at `weight` (0..1): the blend
// mode's color mixed in by weight, alpha untouched.
[[nodiscard]] std::array<std::uint8_t, 4> color_replace_pixel_rgba8(std::array<std::uint8_t, 4> original,
                                                                    EditColor foreground, ColorReplaceMode mode,
                                                                    float weight) noexcept;
// The same on deep-scale values in `domain` (16 and 32-bit pixels); `paint` is already in it.
[[nodiscard]] std::array<float, 4> color_replace_pixel_deep(std::array<float, 4> original, DeepChannels paint,
                                                            ColorReplaceMode mode, float weight,
                                                            DeepDomain domain) noexcept;

// One stroke of the tool on one pixel buffer. Holds the stroke-start copy of the buffer
// (copy-on-write, so it costs nothing until a dab writes), the sample for Once and Background
// Swatch, and the largest weight each pixel has taken, so overlapping dabs never compound:
// every write is the stroke-start pixel repainted at its largest weight so far.
class ColorReplaceStroke {
 public:
  // Selection coverage (0..1) of a buffer pixel; empty means everything is selected.
  using SelectionCoverage = std::function<float(std::int32_t, std::int32_t)>;

  // `foreground_gain` scales the paint in linear light on a 32-bit buffer (the foreground
  // swatch's Intensity); 1 otherwise.
  ColorReplaceStroke(const PixelBuffer& source, const ColorReplaceOptions& options, ColorReplaceSampling sampling,
                     EditColor foreground, EditColor background, float foreground_gain = 1.0F);

  // Indexed documents: weights under the context's coverage threshold write nothing, the rest
  // paint in full, and every written pixel snaps to the palette. Not owned; may be null.
  void set_palette_snap(const PaletteSnapContext* palette) noexcept { palette_ = palette; }

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
  [[nodiscard]] const std::optional<ColorReplaceRgb>& sample() const noexcept { return sample_; }

 private:
  static constexpr std::int32_t kTileShift = 6;
  static constexpr std::int32_t kTileSize = 1 << kTileShift;
  using Tile = std::array<float, static_cast<std::size_t>(kTileSize) * kTileSize>;

  [[nodiscard]] float& cap_at(std::int32_t x, std::int32_t y);

  PixelBuffer source_;
  ColorReplaceOptions options_;
  ColorReplaceSampling sampling_;
  EditColor foreground_;
  DeepChannels deep_paint_{};
  DeepDomain domain_{DeepDomain::Encoded};
  std::optional<ColorReplaceRgb> sample_;
  const PaletteSnapContext* palette_{nullptr};
  std::unordered_map<std::uint64_t, std::unique_ptr<Tile>> caps_;
  std::vector<float> clipped_footprint_;
};

}  // namespace patchy
