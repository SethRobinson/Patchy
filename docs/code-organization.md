# Code organization and translation-unit ownership

Read this before moving functions, adding members to the large UI classes, splitting translation units, or doing broad refactors. Keep moves pure: moving a function between files must not change its body. Helpers belong in one place; never copy a helper when promoting it for cross-TU use.

Related documents:

- [code-organization-main-window.md](code-organization-main-window.md): which `main_window_*.cpp` translation unit owns each MainWindow area (the feature TUs included), the `ui/layer_merge` module behind `merge_down`, and what stays in `main_window.cpp`.
- [code-organization-modules.md](code-organization-modules.md): the other deliberate splits (preset trees and manager dialogs, layer style mask operations, compositor targets instantiated once each, the Affinity importer, layer style and adjustment dialogs, core geometry and resampling, single-home helpers, dialog previews, the gradient preset popup, the test suites).

## MainWindow

`MainWindow` is one class declared in `src/ui/main_window.hpp`, with its implementation split by area into `main_window_*.cpp` translation units; [code-organization-main-window.md](code-organization-main-window.md) says which one owns each area.

Per-file helpers stay in an anonymous namespace. When a second TU needs one, move it to the appropriate module (or `main_window_shared` for window coordination), declare it in the header, and remove the old definition. A duplicated helper with a static local forks its state; an extern declaration beside a same-name anonymous-namespace definition makes calls ambiguous. Each implementation includes its own dependencies. Never copy another file's complete include block or rely on an unrelated feature header to provide a declaration. Public headers include value/member dependencies and forward-declare pointer/reference dependencies where possible. Keep private implementation headers local to their module.

### Growth rule for MainWindow and CanvasWidget

`main_window.hpp` (about 4,250 lines) reaches 205 translation units and `canvas_widget.hpp` (about 4,560 lines) reaches 192 (measured November 4, 2026); in the 30 days before October 25, 2026 they changed in 263 and 144 of 1,387 commits. Each such commit recompiles about 200 TUs. New feature state and behavior therefore live in a helper class in its own header and source (the `DocumentSessionStore`, `SessionHistory`, `DocumentSaveCoordinator` and `DocumentRecoveryCoordinator` pattern), held by the window or canvas through a `std::unique_ptr` and a forward declaration; the giant headers gain one member and one declaration, never a new family of methods or private state. Tests reach the helper through `MainWindowTestAccess`, whose bodies live in `tests/ui/ui_test_access.cpp` so an accessor edit recompiles one object. Headers forward-declare what they use only through pointers and references (`QDialog` and `QListWidget` in `main_window.hpp`, `QWidget` and `CanvasWidget` in `filter_workflows.hpp`), and `ui/canvas_types.hpp` serves a header that needs only the canvas enums and value types.

### Session lifetime and startup

`DocumentSession` (the open-document record: document, title, path, history stacks, save
state, canvas and float-window binding, Smart Object parentage) is a standalone type in
`ui/document_session.hpp`; `MainWindow::DocumentSession` is an alias of it, so every TU and
friend keeps its spelling. The list itself is a `DocumentSessionStore`
(`ui/document_session_store.{hpp,cpp}`, the member is still named `sessions_`): it owns the
sessions in creation order and provides the lookups (`find_by_id`, `find_by_canvas`,
`smart_object_children`, `contains`), the `ids()` snapshot for loops that close documents,
`add`, and `remove`, which hands the entry back so the caller chooses when the session dies.
`MainWindow::session_with_id` / `session_for_canvas` forward to it. `SessionHistory`
owns each session's undo/redo stacks, state identities, rotation, stroke compaction,
and retained-byte accounting; the window owns canvas invalidation and History-panel
presentation. `DocumentSession::HistoryState` remains an alias for compatibility.
`DocumentSaveCoordinator` owns save routing, writer dispatch, option reuse, and saved
state bookkeeping through `SaveHost`. Dialogs, translated status messages, and embedded
Smart Object commits remain host responsibilities. Re-resolve session ids after host
callbacks; never retain a session pointer across a callback that can close it.

Startup creates no document. The start panel in `src/ui/start_panel.cpp` overlays `document_tabs_` only while `sessions_` is empty. `load_tool_settings()` runs once when the first document session is added because it needs a canvas. Only `src/app/main.cpp` calls `MainWindow::begin_startup_update_check`, never construction, so tests start no network request unless one calls it against a local manifest server. `show_window` supplies the historical test document; use `show_window_empty` for real empty-workspace behavior.

`add_document_session` initializes history and hides the start panel before adding
the tab with signals blocked, then explicitly calls `activate_document_canvas`
once. That activation owns the initial panel refresh; file-open callers must not
repeat it after fitting the view. `SessionActivation::Background` (the pages of a
multi-page PDF after the first) stops after publishing the session: no tab switch and
no activation, so the active document is untouched, and the session fits its view on
its first activation instead. Row-build progress and its edit guard are
documented in [layer-panel.md](layer-panel.md#rebuilds-and-absent-rows).

Session data must outlive canvas event delivery. `~MainWindow` detaches every canvas with `set_document(nullptr)` before member destruction frees Documents. `close_document_session` destroys the canvas before erasing the session because QWidget destruction can deliver focus-out events to child widgets. Preserve both orders. References into `SmartObjectStore` do not survive `add_embedded`.

### App lifecycle (startup and quit)

- Startup is timed phase by phase (`support/startup_trace.hpp`); new startup work gets its own
  phase and is measured with `patchy_perf_tests startup` ([startup-performance.md](startup-performance.md)).
- Never leave a Qt host lookup pending at quit: `~QCoreApplication` waits for `QHostInfo`'s thread
  pool with no timeout (GitHub issue 48, a macOS quit freeze). `update_checker.cpp` resolves the host
  on a detached thread first; any new network client does the same.
- The tracked-worker wait after `app.exec()` is bounded (`finish_after_event_loop`): a worker still
  blocked after 10 s ends the process without destructors after dropping the recovery folder, so
  never rely on a destructor for anything the user could lose.
- Never rely on Qt's automatic quit: it is inactive before `app.exec()` (startup pumps events for
  command-line opens and the update check), and inside exec a shown window deletes its posted event.
  `main.cpp` skips exec for a window closed during startup; the update close calls
  `QCoreApplication::exit(0)`.

## CanvasWidget

`canvas_widget_vector_preview.cpp` owns the optional vector view's worker/cache lifecycle;
`vector_preview_renderer.{hpp,cpp}` owns its scene and tiled renderer. See [vector-preview.md](vector-preview.md).
`render/raster_view_context.hpp` scopes full paint bounds and transient style-mask
behavior to a viewport renderer's compositor call; normal renders have no context.

`CanvasWidget` is split into `canvas_widget_*.cpp` files for events, render, view, guides, selection, selection engines, Object Selection (`object_select`), brush, draw tools, crop, Perspective Crop, transform, move, pen, vector tools, path point and segment commands (`path_edits`), artboards, Patch tool, Spot Healing, Red Eye, the Ruler, preview point handles (`preview_points`), script strokes, and cursors. Free transform and warp remain together in `canvas_widget_transform.cpp` because they share pending-session state. Promote cross-TU helpers to `canvas_widget_shared.{hpp,cpp}`.

`canvas_types.hpp` owns tool enums, selection snapshots, render-cache diagnostic counters,
and related value types. `CanvasWidget::...` aliases preserve existing source spellings
and enum values. Session history and stress-report data include this header without the
widget class; `main_window.hpp` must not acquire the widget through either dependency.
`image_save_options.hpp` similarly separates save-option values from image I/O entry points.

`canvas_widget.cpp` keeps construction, document lifecycle, setters, smart-filter-mask targeting, callback plumbing, and picking helpers. Patent-constraint comments for Quick Select solve-on-release and Magnetic Lasso finish-time region construction stay verbatim with their functions in `canvas_widget_selection_engines.cpp`.

## PSD codec

The PSD codec uses one TU per block family: `psd_channel_data`, `psd_image_resources`, `psd_adjustments`, `psd_layer_styles`, `psd_text_read`, `psd_text_write`, `psd_text_legacy` (the PS 5.x `tySh` record), `psd_text_engine_block` (the document `Txt2` block, with its generated `psd_text_engine_template`; [txt2.md](txt2.md)), `engine_data` (EngineData parsing), `psd_layer_records`, `psd_smart_objects`, `psd_vector`, `psd_filter_effects`, `psd_patterns`, `psd_artboard` (`artb`/`artd`), the native Smart Filter descriptors (`psd_smart_filter_adjustments`, `psd_smart_filter_gallery`, `psd_smart_filter_table`), and `psd_tiff` (layered TIFF tag 37724, read through `psd_little_endian`), plus the header-only text-run model `psd_text_runs.hpp`, with shared descriptor and big-endian primitives in `psd_descriptor` and `psd_binary`. `psd_layer_styles` owns the `psd_layer_effects.hpp` exports used by ASL I/O.

Shared internal constants, record types, and declarations live in `psd_io_internal.hpp`; never include it outside `src/psd` (the core tests in `tests/core` may). A declaration another module needs goes in a public `src/psd` header; `formats/photoshop_adjustment_presets.cpp` still includes it for the Levels, Black & White and Hue/Saturation payload parsers. Shared plumbing definitions live in `psd_io_common.cpp`. `psd_document_io.cpp` keeps the read drivers and public `DocumentIo` API. The writer is byte-pinned, so any body change must satisfy the serialization canaries.

## Build-system ownership

`patchy-mcp` is a native console target sharing `src/app/main.cpp` initialization
and `patchy_ui` with the application. `app/mcp_server.cpp` owns startup/proxying,
`app/mcp_stdio.cpp` owns binary stdio, `ui/mcp_session.cpp` owns shared request
lifetime, `ui/mcp_attachment.cpp` owns the interactive app's local socket, and
`ui/mcp_activity.cpp` owns the status-bar indicator and request input guard.
`ui/script_automation.cpp` owns state/preview/history and validates
strokes; `ui/canvas_widget_script_stroke.cpp` calls the existing native stroke
helpers. The shared `patchy_agent_kit` target assembles the installable skill and
copies authoritative API references once. See [ai-control.md](ai-control.md).

CMake runtime assets use shared copy-once targets: `patchy_translations` and `patchy_qt_base_translations` (one `qtbase_<code>.qm` per language in `PATCHY_TRANSLATED_LANGUAGES`), `patchy_qt_runtime`, `patchy_bundled_fonts`, `patchy_bundled_scripts` and `patchy_ai_runtime`, plus `patchy_gegl_bundle` (`cmake/patchy_gegl.cmake`), which `patchy_ui` depends on directly. Never attach per-target POST_BUILD copies into the shared output directory because parallel Ninja builds can race. New executables join them through the existing `patchy_copy_*` helpers, which add the dependency (`patchy_copy_ai_runtime` also links the web build's page-side host script).

Strict warnings belong to Patchy targets. Do not weaken a target's warning level to
accommodate bundled miniz, stb, zstd, or another third-party translation unit. If a
vendored source emits an unavoidable diagnostic, attach the smallest toolchain-specific
exclusion to that source file and warning number. Do not edit vendored code for warning
style. Wholly vendored library targets keep any upstream warning policy isolated on
that target.

On Apple platforms, guarded `CMP0156` and `CMP0179` use the modern linker's static
library rescan and first-occurrence behavior. This removes duplicate-library linker
diagnostics without flattening or reordering Patchy's target dependency graph. Keep the
policies Apple-only and retain the existing `target_link_libraries` ownership.
