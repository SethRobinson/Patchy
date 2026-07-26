# Refactor backlog

This file lists current, verified cleanup opportunities. Completed work and refactor
chronology do not belong here. Before taking an item, confirm it still exists with a
repository-wide search and read the area-specific documentation linked from
`AGENTS.md`.

All work here is behavior-preserving unless an entry explicitly identifies a bug.
Preserve test registration order, persisted identifiers, serialization bytes,
translation contexts, widget object names, deterministic rendering, and byte-stability
canaries.

## Test infrastructure

- Add a `PATCHY_TEST(fn)`-style registration helper to remove the duplicated test name
  and function entries.
- Share the runner loop used by the core, UI, performance, and curves-clipping test
  binaries. Keep each suite's registration order and filtering behavior unchanged.

## Repeated implementation

### Filters and formats

- `filter_engine.cpp` and `builtin_filters.cpp` contain near-identical filter kernels
  with different progress plumbing. Any extraction must keep both outputs
  byte-identical and must not route legacy calls through `default_invocation`.
- The raw-or-PackBits plane loop appears in PSD channel data, PSD patterns, PAT, ABR,
  and PSD filter effects. PAT also duplicates the PSD pattern VMA slot parser. ABR's
  16-bit conversion intentionally differs from `deep_sample_to_byte`.
- `psd_smart_objects.cpp` has its own Pascal-string reader. Its unpadded form is the
  shared reader with `padded_multiple=1`.
- PSD `document_alpha_mask_layer` resembles
  `core/layer_render_utils::document_alpha_rgba8`, but their predicates differ. Diff
  the preconditions before sharing code.
- Indexed PNG export predates `indexed_flatten_for_palette_mode` and may be able to use
  it. Verify encoded bytes before changing the path.
- BMP and UI dots-per-meter conversion use different 72 and 300 PPI fallbacks. Share
  them only through an explicit fallback parameter.
- The brush paper-chip thumbnail is drawn in two places. Replace the paired "keep in
  sync" implementations with one helper.

### MainWindow and CanvasWidget

- Share the destructive-adjustment guard, apply, and restore phases, the Smart Filter
  command guard preambles, and the remaining progress-dialog implementations through
  `main_window_shared`.
- Promote the busy-progress-dialog pattern used by `main_window.cpp` and
  `main_window_palette.cpp` to `main_window_shared`.
- Consider small parameterized helpers for the undo/redo pair, horizontal/vertical
  flip pair, fill/clear ladder, view-menu checkbox sync, and Smart Object
  relink/replace and update/embed preambles.
- Keep the three text-settings-from-editor variants in `main_window.cpp`; their
  differences are real and the text pipeline is still same-TU code.
- Three latest-wins preview state machines remain after the shared parameter panel,
  preview proxy, overlay sync, and coalesced emitter extractions.
- `layer_style_dialog.cpp` still repeats blend-mode rows and picker wiring that was not
  covered by `RgbColorRowWidgets`.
- Canvas selection growth and select-similar share a preamble. The canvas also repeats
  arrow-key delta handling, move-session reset, guide snapping, and component-channel
  checks.
- `qimage_from_pixel_buffer` still uses per-pixel `setPixelColor`. Treat a scanline
  rewrite as a pinned-output performance change.
- `compose_document_pixel` and `compose_layer_pixel` omit clipping-group folding and
  layer styles. Decide whether the eyedropper's fast-path difference is intentional
  before converging it with the reference compositor.

## Structural extractions

- Define a supported render facade for code that calls `render_detail` directly.
  Consider a `CompositeOptions` value instead of the trailing defaulted parameters on
  `composite_layers`. Keep per-pixel templates out of virtual or type-erased paths.
- Split `af_document_io.cpp` along container parsing, object decoding, and document
  assembly when the importer next needs substantial work.
- Split `main_window_stress_test.cpp` only if the scenario table, execution, or
  reporting needs independent work. Stress-test step ids are persisted.
- Replace `request_layer_style_settings` with a file-local context and per-effect page
  builders. Page construction order matches `LayerStyleCategoryPage` values and is
  load-bearing. Preserve preview cancellation and pattern-store restoration.
- Decompose `request_visual_filter_gallery` after the layer-style dialog establishes
  the pattern. Recipe ids, parameter keys, captured colors, and the tilt-shift overlay
  are compatibility-sensitive.

## Bugs and performance risks

- `color_panel.cpp` uses function-local static registries to connect every picker to
  MainWindow palette mode. Clearing the hooks is order-dependent if multiple windows
  coexist.
- PSD import copies preserved tagged blocks, including potentially large Smart Object
  data. Use mutable ownership carefully to remove the copies.
- PSD writer layer count is cast to signed 16-bit without guarding overflow above
  32,767 records.
- `estimate_text_size_from_alpha` uses a revision-bumping pixel accessor on a local
  layer before insertion.
- `CanvasWidget::focusOutEvent` does not cancel a plain content-target shape drag, so
  `drawing_shape_` can survive an application switch.

## Longer-term design

- A nested text-import record could replace the text-only optionals on `LayerRecord`.
- Extracting document session and undo ownership from `MainWindow` remains the
  highest-leverage class boundary.
- Canvas tool enums could move to a small `ui/tool_types.hpp` header.
- The options bar's `current_*` mirrors indicate a missing tool-options model.
- Canvas callback fields could become a clearer single-listener interface or Qt
  signals.
- The `qimage_from_document_rect*` overload family and `RenderedDocumentPatch` could
  use request and result types in a small header.
- Replace ambiguous boolean parameters such as `flatten_confirmed` and pixel-tool
  `erase` flags with named option types when those APIs next change.

## Build system

- The vcpkg manifest does not cover all Qt modules used by the build, so
  `dev-vcpkg` is not a reliable bootstrap path.
- `patchy_color` is referenced before its target definition. CMake accepts this, but
  the ordering is easy to break during target reorganization.
