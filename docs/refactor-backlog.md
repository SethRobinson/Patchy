# Refactor backlog

Deferred findings from the July 2026 code survey (14-agent sweep of the super files,
duplication, and design). The super-file splits, the const-walk revision-bump fixes, the
filter_workflows ODR fix, and the close_document_session teardown-order fix landed in
that round; everything below is verified but deliberately deferred. Line numbers are from
July 2026 and drift; symbol names are the stable reference.

## Test-suite splits (largest remaining super files)

tests/ui_visual_tests.cpp (~50k lines) and tests/test_main.cpp (~26k) are single TUs by
design today. A split design that preserves behavior:

- Keep ONE binary each; split into per-area .cpp files (tests/ui/, tests/core/), each
  exposing a `std::vector<TestCase> <area>_tests()` registration function, concatenated
  in main() in a FIXED order. Do NOT use static self-registration (cross-TU init order
  is linker-dependent and would reorder the suite).
- Run order is load-bearing in the UI suite: `visual_contact_sheet_contains_new_feature_artifacts`
  CHECKs ~200 artifacts written by earlier tests and must stay last; readme shots late.
  `cleanup_after_visual_test` restores only language, so QSettings state leaks between
  tests by construction — order changes need a full-suite shakeout run.
- Shared helpers move (never copy) to a support TU: the accept_*_dialog drivers,
  clear_brush_tip_test_state/clear_pattern_test_state, tablet_test_device (three static
  QPointingDevice locals — exactly-once rule), click_layer_row_thumbnail (documented UAF
  fix, keep semantics). MainWindowTestAccess is befriended BY NAME in main_window.hpp —
  the qualified name must not change.
- Core suite: 458 tests, no order dependence found, but keep order anyway. The manual
  registration vector is a silent-drop hazard: verify the before/after [PASS] name lists
  are identical.
- Harness niceties to bundle: port the UI suite's dbghelp AV handler to the core main;
  a PATCHY_TEST(fn) macro would kill the name/function double-entry; the four hand-rolled
  runner loops (test_main, ui_visual_tests, perf_tests, curves_clipping_preview_tests)
  could share one; curves_clipping_preview_tests re-defines CHECK instead of using
  test_harness.hpp. patchy_core_tests lacks /bigobj (C1128 risk as test_main grows).

## Duplication inventory (verified copy-paste; sizes approximate)

Preset libraries and managers (~1,900 lines total, the biggest win):
- Five *_library.cpp files (style, pattern, brush_tip, gradient, filter_look) share a
  quintuplicated CRUD/sidecar/reload/sort/folders skeleton (rename_style vs rename_pattern
  etc. are diff-identical), a triplicated factory-defaults version-gate quartet, and
  byte-identical file-scope helpers (utf8 conversions, save_png, pattern_tiles_equal,
  default_storage_dir x5). Suggested shape: PresetLibraryBase (QObject + changed()) +
  a traits/template layer; sidecar JSON bytes and version-gate semantics are persisted
  contracts — parameterize the deltas (brush's QFile write + extra keys; gradient's
  always-write-folder), never normalize them. Keep per-class tr() strings.
- Four *_manager_dialog.cpp files are structural quadruplets (tree + details form +
  Import/Export/Duplicate/Delete/Restore + Close/Use); use_selected is diff-identical
  between style and pattern managers. Suggested: a scaffold builder taking library
  callbacks + objectName prefix (objectNames are load-bearing for UI tests). Pattern
  and brush managers also hand-roll reload_tree ~95% identical to StyleBrowserWidget::reload
  — generalize StyleBrowserWidget into a PresetTreeWidget.
- Small: the brush "paper chip" thumbnail drawn twice with mutual "keep in sync"
  comments. (Done July 2026: the popup screen-edge clamping trio now shares
  dialog_utils position_popup_below, which also fixed the previously unclamped
  show_gradient_preset_popup.)

Filters (~800 lines, canary-gated):
- filter_engine.cpp re-implements ~20 builtin_filters.cpp kernels near-identically with
  progress added (docs/filters.md pins the two PATHS as separate contracts — extraction
  must keep both outputs byte-identical; never redirect legacy through default_invocation).
- The ~30-line RGB->RGBA staging block around render_photoshop_* appears 8x (4 in each
  file). One wrapper removes ~240 lines; outputs must stay byte-identical. This is pure
  scaffolding OUTSIDE the patent design-around kernels.
- filter progress trio + filter_progress_phase duplicated across filter_engine,
  filter_registry, smart_filter_renderer; recipe_blend_mode_supported and
  union_bounds/checked_union_bounds duplicated between filter_registry and
  smart_filter_renderer.
- Levels RECORD math (clamp_levels_record/levels_master_record/set_levels_master_record
  and the per-channel accessors) is deduped: exported from core/adjustment_layer.hpp,
  copies deleted from ui/filter_workflows.cpp, psd/psd_adjustments.cpp, and the
  clamp_record lambda in main_window_adjustments.cpp (ui::LevelsSettings is now an
  alias of LevelsAdjustment, like CurvesSettings). DO NOT merge the two levels
  TRANSFER formulas: core levels_channel rounds through a float (clamp_byte) while
  ui map_levels_value lrounds the double, and MSVC-verified outputs differ by 1/255
  on real inputs (value 4, record {0,45,121%,0,255}: core 35, ui 34; hundreds more
  across the domain). Unifying either direction changes pinned pixels.
- DO NOT merge the box-blur family (layer_compositor, brush_tip, canvas feathering,
  filter tent blur): deliberately different numerics, each pinned by different canaries.

I/O and formats:
- acv_curves_io.cpp re-implements psd::BigEndianReader line-for-line; use it.
- read_file/write_file wrappers copy-pasted across ~9 format TUs (+ 4 repeat the
  rename-first-layer-to-stem block); one shared helper with a format-name parameter.
- The raw-or-PackBits plane decode loop re-implemented 5x around the shared
  decode_packbits (psd channel data, psd_patterns, pat_reader, abr_reader,
  psd_filter_effects); pat_reader duplicates psd_patterns' whole VMA slot parse
  (comment-acknowledged) — export a parameterized reader. NOTE: abr uses a DIFFERENT
  16->8 conversion than deep_sample_to_byte; do not unify those.
- Pascal-string pair duplicated in psd_smart_objects (unpadded = padded_multiple=1).
- document_alpha_mask_layer (psd) duplicates core layer_render_utils'
  document_alpha_rgba8 precondition cascade (predicates differ subtly — diff first).
- Rgba8FlattenTarget/Rgba8RenderTarget duplication across formats is DOCUMENTED
  deliberate (document_flatten.cpp:18); the PNG-8 indexed export predates
  indexed_flatten_for_palette_mode and could adopt it (verify bytes).
- ppi/dots-per-meter converters in bmp vs image_document_io have DIVERGENT fallbacks
  (72 vs 300) — likely deliberate per docs/resolution-units.md; consolidate only with
  explicit fallback parameters.

MainWindow/adjustments internals:
- main_window_adjustments.cpp: the async preview worker lambda x4 (~200 lines), the
  destructive-dialog guard/apply/restore phases x4 (~250 lines), the smart-filter
  command guard preamble x6 (~100 lines), progress-dialog boilerplate x4 (two sites
  bypass the existing progress_dialog_filter_progress helper).
- The "busy progress dialog" pattern is copied across main_window.cpp x2 and
  main_window_palette.cpp (cross-TU — promote to main_window_shared per the split rule).
- undo()/redo() are ~45-line mirror images; flip_active_layer_horizontal/vertical
  identical modulo the flip fn; fill/clear share a ~55-line branch ladder; the view-menu
  checkbox sync block x2; smart-object relink/replace share the repoint_layers lambda
  (~38 identical lines) and update/embed share a resolve-and-read preamble.
- The text-settings-from-editor block appears 3x in main_window.cpp (small real
  differences; text-tool constraint: same-TU helper only).
- FilterControlSpec parameter editors built twice (filter_workflows vs gallery);
  three parallel latest-wins preview state machines (the byte-identical
  CoalescedPreviewEmitter twins were merged into ui/coalesced_preview_emitter.hpp,
  July 2026); the gallery's two spatial-overlay
  callbacks re-declare four sync lambdas each (~75 lines/copy — the tilt-shift OVERLAY
  DRAWING is a patent design-around, only the sync helpers may be factored).
- layer_style_dialog.cpp: the RGB color row block x7, picker click handlers x9,
  blend-mode combo row x11. (add_slider_spin_row and filter_workflows' add_slider_row
  now both forward to dialog_utils add_dialog_slider_spin_row, July 2026;
  add_color_slider_row could adopt it too but differs in structure.)
- canvas: grow_selection/select_similar share a verbatim ~45-line preamble; the
  arrow-key delta switch twice in keyPressEvent; the former image_from_pixels /
  layer_source_image twins are now edit_conversions qimage_from_pixel_buffer
  (July 2026) but keep the per-pixel setPixelColor perf smell (deliberately not
  optimized during the dedup — fix it as its own pinned-output change; the
  gallery's scanLine-based image_from_pixels is a separate, faster variant);
  the 9-line move-session reset block x3 (set_document_internal,
  set_tool, set_edit_locked) — drift risk; three parallel snap resolvers in the guides TU;
  the component-channel enum check written out at ~25 sites (wants a predicate).
- compose_document_pixel/compose_layer_pixel re-implement compositing without
  clipping-group folding or styles for the eyedropper/info readout — sampled color can
  differ from the rendered composite; likely a deliberate O(1)-per-sample fast path,
  document or converge deliberately.

## Second-tier extractions

- core/pixel_tools.cpp (~2.6k): the document/layer geometry half (crop/rotate/flip/
  resize/scale + DocumentChannel geometry) is nearly disjoint from painting — pure-move
  into core/document_geometry.cpp; keep pixel_tools.hpp as umbrella initially;
  tool_write_paths_digest_baseline gates it.
- render/layer_compositor.hpp (2.5k-line header): ~1,000 lines are non-template mask
  machinery (EDT, dilate/blur, PatternTileSampler) movable to a .cpp behind a small
  header; the composite_* templates STAY (type-erasing Target would put a virtual call
  in the reference compositor's per-pixel path). Pinned compositor tests gate it.
  Related: formats/ui code calls render_detail:: directly, bypassing the Compositor
  facade — promote the real API (composite_layers, CompositeSample, LayerBoundsOverride,
  StyleMaskProvider) or widen the facade; composite_layers' four trailing defaulted
  parameters want a CompositeOptions struct.
- ui/filter_workflows.cpp (~2.4k): the Levels widgets + the four request_*_settings
  adjustment dialogs (~1,300 lines) move cleanly to a new adjustment_dialogs.cpp
  (declarations already in filter_workflows.hpp; keep the header whole).

## Megafunction dialogs (defer until the next feature forces them open)

- layer_style_dialog.cpp: request_layer_style_settings is ONE ~3,700-line function
  (82% of the file) using 60+ by-reference lambdas as shared mutable state. Plan:
  a file-local LayerStyleDialogContext class + per-effect page builders + table-driven
  default_*/ensure_*/update_*_color_preview families keyed by effect kind. Stage one
  effect page at a time; widget objectNames, construction order, and the preview/cancel
  pattern-store-restore contract must stay identical.
- visual_filter_gallery_dialog.cpp: request_visual_filter_gallery is one ~2,035-line
  function, same idiom, milder. Decompose after (and following the pattern of) the
  layer-style refactor. Recipe IDs/parameter keys/captured colors are persistence
  contracts; the tilt-shift grip-bar overlay is patent-pinned.

## Design and bug notes (lower severity, unfixed)

- Detached std::threads in the async preview/render machinery capture a raw
  QCoreApplication* and invokeMethod on it; nothing joins them at shutdown — quitting
  mid-render can call into a destroyed QApplication and race static-cache teardown
  (main_window_adjustments.cpp, canvas_widget_render.cpp, visual_filter_gallery_dialog.cpp).
  A shutdown latch or tracked jthread owner would bound it.
- color_panel.cpp: three function-local-static global registries invisibly couple every
  picker to MainWindow palette mode; ~MainWindow clears the hook unconditionally
  (order-dependent if two windows ever coexist). Compare-before-clear or an owner object.
- PSD import copies: read_layers' `std::move(block)` on a const-ref is a silent deep
  copy of every preserved block (multi-MB smart-object blobs); read_layer_record copies
  each tagged-block payload then re-reads it. Perf only; fix with a mutable iteration.
- PSD writer: layer count cast to int16 with no overflow guard (>32,767 records wraps;
  negative collides with the merged-transparency flag).
- psd text: estimate_text_size_from_alpha calls the revision-bumping pixels() on a
  pre-insertion local (harmless today, stale pattern).
- CanvasWidget::focusOutEvent cancels every live gesture EXCEPT a plain content-target
  shape drag (drawing_shape_ stays true) — stale shape preview after Alt+Tab.
- LayerRecord is a record-of-everything (~20 text-only optionals) — a nested TextImport
  sub-struct would improve cohesion (body-touching, separate change).
- MainWindow/CanvasWidget god-class notes for any future redesign: DocumentSession
  (undo stacks, smart-object links) is a private nested struct only friends can reach —
  extracting a session/undo manager is the highest-leverage cut; CanvasWidget's 42
  std::function single-listener callbacks re-implement signals for exactly one listener;
  the CanvasWidget-nested enums (SelectionMode, LayerEditTarget, ...) force
  main_window.hpp and others to include the whole canvas header (want ui/tool_types.hpp);
  the current_* options-bar mirror convention (AGENTS.md) is a missing ToolOptionsModel;
  image_document_io.hpp's 8-overload qimage_from_document_rect* family wants a request
  struct, and RenderedDocumentPatch its own small header; bool-flag params
  (save_document_to_path's flatten_confirmed, pixel_tools' trailing `bool erase` x8).
- Dead code removed (July 2026): adjustment_layer_detail (main_window_layer_panel.cpp)
  and patchy_layer_style_payload plus its orphaned write-side helpers
  (psd_layer_styles.cpp — plFX is read for back-compat but styles are written as
  lfx2 only).
- Build: the patchy target re-copies the whole test-fixtures tree on every rebuild
  (POST_BUILD, no up-to-date check); vcpkg.json's manifest (qtbase+qtsvg) cannot satisfy
  the build's PrintSupport/Network/LinguistTools/Test + qtimageformats requirements, so
  the dev-vcpkg preset is likely unusable as-is; patchy_color is referenced 54 lines
  before its target definition (legal, but a reordering trap).
