# Document tab context menu

Read this before changing the menu a right-click on a document tab opens, or Rename File.
Floating a document (also on this menu) is in [float-windows.md](float-windows.md).

`MainWindow::show_document_tab_context_menu` (src/ui/main_window_sessions.cpp) builds the
menu `documentTabContextMenu` fresh on each right-click:

| Item | objectName | Notes |
|---|---|---|
| Close / Close Others / Close All | `documentTabClose*Action` | Close Others needs a second document |
| Float in Window | `documentTabFloatAction` | hidden on wasm |
| Reopen Document | `documentTabReopenAction` | reloads the file in place; asks first when modified |
| Reveal in Explorer (Finder, file manager) | `documentTabRevealAction` | per-platform label |
| Copy File Path | `documentTabCopyPathAction` | native separators |
| Rename File... | `documentTabRenameFileAction` | below; hidden on wasm |

The file items act on the session under the clicked tab (by session id, since the session
may close while the menu is open) and are disabled when it has no path: a never-saved
document or an embedded Smart Object's tab. Rename File is also disabled on a linked Smart
Object's tab, whose path is the parent's link target.

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

## Tests

UI filter `ui_document_tab_rename_file` (tests/ui/document_rename_tests.cpp): path, title,
tab, window title, recent position and sidecar after a rename with the document still
modified, then Save to the new name; an existing target refused with both files unchanged;
a case-only rename; invalid names and the unchanged name; the item disabled for an
untitled document; Unicode folder and names; Dark and Light captures
(`rename_file_tab_menu_*`, `rename_file_dialog_*`). The other menu items:
`ui_document_tab_context_menu` (tests/ui/layer_context_lifecycle_tests.cpp).
