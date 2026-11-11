# Photoshop text pixel placement

Split from [text-render-calibration.md](text-render-calibration.md): where Photoshop puts a type layer's pixels (fractional anchors kept in the TySh, line starts and each glyph's x rounded to whole pixels, glyph ink outside the advance box) and how Patchy's renderer and editor match it.

## Pixel grid and fractional anchors

PS 27.9 COM captures: "Hg", Arial 48 px, Sharp, placed at x or y 100.0 / 100.3 /
100.5 / 100.7, plus 10-degree rotated and 150% scaled variants; two are committed as
`test-fixtures/psd/photoshop-text-anchor-{whole,half}.psd` (x 100.0 and 100.5).

- **The TySh keeps the fractional anchor** (tx 100.3/100.5/100.7 round-trip exactly). Patchy keeps it too:
  `patchy.text.transform` serializes at 17 significant digits (`serialize_layer_affine_transform`),
  `committed_text_transform` leaves tx/ty alone when their rounding already equals the committed
  document point, and integer moves add to them.
- **Photoshop rasterizes with each LINE START rounded to a whole pixel, halves up**
  (`snap_to_pixel_grid`, core/pixel_grid.hpp: floor(v + 0.5)). The line start is the anchor minus
  the justification offset in document space, so left-aligned x 100.3 renders like 100.0 and
  100.5 like 100.7; y likewise, rotated and scaled layers included. Centred and right text does
  NOT round the anchor: the sweep (`local-test-fixtures/psd/ps2026_text_anchor_just/`,
  `capture_anchor_just.jsx`: left/centre/right x 100.0/.3/.5/.7 x scale 100%/90%, plus Bookman
  "Pause" replicas of the Dungeon Scroll buttons) puts the centred "Hg" (advance box +-30.68) at
  column 72 for 100.0 but 73 for 100.3, scaled 90% at 75 then 76, right-justified at 42 for all
  four. Every case fits round(anchor + line offset) + left side bearing, none round(anchor).
  Three places apply it. `build_text_render_plan` snaps dy and, for an axis-aligned transform,
  keeps the fractional line start for the per-glyph rounding below (`round_line_starts` moves
  whole lines only where glyphs are not aligned individually; rotated, sheared, vertical and
  drawContents renders keep the anchor snap). The session override for an imported layer's
  re-render puts Patchy's line start on Photoshop's WITH its fraction: from the TySh advance box
  when the file has one (`psd_point_text_local_bounds_transform_for_pixels`), else from
  Photoshop's ink column less this render's side bearing plus the anchor fraction recorded at
  session entry (`kTextEditorSourceLineStartFractionProperty`; `anchored_text_transform_for_pixels`
  truncates the justification width correction to whole pixels;
  `psd_point_text_document_bounds_transform_for_pixels` estimates the fraction for CS-era scaled
  layers). A fractional x translation commits through the crisp re-render
  (`render_crisp_transformed_text_for_editor`), never a half-pixel resample. The editor's
  document point (`set_text_editor_transform_override`, `rendered_text_bounds_for_editor`,
  session entry) rounds the same way. Pinned by `ui_text_transform_rerender_rounds_anchor_like_photoshop`,
  `ui_box_text_edit_keeps_fractional_anchor`, `psd_text_anchor_captures_keep_fractional_transform`
  and `ui_psd_centered_text_commit_rounds_line_start_like_photoshop`.
- **The first baseline lands on the anchor's y.** Photoshop draws horizontal point text with its
  first baseline on the TySh ty, rounded like a line start (whole-pixel ty: glyph bottoms end
  exactly on that row boundary). A translation-only layer still showing Photoshop's raster
  (`psd_raster_preview`) re-renders through the ink-top pin (`anchored_text_transform_for_pixels`),
  and that pin counted Patchy's faint antialiased top row as ink: 16 of 25 words of the issue 686
  file came out 1 px low. The session start now records `snap_to_pixel_grid(ty)`
  (`kTextEditorSourceDocumentBaselineProperty`) and `capture_text_editor_source_ink_offset` moves
  the reference so the first baseline sits there, when the ink pin lands within 1.5 px of it; a
  larger gap (a transform that no longer describes the raster) keeps the ink pin. What remains is
  each engine's own antialiasing (Photoshop's edges are crisper; about 0.2 px of centroid).
  Pinned by `ui_update_all_text_layers_puts_photoshop_baselines_on_the_anchor`.
- **Glyph ink outside the advance box is kept, and the buffer grows around the origin.**
  Qt's line rect is the advance box; a negative left side bearing (script and italic faces: the
  Balmoral LET "é" starts 9 px left of the pen at 1011 px), ink past the last advance, or a swash
  above Qt's ascent lies outside it, and Photoshop rasterizes all of it. `build_text_render_plan`
  measures the real ink per line (`line_glyph_ink_rect`: `QRawFont::boundingRect` per glyph,
  widened by any stretch because the DirectWrite engine's boxes ignore it, plus half the faux-bold
  stroke, the faux-italic descender lean, and on macOS 1 px of slack because CoreText's boxes
  are a pixel tighter than its raster) and `grow_point_text_rect_to_glyph_ink` extends
  `local_rect` by 2 px past whichever edge the ink exceeds; ink that fits leaves the rect and
  every pinned raster untouched. Local (0, 0) stays the line start / first line top, so the
  transform still names the pen and the buffer starts at transform + `local_rect.topLeft()`
  (`rendered_text_bounds_for_editor`, the resample offset in commit and preview, the SVG and
  Affinity placements); the PSD pins convert buffer-space ink to local space before comparing. Pinned by
  `ui_point_text_render_keeps_glyph_overhang` (Arial Italic "jf": transform at the pen, re-entry
  byte-identical, TySh tx at the pen) and, with the reporter's file and font in the local
  fixtures, `ui_la_methode_psd_text_commit_keeps_glyph_overhang_if_available` (ink within 1 px of
  Photoshop's, clear buffer margins, TySh at Photoshop's pen), plus zoom and edit independence
  and the live preview's placement (`..._is_zoom_and_edit_independent_if_available`,
  `..._preview_sits_on_photoshop_ink_if_available`). Known gap: box text keeps its 2 px `kHorizontalBleed` and still clips an
  overhanging first or last glyph; its frame-origin contract differs and is untouched.
- **Each glyph's x rounds too.** Photoshop rounds EACH glyph's absolute x position, not just the
  line start: at x 100.5 the "H" moved one column while the "g" (100.5 + 34.67 = 135.17 -> 135,
  the same column as 134.67) stayed, and the centered/right "Hg" right edge moves at .7 while the
  left edge does not. Qt places glyphs at fractional advances, so a Photoshop-layout line through
  an axis-aligned transform is drawn run by run (`draw_line_glyphs_pixel_aligned`, gated by the
  plan's `pixel_align_glyphs`): every glyph origin is mapped to document space, snapped with
  `snap_to_pixel_grid`, and mapped back through the residual scale before `drawGlyphRun`, per
  fragment for colour, with faux bold filling and stroking glyph paths as the vertical plan does.
  **Glyph images are drawn from the unstretched face through an explicit x scale** equal to the
  advance ratio the engine delivered: FreeType (the offscreen suite) stretches images and
  advances for a stretched QFont, DirectWrite (a Windows window) stretches the advances only,
  so a stretched glyph drawn through the QFont is narrow on screen while every offscreen pin
  passes. Rotated or
  sheared transforms and the drawContents fallback keep `QTextLine::draw` (their stretch is
  still engine-dependent, a known gap; so is vertical type). Pinned by
  `ui_psd_left_text_commit_rounds_each_glyph_like_photoshop` on the whole/half fixtures: the
  re-render keeps Photoshop's half-alpha edge columns exactly, left edge +1 and right edge +0
  between the pair.
- Box text keeps a fractional `/BoxBounds` in PS (100.6 x 80.3); `patchy.text.box_width/height`
  round it (`extract_type_tool_text_box`). The frame origin rounds like a point anchor.
