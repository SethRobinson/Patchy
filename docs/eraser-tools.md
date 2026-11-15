# Background Eraser and Magic Eraser

Two tools in the Eraser button's flyout (Eraser / Background Eraser / Magic Eraser, Shift+E cycles; the two new tools ship without a key of their own), GitHub issue 727. Hotkey command ids `tools.background_eraser`, `tools.magic_eraser` and the cycle `tools.cycle.eraser`; all permanent. The Background Eraser shares its match with the Color Replacement tool ([color-replacement.md](color-replacement.md)) and rests on the same decision: read the "Color Replacement tool" entry in [legal-constraints-selection.md](legal-constraints-selection.md#color-replacement-tool) before changing how it matches or erases. The plain Eraser and the transparency-lock rule are in [tools.md](tools.md); the flyout in [tool-flyouts.md](tool-flyouts.md).

## Background Eraser

A brush that erases the pixels close to a sampled color and leaves the rest under the tip alone.

- A stroke is the Color Replacement tool's stroke: dabs a quarter of the brush diameter apart, placed by distance (the first at the press), Shift+click joins the last stroke's end with a line, Alt+right-drag sets Size and Soft. Each dab compares the layer's **stroke-start** pixels in the tip's box with one sampled color and erases the ones that match. Overlapping dabs never compound: every pixel keeps the largest weight any dab gave it, and each write is the stroke-start pixel erased at that weight (`BackgroundEraserStroke`).
- The erase lowers alpha to alpha x (1 - weight) and leaves the color. Nothing else about the pixel changes: there is no color correction of the pixels that stay (no edge color decontamination).
- The selection limits the erase (its coverage scales the weight), pixels of the layer outside the canvas are left alone, and the layer never grows.
- One undo step per stroke ("Background Eraser").
- It reads and writes the active pixel layer only (no Sample All Layers). Text, shape and Smart Object layers go through the same rasterize prompt or refusal as the toning brushes (it shares their press path, `is_local_adjustment_tool`). It refuses a layer mask, a channel, Quick Mask and a Smart Filter mask with a status message. Alt does nothing on it (no color pick, no opposite tool).

Options bar order: Tip, Size | Soft | Sampling, Limits | Tolerance. Tip, Size and Soft are the **Eraser's** own set (`tools/eraserSize`, `tools/eraserSoftness`): the two brushes of the flyout share them, and the Brush keeps its own. There is no Opacity or Flow.

- **Sampling** (`backgroundEraserSamplingCombo`): Continuous (default) reads the stroke-start pixel under the center of every dab; Once keeps the pixel under the first dab that lands on an opaque pixel; Background Swatch uses the background color.
- **Limits** (`backgroundEraserLimitsCombo`): Discontiguous, Contiguous (default) or Find Edges, with the Color Replacement tool's rules.
- **Tolerance** (`backgroundEraserToleranceSpin`, 0..100%, default 30): the Color Replacement tool's scale, the Magic Wand's metric on display RGB (`color_match_wand_tolerance`).
- The edge is always soft: a pixel just past the tolerance distance is partly erased, falling in a straight line to nothing a quarter of that distance further (the Color Replacement tool's Anti-alias, always on; Find Edges gives none).

Settings keys (permanent): `tools/backgroundEraserSampling` (`continuous`, `once`, `background`), `tools/backgroundEraserLimits` (`discontiguous`, `contiguous`, `findEdges`), `tools/backgroundEraserTolerance`. An unknown token or an unreadable number is the default.

## Magic Eraser

One click erases the area the Fill tool would flood from that point: the pixels within Tolerance of the clicked color, joined to the click or, with Contiguous off, anywhere on the layer.

- The region comes from the Fill tool's own region pass (`collect_flood_region` and `FloodColorTest` in pixel_tools.cpp), so the same click, Tolerance and Contiguous give the same pixels as a fill. The selection limits it. The layer never grows; pixels outside its buffer are already empty.
- One undo step per click ("Magic Eraser"). A click that changes nothing leaves the layer as it was (a layer without alpha stays without one).
- It refuses a layer mask, a channel, Quick Mask and a Smart Filter mask; text, shape and Smart Object layers are refused like any pixel edit.

Options bar order: Tolerance | Anti-alias, Contiguous, Sample All Layers | Opacity.

- **Tolerance** (`magicEraserToleranceSpin`, 0..255, default 32): the Magic Wand's and the Fill tool's scale.
- **Anti-alias** (`magicEraserAntiAliasCheck`, default on): pixels just past the tolerance (up to a quarter of the tolerance distance further, `FloodColorTest::fringe`) are erased in part. With Contiguous only those touching the region count; with it off, every such pixel.
- **Contiguous** (`magicEraserContiguousCheck`, default on).
- **Sample All Layers** (`magicEraserSampleAllLayersCheck`, default off): the region is found in the document as displayed (the canvas's composite, 8 bits, as the Magic Wand reads it), and the erase is still written to the active layer.
- **Opacity** (`magicEraserOpacitySpin`, `magicEraserOpacitySlider`, 0..100%, default 100): how much of the region's alpha goes. 0% erases nothing.

Settings keys (permanent): `tools/magicEraserTolerance`, `tools/magicEraserAntiAlias`, `tools/magicEraserContiguous`, `tools/magicEraserSampleAllLayers`, `tools/magicEraserOpacity`.

## Background layers and transparency locks

Both tools follow the Eraser's existing rule ([tools.md](tools.md), "Background lock, Eraser and Clear under a transparency lock"); neither converts a Background into a layer.

- With transparency locked (File > New's opaque Background, or Lock Transparent Pixels) the erased pixels move toward the **background color** by the erase weight and keep their alpha. Fully transparent pixels stay empty, and a locked layer without alpha gains none.
- An unlocked layer without alpha (an opened JPEG) gains an alpha channel and erases to transparency (`ensure_layer_alpha_for_erase`).
- Lock Image Pixels refuses the edit like any paint tool.

## Bit depth, palettes, CMYK

- 16 and 32-bit layers are erased at depth (`load_pixel` / `store_pixel`, `write_pixel`): the alpha is not rounded through bytes. The match compares display-encoded values on the 0..255 scale; a 32-bit layer is sRGB-encoded for the comparison.
- Palette documents: the Background Eraser drops a weight under the palette's coverage threshold and erases the rest in full; the Magic Eraser goes through `write_pixel`, which binarizes and snaps the same way.
- CMYK documents edit their RGB pixels like every brush ([cmyk.md](cmyk.md)).

## Where it lives

- `src/core/color_match_stroke.{hpp,cpp}`: the shared match (tolerance, tokens, one dab's weights, `ColorMatchStroke`: stroke-start copy, sample, per-pixel largest weight). `src/core/color_replace.*` and `src/core/background_eraser.*` are the two tools on top of it: one paints a color, the other lowers alpha.
- `src/core/pixel_tools.cpp`: `magic_erase` beside `flood_fill`, both on `collect_flood_region`; `ensure_layer_alpha_for_erase`.
- `src/ui/canvas_widget_color_replace.cpp`: the stroke segment both brushes walk (`color_match_brush_segment`). `src/ui/canvas_widget_erasers.cpp`: the options, the Magic Eraser's click (`magic_erase_pixels`) and the two script calls. The press is in `canvas_widget_events.cpp`.
- Options bar and settings: `main_window_actions_options_bar.cpp`, `main_window_tool_options.cpp` (`apply_eraser_tool_settings`, `sync_eraser_tool_controls`).
- Icons: `src/ui/icons/tool-background-eraser.svg` (an eraser block over two accent squares of a checkerboard) and `tool-magic-eraser.svg` (an eraser block and an accent sparkle), Patchy's own drawings.

## Scripting

- `layer.eraseBackground({points, size?, hardness?, sampling?, backgroundColor?, limits?, tolerance?})` erases one stroke with a procedural round tip on the document's active layer and returns the changed bounds `{x, y, width, height}` (all zero when nothing matched). `sampling: "background"` needs `backgroundColor`, which is also the color a transparency-locked layer is erased to (white when not given).
- `layer.magicErase({x, y, tolerance?, antiAlias?, contiguous?, sampleAllLayers?, opacity?, backgroundColor?})` is one click and returns the changed bounds.
- Both are strict (an unknown key, a wrong type or an out-of-range value throws before anything is erased) and never change the artist's tool or options.

## Deliberate differences and limits

- A Background is never converted to a layer by these tools: under its transparency lock they paint the background color, as the Eraser does. Unlock the layer (or use Layer from Background) to erase to transparency.
- The Background Eraser has no Protect Foreground Color and no Sample All Layers.
- Sampling reads the stroke-start pixels, so within one stroke a pixel is judged by the color it had when the stroke began.
- Find Edges is Patchy's own rule (a neighbour step limit), and the tolerance scales are Patchy's own, not calibrated against another editor.

## Tests

Core `background_eraser_*` and `magic_eraser_*` (tests/core/eraser_tools_tests.cpp): the Background Eraser changes exactly the pixels Color Replacement repaints for the same dab, each Limits and Sampling choice, the lock and no-alpha rules, overlapping dabs, selection, 16 and 32 bits; the Magic Eraser clears exactly the pixels the Fill tool paints for the same click at 8, 16 and 32 bits, Contiguous, Anti-alias, Opacity, selection, Sample All Layers and the lock rules. UI `ui_eraser_*`, `ui_background_eraser_*`, `ui_magic_eraser_*` and `ui_script_erase_background_and_magic_erase` (tests/ui/eraser_tools_tests.cpp): the flyout and Shift+E, each options bar, one undo step, a locked Background, settings across a restart, 16-bit layers, the script calls.
