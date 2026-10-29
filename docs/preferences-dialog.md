# Preferences dialog

`MainWindow::show_preferences` in `src/ui/main_window_preferences.cpp` builds the dialog
(`patchyPreferencesDialog`) on every open.

## OK and Cancel

The button box `preferencesButtonBox` has OK and Cancel (issue 510). Controls only change the
dialog: the `Accepted` branch of `show_preferences` stores and applies every setting, so a new
setting is written there, never from its control's signal. Cancel, Esc and the chrome X skip
that branch, and the `restore_theme` scope guard undoes the theme list's live preview
(`ThemesPage::revert`). Buttons that act at once stay done on Cancel: the preset managers,
Customize Tool Palette (its own OK), theme import, reload and delete, Remove Added Fonts, model
downloads and deletes. The page and dialog size are remembered either way.

## Pages

In order: Application, Colors and Themes, Tools, Metadata and Watermarks, Pen, Units & Grids,
Snapping, Hotkeys, Plug-ins (Windows only, [plugins.md](plugins.md)) and Presets (one button per
preset manager, issue 485). Tool and canvas-input
behavior (the mouse wheel, Free Transform options) goes on Tools; Pen holds only pen and tablet
settings; Application holds app-level settings (language, interface scale and text size
([ui-text-size.md](ui-text-size.md)), updates, recovery, open/save policies); Colors and Themes the color scheme and themes ([theme-files.md](theme-files.md#preferences-ui)).

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
- **Open combo lists (issue 489).** Qt opens a list as wide as its box, and the list's own
  row padding then elided entries a compact box shows whole. `keep_preferences_fields_compact`
  gives every combo `fit_combo_popup_to_entries`, which widens the list on each open to its
  widest entry (`combo_list_entry_width`), so it follows the current language. Neither Qt
  size hint is enough: Fusion's menu rows report a flat minimum for every entry, and Windows
  11 insets list text 5 px a side beyond the hint, so the width comes from the style's own
  text rectangle. `ui_preferences_combo_lists_fit_their_entries` opens every combo on every
  page in English, German and Russian; it covers the Windows styles only when Qt finds the
  styles plug-in (a development build needs `QT_PLUGIN_PATH` set to the Qt kit's plugins).
- **Theme.** The list and page area use the `dialog_tab_*` roles (named for the old tab bar;
  role names are `.patchytheme` keys and never change); the selected row adds
  `@text_on_raised` text and an `@accent` bar on its left edge. No colors in code ([ui-conventions.md](ui-conventions.md#color-scheme)).
- **Keyboard.** The list takes the focus on open, so Up and Down step through the pages.
- **Hotkeys page.** Its ~150 rows are built on the first visit (the stack's `currentChanged`),
  after the dialog sheet, so Qt never repolishes them. Keep the page at its position.
- **Colors and Themes page.** Its widgets are built with the dialog, but the themes are read on
  the first visit (the same `currentChanged`), because loading every theme package is slow.

## Opening speed

The dialog is rebuilt on every open, so its build cost is what the user waits for (local-022:
a slow open looked like a hang). Measured offscreen on an i9-11900KF, it went from about 190 ms
per open (650 ms when opening on Hotkeys) to about 95 ms (140 ms on the first open of a session).

- **One sheet change.** Every `setStyleSheet` on the dialog restyles every widget on every page
  (about 45 ms each), so the preferences rules and `dialog_spinbox_button_style()` go in as one
  `append_themed_style`, after the pages exist. Never add a second append.
- **Built in place.** The page list, page stack, buttons and size grip are created inside the
  chrome's content widget. Reparenting the filled stack there afterwards re-resolved every
  page's style (27 ms).
- **CMYK profiles.** `installed_cmyk_profiles()` keeps each file's result for the session
  ([cmyk.md](cmyk.md#which-profile)); the first open still reads them.
- **Lazy start page.** When the dialog opens on Hotkeys or Colors and Themes, it shows first
  with a `preferencesLoadingNote` ("Loading...") on that page; the page is built once the note
  has painted (a 250 ms fallback covers a dialog that never paints), then the note goes. While
  the start page is chosen, `choosing_start_page` keeps the `currentChanged` handlers from
  building it. Switching to either page later builds it at once under the wait cursor.
- **Wait cursor.** `show_preferences` sets `Qt::WaitCursor` from the menu click and restores it
  on the dialog's first event-loop turn (a guard restores it on every other way out); the
  Hotkeys build sets it again. The Hotkeys panel itself (about 200 ms to build, 200 ms to show
  its widgets) is the remaining slow part.

## Remembered state

- Each list row carries a page id in `Qt::UserRole`: `application`, `themes`, `tools`, `metadata`,
  `pen`, `unitsAndGrids`, `snapping`, `hotkeys`, `plugins`, `presets`. They are persisted; never
  rename one.
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
`ui_preferences_dialog_shows_every_tab` that every title shows whole;
`ui_preferences_open_shows_busy_cursor_and_fills_start_page_after_showing` the wait cursor and
the lazy start page; `ui_preferences_cancel_button_discards_changes`,
`ui_preferences_escape_discards_changes` and `ui_preferences_ok_button_keeps_changes` the two endings. A test that opens the dialog on Hotkeys or Colors and Themes calls
`wait_for_preferences_start_page` before looking at that page (`select_preferences_page` does).
