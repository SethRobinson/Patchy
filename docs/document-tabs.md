# Document tab context menu

Read this before changing the menu a right-click on a document tab opens, Rename File, Revert,
the full-path tab tooltips and Properties File line, or what opening an already open file does.
Floating a document (also on this menu) is in [float-windows.md](float-windows.md).

`MainWindow::show_document_tab_context_menu` (src/ui/main_window_sessions.cpp) builds the
menu `documentTabContextMenu` fresh on each right-click:

| Item | objectName | Notes |
|---|---|---|
| Close / Close Others / Close All | `documentTabClose*Action` | Close Others needs a second document |
| Float in Window | `documentTabFloatAction` | on wasm it shows the [desktop-only prompt](wasm.md#desktop-only-commands) |
| Reopen Document | `documentTabReopenAction` | reloads the file in place; asks first when modified |
| Revert | `documentTabRevertAction` | Revert below for the clicked document; shows F12 |
| Reveal in Explorer (Finder, file manager) | `documentTabRevealAction` | per-platform label |
| Copy File Path | `documentTabCopyPathAction` | native separators |
| Rename File... | `documentTabRenameFileAction` | below; hidden on wasm |
| Scan for Watermarks... | `documentTabScanForWatermarksAction` | after a separator; File > Metadata > Scan for Watermarks for the clicked document |

The file items act on the session under the clicked tab (by session id, since the session
may close while the menu is open) and are disabled when it has no path: a never-saved
document or an embedded Smart Object's tab. Rename File is also disabled on a linked Smart
Object's tab, whose path is the parent's link target.

Scan for Watermarks works on any document, saved or not. It activates the clicked document
(after the menu has closed), then runs `show_file_info_dialog(true)`: File Info on its
Watermarks page with the scan already run ([watermarks.md](watermarks.md#scan-results)). The
item shows the shortcut of File > Metadata > Scan for Watermarks... (`fileScanForWatermarksAction`, hotkey
id `file.scan_for_watermarks`, no default key), which owns the key.

## Rename File

Renames the document's file on disk, in its folder. Safe while the document is open:
every reader loads the whole file into memory and closes it, so no handle is held on any
platform. Code: `src/ui/document_file_rename.{hpp,cpp}` (name rules, the rename, the dialog)
and `MainWindow::rename_document_file` / `adopt_renamed_document_path`
(src/ui/main_window_files.cpp).

- **Dialog** (`patchyRenameFileDialog`, through `exec_dialog`): the stem in
  `renameFileNameEdit`, pre-filled and selected; the extension is fixed text
  (`renameFileExtensionLabel`, everything from the last dot), so a rename never changes
  the format. Problems show in `renameFileProblemLabel` (the warning banner roles) while
  typing, with OK (labelled Rename) disabled. An unchanged name closes with nothing done.
  A rename the disk refuses keeps the dialog open with the reason, for another name.
- **Name rules** (`file_name_problem`): not blank, not only dots and spaces, no `/`, `\` or
  control characters, not a Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, with
  any extension; refused on every platform so the file stays usable on Windows), at most
  255 characters (UTF-8 bytes off Windows). Windows also refuses `< > : " | ? *` and a
  trailing dot or space; macOS refuses `:`.
- **Never replaces a file** (`rename_file_without_replacing`): Windows uses `MoveFileExW`
  without `MOVEFILE_REPLACE_EXISTING` (its error codes tell a sharing violation, "open in
  another program", from the rest); elsewhere `QFile::rename`, whose engine refuses an
  existing target. Never `std::filesystem::rename` or the atomic writer's replacing rename.
  A target that exists is refused unless it is the same file under a case-only change
  (`std::filesystem::equivalent` plus a case-insensitive name match): `photo.psd` to
  `Photo.psd` on a case-insensitive volume goes through a unique
  `<name>.<pid>-<random>.patchy-rename` sibling, and back if the second step fails.
- **After the rename**: every session whose path is the old file (compared without case on
  Windows and macOS) takes the new path; a title equal to the old name, or starting with it
  and a space ("photo.psd (Recovered)"), takes the new name; `image_save_options_path`
  follows. Nothing marks the session saved: unsaved edits stay and the next Save writes
  the new name. Then the recent-files entry is replaced where it stands
  (`replace_recent_file`, which also clears both names from the missing and confirmed
  caches), the recovery sidecar is rewritten (`DocumentRecoveryCoordinator::refresh_sidecar`,
  keeping the copy's `savedAt`; a write in flight is followed by the rewrite), and
  `refresh_document_tab_titles` updates tabs, float titles and the window title (macOS
  `windowFilePath` included). The Window menu rebuilds on show. Scripts read `doc.name` and
  `doc.path` live, and the MCP state fingerprint changes by itself.
- **Not touched**: other documents that link the file as an external Smart Object (the
  same as a rename in the file manager; they relink).

No scripting function: the UI tests drive the real menu and dialog, so none was needed.

## Full path

Two open files with one name, from different folders, tell apart by their paths (local-032):

- **Tab tooltip**: every document tab's tooltip is the document's full path with native
  separators (`session_file_path_text`). `MainWindow::set_document_tab_label` sets the tab's
  text and tooltip together; every place a tab is added, reinserted or retitled goes through
  it (`add_document_session`, Dock to Tabs, `refresh_document_tab_titles`), so the tooltip
  follows open, Save As, Rename File and Revert. A document without a path says why
  (`session_no_file_text`): "Not saved yet", or "Smart Object in <parent>" for an embedded
  Smart Object's contents tab. A linked Smart Object's tab shows its linked file, the file
  its Save writes.
- **Properties panel**: `documentPathLabel` (`DocumentPathLabel`,
  src/ui/document_path_label.{hpp,cpp}), right under the document summary line, reads
  "File: <path>" for the active document (`refresh_properties_document_path`, run from
  `refresh_document_info` and `refresh_document_tab_titles`), or the same no-path text. A
  path too long for the panel is elided in the middle; the full path stays in its tooltip and
  in its right-click Copy File Path (the tab menu's own string). The line is selectable only
  while it is shown whole, so a selection never copies an elided path. Hidden with no
  document.
- Float windows keep the title alone in their title bar; the Properties line covers them
  while they are active.

Test: `ui_document_tab_path_tooltip_tells_same_names_apart` (tests/ui/document_rename_tests.cpp;
two `apple.psd` from two folders, one with a non-ASCII name; Save As, Rename File, elision, an
untitled document). Captures: `ui_document_tab_path_tooltip_review_captures`
(`document_path_properties_*`, `document_path_tab_tooltip_*`, a sample path set on the
widgets so no machine path shows).

## Opening a file that is already open

`MainWindow::open_document_path` (src/ui/main_window_files.cpp), which File > Open (each
picked file), Open Recent, drops on the window, the tab bar or an empty Layers panel, and a
second launch's forwarded files all go through, first looks for a session showing the file
(`session_for_file_path`, keyed by `file_identity_key` in src/ui/qt_paths.hpp: the canonical
path when the file exists, else the cleaned absolute path, case-folded on Windows and macOS
only). A match is activated (`activate_document_session`, so a floating document's window
comes forward), the file moves to the top of Open Recent and the status bar says "<name> is
already open"; nothing reloads and no prompt asks about unsaved changes (GitHub issue 560).
Open Folder skips images already open the same way. Untitled and pathless sessions (browser
picks, PDF pages after the first) never match; a renamed file matches by its new path.

`OpenExisting::LoadCopy` keeps the old behavior: `app.open` in scripts (documented to return
a new document), Edit Contents of a linked Smart Object (an open copy may belong to another
parent), the stress test's PSD reloads, and the test helper `MainWindowTestAccess::open_document_path`
(tests reopen files they just saved; `open_document_path_as_user` takes the user path).
UI tests: `ui_open_already_open_file_switches_to_its_tab`, `ui_unicode_open_twice_keeps_one_document`.

## Tests

UI filter `ui_document_tab_rename_file` (tests/ui/document_rename_tests.cpp): path, title,
tab, window title, recent position and sidecar after a rename with the document still
modified, then Save to the new name; an existing target refused with both files unchanged;
a case-only rename; invalid names and the unchanged name; the item disabled for an
untitled document; Unicode folder and names; Dark and Light captures
(`rename_file_tab_menu_*`, `rename_file_dialog_*`). The other menu items:
`ui_document_tab_context_menu` (tests/ui/layer_context_lifecycle_tests.cpp); Scan for
Watermarks: `ui_document_tab_scan_for_watermarks` (tests/ui/file_info_tests.cpp, capture
`ui_document_tab_menu_scan_for_watermarks`).

## Revert

`file.revert` (F12, GitHub issue 584; `MainWindow::revert_active_document` in main_window_files.cpp) reloads the active document from its file. It has no menu-bar row since issue 619's second follow-up: the window carries `fileRevertAction` (F12, Search Commands), and the tab menu's Revert (enabled by `session_can_revert` for the clicked tab's document, off under a preview dialog) activates that document after the menu closes and runs the same command (`ui_issue_619_preferences_in_edit_and_revert_in_tab_menu`). Enabled for a document with a file in a format File > Open reads (or one opened by its content), never for an untitled document or an Edit Contents tab (`active_document_can_revert`). A modified document asks first (`revertDocumentMessageBox`); one with open smart object contents is refused. The file opens as a new session before the old one closes without a save prompt, so a file that no longer opens leaves the document as it was; the new tab moves to the old tab's place. Not undoable; the file is only read. Test: `ui_issue_584_revert_reloads_the_saved_file`.
