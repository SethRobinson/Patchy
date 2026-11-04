# Text layout plan and transforms

Split from [text-tool.md](text-tool.md): the line plan that draws a text layer and answers caret, selection and hit-testing, the layout metrics handed to the PSD writer, and how document geometry operations keep each text layer's transform in sync.

## One layout authority (src/ui/text_layout.hpp)

The renderer draws a text layer by walking a LINE PLAN (one `QTextLine` per visual line with the
origin it is drawn at) rather than letting QTextDocument place the lines, because Photoshop's
leading model moves baselines off Qt's natural spacing. `text_layout.hpp` owns that plan and is
the single geometric authority: `photoshop_text_layout_plan` / `boxed_text_render_plan` produce
it, and `TextLineGeometry` reads the same plan back for caret rects, selection rects and
hit-testing. Built with the render pass's `boxed` / `photoshop_layout` flags, its answers agree with the
drawn glyphs.

Caret and selection geometry MUST go through `TextLineGeometry`, never Qt's natural
`blockBoundingRect` origins: those drift roughly (leading - Qt line spacing) px per line on a
PS-model layer (pinned by `ui_psd_text_caret_follows_photoshop_leading`). `BoxTextLineRenderItem::block_position` exists only for this:
`QTextLine::lineNumber()` is block-local. Caret lookup resolves the owning block FIRST (as
`QTextDocument::findBlock` does): a document-wide line scan answers the previous block for a
position at the start of the next one.

**The caret layout must be built with the same SCALES as the render pass**:
`build_text_editor_document_space_layout` mirrors `update_text_editor_preview` argument for
argument (`metric_scale` AND the PSD-frame `layout_scale`, `text_editor_size_display_scale` when
`photoshop_layout && usesPsdTextFrame`). A frame session keeps its runs in raw engine units and
folds the frame's vertical scale in only at render time, so a default `layout_scale` of 1.0 lays
the caret out at the raw size while the glyphs draw scaled
(`ui_psd_frame_text_highlight_matches_scaled_glyphs`).

**A face's natural width is `QFont::AnyStretch`, never stretch 100.** Qt synthesizes
request / face width class, so a Condensed face (width class 75) asked for stretch 100
stretches 133 percent. Every advance-ratio reference in text_document.cpp and text_render.cpp
(`set_stretch_for_advance_ratio`, `scale_font_width`, `dominant_run_width_residual`, the
pixel-aligned glyph drawer) measures against AnyStretch; measuring against 100 stretches an
unstretched condensed line a third wider than the caret layout
(`ui_title02_tracked_legacy_text_caret_matches_glyphs_if_available`,
`ui_psd_text_tracking_click_lands_on_glyphs`).

A paragraph separator (no fragment) is the NEXT block's `charFormat()`, a blank paragraph's
only format: copying, rescaling, substituting or serializing char formats must cover
separators, or committed blank lines lose their size and leading
(`ui_text_blank_lines_keep_fixed_leading`, `ui_duke_psd_text_runs_survive_reedit`).

Mouse hit-testing goes through the same plan, never `QTextEdit::cursorForPosition`.
`MainWindow::handle_text_editor_viewport_mouse_event` intercepts left press/drag/double-click for
the flat case and `TransformedTextEditOverlay::cursor_position_for_overlay_point` the transformed
one, both ending at `TextLineGeometry::position_at`. `ui_psd_text_click_returns_to_the_caret_it_drew` pins the round trip. Double-click
selects the word; a third click inside the double-click interval selects the visual line
(`TextLineGeometry::line_range_at`, trailing whitespace trimmed; tracked via the
`patchy.textTripleClick*` properties; GitHub issue 74, `ui_text_triple_click_selects_the_line`). The editor widget
is SIZED from that layout too, never `QTextDocument::size()`: its rect is its hit area, and a
line past a too-short bottom edge clicks through to the canvas (focus-loss auto-commit) (`ui_transformed_text_click_returns_to_the_caret_it_drew`).

The Type-tool press that OPENS a session on an existing layer goes to the canvas, so Qt delivers
that gesture's drag there, not to the new editor. The canvas forwards it
(`dragging_text_entry_selection_` to `MainWindow::extend_text_entry_selection`), so one
press-drag enters and selects (`ui_text_press_drag_from_outside_session_selects_range`).

`render_text_pixels_with_local_rect` = `build_text_render_plan` (layout, line plan, local rect,
post-fold residual transform) + `draw_text_render_plan(plan, QPainter&)`. The raster path draws
the plan into a QImage; `draw_text_layer_to_painter` (`ui/text_layer_painter.hpp`, editable PDF
export) draws the same plan through the layer's canonical text transform, so PDF text lands on
the raster. Keep the two consumers on one plan. `local_rect` is text-local (origin = line start and
first line top = the transform origin) and starts left of or above (0, 0) when glyph ink
overhangs, so placements add `local_rect.topLeft()` to the transform translation; the buffer
corner is not the pen ([text-render-calibration-pixel-grid.md](text-render-calibration-pixel-grid.md), "Glyph ink").
The render also returns the plan's layout
metrics (first baseline, box baseline inset, auto-leading fraction); every caller storing the
pixels on a layer hands them to `store_text_layout_metrics` for the PSD writer
([text-render-calibration-patchy-authored.md](text-render-calibration-patchy-authored.md), "re-renders where Patchy drew it").

`TextRenderPlan` declares its owning `TextRenderDocument` before its line handles, so the handles die first. Handles are valid only while that document lives without relayout. `build_text_render_document` must finish `setTextWidth` before building any plan.

## Auto leading handed to Photoshop

The PSD writer sends one paragraph `/AutoLeading` fraction per layer
(`kLayerMetadataTextAutoLeading`, from `text_layout_metrics_for_plan` at render time):
Qt's baseline pitch between the first paragraph's first two lines (else the first line's height)
over the largest run size on those same lines, never the layer's largest run (a large spacer
paragraph elsewhere would shrink the fraction and Photoshop would stack the other lines too
tightly). Test: `ui_script_text_auto_leading_ignores_spacer_paragraphs`.

## Document geometry operations follow the text transform

Every operation that remaps document space (Image Size, Canvas Size, Crop to Selection, Rotate
Left/Right/Arbitrary, layer Flip Horizontal/Vertical, Shift Seams) composes its matrix
onto each text layer's `patchy.text.transform` (`compose_text_layer_transform`,
document_geometry.cpp) BEFORE mutating the layer, so the implicit case can materialize
translate(bounds) from pre-operation bounds. A layer with no stored transform gets one only under
a matrix with a linear part; a pure translation already rides in the bounds. Without it the next
metadata re-render (an edit commit, `--append-text`, a PSD save) used the stale transform.
`ui_*_keeps_text_transform_in_sync` and `ui_layer_flip_keeps_text_mirrored_across_reedit` pin
each operation with a no-change re-edit. `patchy.psd.text.*` stays untouched on purpose: it is
the import snapshot, and diverging from it routes the PSD writer off the templated TySh (and
turns off the PSD-frame edit session via `layer_patchy_text_transform_overrides_psd_source`).

Image Size RESAMPLES, so composing the matrix is not enough: after the resized document is
swapped in, `resize_document_image` (dialog, `doc.resizeImage` and MCP) runs
`rerender_text_layers_through_transforms`, which re-renders every text layer whose transform
carries scale with the free-transform commit's rules (`rerender_text_layer_through_stored_transform`):
Patchy-authored point AND box text fold the vertical scale into the size, per-run sizes,
baseline shifts, paragraph metrics and frame dims (`fold_text_transform_scale_into_font_size`) and re-rasterize
through the residual; installed-font PSD point text re-renders crisp through the glyph-aligned
transform; everything else keeps the resampled raster and its raster status. The options bar
derives its displayed size from the transform's vertical scale for ANY layer, so documents saved
in that split state edit at the effective size (`ui_image_size_dialog_*`,
`ui_split_state_text_size_spin_shows_effective_size`).

Negative-determinant (flipped) transforms are ordinary transforms everywhere: the free-transform
commit composes the signed delta, the crisp re-render draws THROUGH the mirrored matrix, and the
drag preview's source blit applies the scale signs like the proxy path
(`ui_point_text_flip_transform_mirrors_and_survives_reedit`: flip, re-edit, flip back).
