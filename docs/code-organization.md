# Code organization and translation-unit ownership

Read this before moving functions, adding members to the large UI classes, splitting translation units, or doing broad refactors. Keep moves pure: moving a function between files must not change its body. Helpers belong in one place; never copy a helper when promoting it for cross-TU use.

## MainWindow

`MainWindow` is one class declared in `src/ui/main_window.hpp`, with its implementation split by area:

- `main_window_chrome.cpp` - frameless-window machinery, `configure_window_chrome()`, and `use_custom_window_chrome()`.
- `main_window_palette.cpp` - palette-mode mutations, palette file I/O, panel/chip refresh, and compliance scanning.
- `main_window_adjustments.cpp` - adjustment-layer creation, editing, previews, and the destructive posterize/threshold/brightness-contrast appliers. The four `apply_levels/curves/hue_saturation/color_balance_adjustment` members live here because their primary callers are the `new_*_adjustment_layer` flows; the destructive dialogs call across TUs through `main_window.hpp`.
- `main_window_filters.cpp` - Smart Filter creation/editing/stack operations, the destructive Filter-menu `apply_filter` flow, Liquify, and the Filter Gallery, including the gallery's cancellable preview state machine.
- `main_window_destructive_adjustments.cpp` - the destructive Levels, Curves, Hue/Saturation, and Color Balance dialog flows, driven by the shared async pixel-preview launcher in `main_window_shared`.
- `main_window_actions.cpp` - the `create_actions()` orchestrator, translation binding, and retranslation machinery. The phase builders live in `main_window_actions_menus.cpp` (menu bar), `main_window_actions_tool_palette.cpp` (tool palette, `add_tool_action`, tool icons), and `main_window_actions_options_bar.cpp` (options bar, `FlowLayout`, option-bar widgets). Cross-phase state travels through `ActionBuildContext` in `main_window_actions_internal.hpp`, which no TU outside `main_window_actions*.cpp` may include; the context dies when `create_actions()` returns, so lambdas must never capture it. Construction order is the contract: menus, tool palette, options bar, translation binding, final refresh, and retranslation callbacks run in registration order.
- `main_window_layer_ops.cpp` - clipboard operations, transform/warp dialogs, layer/folder operations, masks, layer styles and context menu, delete/move, merge-visible, fill/clear/stroke, selection geometry, flips, crop-to-selection, and canvas rotation. `rasterize_active_layers`, `rasterize_active_layer_styles`, and `merge_down` stay in `main_window.cpp` because they render text through the internal text pipeline.
- `main_window_tool_options.cpp` - preset-library accessors, brush-tip import/define, per-layer controls, colors and gradients, tool activation/settings, transform-session controls, options-bar registration, selection-mode buttons, and brush-control synchronization. `current_text_color` and `sync_text_options_from_active_editor` stay in `main_window.cpp` because they use internal text helpers.
- `main_window_theme.cpp` - `photoshop_style()` and application-wide QSS, declared in `main_window_shared.hpp`.
- `main_window_plugins.cpp` - legacy Photoshop plug-in scanning, registration, and execution.
- `main_window_layer_panel.cpp` - layer-row widgets, thumbnails, summaries, refresh, drag, and visibility plumbing.
- `main_window_files.cpp` - the single `file_format_entries()` definition, open/save/export/print/import, and recent files/folders.
- `main_window_smart_objects.cpp` - smart-object export, commit, refresh, relink, embed, replace, convert, and place flows.
- `main_window_sessions.cpp` - document sessions, tabs, close paths, float windows, and `update_start_panel_visibility()`.
- `main_window_preferences.cpp` - Preferences, guides, and pen/view settings.
- `main_window_document_dialogs.cpp` - Image Size/Canvas Size and resize/reset members. The New Document dialog lives in `src/ui/new_document_dialog.cpp`; its preset ids and `newDocument/` keys are persisted and append-only. Screen presets and `reset_document` default to 72 PPI; print presets use 300 PPI.
- `main_window_docks.cpp` - dock creation and right-dock resize handles.
- `main_window_history.cpp` - undo/redo, snapshots, selection history, and history-panel refresh.
- `main_window_vector.cpp` - shape/fill layers, vector masks, work-path operations, and the shape-appearance preview.
- `main_window_channels.cpp` - document channels, alpha channels, Quick Mask, and channel-panel refresh.
- `main_window_paths.cpp` - the Paths panel, path thumbnails, and path/selection conversions.
- `main_window_scripting.cpp` - the Scripts menu, script editor, and CLI script execution.
- `main_window_stress_test.cpp` - the stress-test runner and its CLI entry points.
- `main_window_shared.{hpp,cpp}` - helpers used by more than one MainWindow TU, including the async pixel-preview state/launcher and the progress-dialog filter-progress adapter.

Per-file helpers stay in an anonymous namespace. When a second TU needs one, move it to `main_window_shared`, declare it in the header, and remove the old definition. A duplicated helper with a static local forks its state; an extern declaration beside a same-name anonymous-namespace definition makes calls ambiguous. The split TUs deliberately repeat `main_window.cpp`'s complete include block.

`main_window.cpp` keeps the constructor and event/input plumbing, `configure_canvas`, the text tool and render pipeline, text-dependent rasterize/merge operations, registration helpers, `PreviewDialogEditLock`, and document-action-state machinery. Do not move the text tool as a simple split: it requires a designed `text_render` module with its own interface.

### Session lifetime and startup

Startup creates no document. The start panel in `src/ui/start_panel.cpp` overlays `document_tabs_` only while `sessions_` is empty. `load_tool_settings()` runs once when the first document session is added because it needs a canvas. `MainWindow::begin_startup_update_check` is called only from `src/app/main.cpp`, so tests do not start network requests. `show_window` supplies the historical test document; use `show_window_empty` for real empty-workspace behavior.

Session data must outlive canvas event delivery. `~MainWindow` detaches every canvas with `set_document(nullptr)` before member destruction frees Documents. `close_document_session` destroys the canvas before erasing the session because QWidget destruction can deliver focus-out events to child widgets. Preserve both orders. References into `SmartObjectStore` do not survive `add_embedded`.

## CanvasWidget

`CanvasWidget` is split into `canvas_widget_*.cpp` files for events, render, view, guides, selection, selection engines, brush, draw tools, transform, move, pen, and cursors. Free transform and warp remain together in `canvas_widget_transform.cpp` because they share pending-session state. Promote cross-TU helpers to `canvas_widget_shared.{hpp,cpp}`.

`canvas_widget.cpp` keeps construction, document lifecycle, setters, smart-filter-mask targeting, callback plumbing, and picking helpers. Patent-constraint comments for Quick Select solve-on-release and Magnetic Lasso finish-time region construction stay verbatim with their functions in `canvas_widget_selection_engines.cpp`.

## PSD codec

The PSD codec uses one TU per block family: `psd_channel_data`, `psd_image_resources`, `psd_adjustments`, `psd_layer_styles`, `psd_text_read`, `psd_text_write`, and `psd_layer_records`. `psd_layer_styles` owns the `psd_layer_effects.hpp` exports used by ASL I/O.

Shared internal constants, record types, and declarations live in `psd_io_internal.hpp`; never include it outside `src/psd`. Shared plumbing definitions live in `psd_io_common.cpp`. `psd_document_io.cpp` keeps the read drivers and public `DocumentIo` API. The writer is byte-pinned, so any body change must satisfy the serialization canaries.

## Other deliberate splits

- `ui/preset_tree_widget.{hpp,cpp}` is the generalized folder-grouped preset tree (reload preserving selection/collapse, folder expansion, click dispatch); `ui/style_browser.{hpp,cpp}` keeps `StyleBrowserWidget` as a thin adapter over it whose public API, signals, and objectNames are load-bearing for `layer_style_dialog.cpp` and UI tests. `ui/preset_manager_scaffold.{hpp,cpp}` owns the shared manager-dialog shell (layout, action rows, delete plumbing, use_selected); each `*_manager_dialog.cpp` keeps its previews, persistence, import/export, extra buttons, and strings local so translation contexts and per-dialog behavior deltas stay put. The gradient manager deliberately shares only the button row: its tree is structurally different (non-modal, nested slash-path folders, expanded-set semantics).
- `render/layer_style_mask_ops.{hpp,cpp}` owns the non-template layer-style mask machinery (max-filter/dilate, the exact EDT, stroke distance fields, blur/expand/supersample mask preparation, chamfer falloff, bevel height mask, and Satin mask preparation including `PreparedSatin`). `layer_compositor.hpp` includes the header, so consumers are unchanged; the per-pixel `composite_*`/`render_*` templates, `StyleMaskProvider`, and `style_mask_for_render` stay in `layer_compositor.hpp` because type-erasing the render target would put a virtual call in the reference compositor's per-pixel path. Mask math and iteration order are byte-pinned by the compositor canaries; keep bodies verbatim when reorganizing.
- `ui/adjustment_dialogs.cpp` owns the per-adjustment dialog half of `filter_workflows.cpp`: Levels widgets, adjustment requests, and Hue/Saturation converters. `filter_workflows.cpp` keeps catalog helpers, generic filter settings, Smart Filter blending, and pixel application. Their shared private helpers live in `ui/filter_workflows_internal.hpp`, which no other TU may include.
- `core/document_geometry.cpp` owns crop, rotate, flip, image/canvas resize, and `DocumentChannel` geometry from `pixel_tools.cpp`. Painting stays in `pixel_tools.cpp` and is byte-pinned by `tool_write_paths_digest_baseline`. `core/pixel_tools.hpp` remains the umbrella header; cross-half private helpers are declared in `core/pixel_tools_internal.hpp`, which no other directory may include. Geometry uses public `core/blend_math.hpp` for `clamp_byte`, and callers outside `pixel_tools.cpp` pass `extension_color` explicitly.
- `ui/gradient_preset_popup.{hpp,cpp}` owns the anchored gradient quick-picker popup shared by the layer-style Preset buttons, the gradient toolbar Presets button, and the Edit Gradient Stops dialog. Its child objectNames are derived from the anchor button's objectName and are load-bearing for `layer_style_dialog.cpp` and UI tests.
- The core test suite uses thematic TUs under `tests/core`; the UI suite uses thematic TUs under `tests/ui`. Their registration and helper rules live in [testing.md](testing.md).

## Build-system ownership

CMake runtime assets use shared copy-once targets: `patchy_bundled_fonts`, `patchy_qt_runtime`, and `patchy_qt_base_translations`. Never attach per-target POST_BUILD copies into the shared output directory because parallel Ninja builds can race. New executables call the existing `patchy_copy_*` helpers.
