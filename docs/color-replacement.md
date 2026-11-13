# Color Replacement tool

A brush that repaints pixels close to a sampled color with the foreground color (GitHub issue 720). It shares the Brush button's flyout (Brush / Color Replacement, Shift+B cycles; the tool itself ships without a key). Hotkey command id `tools.color_replacement`, cycle command `tools.cycle.brush`; both permanent. The binding limits are in [legal-constraints-selection.md](legal-constraints-selection.md#color-replacement-tool); read that entry before changing how the match or the paint works. Brush engine: [brushes.md](brushes.md). Blend math: [blend-modes.md](blend-modes.md).

## What a stroke does

- A stroke is a row of dabs a quarter of the brush diameter apart, placed by distance (the first at the press), so the pointer's event rate does not matter. Shift+click joins the last stroke's end with a line, Alt+click picks the foreground color, Alt+right-drag sets Size and Soft, as on the Brush.
- Each dab compares the layer's **stroke-start** pixels in the tip's box with one sampled color, and paints the foreground color over the ones that match. The match and the paint always read the stroke-start pixels, so a dab that lands on pixels an earlier dab of the same stroke already repainted still sees the color that was there. Overlapping dabs never compound: every pixel keeps the largest weight any dab gave it, and each write is the stroke-start pixel repainted at that weight (`ColorReplaceStroke`).
- Alpha never changes. Fully transparent pixels are skipped; partly transparent ones are recolored like opaque ones. Lock Transparent Pixels therefore changes nothing; Lock Image Pixels refuses the stroke like any paint tool.
- The selection limits the change (its coverage scales the weight), pixels of the layer outside the canvas are left alone, and the layer never grows.
- One undo step per stroke ("Color replacement").
- The tool reads and writes the active pixel layer only. Text, shape and Smart Object layers go through the same rasterize prompt or refusal as the toning brushes (it shares their press path, `is_local_adjustment_tool` in canvas_widget_events.cpp). It refuses a layer mask, a channel, Quick Mask and a Smart Filter mask with a status message.

## Options

Options bar order: Tip, Size | Mode | Soft | Sampling, Limits | Tolerance, Anti-alias. Tip, Size and Soft are the Brush family's shared controls and values. There is no Opacity or Flow: a matching pixel under a full-coverage tip is replaced in full.

- **Tip**: the Brush's tip picker. Bitmap tips, the procedural Round and Square, and the tip shape's angle and roundness all apply (`brush_dab_coverage`); brush dynamics do not (each dab is the static tip). A pen's pressure sets the size when the global pen setting does.
- **Mode** (`colorReplaceModeCombo`): Hue, Saturation, Color (default), Luminosity. The paint is `blend_rgb` / `blend_rgb_deep` of the foreground over the stroke-start pixel with the layer blend mode of that name, mixed in by the dab weight. No formulas of its own.
- **Sampling** (`colorReplaceSamplingCombo`): which color is compared. Continuous (default) reads the stroke-start pixel under the center of every dab; Once keeps the pixel under the first dab that lands on an opaque pixel; Background Swatch uses the background color.
- **Limits** (`colorReplaceLimitsCombo`): which matching pixels of the dab change. Discontiguous: all of them. Contiguous (default): those joined to the dab's center through fully matching pixels (4-connected, through the whole box, so a tip with gaps still joins up); nothing when the center pixel itself does not match. Find Edges: Contiguous, but the area also stops where two neighbouring pixels differ by more than half the tolerance distance (an abrupt step), and it gets no anti-alias fringe, so a sharp edge inside the tolerance stays sharp.
- **Tolerance** (`colorReplaceToleranceSpin`, 0..100%, default 30): the Magic Wand's metric on display RGB. The percent is the accepted color distance as a share of one channel's full range (30% is a distance of 76); the wand accepts twice its tolerance, so the percent maps to wand tolerance percent x 255 / 200 (`color_replace_wand_tolerance`: 30% is wand 38, near the wand's default 32), and a pixel matches when the sum of squared channel differences is within 4 x tolerance squared, exactly what `color_within_tolerance` accepts on three channels. 0% is an exact match, and 100% takes every color. The scale is Patchy's own choice, not calibrated against another editor.
- **Anti-alias** (`colorReplaceAntiAliasCheck`, default on): a pixel just past the tolerance distance is partly replaced, falling in a straight line to nothing a quarter of that distance further. With Contiguous only such pixels touching the joined area count.

Settings keys (permanent): `tools/colorReplaceMode` (`hue`, `saturation`, `color`, `luminosity`), `tools/colorReplaceSampling` (`continuous`, `once`, `background`), `tools/colorReplaceLimits` (`discontiguous`, `contiguous`, `findEdges`), `tools/colorReplaceTolerance`, `tools/colorReplaceAntiAlias`. An unknown token or an unreadable number is the default. The enums are append-only.

## Bit depth, palettes, CMYK

- 16-bit layers are painted at depth through `load_pixel` / `store_pixel` (no byte rounding). The match compares display-encoded values on the 0..255 scale.
- 32-bit layers are linear: the match compares the sRGB-encoded color (what the user sees), and the paint is the linear-light blend the Brush's Mode uses at 32 bits, with the foreground swatch's Intensity as its gain.
- Palette documents: a weight under the palette's coverage threshold writes nothing, the rest paint in full, and the result snaps to the palette (RGB only, alpha kept).
- CMYK documents paint their RGB pixels like every brush ([cmyk.md](cmyk.md)).

## Where it lives

- `src/core/color_replace.{hpp,cpp}`: the tokens, the match, one dab's weights (`color_replace_dab_weights`), the per-pixel paint and `ColorReplaceStroke` (stroke-start copy, sample, per-pixel largest weight in 64 px tiles). All work is bounded to the dab's box; nothing runs per repaint.
- `src/ui/canvas_widget_color_replace.cpp`: the stroke segment (`color_replace_brush_segment`, reached from `local_adjustment_brush_segment`) and the script stroke. The stroke object lives in `color_replace_stroke_` and is dropped by `clear_brush_stroke_tracking`.
- Options bar and settings: `main_window_actions_options_bar.cpp`, `main_window_tool_options.cpp` (`apply_color_replace_settings`, `sync_color_replace_controls`).
- Icon: `src/ui/icons/tool-color-replace.svg`, Patchy's own drawing (a brush handle over an outlined disc and a filled accent disc).

## Scripting

`layer.replaceColor({points, color, size?, hardness?, mode?, sampling?, backgroundColor?, limits?, tolerance?, antiAlias?})` paints one stroke with a procedural round tip on the document's active layer and returns the changed bounds `{x, y, width, height}` (all zero when no pixel matched). It never changes the artist's tool or options. Tokens are the settings tokens above. Strict: an unknown key, a wrong type or an out-of-range value throws before anything is painted; `sampling: "background"` needs `backgroundColor`.

## Deliberate differences and limits

- Sampling reads the stroke-start pixels, never pixels the stroke already changed. A later stroke does see the earlier strokes' result, so with Continuous a spot no dab centered on (a bright highlight, a rim sliver) is finished by a click on it, not by dragging past it again.
- Dabs are placed by distance from the press; the end of a drag gets no extra dab.
- Find Edges is Patchy's own rule (a neighbour step limit), not a measured copy of another editor's.
- No Smoothing, no brush dynamics, no Opacity or Flow.
- It samples the active layer only (no Sample All Layers).

## Tests

Core `color_replace_*` (tests/core/color_replace_tests.cpp): the tolerance against `color_within_tolerance`, the tokens, each Mode against `blend_rgb`, each Limits and Sampling choice, alpha, selection, overlapping dabs, anti-alias, 16 and 32 bits, clipping and tips with gaps. UI `ui_color_replace_*` and `ui_script_replace_color_strokes_and_validation` (tests/ui/color_replace_tests.cpp): the flyout, the options bar, a stroke with undo, selection, alpha and locks, settings across a restart, a 16-bit layer, the script call.
