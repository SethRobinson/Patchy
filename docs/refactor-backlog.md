# Refactor backlog

Deferred findings from the July 2026 code survey (14-agent sweep of the super files,
duplication, and design). The super-file splits, the const-walk revision-bump fixes, the
filter_workflows ODR fix, and the close_document_session teardown-order fix landed in
that round; everything below is verified but deliberately deferred. Line numbers are from
July 2026 and drift; symbol names are the stable reference.

## Test-suite splits (both DONE, July 2026)

Both monoliths are split; each suite stays ONE binary whose main() concatenates
per-group registration functions in a FIXED order that reproduces the original
registration vector entry for entry (never static self-registration; cross-TU init
order is linker-dependent and would reorder the suite). Both splits verified the
before/after [PASS]/[FAIL]/[SKIP] name lists byte-identical; all byte-stability
canaries stayed green with no re-pin.

The core half: tests/test_main.cpp (~26k lines, 458 tests) became tests/core/ — 24
thematic group TUs, each ending in a
`std::vector<patchy::test::TestCase> <group>_tests()` registration function
(declarations in tests/core/test_groups.hpp), concatenated by tests/core/main.cpp.
Shared helpers were MOVED (never copied) into
`namespace patchy::test` in tests/core/core_test_support.{hpp,cpp} (general: solid_rgb/
solid_rgba, make_tool_document, tool_options, find_layer_named, artifact writers,
rgb_diff, fnv1a, the test_*_smart_filter_stack builders) and
tests/core/psd_test_support.{hpp,cpp} (PSD byte-level: BE readers/writers, pascal
strings, PsdLayerChannelRecord parsing, layer-block/image-resource payload helpers,
blend-if payloads); group-exclusive helpers stayed in each TU's anonymous namespace.
The support headers are Qt-free and must stay that way. Adding a core test = add the
function to the right tests/core/<group>_tests.cpp and append a {"name", fn} entry to
that TU's registration vector; new groups also touch test_groups.hpp, main.cpp, and
CMakeLists.txt.

The UI half: tests/ui_visual_tests.cpp (~50k lines, 663 tests) became tests/ui/ in
three passes and the monolith file is deleted. Layout: 30 thematic group TUs
(app_shell through readme_screenshot; tests/ui/ui_test_groups.hpp declares them in the
load-bearing concatenation order), tests/ui/main.cpp (crash handlers, bootstrap,
runner loop), tests/ui/ui_test_support.{hpp,cpp} (`namespace patchy::test::ui`; shared
helpers MOVED, never copied: the accept_*_dialog drivers, click_layer_row_thumbnail
(documented UAF fix, keep semantics), tablet_test_device's three static
QPointingDevice locals defined exactly once, clear_brush_tip_test_state/
clear_pattern_test_state, the smart-object fixture helpers open_smart_object_fixture/
convert_fixture_source_to_external), and tests/ui/ui_test_access.hpp
(MainWindowTestAccess, befriended BY NAME in main_window.hpp; the qualified name must
not change). Each group TU repeats the historical monolith include block (tests/ is on
the target's include path) and the target keeps /bigobj (several TUs exceed 3k lines).
Adding a UI test = add the function to the right tests/ui/<group>_tests.cpp and append
a {"name", fn} entry to that TU's registration vector; new groups also touch
ui_test_groups.hpp, tests/ui/main.cpp, and CMakeLists.txt. Run order stays
load-bearing: `visual_contact_sheet_contains_new_feature_artifacts` CHECKs ~200
artifacts written by earlier tests and opens readme_screenshot_tests' slice (registry
position 654 of 663; keep that position); `cleanup_after_visual_test` restores only
language, so QSettings state leaks between tests by construction, and order changes
need a full-suite shakeout run. shot_readme_* test names are a contract with
scripts/make-readme-screenshots.ps1 (runtime "shot_readme" substring filter).

Harness niceties still open: a PATCHY_TEST(fn) macro would kill the name/function
double-entry; the four hand-rolled runner loops (tests/core/main.cpp,
tests/ui/main.cpp, perf_tests, curves_clipping_preview_tests) could share one.
(The core suite's /bigobj C1128 risk is gone now that no core test TU exceeds ~3k lines.)
DONE (July 2026): the UI suite's crash handlers (dbghelp AV stack walker + the POSIX
signal/terminate reporters) are ported verbatim to tests/core/main.cpp;
curves_clipping_preview_tests now includes test_harness.hpp instead of re-defining CHECK.

## Duplication inventory (verified copy-paste; sizes approximate)

Preset libraries and managers (~1,900 lines total, the biggest win):
- DONE (July 2026): the five *_library.cpp files now share src/ui/preset_library.hpp —
  PresetLibraryBase (QObject, changed() signal, storage dir resolution, json/storage
  paths) + PresetLibraryT<Traits> (entries/find_entry/find_entry_if, sort, folders(),
  and the rename/set-folder/remove skeletons taking per-class write/remove callables).
  Template members instantiate lazily, so FilterLookLibrary (no folders) uses the same
  base. Concrete classes keep Q_OBJECT solely for their tr() contexts (the signal
  lives in the base; moc handles the template base via the *LibraryBase alias). The
  persisted deltas were parameterized, not normalized: brush's QFile sidecar write and
  extra keys, gradient's always-write-folder sidecar and payload+sidecar rewrites, the
  per-class skip-vs-rewrite behavior for no-op renames, and gradient's no-ungrouped-first
  sort order. Shared helpers (utf8 conversions, save_png, pattern_tiles_equal) live in
  patchy::ui::presets. Deliberately NOT merged: the factory-defaults version-gate
  quartets (the rosters/comparisons ARE the deltas and the gate semantics are persisted
  contracts; bodies kept verbatim per class) and layer_style_dialog's local
  pattern_tiles_equal (that file is its own backlog entry). Verified byte-identical
  sidecar/payload output across add/rename/set-folder/restore/reset on all five
  libraries before and after the extraction.
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
- DONE (July 2026): the ~30-line RGB->RGBA staging block around render_photoshop_*
  (8x, 4 per file) now shares stage_rgba_and_render in filters/rgba_filter_staging.hpp
  (internal to src/filters; never include it elsewhere). Each site keeps its own
  guard/progress preamble and passes its exact render_photoshop_* call as a lambda;
  the helper's copy loops are the historical block verbatim (the two files had differed
  only in pointer-star whitespace). Outputs stayed bit-identical (calibrated filter
  tests and catalog canaries green, no re-pin).
- DONE (July 2026): filter_progress_phase (byte-identical in filter_engine +
  filter_registry), recipe_blend_mode_supported / supported_blend_mode, and
  union_bounds / checked_union_bounds (identical math, per-caller error text kept
  via an overflow_message parameter) now live in filters/filter_support.hpp
  (internal to src/filters; never include it elsewhere). smart_filter_renderer's
  report_progress/report_fraction/phase_progress were deliberately NOT merged:
  its phase_progress uses kProgressScale-based overflow-guarded math, a real delta.
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
- DONE (July 2026): acv's Reader now derives from psd::BigEndianReader (keeping only
  the ACV-specific read_tag/remaining_bytes_are_zero); truncation errors now say
  "Unexpected end of PSD data" (the UI wraps them in the generic load-failure notice,
  and ACV records are the same shapes as native PSD curv records).
- DONE (July 2026): the read_file/write_file wrappers across aseprite/bmp/gif/ico/
  ilbm/pcx/tga now share formats/format_file_io.{hpp,cpp} (read_file_bytes,
  rename_first_layer_to_stem, write_file_bytes; format-name parameter keeps every
  historical error string byte-identical). Internal to src/formats.
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
- NOTE (July 2026): main_window.cpp was split further (theme QSS -> main_window_theme.cpp,
  legacy plugins -> main_window_plugins.cpp, retranslation -> main_window_actions.cpp,
  clipboard + layer ops -> main_window_layer_ops.cpp, tool/options-bar/color state ->
  main_window_tool_options.cpp; ~14.6k -> ~9k lines). The remainder is the text tool
  (~7k lines, needs the designed text_render module, not a file split), input plumbing,
  and the text-pipeline-dependent rasterize/merge_down. Duplication items below still
  exist; several now live in the new TUs (flip pair, fill/clear ladder -> main_window_layer_ops.cpp).
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
- DONE (July 2026): the twice-built FilterControlSpec parameter editors
  (filter_workflows vs gallery) now share ui/filter_parameter_panel.{hpp,cpp}
  (FilterParameterPanel; FilterParameterPanelOptions preserves the deliberate
  spin-symbol/width/range deltas). Also DONE (July 2026): the gallery's
  spatial-overlay sync lambdas are factored into ui/filter_overlay_sync.{hpp,cpp}
  plus FilterParameterPanel::sync_control, and the proxy/render-worker
  machinery moved verbatim to ui/filter_preview_proxy.{hpp,cpp} (both now also
  serve the direct filter dialogs' in-dialog preview; the tilt-shift OVERLAY
  DRAWING stayed untouched in zoomable_image_preview.cpp — it is a patent
  design-around). Still open: three parallel latest-wins preview state machines
  (the byte-identical CoalescedPreviewEmitter twins were merged into
  ui/coalesced_preview_emitter.hpp, July 2026).
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

- core/pixel_tools.cpp: DONE (July 2026) — the geometry half moved verbatim into
  core/document_geometry.cpp; pixel_tools.hpp stays the umbrella header and the seven
  both-halves helpers stay defined in pixel_tools.cpp, declared in
  core/pixel_tools_internal.hpp (never include it outside src/core).
  tool_write_paths_digest_baseline still gates both files.
- render/layer_compositor.hpp (2.5k-line header): ~1,000 lines are non-template mask
  machinery (EDT, dilate/blur, PatternTileSampler) movable to a .cpp behind a small
  header; the composite_* templates STAY (type-erasing Target would put a virtual call
  in the reference compositor's per-pixel path). Pinned compositor tests gate it.
  Related: formats/ui code calls render_detail:: directly, bypassing the Compositor
  facade — promote the real API (composite_layers, CompositeSample, LayerBoundsOverride,
  StyleMaskProvider) or widen the facade; composite_layers' four trailing defaulted
  parameters want a CompositeOptions struct.
- ui/filter_workflows.cpp: DONE (July 2026) — the Levels widgets + the four
  request_*_settings adjustment dialogs moved verbatim to ui/adjustment_dialogs.cpp;
  filter_workflows.hpp stays the shared header for both halves, and the two
  both-sides helpers (clamp_levels_settings, AdjustmentPreviewRequest) are declared
  in ui/filter_workflows_internal.hpp (never include it outside those two TUs).

## Megafunction dialogs (defer until the next feature forces them open)

- layer_style_dialog.cpp: request_layer_style_settings is ONE ~3,500-line function
  using 50+ by-reference lambdas as shared mutable state. Plan: a file-local
  LayerStyleDialogContext class + per-effect page builders. Stage one effect page
  at a time; widget objectNames, construction order (LayerStyleCategoryPage enum
  values ARE the QStackedWidget indices, so pages must be created in enum order),
  and the preview/cancel pattern-store-restore contract must stay identical.
  Done July 2026 (both stages suite-verified): (1) the ensure_*/count/add/remove
  per-kind families are table-driven via effect_vector_ops_for_kind
  (LayerStyleEffectVectorOps keyed by LayerStyleEffectKind; ensure_effect template);
  (2) the RGB color row block x7, the update_*_color_preview lambdas, and the modal
  picker click handlers x7 now share RgbColorRowWidgets + make_color_rows +
  connect_color_row_picker (Color Overlay keeps its label + separate button but
  fills the same struct; the bevel highlight/shadow pickers stay separate because
  they live-preview during picking). Remaining: the context class + moving each
  effect page's construction, load/save switch case, and wiring into per-effect
  builders.
- visual_filter_gallery_dialog.cpp: request_visual_filter_gallery is one large
  function (shrunk by ~700 lines in July 2026 when the parameter panel, proxy
  machinery, and overlay sync moved to their own TUs, but still a megafunction).
  Decompose after (and following the pattern of) the layer-style refactor.
  Recipe IDs/parameter keys/captured colors are persistence contracts; the
  tilt-shift grip-bar overlay is patent-pinned.

## Design and bug notes (lower severity, unfixed)

- FIXED (July 2026): the linux-only full-suite segfault in
  `ui_pen_tool_path_mode_keys_and_handles` (surfaced by the .af importer's
  layout shift). A full-suite ASAN run (new `linux-asan` preset; recipe in
  docs/platform.md) named two latent use-after-frees, neither in pen code:
  ~MainWindow destroyed `sessions_` (freeing every Document) before ~QWidget's
  close/deactivate focus-outs reached the still-alive child canvases, whose
  handlers walk `canvas->document_` (fix: the dtor body now detaches every
  session canvas via `set_document(nullptr)` first, the same ordering rule
  close_document_session enforces); and `new_smart_object_via_copy` read a
  SmartObjectStore `find()` pointer after `add_embedded` reallocated the store
  (fix: copy fields out before the add; the invalidation contract is now on
  `add_embedded`). Fixing those also unmasked a stale contact-sheet expectation:
  the Flow/Airbrush change renamed the `ui_airbrush_no_same_stroke_stack`
  artifact to `ui_airbrush_stationary_build_up` but not the list entry, and
  leftover PNGs in long-lived build dirs hid the mismatch everywhere except a
  fresh build tree.

- FIXED (July 2026): the detached async preview/render std::threads now run through
  `run_tracked_background_worker` (src/ui/background_workers.{hpp,cpp}); app main and
  the UI test runner call `wait_for_tracked_background_workers()` after the event loop
  and before QApplication destruction, closing the quit-mid-render shutdown race. Any
  NEW detached worker must use the tracker, never a bare std::thread(...).detach().
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
- Build: DONE (July 2026) the patchy fixture copy now uses copy_directory_if_different
  (was an unconditional copy_directory every rebuild); vcpkg.json's manifest (qtbase+qtsvg) cannot satisfy
  the build's PrintSupport/Network/LinguistTools/Test + qtimageformats requirements, so
  the dev-vcpkg preset is likely unusable as-is; patchy_color is referenced 54 lines
  before its target definition (legal, but a reordering trap).
