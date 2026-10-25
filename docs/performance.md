# Performance: the rendering-performance rules

Read before rendering work. `AGENTS.md` owns universal revision/repaint rules; the profiling stress test (PATCHY 64 scene, its steps, baselines and reports) is in [stress-test.md](stress-test.md). The optional screen-resolution renderer is documented in [Dynamic Vector Preview](vector-preview.md). Startup timing, the `patchy_perf_tests startup` scenario and its regression check are in [startup-performance.md](startup-performance.md).

## History: snapshots, stroke patches, and the byte budget

`DocumentSession::history` owns a `SessionHistory` (`ui/session_history.{hpp,cpp}`): its stacks, state ids, selection coalescing, rotations, stroke compaction and retained-byte accounting. Its public stack views are const. MainWindow retains snapshot timing, pending-stroke identity guards and canvas/panel refresh; `enforce_session_history_budget` applies the shared budget over the session store.

Every command pushes a copy-on-write `Document` snapshot (`push_undo_snapshot` / `record_history_push`, main_window_history.cpp), so an entry retains exactly the buffers the edit detached. Paint strokes (Brush, Eraser, Clone, Healing, Smudge, Dodge/Burn/Sponge/Blur/Sharpen, Mixer, Pattern Stamp) shrink that snapshot to a `StrokePatch` (core/stroke_patch.hpp) instead of retaining the whole painted layer (GitHub issue 109): the stroke's dirty bounds inside the one buffer it wrote (layer pixels, layer mask, or saved alpha channel) with the pre-stroke pixels, plus the post-stroke pixels once the first undo captures them. The canvas reports strokes through `set_paint_stroke_history_callback` (Begin right after the before-edit push; Finish on release or focus loss with the union of every dirty rect the stroke reported); `MainWindow::handle_paint_stroke_history_event` converts the top entry only when nothing else changed: same session and state ids, no intervening push or undo, and `capture_stroke_patch`'s storage walk finds the target as the ONLY detached buffer with unchanged geometry. Anything else (a mask that grew during the stroke, Stroke Path's scripted strokes, Quick Mask and Smart Filter mask strokes) keeps the whole-document snapshot, so correctness never depends on the patch. The transient cost during a stroke is unchanged (the live layer detaches from the snapshot); only the retained cost shrinks. `PATCHY_STROKE_PATCH_HISTORY=0` disables the shrink for diagnosis; `MainWindowTestAccess::set_stroke_patch_history_enabled` is the test seam (tests/ui/history_stroke_patch_tests.cpp compares both paths pixel for pixel).

A `HistoryState` is therefore a document or a patch (`is_stroke_patch`). `rotate_history_state` swaps documents for snapshot entries and writes a patch into the live document in place (through the revision-bumping accessors, so thumbnails and caches refresh). A patch is relative to the next newer entry (or the live document), which is why eviction from the stack front is always safe and why `materialize_history_state` (New Document From This State on a patch row) starts from the nearest newer snapshot and writes the intervening patches back.

History is byte-budgeted: every push and every patch shrink runs `MainWindow::enforce_history_memory_budget`, a COW-aware pointer walk (`core/document_memory.hpp`) plus the patches' own bytes, evicting the oldest undo states across all sessions once their marginal bytes exceed `history_memory_budget_bytes()` (`ui/memory_info.hpp`; `PATCHY_HISTORY_BUDGET_TEST_MB` is the test override), keeping at least `kMinUndoStatesUnderPressure` states per session under the 40-state cap.

Restores go through `CanvasWidget::set_document_for_history_restore`, which keeps the previous frame + render diagnostics when the restored document has the same size (plain `set_document` dumps both). Together with the paint deferral below, history steps on big documents swap frames instead of flashing checkerboard. Rotations accumulate one `HistoryRefreshRegion` (full, or the union of changed areas: a snapshot step's per-layer revision diff below, a patch step's document rect plus the layer's effect bounds when they moved), and History-panel jumps (`jump_to_history_state`) rotate any number of states and run the canvas/UI refresh tail once, so a 20-state jump costs one repaint, not 20.

## Smart invalidation and style-mask caching

A snapshot undo/redo step diffs the two documents per layer (globally-unique revisions + visibility; structure/document-level changes fall back to full) and invalidates only the changed effect-bounds region below 8 Mpx (`history_restore_changed_region`, session_history.cpp). The expensive per-effect style masks (EDT/spread/interior blurs) are cached across renders keyed by layer CONTENT revision with domains stored relative to the layer origin, so moves and patch renders reuse them (`StyleMaskProvider` in render/layer_compositor.hpp, LRU + in-flight latch in image_document_io.cpp; `PATCHY_STYLE_MASK_CACHE_OFF=1` disables). The LRU budget is `style_mask_cache_budget_bytes()` (memory_info.hpp; `PATCHY_STYLE_MASK_BUDGET_TEST_MB` for tests). An LRU smaller than a document's masks thrashes: every render rebuilds every mask and strips serialize behind the latch (a 4000x2781 poster with about 600 MB of masks renders twice as slowly under a fixed 256 MB as when resident). The commit side upholds this: a pure-translation move bumps ONLY render revisions (`translate_moved_layer_metadata` goes through Layer's render-only mutators, and the vector/smart writer dirty marks use `metadata_without_content_bump`), so masks and the baked styled-layer cache stay warm across move commits. Document-positioned caches (layer-panel content/mask thumbnails, the paths panel's transient layer-path row) therefore key on the RENDER revision. Channel-separable adjustments (Levels/Curves/Color Balance) composite through exact 256-entry LUTs (`build_adjustment_lut`); Hue/Saturation stays per-pixel. Cached-mask renders are only used where the legacy windowed domain equals the full domain (gate in DocumentStyleMaskProvider), so full-render bytes are unchanged (pinned compositor tests hold).

## Reads must not bump layer revisions

Layer's mutable accessors bump render/content revisions on ACCESS, so read-only code must go through const layers (`std::as_const`), or it silently invalidates every revision-keyed cache (a lookup walk like `Document::find_layer` would bump thousands of layers per frame; it walks const). Hunt regressions with `PATCHY_REV_TRACE=1` (stderr REVBUMP lines per accessor+layer).

## Layer growth under a stroke

Every new pixel layer is canvas-sized except a pasted one, which is its pixels' size; a dab outside a layer grows it (`expand_layer_to_include_rect`). Growth re-copies the layer, so per dab it is quadratic over a stroke that leaves a small layer. Interactive strokes open a `LayerGrowthReserve` (core/pixel_tools.hpp) in `arm_paint_stroke_history` for a layer smaller than the canvas: growth then takes headroom (half the old size, at least 256 px, clamped to the canvas), and the release and `finish_paint_stroke_history` close it, trimming the layer to exactly the bounds and bytes per-dab growth gives, before the stroke is reported or recorded. Same-format growth copies rows with `memcpy`. Pinned by `tool_small_layer_grows_cheaply_under_a_long_stroke` (core) and `ui_brush_stroke_off_a_small_layer_ends_with_exact_bounds`.

## Nothing O(layer pixels) may run per repaint

`opaque_pixel_local_rect` (the Move tool's passive-box bounds) reaches paint through `move_transform_controls_rect`, so it must never rescan the alpha channel per frame (on a 70 Mpx layer that costs about half a second per zoom/pan step). The shared `visible_alpha_local_bounds(const Layer&)` helper in `layer_render_utils.cpp` serves the Move tool and alpha-aligned layer effects: a row scan with early exits and a bounded LRU keyed by the app-globally-unique PIXEL revision, with a per-key in-flight latch so parallel render strips do not duplicate a cold scan. Style edits do not change the pixel revision; mutable pixel access and `set_pixels` do. `PATCHY_ZOOM_TRACE=1` prints paint/zoom/view-changed phase timings over 2 ms to stderr, and `patchy_perf_tests.exe zoom` (env `PATCHY_PERF_ONSCREEN=1`, `PATCHY_PERF_ZOOM_BG=1` selects the tent's 70 Mpx BG layer, `PATCHY_PERF_ZOOM_SELECTION=1` adds marching ants) measures per-step latency on the PSBtest tent (ceiling: about 15 ms per step maximized).

## Paint strokes: patched display mips and coalesced input (GitHub issue 165)

Fast strokes on big images must not paint straight chords: no per-move work may scale with the whole image or view.

- **Display mips are patchable.** Every zoomed-out chain (composite, move/transform/warp bases, curves clipping, aligned patch draws) halves with `halved_display_mip` (canvas_widget_shared): each output pixel averages its own 2x2 block, alpha-weighted for straight-alpha formats, odd edges average what they have. `patch_render_cache_patches` recomputes only the blocks under the patches (`patch_display_mip_cache`), bit-identical to a rebuild; `RenderCacheDiagnostics::display_mip_rebuilds` counts the full builds and a stroke adds none. `PATCHY_DISPLAY_MIP_PATCH=0` restores the rebuild for diagnosis. `document_changed_impl` repaints the dirty widget rects grown by the smooth scale's reach (`widget_repaint_rect_for_document_rect`) at every zoom, never the whole widget.
- **Every folded mouse position reaches the stroke.** `mouseMoveEvent` feeds `coalesced_stroke_positions` (src/ui/coalesced_pointer_input) through `advance_paint_stroke` before the event's own point and repaints once. Windows recovers the positions from `GetMouseMovePointsEx` (the event's native point is its window position times the device pixel ratio; history entries map back as deltas over that ratio). X11 stops dropping queued motion because `UncompressedPointerInputScope` holds `Qt::AA_CompressHighFrequencyEvents` off exactly while `painting_` is set (`sync_stroke_input_compression` at every `painting_` change). The same scope turns AppKit's mouse coalescing off on macOS (`set_mac_mouse_coalescing_enabled`, which also covers pen drags there). Pen input needs nothing else: Qt already delivers the Windows pen history and never compresses X11 tablet motion. `CanvasWidget::set_coalesced_mouse_positions_provider` is the test seam (`ui_brush_stroke_paints_every_coalesced_mouse_sample`).
- **Per-pixel stroke state is a tiled grid, never a hash map.** The opacity cap, accumulated alpha, alpha caps and the Wet Edges footprint are read for every pixel of every dab; `StrokePixelMap` (src/ui/stroke_pixel_map.hpp: 64x64 dense tiles plus a one-tile cache); a hash map cost a 300 px brush about 0.14 ms per document pixel of travel. Same semantics, so stroke bytes are unchanged.
- `patchy_perf_tests.exe liquifydrag`: Liquify drag frames and Apply on large layers, with ceilings ([liquify.md](liquify.md)).
- `patchy_perf_tests.exe brushstroke` times one move and its paint on a white 4872x6836 image at fit, 100%, 150%, 200% and 800% zoom, plus a full-view repaint per zoom (`PATCHY_PERF_SAMPLER=1` attributes each case).
- **Big round brushes test only new ground (GitHub issue 135).** Round dabs and hard round segments visit only each row's span of their footprint, and a hard round segment skips the disk the stroke's previous segment painted (`EditOptions::stroke_covered_disk`, set by `draw_brush_segment` while color, opacity and gate hold and no feathered selection applies). `patchy_perf_tests.exe a5brush` times 50/300/731 px strokes on A5 at 300 ppi; `idle` reports a fresh canvas's CPU, paints and timer events with nobody touching it.

## Interactive drag previews (Move and Free Transform)

The move-drag proxy/base machinery, the display-resolution preview-scaled document, cache retention across drags, and the free-transform preview live in [interactive-previews.md](interactive-previews.md); read it before touching those paths. The universal rule stays here: a drag handler must never synchronously recomposite the document: previews render from bounded caches and full invalidations belong to the post-release async refresh. Commit-path bytes are untouched by the preview machinery by design (corpus digests and the transform-commit suites pin it), and the stress move matrix (steps 29-33) plus step `61_transform_drag_proxy` pin the latch counters ([stress-test.md](stress-test.md)).

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

The Layers panel's rules (three-pass rebuild order, no rebuild on canvas-driven selection of existing rows, viewport-bounded row masks) and their measured numbers live in [layer-panel.md](layer-panel.md). Profiling: `refresh_layer_list`, `set_active_layer_from_selection`, `restyle_layer_rows`, `update_layer_target_styles`, and `refresh_options_bar` log under `PATCHY_UI_PROFILE=1`; `PATCHY_ZOOM_TRACE=1` adds the Move-tool press phases (`move_press.*`, `handle_transform_start` for a passive-handle grab). `patchy_perf_tests.exe layerpanel` times the Quintavius rebuild, folder collapse/expand, and a Layer Style open/cancel round trip (af-spike/web_samples2); `manylayers` times a panel row click, a canvas auto-select click, and two Move-tool press/drag/release rounds on the 2056-layer Little-Everywhere-fixed.psd (local-test-fixtures/psd); both [SKIP] when the file is absent; `PATCHY_PERF_MANYLAYERS_PSD=<path>` points it at another file and `PATCHY_PERF_RENDER_PHASE=1` adds a timed full render (pair it with `PATCHY_RENDER_SINGLE_THREADED=1` so the sampler sees the compositor). `PATCHY_PERF_SAMPLER=1` samples 10 ms main-thread stacks per measured phase.

Duplicate Layer skips the rebuild (GitHub issue 402): `insert_duplicated_layer_rows` builds only the copies' rows through the rebuild's own `make_layer_list_item` and `make_layer_list_row_widget` (folders, clipping runs, the name filter and rows inside collapsed folders fall back to `refresh_layer_list`), and the canvas invalidates only the copies' render bounds. Tree walks that place a layer stay const: `layer_siblings_containing` once walked the mutable `children()`, bumping every layer below the anchor and re-rendering all their thumbnails per duplicate (`ui_duplicate_layer_adds_rows_without_rebuilding_the_panel` pins the revisions). `patchy_perf_tests.exe duplicate` times 300 duplicates of a text layer per bucket (`PATCHY_PERF_DUP_COUNT`; `PATCHY_PERF_DUP_FILL=1` duplicates the full-canvas Background instead, where the full recomposite is the cost).

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
(`first_render_spinner_active` drives the overlay with no stored state);
pixel-huge documents keep a synchronous first paint. Both gates
are compile-time constants on purpose: tests need deterministic paint
semantics, and nothing in the suites builds 200 layers.

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
(src/ui/filter_workflows.cpp) and `heal_mask_from_surroundings` are the references.

## Compositor row kernels, mask plane, and the byte-identity corpus

The per-pixel general loop in `composite_pixel_layer` (render/layer_compositor.hpp) is the compositor's slow path: a layer falls off the integer scanline fast path the moment it has a mask, a non-Normal blend mode, or any advanced feature, where throughput falls from ~860 Mpx/s to ~30 Mpx/s. These fast paths cut it without changing a single output byte:

- **Mask coverage plane.** `build_mask_coverage_plane` folds the raster and vector mask factors over the draw rect in one row-walked pass, replacing the per-pixel `layer_mask_alpha_for_render` call chain (which paid two bounds-validated `PixelBuffer::pixel()` calls per pixel). The plane's expressions keep `layer_mask_alpha_at`'s exact float operation order, so values are bit-identical; the general loop reads the plane for every masked layer on every target.
- **`composite_blended_row` row kernel** (QImageCompositeTarget, ui/qimage_composite_target.hpp). When no per-pixel gate beyond coverage remains (no blend-if, satin, folded overlays, knockout, special fill, or backdrop pre-blend; Dissolve excluded), the base pass runs as a row walk whose arithmetic is a verbatim replica of `composite_color` + `composite_blended_rgb` (mode dispatch through the real `blend_rgb`). NEVER reuse `composite_source_row`'s integer math for such kernels: the integer and float paths round differently by design, and pinned bytes hold only because the replica keeps the exact float expression trees.
- **MSVC link-time optimization.** CMakeLists.txt owns Release flags (AGENTS.md); `/fp:precise` keeps corpus bytes unchanged. LTCG relinks dominate rebuild time. Compile time: each compositor target is instantiated in one TU (`PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS`, code-organization.md). Without LTCG (opt-dev) the `layer_compositor.cpp` helpers are real calls; the default `patchy_perf_tests` run checks that moving one out of the header cost nothing.
- **Float row kernels on the group and flatten targets.** `IsolatedClipGroupTarget` (layer_compositor.hpp) and `Rgb8PixelBufferTarget` (render/composite_targets.hpp) expose `composite_blended_row`, so isolated/masked/non-passthrough groups, clip runs, and PSD flatten leave the general per-pixel loop for gate-free layers. Each kernel is a float-exact transcription of that target's `record_clip_coverage` + `composite_color` chain (the tier dispatch never calls `record_clip_coverage`, so the isolated kernel replicates it internally, including the frozen clip-matte gate and max-alpha update; the blend reads the CLAMPED destination alpha while the plane update reads the stored value). Neither target gets an integer `composite_source_row`: byte identity is against the float general loop, and tier-1-eligible layers fall through to the float kernel automatically (tier 1's conditions are a strict subset of tier 2's).

Guarding all of it, the **byte-identity corpus**: `composite_corpus_flatten_digests_are_stable` (tests/core/composite_corpus_tests.cpp) and `composite_corpus_render_digests_are_stable` plus the override-equivalence test (tests/ui/composite_render_tests.cpp) flatten/render every committed `test-fixtures/psd` document plus anything dropped into `local-test-fixtures/composite-corpus/`, single-threaded, and compare FNV-1a digests against the tracked baselines in `test-fixtures/psd/*-digests.txt` (plus a local overlay). Any compositor optimization must keep these green; after a DELIBERATE rendering change, delete the tracked files, rerun, and commit the diff. Isolated-group bounds are override-aware (`layer_render_bounds_for_render`): move/transform preview overrides do not force full-clip isolation buffers, and the styled pass-through silhouette includes override-moved children.

Remaining slow paths (future tiers): `merge_layer_into` and the channel-restricted/`GroupMaskedTarget` wrappers (deliberately per-pixel: fusing their factors into a kernel row would change float association order), the three float divides per pixel (skipped only at output alpha 1.0), exact-value SIMD for the row kernels, a thread pool replacing the per-strip `std::async` spawns, and display-resolution interactive compositing. GPU compositing stays deliberately unpursued: byte identity with the CPU reference is effectively unachievable on GPU float paths, and a preview-only GPU tier comes only after the CPU tiers are exhausted.

## Vector-shape edits invalidate their effect rect, not the canvas

Shape drag-out commits, combine-extend drags, Direct/Path Select anchor drags, vector-mask path edits, and the Shape Appearance preview all call `document_changed_effect_bounds(old ∪ new layer effect rect)` instead of the bare full-canvas `document_changed()` (main_window_vector.cpp, canvas_widget_vector_tools.cpp; combine-extend refreshes with `refresh_layer_thumbnails`, not `refresh_layer_list`). Below the 8 Mpx async-defer threshold (and ALWAYS on wasm) a full invalidation is a synchronous full recomposite (several times slower on the quick stress shape steps), so new shape work must keep invalidations bounded. The anchor-drag re-bake still runs full-quality per move; a coarse-preview latch remains a future tier. The warp preview's patches-over-base structure and its view-resolution drag frames (GitHub #200, `patchy_perf_tests warpdrag`) are in [warp.md](warp.md).

## Deferred full-refresh paint

`paintEvent` defers a FULL recomposite to the fire-and-forget async refresh and keeps drawing the previous frame whenever `CanvasWidget::should_defer_full_refresh_to_async()` says so (cache dirty + same-size previous frame + no processing operation + document at or above the compile-time `kProcessingOverlayDirtyAreaThreshold`, 8 Mpx). Big documents do not flash checkerboard on add-layer/undo/blend changes. Deliberately keyed on the compile-time constant (not the `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` override) so overlay-path tests keep their blocking semantics; sub-threshold documents render synchronously in paint.

## Wasm memory diagnostics

The headless wasm stress harness (`scripts/wasm/stress-harness.html`, `memsoak.js`) and the Safari/iOS memory diagnostics are in [wasm-memory.md](wasm-memory.md).
