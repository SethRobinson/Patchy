# Interface text size

Preferences > Application > Text size (GitHub issue 495) sizes the interface's text alone, as a
percent of the default, on top of the interface scale (which scales everything,
[ui-conventions.md](ui-conventions.md#standing-ui-rules)). Read this before writing a font size,
or the width or height of something that holds text, into a stylesheet or a widget.

## Setting

- Field `preferencesUiTextSizeSpin` (in `preferencesUiTextSizeRow`) under Interface scale: any
  whole percent from `kMinUiTextSizePercent` to `kMaxUiTextSizePercent` (80 to 125), 5% arrow
  steps (`src/ui/app_settings.hpp`). Key `preferences/uiTextSizePercent` (a compatibility
  contract); 100 is stored as no key, and a missing or non-numeric value reads as 100.
- Read once at startup: `main.cpp` calls `set_ui_text_size_percent(stored_ui_text_size_percent())`
  before the application font is set and before the first stylesheet is built. A change applies
  on the next start (a page reload on the web build); OK shows the restart notice
  (`preferencesUiTextSizeMessageBox`, or one combined notice with the interface scale's when both
  changed). Applying it live would need every fixed size computed at construction redone.
- The range is what the sweep below was clean at. Widen it only after the same sweep passes at
  the new end.

## How it applies

`src/ui/ui_text_size.hpp` holds the process-wide percent. At 100 every helper returns its input,
so the default interface is unchanged.

- **Application font.** `main.cpp` scales the platform UI font through `ui_text_scaled_font`.
- **Stylesheets.** `apply_theme_tokens` runs `resolve_ui_text_sizes` on every themed sheet first:
  each `font-size: <n>px` becomes `ui_text_px(n)` (in proportion, smaller too), and `@tx(<n>)`
  becomes `ui_text_length(n)` for a length tied to a line of text. A size already taken from a
  live font (which carries the text size) is written `font-size: <n>px /* scaled */` and left
  alone (the AI Setup dialog). `photoshop_style()` is cached per text size as well as per theme.
- **Lengths that hold text.** `ui_text_length(n)` grows a width or height with a larger text
  size and keeps `n` at a smaller one: the padding, frames and arrows beside the text do not
  shrink, so a field scaled down to 80% cut its own text. Fixed widths of options-bar combos and
  spin boxes, `configure_dialog_spinbox`, the Layer Style category list, the Filter Gallery
  columns, Liquify's note, the Export for Web settings column, Divide Scanned Photos' side
  panel, the New Document fields and preset cards (card height grows by its three text lines),
  the recent-file rows (`recent_row_height`), the start panel column, the Curves axis labels,
  the About dialog's width and the artboard preset picker's cap use it. The app font is scaled to
  a whole pixel size (`ui_text_scaled_font`), rounded as the sheets are, so sizes measured before
  a widget is polished match the polished font.
- **Layouts that change away from 100%.** Where the longer languages ran out of room even with the
  lengths grown, the layout changes instead (above 100%): the Filter Gallery's Live Canvas
  Preview goes on a line under the zoom controls, Trace Image's status line wraps, the Strip
  All Metadata option is a `WrappingCheckBox`, and Divide Scanned Photos' side column is as wide
  as its content asks (at 80% its form stopped wrapping rows under their labels). `keep_wrapped_label_height` keeps a wrapped label's
  minimum at its text height so a short panel scrolls instead of cutting it (the empty Palette
  hint).
- **Dialogs.** At a non-default size `place_dialog` (desktop) grows a dialog to its size hint
  and to the height its word-wrapped text needs, up to the screen, and never shrinks one; a
  layout request after it shows (text set later, a status line) grows the height again
  (`grow_dialog_for_text_size`, `DialogPositionMemoryFilter`). Message boxes and progress
  dialogs size themselves. The About dialog polishes its labels before measuring its height.
- **Rules.** Write UI font sizes in `px` in a themed sheet, never `pt` and never in a sheet that
  skips `apply_theme_tokens`. A fixed width or height that has to hold text is `@tx(n)` in QSS
  or `ui_text_length(n)` in C++, or comes from font metrics; icon sizes, borders and art stay
  fixed. A font derived in C++ from a widget's font follows already; a literal pixel or point
  size for UI text goes through `ui_text_px`/`ui_text_pt`.

## Verification

`tests/ui/ui_text_size_tests.cpp`. `ui_text_size_sweep_keeps_text_fitting_<en|de|fr|ru|ja>`
opens the home screen, the main window (every tool's options bar, every panel tab), the
Preferences pages, the Update prompt and the dialog of every `...` menu command and every filter
(printing, scanning, scripts, plug-ins, Free Transform and Qt's own file dialog left out) at 100%
and at both ends of the range, and fails on text that fits at 100% but not at the end: a label
or button narrower or shorter than its text, a field shorter than a line or narrower than its
value, a combo narrower than its text, a list row or tab shorter than its text, or a text widget
pushed past its parent's edge or past the right edge of a scroll area that does not scroll
sideways. The offscreen screen is 800 x 800, so each dialog is checked at the size a 1366 x 768
laptop gives it (1366 x 728 past the taskbar; a message box at most 886 px wide, Qt's limit
there); dialogs whose minimum is larger are listed as notes in
`test-artifacts/ui_text_size_report_<lang>.txt`. At 130% and up the Filter Gallery, Trace Image
and Strip All Metadata no longer fit that screen in German, French and Japanese, and at 150% New
Document, Layer Style, Perspective Planes and About need more than it, hence the 125% top. `PATCHY_TEXT_SIZE_SIZES` and
`PATCHY_TEXT_SIZE_MENUS=0` narrow a run; `PATCHY_TEXT_SIZE_CAPTURE=1` saves every checked state as
`ui_text_size_<size>_<lang>_<state>.png` for review.

Known limits: lists and combos that elide (Export for Web formats, Filter Gallery names, New
Document cards) elide sooner at 125%; strings the sweep found cut at 100% already (a Russian
option in Divide Scanned Photos, the Camera Raw zoom field in German) stay as they are at 100%.
