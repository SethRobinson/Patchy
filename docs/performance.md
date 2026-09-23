# Performance: the rendering-performance rules

Read before rendering work. `AGENTS.md` owns universal revision/repaint rules; the profiling stress test (PATCHY 64 scene, its steps, baselines and reports) is in [stress-test.md](stress-test.md). The optional screen-resolution renderer is documented in [Dynamic Vector Preview](vector-preview.md). Startup timing, the `patchy_perf_tests startup` scenario and its regression check are in [startup-performance.md](startup-performance.md).

Related documents:

- [performance-strokes.md](performance-strokes.md): undo history (copy-on-write snapshots, stroke patches, the byte budget, restores and History-panel jumps), layer growth under a stroke, and per-move paint-stroke cost (patched display mips, coalesced input, round-brush and stamp-segment skips).
- [performance-compositor.md](performance-compositor.md): compositor row kernels, the mask coverage plane and the byte-identity corpus, the remaining slow paths and why GPU compositing stays unpursued, and the Gaussian line filters.

## Smart invalidation and style-mask caching

During a pixel brush stroke, expand damage by the sampling reach of the active layer's styles and each styled ancestor. Interior shading can change inside existing ink beyond a dab. Include both the translation and blur for Satin and inner shadows, and the height and soften reach for Bevel. Spread and choke consume part of a glow or shadow's Size. For a patch below the large-update threshold and smaller than a quarter of the painted layer, use the compositor's draw-clipped mask windows for that layer. Unedited layers and larger updates keep the content-revision cache. Compare the in-stroke composite with a fresh CPU render in `ui_styled_brush_damage_matches_full_render`.

A stroke on the layer's mask takes the same route (`brush_update` in `document_changed_impl` covers `LayerEditTarget::Mask`): a mask dab changes the layer's silhouette as an eraser dab does, and without the windows every move rebuilt every mask over the whole layer. A feathered mask keeps the plain region refresh, because a dab then reaches as far as the feather.

A dab also changes every group around the painted layer. A group with effects, a knockout group and a folder that is a clipping base composite from their flattened children (`group_silhouette_for_render`), cached under a revision that mixes in every descendant's, and a group's effect masks cover the whole group. Each dab therefore flattened every child over the whole group and rebuilt every group mask. During a stroke the canvas keeps, for each group on the way down to the painted layer (and for the painted group itself when the stroke is on its mask), the revision of the state it last rendered: `brush_group_bases_`, set by `capture_brush_group_bases` at the press and after every patch. The patch render gets them as `StrokePatchWindows::GroupBase` with the rects where that group's children changed (the dab grown by the effects below the group, recorded by `expand_brush_damage`); `DocumentStyleMaskProvider::group_silhouette_base` hands the compositor the cached silhouette of that state, and it flattens the children again inside those rects only. The base from the press stays cached, since Undo renders that state. Later bases are the stroke's own steps: they are taken out of the cache and patched in place, so a long stroke neither copies the silhouette per dab nor pushes other layers' masks out of the LRU. Groups with effects also take mask windows (`StyleMaskProvider::group_masks_windowed`) under the painted layer's size gate. The bases fail closed: `document_changed_impl` takes them out and only the patch it renders writes them back, `cancel_brush_realign_job` and `set_document` clear them, and a silhouette of another size (a layer that grew under the stroke) is refused, so after anything but a further dab on the same layer the next patch flattens the group once. At the end of a stroke `finish_paint_stroke_history` moves the last silhouette to the group's revision then (`rekey_style_masks`), because the release can bump a revision without changing a pixel and the next press would otherwise find nothing. A background realign pass gets the same bases when it starts (never to consume): it snapshots the state the mouse handler is about to patch, and without them it could reach the silhouette first and flatten the whole group while the handler waits on that mask's latch. Nothing is taken out of the cache while a pass runs. Still whole-group per move: a group whose own effects include a layer-aligned gradient (`expand_brush_damage` adds its whole bounds), and the passes a realign job starts after its first, which carry no bases. `ui_styled_group_and_mask_strokes_match_full_render` compares every frame of strokes in nine scenes (layer mask, pass-through, isolated and nested groups with effects, a clipping-base folder, a group mask, the Eraser, 16-bit, a layer-aligned gradient inside a styled group) with a fresh render and checks that the state before the stroke keeps its cached masks.

Image > Rotate and Flip of the whole canvas (the quarter turns, 180, both flips, Rotate Arbitrary) run on a worker against a private copy of the document while the canvas shows its delayed Processing overlay (`MainWindow::changed_document_with_processing`, the `resize_document_image` pattern: the displayed document stays whole during the wait, the undo snapshot and the swap follow it). The quarter turn itself copies in 64 px tiles (`rotate_quarter_turn_tiles`, document_geometry.cpp): a plain row walk writes one destination column per source row, a cache and TLB miss per pixel on a wide buffer. `patchy_perf_tests canvasrotate` on a 32000x4000 RGBA canvas (i9-14900HX): clockwise 2.84 s before and 0.19 s after, 180 degrees 5.7 s and 0.51 s. Pinned by `ui_rotate_canvas_runs_behind_the_processing_overlay` and `layer_quarter_turns_move_every_pixel_across_tile_edges`. Layer > Arrange > Rotate still turns its layers on the UI thread.

`patchy_perf_tests layerconditions` times one stroke of each paint tool, a run of arrow-key nudges, a held arrow key, a Move drag and a shape anchor drag on the same 2480 x 3508 art under ten layer conditions (plain, effects, an aligned gradient, a plain and a styled group, under adjustment layers, the mask of a plain and of a styled layer, a shape layer without and with effects; `PATCHY_PERF_CONDITIONS`, `PATCHY_PERF_CONDITION_CASES`), and fails when a Brush move in a styled group or on a styled layer's mask costs over four times one on the styled layer, or when a point drag, a nudge or a held arrow key on the shape layer with effects costs over three times the same on the one without. On an i7-8700K (12 threads) at fit zoom with a 100 px brush, a move including its event pass: plain 1 ms, on a layer with five effects 58 ms, inside a group with those effects 2.0 s before and 65 ms after (Clone, Smudge, Dodge and Eraser 1.2 s to about 40 ms), on the mask of a layer with effects 1.8 s before and 105 ms after. The shape layer with effects, same machine: an arrow-key nudge 505 ms a key before and 5 ms after (the patch lands about 170 ms later from the Move commit worker, [move-tool.md](move-tool.md#arrow-key-nudges)), a key held for a second 15.3 s of key handling before and none after, a Direct Select point drag 837 ms a move before and 92 ms after with 0.47 s at release for the drafted effects. A held arrow on the full-canvas pixel layer landed no frame until the key came up and now lands 18 in the second. What the scenario still shows as slow in November 2026: a shape point drag bakes the whole shape on every move (90 ms without effects); a layer whose effects cross the canvas edge has its masks rebuilt for every patch and composites in one pass (a nudge's background render takes 0.7 to 0.9 s); a nudge inside a group with effects flattens the group again (1.4 to 2.3 s a render, so a one-second hold shows one frame at its end); installing a 3 Mpx patch costs about 25 ms on the key handler's thread.

A layer-aligned gradient recolors the painted layer's whole silhouette when a dab moves its alpha bounds, and a stroke that leaves the painted bounds moves them on every move. The mouse handler patches only the brush area. The silhouette goes to a brush realign job (`BrushRealignJob`, canvas_widget.hpp): a worker renders it from a document snapshot with mask windows, in row bands across a quarter of the workers (`render_document_region` splits a large rect when a windowed layer is named; with every worker on the bands the handler's own moves ran three to four times slower), and a queued completion lands it. Areas the stroke patched after the snapshot are left out of the landing. An anchor that moved again starts the next pass when this one lands, so one pass runs at a time and the frame is exact once the stroke rests. `render_settled()` is false while a pass is pending, `wait_for_move_commit_job()` waits for it, a full refresh drops it, and any change other than a further dab on the same layer cancels it and takes over its region. A silhouette under 65,536 pixels, and every silhouette where background work runs inline, is redrawn in the handler. Never fold the silhouette back into the handler's region or route it to a full refresh: on a 2480 x 3508 layer with Satin, Bevel, Outer Glow and Stroke each move cost 0.8 to 1.6 s, and a full refresh restarts on every move. `ui_styled_brush_growth_redraws_silhouette_in_background` covers the moves, passes that overlap later dabs, and a change that cancels a pass.

A snapshot undo/redo step diffs the two documents per layer (globally-unique revisions + visibility; structure/document-level changes fall back to full) and invalidates only the changed effect-bounds region below 8 Mpx (`history_restore_changed_region`, session_history.cpp). The expensive per-effect style masks (EDT/spread/interior blurs) are cached across renders keyed by layer CONTENT revision with domains stored relative to the layer origin, so moves and patch renders reuse them (`StyleMaskProvider` in render/layer_render_types.hpp, LRU + in-flight latch in image_document_io.cpp; `PATCHY_STYLE_MASK_CACHE_OFF=1` disables). The LRU budget is `style_mask_cache_budget_bytes()` (memory_info.hpp; `PATCHY_STYLE_MASK_BUDGET_TEST_MB` for tests). An LRU smaller than a document's masks thrashes: every render rebuilds every mask and strips serialize behind the latch (a 4000x2781 poster with about 600 MB of masks renders twice as slowly under a fixed 256 MB as when resident). The commit side upholds this: a pure-translation move bumps ONLY render revisions (`translate_moved_layer_metadata` goes through Layer's render-only mutators, and the vector/smart writer dirty marks use `metadata_without_content_bump`), so masks and the baked styled-layer cache stay warm across move commits. Document-positioned caches (layer-panel content/mask thumbnails, the paths panel's transient layer-path row) therefore key on the RENDER revision. Channel-separable adjustments composite through exact 256-entry LUTs (`build_adjustment_lut`); its exclusion list keeps channel-mixing kinds (Hue/Saturation, Threshold, the #250 kinds) and ink-space adjustments per-pixel, and a new channel-mixing kind must join it. Cached-mask renders are only used where the legacy windowed domain equals the full domain (gate in DocumentStyleMaskProvider), so full-render bytes are unchanged (pinned compositor tests hold).

An edit that only shows or hides effects keeps the layer's masks. The mutable `layer_style()` accessor bumps the content revision, so without help the next frame recomputed every mask, and one mask is computed by one thread while the other strips wait on its latch: on a 2480 x 3508 layer with Bevel, Stroke, Satin, Gradient Overlay and Outer Glow that was about 410 ms of a 750 ms frame (6 cores, 12 threads). The Layers panel's effect eyes, their sweep and the Layer Styles eye (`apply_layer_effect_row_edit` with `LayerEffectRowEdit::Visibility`) read `style_mask_cache_revision` right before the edit and call `carry_style_masks_across_effect_visibility_edit` after it (image_document_io.hpp): the masks cached under the old revision are filed under the new one too, sharing their planes, and the old keys stay, so undo and redo find theirs. Each copy counts its full size against the budget, which only makes stale copies leave first. This is sound because a mask is made from the layer's pixels and masks and its own effect's settings, and the effect index is the instance's position in its list. The exception is a Stroke Emboss bevel, whose height field is built from the strokes that are on: a layer with one leaves its BevelHeight masks behind. A new mask that reads another effect's `enabled` switch must be left out there too, and no edit that changes a setting, adds, removes or reorders an effect may carry. `style_mask_cache_misses()` counts computed masks for tests. `patchy_perf_tests effecteye` clicks each effect eye of that page off and on (local-test-fixtures/local-063, `PATCHY_PERF_EFFECTEYE_PSD` for another file, [SKIP] when absent) and reports handler, eye paint and finished-frame times, the masks each frame computed and whether the frame equals a render with no cached mask: median frame 747 ms before, 331 ms after, the handler about 20 ms either way. The rest is the recomposite itself. Tests: `ui_layer_effect_eye_toggle_reuses_style_masks_and_matches_cold_render`, `ui_layer_effect_eye_toggle_recomputes_stroke_emboss_bevel`.

## Reads must not bump layer revisions

Layer's mutable accessors bump render/content revisions on ACCESS, so read-only code must go through const layers (`std::as_const`), or it silently invalidates every revision-keyed cache (a lookup walk like `Document::find_layer` would bump thousands of layers per frame; it walks const). Hunt regressions with `PATCHY_REV_TRACE=1` (stderr REVBUMP lines per accessor+layer).

## Nothing O(layer pixels) may run per repaint

`opaque_pixel_local_rect` (the Move tool's passive-box bounds) reaches paint through `move_transform_controls_rect`, so it must never rescan the alpha channel per frame (on a 70 Mpx layer that costs about half a second per zoom/pan step). The shared `visible_alpha_local_bounds(const Layer&)` helper in `layer_render_utils.cpp` serves the Move tool and alpha-aligned layer effects: a row scan with early exits and a bounded LRU keyed by the app-globally-unique PIXEL revision, with a per-key in-flight latch so parallel render strips do not duplicate a cold scan. Style edits do not change the pixel revision; mutable pixel access and `set_pixels` do. `PATCHY_ZOOM_TRACE=1` prints paint/zoom/view-changed phase timings over 2 ms to stderr, and `patchy_perf_tests.exe zoom` (env `PATCHY_PERF_ONSCREEN=1`, `PATCHY_PERF_ZOOM_BG=1` selects the tent's 70 Mpx BG layer, `PATCHY_PERF_ZOOM_SELECTION=1` adds marching ants) measures per-step latency on the PSBtest tent (ceiling: about 15 ms per step maximized).

## Interactive drag previews (Move and Free Transform)

Move drags (proxy/base machinery, the display-resolution preview-scaled document, cache retention) live in [interactive-previews.md](interactive-previews.md), Free Transform drags in [free-transform-preview.md](free-transform-preview.md); read them first. The universal rule stays here: a drag handler must never synchronously recomposite the document: previews render from bounded caches and full invalidations belong to the post-release async refresh. Commit-path bytes are untouched by the preview machinery by design (corpus digests and the transform-commit suites pin it), and the stress move matrix (steps 29-33) plus step `61_transform_drag_proxy` pin the latch counters ([stress-test.md](stress-test.md)).

## Text edit sessions

What keeps an inline text session cheap (none of it changes committed pixels):

- **The edited layer is hidden only when its preview replaces it**, in the same
  `document_changed_effect_bounds` call (`hide_text_editor_source_layer`).
- **The expensive-style debounce keeps the last preview** through
  `kExpensiveTextEditorPreviewDelayMs` rather than removing the preview layer (a per-keystroke
  insert/remove of a styled layer plus two recomposites).
- **Commit repaints are bounded** to the committed layer's effect bounds (no bare
  `document_changed()` in `commit_text_editor`).

## Layer-panel rebuilds and selection

The Layers panel's rules (three-pass rebuild order, no rebuild on canvas-driven selection of existing rows, viewport-bounded row masks) live in [layer-panel.md](layer-panel.md#rebuilds-and-absent-rows). Profiling: `refresh_layer_list`, `set_active_layer_from_selection`, `restyle_layer_rows`, `update_layer_target_styles`, and `refresh_options_bar` log under `PATCHY_UI_PROFILE=1`; `PATCHY_ZOOM_TRACE=1` adds the Move-tool press phases (`move_press.*`, `handle_transform_start` for a passive-handle grab). `patchy_perf_tests.exe layerpanel` times the Quintavius rebuild, folder collapse/expand, and a Layer Style open/cancel round trip (af-spike/web_samples2); `manylayers` times a panel row click, a canvas auto-select click, and two Move-tool press/drag/release rounds on the 2056-layer Little-Everywhere-fixed.psd (local-test-fixtures/psd); both [SKIP] when the file is absent; `PATCHY_PERF_MANYLAYERS_PSD=<path>` points it at another file and `PATCHY_PERF_RENDER_PHASE=1` adds a timed full render (pair it with `PATCHY_RENDER_SINGLE_THREADED=1` so the sampler sees the compositor). `PATCHY_PERF_SAMPLER=1` samples 10 ms main-thread stacks per measured phase.

Duplicate Layer skips the rebuild (GitHub issue 402): `insert_duplicated_layer_rows` builds only the copies' rows through the rebuild's own `make_layer_list_item` and `make_layer_list_row_widget` (folders, clipping runs, the name filter and rows inside collapsed folders fall back to `refresh_layer_list`), and the canvas invalidates only the copies' render bounds. Tree walks that place a layer stay const (`layer_siblings_containing`): a walk through the mutable `children()` bumps every layer below the anchor and re-renders all their thumbnails per duplicate (`ui_duplicate_layer_adds_rows_without_rebuilding_the_panel` pins the revisions). `patchy_perf_tests.exe duplicate` times 300 duplicates of a text layer per bucket (`PATCHY_PERF_DUP_COUNT`; `PATCHY_PERF_DUP_FILL=1` duplicates the full-canvas Background instead, where the full recomposite is the cost).

Visibility sweeps use coalesced snapshot refreshes without blocking input.
Cache retention, background Move preparation and queue contracts are in
[interactive-previews.md](interactive-previews.md#visibility-render-waits).

## Deep documents defer full recomposites (layer-count gate)

A full recomposite can take seconds on a document with hundreds of layers even
when its pixel area is tiny (the 622-layer card template is under 1 Mpx but
composited ~5 s cold: dozens of styled layers recompute stroke EDT masks), and
below the 8 Mpx pixel threshold it would run synchronously inside
`paintEvent`. `should_defer_full_refresh_to_async` therefore also engages at
`kDeferFullRefreshMinLayers` (200, canvas_widget_render.cpp), with the same
previous-frame-async semantics as pixel-huge documents. With no same-size
previous frame to show (first paint of a fresh session),
`should_defer_first_render_to_async` kicks the async composite and paints
checkerboard plus the processing spinner instead of freezing
(`first_render_spinner_active` drives the overlay with no stored state).
A very large canvas takes the same first-paint route from
`kDeferFirstRenderMinPixels` (200 Mpx) up: composited inside the paint, a
1,048 Mpx canvas (eight scanned PDF pages as artboards at 300 ppi) then held
the window while paint built the zoomed-out display mips of its 4 GB frame.
A session's first async frame builds the mip chain of the fitted view on the
worker (`fit_logical_zoom`), because the open fits the view right after the
first paint asks for the frame
(`ui_canvas_very_large_document_first_paint_runs_in_background`). Other
pixel-huge documents keep a synchronous first paint. The gates
are compile-time constants on purpose: tests need deterministic paint
semantics, and nothing else in the suites builds 200 layers or 200 Mpx.

## Parallel strip rendering

Full renders at 4 Mpx+ use horizontal strips (`render_document_rect`).
`PATCHY_RENDER_SINGLE_THREADED=1` and tracing/profiling force sequential rendering.
A render of 0.5 to 4 Mpx (a region patch, or a small document's frame) takes strips too
while every effect mask it reads is a cached full-layer one (`patch_strips_allowed`): each
strip then reads the bytes one pass would. A visible layer whose padded bounds cross the
canvas edge has masks windowed to each render's clip, so a clip that reaches its effects
keeps the one pass; the provider records any windowed mask a strip render read after all
(`clip_windowed_masks`) and that render is redone in one pass. In `render_document_region`
a rect of that size renders on the calling thread, where it may use every worker, instead
of holding one worker of the rect pool: an arrow-key nudge of a large layer is one such
rect plus a one-pixel sliver. Counted in `patch_strip_renders()` and
`patch_strip_fallbacks()`; pinned by `region_patch_strips_match_one_pass` (8 and 16-bit,
against one pass and against a full render).
Fan-outs size their workers from `hardware_worker_threads()` (core/worker_budget.hpp): `hardware_concurrency()`, or `PATCHY_RENDER_THREADS=<n>` to emulate fewer cores (read once).
Styled groups use their complete child silhouette; the bounded style-mask LRU
shares it across strips and repaints, keyed by descendant render revisions.
Transient geometry bypasses silhouette caching. Group effect domains stay complete
outside the canvas too. An effect must give a pixel the same value in every window;
[what still does not](performance-compositor.md#strip-renders-against-the-one-pass-walk) is
a level of rounding, so keep sequential mode for cross-run checksums.

Detach copy-on-write pixel storage on the launching thread before any parallel write.
`PixelBuffer` shares its bytes across copies (undo, render snapshots, `Document` copies) until
the first non-const access. Worker strips calling non-const `row()`/`pixel()`/`data()` on a
still-shared buffer race to detach, each copying while another's replacement frees the bytes
(access violations, or heap corruption long after). Call `pixels.data()` once on the launching
thread and hand the workers spans or raw pointers; `apply_row_spans_in_parallel`
(src/ui/filter_workflows.cpp) and `heal_mask_from_surroundings` are the references;
`paint_rows_in_strips` (src/core/pixel_tools.cpp, the Gradient tool's paint) is the one for a
paint that ticks: batches of strips, a tick on the calling thread between them, the storage
taken anew for each batch. A
progress tick pumps events, and a pumped render or recovery snapshot re-shares the bytes, so
take the span after the last tick, or re-take it after each tick of a sequential walk (the
Spot Healing, Healing Brush and Patch tool walks do;
`ui_remove_object_sequential_heal_detaches_after_each_tick`).

## Vector-shape edits invalidate their effect rect, not the canvas

Shape drag-out commits, combine-extend drags, Direct/Path Select anchor drags, vector-mask path edits, and the Shape Appearance preview all call `document_changed_effect_bounds(old ∪ new layer effect rect)` instead of the bare full-canvas `document_changed()` (main_window_vector.cpp, canvas_widget_vector_tools.cpp; combine-extend refreshes with `refresh_layer_thumbnails`, not `refresh_layer_list`). Below the 8 Mpx async-defer threshold (and always on single-thread wasm: `kBackgroundWorkRunsInline`) a full invalidation is a synchronous full recomposite (several times slower on the quick stress shape steps), so new shape work must keep invalidations bounded. Vector-mask path drags go further (#447 follow-up, `patchy_perf_tests vectormaskdrag`): `changed_path_boxes` (canvas_widget_vector_tools.cpp) covers only where the boundary swept, thin boxes along each changed segment (de Casteljau pieces of the old and new controls, joined until the padded region fits a partial refresh), `update_vector_mask_raster_region` re-rasterizes just those boxes into the coverage cache, and a layer whose effects are expensive (`layer_style_preview_is_expensive`) drafts them hidden from the first move to release (`restore_path_drag_effects`; the undo snapshot is taken first): their style masks rebuild over the whole layer on every content revision. 2000x3000, canvas-filled layer, 4-point curved mask: about 13 to 22 ms per move, one full render at release. Shape-layer anchor drags still re-bake full-quality per move, and each bake gives the layer's effects new masks over the whole layer, so a point or handle drag on a shape layer drafts expensive effects the same way (`draft_path_drag_effects` in `write_shape_layer_path`; several layers in a cross-layer Direct Select drag; a cancelled drag restores them too, `cancel_pointer_gestures`; `ui_shape_point_drag_drafts_expensive_effects`). The exception to the per-move bake is a step that translates the whole path (Path Select dragging every shape on the layer, GitHub #678, `patchy_perf_tests shapedrag`): it shifts the existing raster by whole pixels with `set_vector_shape_translated` (content-keyed caches survive) and re-bakes once at release; 3500x2300 photo, stroke layer, 1300 px pentagon at fit view on a 32-thread laptop: 162 ms per move before, 14 ms after (a Move-tool drag of the same layer: about 5 to 28 ms). Gradients not aligned with the layer and patterns keep the per-move re-bake, since their paint stays anchored to the canvas. The warp preview's patches-over-base structure and its view-resolution drag frames (GitHub #200, `patchy_perf_tests warpdrag`) are in [warp.md](warp.md).

## Deferred full-refresh paint

`paintEvent` defers a FULL recomposite to the fire-and-forget async refresh and keeps drawing the previous frame whenever `CanvasWidget::should_defer_full_refresh_to_async()` says so (cache dirty + same-size previous frame + no processing operation + document at or above the compile-time `kProcessingOverlayDirtyAreaThreshold`, 8 Mpx, or the layer-count gate above; never on single-thread wasm, as above). Big documents do not flash checkerboard on add-layer/undo/blend changes. The frame kept on screen keeps its display mips (`stale_frame_stays_on_screen`; [gradients-fill-handles.md](gradients-fill-handles.md#release-without-a-pause)). Deliberately keyed on compile-time constants (not the `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` override) so overlay-path tests keep their blocking semantics; documents under both gates render synchronously in paint.

## Wasm memory diagnostics

The headless wasm stress harness (`scripts/wasm/stress-harness.html`, `memsoak.js`) and the Safari/iOS memory diagnostics moved to [wasm-memory.md](wasm-memory.md) under the same headings.

## Hardware-agnostic GPU planning and equivalence

The render graph, dirty-region coalescing, tile invalidation, and fake device
contracts are documented in [Hardware-agnostic GPU render graph](gpu-render-graph.md).
The current `compare_pixel_buffers()` policy and fake tiled renderer establish the
first structured `render_cpu()` versus `render_gpu()` comparison, but the fake path
still calls the CPU compositor for tile contents. They are useful correctness and
invalidation infrastructure, not a performance measurement, and do not imply that
a physical GPU was used. A future benchmark must compare native `render_gpu()` and
`render_cpu()` on the same scene, format, and document revision before claiming a
speedup. Zero-copy presentation, complete filter shaders, HDR/16-bit paths, and
native device-loss recovery require real platform validation after their contracts
and equivalence tests are implemented.
