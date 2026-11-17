#pragma once

// The Background Eraser's fixed rules (GitHub issue 727, docs/eraser-tools.md).
//
// A stroke is a row of brush dabs. Each dab compares the stroke-start pixels under the tip
// with ONE sampled color (the Magic Wand's metric on display RGB), optionally keeps only the
// matching pixels connected to the dab's center, and erases those pixels at once. The match,
// the Sampling choices and the Limits are core/color_match_stroke.hpp, shared with the Color
// Replacement tool; this tool lowers alpha where that one paints a color.
//
// Legal boundary (docs/legal-constraints-selection.md, "Color Replacement tool": the Background Eraser
// rests on the same decision): the tool stays in the form published in 1999. One sampled point
// in display RGB with a hard tolerance and a short linear fringe; the pixels erased as the dab
// lands. Never a Lab point or area model, a Gaussian or other smoothing function around the
// sample, histogram or range normalization, a stored spatial or range mask, an erase that can
// be adjusted afterwards, or a color correction of the pixels that stay (no edge color
// decontamination). Protect Foreground Color, part of that 1999 form (reviewed 2026-11-17), is
// a second color the caller names (set_protected_color): the foreground swatch or a script's
// argument, never a color sampled from the tip's region or a statistic of pixels.

#include "core/color_match_stroke.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_tools.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace patchy {

struct BackgroundEraserOptions {
  ColorMatchLimits limits{ColorMatchLimits::Contiguous};
  int tolerance{kDefaultColorMatchTolerance};  // percent, 0 = exact matches only
  // Protect Foreground Color: the tool's owner hands each stroke the foreground color
  // (BackgroundEraserStroke::set_protected_color) while this is on.
  bool protect_foreground{false};

  // The shared match's options: the fringe past the tolerance is always on, so the erased
  // area's edge is soft (Find Edges gives none, as in the shared rule).
  [[nodiscard]] ColorMatchOptions match() const noexcept { return {limits, tolerance, true}; }

  friend bool operator==(const BackgroundEraserOptions&, const BackgroundEraserOptions&) = default;
};

// One stroke-start pixel erased at `weight` (0..1). With an alpha channel and no transparency
// lock the alpha falls to alpha x (1 - weight) and the color stays. Otherwise (a locked
// Background, Lock Transparent Pixels, a layer without alpha) the color moves toward
// `background` by weight and the alpha stays: the Eraser's own rule (docs/tools.md).
[[nodiscard]] std::array<std::uint8_t, 4> background_eraser_pixel_rgba8(std::array<std::uint8_t, 4> original,
                                                                        float weight, bool to_background_color,
                                                                        EditColor background) noexcept;
// The same on deep-scale values (16 and 32-bit pixels); `background` is already in the
// buffer's domain.
[[nodiscard]] std::array<float, 4> background_eraser_pixel_deep(std::array<float, 4> original, float weight,
                                                                bool to_background_color,
                                                                std::array<float, 3> background) noexcept;

// One stroke of the tool on one pixel buffer. The shared ColorMatchStroke holds the
// stroke-start copy of the buffer, the sample and the largest weight each pixel has taken, so
// overlapping dabs never compound: every write is the stroke-start pixel erased at its largest
// weight so far.
class BackgroundEraserStroke {
 public:
  using SelectionCoverage = ColorMatchStroke::SelectionCoverage;

  // `lock_transparent_pixels` (or a buffer without alpha) erases to `background` and keeps
  // alpha. A caller that wants transparency on a layer without alpha gives it alpha first
  // (ensure_alpha_for_erase). `background_gain` scales the background color in linear light on
  // a 32-bit buffer (the background swatch's Intensity); 1 otherwise.
  BackgroundEraserStroke(const PixelBuffer& source, const BackgroundEraserOptions& options,
                         ColorMatchSampling sampling, EditColor background, bool lock_transparent_pixels,
                         float background_gain = 1.0F);

  // Indexed documents: weights under the context's coverage threshold write nothing, the rest
  // erase in full, and a pixel erased to the background color snaps to the palette. Not owned.
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
  [[nodiscard]] const std::optional<ColorMatchRgb>& sample() const noexcept { return match_.sample(); }

  // Protect Foreground Color: a pixel at least as near to `color` (display RGB; its alpha is
  // ignored) as to the sampled color is not erased (color_match_protection_scale). Set it
  // before the first dab; empty protects nothing.
  void set_protected_color(std::optional<EditColor> color) noexcept;

 private:
  ColorMatchStroke match_;
  EditColor background_;
  std::array<float, 3> deep_background_{};
  bool to_background_color_{false};
};

}  // namespace patchy
