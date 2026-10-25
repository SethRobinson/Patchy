# Refactor backlog

This file lists current, verified cleanup opportunities. Completed work and refactor
chronology do not belong here. Before taking an item, confirm it still exists with a
repository-wide search and read the area-specific documentation linked from
`AGENTS.md`. The weekly health check (last run October 25, 2026) re-verifies every entry.

All work here is behavior-preserving unless an entry explicitly identifies a bug.
Preserve test registration order, persisted identifiers, serialization bytes,
translation contexts, widget object names, deterministic rendering, and byte-stability
canaries.

## Build time

Measured from `build\opt-dev\.ninja_log` (October 25, 2026): a full opt-dev build is about
143 CPU-minutes, half of it test translation units. The reasons and the cheap fixes already
made are in [building.md](building.md) ("Compile-time profile").

- `render/layer_compositor.hpp` (about 4600 lines, 2850 of them template bodies over a
  `Target` type, 1100 non-template inline bodies, no `extern template`) is instantiated in
  full by each of its 13 includers for their own local Target class: `ui/image_document_io.cpp`
  (121 s), `render/compositor.cpp`, `render/native_duotone.cpp` (147 lines, 57 s),
  `formats/bmp_document_io.cpp`, `formats/document_flatten.cpp`. `bmp_document_io.cpp`'s
  `Rgba8RenderTarget` is method-for-method `document_flatten.cpp`'s `Rgba8FlattenTarget` minus
  an origin offset, and its `Rgb8RenderTarget` largely matches `compositor.cpp`'s
  `Rgb8PixelBufferTarget`. Merge the Target classes, add `extern template` declarations with
  one explicit-instantiation TU per Target, move the cold non-template helpers to a `.cpp`, and
  give `formats/ora_document_io.cpp` (which uses only `layer_knockout_mode`) a small header.
  Output is pinned by the compositor canaries and the composite corpus digests.
- `ui/appearance_properties.cpp` takes about 100 s for 440 lines: 112 `*_FIELD` macro
  expansions, each a generic lambda with `if constexpr (requires ...)` probes inside
  `add_effects<auto Member>`. Replace them with one templated member-pointer helper driven by a
  per-effect constexpr field table. Property keys are persisted identifiers.
- The builder functions over 1000 lines (`build_options_bar` 3860, `request_layer_style_settings`
  2761, `build_menu_bar_actions` 2331, `show_preferences` 1983, `request_visual_filter_gallery`
  1664, `request_shape_appearance_settings` 1497, `make_layer_row_widget` 1225 with about 30
  defaulted `std::function` parameters, `CanvasWidget::mousePressEvent` 1110) inflate optimized
  compile times and are hard to review. Split along their existing comment sections; keep
  object names, translation contexts and registration order.
- Build caching: no compiler launcher and every preset has its own binary directory, so the
  vendored codecs (about 825 CPU-seconds) recompile per preset. Options: sccache with `/Z7`
  for opt-dev (`/Zi` is set at the top of `CMakeLists.txt`), or a shared prebuilt install of the
  vendored static libraries. Seth's call.
- The translation gates run on every build (`patchy_check_translation_catalogs` depends on every
  Patchy source, about 2.6 s opt-dev; the runtime gate relinks and reruns after any `patchy_ui`
  change, about 11.5 s opt-dev, 56 s release). Whether opt-dev may defer them is Seth's call.
- `patchy-mcp` recompiles `app/mcp_server.cpp` and `app/mcp_stdio.cpp` (only `main.cpp` needs
  its own define); they could be shared objects.
- Qt umbrella includes are clean, but `<filesystem>` reaches 404 of 683 TUs through
  `color/color_management.hpp`, `formats/bmp_document_io.hpp` and `ui/image_save_options.hpp`
  (which includes the whole `formats/dds_document_io.hpp` and `rttex_document_io.hpp` for option
  enums). `ui/dialog_utils.hpp` includes `<QSizeGrip>` and `<QMessageBox>` for `VisibleSizeGrip`
  and a few callers; a separate header would take them out of 121 TUs.

## Test infrastructure

- Add a `PATCHY_TEST(fn)`-style registration helper to remove the duplicated test name
  and function entries.
- `run_test_suite` (tests/test_harness.hpp) now drives the core and UI mains; the perf,
  curves-clipping and translation-check mains still have their own loops, and the crash
  handlers in `tests/core/main.cpp` and `tests/ui/main.cpp` are copies. Share them without
  changing registration order or filtering.
- 25 test files define their own `make_*`/`solid_*` document and layer helpers although
  `tests/core/core_test_support.hpp` provides `solid_rgba`; `MainWindow window` plus
  `show_window` plus `require_canvas` is repeated about a thousand times across 103 UI test
  files. A shown-window fixture would cover most of it. `tests/synthetic_dng.hpp` is included
  by 92 test TUs and unused in about 50; 7 core tests include `stb_image.h` without decoding.

## Repeated implementation

### Filters and formats

- `filter_engine.cpp`, `builtin_filters.cpp` and `smart_filter_renderer.cpp` still carry
  parallel kernels beyond the ones `filter_kernels.cpp` now owns (box blur, radial blur,
  emboss, mosaic, the premultiplied accumulator, the noise hash). Provably identical
  legacy/named pairs left: the 5-tap Gaussian, Sharpen, Edge Detect (plus the Sobel
  copy in Glowing Edges), Twirl, Wave, Pinch/Bloat, the cloud/lattice noise, Film
  Grain, Color Halftone, Invert/Grayscale/Sepia/Threshold, the auto LUT loops, the
  luminance and progress-reporter helpers (four reporters, one in `filter_workflows.cpp`).
  Not identical, keep apart: the legacy r=1 box blur (summation order), Vignette, the
  photo looks and contrast/saturation helpers (float vs double), Unsharp Mask, Motion
  Blur, Posterize. Any extraction must keep both outputs byte-identical, add a legacy ==
  named pin first, and must not route legacy calls through `default_invocation`. The
  deep (16/32-bit) kernels in `deep_filters.cpp` could be templates over the pixel type
  but differ by design in places (double running sums, no large-distance motion kernel).
- Table-described native Smart Filters have two near-duplicate tables doing the same job
  (PSD read, author and patch, render validation, settings dialog, Layers-panel text):
  `core/smart_filter_schema.*` (Distort and Stylize: `SchemaSmartFilter`, integers plus a
  point list) and `core/smart_filter_table.*` (Pixelate, Render, Noise and Other:
  `TableSmartFilter`, typed values including unit doubles, integer lists and optional
  keys). Merge them into one table whose value model covers both, keeping each filter's
  descriptor bytes (key order, id forms, absent optional keys), dialog object names and
  translation sources unchanged; the `photoshop-smart-filter-*` capture tests pin the
  bytes and pixels.
- The raw-or-PackBits plane loop appears in PSD channel data, PSD patterns, PAT, ABR,
  and PSD filter effects. PAT also duplicates the PSD pattern VMA slot parser. ABR's
  16-bit conversion intentionally differs from `deep_sample_to_byte`.
- `psd_smart_objects.cpp` has its own Pascal-string reader. Its unpadded form is the
  shared reader with `padded_multiple=1`.
- Byte order: `support/byte_order.hpp` now holds the fixed-width readers, writers and appends,
  but about 20 files still shift bytes by hand; move them over as they are touched. UTF-16
  surrogate decoding appears five times (`formats/metadata/exif_tags.cpp`,
  `metadata_containers.cpp`, `psd/psd_io_common.cpp` twice, `psd_descriptor.cpp`,
  `psd_text_read.cpp`). The Linux and wasm HEIF readers share the lcms transform setup; Windows
  HEIF and JXR share the WIC colour transform.
- PSD `document_alpha_mask_layer` resembles
  `core/layer_render_utils::document_alpha_rgba8`, but their predicates differ. Diff
  the preconditions before sharing code.
- Indexed PNG export predates `indexed_flatten_for_palette_mode` and may be able to use
  it. Verify encoded bytes before changing the path.
- BMP and UI dots-per-meter conversion use different 72 and 300 PPI fallbacks. Share
  them only through an explicit fallback parameter.
- The brush paper-chip thumbnail is drawn in two places (`brush_tip_library.cpp`,
  `brush_tip_picker.cpp`). Replace the paired "keep in sync" implementations with one helper.

### Core helpers

- `clamp_byte` has int/int64/double variants in `filters/builtin_filters.cpp`,
  `filters/smart_filter_table_render.cpp` and `ui/filter_preview_proxy.cpp` beside the public
  float one in `core/blend_math.hpp`. They stay separate on purpose: the integer clamps sit in
  per-pixel loops and the double variant rounds differently, so none is bit-identical to a
  float `lround`. `parse_double_token` appears in `core/smart_object.cpp` and
  `core/text_warp.cpp`.
- Tool-classification switches with overlapping case lists sit in `canvas_widget_events.cpp`
  (twice) and `canvas_widget_shared.cpp`.

### MainWindow and CanvasWidget

- Free Transform's samplers in `canvas_widget_transform.cpp` (`sample_nearest`, `sample_bilinear`,
  `sample_bicubic`, and the gray8 switch) take their cubic weights from `core/resample.hpp` but
  keep their own QImage inverse-mapping loops. They could sit on a core `sample_at(PixelBuffer,
  x, y, method)` once someone wants to re-pin `gray8_resample_identity_and_default_fill` and
  `ui_group_transform_resamples_linked_masks`. The cubic variants already share
  `cubic_tap_weight`; only the loops remain separate.
- Share the destructive-adjustment guard, apply, and restore phases, the Smart Filter
  command guard preambles, and the remaining progress-dialog implementations through
  `main_window_shared` (22 `QProgressDialog` sites, 8 of them in `main_window_files.cpp`).
- Promote the busy-progress-dialog pattern used by `main_window.cpp`,
  `main_window_palette.cpp` and `main_window_layer_ops.cpp` to `main_window_shared`.
- The model-download progress lambda is duplicated between `main_window_ai_selection.cpp` and
  `main_window_watermarks.cpp`.
- Consider small parameterized helpers for the undo/redo pair, horizontal/vertical
  flip pair, fill/clear ladder, view-menu checkbox sync, and Smart Object
  relink/replace and update/embed preambles.
- Three latest-wins preview state machines remain after the shared parameter panel,
  preview proxy, overlay sync, and coalesced emitter extractions.
- `layer_style_dialog.cpp` still repeats blend-mode rows and picker wiring that was not
  covered by `RgbColorRowWidgets`.
- Canvas selection growth and select-similar share a preamble. The canvas also repeats
  arrow-key delta handling, move-session reset, guide snapping, component-channel checks,
  handle-resize rectangle math (`canvas_widget_artboard.cpp`, `canvas_widget_crop.cpp`,
  `canvas_widget_selection.cpp`) and the lock-transparent pixel write path
  (`canvas_widget_patch_tool.cpp`, `canvas_widget_spot_healing.cpp`, twice in
  `canvas_widget_brush.cpp`; painting is byte-pinned).
- Pan mouse handling is repeated in `pattern_manager_dialog.cpp` and `tile_preview_window.cpp`
  (the trace and palette previews now derive from `ui/zoomable_image_preview.hpp`); the panel list plus action-bar
  setup in `channel_panel.cpp` and `paths_panel.cpp`; JSON sidecar load/save in
  `pattern_library.cpp` and `style_library.cpp`; `QIconEngine::paint` mode handling in
  `action_icons.cpp` and `icon_theme.cpp`; the filter overlay `sync_role` blocks and zoom
  buttons in `filter_workflows.cpp` and `visual_filter_gallery_dialog.cpp`.
- Seven direct `QFileDialog::getExistingDirectory` calls bypass the `dialog_utils.hpp`
  file-dialog wrappers.

## Structural extractions

- Extend the narrow render facade when more callers need compositor internals.
  Consider a `CompositeOptions` value instead of the trailing defaulted parameters on
  `composite_layers`. Keep per-pixel templates out of virtual or type-erased paths.
- Split `main_window_stress_test.cpp` only if the scenario table, execution, or
  reporting needs independent work. Stress-test step ids are persisted.
- Decompose `request_visual_filter_gallery` using the layer-style dialog module pattern.
  Recipe ids, parameter keys, captured colors, and the tilt-shift overlay
  are compatibility-sensitive.

## Bugs and performance risks

- `color_panel.cpp` uses function-local static registries to connect every picker to
  MainWindow palette mode. Clearing the hooks is order-dependent if multiple windows
  coexist.
- Unguarded mutable statics beside mutex-guarded caches in `text_font_service.cpp`
  (`covered`, `loaded`, `attempted`); PDF saves draw text on a worker thread. Also check
  `user_fonts.cpp`, `font_face_name_index.cpp`, `main_window_theme.cpp`, `sound_effects.cpp`.
- 29 `catch (...)` sites in `src` swallow errors with no diagnostic (`canvas_widget_render.cpp`,
  `canvas_widget_selection.cpp`, `main_window_tool_options.cpp`, `layer_merge.cpp`,
  `main_window_vector.cpp`, `vector_preview_renderer.cpp`, `canvas_widget_move.cpp`,
  `main_window_watermarks.cpp`, `app/mcp_server.cpp`, ...). Add a debug log or catch
  `std::exception` first.
- PSD import copies preserved tagged blocks, including potentially large Smart Object
  data. Use mutable ownership carefully to remove the copies.
- `estimate_text_size_from_alpha` uses a revision-bumping pixel accessor on a local
  layer before insertion.
- `VisibleSizeGrip` (dialog_utils.cpp) paints a hardcoded light gray, chosen for the
  Dark scheme; it is not scheme-aware, so its strokes can lose contrast on Light's
  pale popups. Route the color through a theme role.
- `Rgb8PerPixelTarget` (the BMP RGB writer) is `Rgb8PixelBufferTarget` without the
  `composite_blended_row` row kernel and the strip origin (render/composite_targets.hpp;
  method bodies otherwise identical, October 2026). Merging them routes BMP RGB export
  through the row kernel, whose byte identity with the general loop is pinned only by the
  PSD merged image and the composite corpus flatten digests. Pin BMP export bytes for a
  layered document with masks and non-Normal modes first, then drop the per-pixel class
  and its instantiation TU. native_cmyk's `NativeTarget` stays TU-local and still
  instantiates `composite_pixel_layer` itself; a header plus instantiation TU would cut
  that TU the same way.
- Styled groups have no UI render cache: every
  repaint re-flattens the group's children and re-runs the effect passes
  (`composite_document_layer` routes styled groups through `composite_layer`).
  Isolated groups always paid the flatten, so the new cost class is the effect
  stack plus the pass-through silhouette flatten. A `LayerStyleRenderCache`-style
  cache keyed on the group's `content_revision()` needs a guarantee that every
  child mutation path bumps the ancestor revision first.
- Likely unused includes (confirm with the `opt-dev-no-pch` preset): `canvas_widget_events.cpp`
  includes `ui/tool_cursors.hpp`; `psd/psd_document_io.cpp` includes `render/compositor.hpp` and
  `core/style_contour.hpp`; `ui/script_api.cpp` includes `formats/apng_writer.hpp`;
  `main_window_files.cpp` includes `core/layer_metadata.hpp` and `core/psd_source_colors.hpp`.

## Longer-term design

- A nested text-import record could replace the text-only optionals on `LayerRecord`.
- Continue separating text editor interaction and window coordination after the font,
  document, render, save, and per-session history modules. Preserve the distinct editor
  settings conversions and canvas/session destruction order.
- The options bar's `current_*` mirrors indicate a missing tool-options model.
- Canvas callback fields (56 `std::function` members on `CanvasWidget`) could become a
  clearer single-listener interface or Qt signals.
- The `qimage_from_document_rect*` overload family and `RenderedDocumentPatch` could
  use request and result types in a small header.
- Replace ambiguous boolean parameters such as `flatten_confirmed` and pixel-tool
  `erase` flags with named option types when those APIs next change.

## Build system

- `patchy_color` is referenced before its target definition. CMake accepts this, but
  the ordering is easy to break during target reorganization.
- The absolute VS 18 cmake.exe path is hardcoded in `scripts/make-readme-screenshots.ps1`,
  `scripts/release/build-release.bat` (with a PATH fallback), `scripts/release/build-wasm.bat`,
  `scripts/run-tests.ps1` and the AGENTS.md handoff command, against the vs-env.bat single-owner
  principle. A bare `cmake` is not a safe fallback on the dev box: a MinGW cmake precedes the
  VS one on PATH even inside the VS environment; resolve it through vswhere like vs-env.bat.
