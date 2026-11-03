# Content-Aware Move

The Content-Aware Move tool (GitHub issues 599 and 369) moves or copies a selected object and heals it into its new place; in Move mode it also fills the spot the object left. It is the Patch tool's drag ([healing.md](healing.md#patch-tool)) with a different release, and it sits inside a recorded patent boundary: read the Content-Aware Move bullet in [legal-constraints.md](legal-constraints.md) and the claim record in [patent-research-content-aware-move.md](patent-research-content-aware-move.md) before changing anything here.

## Gesture

- `CanvasTool::ContentAwareMove` (appended to the enum), hotkey id `tools.content_aware_move` (no default key), in the Healing flyout after Patch and in its Shift+J cycle ([tool-palette.md](tool-palette.md)). Icon `tool-content-aware-move.svg`: a dashed rounded square (the old spot), a solid one with an accent dot (the new spot) and a bent arrow between them.
- Drawing the outline is the Patch tool's lasso (`uses_patch_drag` in `ui/canvas_types.hpp` routes both tools through the same press, cursor and release code): freehand outline, the New/Add/Subtract/Intersect combine modes with the tool's own slot (selection tool index 9), Shift/Alt forcing outline drawing. An existing selection of any origin works too. The Lasso's Alt switch to straight segments is not shared (the Patch tool does not have it either).
- A press inside the selection in Replace mode starts the drag (`begin_patch_tool_drag`, which latches `patch_tool_drag_content_aware_move_`). While dragging, the canvas shows the untouched composite, a raw ghost of the region (the frozen retouch snapshot cut to the selection's soft mask) at the pointer offset, and the statically dashed outline at that offset. **Nothing is drawn, healed or written in the original spot until release.** A release below the drag threshold is a no-op; Escape, a tool switch, edit locks and document teardown cancel with no history entry; Enter commits a live drag like a release.
- Switching to the tool shows a how-to status line; committing an outline says where to drag (Extend says "copy").

## Release (`commit_content_aware_move`, canvas_widget_patch_tool.cpp)

Runs once, on release, in one `begin_edit("Content-Aware Move")` pixel step, followed by the selection step that moves the selection onto the copy (`move_selection_with_patch_drop`, shared with Patch Destination).

- **Move**: (1) the old spot is filled first with Remove Object's shape-derived method: the selection's coverage padded by one uncovered cell goes to `spot_heal_source_map` (one coherent rigid reflection or translation chosen from the mask's shape, fixed candidate order, no pixel read) with the drop region passed as an excluded shape, so a candidate that would read where the object is going counts as a violation and the next clean one is taken; then `heal_mask_from_surroundings` writes source texture plus the healing membrane. (2) The retouch snapshot is taken again (the filled document), and the copy is healed in at the drop point through `write_patch_heal`, the Patch tool's Destination heal: texture from the original snapshot at the dragged offset, boundary tone from the refreshed snapshot, so a drop overlapping the old spot blends into the fill rather than into the object. A map that finds no unselected pixels refuses with a status error and no history entry.
- **Extend**: the copy and heal only (step 2 against the original snapshot); the original stays.
- 16/32-bit layers heal narrowed copies (`NarrowedLayerEdit`, per step), as Spot Healing and Patch do.

There is no exemplar or patch search in this tool, no Structure or Color setting, and no Transform on Drop: each needs its own legal check first ([legal-constraints.md](legal-constraints.md)).

## Options

Options bar: the four combine-mode buttons, Mode (`contentAwareMoveModeCombo`: Move, Extend) and the shared retouch Sample (`retouchSampleLayersCombo`, Current Layer / Current & Below / All Layers, with Ignore Adjustment Layers; [healing.md](healing.md#sample)), which stands in for a Sample All Layers box. Mode is session-only like the Patch tool's: every start is Move, a mid-session choice reaches new documents (`main_window_sessions.cpp`). The Info panel lists the mode and the Sample choice.

## Tests

tests/ui/brush_engine_stroke_tests_healing_patch.cpp: `ui_content_aware_move_move_fills_old_spot_and_places_copy` (mid-drag frame: the old spot unchanged on screen and in the layer, the ghost at the drop point; then the exact fill and copy on uniform surroundings), `ui_content_aware_move_extend_keeps_original`, `ui_content_aware_move_fill_skips_the_drop_region`, `ui_content_aware_move_tool_in_flyout_escape_and_undo` (flyout place, options bar, Escape, two undo steps) and `ui_content_aware_move_gallery` (artifacts). Core: `spot_heal_source_map_skips_excluded_cells`. Flyout and cycle: `ui_stamp_and_gradient_flyouts_swap_tools`, `ui_tool_cycle_hotkeys_walk_each_flyout`, `ui_tool_palette_customize_hidden_tools_keep_hotkeys`.
