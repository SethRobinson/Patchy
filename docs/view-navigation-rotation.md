# View rotation (Rotate View)

Read this before changing how the canvas maps document and widget coordinates, the rotated drawing path, the Rotate View tool, or anything that draws an overlay from document geometry. The upright view's zoom and pan model is in [view-navigation.md](view-navigation.md).

The view can turn to any angle, the way a painter turns a sheet of paper. Only the view turns: the document, its pixels, saves and history never change, and nothing about rotation is undoable or saved. The angle belongs to each canvas, like zoom.

## The view transform

- `CanvasViewTransform` (src/ui/canvas_view_transform.hpp) is the one mapping: `widget = pan + R(angle) * zoom * document`, angles clockwise in degrees, wrapped into (-180, 180] (`normalized_angle`, which also snaps rounding dust back to 0). `pan_` stays the widget position of the document origin, so a canvas size change keeps the origin in place and hand, wheel and spacebar panning keep adding screen deltas.
- `CanvasWidget::view_transform()` builds it from `zoom_`, `pan_` and `view_rotation_degrees_`. The funnel (`document_position*`, `widget_position*`, `widget_rect_for_document_rect`) and every hand-built `QTransform` read it. **Never write `zoom_ * x + pan_` or `QTransform(zoom_, 0, 0, zoom_, pan_...)` again**: map through the transform (`map_to_widget`, `map_vector_to_widget`, `to_widget()`).
- **Angle 0 is bit-identical to the code before rotation.** Every mapping has an explicit upright branch with the old per-axis arithmetic, so pixel-aligned placement, deep-zoom snapping and all byte and visual tests stay valid. Right angles use exact 0/1/-1 cosines.
- `widget_rect_for_document_rect` returns the on-screen bounding box under rotation. That is right for repaint and clip rects; an outline that must follow the turn maps the corners (`map_document_rect`) or draws in the upright frame (below).
- `set_view_rotation(degrees)` pivots on `view_content_rect()`'s center, keeping the document point there in place, then constrains the pan and notifies view listeners (the options bar's angle, the text editor overlay).
- `view_rotation()` and `set_view_rotation` speak of the user's angle. A Crop session with Move Image Under Crop Box on adds a turn of its own on top (`crop_view_turn_degrees_`, minus the crop box's angle, so the box shows upright), which those two leave out and the session gives back when it ends ([crop-tool.md](crop-tool.md#move-image-under-crop-box)). `view_transform()` and everything that draws use the whole angle. `patchy.ui.rotation` reads and sets the user's angle.

## Navigation under rotation

- Pan limits, scroll bars, Fit, Fill, centering, paging, resize re-anchoring and the zoom presets work per axis on the rotated document's bounding box: its top-left is `pan + bounds_offset()` and its size `extent()`. Each function adds the offset before its axis math and subtracts it after, which is a no-op at angle 0.
- `zoom_to_document_rect` fits the rect's on-screen bounding box and centers the rect; the Zoom tool's click anchor clamps in document space.
- Shift-constrain, arrow nudges and marquee, crop and shape drag-outs stay on the document's axes, as in other editors: those paths already work on document points.

## Drawing a rotated view

- `paintEvent` keeps its upright path untouched. A rotated view takes `paint_rotated_document` (canvas_widget_render_rotated.cpp): the painter turns to `to_widget()` and draws every composite, mip, patch, move proxy, commit hold, patch-tool ghost, channel tint and mask overlay over its document rect, clipped to the document. Work stays proportional to the exposed screen area.
- The checkerboard stays screen-aligned inside the document's (or each artboard's) on-screen outline; ghost tiles draw one by one up to `kRotatedTilingTileLimit`.
- Smoothing: `uses_smooth_rotated_display_scaling` smooths below 200%, whole zooms included (rotated pixels never land on exact blocks), and draws nearest from 200% up, where pixels read as squares (`kSmoothMagnificationLimit`, GitHub issue 706). The deep-zoom raster, pixel-aligned placement and Dynamic Vector Preview are axis-aligned, so `deep_zoom_pixels_active()` and `vector_preview_available_for_view()` are false while rotated; the pixel grid draws as thin lines (`draw_rotated_grid_overlay`).
- The selection outline maps the document loops through the transform (the device-resolution trace below 100% is upright only); its cache key carries the angle.
- Grid, guides and the document border draw as transformed lines. Rulers read a document axis at 0, 90, 180 and 270 degrees (turned a quarter, the top ruler reads y); at any other angle they keep their bands without ticks. A guide dragged out of a ruler takes the document axis nearest the screen axis.

## The upright overlay frame

- Rect-based overlays and their hit-tests compute as if the view were upright and the painter turns that frame about the document origin's screen point: `UprightOverlayScope` makes `view_transform()` report angle 0 while open, `upright_overlay_to_widget()` is the turn, `upright_overlay_point` brings a screen point into the frame (unchanged inside a scope, so nested hit-tests convert once). Distances are unchanged by a turn, so handle reach, hit zones and snapping tolerances keep their meaning.
- paintEvent opens the frame around: layer outlines, Free Transform, warp and move controls, artboards, snap guides, marquee handles, Object Selection and Red Eye boxes, the shape preview, the crop box and shield, the Move tool's selection rectangle, the text rect preview and the Zoom marquee. Point-based overlays (paths, the pen, the ruler, preview points, retouch markers) already map through the funnel and draw outside it, so their labels stay upright.
- Hit-tests that read box geometry open it too: `transform_handle_at`, `transform_rotate_cursor_at`, `marquee_resize_handle_at`, `artboard_handle_at`, `artboard_label_at`, `artboard_add_button_at`. A new rect-based overlay or hit-test joins the same frame.
- Inside the frame, `rect()` is not what the screen shows: culls and shields use `overlay_view_rect()` and `overlay_exposed_rect()`. A dirty rect computed per axis (`move_snap_guides_widget_rect`) repaints the whole view while rotated.
- Resize and rotate cursors add `view_rotation_degrees_` to the box's own angle; the artboard resize cursor goes through `set_transform_cursor_for_handle` while rotated.

## Brushes, pens and text

- The brush cursor, its large hover outline and the dab preview turn with the view (the tip stays aligned with the document); the cursor cache key carries the angle.
- Pen tilt direction and barrel rotation are screen angles: `pen_input_sample_from_tablet_event` turns them back into document angles.
- Text editing under rotation goes through `TransformedTextEditOverlay`, which maps through the canvas while rotated; plain point text gets the canvas preview and a provisional-layer target, because the inline `QTextEdit` cannot turn. A view turned mid-session switches the session to that preview.

## The Rotate View tool and commands

- `CanvasTool::RotateView` (hotkey id `tools.rotate_view`, default R; Smudge gave up R and stays on the detail group's Shift+R) shares the Hand's flyout (`handToolButton`, `tools.cycle.hand` on Shift+H). A drag turns the view about the view center by the pointer's travel around it (`canvas_widget_rotate_view.cpp`), Shift snaps to 15 degrees, Escape during a drag puts its angle back; a compass HUD shows the document's up direction and the angle while dragging.
- **Escape turns a turned view upright with any tool**: `keyPressEvent` takes an Escape that nothing above used (no gesture, transform, crop, path or text session to cancel) before the full-screen exit and layer deselection. A transform's or crop's Escape still cancels that first.
- Options bar: Rotation Angle (`rotateViewAngleSpin`, wraps at plus or minus 180) and Reset View (`rotateViewResetButton`). View menu: Rotate View Left, Rotate View Right (15 degrees, `view.rotate_left`, `view.rotate_right`) and Reset View Rotation (`view.rotate_reset`), no default keys. The GIMP-style hotkey set unbinds Rotate View (its R is Rectangle Select).
- Scripting: `patchy.ui.rotation` ([scripting-api-changes.md](scripting-api-changes.md)).

## Tests

`tests/ui/view_rotation_tests.cpp`: transform round trips and exactness, the pivot, Fit and scroll bars on the rotated bounds, each pixel drawn under its mapped center at 0, 90, 180, 30 and -63 degrees, a brush stroke and a marquee under rotation, Free Transform handles and cursors, overlay captures, rotated text editing, the tool and View commands, and the scripting property. `PATCHY_PERF_ZOOM_ROTATION=<degrees>` runs `patchy_perf_tests zoom` on a rotated view: at 30 degrees on the 70 Mpx tent PSB (dev box, November 2026) every step stayed between 3 and 9 ms, as the upright sweep does, with no full recomposite.
