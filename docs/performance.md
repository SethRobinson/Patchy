# Performance: the stress test and the rendering-performance rules

Read before rendering or stress-harness work. `AGENTS.md` owns universal revision/repaint rules. The optional screen-resolution renderer is documented in [Dynamic Vector Preview](vector-preview.md).

## Profiling stress test (PATCHY 64 scene)

A built-in, deterministic performance harness: it closes all documents, builds the "PATCHY 64" retro desk scene, then widens the canvas 50% and builds the "C2 ARCADE" corner next to it (layers organized into nested folders as it goes), timing 60 stable-id steps across paint/text/styles/filters/adjustments/move/interact/history/io/vector/smart, then writes reports. Entry points: Preferences > Application > Development (warning dialog, results dialog, scene left open) and `patchy.exe --stress-test[=quick|small|standard|huge] [--stress-report-dir <dir>] [--stress-depth 16|32]`, which skips the splash/update check AND the single-instance mechanism, runs, writes reports, and exits 0 (success) / 1 (failure or unsettled step) / 2 (bad preset token). `--stress-depth` converts the fresh document to 16 or 32 bits (32-bit runs skip filters that depth does not offer). Default preset is **quick (1024 px, about a minute)**; run **standard (4096 px, several minutes)** for full-scale measurements: the move outline-preview thresholds and the async full-refresh path only engage at standard and above. Implementation: `src/ui/main_window_stress_test.cpp` (`StressTestRunner`, a MainWindow friend), shared types in `src/ui/stress_test.hpp`.

- Reports land in `%APPDATA%\Patchy\stress-reports\` as `stress-<timestamp>.{json,txt}` plus stable `stress-latest.{json,txt}` copies (agents: read `stress-latest.json`), alongside `stress-scene.png` and `stress-scene.psd`. Meaningful numbers need a REAL screen; the offscreen `smoke` preset (hidden, 512 px) exists only for the `ui_stress_test_smoke_preset_writes_report` suite test.
- The rating is 1000 x geometric mean of per-step baseline/actual over `kStepBaselines` in main_window_stress_test.cpp (>1000 = faster than baseline). To recalibrate: on the reference machine run the preset named in `kBaselineTag` (currently quick), paste the TXT report's suggested table over `kStepBaselines`, bump `kBaselineTag`. Step ids are diffed across runs and key the baselines: never rename one.
- Move matrix (steps 29-33) pins the move-drag preview fallbacks: `RenderCacheDiagnostics` reports `move_proxy_previews` (drag latched the translated-snapshot proxy) and `move_outline_previews` (last-resort dashed outline), thresholds `kMoveOutlineDirtyAreaThreshold` / `kStyledMoveOutlineDirtyAreaThreshold` in canvas_widget_move.cpp. At standard size, steps 29/32 must report both counters 0 (live path) and steps 30/31 must report `move_proxy_previews == 1` with `move_outline_previews == 0`; per-step diagnostics deltas in the JSON show dirty-rect behavior (partial patches vs full refreshes). The scene's prop layers are cropped to their opaque pixels after painting (`tighten_layer_to_opaque`) because the move machinery sizes its work from RAW layer bounds; `add_layer()` buffers are full-canvas, which would push every drag over the proxy threshold.
- Text sizes in the scenario are computed in PIXELS via the shared `text_pixels_to_points` (main_window_shared); the size spin takes points, converted at the document's print PPI (default 300, not 96).
- Scene steps that would otherwise commit dozens of tool strokes (key grid, scanlines, pixel art) write cells directly into the layer buffer under ONE undo snapshot and invalidate per cell: `push_undo_snapshot` copies the whole Document (~0.5 GB at 4096 px) per commit. The runner trims `undo_stack` between steps for the same reason; the Huge preset still peaks at several GB.
- The composite checksum in the report is FNV-1a over the final flatten, comparable on one machine only (text AA varies across machines, and the strip-parallel renderer below makes it thread-count dependent too). A checksum change during optimization work means rendering changed, not just speed.
- Adding a step: call `step("NN_id", "label", "category", body)` (or `fps_step` for drag phases) inside a phase, keep ids stable, add a `kStepBaselines` entry, and scale geometry through the `at()`/`motion_steps()` helpers so smoke stays fast. All user-facing strings via tr() and the catalogs ([localization.md](localization.md)); report content stays English (machine-readable).
- Layer folders are created DURING construction: step 10 starts "Desk props", 12 "Boot screen", 45 "C2 Arcade" + its nested "Arcade cabinet", 47 "Arcade signage"; later steps insert into them by anchoring on a child via `activate_layer_for_insert`. Text layers always commit at the top of the ROOT stack (not above the active layer), so scripted text belonging to a folder is tucked in right after creation via `move_layer_into_folder`. Whole-scene overlays (vignette/grain) call `activate_top_layer` first or a stale row anchor drops them INSIDE whatever folder was last active.
- Scene 2 (steps 44-59): step 44 widens the canvas 50% (`resize_canvas_and_layers`, anchor Left); the arcade corner covers shape-mode drags with gradient fills/strokes, booleans, pen/polygon/custom-shape/arrow-line work, an appearance re-bake, a selection-to-work-path roundtrip (dismissed after), filters behind halftone CRT glass, a smart object + Gaussian smart-filter commit (rasterized at the end so later geometry ops stay legal), a flag-preset warp bake, and a full-scene PSD save+reload (the disk artifact is the complete two-scene document). Execution order is 01-34, 61, 35-41, 44-51, 57, 58, 52, 53, 59, 54, 55, 56, then 42/43 last; ids stay append-only and order is not part of the id contract (the retired `60_layer_folders` id is not reused). Scene-2 geometry uses `at()` fractions above 1.0.
- Gotchas the scene-2 steps encode: the shape tools' persisted Shape|Path|Pixels mode means raster `draw_shape` must force `VectorToolMode::Pixels` for its drag; `activate_tool` syncs the options-bar vector fill/stroke mirrors FROM the active shape layer, so a scripted appearance must be set after the tool switch (and the user's mirrors are saved/restored around the vector steps); `add_layer` anchors on the layer-list ROW selection before the active layer, and an unblocked `clearSelection()` fires the selection handler which re-asserts the old active layer (see `activate_layer_for_insert`); `create_layer_folder_from_layers` keeps the ids' order as the folder's top-to-bottom child order, so scripted membership must be sorted by current stacking position first (see `group_layers_into_folder`); text layer names truncate at 24 chars, so long-text layers are targeted by captured id, never by name; a shape-tool release on the press's own document pixel is a click that opens the modal Create Shape dialog, and snapping (8 SCREEN px per axis) can collapse both ends of a small on-screen drag onto one target, so shape-tool drags use `shape_drag`, which zooms in first.
- No scripted step may open a modal dialog: nothing answers it (a hang). The guard timer in `run()` dismisses any non-progress modal, warns with the step id, and fails the run.
- `CanvasWidget::render_settled()` (public) reports "no recomposite pending or in flight"; the runner's settle loop = repaint() + pump until settled. Reuse it for any future "wait until the canvas is truly current" need instead of sleeping.
- A progress dialog (`stressTestProgressDialog`, WindowModal) tracks the 60 steps; Cancel stops at the next step boundary, writes the partial report (`"cancelled": true`, exit code 1 from CLI), and leaves the partial scene document open and MODIFIED, unlike a completed run whose scene is marked saved.

## History: snapshots, stroke patches, and the byte budget

`DocumentSession::history` owns a `SessionHistory` (`ui/session_history.{hpp,cpp}`): its stacks, state ids, selection coalescing, rotations, stroke compaction and retained-byte accounting. Its public stack views are const. MainWindow retains snapshot timing, pending-stroke identity guards and canvas/panel refresh; `enforce_session_history_budget` applies the shared budget over the session store.

Every command pushes a copy-on-write `Document` snapshot (`push_undo_snapshot` / `record_history_push`, main_window_history.cpp), so an entry retains exactly the buffers the edit detached. Paint strokes (Brush, Eraser, Clone, Healing, Smudge, Dodge/Burn/Sponge/Blur/Sharpen, Mixer, Pattern Stamp) used to retain the whole painted layer (16 MB per stroke on a 2000x2000 RGBA document, GitHub issue 109). They now shrink that snapshot to a `StrokePatch` (core/stroke_patch.hpp): the stroke's dirty bounds inside the one buffer it wrote (layer pixels, layer mask, or saved alpha channel) with the pre-stroke pixels, plus the post-stroke pixels once the first undo captures them. The canvas reports strokes through `set_paint_stroke_history_callback` (Begin right after the before-edit push; Finish on release or focus loss with the union of every dirty rect the stroke reported); `MainWindow::handle_paint_stroke_history_event` converts the top entry only when nothing else changed: same session and state ids, no intervening push or undo, and `capture_stroke_patch`'s storage walk finds the target as the ONLY detached buffer with unchanged geometry. Anything else (a mask that grew during the stroke, Stroke Path's scripted strokes, Quick Mask and Smart Filter mask strokes) keeps the whole-document snapshot, so correctness never depends on the patch. The transient cost during a stroke is unchanged (the live layer detaches from the snapshot); only the retained cost shrinks. `PATCHY_STROKE_PATCH_HISTORY=0` disables the shrink for diagnosis; `MainWindowTestAccess::set_stroke_patch_history_enabled` is the test seam (tests/ui/history_stroke_patch_tests.cpp compares both paths pixel for pixel).

A `HistoryState` is therefore a document or a patch (`is_stroke_patch`). `rotate_history_state` swaps documents for snapshot entries and writes a patch into the live document in place (through the revision-bumping accessors, so thumbnails and caches refresh). A patch is relative to the next newer entry (or the live document), which is why eviction from the stack front is always safe and why `materialize_history_state` (New Document From This State on a patch row) starts from the nearest newer snapshot and writes the intervening patches back.

History is byte-budgeted: every push and every patch shrink runs `MainWindow::enforce_history_memory_budget`, a COW-aware pointer walk (`core/document_memory.hpp`) plus the patches' own bytes, evicting the oldest undo states across all sessions once their marginal bytes exceed `history_memory_budget_bytes()` (`ui/memory_info.hpp`: RAM/4 clamped to 512 MB-4 GB on desktop, 256 MB on wasm, `PATCHY_HISTORY_BUDGET_TEST_MB` test override), keeping at least `kMinUndoStatesUnderPressure` (3) states per session under the unchanged 40-state cap.

Restores go through `CanvasWidget::set_document_for_history_restore`, which keeps the previous frame + render diagnostics when the restored document has the same size (plain `set_document` dumps both). Together with the paint deferral below, history steps on big documents swap frames instead of flashing checkerboard. Rotations accumulate one `HistoryRefreshRegion` (full, or the union of changed areas: a snapshot step's per-layer revision diff below, a patch step's document rect plus the layer's effect bounds when they moved), and History-panel jumps (`jump_to_history_state`) rotate any number of states and run the canvas/UI refresh tail once, so a 20-state jump costs one repaint, not 20.

## Smart invalidation and style-mask caching

A snapshot undo/redo step diffs the two documents per layer (globally-unique revisions + visibility; structure/document-level changes fall back to full) and invalidates only the changed effect-bounds region below 8 Mpx (`history_restore_changed_region`, session_history.cpp). The expensive per-effect style masks (EDT/spread/interior blurs) are cached across renders keyed by layer CONTENT revision with domains stored relative to the layer origin, so moves and patch renders reuse them (`StyleMaskProvider` in render/layer_compositor.hpp, LRU + in-flight latch in image_document_io.cpp; `PATCHY_STYLE_MASK_CACHE_OFF=1` disables). The LRU budget is `style_mask_cache_budget_bytes()` (memory_info.hpp): RAM/8 clamped to 256 MB-2 GB on desktop, 48 MB on wasm, `PATCHY_STYLE_MASK_BUDGET_TEST_MB` for tests. An LRU smaller than a document's masks thrashes: every render rebuilds every mask and strips serialize behind the latch (the 4000x2781 pinball poster's ~600 MB of masks took 19 s per full render under a fixed 256 MB, 9 s resident). The commit side upholds this: a pure-translation move bumps ONLY render revisions (`translate_moved_layer_metadata` goes through Layer's render-only mutators, and the vector/smart writer dirty marks use `metadata_without_content_bump`), so masks and the baked styled-layer cache stay warm across move commits. Document-positioned caches (layer-panel content/mask thumbnails, the paths panel's transient layer-path row) therefore key on the RENDER revision. Channel-separable adjustments (Levels/Curves/Color Balance) composite through exact 256-entry LUTs (`build_adjustment_lut`); Hue/Saturation stays per-pixel. Cached-mask renders are only used where the legacy windowed domain equals the full domain (gate in DocumentStyleMaskProvider), so full-render bytes are unchanged (pinned compositor tests hold).

## Reads must not bump layer revisions

Layer's mutable accessors bump render/content revisions on ACCESS, so read-only code must go through const layers (`std::as_const`), or it silently invalidates every revision-keyed cache (a lookup walk like `Document::find_layer` would bump thousands of layers per frame; it walks const). Hunt regressions with `PATCHY_REV_TRACE=1` (stderr REVBUMP lines per accessor+layer).

## Nothing O(layer pixels) may run per repaint

`opaque_pixel_local_rect` (the Move tool's passive-box bounds) reaches paint through `move_transform_controls_rect`, so it must never rescan the alpha channel per frame (on a 70 Mpx layer that costs about half a second per zoom/pan step). The shared `visible_alpha_local_bounds(const Layer&)` helper in `layer_render_utils.cpp` serves the Move tool and alpha-aligned layer effects: a row scan with early exits and a bounded LRU keyed by the app-globally-unique PIXEL revision, with a per-key in-flight latch so parallel render strips do not duplicate a cold scan. Style edits do not change the pixel revision; mutable pixel access and `set_pixels` do. `PATCHY_ZOOM_TRACE=1` prints paint/zoom/view-changed phase timings over 2 ms to stderr, and `patchy_perf_tests.exe zoom` (env `PATCHY_PERF_ONSCREEN=1`, `PATCHY_PERF_ZOOM_BG=1` selects the tent's 70 Mpx BG layer, `PATCHY_PERF_ZOOM_SELECTION=1` adds marching ants) measures per-step latency on the PSBtest tent (~10-15 ms per step maximized; ~255-300 ms before the cache).

## Interactive drag previews (Move and Free Transform)

The move-drag proxy/base machinery, the display-resolution preview-scaled document, cache retention across drags, and the free-transform preview live in [interactive-previews.md](interactive-previews.md); read it before touching those paths. The universal rule stays here: a drag handler must never synchronously recomposite the document: previews render from bounded caches and full invalidations belong to the post-release async refresh. Commit-path bytes are untouched by the preview machinery by design (corpus digests and the transform-commit suites pin it), and the stress move matrix (steps 29-33) plus step `61_transform_drag_proxy` pin the latch counters.

## Text edit sessions

What keeps an inline text session cheap (none of it changes committed pixels):

- **The edited layer is hidden only when its preview replaces it**, in the same
  `document_changed_effect_bounds` call (`hide_text_editor_source_layer`).
- **The expensive-style debounce keeps the last preview** through
  `kExpensiveTextEditorPreviewDelayMs` rather than removing the preview layer (a per-keystroke
  insert/remove of a styled layer plus two recomposites).
- **Commit repaints are bounded** to the committed layer's effect bounds (no bare
  `document_changed()` in `commit_text_editor`).

One stress run is not a measurement: text steps swing 50%+ between runs and stale
`kStepBaselines` can make a faster build look slower. Alternate builds, compare means.

## Layer-panel rebuilds and selection

The Layers panel's rules (three-pass rebuild order, no rebuild on canvas-driven selection of existing rows, viewport-bounded row masks) and their measured numbers live in [layer-panel.md](layer-panel.md). Profiling: `refresh_layer_list`, `set_active_layer_from_selection`, `restyle_layer_rows`, `update_layer_target_styles`, and `refresh_options_bar` log under `PATCHY_UI_PROFILE=1`, and `PATCHY_ZOOM_TRACE=1` adds the Move-tool press phases (`move_press.*`, including `handle_transform_start` for a passive-handle grab). `patchy_perf_tests.exe layerpanel` times the Quintavius rebuild, folder collapse/expand, and a Layer Style open/cancel round trip (af-spike/web_samples2; [SKIP] when absent); `manylayers` times a panel row click, a canvas auto-select click, and two Move-tool press/drag/release rounds on the 2056-layer Little-Everywhere-fixed.psd (local-test-fixtures/psd; [SKIP] when absent); `PATCHY_PERF_MANYLAYERS_PSD=<path>` points it at another file and `PATCHY_PERF_RENDER_PHASE=1` adds a timed full render (pair it with `PATCHY_RENDER_SINGLE_THREADED=1` so the sampler sees the compositor). `PATCHY_PERF_SAMPLER=1` samples 10 ms main-thread stacks per measured phase.

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

## Compositor row kernels, mask plane, and the byte-identity corpus

The per-pixel general loop in `composite_pixel_layer` (render/layer_compositor.hpp) is the compositor's slow path: a layer falls off the integer scanline fast path the moment it has a mask, a non-Normal blend mode, or any advanced feature, where throughput falls from ~860 Mpx/s to ~30 Mpx/s. These fast paths cut it without changing a single output byte:

- **Mask coverage plane.** `build_mask_coverage_plane` folds the raster and vector mask factors over the draw rect in one row-walked pass, replacing the per-pixel `layer_mask_alpha_for_render` call chain (which paid two bounds-validated `PixelBuffer::pixel()` calls per pixel). The plane's expressions keep `layer_mask_alpha_at`'s exact float operation order, so values are bit-identical; the general loop reads the plane for every masked layer on every target.
- **`composite_blended_row` row kernel** (QImageCompositeTarget, image_document_io.cpp). When no per-pixel gate beyond coverage remains (no blend-if, satin, folded overlays, knockout, special fill, or backdrop pre-blend; Dissolve excluded), the base pass runs as a row walk whose arithmetic is a verbatim replica of `composite_color` + `composite_blended_rgb` (mode dispatch through the real `blend_rgb`). NEVER reuse `composite_source_row`'s integer math for such kernels: the integer and float paths round differently by design, and pinned bytes hold only because the replica keeps the exact float expression trees.
- **MSVC link-time optimization.** CMakeLists.txt owns Release flags (AGENTS.md); `/fp:precise` keeps corpus bytes unchanged. LTCG relinks dominate rebuild time.
- **Float row kernels on the group and flatten targets.** `IsolatedClipGroupTarget` (layer_compositor.hpp) and `Rgb8PixelBufferTarget` (compositor.cpp) expose `composite_blended_row`, so isolated/masked/non-passthrough groups, clip runs, and PSD flatten leave the general per-pixel loop for gate-free layers. Each kernel is a float-exact transcription of that target's `record_clip_coverage` + `composite_color` chain (the tier dispatch never calls `record_clip_coverage`, so the isolated kernel replicates it internally, including the frozen clip-matte gate and max-alpha update; the blend reads the CLAMPED destination alpha while the plane update reads the stored value). Neither target gets an integer `composite_source_row`: byte identity is against the float general loop, and tier-1-eligible layers fall through to the float kernel automatically (tier 1's conditions are a strict subset of tier 2's).

Guarding all of it, the **byte-identity corpus**: `composite_corpus_flatten_digests_are_stable` (tests/core/composite_corpus_tests.cpp) and `composite_corpus_render_digests_are_stable` plus the override-equivalence test (tests/ui/composite_render_tests.cpp) flatten/render every committed `test-fixtures/psd` document plus anything dropped into `local-test-fixtures/composite-corpus/`, single-threaded, and compare FNV-1a digests against the tracked baselines in `test-fixtures/psd/*-digests.txt` (plus a local overlay). Any compositor optimization must keep these green; after a DELIBERATE rendering change, delete the tracked files, rerun, and commit the diff. Isolated-group bounds are override-aware (`layer_render_bounds_for_render`): move/transform preview overrides do not force full-clip isolation buffers, and the styled pass-through silhouette includes override-moved children.

Known remaining slow-path costs for future tiers (validated ordering in the August 2026 optimization plan): `merge_layer_into` and the channel-restricted/`GroupMaskedTarget` wrappers (deliberately per-pixel; fusing their factors into a kernel mask row would change float association order), the three float divides per pixel (skipped only when the output alpha is exactly 1.0), exact-value SIMD for the row kernels, a thread pool replacing the per-strip `std::async` spawns, and display-resolution interactive compositing (composite previews at the current mip level; the general form of what the move/transform proxies do). **GPU document compositing** now has two conservative capability tiers: simple top-level 8-bit RGB/RGBA pixel stacks with Normal/source-over blending use GPU textures, while separable blend modes, unfeathered gray8 masks, and supported RGB Blend If thresholds use portable QSB fragment passes that sample the accumulated backdrop. When Dawn is installed, the same tiers can use WebGPU compute tile passes with full-frame or dirty-region readback, preserving an all-or-nothing document decision. Groups, non-separable modes, unsupported or malformed Blend If payloads, feathered masks, and advanced features remain on the byte-authoritative CPU path. The desktop `PATCHY_ENABLE_GPU_CANVAS` build flag includes the Qt Quick/RHI graphics backend by default, with automatic OpenGL, Vulkan, Metal, or Direct3D selection and a CPU fallback for software adapters, unavailable scene graphs, or unsupported document features; the optional `PATCHY_ENABLE_WEBGPU` flag adds automatic Dawn discovery but never makes Dawn mandatory. The CPU path remains the output authority for exports and byte-identity tests.

## Vector-shape edits invalidate their effect rect, not the canvas

Shape drag-out commits, combine-extend drags, Direct/Path Select anchor drags, vector-mask path edits, and the Shape Appearance preview all call `document_changed_effect_bounds(old ∪ new layer effect rect)` instead of the bare full-canvas `document_changed()` (main_window_vector.cpp, canvas_widget_vector_tools.cpp; combine-extend refreshes with `refresh_layer_thumbnails`, not `refresh_layer_list`). Below the 8 Mpx async-defer threshold (and ALWAYS on wasm) a full invalidation is a synchronous full recomposite (several times slower on the quick stress shape steps), so new shape work must keep invalidations bounded. The anchor-drag re-bake still runs full-quality per move; a coarse-preview latch remains a future tier. The warp preview's patches-over-base structure is in [warp.md](warp.md).

## Deferred full-refresh paint

`paintEvent` defers a FULL recomposite to the fire-and-forget async refresh and keeps drawing the previous frame whenever `CanvasWidget::should_defer_full_refresh_to_async()` says so (cache dirty + same-size previous frame + no processing operation + document at or above the compile-time `kProcessingOverlayDirtyAreaThreshold`, 8 Mpx). Big documents do not flash checkerboard on add-layer/undo/blend changes. Deliberately keyed on the compile-time constant (not the `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` override) so overlay-path tests keep their blocking semantics; sub-threshold documents render synchronously in paint.

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

The current Dawn bridge is a bounded execution path, not a promise of a
throughput win: it validates a full or dirty tile graph, executes each planned
tile, and reports the number of rendered tiles and padded readback bytes.
Benchmark results must therefore identify whether they measure Qt RHI
presentation, Dawn document composition, dirty-tile reuse, or the CPU reference.
The path still uploads source layers per composition call and copies the
assembled image through Qt Quick; zero-copy presentation and platform-specific
device-loss recovery remain separate work.
