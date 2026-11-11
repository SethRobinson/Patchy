# Copy CSS

Split from [web-export.md](web-export.md). Layer > Export > Copy CSS (`layer.copy_css`,
`layerCopyCssAction`, no default key; GitHub issue 688) puts the selected layers on the clipboard
as CSS text, one class per layer. The same action ends the Export submenu of the layer row menu
and of the Background's menu (`add_layer_context_export_menu`); it sits in the Export submenus
because the Layer menu and the row menu are both kept short
(`ui_layer_menu_hosts_export_lock_and_hide_entries`, `ui_layer_context_menu_fits_short_screens`).

Code: `src/formats/css_layer_export.{hpp,cpp}` (`patchy::css::layers_to_css`, Qt-free),
`MainWindow::copy_as_css` (main_window_layer_ops.cpp), `ScriptDocumentObject::cssForLayers`.
The blend-mode names are the SVG writer's map (`svg::detail::blend_mode_css`,
[svg.md](svg.md)): one table for both. The text runs come from
`psd::text_style_runs_from_layer` / `text_paragraph_runs_from_layer`, what the PSD writer reads.

## The text

```css
/* Patchy: poster.psd */

.card {
  position: absolute;
  left: 8px;
  top: 98px;
  width: 104px;
  height: 64px;
  z-index: 2;
  box-sizing: border-box;
  background-color: #ffcc00;
  border: 4px solid #102030;
  border-radius: 10px;
}
```

- A comment names the document (its tab title; `*/` and control characters in it are
  neutralized). Then one class per listed layer in panel order, top row first, whatever order
  the ids came in. Lines end with `\n`; lengths are px in document pixels, rounded to 2 decimals
  with no trailing zeros (em values to 3), built from integers so no locale changes them.
  The same document and selection give the same bytes on every platform.
- `z-index` is the layer's row in the whole panel counted from the bottom (1): every layer
  and folder counts, hidden ones and those inside folders included, and a folder sits above
  its contents. So the numbers depend on the document, not on the selection: classes copied
  at different times still stack as the layers do. Photoshop's Copy CSS also writes
  `z-index`; its numbering was not compared (running it would overwrite the clipboard).
- Class names (`class_name_from_layer_name`): ASCII letters lowercased; digits, `-` and `_`
  kept; non-ASCII characters kept as typed (CSS identifiers allow them); everything else,
  Unicode spaces and bytes that are not UTF-8 included, one `-`; a leading digit gets `layer-`
  in front; nothing left is `layer`. A repeated name gets `-2`, `-3`, skipping names already
  used.
- Left out, and counted in the status line: hidden layers (their own eye or a folder's) and
  layers with no extent (no pixels, adjustment layers, empty folders). Nothing usable: the
  clipboard is left alone and the status line says "Select a visible layer with content to
  copy as CSS".
- The clipboard gets plain text, and the internal layer clipboard is dropped
  (`set_system_clipboard_mime`), as Copy as SVG does.

## What maps to what

Every class: `position: absolute`, `left`, `top`, `width`, `height`, `z-index`; `opacity` when below
100%; `mix-blend-mode` when the mode is not Normal and CSS has it (Pass Through and the modes
CSS lacks say nothing).

| Layer | Box | Also |
|---|---|---|
| Pixel, smart object | visible pixels (`layer_visible_alpha_bounds`; masks are not applied) | `opacity` is layer opacity x fill opacity while no effect renders, layer opacity alone otherwise (CSS `opacity` would dim the shadow too) |
| Folder | union of its visible layers' boxes | opacity and blend mode only; its layers get classes only when selected themselves |
| Fill layer (no path) | the canvas | the fill |
| Live rectangle, rounded rectangle, ellipse | the outline, grown by the border (below) | fill, border, `border-radius`, `box-shadow` |
| Any other outline (pen paths, polygons, lines, rotated live shapes, merged multi-paint layers) | visible pixels, stroke included | fill and border of a single-paint layer, with no radius: the class is the box of the outline, not its shape |
| Text | visible pixels | font properties, `text-shadow` |

Shapes and text carry fill opacity in their colors (`rgba()`), so `opacity` is the layer
opacity alone and effects keep their strength.

- **Fill**: solid is `background-color`. A Linear or Radial gradient (not noise) is
  `background-image: linear-gradient(...)` or `radial-gradient(circle R at X Y, ...)` from
  `vector_export::gradient_export_geometry`, with the gradient's own stops joined by straight
  ramps (the Classic easing and moved midpoints are not resampled into dense stops, unlike
  the SVG export). Patchy's angle is counterclockwise from the right and its ramp is the chord
  through the box center; CSS's is clockwise from up and runs corner to corner, so the angle
  is `90 - angle` and each stop's percent is where Patchy's ramp puts it on the CSS line.
  Reflected, Angle, Diamond, noise and pattern fills give no background. A stroke with its
  fill off gives none either.
- **Border**: a solid-color stroke is `border: <width> solid|dashed|dotted <color>` (dotted for
  the dot presets, dashed for any other dash array; the stroke's opacity in the color) with
  `box-sizing: border-box`. For a live box the border box is the outline grown by 0 (Inside),
  half the width (Center) or the width (Outside), so the border lands where the stroke is
  drawn. Gradient and pattern strokes give no border.
- **Radius**: `border-radius: 50%` for an ellipse; a rounded rectangle's corner radii in CSS
  order (top-left, top-right, bottom-right, bottom-left, the model's own order), one value
  when they agree. A CSS radius is the border's outer edge, so each nonzero radius grows by
  the same amount as the box.
- **Text**, from the first style run and first paragraph: `font-family` (quoted),
  `font-size` (the run size times the layer's text transform scale), `font-weight` (from the
  face name: Thin 100 to Black 900, Demi 600, else the bold flag; faux bold counts as bold),
  `font-style: italic` (italic, oblique or faux italic), `color`, `line-height` (a fixed
  leading in px; a Photoshop-layout layer's auto leading as its unitless fraction; `normal`
  for Patchy's own text, which uses the font's natural spacing), `letter-spacing` (tracking /
  1000 em, when not 0), `text-align` (the justify modes add `text-align-last`),
  `direction: rtl`, `text-transform: uppercase` or `font-variant: small-caps`,
  `text-decoration`, `writing-mode: vertical-rl`. Point text adds `white-space: nowrap`
  (`pre` when it has line breaks): the box is as wide as the ink, and a browser's font
  metrics may differ by a pixel. Known limit: the box is the ink's, and a browser starts
  the first line at the top of its line box, so text renders a few pixels lower than in
  Patchy (placing it exactly needs the font's ascent, which the Qt-free generator lacks).
- **Drop Shadow** (every enabled one, when the layer's effects are shown): offset
  `-cos(angle) x distance`, `sin(angle) x distance`; color with the effect's opacity. A live
  box or fill layer gets `box-shadow` with blur `size - spread` and spread
  `size x spread%`; text gets `text-shadow` with blur `size`; every other layer gets
  `filter: drop-shadow(...)`, which follows the layer's transparency where `box-shadow`
  would shadow a rectangle (its blur is a standard deviation, so `size / 2`). The shadow's
  blend mode, contour and the long-shadow option are not expressed. No other layer effect is
  written.

## Scripting

`doc.cssForLayers(layers)` returns the same text for an array of the document's layers (`""`
when none gave a class) and leaves the clipboard alone; the menu command is
`app.runCommand("layer.copy_css")`.

## Tests

Core (`tests/core/css_layer_export_tests.cpp`, filter `css_`): names and deduplication, pixel
layers, shapes (border alignment, radii, dashes), gradients, other outlines, text (runs, the
single-value fallback keys, styles), drop shadows, hidden layers, folders, the header,
`z-index` (`css_z_index_follows_the_document_stack`). Each pins the exact text. UI: `ui_copy_css_copies_selected_layers_as_classes` (clipboard text,
panel order, the row menu's Export submenu captured as `issue688_copy_css_export_submenu`,
the script call, the nothing-usable message).
