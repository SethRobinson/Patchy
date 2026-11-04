# 32-bit color choice

Split from [high-bit-depth.md](high-bit-depth.md): the Intensity in stops that a foreground or background color carries on 32-bit documents, where it lives, its controls, the Eyedropper and the Gradient tool, and what does not carry it yet.

## Color choice at 32 bits

A foreground or background color is its 8-bit sRGB base color plus an Intensity in
stops (`core/hdr_color.hpp`: `HdrColor`, `PaintIntensity`, range -10 to +10). Its
linear value is the decoded base times 2^stops; 1.0 is display white. Only 32-bit
layers apply the Intensity (`EditOptions::primary_gain`/`secondary_gain`, read where the
deep paint paths decode the paint color: `write_pixel_blend_deep`, the deep
`write_pixel_paint_mode`, the deep clear, `render_brush_stroke_pixel_deep`), so Brush,
Pencil, pixel shapes, fills, Edit > Fill and Fill With Background Color paint values above
1.0; 8 and 16-bit documents paint the base color, unchanged.
- **Where it lives.** `CanvasWidget::paint_intensity()` per canvas and
  `MainWindow::paint_intensity_` across canvases (never saved; a restart is 0 stops).
  `set_primary_color`/`set_secondary_color` that change the color reset their side to 0;
  Swap swaps it, Default Colors clears it. Swatches, palettes, custom colors and the
  settings keep 8-bit colors only (no format change).
- **UI** (`ui/hdr_color_controls.hpp`, shown by `refresh_hdr_exposure_control` while the
  active document is 32-bit). The Foreground and Background Color windows show the full
  form under the picker: a caption naming the 32-bit document, a strip of swatches from
  -3 to +3 stops around the color (`hdr_preview_color`: scaled, sRGB-encoded, clipped at
  white; a click moves the Intensity by that offset), the Intensity slider and spin, and
  editable linear R, G, B (`hdr_color_from_linear` splits a typed value into base and
  stops). The Color panel shows the compact form (strip, Intensity, linear readout) below
  its view, for the foreground. A base color picked in the window or the panel keeps the
  Intensity they show. 16 bits keep the 0-255 fields: they are display-encoded and narrow
  exactly, so a 0-1 or 0-32768 mode would add no precision a user can act on.
- **Eyedropper.** A 32-bit document picks the linear values of a depth render (All
  Layers, Current & Below and the no-adjustment choices) or of the layer's own floats
  (Current Layer), not the display, so the preview exposure does not change a pick.
  `hdr_color_from_linear` keeps 0 stops within display range and otherwise sets the
  stops that bring the brightest channel to 1.0; the other channels keep 8-bit base
  precision.
- **Gradient tool.** `GradientStop::intensity_stops` scales a stop in
  `gradient_color_at_deep` before the linear mix. FG/BG stops take the colors' Intensity
  (presets interpolate it by location); Edit Gradient Stops edits the current stop's at
  32 bits. `tools/gradientStops` writes `intensity` only when nonzero (old readers ignore
  it). Fill layers, Gradient Overlay/Stroke, GRD and PSD gradients stay 8-bit.
- **Not carried yet:** Mixer Brush pickup, text, shape, layer-style and fill-layer colors,
  the Info readout. Tests: `ui_hdr_`, `hdr_color_`.
