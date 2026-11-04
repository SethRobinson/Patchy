# Text as vector outlines: Convert to Shape and Create Work Path

GitHub issue 598. Type > Convert to Shape (`typeConvertToShapeAction`, hotkey id
`type.convert_to_shape`, no default key) turns every selected text layer into a shape layer made
from its glyph outlines; Type > Create Work Path (`typeCreateWorkPathAction`,
`type.create_work_path`) puts the same outlines into the work path and keeps the text. Both also sit
in the Layers panel's Rasterize submenu for a text layer (the menu's top level is at its row bound,
#461) and in the Move tool's canvas menu over a text layer. Scripts: `layer.convertToShape()` and
`layer.createWorkPath()` ([scripting-guide](../scripts/bundled/scripting-guide.md)).

## Where the outlines come from

- No raster trace. `text_layer_outlines` (main_window.cpp, declared in `ui/text_layer_painter.hpp`)
  draws the layer's text plan into `OutlineRecordingDevice` (`ui/text_outline.{hpp,cpp}`), a paint
  device whose engine declares every feature and records fills as paths. Glyphs arrive through
  `QPaintEngine::drawTextItem`'s default, which asks the font engine for the glyph outlines at the
  laid-out positions and fills them through `drawPath` (macOS document text fills each glyph
  outline itself in `draw_document_glyph_run`, so it arrives as path fills); underline and
  strikethrough bars arrive as rects; a stroked path (faux bold's pen) is kept as
  `QPainterPathStroker`'s outline. Every path is mapped by the painter's transform, so faux italic
  shear, vertical columns, rotated Roman glyphs and the layer transform all apply. Clips are
  ignored; images (colour emoji) are counted and dropped, and the status message says so.
- Unwarped text goes through `draw_text_layer_to_painter`, the editable PDF export's drawing, so the
  outlines sit exactly where the layer's raster is (imported Photoshop previews included, placed by
  `imported_preview_text_plan_transform`). A missing font draws in the substitute an edit would use;
  the status message says so. Strong anti-aliasing's extra stroke is a raster weight and is left out.
- Warped text: the plan is drawn unwarped in plan-local space (what `render_warped_text_pixels_for_layer`
  rasterizes), then `map_outline_path` carries each outline through the same map the raster takes:
  (x, y) to the warp box's (u, v), `evaluate_warp_mesh`, then `warped_text_session_transform`. Lines
  and curves are split into pieces about 4 units long; each piece keeps exact end points and handles
  from the map's derivative, so the result stays a smooth path. The effective warp box comes from
  `render_warped_text_pixels_for_layer`'s `effective_warp`.

## From outlines to Patchy paths

- `append_outline_subpaths` turns one nonzero-winding outline into closed subpaths whose groups fill
  the same area under Patchy's rule (even-odd inside a group, groups combined by Add). Each contour's
  parent is the smallest contour holding it (box and three sample points); along the parent chain the
  winding just inside and outside each contour decides whether it is an edge of the fill. Non-edges
  (a solid contour inside a solid one) are dropped; kept contours join their outermost kept ancestor's
  group, lead first, so the PSD writer sees each group's records together. Crossing contours, such as
  a variable font's overlapping strokes, become separate united groups.
- Straight cubics become corner knots without handles; a knot whose handles are opposite and
  collinear is smooth.
- `shape_content_from_text_outlines`: a solid fill in the text colour, no stroke. Several colours
  become `VectorShapeContent::parts` (one per colour, first-appearance order), the representation
  merged shape layers use, which PSD writes as a group of native shapes ([layer-merging.md](layer-merging.md)).
- `convert_text_layer_to_shape` converts in place: `clear_layer_text_metadata`, the shape markers,
  `mark_layer_vector_block_dirty`, `update_vector_shape_raster`. Id, name, opacity, Fill, blend mode,
  visibility, locks, clipping, masks and layer style stay. One undo step for all selected layers;
  locked layers are skipped.
- Create Work Path replaces the work path like Make Work Path from Selection; with several text
  layers selected their outlines share one work path.

## Tests

`tests/ui/text_outline_tests.cpp`: `ui_text_convert_to_shape_matches_text_and_undoes` (outline
bounds within 1.5 px of the text's ink, coverage IoU above 0.97, properties kept, one undo),
`ui_text_create_work_path_keeps_text`, `ui_text_convert_to_shape_handles_area_vertical_and_warp`,
`ui_text_convert_to_shape_matches_small_point_text` (30 and 64 px point text, which Qt draws from
cached glyph images: coverage IoU above 0.97 on macOS, 0.88 elsewhere),
`ui_text_convert_to_shape_keeps_colors_and_round_trips_psd` (parts, PSD round trip; writes
`test-artifacts/ui_text_convert_to_shape.psd` for a Photoshop open check),
`ui_script_text_convert_to_shape_and_work_path`, `ui_text_outline_commands_in_menus` and
`ui_text_outline_groups_follow_nonzero_fill`.
