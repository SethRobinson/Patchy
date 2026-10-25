# Interactive drag previews: Move proxy/base machinery and the free-transform preview

[performance.md](performance.md) owns the universal rules and the stress harness. Read this before changing the Move tool's drag previews, the preview-scaled document, or the free-transform preview.

## Visibility render waits

`set_layer_visibility` updates the flags and rows immediately, then calls
`layer_visibility_changed`. The canvas patches visibility in its scaled scene,
invalidates the stationary Move backdrop, and queues a full snapshot refresh.
Same-turn edits coalesce before snapshotting; there is one active refresh and
one latest pending state. Completion checks its generation. Paint holds the
previous image, while the dirty flag forces exact-pixel readers to refresh.
Other edits cancel the hold and retain full invalidation, so a partial patch
cannot leave stale pixels from an unfinished visibility refresh.

Folder visibility still rebuilds rows to preserve all inherited control states;
the persistent viewport owns the sweep's grab. The native-window eye-sweep
regression injects slow rendering, crosses expanded/collapsed folders, releases
outside the list, and compares the settled canvas with a full refresh.
Do not remove `ExcludeUserInputEvents` from synchronous processing waits:
those workers read the live Document and must retain their reentrancy guards.

## Move-drag proxy preview

Heavy Move-tool drags latch onto a translated-snapshot proxy (canvas_widget_move.cpp / canvas_widget_events.cpp):

- The gate `moving_layers_should_use_outline_preview` measures WORK, not extent: the sum of every moving layer's canvas-clipped effect rect (averaged over the old and new deltas), against compile-time area thresholds (lower for styled layers). A dragged folder of overlapping stacked copies costs one composite per member per preview patch, so the per-layer sum predicts the frame cost while a bounding box stays tiny. For disjoint layers the sum never exceeds the bounding-box area, so the stress contract calibration holds.
- Folder styles count: `MovingLayer.expensive_style` includes every ancestor whose `group_style_renders`, and `MovingLayer.ancestor_effect_padding` SUMS the styled ancestors' `layer_style_effect_padding` per nesting level (mirroring the compositor's nested outsets; core's `layer_effect_padding` maxes and must not be used for this). The padding feeds `moving_layer_effect_rect`, which sizes the gate metric, the live preview patches, the base-cache hole, and the release patch region, so a styled folder's shadow spill is repainted on commit.
- On latch (sticky per drag, counted once in `move_proxy_previews`): build the base cache (moving layers hidden via render overrides; `ensure_move_base_cache` never toggles `set_visible`, which bumps revisions and cold-invalidates the style-mask/alpha-bounds caches) plus a one-time snapshot of ONLY the moving subtree (`ensure_move_proxy_image`): non-moving non-adjustment leaves are hidden, so intra-set blending, clip runs, styled ancestor folders, and adjustment layers bake in. Snapshots above `kMoveProxyMaxPixels` downscale by sqrt like the transform proxy; above `kMoveProxyLastResortSnapshotArea` the drag keeps the dashed outline (counted in `move_outline_previews`). Per frame the paint is base + one translated blit.
- Off-canvas content: the compositor clips renders to the canvas, so a set hanging off it snapshots shifted onto the canvas through bounds overrides (`move_proxy_snapshot_shift`, whole 2^level blocks, both the synchronous and worker builds) and `move_proxy_document_rect_` maps back; only a set larger than the canvas stays clipped. The dashed outline also draws on the pasteboard, so `moving_layers_outline_dirty_rect` is the one drag repaint NOT clipped to the canvas (release repaints it too). Pinned by `ui_move_proxy_snapshots_offcanvas_layer_content` and `ui_move_outline_repaints_on_pasteboard`.
- Approximations until release (same contract as the transform proxy): blend modes against the real backdrop read as Normal, deep-zoom (>= 6x) blits skip the deep-zoom raster and its pixel grid. Group effect geometry is exact for pure translation (effects derive from the silhouette, which translates rigidly). Mouse release schedules accurate patches when a hold or cold-preparation request exists; other expensive releases use `force_processing_wait`.
- Time escape hatch: the area gate only prices the MOVING layers, so a cheap layer dragged across an expensive styled stack stays live no matter how slow it is. A live preview frame slower than `live_preview_frame_latch_ms()` (canvas_widget_shared.cpp; env `PATCHY_MOVE_LIVE_LATCH_MS`, where 0 latches after any live frame, is the test hook) latches the proxy on the next move. The latch PERSISTS across drags of the same moving set at the same composite level (`move_live_latch_key`, mirroring the transform session latch), so a re-drag proxies from its first frame; document/tool/lock changes or a key mismatch at press re-price. Machine-dependent by design; the stress contract steps stay well under the threshold on the reference machine.
- A move drag never synchronously recomposites a dirty render cache: the drag path has no `ensure_render_cache` call. The base build is self-sufficient (scaled document at level >= 1; at level 0 a dirty cache takes one banded whole-canvas hidden-layer render), and the post-release async refresh owns full invalidations.
- Retention across drags: the base cache and proxy snapshot survive a commit on the precommit-patch and zero-delta release routes, keyed by the sorted moving ids + build composite level (`move_preview_cache_reuses` counts one reuse per press). The base excludes the moving set so its translation cannot invalidate it; the proxy rect translates by the committed delta (losing mip-grid alignment, the same AA-phase approximation every mid-drag blit has), and a snapshot still canvas-clipped after the shift is dropped instead. Every external change funnels through `invalidate_retained_move_caches` - document_changed_impl plus the bypassing overloads (`document_changed()`, `document_changed_async_preview`, `force_refresh`, the mask/channel edit branches), which drop the preview-scaled document except for visibility-only updates - and a change landing mid-drag defers the clear to release (no retention that drag).
- Release-commit integrity: the commit captures `commit_delta`, the moving set, and the committed ids into locals at release entry and reads only those through patch render, layer mutation, and retention. Canvas input handlers drop user input while `processing_render_wait_active_` (releases are parked in `deferred_wait_release_` and replayed after the outermost wait unwinds so open gestures still end), and ShortcutOverride is accepted, because the wasm nested waits deliver DOM input synchronously (see wasm.md). The precommit-patch route also marks an in-flight async render-cache refresh pending (its snapshot predates the commit; the completion re-snapshots instead of installing stale pre-move pixels), and `patch_render_cache_patches` fails closed: one invalid patch rejects the whole set so the release falls back to `document_changed_effect_bounds`. Pinned by `ui_move_commit_ignores_reentrant_input_during_processing_wait` plus the pinball fixture test's force-refresh pixel comparison.
- Latch hitch bounded by banded renders: the base-hole and snapshot renders go through `qimage_from_document_rect_with_hidden_layers_banded` (image_document_io.cpp), horizontal bands across workers. PREVIEW-ONLY: band-windowed style blurs can differ ~1-2/255 from the unbanded render near styled layers; commit and render-cache patch paths never consume banded output.

Large/deep documents use `should_prepare_move_preview_async`, sharing the
full-refresh size/layer thresholds. A cold Move starts one snapshot worker and
draws a moving outline immediately with the animated, nonblocking
"Rendering preview..." canvas badge. Its animation continues while the pointer
is stationary; the badge disappears on completion, release or cancellation,
even if an abandoned worker is still finishing. Other preview operations retain
their delayed badge. The worker builds the scaled scene, hidden
backdrop and proxy; completion checks cancellation, gesture generation, moving
IDs and preview level, then paints at the current delta. A superseding request
waits for the existing worker; cancellation is checked between preparation
stages. Small documents and single-threaded wasm retain synchronous preparation.
Release before readiness still commits geometry and queues accurate patches,
holding the prior canvas until they land. Approximate pixels never enter the
accurate render cache. `render_settled()` includes preview work;
`manylayers` reports first-frame, preview-ready and final-settle times separately.
`PATCHY_PERF_VERIFY_FINAL=1` additionally compares the settled canvas with a
fresh full render after timing.

Idle focus loss preserves completed base/proxy images and the slow-frame latch;
focus loss during a gesture still cancels it. Visibility patches the scaled
scene in place. Other invalidations drop the scene but preserve reusable
surface mip buffers. A clipped proxy rebuilds on the next drag. Tool, document, selection and zoom boundaries still validate or
discard the retained images.

Artboards (docs/artboards.md, "Fast paths"): the proxy snapshot unclips the moving layers' artboards and paint clips the blit to the artboard the content lands in; the live patches render from the re-nested copy auto-nesting will produce.

## Deferred Move commit

A Move release defers its accurate full-res patches (vacated plus destination region) instead of rendering them under the processing overlay (`render_document_patches_with_processing`) whenever that render would wait (`force_processing_wait` or `dirty_region_should_use_processing_wait`) and a preview frame can be held (`can_hold_move_commit_preview`: a base cache plus either the proxy or live patches rendered at the release delta; never on single-threaded wasm. A canvas-clipped proxy still holds: its gap beats the freeze):

- The layer bounds and metadata change at once and the history entry is pushed as before; the panel and host are notified immediately.
- `arm_move_commit_hold` copies the base (possibly mip-sized) plus the proxy at its committed rect, or the live patches; `paintEvent` draws that hold instead of the render cache while `move_commit_job_` is pending (the transform hold's rules). Past the standard overlay delay the "Processing..." badge shows over the hold (`background_refresh_overlay_visible`; also during a same-size async full refresh, as after Undo). A repaint marks that refresh pending only if an invalidation bumped the generation since its snapshot (`async_render_cache_in_flight_generation_`); the badge's own repaints must not, or a refresh slower than the badge delay restarts at every completion forever (`ui_background_refresh_lands_while_processing_badge_animates`).
- `start_move_commit_job` snapshots the document (COW) and renders the accurate patches on a tracked worker; the queued completion (`finish_move_commit_job`) patches the render cache if its generation still matches, otherwise it does nothing. The cache is stale inside the job region but NEVER dirty meanwhile, so no full refresh races the job.
- Exact-pixel readers wait: the Magic Wand / Quick Select / Magnetic Lasso source captures, the curves clipping preview, `rebuild_transform_base_cache`, and the level-0 `ensure_move_base_cache` call `wait_for_move_commit_job` (the processing-overlay wait) before reading the cache. Paint never waits, and `ensure_render_cache` itself does not either (`QWidget::paintingActive()` is false inside `paintEvent` here, so a guard there would block every repaint on the job). `render_settled()` is false while a job is pending, so test settle loops cover it.
- Any other document change cancels the job: `document_changed_impl` folds the job region into a partial invalidation first; the full-invalidation entry points (`document_changed`, `document_changed_async_preview`, `force_refresh`, `active_edit_target_changed_impl`, `set_document_internal`) just cancel, since they refresh everything. A further Move commit replaces the pending request over the union of all unlanded regions from the newer snapshot.
- Diagnostics: `move_deferred_commits` counts deferred releases; a landed job counts as a `move_precommit_patches` patch. `PATCHY_RENDER_TRACE=1` prints `deferred=1` on the release line; `manylayers` (below) times it end to end.
- Tests: `ui_move_release_defers_accurate_patches_behind_a_hold`, `ui_move_deferred_commit_yields_to_undo`, `ui_move_deferred_commit_serves_exact_pixels_to_readers`, `ui_move_second_drag_while_commit_pending_merges_jobs`, `ui_move_deferred_commit_shows_processing_badge_after_delay` (all on `DeferredMoveScene`: proxy latched via `PATCHY_MOVE_LIVE_LATCH_MS=0`, worker slowed by `PATCHY_PROCESSING_RENDER_TEST_DELAY_MS`). The reentrancy test keeps the synchronous route on purpose (its release delta differs from the last live patch, so no hold exists).

`MoveCommitWorker` bounds accurate Move work to one running render plus one
replaceable pending request per canvas. Requests accumulate all unlanded dirty
regions and carry the latest immutable snapshot. Supersession cancels at region
boundaries; an active region finishes safely. Replaced pending promises resolve
without rendering. The worker drains the queue itself, so exact-pixel readers
can wait without needing UI completion delivery to launch the latest work.
Readers recheck for a replaced job after each wait. Generation checks still
reject obsolete results. Rapid-commit and Undo regressions compare final pixels
against independent full refreshes.

## Display-resolution move previews

`PreviewScaleCache` retains one level of pixel, raster-mask and vector-mask
surfaces per current layer. Keys compare COW storage identity, shape, format and
level. Owning the source prevents pointer reuse and makes subsequent mutations
detach; no pixel scans are needed to check a hit. Every build copies current
metadata and rescales styles/geometry, reusing only unchanged raster buffers.
Deleted layers and removed surfaces are pruned, level changes replace the
cache, and document/session replacement drops it. Background preparation uses
its own cache copy and publishes it only with a valid result.

At zoom <= 50% the move drag composites its previews from a PREVIEW-ONLY scaled document: `build_preview_scaled_document` (core/layer_render_utils.cpp) copies the document (PixelBuffers are COW) and box-downscales every raster surface by 2^level - layer pixels (alpha-weighted; deep ones per typed sample), masks, vector-mask caches - with bounds floored and buffer-matched, and style falloffs (shadow/glow/stroke/satin/bevel sizes and distances, vector feather) divided to match. Ids and structure are preserved so the move overrides work unchanged. `preview_composite_level_for_zoom` (canvas_widget_shared.cpp) is the display mip level clamped to 3. The canvas caches one scaled document (`preview_scaled_document_`, prepared per level, kept across drags and visibility changes, otherwise rebuilt from the reusable surface cache); the move base cache, proxy snapshot, and live patches all render from it at 4^level less cost, patch `document_rect`s staying full-res while the images draw directly as their own mips. A committed move whose release patched the render cache keeps the scaled document and REFITS its copies of the moved layers (`retarget_preview_scaled_for_committed_move` -> `retarget_preview_scaled_layer_bounds`, recomputing positions from the committed full-res layers, never translating by a scaled delta, which floors differently); the proxy snapshot renders moved layers from the scaled document WITHOUT bounds overrides. `RenderCacheDiagnostics::move_scaled_previews` counts one live-path engagement per drag. Approximation contract: blending downscaled sources is not the same math as downscaling a full-res composite, pattern fills keep full-res tiles, and scaled patches are never reused for the release cache patch - release always renders full-res.

At pixel-aligned zoom paint places a patch through the base image's own mapping (`pixel_aligned_target_rect` origin plus `document_rect` times its scale). Rounding the patch's own rect put move, transform and warp patches a pixel off the base under a fractional pan: `toAlignedRect()` rounds outward, `std::round` treats a negative half-pixel pan differently (GitHub #200).

## Free-transform preview

The base cache + region patches preview and multi-target Free Transform are in [free-transform.md](free-transform.md).

Clip-group drags (GitHub #398): a clip run above the target counts as content above, so a clipped shape used to composite a patch over its whole transformed area per mouse-move (26 ms against 5 ms unclipped at 1080p, 80 ms at 4000x3000, where the proxy latch then dropped the members until release). When `transform_clip_group_preview_possible` holds (Pixel-kind Normal base without rendered style, mask, Blend If, channel restriction or knockout; visible members all non-adjustment Normal leaves; nothing visible above the run; ancestors pass-through or full-strength Normal folders without masks or styles; no artboard; 8-bit RGB, not palette), drag frames take neither patches nor the proxy: `ensure_transform_clip_group_image` renders the members once per session over transparency (members unclipped on a copy-on-write copy, every other leaf hidden, banded, bounded by `kTransformProxyMaxPixels`), and `draw_transform_clip_group_preview` paints the transformed source at opacity times Fill into an offscreen image over its widget extent, draws that snapshot `CompositionMode_SourceAtop`, and blits the result over the base cache, which already hides the run. Source-atop is the compositor's frozen clip merge for Normal members (members over the base color at full strength, coverage from the base), so the frame matches the accurate render up to resampling; release renders the accurate patches as after a proxy latch. Anything else keeps the composited patches. Counted once per drag in `transform_clip_group_previews`. Pinned by `ui_free_transform_clipped_base_drag_blits_clip_group` (frame against the resting render), `ui_free_transform_clipped_base_keeps_patches_when_blit_cannot_match` and `patchy_perf_tests clipshape` (clipped within 3x + 8 ms of the unclipped drag).

## Path overlay during drags

`draw_path_edit_overlay` maps a layer-owned path (shape path, vector-mask path, panel-selected shape outlines) through `CanvasWidget::layer_preview_transform(id)`: the Move delta while the layer is in `moving_layers_`, or `free_transform_preview_delta()` (the session's `free_transform_delta`) while it is the transform target. Display only; hit-testing keeps document coordinates because the session owns the mouse. Document paths (work path, saved rows) stay put. A shape layer's own path draws only under the path and shape tools (vector-tools.md), so under Move it shows no outline; a vector-mask path rides the Move drag. `path_overlay_preview_document_rect()` (old and new extents, padded for anchor squares) joins the move repaint in `mouseMoveEvent` and `transform_preview_document_rect()`, so the bounded drag repaints never leave a stale outline. Pinned by `ui_path_overlay_follows_move_drag` and `ui_path_overlay_follows_free_transform`. The drag readout panel ([tools.md](tools.md)) rides the same rule: `update_drag_readout_region` unions its last painted and next widget rects on every drag frame (painting records the rect, GitHub issue 352), and release/cancel clear it (`ui_move_handle_drag_readout_leaves_no_trail`).
