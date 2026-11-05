# Hotkey files and shortcut sets

Preferences > Hotkeys (`HotkeyEditorPanel`, `src/ui/hotkey_editor.cpp`) edits the override map `HotkeyRegistry` keeps in the `hotkeys` settings group (command id to key sequences, only the commands that differ from their defaults; [ui-conventions.md](ui-conventions.md#hotkeys)). Below the search row sit the "Shortcut set" combo with Rename... and Delete, and under them Import... and Export... (GitHub issue 633). Every one of them only stages, like a hand edit: OK applies and stores the sets, Cancel drops it.

## Hotkey files

`src/ui/hotkey_files.cpp`. Export... writes the staged override map, named after the set shown; Import... adds the file as a set of the user's own (below). Both open in the folder last used (`hotkeySets/lastFolder`, else Documents).

```json
{
  "format": "patchy-hotkeys",
  "version": 1,
  "shortcuts": {
    "file.new": ["Ctrl+F9"],
    "edit.undo": []
  }
}
```

- Persisted identifiers (never rename): the suffix `.patchykeys`, the `format` token `patchy-hotkeys`, the keys `format`, `version`, `shortcuts`, and the command ids themselves. A later version may only add keys; readers ignore keys they do not know and accept any `version` of 1 or more.
- Keys are `QKeySequence::PortableText`, so `Ctrl` is Cmd on macOS and a file moves between platforms. An empty array means the command was deliberately left without a key; a string in place of the array is read as one key.
- Writes go through `write_file_bytes_atomically` with the path converted by `to_filesystem_path`; the browser build hands the file out as a download.
- Import reads the whole file first (files over 1 MB are refused), so a file that is not a hotkey file fails before anything is asked or changed. It then asks for the set's name, prefilled with the file's name made unique (`unique_set_name`); the new set takes the file's keys (not a merge: commands the file does not name keep their defaults) and is selected. `import_from_file(path, name)` skips the question for tests and automation. Command ids this Patchy does not register are skipped and counted; so are key texts that do not parse, have more than one chord, or are keys the capture field refuses (`is_reserved_binding_key`) unless one of Patchy's defaults already uses them as a shortcut (Tab for Hide Panels, Shift+Backspace for Fill), so a built-in set's export imports whole. A command whose every key was refused keeps its default. The counts show in the `hotkeyTransferStatus` line under the set row. Conflicts between imported keys resolve through the registry's usual rule (registration order; the losers show their note).

## Shortcut sets

`src/ui/hotkey_sets.cpp`. The combo lists Patchy default, Photoshop-style, GIMP-style and Krita-style, each followed by "(built-in)" (`tr("%1 (built-in)")`) so nobody expects to edit a shipped set in place; the replace-your-changes question names the set without it (`shortcut_set_name`). A set is a list of overrides on top of Patchy's defaults (`HotkeySetEntry`: command id, PortableText keys joined by newlines, optional macOS keys); choosing one stages exactly `hotkey_set_overrides(set)`. The box is as wide as its widest entry in the current language, Custom included, plus four average characters (`fit_set_combo_width`, redone on a language or style change; a user's name counts up to 32 characters), so it neither looks crammed nor jumps when Custom comes and goes. Preferences' minimum width counts the panel's header rows as well as its list, so the set row never overlaps.

## Your own sets

Built-in sets never change (Seth, November 5, 2026). The panel keeps an active set id and the user's sets (`UserHotkeySet`: id, name, overrides), staged with the keys:

- Every change of the staged keys runs `note_staged_change`. With a user set shown, the set takes the change. With a built-in set shown, the keys move to the Custom set (`kCustomHotkeySetId`, no name, shown as "Custom"), which is created when missing; when Custom already holds different keys, `hotkeyCustomReplaceMessageBox` asks first and Cancel undoes the change. The status line says the change went to Custom.
- Rename... (`hotkeySetRenameButton`) and Delete (`hotkeySetDeleteButton`) work on the user set shown and are off for a built-in one. Renaming Custom turns it into a named set (`user-<n>` id), so the next edit of a built-in starts a new Custom. Names are unique among the user's sets and never equal a built-in set's name or Custom's, ignoring letter case (`set_name_taken`). Deleting the set shown selects Patchy default.
- Switching sets never asks: every set keeps its keys. Reset All selects Patchy default and leaves the user's sets alone.
- Storage (persisted identifiers, `hotkey_sets.cpp`): the `hotkeySets` settings group, never under `hotkeys`, whose every key the registry reads as a command id. `hotkeySets/user` is an array of `id`, `name` and `shortcuts` (the hotkey file JSON); `hotkeySets/active` is the set shown on OK; `hotkeySets/lastFolder` is the import and export folder. On open, `load_shortcut_sets` keeps the stored set when the registry still holds its keys, puts keys changed elsewhere into the stored user set, or else selects the matching built-in set or puts them in Custom (how keys saved before user sets arrive).
- Tests: `ui_hotkey_editor_built_in_sets_stay_unchanged`, `ui_hotkey_editor_exports_and_imports_files`. Test restorers snapshot `hotkeySets` with `hotkeys` (`SettingsGroupRestorer`), and the UI suite clears it at start. The panel is built after Preferences fits its combos, so it calls `fit_combo_popup_to_entries` on the box itself; the open list shows every entry whole.

Rules for the tables:

- Only commands Patchy registers; keys for commands Patchy lacks are left out. A set that takes a key one of Patchy's defaults uses must give that command another key or `""` (no key), so the set resolves with no suppression: `ui_hotkey_sets_have_no_conflicts` checks every set on every platform like `ui_hotkey_defaults_have_no_conflicts` checks the defaults, and also that each id exists, no id repeats, and no key is reserved for the canvas unless a Patchy default already uses it (plain digits, Space, arrows, Return, Backspace: Krita's 1 and 2 zoom keys are left out for that reason).
- Patchy's defaults already follow Photoshop's for most commands, so the Photoshop-style set is short; GIMP-style and Krita-style move more letters.
- Where a program's manual gives no default for a function, the set keeps Patchy's key.

## Sources and legal limits

Binding rule: [legal-licensing.md](legal-licensing.md) ("Shortcut sets"). Each table was built from the program's published user documentation only, read on November 5, 2026, and records facts (which key runs which command), never documentation text. Never use GIMP's `menurc`/`shortcutsrc`, Krita's shipped `.shortcuts` files, either program's source, or Adobe `.kys` files. Program names identify the other products only (referential use); README's trademark notice stays.

- **Photoshop-style**: Adobe's "Default keyboard shortcuts" help page (helpx.adobe.com/photoshop/using/default-keyboard-shortcuts.html and its PDF; the live page has redirected since 2026 to a page without key tables, so its last published tables were read, capture of October 27, 2025), Adobe's Photoshop CC Windows keyboard shortcuts reference PDF (menu keys: Proof Colors, Gamut Warning, Close All) and the Layers help page (Merge Visible and the stamp key). Keys Photoshop gives a whole tool group (Shift+letter walks it) are not copied onto every member: Patchy keeps one letter per group plus its cycle command.
- **GIMP-style**: the GIMP 3.0 User Manual (docs.gimp.org/3.0/en): each tool's and menu command's "Activating" section and the Keys and Mouse Reference (`key-reference-*.html`); the 2.10 manual matched it. The set leaves Rotate View unbound: GIMP turns the view with Shift+middle drag and its R is Rectangle Select.
- **Krita-style**: the Krita Manual 5.3 (docs.krita.org/en): the tool pages and the main menu pages. The manual gives no key for many tools (crop, gradient, fill, text and others), so those keep Patchy's.
