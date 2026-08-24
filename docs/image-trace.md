# Image tracing to shapes

`Layer > New > Trace Image to Shapes...` (command id `layer.trace_image_to_shapes`, default Ctrl+Alt+Shift+T, object name `layerTraceImageAction`; the same action sits in the Layers panel context menu after Rasterize) turns the active pixel layer into a group of editable shape layers, one solid fill per color, the way Illustrator's Image Trace does. The source layer is hidden, not deleted. Scripts call `layer.traceToShapes(options)` (docs/scripting.md). Legal boundaries live in [legal-constraints.md](legal-constraints.md) ("Vector tracing"); the claim-level record is in [patent-research.md](patent-research.md). Do not widen the feature past those boundaries without a new review.

## Code map

- `src/core/image_trace.{hpp,cpp}`: `ImageTraceOptions`, `trace_image` (the pipeline), `render_image_trace` (solid-fill preview raster), `build_image_trace_group` (the group of shape layers), and the option mappings `image_trace_fit_tolerance` / `image_trace_corner_angle`. Qt-free, deterministic.
- `src/core/mask_outline.{hpp,cpp}`: `trace_mask_outlines`, the pixel-edge boundary walker promoted from the selection outline code (the UI wrapper in `selection_outline.cpp` converts its loops to Qt types without changing a coordinate; the outline stress tests guard that).
- `src/core/path_fit.{hpp,cpp}`: `PathFitOptions` and the options overload of `fit_closed_loop` (corner angle, significant-vertex tangents, snap curves to lines). The two-argument overload keeps Make Work Path's historical output.
- `src/ui/image_trace_dialog.{hpp,cpp}`: the modal dialog (`imageTraceDialog`) with its zoomable preview (`imageTracePreview`), the preset table (`image_trace_presets`), and the latest-wins background tracer.
- `src/ui/main_window_vector.cpp`: `MainWindow::trace_image_to_shapes` (guards, settings, dialog) and `insert_image_trace_layers` (undo entry, group insert, source hidden, the frontmost traced shape activated so the pen and path tools edit the trace at once; the script still returns the group). `src/ui/script_api.cpp`: `ScriptLayerObject::traceToShapes`.
- Tests: `tests/core/image_trace_tests.cpp` (pipeline, fitter options, mask outline), `tests/ui/image_trace_ui_tests.cpp` (dialog flow with undo, scripting).

## Pipeline

1. **Label map.** 16-bit and float buffers convert to 8-bit first (value/257 rounded; floats clamp to 0..1). With a selection active the command traces `pixels_limited_to_selection` (main_window_shared), an RGBA copy whose alpha is 0 wherever selection coverage is below 128, so the selection is a per-pixel input predicate exactly like the alpha cut, never a per-region parameter (the legal boundary). Pixels with alpha below 128 are untraced. Black and White thresholds integer luminance (299/587/114). Grayscale and Color quantize with the shared deterministic median cut (`core/palette`), Color through the `PaletteLut` nearest-entry table, never dithered. Ignore White turns every palette entry with all channels at or above 240 into untraced.
2. **Noise.** 4-connected components smaller than `noise` pixels take the label they share the longest border with (untraced counts, so dust inside transparency disappears). Repeats until stable, at most 8 passes.
3. **Contours.** Each label is traced at once within its bounds; loops attribute to their component through the canonical start vertex (outer loop: the top-left corner of the topmost-leftmost pixel; hole: the top-left corner of the topmost-leftmost inside pixel, whose upper neighbor is the owner). Outer loops read clockwise (positive signed area), holes counterclockwise.
4. **Method.** Abutting: one layer per label, outer loops `Add`, holes `Subtract`, one shape group per subpath in trace order (the Make Work Path convention, which puts every hole after its outer loop and every island after its hole). Overlapping: for every hole an 8-connected flood over the non-owner pixels finds the enclosed components; a component's parent is the owner of the smallest hole containing it, depth is the nesting depth, and a hole that encloses a component is dropped (painted over, the children stack on top). Holes enclosing nothing stay holes, so a ring around transparency survives. Layers are keyed by (depth, label).
5. **Staircase settling.** Before fitting, every vertex between two stair edges (edges no longer than `clamp(3 * tolerance, 1.5, 6)` px whose ends turn in opposite directions) moves to the average of their midpoints. Real corners (longer edges) and the caps of 1 px wide features (same turn at both ends) stay in place. Without this, Douglas-Peucker takes stair corners as chord endpoints and keeps 1 px steps as anchors near 1 px tolerance.
6. **Fit.** `fit_closed_loop` with `PathFitOptions{tolerance, corner angle, smooth_corner_tangents = true, snap_curves_to_lines}`. Fitted subpaths with fewer than three anchors and collapsed handles are dropped.
7. **Order.** Layers back to front: ascending depth, then descending pixel area, then palette color key. Layer names are `#RRGGBB`; the group is "Traced <source name>".

## Options

| Option | Range, default | Meaning |
| --- | --- | --- |
| Mode | Color | Color, Grayscale, Black and White. |
| Colors / Grays | 2..64, 16 | Palette size for Color / Grayscale. |
| Threshold | 1..255, 128 | Black and White: luminance below it is black. |
| Paths | 0..100, 50 | Fit tolerance, log scale: 4 px at 0, 1 px at 50, 0.25 px at 100 (`image_trace_fit_tolerance`). |
| Corners | 0..100, 75 | Corner angle threshold: 120 degrees at 0, 30 at 100 (`image_trace_corner_angle`). |
| Noise | 1..100 px, 25 | Minimum region area. |
| Method | Abutting | Abutting (cutouts) or Overlapping (stacked). |
| Snap curves to lines | off | Collapse cubics whose handles stay within tolerance of the chord. |
| Ignore white | off | White regions become untraced. |

Presets (`image_trace_presets`) are fixed option sets; the combo shows "Custom" whenever the controls match none of them. Save... / Delete beside the combo manage user presets (`ImageTraceUserPreset`; settings key `imageTrace/userPresets`, a compact JSON array using the `imageTrace/*` spellings; names are the identity, case-insensitive, built-in names are refused, malformed elements are skipped one by one). They list after a separator; a built-in wins a tie when the controls match both. The last-used options persist under the `imageTrace/*` settings keys (persisted identifiers; never rename).

## Fitter hardening (August 2026)

`generate_bezier` rejects least-squares handle lengths above the run's polyline length and handles that cross when projected onto the chord (paper.js's order check), and `max_fit_error` also evaluates the curve halfway between neighboring samples against the polyline midpoint. A run with one or two interior samples otherwise fits handles hundreds of pixels long that spike between samples (the first photo trace drew image-spanning hairlines). These apply to every `fit_closed_loop` caller; the path-fit tests and `ui_make_work_path_from_selection_traces_selection` cover the unchanged cases.

## Dialog and preview

Controls on the left (each numeric option is a slider plus spin box through `add_dialog_slider_spin_row`; the spin boxes keep their object names), the rendered trace (`render_image_trace` over a checkerboard) on the right with zoom buttons, wheel zoom, drag pan, and double-click fit/100%. While a trace runs the `ActivitySpinner` (`imageTraceBusySpinner`, theme role `dialog_busy_spinner`) shows beside the info label and the preview takes the busy cursor; `imageTraceSizeWarningLabel` appears when `image_trace_result_is_large` (2,000 layers or 20,000 anchors) and `imageTraceSelectionNoteLabel` when the command was limited to a selection. Preview downsampling was evaluated and rejected: OK reuses the preview trace, and Noise/Paths are pixel units. Every control change re-traces the layer's full pixels on a tracked background worker after a 150 ms debounce, latest-wins: stale work aborts through the tracer's cancellation poll. OK waits for the trace of the current settings and hands that result to the caller, so nothing traces twice. The info label reads "N shape layers, M anchors". The preview shows only the traced result: no outline overlays, no source/trace rendition switches, no per-region controls (the legal boundary). Large photos (12 MP) trace in about 5 s into around 60,000 subpaths, which is the expected scale for a photo; exported SVGs of such traces are tens of megabytes.

## Scripting

`layer.traceToShapes({mode, colors, threshold, paths, corners, noise, method, snapCurvesToLines, ignoreWhite})` traces synchronously, inserts the group above the layer, hides the layer, and returns the group (null when nothing traced). With a document selection it traces only the selected area, like the dialog (no option: scripts control the selection through the selection API). Unknown option names throw. Text, shape, and Smart Object layers trace their cached pixels without a rasterize prompt (the menu command rasterizes first through the shared prompt).

## Not implemented (later phases)

Centerline (stroke) tracing, automatic palette sizing ("Full Tone"), and tracing into a work path. The centerline patent (US 7876932) expired in 2025, so that phase needs only a skeletonization design, not a new clearance.
