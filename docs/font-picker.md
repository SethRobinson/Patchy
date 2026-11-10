# Font picker popup

The text bar's font family control (`FontPickerCombo`, `src/ui/font_picker.*`) is a `QFontComboBox`
whose popup is Patchy's own: a search box, Style and Language filters, the Favorites toggle, the
family list (a Recent section on top, or only the Favorites) and a hover/keyboard preview pane. The Resolve Missing Fonts dialog uses the same
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
- Family names are plain (#646 follow-up): the combo's model is a `QStringListModel` subclass that
  Qt still fills from `QFontDatabase::families()`, but its display and edit text drop Qt's
  " [Foundry]" suffix, so the closed control, `currentText()` and `currentFont()` name "Arial",
  never "Arial [TMC ]". The model keeps one row per foundry copy (Qt selects rows by its own list);
  the popup lists the family once. Recent and Favorites saved with a suffix read back plain, once.
- Row buttons (`font_picker_row_button_rect`, `FontPickerRowButton`): every family row has the
  favorite star at its right; Recent rows have the remove x rightmost with the star just left of
  it. Their space is reserved on every row so names never re-elide on hover. A favorite's star is
  filled in `font_favorite_star` always; an empty star and the x show on the hovered row only, in
  `text_secondary` (`text_primary` over the button). `RowButtonController`, an event filter on the
  list's viewport, owns presses inside a button rect: the row is neither selected nor picked, the
  action runs queued on release over the same button (it rebuilds the model), and a double click
  toggles twice. Clicks elsewhere on the row and all keyboard handling are untouched; there is no
  keyboard shortcut for the buttons.
- The popup opens on the current family's Recent row with the list at the top, so the section is in
  view; a current family that is not recent opens centered in the full list.

## English names (issue 691)

Preferences > Application, "Show font names in English" (`ui/showFontNamesInEnglish`, a persisted
identifier, off by default) shows a family the font database lists in another script under its
English name. Font databases name a face for the user's locale
([font-resolution.md](font-resolution.md)), and some fonts carry only a native name on top of a
Latin PostScript name. Code: `src/ui/font_display_names.{hpp,cpp}`.

- **Display only.** The listed family stays the key: the combo's model, `currentFont()`, a row's
  `Qt::DisplayRole`, Recent, Favorites, the text engine and the PSD reader and writer never see the
  shown name. A family row draws `kFontPickerShownNameRole`; the preview pane's specimen line and
  the closed control show the same name. Hovering a renamed row gives the listed name as a tooltip.
- **The name comes from the face's own `name` table** (`english_family_name_from_name_table`, read
  through `QRawFont`, so every platform's font engine answers): the family record of the kind the
  database listed (GDI family id 1, or typographic family id 16) in Windows en-US, any Windows
  English, Macintosh English, then any record in Latin script; the other kind next; the
  PostScript name (id 6) last. A candidate must be Latin script whatever language it is filed
  under, and a family already named in Latin script (`font_name_is_latin`) is never opened.
- **One table per font database load** (`font_display_names()`): built on first use, one font
  engine load per family listed in another script, answers kept by family and style list so a
  later `fontDatabaseChanged` reopens nothing. Empty while the option is off. Nothing is read per
  repaint. A load measured 2 to 6 ms per family on Windows (154 system families, November 2026),
  paid once per launch when the options bar is built; there is no disk cache, so a machine with
  hundreds of such families is where one would be worth adding.
- **A shown name is unique** (`FontDisplayNames::build`): a name another family is listed or
  already shown under is dropped and that family keeps its listed name, so
  `family_for_display_name` always leads back to one family.
- **Search** matches the listed and the shown name. The rows are ordered by shown name
  (case-insensitive) when any family has one; otherwise Qt's order stands.
- **Closed control.** `show_row_display_name` sets the line edit to the shown name with the combo's
  signals blocked, like a missing family's name: unblocked, the text would reach `QFontComboBox`
  through `currentTextChanged` and become `currentFont()`'s family. `row_family()` answers the
  listed family. Enter on a shown name typed into the control is taken by the combo's event
  filter, which selects the family's row: the stock handling would add the English name as a new
  row. The control's inline completion still completes listed names only.
- **Toggling** emits `FontDisplayNamesNotifier::changed`; every `FontPickerCombo` refreshes its
  closed control, and the next popup lists the new names. No restart.
- Tests inject names with `ScopedFontDisplayNameLookup`, because the offscreen font database
  lists every family in English.

## Recent section (issue 569)

- Every user pick (`QComboBox::activated`: a popup pick, or a name typed into the closed control
  that matches a family) moves the family to the front of `ui/textFontPickerRecentFamilies` (a
  string list, no repeats, at most 8 kept). Families no longer installed stay stored but are left
  out of the section.
- The section (header, its rows, the All Fonts header) shows only while the search box is empty:
  while searching the matches are the list. The Style and Language filters apply to it as to the
  full list; when nothing recent passes, no headers show.

- The x on a Recent row (issue 569 follow-up) removes the family from the stored list
  (`remove_recent_font_family`) and rebuilds the rows keeping the scroll position; the popup stays
  open and the section hides when nothing recent is left.

## Favorites (issue 630)

- The star toggles a family in `ui/textFontPickerFavoriteFamilies` (a string list in the order
  added; persisted identifier). Families no longer installed stay stored but never show. The
  model's `kFontPickerFavoriteRole` reads the popup's set of installed favorites.
- The star `QToolButton` (`textFontPickerFavoritesButton`, checkable, `Qt::NoFocus`) right of the
  Language combo switches to the Favorites view: a Favorites header, then every favorite that
  passes the search and both filters, in the full list's order; Recent and All Fonts hide. It is
  a separate toggle, not a Style entry, because favorites are not a style. With nothing to list,
  an `empty_note` row (unselectable) says how to add favorites or that none match. The toggle
  persists in `ui/textFontPickerFavoritesOnly` (bool), like the filters. Unstarring a row in the
  Favorites view removes it at once.

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

### Hidden matches (issue 647)

While the search box has text, `textFontPickerHiddenMatchesButton` (a flat `QToolButton` under the
list, `text_secondary` ink) counts the families that match the search but fail the Style or
Language filter ("More matches with All Styles and All Languages: %n"); in the Favorites view it
counts favorites only. It hides at zero, with an empty search, and while every filter is All.
Clicking it sets both combos to All. The count comes from the same pass over family names as the
rows, so it loads no font; families still waiting to be classified are not counted until read.

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
list height, Enter in an open list, Other, the favorite star, the Favorites view with filters and
persistence, the Recent x, the hidden-matches hint, plain names for foundry duplicates; English
names: `ui_font_english_name_*`, `ui_font_display_names_round_trip_to_the_listed_family`,
`ui_font_picker_shows_english_names_when_enabled`,
`ui_preferences_show_font_names_in_english_checkbox`); the popup's older cases are
`ui_text_font_picker_*` in `tests/ui/text_editor_font_picker_tests.cpp`.
