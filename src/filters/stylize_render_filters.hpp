#pragma once

#include "core/layer.hpp"
#include "filters/filter_registry.hpp"

#include <cstdint>

namespace patchy {

// Patchy's own Stylize and Render kernels (GitHub issue 496): Wind, Tiles,
// Extrude, Lens Flare and Flame. Each works in place on an RGBA8 buffer of the
// layer (or the document-wide embed of a canvas-filling filter), uses only
// integer math or + - * / and sqrt on doubles, and draws its randomness from
// splitmix64, so a render is the same on every toolchain. Design notes:
// docs/stylize-render-filters.md; the boundary is in
// docs/legal-constraints.md.

enum class WindMethod : std::uint8_t { Wind, Blast, Stagger };

// Streaks bright and opaque content downwind along each row. `from_left`
// blows toward the right; `length` is the longest streak in pixels.
void render_wind(PixelBuffer& rgba, WindMethod method, bool from_left, int length,
                 const FilterProgress* progress = nullptr);

enum class TilesFill : std::uint8_t { Background, Foreground, Inverse, Unaltered, Transparent };

// Cuts the buffer into square tiles (`count` along the shorter side) and moves
// each by up to `max_offset_percent` of a tile; the gaps show `fill`.
void render_tiles(PixelBuffer& rgba, int count, int max_offset_percent, TilesFill fill,
                  RgbColor foreground, RgbColor background, int seed,
                  const FilterProgress* progress = nullptr);

// Raises square cells toward the viewer: each block's front face moves out
// from the buffer center by up to `depth` pixels, drawn over shaded sides.
void render_extrude(PixelBuffer& rgba, bool pyramids, int size, int depth, bool level_based,
                    bool solid_front, int seed, const FilterProgress* progress = nullptr);

enum class LensFlareStyle : std::uint8_t { Classic, Star, Ring, Anamorphic };

// Screens a flare (core glow, rays, halo ring and ghost circles on the line
// through the buffer center) over the buffer; transparent pixels take the
// light with matching alpha.
void render_lens_flare(PixelBuffer& rgba, double center_x_percent, double center_y_percent,
                       int brightness_percent, LensFlareStyle style,
                       const FilterProgress* progress = nullptr);

enum class FlamePalette : std::uint8_t { Classic, Blue, Green, Colors };

// Procedural flames rising from a base line centered on (base_x, base_y)
// percent, `width_percent` of the buffer wide, `length_percent` of its height
// tall, composited over the buffer.
void render_flame(PixelBuffer& rgba, double base_x_percent, double base_y_percent,
                  int length_percent, int width_percent, int turbulence, FlamePalette palette,
                  RgbColor foreground, RgbColor background, int seed,
                  const FilterProgress* progress = nullptr);

}  // namespace patchy
