# Text tool and Character panel

The inline text editor's session machinery, commit/cancel semantics, and the Character panel. The Photoshop text LAYOUT model (imported type layers, leading rules, runs metadata v3) lives in [ps-compat.md](ps-compat.md) "Photoshop text model"; Warp Text lives in [warp.md](warp.md); offscreen-test font registration gotchas are in AGENTS.md "Testing notes".

Do NOT attempt to split the text code out of main_window.cpp as a pure file move: the text render pipeline is shared between too many members; it is really a "design a text_render module with its own header" job, not a file split (tried and backed out).

## Session lifecycle (provisional layer, commit, cancel)

- A Type-tool click inserts a provisional 1x1 text layer immediately (marker `patchy.internal.provisional_text`); `commit_text_editor` removes it first via the marker-checked `MainWindow::take_provisional_text_layer` (a stale id can never delete an unrelated layer), then snapshots and recreates the committed layer under the same id — cancel/empty-commit leaves history and modified state untouched.
- Mutating actions that take no focus (e.g. the layer lock buttons) must call `finish_active_text_editor()` first, or they operate on a half-committed session.

## Delete semantics

Delete on a text layer deletes the OBJECT, never its pixels: pixel-clearing leaves an invisible layer whose metadata resurrects the text (`clear_active_layer` special-cases it; mixed selections clear pixels + delete text layers in one undo step).

## Options bar while an editor is open

- The options bar shows session apply/cancel buttons (`textApplyButton`/`textCancelButton`) while an editor is open; they must keep `Qt::NoFocus`, otherwise the editor's focus-loss auto-commit fires on mouse press and Cancel commits instead of canceling.
- The font combo is a `FontPickerCombo` (src/ui/font_picker.*, a QFontComboBox whose overridden showPopup opens a searchable list + writing-system preview); its popup objectName `textFontPickerPopup` must stay matched by `is_text_option_widget` (a Qt::Popup is a window, so isAncestorOf-based ownership misses it and focusing the search box would auto-commit the session).
- Any new UI that must coexist with an open text session needs the same `is_text_option_widget` exemption from the focus-loss auto-commit.
- The inline editor claims the standard Bold and Italic shortcuts in `ShortcutOverride` before the app-level Ctrl+B Color Balance and Ctrl+I Invert actions can consume them. The key press toggles the same options-bar buttons so selection and typing-format behavior stay on one path.

## Character panel

- Opened via options bar > Character... while the Text tool is active. It edits the LIVE editor session (leading auto/fixed, tracking, H/V glyph scales) per selection.
- With no live session its controls gray out and a hint label (`textCharacterHint`) says to click in text; the state is kept live by `refresh_options_bar()` calling `sync_text_character_dialog_from_editor()` (every session boundary funnels through that refresh). Without that call the non-modal dialog kept stale enabled controls after a commit and edits silently no-oped (`ui_text_character_panel_disables_without_session` pins it).
- Its dialog (`textCharacterDialog`) is exempted from the editor's focus-loss auto-commit via `is_text_option_widget`.
- Setting fixed leading opts the layer into the Photoshop layout marker at commit (explicit leading does not render under Qt-natural layout; see [ps-compat.md](ps-compat.md)).
