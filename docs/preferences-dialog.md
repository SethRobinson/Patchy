# Preferences dialog

`MainWindow::show_preferences` in `src/ui/main_window_preferences.cpp` builds the dialog
(`patchyPreferencesDialog`) on every open.

## Pages

In order: Application, Colors and Themes, Tools, Metadata and Watermarks, Pen, Units & Grids,
Snapping, Hotkeys, and Plug-ins (Windows only, [plugins.md](plugins.md)). Tool and canvas-input
behavior (the mouse wheel, Free Transform options) goes on Tools; Pen holds only pen and tablet
settings; Application holds app-level settings (language, updates, recovery, open/save
policies); Colors and Themes the color scheme and themes ([theme-files.md](theme-files.md#preferences-ui)).

The pages are listed down the left (issue 451): `preferencesPageList`, a QListWidget, drives
`preferencesPageStack`, a QStackedWidget, so a new page adds a row instead of widening the
dialog (the tab row it replaced did). Each page is a `preferencesTabHost` holding a
vertical-only `preferencesTabScroll`.

- **Widths.** The list is fixed to its longest title in the current language, measured after
  the dialog sheet is set so the row padding counts. Titles stay the tab-style strings
  (`"Units && Grids"`); `preferences_page_list_text` shows `&&` as `&` and drops mnemonic
  ampersands. The page area's minimum width is the widest page content plus a vertical scroll
  bar, because a scroll area reports almost no minimum of its own and the pages never scroll
  sideways.
- **Minimum width (issue 451).** About 750 px with the list, in every shipped language
  (`ui_preferences_minimum_width_fits_and_fields_stay_compact` caps it at 760 and checks that
  nothing is cut off at that width); the dialog opens at 840. Nothing on a page may need a wide
  single line: options are `WrappingCheckBox`/`WrappingRadioButton` (`dialog_utils.hpp`), notes
  word-wrap, forms use `WrapLongRows` (a field that does not fit goes under its label), hotkey
  names over 200 px wrap, the theme buttons flow, and the theme list and preview are sized to
  fit. Only a row with a label item wraps: give an unlabeled field an empty `QLabel`, never
  `addRow(QString(), field)`, which squeezes it. The Hotkeys panel, built on first visit,
  re-runs the minimum-width fit.
- **Field widths.** `keep_preferences_fields_compact` runs after the dialog sheet: a combo is as
  wide as its longest entry, at most 320 px (a longer list elides, down to 16 characters), spin
  boxes stop at 160 px, one-line text fields at 360 px, and a button with the
  `preferencesTextWidthButton` property at its text. Lists (plug-in folders) still stretch. Do
  not undo it with an Expanding policy.
- **Theme.** The list and page area use the `dialog_tab_*` roles (named for the old tab bar;
  role names are `.patchytheme` keys and never change); the selected row adds
  `@text_on_raised` text and an `@accent` bar on its left edge. No colors in code ([ui-conventions.md](ui-conventions.md#color-scheme)).
- **Keyboard.** The list takes the focus on open, so Up and Down step through the pages.
- **Hotkeys page.** Its ~150 rows are built on the first visit (the stack's `currentChanged`),
  after the dialog sheet, so Qt never repolishes them. Keep the page at its position.
- **Colors and Themes page.** Its widgets are built with the dialog, but the themes are read on
  the first visit (the same `currentChanged`), because loading every theme package is slow.

## Remembered state

- Each list row carries a page id in `Qt::UserRole`: `application`, `themes`, `tools`, `metadata`,
  `pen`, `unitsAndGrids`, `snapping`, `hotkeys`, `plugins`. They are persisted; never rename one.
- The dialog reopens on the page it was closed on (OK or Cancel): key `preferences/lastPage`
  (`stored_preferences_last_page`), read when the dialog opens with `PreferencesPage::Last`,
  the default. An unknown id opens Application. `show_hotkey_preferences` opens Hotkeys with
  its search field filled.
- The dialog resizes by a `VisibleSizeGrip` and remembers `preferences/dialogSize`.

## Tests

Tests pick a page by its listed title through `select_preferences_page` and
`preferences_page_list` (`tests/ui/ui_test_support.hpp`), never by index. The UI suite's
per-test cleanup clears `preferences/lastPage`, so every test opens on Application.
`ui_canvas_aid_preferences_and_guide_dialogs_work` pins the order;
`ui_preferences_page_list_switches_pages_and_remembers_the_last` the list, keys and memory;
`ui_preferences_dialog_shows_every_tab` that every title shows whole.
