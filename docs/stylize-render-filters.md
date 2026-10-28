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
`std::*_distribution`, no trigonometric or exponential calls; whole-degree angles go through
`degrees_unit_vector`, fixed Taylor series), so the pinned FNV-1a digests in
`filter_issue_496_outputs_are_deterministic_and_pinned` and
`filter_issue_496_wind_angles_and_flame_presets_are_pinned` hold on every toolchain. Sizes are
pixels where a parameter says px (scaled for previews), otherwise fractions of the buffer.

- **Wind** (method Wind, Blast, Stagger; direction; angle 0 to 360 degrees with the angle dial,
  0 blowing right, 90 up; length px). `direction` is the version-1 key: Angle (the default)
  reads `angle`; From the Right and From the Left, which old recipes and Looks carry, render 180
  and 0 degrees, the original row pass byte for byte. Pixels are gathered into lines along the
  wind, one pixel per step of the major axis (rows at 0 and 180 degrees); each line, scanned
  downwind, keeps per premultiplied channel and alpha the larger of the pixel and a carried
  value that falls linearly over the line's streak length (per pixel of travel, so diagonal
  streaks are as long), so bright and opaque content smears downwind. Wind streaks about two
  lines in three by varied lengths; Blast every line, two passes, 1.5 to 3 times longer;
  Stagger shifts bands of 1 to 6 lines by up to the length first.
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
- **Flame**: see below. Fills the canvas, so it works on an empty layer; with a selection only
  the selected part shows.

## Flame

Rewritten in the October 2026 follow-up to issue 496 (the first version drew one noise field
per column). Keys: `colors` (the preset; its first four tokens are the version-1 values:
`classic` is Campfire, `blue` Blue Gas, `green` Green Magic, `colors` Foreground to
Background), `mode` (`base_line`, `follow_path`), `rise` (`up`, `from_path`), length percent
of the buffer height, width percent of its width (Base Line), turbulence, flicker size,
density, lean (-60 to 60 degrees), core brightness, glow, smoke, sparks, opacity, seed (with
Randomize) and the base center crosshair (Base Line). The percentages scale the preset's own
amounts. Old recipes still open; their pixels changed with the renderer and the digests were
re-pinned deliberately.

- Presets (Patchy's own looks): Campfire, Candle (one tongue with a blue root), Torch,
  Wildfire, Fire Wall, Fire Line (low and wide), Embers (short, many sparks), Smoky Fire, Soft
  Glow, Blue Gas, Green Magic, Purple Spirit, Cold Flame and Foreground to Background. Each
  sets a color ramp (heat 0 at the edge to 1 at the core), a core and root color, a smoke
  color, the tongue width over height, height spread, density, turbulence, flicker and the
  glow, smoke and spark amounts (`flame_style`).
- Tongues: roots every `0.45 * tongue width / density` pixels with a hashed jitter, along the
  base line (heights fall off toward its ends) or along every guide polyline. A tongue rises
  straight up (rotated by the lean) or, with Away from the Path, along the path normal that
  faces up, nudged upward. Each is a teardrop heat field (`tongue_profile`: widest a quarter of
  the way up), swayed sideways by a broad noise field growing with height and eroded by a fine,
  vertically stretched, domain-warped noise field, both shared by all tongues so neighbours
  move together. A pixel's heat is its hottest tongue plus 0.15 of the rest, so overlaps glow
  without washing out.
- Color and compositing, premultiplied: smoke (above the tips, its own noise) over the layer,
  then glow (the flame light box-blurred three times on a quarter-resolution grid) screened as
  light, then the flame (ramp color, the core color above heat 0.84, the root tint, coverage a
  smoothstep of heat), then sparks (soft dots with a short trail, hashed above the tongues).
  Edges stay ramp-colored at low alpha, so there is no dark fringe on an empty layer.
- Follow Path: the UI flattens the Paths panel's selected path (`stroke_polylines_for_path`)
  into `FilterInvocation::guide_paths` in the filter buffer's pixels, like the captured
  colors: `FilterRegistry::normalize` keeps them, `scale` scales them with the preview proxy,
  Last Filter re-reads the current path, and recipes and Saved Looks keep none. Without a path
  the dialog shows `filterDialogNotice` and OK refuses with a status message; the kernel itself
  draws nothing. Patchy has no Type > Create Work Path: to burn text, Ctrl-click the text
  layer's thumbnail and use Make Work Path from Selection.
- Cost: the noise fields cover only the box the tongues, smoke and sparks reach; the dialog
  preview runs on the bounded proxy.

## Bit depth

None is in the deep lists: 16-bit layers run them on an 8-bit copy, 32-bit layers do not offer
them ([high-bit-depth.md](high-bit-depth.md)).
