# Healing tools

The healing family shares one patent boundary: every tool is user-directed or footprint-shape-directed, and every blend is the classic healing math of the EXPIRED US 6587592 (Georgiev, expired 2021-11-19), whose express teaching is published earlier work, free to use. None of these tools may gain PatchMatch-style search (offset propagation or random perturbation), reshuffling, gradient-domain compositing of source gradients, or live per-move classification while the gates recorded in [legal-constraints-filters.md](legal-constraints-filters.md) are active (US 8285055 / US 8340463 / US 8355592 into 2031, US 9058699 to 2029, US 8050498 to Nov 3, 2029); content-driven source selection is allowed only as the exhaustive exemplar search that file allows ("Content-aware fill and exemplar inpainting"). Constraint comments live at the implementation sites (`core/heal_membrane.cpp`, `core/spot_heal.cpp`, `canvas_widget_spot_healing.cpp`, `canvas_widget_patch_tool.cpp`, `finish_healing_brush_stroke`) and must stay in sync with [patent-research.md](patent-research.md).

The blend engine is **the healing membrane** (`core/heal_membrane.{hpp,cpp}`, `solve_heal_membrane`): destination-minus-source offsets on the ring of cells just outside the region are interpolated harmonically across the interior (relaxation on the OFFSET field with Dirichlet boundary conditions, the expired patent's teaching, NOT Poisson compositing of gradients), and the healed pixel is source texture plus the smooth offset. The solver is integer 16.16 fixed-point SOR on a coarse-to-fine cascade with fixed sweep counts and lexicographic order, byte-deterministic across toolchains. Spot Healing, the Patch tool's Source/Destination modes and the Healing Brush use it. The Patch tool's Transparent option instead extracts detail with a separable box-blur local mean.

On 16/32-bit layers the Healing Brush (like Clone) runs at depth (`retouch_source_deep`); Spot Healing, Remove Object, and Patch heal a narrowed 8-bit copy through `NarrowedLayerEdit`, and only the healed pixels come back at 8-bit precision. See [high-bit-depth.md](high-bit-depth.md).

Related documents:

- [healing-remove-object.md](healing-remove-object.md): Edit > Remove Object (dialog, worker fill, variations, tone match, edge feather), the content-aware exemplar fill with its Nearest Edge fallback, and the entry points that share the command.

## Sample

Clone, Healing, Spot Healing, Patch and Remove Object share one Sample choice (issue 382; combo `retouchSampleLayersCombo`, canvas `retouch_sample_layers_`, a `SampleLayers` value limited to three choices), persisted as the Pick tool's token under `tools/retouchSampleLayers`; a missing token falls back to the pre-382 bool `tools/retouchSampleAllLayers` (true All Layers, false Current Layer), still written (anything but Current Layer as true) for older builds. The gesture snapshot (`retouch_source_snapshot()`, deep `retouch_source_deep()`) is:

- **All Layers** (default): the merged document (`render_document_image_with_processing()`).
- **Current & Below**: the merged document without the layers above the active one (`sample_layers_excluded_ids`, the Pick tool's walk), via `render_document_image_with_processing(hidden_ids)`; deep flattens a copy with them hidden. The destination tone comes from the snapshot too, so a heal onto an empty layer above the photo writes opaque healed pixels. No active layer: a null snapshot, nothing written.
- **Current Layer**: the active pixel layer alone (`active_layer_sample_image`, mask and opacity folded into alpha).

**Ignore Adjustment Layers** (issue 383; `retouchIgnoreAdjustmentsCheck`, key `tools/retouchIgnoreAdjustments`, default off, enabled only for the composite choices) renders the No Adjustments variant (`retouch_effective_sample_layers()`), so a Curves above is not sampled in and then applied again.

The snapshot is taken once per gesture, never per move; Clone and Healing fill theirs tile by tile as the stroke reaches it ([performance-strokes.md](performance-strokes.md), issue 597 follow-up 3). Tests: `ui_retouch_sample_*`, `ui_retouch_ignore_adjustment_layers_leaves_adjustments_out_of_sample`.

## Classic Healing Brush

An explicit-source pixel tool: Alt-click picks a source in the gesture snapshot; Aligned shares Clone's across-stroke offset. While painting, the stroke stamps the raw source as Clone does (`clone_brush_stamp_segment`, a preview, nothing tone matched) and records each pixel's coverage, opacity, softness and selection included (`brush_stroke_alpha_caps_`). On release (or focus loss) `finish_healing_brush_stroke` heals the footprint once: destination-minus-source offsets on the cells just outside it (source = pixel plus the stroke's offset, both from the snapshot, so Sample applies) are solved across it by the membrane, and the healed color is written over the stroke-start pixels (kept at the press) by that coverage. Diffusion 1-7 averages the boundary offsets over boundary cells within Diffusion - 1 px before the solve, so rim grain does not print into the heal (1 = unaveraged). 16/32-bit offsets are solved scaled by 16; footprints of 262,144+ px fan out over row strips after detaching the layer bytes. Selection, transparent lock, palette snap and undo follow Clone.

**Mode** (issue 381; `healingModeCombo`, `HealingMode` in `ui/canvas_types.hpp`, key `tools/healingMode` with the permanent `kHealingModeTokens`, default Normal): Normal writes the healed color straight. Replace skips the heal: the live stamp is the result (grain kept at a soft edge). The other six composite the healed color over the stroke-start pixel as one paint dab with that blend mode (`composite_paint_rgba8`/`composite_paint_deep`; amount = coverage times source alpha). Only ordinary layer pixels are written (no private PSD metadata). Tests: `ui_healing_brush_modes_blend_the_healed_color` (8 and 16 bits), `ui_healing_mode_option_persists_and_reaches_new_documents`, `ui_healing_brush_transfers_detail_and_preserves_destination_tone` (with the PSD round trip), `ui_healing_brush_heals_a_spot_wider_than_the_ring`.

## Clone and Healing cursor feedback

Issue 380, Clone and Healing Brush (`retouch_source_tool_active()`), from the folded modifiers of the event handled (`retouch_cursor_modifier_override_`):

- **Alt**: the source cursor (`build_retouch_source_cursor`, the source marker's ring and crosshair; hotspot 12, 12).
- **Ctrl**: the temporary Move, shared by every painting tool ([brushes-ui.md](brushes-ui.md), "Painting tool modifiers"). Ctrl on a ruler guide still drags the guide.
- **Not allowed** (`Qt::ForbiddenCursor`) where a press would be refused (`retouch_press_would_refuse`): no source yet, a grayscale channel target, or no unlocked pixel layer active (the footprint is then painted on the canvas at every size, issue 516). On artboards only the missing source counts (a press can retarget to the clicked artboard's layer).
- **While painting**: a small crosshair at the point the stroke samples (`retouch_source_indicator_`, set by `clone_brush_segment`) replaces the Alt-click marker.

`ui_healing_brush_cursor_feedback_and_ctrl_move` pins every state and the Ctrl drag.

## Clone source settings

Issue 424. A gear after Aligned on the Clone Stamp and Healing Brush bars (`cloneSourceOptionsButton`, menu `cloneSourceMenu`) holds `CloneSourcePanel` (src/ui/clone_source_panel.hpp). Popup only: no column panel or Window menu entry (Seth, October 27, 2026). Lines under Point, Offset and the transform rows; the offset note is an info tooltip right of X (issue 574); Healing hides the transform rows (issue 596).
- **Slots.** Each canvas keeps four sources (`CanvasWidget::kCloneSourceSlotCount`): point, offset, whether the offset is fixed, transform. Alt-click sets the active one, outside the document clears it (issue 516), as does Clear (issue 574; Reset is the transform only). Clearing zeroes the slot's offset too, and the panel shows 0, 0 for an empty slot (issue 632). A switch stores the live slot and loads the other. Per document, session only.
- **Offset** is source minus destination. A typed one (Aligned on, source set) is fixed for the next strokes (`set_clone_source_offset`); with Aligned off every press resets it, so the fields are off.
- **Scale, rotation, flips** act about the source point; flips mirror first (negated inverse rows), so Rotation still turns the result clockwise. `core/clone_source_transform.hpp` maps a destination pixel to a Q8 source position in integer math (Q24 inverse matrix, quarter turns exact); the stamp samples it bilinear in premultiplied form (`sample_clone_source_rgba8`, the deep stamp in float) and skips positions whose nearest pixel is outside. The mapping is built at the press (`clone_stroke_mapping_`). At 100%, 0 degrees, no flip `CloneSourceMapping::identity()` keeps the plain offset read, byte-identical (`ui_clone_source_identity_transform_keeps_stroke_bytes`). The Healing Brush always samples untransformed: its reviewed source is the plain Alt-click offset ([legal-constraints-filters.md](legal-constraints-filters.md)), so a transformed healing source needs its own check.
- **Overlay.** Show overlay (on at 100% by default, issue 426) and its opacity (`tools/cloneOverlay`, `tools/cloneOverlayOpacity`, every canvas) draw the displayed composite (`render_cache_`) through the next stroke's mapping under the hovered brush, clipped to the footprint and the canvas (`draw_clone_source_overlay`). Only the source rect under the brush is drawn; a move or resize repaints old and new rects. A drag resize centres it on the size disc (issue 516). Hidden while painting, panning, or holding Alt outside a drag resize (issue 573). The checkbox is only in the popup: a bar copy wrapped the Healing Brush row at 1366 px (issue 458).
- **Layout** (issue 515): `sizeHint` adds height-for-width; a hint change (rows hidden on a tool switch too) re-measures the QMenu, which keeps item sizes across reopening.
- Tests: `clone_source_*` (core) and `ui_clone_source_*`.

## Spot Healing

Spot Healing removes a blemish with no source pick: the drag accumulates a soft procedural footprint (doc-sized Grayscale8 of brush_coverage capsules, a translucent trail like Quick Select's) and the heal runs once on release in `finish_spot_heal_stroke()`. Escape cancels the footprint before anything is written; `begin_edit` is deferred to release, so a canceled stroke leaves no history entry (a press-time `can_begin_pixel_edit` precheck still reports refusals immediately).

The texture comes from ONE coherent rigid mapping chosen in `core/spot_heal.cpp` (`spot_heal_source_map`): the footprint reflected across the line just past its nearest boundary, or translated clear of its own extent when the reflection would land back inside. Candidate directions (nearest-boundary, opposite, both perpendiculars; reflection before translation) are tried in a fixed order and scored only by mask-geometry validity: pixel content is never read, so there is no patch search. A coherent map keeps the sampled texture spatially continuous (a per-pixel mirror field smears radial streaks). The membrane then bends the pasted texture's tone into everything around the stroke. Off-canvas never acts as a source; edge blemishes sample from the interior side.

Options: Size, Soft (the footprint skirt feathers the heal), Sample. There is deliberately no Opacity, no Aligned, no Diffusion (the membrane replaces the ring radius), and no coverage cap: one write per pixel per stroke, outside the brush stroke compositor. Shift-click stroke connect and `[`/`]` sizing work as with the other brushes. Coverage: `spot_heal_source_map_*` and `heal_membrane_interpolates_boundary_offsets` in tests/core/infra_selection_tests.cpp; behavior plus the visual gallery (`ui_spot_healing_*`, `ui_spot_healing_and_patch_texture_gallery`) in tests/ui/brush_engine_stroke_tests_healing_patch.cpp.

## Patch tool

The Patch tool is a selection tool plus a heal: drawing reuses the lasso machinery wholesale (freehand outline, New/Add/Subtract/Intersect combine modes, feather and anti-alias, its own slot in the per-tool combine-mode table), or an existing selection of any origin can be used. A press inside the selection in Replace mode starts the drag; Shift/Alt force outline drawing (Photoshop parity). The drag shows a statically dashed copy of the outline at the pointer offset; Destination adds a raw ghost of the region there (the frozen snapshot cut to the mask). Source shows nothing inside the original spot ([legal-constraints-filters.md](legal-constraints-filters.md), US 10332291), and nothing recomposites or is classified per move. Release below the drag threshold is a no-op; Escape, tool switches, edit locks, and document teardown cancel the drag with no history entry; Enter commits a live drag like a release, and Enter with an outline and no drag runs Remove Object ([healing-remove-object.md](healing-remove-object.md)). Switching to Patch shows a how-to status line; committing an outline shows a mode-appropriate drag hint (Source adds the Enter shortcut).

The commit in `commit_patch_tool_drag()` runs once on release, with the drag offset as the only source choice. Modes:

- **Source** (default): the dragged-from region takes the dragged-to texture plus the membrane solved over the region (boundary offsets = destination minus source on the ring of uncovered cells, padded one cell). The selection stays put.
- **Destination**: the region is copied onto the drop point through the same membrane with roles swapped, and the selection follows the copy as its own history step after the pixel step.
- **Transparent** (both modes): texture-only transfer, destination pixel plus the source's high-pass detail (source minus its separable box-blurred local mean at the region-derived radius), with the membrane solved on the negated boundary detail so the overlay's edge residual fades to zero instead of printing the selection outline; destination alpha is kept. Nearly a no-op on same-texture content by design (it is for marks over plain backgrounds).

Writes follow the clone conventions: selection coverage from the drag mask, transparent-pixel lock, palette snap, layer expansion, out-of-canvas sources skipped (Dirichlet offsets read the snapshot clamped). Areas at or above 262,144 pixels fan out the blend over row strips (`std::async` plus `max_blocking_fanout_workers`, `PATCHY_RENDER_SINGLE_THREADED` honored); the membrane solves once up front and every healed pixel is a pure function of the snapshot, the mask, the offset, and the solved membrane, so the fan-out is byte-identical to the sequential walk. Both heal writers detach the layer's copy-on-write bytes on the calling thread before fanning out (the undo snapshot shares them; see the AGENTS.md invariant) and the strips write through one span. Mode and Transparent are session-only: every startup begins at Source with Transparent off (a persisted Destination or Transparent reads as "the tool is broken" a session later); mid-session choices still propagate to new documents. The retired keys `tools/patchMode` and `tools/patchTransparent` must never be reused. Coverage: `ui_patch_tool_*` (the outline-only Source preview in `ui_patch_tool_source_drag_preview_leaves_original_spot`), `ui_patch_options_sync_canvas_and_persist`, and the texture gallery.

## Content-Aware Move

Patch's drag, then a move or a copy on release; gesture, fill and options: [content-aware-move.md](content-aware-move.md).

## Red Eye

The Red Eye tool (issue 370): the user clicks or drags over an eye, fixed color rules grow the red area from that point and desaturate and darken it by each pixel's redness. Gesture, method, calibration and limits: [red-eye.md](red-eye.md).

## Palette placement

The Healing flyout (`healingToolButton`) holds Healing Brush (J, the default action), Spot Healing, Patch, Content-Aware Move, and Red Eye; Shift+J cycles through them ([tool-flyouts.md](tool-flyouts.md)). Hotkey ids `tools.healing`, `tools.spot_healing`, `tools.patch`, `tools.content_aware_move`, `tools.red_eye` are persisted contracts. Their icons keep distinct silhouettes ([tool-palette.md](tool-palette.md#icons)).
