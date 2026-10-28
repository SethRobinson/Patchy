#pragma once

#include "core/layer.hpp"
#include "filters/filter_registry.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace patchy {

// Patchy's own Stylize and Render kernels (GitHub issue 496): Wind, Tiles,
// Extrude, Lens Flare and Flame. Each works in place on an RGBA8 buffer of the
// layer (or the document-wide embed of a canvas-filling filter), uses only
// integer math or + - * / and sqrt on doubles, and draws its randomness from
// splitmix64, so a render is the same on every toolchain. Design notes:
// docs/stylize-render-filters.md; the boundary is in
// docs/legal-constraints.md.

enum class WindMethod : std::uint8_t { Wind, Blast, Stagger };

// Streaks bright and opaque content downwind. The wind blows toward
// `angle_degrees` (0 to the right, 90 up; 180 is the old "from the right",
// 0 "from the left", both byte for byte); `length` is the longest streak in
// pixels.
void render_wind(PixelBuffer& rgba, WindMethod method, int angle_degrees, int length,
                 const FilterProgress* progress = nullptr);

// cos and sin of whole degrees by fixed series (no libm), the same everywhere.
[[nodiscard]] std::array<double, 2> degrees_unit_vector(int degrees) noexcept;

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

// Flame's looks (docs/stylize-render-filters.md); each sets the colors, the
// tongue shape and the glow, smoke and spark amounts the dialog scales.
enum class FlamePreset : std::uint8_t {
  Campfire,
  Candle,
  Torch,
  Wildfire,
  FireWall,
  FireLine,
  Embers,
  SmokyFire,
  SoftGlow,
  BlueGas,
  GreenMagic,
  PurpleSpirit,
  ColdFlame,
  Colors,
};

struct FlameSettings {
  FlamePreset preset{FlamePreset::Campfire};
  // Follow Path: tongues root along `guides` instead of the base line.
  bool follow_path{false};
  // Follow Path only: rise away from the path instead of straight up.
  bool rise_from_path{false};
  double base_x_percent{50.0};
  double base_y_percent{100.0};
  int length_percent{40};
  int width_percent{60};
  int turbulence{50};
  int flicker_percent{100};
  int density_percent{100};
  int lean_degrees{0};
  int core_percent{100};
  int glow_percent{100};
  int smoke_percent{100};
  int sparks_percent{100};
  int opacity_percent{100};
  int seed{1};
  RgbColor foreground{};
  RgbColor background{255, 255, 255};
  std::vector<FilterGuidePolyline> guides;
};

// Procedural flames: tongues rooted along a base line (centered on base x/y
// percent, `width_percent` of the buffer wide) or along guide polylines, each
// a teardrop heat field broken up by shared domain-warped noise, colored by
// the preset's ramp with a hot core, over soft glow and smoke, with sparks;
// composited over the buffer (an empty buffer gets the fire with its alpha).
void render_flame(PixelBuffer& rgba, const FlameSettings& settings,
                  const FilterProgress* progress = nullptr);

}  // namespace patchy
