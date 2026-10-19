# Guides

Ruler guides: the document model, PSD storage, drawing, and the drag gestures. Code lives in `src/ui/canvas_widget_guides.cpp` (add, clear, hit test, drag lifecycle, snapping), `CanvasWidget::draw_guides_overlay` (src/ui/canvas_widget_render.cpp), the New Guide and New Guide Layout dialogs (src/ui/main_window_preferences.cpp), and resource 1032 in src/psd/psd_image_resources.cpp. The grid, snapping targets and rulers share those files; Move-tool alignment guides (the magenta snap lines) are a different feature, in [alignment.md](alignment.md).

## Model

- `DocumentGuide` (core/document.hpp) is an orientation plus `position_32`, a signed position in 32nds of a pixel. A vertical guide's position is an x coordinate, a horizontal one's a y.
- **A guide may lie off the canvas.** Photoshop keeps guides on the pasteboard, so a position can be negative or past the far edge (GitHub issue 231: the reporter's 549 x 864 PSB has guides at x -154.2 and 1617.0 and y -1637.3 and 864.3). Nothing clamps a position to the canvas: not the PSD reader or writer, not `add_guide`, not a drag. The UI places guides within `kMaxGuideOffsetPixels` (300000 px, the PSB dimension cap) of the origin either way; a file may carry any int32.
- Guides are document data for undo (`session_history.cpp` compares them) and ride document copies. Artboard canvas growth shifts them with the canvas ([artboards.md](artboards.md)). Canvas Size and Crop leave them where they are.
- There is no scripting or MCP API for guides yet.

## PSD resource 1032

Grid and guides info: version 1, the horizontal and vertical grid cycles (32nds), a guide count, then per guide a big-endian int32 position (32nds) and one direction byte (1 horizontal, 0 vertical). Read and write keep positions exactly, negatives included; Photoshop writes negative positions itself, so writing them back is safe. A file that had the resource gets it rewritten from the document; one without gets it only for non-default grid settings or any guide. Duplicate 1032 blocks collapse to one. Tests: `psd_grid_guides_resource_round_trip_and_replaces_duplicates`, `psd_grid_guides_keep_positions_outside_the_canvas` (flat, layered and PSB saves), `psd_issue231_sample_keeps_pasteboard_guides_if_available` (the reporter's file as `local-test-fixtures/psd/ag-psd-psb-test-src.psb`, from ag-psd's `test/read/psb-test/src.psb`; skips when absent).

## Drawing

`draw_guides_overlay` draws every guide across the whole canvas widget, pasteboard included, as Photoshop does; the rulers paint over the ends afterwards. Lines use a cosmetic pen (1 px, 2 px for the selected guide) at `pixel_aligned_coordinate`, and a guide whose line falls outside the widget is skipped. The guide color is user data (`guide_color()`, Preferences), exempt from the theme roles ([ui-conventions.md](ui-conventions.md)). A guide drag repaints the whole widget. Tests: `ui_rulers_grid_guides_render_and_edit`, `ui_guides_draw_across_the_pasteboard`, `ui_issue231_sample_guides_outside_document_if_available` (12% capture of the reporter's file; skips without the fixture).

## Grabbing and dragging

- `guide_at_widget_position` hits a guide anywhere in the view within 4 screen pixels, on the canvas or the pasteboard, but never over the rulers or their corner (`over_ruler_bars`). Guides are grabbed with the Move tool or with Ctrl held under any tool; a locked guide selects but does not move.
- A press on a ruler starts a new guide (top ruler horizontal, left ruler vertical); Alt flips the orientation mid-drag.
- **Where a drop lands decides keep or remove.** A drop anywhere in the view outside the ruler bars keeps the guide, on the canvas or the pasteboard (issue 231; it used to remove any guide dropped off the canvas). A drop on a ruler or its corner, or outside the widget, removes a moved guide and cancels a new one; the dragged line turns into a red dashed line while the drop would remove it.
- A free drag lands on a whole pixel (`snap_to_pixel_grid`, GitHub issue 197), so a drag at 1000% reads and stores 100, not 100.41. Shift snaps to the ruler's minor ticks in the active ruler unit. With snapping on, the grid (when visible), the canvas edges and center, and layer and selection edges keep their exact lines: a grid line or an odd canvas's center can sit between pixels on purpose. Moving a guide keeps the grab offset, and a press that never moves leaves a fractional PSD-loaded position alone (`ui_guide_drag_lands_on_whole_pixels`).
- A guide drag (new from a ruler, or moving one) shows the guide's position under the Show Transformation Values preference (GitHub issue 36): `X:` for a vertical guide, `Y:` for a horizontal one, in the ruler unit through `ruler_pixels_per_unit` and `format_measurement` (whole pixels plain, a 1/32 px fraction with two places; physical units at `measurement_unit_decimals`), hidden while the drop would remove the guide. Only the status-bar mirror goes through `update_drag_readout_region` (`ui_guide_drag_shows_position_readout`). The readout itself is described with the transform readout in [tools.md](tools.md).
- Tests: `ui_guide_dragged_onto_pasteboard_stays` (a drop on the pasteboard keeps the guide at a negative position, it can be grabbed there again, a drop on the left ruler removes it, a new guide dropped past the canvas is kept, one released outside the view is not).

## Dialogs

New Guide takes an orientation and a position in the ruler unit (percent of the extent the guide runs across), from -300000 to 300000 px, so a typed negative or past-the-canvas value places a pasteboard guide; it keeps a typed decimal (Photoshop 27.10 stores 33.53 px as typed). New Guide Layout spaces columns and rows evenly across the canvas. Both are covered by `ui_canvas_aid_preferences_and_guide_dialogs_work`.
