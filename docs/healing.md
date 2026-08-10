# Healing tools

The healing family shares one legal envelope: every tool is user-directed and every blend is the classic frequency-separation math cleared by the expiry of Adobe's US 6587592 (2021). None of these tools may gain patch search, synthesis-by-example, reshuffling, gradient-domain or Poisson solves, live per-move classification, or content-driven source selection while the gates recorded in [legal-constraints.md](legal-constraints.md) are active (PatchMatch family US 8285055 / US 8340463 / US 8355592 into 2031, gradient-domain compositing US 9058699 to 2029, live classify-and-display US 8050498 to Nov 3, 2029). Constraint comments live at the implementation sites (`canvas_widget_shared.cpp` healing_sample, `canvas_widget_spot_healing.cpp`, `canvas_widget_patch_tool.cpp`, `core/spot_heal.cpp`) and must stay in sync with [patent-research.md](patent-research.md).

The shared math lives in `canvas_widget_shared.cpp`: `healing_ring_tone` (alpha-weighted 8-sample ring average with a center fallback) and `healing_sample` (destination ring tone plus the source pixel's difference from its own ring tone). The tone radius everywhere derives from the Healing Brush's Diffusion 1-7 formula `max(1, (size * (9 - diffusion) + 15) / 16)`; Diffusion persists as `tools/healingDiffusion` and its options-bar spin serves Healing, Spot Healing, and Patch.

## Sample All Layers

Clone, Healing, Spot Healing, and Patch share one Sample All Layers checkbox (canvas-owned `retouch_sample_all_layers_`, key `tools/retouchSampleAllLayers`, default on). Checked keeps the historical behavior: the per-gesture source snapshot is the merged document (`CanvasWidget::retouch_source_snapshot()` wraps `render_document_image_with_processing()`). Unchecked samples the active pixel layer alone through the promoted `active_layer_sample_image` helper (layer mask and opacity folded into alpha), which the Magic Wand and Quick Select sample options also use. The snapshot is taken once at gesture start, never per move. `ui_retouch_sample_all_layers_switches_clone_and_healing_source` pins both states.

## Classic Healing Brush

The Healing Brush is an explicit-source pixel tool: Alt-click picks a source from the gesture snapshot, Aligned shares Clone's across-stroke offset, and Diffusion 1-7 sets the radius separating source detail from local tone. Each output sample adds the source center's difference from its alpha-weighted eight-sample source ring to the matching destination ring tone. The stroke shares Clone's size, opacity, softness, selection, transparent-pixel lock, palette snap, undo, and source-snapshot behavior. Healing writes only ordinary layer pixels; PSD/PSB round-trips need no private metadata. `ui_healing_brush_transfers_detail_and_preserves_destination_tone` pins the math and the PSD round trip.

## Spot Healing

Spot Healing removes a blemish with no source pick: the drag accumulates a soft procedural footprint (doc-sized Grayscale8, stamped as brush_coverage capsules, shown as a translucent trail like Quick Select's) and the heal runs once on release in `finish_spot_heal_stroke()`. Escape cancels the footprint before anything is written; `begin_edit` is deferred to release, so a canceled stroke leaves no history entry (a press-time `can_begin_pixel_edit` precheck still reports refusals immediately).

Sources come from fixed mirror geometry in `core/spot_heal.cpp` (`spot_heal_mirror_sources`): a deterministic two-pass nearest-point transform finds each covered pixel's nearest outside pixel, and the source is its reflection past the rim by a fixed margin, stepping onward in whole-vector increments when a concave footprint re-covers it. Off-canvas never acts as a source, so edge blemishes mirror from the interior. The heal is `healing_sample(snapshot, mirror_source, rim_point, tone_radius)`: the destination tone is sampled at the rim point rather than at the pixel itself, because the pixel sits inside the defect and its own ring tone would preserve the blemish. The choice depends only on the footprint's shape, never on pixel content.

Options: Size, Soft (the footprint skirt is what feathers the heal), Diffusion, Sample All Layers. There is deliberately no Opacity (a partial heal reads as tool failure; Photoshop omits it too), no Aligned, and no coverage cap: one write per pixel per stroke, outside the brush stroke compositor. Shift-click stroke connect and `[`/`]` sizing work as with the other brushes. Core geometry coverage: `spot_heal_mirror_sources_*` in tests/core/infra_selection_tests.cpp; behavior: `ui_spot_healing_*` in tests/ui/brush_engine_stroke_tests_healing_patch.cpp.

## Patch tool

The Patch tool is a selection tool plus a heal: drawing reuses the lasso machinery wholesale (freehand outline, New/Add/Subtract/Intersect combine modes, feather and anti-alias, its own slot in the per-tool combine-mode table), or an existing selection of any origin can be used. A press inside the selection in Replace mode starts the drag; Shift/Alt force outline drawing (Photoshop parity). The drag previews a raw translated blit of the frozen gesture snapshot plus a statically dashed copy of the outline; nothing recomposites and nothing is classified per move. Release below the drag threshold is a no-op; Escape, tool switches, edit locks, and document teardown cancel the drag with no history entry.

The commit in `commit_patch_tool_drag()` runs once on release, with the drag offset as the only source choice. The tone radius feeds the region's area-equivalent disc diameter through the Diffusion formula. Modes:

- **Source** (default): each selected pixel takes the classic heal with its destination tone from `patch_ring_tone_outside_mask`, an 8-sample ring weighted by the pixel's own selection mask so the blemish cannot tone-match itself; interior pixels retry at doubled radii (a fixed ladder) until the ring clears the boundary. The selection stays put.
- **Destination**: the region is copied onto the drop point with the unmasked classic heal (the drop area is legitimate content), and the selection follows the copy as its own history step after the pixel step.
- **Transparent** (both modes): texture-only transfer, destination pixel plus the source's difference from its own ring tone; destination alpha is kept.

Writes follow the clone conventions: selection coverage from the drag mask, transparent-pixel lock, palette snap, layer expansion, out-of-canvas sources skipped. Areas at or above 262,144 pixels fan out over row strips (`std::async` plus `max_blocking_fanout_workers`, `PATCHY_RENDER_SINGLE_THREADED` honored); every healed pixel is a pure function of the snapshot, the mask, and the offset, so the fan-out is byte-identical to the sequential walk. Settings: `tools/patchMode` (0 Source, 1 Destination), `tools/patchTransparent`. Coverage: `ui_patch_tool_*` and `ui_patch_options_sync_canvas_and_persist` in tests/ui/brush_engine_stroke_tests_healing_patch.cpp.

## Palette placement

The Healing flyout (`healingToolButton`) holds Healing Brush (J, the default action), Spot Healing (Shift+J), and Patch (unbound, like Sponge and Sharpen). Hotkey ids `tools.healing`, `tools.spot_healing`, `tools.patch` are persisted contracts. Icon silhouettes stay distinct at palette size: bandage (Healing), circle with burst ticks (Spot Healing), rotated stitched square (Patch).
