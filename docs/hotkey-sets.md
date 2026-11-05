# Hotkey files and shortcut sets

Preferences > Hotkeys (`HotkeyEditorPanel`, `src/ui/hotkey_editor.cpp`) edits the override map `HotkeyRegistry` keeps in the `hotkeys` settings group (command id to key sequences, only the commands that differ from their defaults; [ui-conventions.md](ui-conventions.md#hotkeys)). Below the search row sit the "Shortcut set" combo, Import... and Export... (GitHub issue 633). Every one of them only stages, like a hand edit: OK applies, Cancel drops it.

## Hotkey files

`src/ui/hotkey_files.cpp`. Export... writes the staged override map, Import... replaces it.

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
- Import reads the whole file first (files over 1 MB are refused), so a file that is not a hotkey file fails before anything is asked or changed. With overrides already staged it asks before replacing them (not a merge: commands the file does not name go back to their defaults). Command ids this Patchy does not register are skipped and counted; so are key texts that do not parse, have more than one chord, or are keys the capture field refuses (`is_reserved_binding_key`) unless one of Patchy's defaults already uses them as a shortcut (Tab for Hide Panels, Shift+Backspace for Fill), so a built-in set's export imports whole. A command whose every key was refused keeps its default. The counts show in the `hotkeyTransferStatus` line under the set row. Conflicts between imported keys resolve through the registry's usual rule (registration order; the losers show their note).

## Shortcut sets

`src/ui/hotkey_sets.cpp`. The combo lists Patchy (default), Photoshop-style, GIMP-style and Krita-style. A set is a list of overrides on top of Patchy's defaults (`HotkeySetEntry`: command id, PortableText keys joined by newlines, optional macOS keys); choosing one stages exactly `hotkey_set_overrides(set)`, so any key can still be changed and Export saves the result. The combo shows whichever set the staged map equals (`matching_hotkey_set_id`), or a "Custom" entry that exists only while the map matches none. Switching from Custom to a set asks first; switching between sets does not (nothing hand-made is lost). The box is as wide as its widest entry in the current language, Custom included, plus three average characters (`fit_set_combo_width`, redone on a language or style change), so it neither looks crammed nor jumps when Custom comes and goes.

Rules for the tables:

- Only commands Patchy registers; keys for commands Patchy lacks are left out. A set that takes a key one of Patchy's defaults uses must give that command another key or `""` (no key), so the set resolves with no suppression: `ui_hotkey_sets_have_no_conflicts` checks every set on every platform like `ui_hotkey_defaults_have_no_conflicts` checks the defaults, and also that each id exists, no id repeats, and no key is reserved for the canvas unless a Patchy default already uses it (plain digits, Space, arrows, Return, Backspace: Krita's 1 and 2 zoom keys are left out for that reason).
- Patchy's defaults already follow Photoshop's for most commands, so the Photoshop-style set is short; GIMP-style and Krita-style move more letters.
- Where a program's manual gives no default for a function, the set keeps Patchy's key.

## Sources and legal limits

Binding rule: [legal-licensing.md](legal-licensing.md) ("Shortcut sets"). Each table was built from the program's published user documentation only, read on November 5, 2026, and records facts (which key runs which command), never documentation text. Never use GIMP's `menurc`/`shortcutsrc`, Krita's shipped `.shortcuts` files, either program's source, or Adobe `.kys` files. Program names identify the other products only (referential use); README's trademark notice stays.

- **Photoshop-style**: Adobe's "Default keyboard shortcuts" help page (helpx.adobe.com/photoshop/using/default-keyboard-shortcuts.html and its PDF; the live page has redirected since 2026 to a page without key tables, so its last published tables were read, capture of October 27, 2025), Adobe's Photoshop CC Windows keyboard shortcuts reference PDF (menu keys: Proof Colors, Gamut Warning, Close All) and the Layers help page (Merge Visible and the stamp key). Keys Photoshop gives a whole tool group (Shift+letter walks it) are not copied onto every member: Patchy keeps one letter per group plus its cycle command.
- **GIMP-style**: the GIMP 3.0 User Manual (docs.gimp.org/3.0/en): each tool's and menu command's "Activating" section and the Keys and Mouse Reference (`key-reference-*.html`); the 2.10 manual matched it.
- **Krita-style**: the Krita Manual 5.3 (docs.krita.org/en): the tool pages and the main menu pages. The manual gives no key for many tools (crop, gradient, fill, text and others), so those keep Patchy's.
