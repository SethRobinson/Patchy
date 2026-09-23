# Performance: the rendering-performance rules

Read before rendering work. `AGENTS.md` owns universal revision/repaint rules; the profiling stress test (PATCHY 64 scene, its steps, baselines and reports) is in [stress-test.md](stress-test.md). The optional screen-resolution renderer is documented in [Dynamic Vector Preview](vector-preview.md). Startup timing, the `patchy_perf_tests startup` scenario and its regression check are in [startup-performance.md](startup-performance.md).

Related documents:

- [performance-strokes.md](performance-strokes.md): undo history (copy-on-write snapshots, stroke patches, the byte budget, restores and History-panel jumps), layer growth under a stroke, and per-move paint-stroke cost (patched display mips, coalesced input, round-brush and stamp-segment skips).
- [performance-compositor.md](performance-compositor.md): compositor row kernels, the mask coverage plane and the byte-identity corpus, the remaining slow paths and why GPU compositing stays unpursued, and the Gaussian line filters.

## Smart invalidation and style-mask caching

A snapshot undo/redo step diffs the two documents per layer (globally-unique revisions + visibility; structure/document-level changes fall back to full) and invalidates only the changed effect-bounds region below 8 Mpx (`history_restore_changed_region`, session_history.cpp). The expensive per-effect style masks (EDT/spread/interior blurs) are cached across renders keyed by layer CONTENT revision with domains stored relative to the layer origin, so moves and patch renders reuse them (`StyleMaskProvider` in render/layer_render_types.hpp, LRU + in-flight latch in image_document_io.cpp; `PATCHY_STYLE_MASK_CACHE_OFF=1` disables). The LRU budget is `style_mask_cache_budget_bytes()` (memory_info.hpp; `PATCHY_STYLE_MASK_BUDGET_TEST_MB` for tests). An LRU smaller than a document's masks thrashes: every render rebuilds every mask and strips serialize behind the latch (a 4000x2781 poster with about 600 MB of masks renders twice as slowly under a fixed 256 MB as when resident). The commit side upholds this: a pure-translation move bumps ONLY render revisions (`translate_moved_layer_metadata` goes through Layer's render-only mutators, and the vector/smart writer dirty marks use `metadata_without_content_bump`), so masks and the baked styled-layer cache stay warm across move commits. Document-positioned caches (layer-panel content/mask thumbnails, the paths panel's transient layer-path row) therefore key on the RENDER revision. Channel-separable adjustments composite through exact 256-entry LUTs (`build_adjustment_lut`); its exclusion list keeps channel-mixing kinds (Hue/Saturation, Threshold, the #250 kinds) and ink-space adjustments per-pixel, and a new channel-mixing kind must join it. Cached-mask renders are only used where the legacy windowed domain equals the full domain (gate in DocumentStyleMaskProvider), so full-render bytes are unchanged (pinned compositor tests hold).

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
Fan-outs size their workers from `hardware_worker_threads()` (core/worker_budget.hpp): `hardware_concurrency()`, or `PATCHY_RENDER_THREADS=<n>` to emulate fewer cores (read once).
Styled groups use their complete child silhouette; the bounded style-mask LRU
shares it across strips and repaints, keyed by descendant render revisions.
Transient geometry bypasses silhouette caching. Group effect domains stay complete
outside the canvas too. Other windowed float masks can still differ by 1-2/255 at
strip boundaries; retain sequential mode for their cross-run checksums.

Detach copy-on-write pixel storage on the launching thread before any parallel write.
`PixelBuffer` shares its bytes across copies (undo, render snapshots, `Document` copies) until
the first non-const access. Worker strips calling non-const `row()`/`pixel()`/`data()` on a
still-shared buffer race to detach, each copying while another's replacement frees the bytes
(access violations, or heap corruption long after). Call `pixels.data()` once on the launching
thread and hand the workers spans or raw pointers; `apply_row_spans_in_parallel`
(src/ui/filter_workflows.cpp) and `heal_mask_from_surroundings` are the references. A
progress tick pumps events, and a pumped render or recovery snapshot re-shares the bytes, so
take the span after the last tick, or re-take it after each tick of a sequential walk (the
Spot Healing, Healing Brush and Patch tool walks do;
`ui_remove_object_sequential_heal_detaches_after_each_tick`).

## Vector-shape edits invalidate their effect rect, not the canvas

Shape drag-out commits, combine-extend drags, Direct/Path Select anchor drags, vector-mask path edits, and the Shape Appearance preview all call `document_changed_effect_bounds(old ∪ new layer effect rect)` instead of the bare full-canvas `document_changed()` (main_window_vector.cpp, canvas_widget_vector_tools.cpp; combine-extend refreshes with `refresh_layer_thumbnails`, not `refresh_layer_list`). Below the 8 Mpx async-defer threshold (and always on single-thread wasm: `kBackgroundWorkRunsInline`) a full invalidation is a synchronous full recomposite (several times slower on the quick stress shape steps), so new shape work must keep invalidations bounded. Vector-mask path drags go further (#447 follow-up, `patchy_perf_tests vectormaskdrag`): `changed_path_boxes` (canvas_widget_vector_tools.cpp) covers only where the boundary swept, thin boxes along each changed segment (de Casteljau pieces of the old and new controls, joined until the padded region fits a partial refresh), `update_vector_mask_raster_region` re-rasterizes just those boxes into the coverage cache, and a layer whose effects are expensive (`layer_style_preview_is_expensive`) drafts them hidden from the first move to release (`restore_path_drag_effects`; the undo snapshot is taken first): their style masks rebuild over the whole layer on every content revision. 2000x3000, canvas-filled layer, 4-point curved mask: about 13 to 22 ms per move, one full render at release. Shape-layer anchor drags still re-bake full-quality per move, except a step that translates the whole path (Path Select dragging every shape on the layer, GitHub #678, `patchy_perf_tests shapedrag`): it shifts the existing raster by whole pixels with `set_vector_shape_translated` (content-keyed caches survive) and re-bakes once at release; 3500x2300 photo, stroke layer, 1300 px pentagon at fit view on a 32-thread laptop: 162 ms per move before, 14 ms after (a Move-tool drag of the same layer: about 5 to 28 ms). Gradients not aligned with the layer and patterns keep the per-move re-bake, since their paint stays anchored to the canvas. The warp preview's patches-over-base structure and its view-resolution drag frames (GitHub #200, `patchy_perf_tests warpdrag`) are in [warp.md](warp.md).

## Deferred full-refresh paint

`paintEvent` defers a FULL recomposite to the fire-and-forget async refresh and keeps drawing the previous frame whenever `CanvasWidget::should_defer_full_refresh_to_async()` says so (cache dirty + same-size previous frame + no processing operation + document at or above the compile-time `kProcessingOverlayDirtyAreaThreshold`, 8 Mpx, or the layer-count gate above; never on single-thread wasm, as above). Big documents do not flash checkerboard on add-layer/undo/blend changes. Deliberately keyed on compile-time constants (not the `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` override) so overlay-path tests keep their blocking semantics; documents under both gates render synchronously in paint.

## Wasm memory diagnostics

The headless wasm stress harness (`scripts/wasm/stress-harness.html`, `memsoak.js`) and the Safari/iOS memory diagnostics are in [wasm-memory.md](wasm-memory.md).
