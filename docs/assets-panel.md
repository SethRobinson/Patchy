# Assets panel

Layers kept for reuse in any document (GitHub issue 751). The user saves selected layers as assets and places copies of them into documents later. Read the binding limits before changing it: [legal-constraints-panels.md](legal-constraints-panels.md#assets-panel) ([record](patent-research-assets.md)). In short: one whole local file per asset, no tie between an asset and its source or its placed copies, nothing synced or shared, nothing suggested or classified, and a search box that only filters. The panel's layout, wording and menus are Patchy's own; never model them on another editor's panel.

Code: `src/ui/asset_library.{hpp,cpp}` (storage), `src/ui/assets_panel.{hpp,cpp}` (the panel), `src/ui/main_window_assets.cpp` (adding, placing, the drag over canvases and tabs). Tests: `tests/ui/assets_panel_tests.cpp`.

## The panel

- **Place in the column.** Panel id `assetsDock` (permanent), title row `assetsDockTitle`, `assetsDockTitleLabel`, `assetsDockCollapseButton`, created last by `create_assets_dock` so it is the last section, on its own. It is closed on a first launch and after Window > Reset Panels (`default_hidden_panels_`), so nobody's column grows unasked: Window > Assets (`windowPanelAssetsAction`, command `window.panel_assets`, no default key) opens it expanded, and scripts reach it as `patchy.ui.expandPanel('assets')`. A layout saved before the panel existed keeps it closed ([right-docks.md](right-docks.md#saved-panel-layout)).
- **Rows, top to bottom.** The category picker (`assetsCategoryCombo`) with the Manage button (`assetsManageButton`, its menu `assetsManageMenu`), the search box (`assetsSearchEdit`), the grid (`assetsGrid`, a `QListWidget` in icon mode: a 64 px thumbnail with the asset's name under it), and the Add Selected Layers button (`assetsAddButton`). With nothing to show the grid gives way to a one-line hint (`assetsEmptyHint`): how to add an asset, or that the search matches nothing.
- **Search** filters by asset name, ignoring case, across every category while the box holds text; the category picker is disabled meanwhile and each tooltip names the asset's category. It only filters: Enter in the box does nothing to a document.
- **Categories.** The first category has the permanent id `default` and no stored name until the user renames it; it shows as a translated "General". Manage holds Add Selected Layers, New Category, Rename Category, Delete Category (it asks only when the category still holds assets, and deletes them with it; the last category stays) and Sort by Name. The picked category is remembered in `assets/currentCategory`.
- **An asset's menu** (right-click): Place, Rename, Move to Category, Delete (always asks: the file is removed). Delete and Backspace delete the selected assets, Enter places them. The grid has no drag reordering; Sort by Name is the one ordering command.
- **Thumbnails** are read once per asset per window (`AssetsPanel::icon_for` keeps the icons) and never per repaint. The stored picture is a PNG beside the asset; one that is missing is rendered from the asset's layers again and stored (`MainWindow::asset_thumbnail`).
- **Colors** come from the application theme's list styling. The panel paints nothing of its own.
- **Browser build.** The panel is left out (`create_docks`, `Q_OS_WASM`): user libraries there live in MEMFS and do not outlive the page ([wasm-files-threads.md](wasm-files-threads.md)), so saved assets would vanish on reload.

## Storage

`<app data>/assets` (`asset_library_directory()`; `%APPDATA%\RTsoft\Patchy\assets` on Windows), the per-user folder that also holds user fonts and scripts ([fonts.md](fonts.md)). `PATCHY_ASSETS_DIR` replaces it; the UI suite points it at `test-artifacts/assets/<pid>`. Nothing is read until the panel is first shown or an asset is added, and nothing is created until the first asset or category is saved. No assets ship with Patchy.

- **`<id>.psd`**: one asset. The id is a generated 32-digit hex name, never the user's name for the asset.
- **`<id>.png`**: its cached thumbnail, at most 192 px a side.
- **`index.json`**: `{"format": "patchy-assets", "version": 1, "categories": [{"id", "name", "assets": [{"id", "name"}]}]}`. The arrays are the display order. These tokens and the file names are a file format: they never change.
- Every write is a `QSaveFile`. Adding writes the PSD, then the thumbnail, then the index; deleting writes the index first and then removes the files, so a stop between steps leaves a file nothing lists, never an entry without a file.
- **Damaged or foreign content.** `AssetLibrary::load` keeps an index entry only when its id is well formed, not listed twice, and its file starts with the PSD signature; everything else in the folder is ignored and left alone. A missing or unreadable index is rebuilt from the `<id>.psd` files present, oldest first, into the first category, each named after the layer it holds. An asset whose PSD turns out to be unreadable when it is placed reports that in the status bar.

## What an asset holds

`MainWindow::add_layers_to_assets` builds one document per selected root layer (a group is one asset with its children) and copies the root into it with `copy_layers_between_documents`, the same copy a drag to another document makes, so masks, styles, text, shapes, Smart Objects, their Smart Filter caches and referenced patterns travel as they are.

- **Canvas.** Trimmed to what the layers draw (the union of their render bounds, so a shadow is inside it). A root that draws nothing, or whose Smart Filters depend on where it sits, keeps the source canvas and its position. A side over 30000 px is refused: an asset is a PSD, never a PSB.
- **Document settings.** The source's resolution, color mode, bit depth and profile, and its global light. Guides, channels, paths, file information and the rest of the source document stay behind, and no identifier of the source document or layer is stored.
- **A Background** becomes an ordinary layer.
- **Linked Smart Objects stay linked.** The asset stores the link, not the linked file's contents, re-pathed for the asset's folder exactly as a copy to a document saved there would be; placing re-paths it for the receiving document. If the linked file is later moved, a placed copy shows a missing link like any other.
- The file is written by `psd::DocumentIo::write_layered_rgb8`, so it is a PSD Photoshop opens without a warning ([ps-compat.md](ps-compat.md)). Checked in Photoshop 2027 on 2026-11-26 with the three files `ui_assets_review_capture` keeps (`issue_751_asset_*.psd`): a pixel, a text and a shape asset each opened without a dialog (`scripts\dev\photoshop-flatten-check.ps1`), as a NORMAL, a TEXT and a SOLIDFILL layer on the trimmed canvas at the source's 300 ppi.

## Adding

- **Add Selected Layers** (the footer button, the Manage menu) and **Add to Assets** (`layerAddToAssetsAction`, command `layer.add_to_assets`, on the window with no default key, so Search Commands finds it with the panel closed) run on `selected_or_active_layer_ids()`: one asset per selected root, top to bottom, named after the layer, into the panel's current category. The command opens the panel so the result shows.
- **Rows dragged from a Layers panel** onto the panel do the same (`AssetsPanel::dropEvent` takes the layer drag of [layer-drag-duplicate.md](layer-drag-duplicate.md); the window adds them after the drop returns).
- Adding reads the document only: no undo step, no change. A pending Free Transform or text edit is committed first, as for a copy to another document, and the preview-dialog edit lock refuses the command.

## Placing

`MainWindow::place_assets` reads each asset's PSD and copies its layers into the target with `copy_layers_between_documents`, as Files as Layers does: text stays text, shapes stay shapes, and depth and canvas differences are handled as for layers copied between documents. The copies go above the target's active layer, become the selection, and are one undo step ("Place asset") for the whole call. They are independent copies: nothing links them to the asset, and changing or deleting the asset never changes them.

- **Drag onto a canvas** (a tab's or a float window's): centered on the drop point.
- **Drag onto a document tab**: centered on that document's canvas, and the document becomes the active one.
- **Double-click, Enter or Place**: centered on the active document's canvas. With no document open the status bar says so.
- The drag carries `application/x-patchy-asset-ids` (the ids, one per line) and `application/x-patchy-asset-source` (the dragging process's id; a drag from another Patchy process is refused). `MainWindow::handle_asset_drag_event` runs in the event filter after the cross-document layer drag and before the canvas file drop (GitHub issue 749), and answers every asset drag over a canvas or the tab bar itself, so the file-drop handlers and the layer drag never see one. The preview-dialog edit lock refuses the drop. The placement runs after the drop returns (the panel's `QDrag::exec` is still on the stack).

## Tests

`ui_asset_library_adds_reloads_renames_moves_and_deletes`, `ui_asset_library_skips_damaged_and_foreign_files`, `ui_unicode_asset_library_round_trips` (storage); `ui_assets_panel_is_closed_by_default_and_opens_from_window_menu`, `ui_panel_layout_from_before_assets_panel_keeps_it_closed` (the column and the saved layout); `ui_assets_add_selected_layers_creates_assets_with_thumbnails`, `ui_assets_layer_rows_dropped_on_the_panel_become_assets` (adding); `ui_assets_place_keeps_text_and_shape_layers_editable`, `ui_assets_canvas_drop_centers_on_the_drop_point`, `ui_assets_drop_respects_edit_lock_and_is_not_a_file_or_layer_drag` (placing); `ui_assets_search_and_categories_filter_the_grid`, `ui_assets_manage_dialogs_rename_and_delete` (managing); `ui_assets_review_capture` saves `issue_751_assets_panel` and `issue_751_assets_placed`.
