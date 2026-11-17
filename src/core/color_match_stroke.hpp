#pragma once

// The match that the Color Replacement tool and the Background Eraser share (GitHub issues 720
// and 727; docs/color-replacement.md, docs/eraser-tools.md).
//
// A stroke is a row of brush dabs. Each dab compares the stroke-start pixels under the tip
// with ONE sampled color (the Magic Wand's metric on display RGB) and optionally keeps only
// the matching pixels connected to the dab's center. This file works out each pixel's weight;
// the tool that owns the stroke writes the pixels at once (a color, or less alpha).
//
// Legal boundary (docs/legal-constraints-selection.md, "Color Replacement tool"): the sample stays one
// point in display RGB with a hard tolerance and a short linear fringe. Never a Lab point or
// area model, a Gaussian or other smoothing function around the sample, histogram or range
// normalization before the comparison, a stored spatial or range mask, or anything that can
// be adjusted after the dab is painted.
//
// The Background Eraser's Protect Foreground Color (set_protected_color; docs/eraser-tools.md,
// decided 2026-11-17) is a second point the owner hands in: a pixel at least as near to it as
// to the sample is left alone. It is a color the user chose (the foreground swatch, a script's
// argument): never a color sampled from the tip's region, never an average or other statistic
// of pixels, and the erase sample stays the one pixel under the dab's center, never a measured
// region of the tip.

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
enum class ColorMatchSampling : std::uint8_t { Continuous, Once, BackgroundSwatch };
enum class ColorMatchLimits : std::uint8_t { Discontiguous, Contiguous, FindEdges };

inline constexpr int kMinColorMatchTolerance = 0;
inline constexpr int kMaxColorMatchTolerance = 100;
inline constexpr int kDefaultColorMatchTolerance = 30;

// "continuous", "once", "background".
[[nodiscard]] std::string_view color_match_sampling_token(ColorMatchSampling sampling) noexcept;
[[nodiscard]] std::optional<ColorMatchSampling> color_match_sampling_from_token(std::string_view token) noexcept;
// "discontiguous", "contiguous", "findEdges".
[[nodiscard]] std::string_view color_match_limits_token(ColorMatchLimits limits) noexcept;
[[nodiscard]] std::optional<ColorMatchLimits> color_match_limits_from_token(std::string_view token) noexcept;

struct ColorMatchOptions {
  ColorMatchLimits limits{ColorMatchLimits::Contiguous};
  int tolerance{kDefaultColorMatchTolerance};  // percent, 0 = exact matches only
  bool anti_alias{true};

  friend bool operator==(const ColorMatchOptions&, const ColorMatchOptions&) = default;
};

// Display-encoded RGB on the 0..255 scale (unrounded for 16 and 32-bit pixels).
using ColorMatchRgb = std::array<float, 3>;

// The Magic Wand tolerance (0..255) a percent stands for. The wand accepts a color distance of
// twice its tolerance, so the percent is that distance as a share of one channel's full range:
// percent * 255 / 200, rounded (30% is wand 38, a distance of 76).
[[nodiscard]] int color_match_wand_tolerance(int tolerance_percent) noexcept;

// How strongly `pixel` matches `sample`. 1 when the Magic Wand at
// color_match_wand_tolerance(tolerance) would accept it (the sum of squared RGB differences
// is within 4 * tolerance^2; tolerance 0 is an exact match), and for every color at 100%.
// With anti_alias the match falls in a straight line to 0 over a further quarter of that
// distance; without it, it is 0.
[[nodiscard]] float color_match_strength(const ColorMatchRgb& pixel, const ColorMatchRgb& sample,
                                         int tolerance_percent, bool anti_alias) noexcept;

// How much of a matching pixel's weight is left when `protected_color` is kept (the Background
// Eraser's Protect Foreground Color). 0 when the pixel is at least as near to the protected
// color as to `sample` (plain RGB distance, the metric above); 1 when it is nearer to the
// sample by a quarter of the tolerance distance or more (the anti-alias fringe's width); a
// straight line between. Without anti_alias, or at tolerance 0, the step is hard.
[[nodiscard]] float color_match_protection_scale(const ColorMatchRgb& pixel, const ColorMatchRgb& sample,
                                                 const ColorMatchRgb& protected_color, int tolerance_percent,
                                                 bool anti_alias) noexcept;

// One pixel of a color buffer (3 or 4 channels, any depth) as display-encoded RGB, and its
// alpha as 0..1 (1 without an alpha channel). A 32-bit linear pixel is sRGB-encoded first, so
// the comparison sees the color as it looks.
struct ColorMatchPixel {
  ColorMatchRgb rgb{};
  float alpha{0.0F};
};
[[nodiscard]] ColorMatchPixel color_match_pixel_at(const PixelBuffer& pixels, std::int32_t x, std::int32_t y);

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
[[nodiscard]] std::vector<float> color_match_dab_weights(const PixelBuffer& source, Rect rect, std::int32_t seed_x,
                                                         std::int32_t seed_y, const ColorMatchRgb& sample,
                                                         const ColorMatchOptions& options,
                                                         std::span<const float> footprint);

// A procedural round tip's box and coverage around (center_x, center_y) (core brush_coverage):
// `softness` is 0..100 as on the options bar. (hot_x, hot_y) is the pixel under the center.
struct ColorMatchRoundTip {
  Rect rect;
  std::int32_t hot_x{0};
  std::int32_t hot_y{0};
  std::vector<float> footprint;
};
[[nodiscard]] ColorMatchRoundTip color_match_round_tip(double center_x, double center_y, int diameter, int softness);

// One stroke's match state on one pixel buffer: the stroke-start copy of the buffer
// (copy-on-write, so it costs nothing until a dab writes), the sample for Once and Background
// Swatch, and the largest weight each pixel has taken, so overlapping dabs never compound:
// the owner writes each pixel from its stroke-start value at its largest weight so far.
class ColorMatchStroke {
 public:
  // Selection coverage (0..1) of a buffer pixel; empty means everything is selected.
  using SelectionCoverage = std::function<float(std::int32_t, std::int32_t)>;

  ColorMatchStroke(const PixelBuffer& source, const ColorMatchOptions& options, ColorMatchSampling sampling,
                   EditColor background);

  // Indexed documents: weights under the context's coverage threshold are dropped and the rest
  // become 1 (the owner snaps what it writes). Not owned; may be null.
  void set_palette_snap(const PaletteSnapContext* palette) noexcept { palette_ = palette; }
  [[nodiscard]] const PaletteSnapContext* palette_snap() const noexcept { return palette_; }

  // The stroke-start pixels: what every dab compares and what the owner repaints from.
  [[nodiscard]] const PixelBuffer& source() const noexcept { return source_; }
  [[nodiscard]] const ColorMatchOptions& options() const noexcept { return options_; }

  // One dab. `rect` is the tip's box in buffer coordinates and `footprint` its coverage,
  // row-major; the part outside the buffer is ignored. (center_x, center_y) is the hot spot:
  // where Continuous samples and where the contiguous limits start. The result is the clipped
  // box and, row-major over it, the weight to write each pixel at: 0 for a pixel that does not
  // match, is not selected, or already took at least that weight from an earlier dab. The
  // weights stay valid until the next dab. An empty rect means the dab did nothing.
  struct Dab {
    Rect rect;
    std::span<const float> weights;
  };
  [[nodiscard]] Dab dab(std::int32_t center_x, std::int32_t center_y, Rect rect, std::span<const float> footprint,
                        const SelectionCoverage& selection = {});

  // The color the stroke compares against, once it has one (Continuous: the last dab's).
  [[nodiscard]] const std::optional<ColorMatchRgb>& sample() const noexcept { return sample_; }

  // A color the stroke leaves alone (the Background Eraser's Protect Foreground Color): every
  // weight is scaled by color_match_protection_scale against the dab's sample, so a pixel at
  // least as near to this color as to the sample is never written. The owner names the color;
  // nothing here reads it from the image. Empty (the default) protects nothing.
  void set_protected_color(std::optional<ColorMatchRgb> color) noexcept { protected_color_ = color; }
  [[nodiscard]] const std::optional<ColorMatchRgb>& protected_color() const noexcept { return protected_color_; }

 private:
  static constexpr std::int32_t kTileShift = 6;
  static constexpr std::int32_t kTileSize = 1 << kTileShift;
  using Tile = std::array<float, static_cast<std::size_t>(kTileSize) * kTileSize>;

  [[nodiscard]] float& cap_at(std::int32_t x, std::int32_t y);

  PixelBuffer source_;
  ColorMatchOptions options_;
  ColorMatchSampling sampling_;
  std::optional<ColorMatchRgb> sample_;
  std::optional<ColorMatchRgb> protected_color_;
  const PaletteSnapContext* palette_{nullptr};
  std::unordered_map<std::uint64_t, std::unique_ptr<Tile>> caps_;
  std::vector<float> clipped_footprint_;
  std::vector<float> weights_;
};

}  // namespace patchy
