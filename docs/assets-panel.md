# Assets panel

Layers kept for reuse in any document (GitHub issue 751). The user saves selected layers as assets and places copies of them into documents later. Read the binding limits before changing it: [legal-constraints-interface.md](legal-constraints-interface.md#assets-panel) ([record](patent-research-assets.md)). In short: one whole local file per asset, no tie between an asset and its source or its placed copies, nothing synced or shared, nothing suggested or classified, and a search box that only filters. The panel's layout, wording and menus are Patchy's own; never model them on another editor's panel.

Code: `src/ui/asset_library.{hpp,cpp}` (storage and its worker), `src/ui/asset_thumbnails.{hpp,cpp}` (the pictures and their worker), `src/ui/assets_panel.{hpp,cpp}` (the panel), `src/ui/main_window_assets.cpp` (adding, placing, the drag over canvases and tabs). Tests: `tests/ui/assets_panel_tests.cpp`; timings: `tests/perf_assets.cpp`.

## The panel

- **Place in the column.** Panel id `assetsDock` (permanent), title row `assetsDockTitle`, `assetsDockTitleLabel`, `assetsDockCollapseButton`, created last by `create_assets_dock` so it is the last section, on its own. It is closed on a first launch and after Window > Reset Panels (`default_hidden_panels_`), so nobody's column grows unasked: Window > Assets (`windowPanelAssetsAction`, command `window.panel_assets`, no default key) opens it expanded, and scripts reach it as `patchy.ui.expandPanel('assets')`. A layout saved before the panel existed keeps it closed ([right-docks.md](right-docks.md#saved-panel-layout)).
- **Rows, top to bottom.** The category picker (`assetsCategoryCombo`) with the Manage button (`assetsManageButton`, its menu `assetsManageMenu`), the search box (`assetsSearchEdit`), the grid (`assetsGrid`, a `QListWidget` in icon mode whose delegate paints a 64 px thumbnail with the asset's name under it), and the Add Selected Layers button (`assetsAddButton`). With nothing to show the grid gives way to a one-line hint (`assetsEmptyHint`): how to add an asset, or that the search matches nothing. There is no hint while the library is still being read.
- **Search** filters by asset name, ignoring case, across every category while the box holds text; the category picker is disabled meanwhile and each tooltip names the asset's category. It only filters: Enter in the box does nothing to a document.
- **Categories.** The first category has the permanent id `default` and no stored name until the user renames it; it shows as a translated "General". Manage holds Add Selected Layers, New Category, Rename Category, Delete Category (it asks only when the category still holds assets, and deletes them with it; the last category stays) and Sort by Name. The picked category is remembered in `assets/currentCategory`.
- **An asset's menu** (right-click): Place, Rename, Move to Category, Delete (always asks: the file is removed). Delete and Backspace delete the selected assets, Enter places them. The grid has no drag reordering; Sort by Name is the one ordering command.
- **Thumbnails** come from an `AssetThumbnailCache` the panel owns ([Loading and thumbnails](#loading-and-thumbnails)): a cell asks for its asset's picture when it is first painted and shows a quiet frame until it arrives. Nothing is read or decoded on the UI thread or per repaint.
- **Colors.** The grid's delegate paints the selection, the hover, the placeholder frame and the name with theme roles (`selection_soft_bg`, `list_row_hover_bg`, `list_surface_border`, `text_primary`), as the Home Screen grid does; everything else is the application theme's own styling.
- **Browser build.** The panel is left out (`create_docks`, `Q_OS_WASM`): user libraries there live in MEMFS and do not outlive the page ([wasm-files-threads.md](wasm-files-threads.md)), so saved assets would vanish on reload.

## Storage

`<app data>/assets` (`asset_library_directory()`; `%APPDATA%\RTsoft\Patchy\assets` on Windows), the per-user folder that also holds user fonts and scripts ([fonts.md](fonts.md)). `PATCHY_ASSETS_DIR` replaces it; the UI suite points it at `test-artifacts/assets/<pid>`. Nothing is read until the panel is first shown or an asset is added (`ui_assets_startup_reads_nothing_and_the_grid_decodes_only_what_it_paints` counts the file accesses), and nothing is created until the first asset or category is saved. No assets ship with Patchy.

- **`<id>.psd`**: one asset. The id is a generated 32-digit hex name, never the user's name for the asset.
- **`<id>.png`**: its cached thumbnail, at most 192 px a side.
- **`index.json`**: `{"format": "patchy-assets", "version": 1, "categories": [{"id", "name", "assets": [{"id", "name"}]}]}`. The arrays are the display order. These tokens and the file names are a file format: they never change.
- Every write is a `QSaveFile`. Adding writes the PSD, then the thumbnail, then the index; deleting writes the index first and then removes the files, so a stop between steps leaves a file nothing lists, never an entry without a file.
- **Damaged or foreign content.** A load keeps an index entry only when its id is well formed, not listed twice, and its file starts with the PSD signature; everything else in the folder is ignored and left alone. A missing or unreadable index is rebuilt from the `<id>.psd` files present, oldest first, into the first category, each named after the layer it holds. An asset whose PSD turns out to be unreadable when it is placed reports that in the status bar.

## Loading and thumbnails

Seth, November 26, 2026: anything time consuming is backgrounded so the user feels no lag. An `AssetLibrary` is a UI-thread object; what costs time runs on workers that get plain paths and bytes and answer through queued calls. Both workers follow the Home Screen thumbnails' pattern ([ui-conventions-menus.md](ui-conventions-menus.md#recent-file-thumbnails)): one tracked worker (`run_tracked_background_worker`) drains a queue and ends, so nothing runs while there is nothing to do.

- **The library's worker** (`AssetLibrary::Shared`) runs three kinds of job, one at a time and in order:
  - *Load* (`load_in_background`, from the panel's `showEvent`): reads `index.json` and nothing else, so its cost does not depend on the asset files. `changed()` follows and the grid shows the names. With no usable index it rebuilds the list from the files instead, which parses every asset (the names are in the PSDs); the grid stays empty until that is done.
  - *File check*: after an index load, the signature of every listed file. Entries whose file is gone or is not a PSD are then left out (`assets_removed`, `changed()`), in memory only, as a load always has done.
  - *Add* (`add_asset_in_background`): encodes the PSD, renders the thumbnail and writes both files; the asset is listed on the UI thread when they are whole (`asset_added` carries its picture, so nothing is read back). Queued adds are listed in the order they were queued, and `background_adds_finished` reports the batch to the status bar.
- **Needing the library now.** A change, placing or `ensure_loaded()` while a load is in flight waits for that one read and adopts it; nothing is read twice, and because every change goes through it, an index write can never precede or race the background read. `load()` is the synchronous form (index, then the file check, on the calling thread) for tests and tools.
- **Stale answers.** Each job is a shared record the library also holds. An answer for a job the library no longer holds (a later `load()` did the file check itself), for an asset deleted meanwhile, or for a library that has been destroyed changes nothing.
- **The index write** (rename, move, delete, sort, listing a new asset) stays on the UI thread: about 25 ms at 5000 assets, atomic, and the only writer.
- **Thumbnails** (`AssetThumbnailCache`, owned by the panel): the grid's delegate calls `request(id)` for each cell it paints, so only assets that have been in view are ever read, and an asset never scrolled to is never decoded. The worker reads `<id>.png`; when it is missing it parses the asset, renders the picture (`render_asset_thumbnail`) and stores the PNG again. The cell repaints when the answer arrives. Decoded pictures are kept scaled to the grid's size, least recently painted first out once they pass 32 MB (`kAssetThumbnailMemoryBytes`); a dropped one is decoded again if it is scrolled back to. A fast scroll keeps at most 256 requests queued and drops the oldest.
- **The grid** updates in place for what a library change usually is (the same assets after a rename, more at the end after an add, a few gone after a delete or a move) and is rebuilt only when the list really differs (a category switch, a search). An item holds a name and an id; making one reads nothing.
- **Quit and teardown.** A consented window close calls `finish_pending_asset_writes()`: assets still being written are waited for and listed, so an asset added a moment before quitting is not left unlisted (the library's destructor does the same as a second line). Loads, file checks and thumbnails stop between two files once quit is asked for (`background_workers_stop_requested`) and their answers are never needed. A destroyed library or cache waits for the one job its worker is on, so no worker holds a file of the folder open afterwards. Every file is written through a `QSaveFile`: an interrupted write leaves the old file or none.
- **File-access probe.** `asset_file_access_counts()` counts every open, stat, listing and write of an asset folder, split into UI thread and worker. It is a diagnostic for the tests, which pin with it that startup touches nothing and that the panel's reading is all on the worker.

## Startup and large libraries

Measured with `patchy_perf_tests assets` (`tests/perf_assets.cpp`): it writes libraries of 0, 100, 1000 and 5000 assets (small ones of about 56 KB plus twenty 4000 x 3000 ones of about 5 MB, in four categories with one holding seven tenths) into `test-artifacts/perf-assets`, points Patchy at them with `PATCHY_ASSETS_DIR` and times each step. On an offload host: `scripts\remote\remote-build.ps1 -Target windows -WindowsHost <host> -Preset opt-dev -BuildTarget patchy_perf_tests -SkipTests -PerfScenario assets`. "Stall" is the longest stretch the UI thread spent without returning to the event loop; cold means the file cache was purged first.

An i7-1360P laptop, opt-dev build, November 26, 2026, milliseconds, before the background loading and after it:

| Step | 100 assets | 1000 assets | 5000 assets |
|---|---|---|---|
| App launch to ready, panel closed, warm | 272 -> 272 | 274 -> 271 | 268 -> 267 |
| App launch to ready, panel open, warm | 276 -> 269 | 319 -> 268 | 538 -> 266 |
| App launch to ready, panel open, cold | 455 -> 341 | 759 -> 341 | 2319 -> 346 |
| Open the panel, stall, warm | 13 -> 21 | 84 -> 13 | 1040 -> 15 |
| Open the panel, stall, cold | 77 -> 74 | 634 -> 56 | 2992 -> 62 |
| Switch to the big category, stall | 40 -> 7 | 372 -> 13 | 2047 -> 17 |
| First search keystroke, stall | 5 -> 2 | 54 -> 4 | 306 -> 13 |
| Thumbnails deleted: open the panel, stall | 133 -> 26 | 1114 -> 18 | 5990 -> 21 |
| Thumbnails deleted: a category of large assets, stall | 5767 -> 17 | 26873 -> 16 | 31742 -> 13 |
| Index deleted: open the panel, stall | 4645 -> 21 | 25834 -> 18 | 43028 -> 19 |
| Rename, add or delete one asset, stall | 12 -> 14 | 26 -> 12 | 27 -> 31 |
| Memory after every asset was shown once | | | 80 MB -> 2 MB of pictures |

Adding a 4000 x 3000 layer stalled 1021 ms (2233 ms for a group of three such layers) and now stalls 6 ms (13 ms); the asset appears about as much later as the stall used to last. Duplicate Layer to Document takes about 80 to 100 ms on the same layers.

What still takes time, off the UI thread: with a cold file cache the grid of a 5000-asset library is complete about 3 seconds after the panel shows; a deleted index is rebuilt in 5 to 38 seconds, during which the grid is empty; a large asset's missing thumbnail arrives about a quarter second after its cell is first painted. The stall of 55 to 75 ms on a cold first open is the same with an empty library: the panel's own first layout.

Placing is unchanged and stays on the UI thread, because it changes the document and makes an undo step in order, like every command that reads a file into a document: 390 to 800 ms for the 4000 x 3000 asset and 1740 ms for the group, against 680 to 1140 ms and 2300 ms for Files as Layers on the same content. If that should become a background read later, do it for Files as Layers and placed assets together.

## What an asset holds

`MainWindow::add_layers_to_assets` builds one document per selected root layer (a group is one asset with its children) and copies the root into it with `copy_layers_between_documents`, the same copy a drag to another document makes, so masks, styles, text, shapes, Smart Objects, their Smart Filter caches and referenced patterns travel as they are.

- **Canvas.** Trimmed to what the layers draw (the union of their render bounds, so a shadow is inside it). A root that draws nothing, or whose Smart Filters depend on where it sits, keeps the source canvas and its position. A side over 30000 px is refused: an asset is a PSD, never a PSB.
- **Document settings.** The source's resolution, color mode, bit depth and profile, and its global light. Guides, channels, paths, file information and the rest of the source document stay behind, and no identifier of the source document or layer is stored.
- **A Background** becomes an ordinary layer.
- **Linked Smart Objects stay linked.** The asset stores the link, not the linked file's contents, re-pathed for the asset's folder exactly as a copy to a document saved there would be; placing re-paths it for the receiving document. If the linked file is later moved, a placed copy shows a missing link like any other.
- The file is written by `psd::DocumentIo::write_layered_rgb8`, so it is a PSD Photoshop opens without a warning ([ps-compat.md](ps-compat.md)). Checked in Photoshop 2027 on 2026-11-26 with the three files `ui_assets_review_capture` keeps (`issue_751_asset_*.psd`): a pixel, a text and a shape asset each opened without a dialog (`scripts\dev\photoshop-flatten-check.ps1`), as a NORMAL, a TEXT and a SOLIDFILL layer on the trimmed canvas at the source's 300 ppi.

## Adding

- **Add Selected Layers** (the footer button, the Manage menu) and **Add to Assets** (`layerAddToAssetsAction`, command `layer.add_to_assets`, on the window with no default key, so Search Commands finds it with the panel closed) run on `selected_or_active_layer_ids()`: one asset per selected root, top to bottom, named after the layer, into the panel's current category. The command opens the panel so the result shows. The UI thread only takes the layer copy; the PSD encode, the thumbnail and the file writes happen on the library's worker ([Loading and thumbnails](#loading-and-thumbnails)), and the status bar reports the batch when its last asset is listed.
- **Rows dragged from a Layers panel** onto the panel do the same (`AssetsPanel::dropEvent` takes the layer drag of [layer-drag-duplicate.md](layer-drag-duplicate.md); the window adds them after the drop returns).
- Adding reads the document only: no undo step, no change. A pending Free Transform or text edit is committed first, as for a copy to another document, and the preview-dialog edit lock refuses the command.

## Placing

`MainWindow::place_assets` reads each asset's PSD (on the UI thread, in order: [Startup and large libraries](#startup-and-large-libraries)) and copies its layers into the target with `copy_layers_between_documents`, as Files as Layers does: text stays text, shapes stay shapes, and depth and canvas differences are handled as for layers copied between documents. The copies go above the target's active layer, become the selection, and are one undo step ("Place asset") for the whole call. They are independent copies: nothing links them to the asset, and changing or deleting the asset never changes them.

- **Drag onto a canvas** (a tab's or a float window's): centered on the drop point.
- **Drag onto a document tab**: centered on that document's canvas, and the document becomes the active one.
- **Double-click, Enter or Place**: centered on the active document's canvas. With no document open the status bar says so.
- The drag carries `application/x-patchy-asset-ids` (the ids, one per line) and `application/x-patchy-asset-source` (the dragging process's id; a drag from another Patchy process is refused). `MainWindow::handle_asset_drag_event` runs in the event filter after the cross-document layer drag and before the canvas file drop (GitHub issue 749), and answers every asset drag over a canvas or the tab bar itself, so the file-drop handlers and the layer drag never see one. The preview-dialog edit lock refuses the drop. The placement runs after the drop returns (the panel's `QDrag::exec` is still on the stack).

## Tests

`ui_asset_library_adds_reloads_renames_moves_and_deletes`, `ui_asset_library_skips_damaged_and_foreign_files`, `ui_unicode_asset_library_round_trips`, `ui_asset_library_background_adds_are_ordered_and_finished_at_close` (storage); `ui_assets_startup_reads_nothing_and_the_grid_decodes_only_what_it_paints`, `ui_assets_pending_loads_survive_changes_and_a_closed_window`, `ui_assets_missing_thumbnails_and_index_recover_in_the_background` (loading, by file-access counts and states, never by wall-clock time; `settle_assets` in the test file is the deterministic wait: it pumps events until `AssetsPanel::busy()` stays false); `ui_assets_panel_is_closed_by_default_and_opens_from_window_menu`, `ui_panel_layout_from_before_assets_panel_keeps_it_closed` (the column and the saved layout); `ui_assets_add_selected_layers_creates_assets_with_thumbnails`, `ui_assets_layer_rows_dropped_on_the_panel_become_assets` (adding); `ui_assets_place_keeps_text_and_shape_layers_editable`, `ui_assets_canvas_drop_centers_on_the_drop_point`, `ui_assets_drop_respects_edit_lock_and_is_not_a_file_or_layer_drag` (placing); `ui_assets_search_and_categories_filter_the_grid`, `ui_assets_manage_dialogs_rename_and_delete` (managing); `ui_assets_review_capture` saves `issue_751_assets_panel` and `issue_751_assets_placed`.
