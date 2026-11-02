# Font picker popup

The text bar's font family control (`FontPickerCombo`, `src/ui/font_picker.*`) is a `QFontComboBox`
whose popup is Patchy's own: a search box, Style and Language filters, the family list (a Recent
section on top) and a hover/keyboard preview pane. The Resolve Missing Fonts dialog uses the same
control. Text-session rules for the popup (its objectName, picks that must apply) are in
[text-tool.md](text-tool.md#options-bar-while-an-editor-is-open).

## Rows

- The popup's model (`FontListModel`) is rebuilt on every search keystroke and filter change from
  the combo's own family model. Rows carry `kFontPickerRowKindRole` (`FontPickerRowKind`: family,
  Recent header, All Fonts header) and `kFontPickerRecentRowRole`. Headers have no item flags, so
  they cannot be selected or picked; Up/Down from the search box step over them, and a Page Up/Down
  that lands on one moves to the next family. The All Fonts header draws the separator rule in
  `field_border` (`field_inset_border`, the list frame's color, sits a shade off the list
  background in Dark and the rule vanished; `menu_separator` is too faint in Light; issue 569
  follow-up).
- A pick commits by family name (`findText`), not by row: the same family can be both a Recent row
  and a full-list row.
- The popup opens on the current family's Recent row with the list at the top, so the section is in
  view; a current family that is not recent opens centered in the full list.

## Recent section (issue 569)

- Every user pick (`QComboBox::activated`: a popup pick, or a name typed into the closed control
  that matches a family) moves the family to the front of `ui/textFontPickerRecentFamilies` (a
  string list, no repeats, at most 8 kept). Families no longer installed stay stored but are left
  out of the section.
- The section (header, its rows, the All Fonts header) shows only while the search box is empty:
  while searching the matches are the list. The Style and Language filters apply to it as to the
  full list; when nothing recent passes, no headers show.

## Style and Language filters (issue 570)

Choices persist in `ui/textFontPickerStyleFilter` (tokens `all`, `serif`, `sans-serif`,
`slab-serif`, `script`, `decorative`, `symbol`, `monospace`, `variable`, `other`; new entries are
appended) and
`ui/textFontPickerLanguageFilter` (`all` or the `QFontDatabase::WritingSystem` enum key, such as
`Latin`). Both are persisted identifiers. The combos are `Qt::NoFocus`, so typing stays in the
search box, and Enter there always picks the highlighted font. Both lists use
`use_scrolling_list_popup` (at most `kLongListPopupRows` rows, scrolling; the Language list has
dozens of entries). `ComboListEnterGuard` keeps Enter and Return in an open list: it picks the row
and closes only that list (the key used to reach the font popup and close it), and the search box
hands Enter to any popup open above the font popup.

- Language lists `QFontDatabase::writingSystems()` sorted by translated name; a family passes when
  `QFontDatabase::families(system)` names it.
- Other lists the families whose category stays unknown after every step below (it reads tables
  like the class entries). Language has no such entry: Qt reports at least one writing system for
  every family.
- Monospace is `QFontDatabase::isFixedPitch`. Handwritten and Blackletter are not offered: no font
  field states them reliably (handwriting fonts carry the Script class, blackletter the
  Ornamental one).

### Style classification

`src/core/font_style_class.*` (Qt-free) reads the OS/2 table; `FontStyleIndex`
(`src/ui/font_family_filters.*`) gets the tables through `QRawFont::fromFont(...).fontTable`, so
every platform's font engine answers without a file path.

1. PANOSE family type 5 (Latin Symbol) is Symbol, whatever the class says (Segoe MDL2 Assets carries
   class 8).
2. sFamilyClass (high byte): 1, 2, 3, 4 and 7 Serif (class 4, Clarendon, is Georgia and Century,
   so not Slab), 5 Slab Serif (Rockwell, Courier New), 8 Sans Serif, 9 Decorative, 10 Script,
   12 Symbol.
3. PANOSE: family type 3 Script, 4 Decorative; Latin Text with serif style 6 (Square) Slab Serif,
   2 to 10 Serif, 11 to 15 Sans Serif (15, Rounded, is Calibri and Carlito).
4. A family that claims the Symbol writing system and not Latin is Symbol.
5. The name-based class (`font_class_for_name`: the substitute table, then name words): serif,
   sans, script, display (Decorative). Anything still unknown shows under All Styles and Other.

Variable is the presence of an `fvar` table. Many Google fonts (Montserrat, Oswald, Caveat) leave
both OS/2 fields zero and so only reach a style through their name.

### Cost and the cache

Reading a family loads one font engine, so nothing is read until a table-reading filter (every
Style but All Styles and Monospace) is chosen. Unread families are then classified in 12 ms timer
slices with a "Reading font styles... N%" line; the list fills in every 400 ms and once at the end.
Answers are cached in memory and in `font-style-cache.txt` in the app's local data folder (a
versioned header line, then category token, variable flag, the family's joined
`QFontDatabase::styles` signature and the family name, tab separated). An entry whose style
signature changed is read again.

## Tests

`tests/core/font_style_class_tests.cpp` (`font_style_class_*`: synthetic OS/2 tables and the
bundled web fonts) and `tests/ui/font_picker_filter_tests.cpp` (`ui_font_picker_*`: Recent order,
cap, missing families, header skipping, filters, persistence, the rule's color in both schemes,
list height, Enter in an open list, Other); the popup's older cases are
`ui_text_font_picker_*` in `tests/ui/text_editor_font_picker_tests.cpp`.
