# Selection regions: marching ants, per-pixel lookups, Stroke Selection

Split from [selection-tools.md](selection-tools.md): the traced and cached marching-ants outline, per-pixel selection lookups and combination with the pre-gesture snapshot, Select > Inverse of a soft selection, and Edit > Stroke Selection.

## Marching ants selection outline is traced and cached

The selection outline is not rebuilt per paint from QRegion edge subtractions. `src/ui/selection_outline.*` traces the selection into closed contour loops (`trace_selection_outlines`, marching-squares wall-follow; diagonal pixels stay 4-connected) and `CanvasWidget` caches both the document-space loops and the stroke-ready device-space paths (`ensure_selection_outline_screen_path`, keyed by zoom/pan/viewport), so the 120 ms animation tick only restrokes. Two consequences for future code:

- A selection translated as a whole may call `shift_selection_outline(step)` instead: the document loops and a device trace that covered the whole selection (it lay inside the viewport) are drawn at an offset until the next real invalidation. Only valid while nothing cuts the moved selection (the canvas edge does).
- Any new code that writes `selection_` / `selection_display_region_` directly instead of going through `set_selection_from_region` / `set_selection_from_mask` / `restore_selection_before_edit` / `apply_selection_snapshot` / `reselect` must call `invalidate_selection_outline()` or the ants will render a stale outline.
- Below 100% zoom the outline is retraced at *device resolution* (AA coverage rasterisation thresholded at 50%, `trace_device_selection_outlines`), which merges or drops sub-pixel holes/islands exactly like the scaled-down artwork. Do not "fix" that by tracing document space at low zoom; it strobes. Loops shorter than one 4-4 dash period go to a separate `pinpoint` path drawn with 2-2 dashes over the solid black underlay so single-pixel selections never blink invisible.

Test filters: `selection_outline` (tracer/path units) and `ui_marching_ants` (rendering).

## Per-pixel selection lookups are rasterized, never `QRegion::contains`

`CanvasWidget::selection_alpha_at` is the one per-pixel selection query (fills, floods, brush coverage, the `selection_mask` / `selection_coverage` lambdas that `apply_canvas_edit_state` hands the core tools, for tools and menu commands alike). A feathered or anti-aliased selection answers from `selection_mask_alpha_`, and a hard region of at most four rectangles (`kMaxRectsForDirectContains`) from `QRegion::contains`, which checks every rectangle in turn. A wand selection of a background that weaves around a subject holds thousands of row spans, which made a 1.5 Mpx fill take about 45 s through `contains` (GitHub issue 34), so a hard region with more rectangles answers from a one-bit-per-pixel raster over the selection's bounding rect (`selection_lookup_bits_`), built on first use under a mutex (render and filter workers may query in parallel) and dropped by `invalidate_selection_outline()`, which every selection write already calls. Keep new per-pixel code on `selection_alpha_at`; never call `selected_document_region().contains(point)` in a loop. Test: `ui_fill_of_wand_selection_with_many_spans_is_fast` (both Layer > Fill > Fill With Background Color and the Fill tool over a 1110 x 1388 wand selection with thousands of spans).

Selection combination must follow the same rule for the **pre-gesture snapshot**.
`combine_selection_from_mask` rasterizes a hard `selection_before_edit_` once with
`hard_mask_from_region`, directly into the destination image, then combines each
pixel in place. Soft snapshots use their existing alpha image. Never query
`QRegion::contains` per output pixel or substitute the live selection's lookup
cache for this snapshot: a live marquee may already have changed the selection.
This keeps anti-aliased or feathered lasso/marquee Add, Subtract and Intersect
linear in mask area plus region spans (issue 49). Replace never reads the snapshot, so a marquee
drag over a soft lasso is cheap (issue 340). Tests:
`ui_lasso_subtract_from_noncontiguous_wand_many_spans`,
`ui_marquee_drag_after_antialiased_lasso_is_fast` and `ui_lasso_combines_fragmented_selection_alpha_and_history` (all four modes,
hard/soft bases, anti-alias/feather, exact alpha, Undo/Redo).

Select > Inverse (`invert_selection`) of a soft selection writes `255 - alpha` over the whole canvas, so the soft edge, the 50% marching ants and a fill through it are the exact complement; only a hard selection takes the `QRegion` subtraction (issue 430: subtracting the region dropped the alpha and left the feather band unselected). Test: `ui_select_inverse_keeps_feathered_soft_edge` (per-pixel complement, double inverse, Undo/Redo).

## Stroke Selection

Edit > Stroke Selection... (also in the canvas context menu's selection section, hotkey id `edit.stroke_selection`) opens `patchyStrokeSelectionDialog` (`main_window_layer_ops.cpp`): Width in pixels (1 to 250, `strokeSelectionWidthSpin`), Location (`strokeSelectionLocationCombo`: Inside / Center / Outside, Photoshop's three placements) and a color swatch (`strokeSelectionColorSwatch`, opens `request_patchy_color`). The swatch's `strokeColor` dynamic property is the chosen color's source of truth, which is how `accept_stroke_selection_dialog` sets it in tests. Width and location persist under `tools/strokeSelectionWidth` and `tools/strokeSelectionLocation` (tokens `inside`, `center`, `outside`; compatibility contract); the color always starts from the foreground color.

The band comes from `selection_stroke_region` in `src/ui/qt_geometry.*`, built on QRegion morphology: Inside grows the one-pixel inner rim by width minus one and clips to the selection, Outside dilates the selection by the width and subtracts it, Center unions an inside band of `(width + 1) / 2` and an outside band of `width / 2` (the larger half inside for odd widths). Outside bands clip to the canvas; a selection flush with the canvas edge still strokes that edge inside. `expanded_region` (Expand, Border and Contract share it) is a square dilation: radii through 16 unite `2(2r + 1)` translated regions, larger ones dilate a byte mask with separable window counts (O(area), identical pixels), so Expand/Contract reach Photoshop's 500 px and the stroke dialog 250. Feather goes to 1000 px (`kMaxSelectionFeatherRadius`, `qt_geometry.hpp`); every feather blur is a sliding window. The fill runs through `fill_rect` with the band rasterized once into a byte mask, exposed through `selection_mask`, and the canvas's `selection_coverage` cleared, so a feathered selection strokes a hard band. `fill_rect` queries coverage per pixel and does not consume `selection_scan_rects` (the clear path does); never give it a callback that searches a fragmented `QRegion` per pixel. Every layer guard (no selection, non-pixel or procedural layer, locked pixels) runs before the dialog; `ui_smart_filter_*` guard tests assert the refusal shows no dialog. Tests: `ui_selection_stroke_region_bands_have_exact_widths`, `ui_stroke_selection_dialog_paints_inside_center_and_outside_bands`, `ui_complex_selection_stroke_uses_region_outline`, `ui_stroke_selection_with_many_spans_preserves_gaps`.
