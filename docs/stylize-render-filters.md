# Stylize and Render filters

The Filter > Stylize and Filter > Render commands added for GitHub issue 496. Catalog contracts
(IDs, keys, categories) are in [filters.md](filters.md); this page holds what each filter does.
Kernels: `src/filters/stylize_render_filters.{hpp,cpp}` (Patchy's own) and the native Smart
Filter kernels the others reuse. Tests: `filter_issue_496_*` (core), `ui_issue_496_*` (UI).

## Menus

Stylize lists its entries alphabetically by English name (`main_window_actions_menus.cpp` sorts
that one submenu); every other submenu, and the Filter Gallery, keep catalog order. Render was
already alphabetical: Clouds, Difference Clouds, Fibers, Flame, Lens Flare. The new commands are
catalog filters, so they also appear in the Filter Gallery, Last Filter, recipes, Saved Looks
and script `applyFilter`, and they are not HotkeyRegistry commands.

## Direct commands over native Smart Filter kernels

Solarize, Find Edges, Trace Contour, Diffuse and Fibers run the table-described native kernels
([smart-filters-distort-stylize.md](smart-filters-distort-stylize.md),
[smart-filters-pixelate-render.md](smart-filters-pixelate-render.md)) with the layer buffer as
the filter canvas (`apply_native_table_filter` in `filter_engine.cpp`). Their catalog keys are
the native fields' parameter keys, read by `schema_smart_filter_from_invocation` and
`table_smart_filter_from_catalog_invocation` (`smart_filter_recipe_mapping`), so the direct
result is the Smart Filter's result over the same pixels. `native_smart_filter_kind_for` maps
them: on a Smart Object the command opens the Smart Filter dialog and appends a native entry,
and a Gallery recipe of them maps entry by entry (an SF badge). Solarize and Find Edges have no
settings and apply at once. Trace Contour and Find Edges advertise one pixel of support;
Diffuse hashes document positions, so it advertises none. Fibers fills the canvas.

## Patchy's own filters

Patchy's designs from long-public techniques, never another editor's lens or flame presets or dialog layouts ([legal-constraints.md](legal-constraints.md)). None maps to a Photoshop Smart Filter: on a Smart Object they reach the rasterize prompt. All
use integer math or `+ - * /` and `sqrt` on doubles and draw randomness from splitmix64 (no
`std::*_distribution`, no trigonometric or exponential calls), so the pinned FNV-1a digests in
`filter_issue_496_outputs_are_deterministic_and_pinned` hold on every toolchain. Sizes are
pixels where a parameter says px (scaled for previews), otherwise fractions of the buffer.

- **Wind** (method Wind, Blast, Stagger; direction from the right or left; length px). Each row,
  scanned downwind, keeps per premultiplied channel and alpha the larger of the pixel and a
  carried value that falls linearly over the row's streak length, so bright and opaque content
  smears downwind. Wind streaks about two rows in three by varied lengths; Blast every row, two
  passes, 1.5 to 3 times longer; Stagger shifts bands of 1 to 6 rows by up to the length first.
- **Tiles** (count along the shorter side, maximum offset percent of a tile, gap fill, seed).
  Square tiles from the buffer origin, each moved by a hashed offset; later tiles cover earlier
  ones. Gaps show the background or foreground color, the inverted or unaltered image, or
  transparency.
- **Extrude** (blocks or pyramids, size px, depth px, random or level-based heights, solid
  front faces, seed). Cells of `size` get a height in [0, 1] (hashed, or luminance times
  alpha); lower ones draw first. Each front face moves away from the buffer center by up to
  `depth` pixels at the corners, over its sides swept from the base and shaded 0.4 to 0.75.
  Fronts show the cell's pixels, its average color, or for pyramids four triangles lit from
  the upper left.
- **Difference Clouds** (Clouds' keys). Clouds' exact pixels blended with Difference over the
  layer; transparent pixels show the clouds, so the result is opaque. Over opaque black it is
  Clouds byte for byte. Fills the canvas.
- **Lens Flare** (flare type, brightness 10 to 300 %, center; the center is the dialog's
  draggable crosshair). Light is the sum of a core glow and a wide soft glow (`1 / (1 + d^2 /
  r^2)`), thin rays from `cos(n theta)` raised to the 32nd power with lengths varied by a fixed
  third harmonic (angle multiples by complex powers of the unit vector), a halo ring with a
  slightly wider red and narrower blue radius, and ghost disks with brighter rims along the
  line from the flare through the buffer center. Types: Classic (10 rays, ring, five ghosts),
  Star (16 strong rays), Halo Ring (no rays, strong ring), Wide Streak (a horizontal blue
  streak). The light is screened over the layer as if over black; a transparent pixel takes
  the light as color with alpha `a + (1 - a) max(light)`. Fills the canvas.
- **Flame** (colors, length percent of the height, width percent of the width, turbulence,
  seed, base center; the base is the crosshair, default bottom center). Tongue width is 7 % of
  the flame height. Per pixel: sideways sway from value noise growing with height; per-column
  reach from 1D noise (squared, so licks vary from short to tall) times a parabolic envelope;
  heat `1 - height / reach` broken up by vertically stretched noise scaled by turbulence; the
  base sides cool off and the flame fades just below the base line. Heat picks a palette color
  (Classic Fire, Blue Gas, Green, or Foreground to Background) and coverage, composited Normal
  over the layer. Fills the canvas, so it works on an empty layer; with a selection only the
  selected part shows.

## Bit depth

None is in the deep lists: 16-bit layers run them on an 8-bit copy, 32-bit layers do not offer
them ([high-bit-depth.md](high-bit-depth.md)).
