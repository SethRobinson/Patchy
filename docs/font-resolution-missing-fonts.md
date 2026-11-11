# Missing fonts and substitutes

Split from [font-resolution.md](font-resolution.md): what happens when a document names a font this machine cannot draw (the substitution prompt, the layer badge, the Free Transform question, Type > Resolve Missing Fonts and the replacements it remembers), the built-in substitute table, and same-type stand-ins for fonts the table does not list.

## Missing fonts

- **The substitution prompt** (`confirm_psd_raster_preview_font_substitution`) opens before a
  session on a `psd_raster_preview` layer whose fonts `text_font_problems_for_layer` flags,
  visible or hidden (GitHub issue 177: the hidden path substituted silently). Continue moves the
  session onto the face that draws (`substituted_text_family`,
  `substitute_missing_document_font_families`). CLI automation and Resolve Missing Fonts skip it.
- **The badge follows an open session** (issue 178). `live_text_session_layer_id` names the layer
  a visible session edits; its thumbnail drops the badge and its tooltip stops naming the font
  while the session lasts, because the session already draws installed faces. Cancel brings both
  back (the layer still names the missing font). The thumbnail cache keys on the badge state as
  well as the render revision.
- **The thumbnail tooltip names the font and nothing else** (issue 184): "Font is missing on this
  computer: <name>" or "No glyphs for this text in: <name>". It never claims a substitution: until
  an edit or a transform the layer shows the pixels it was saved with.
- **Free Transform asks first about imported type with a missing font** (issue 185, Photoshop's
  "the existing pixel data will be used"). `CanvasWidget::commit_free_transform` hands every target
  to the missing-font confirm callback before writing anything; Cancel cancels the session
  untouched. It asks only about `text_transform_keeps_missing_font_pixels` layers (PSD-transform
  type, not area text): `rerender_text_layer_through_stored_transform` keeps their resampled
  pixels and font name. Patchy-made type with a substituted font (every Arial layer on Linux)
  re-renders in the substitute with the scale folded into the point size, unasked.
  Image Size and scripted transforms do not ask, and neither does any automation (CLI,
  `--headless`, the connector, a running script outside an artist's manual-edit pause): those go
  ahead as if Continue were pressed. The settings key `text/warnMissingFontsOnTransform` (default true, no UI) turns the question off;
  the UI test main does, since fixtures name fonts a test host may lack.
- **The bar names a missing font** (issue 652). A missing family cannot be a row of the
  font combo, so mirroring a layer set in one (`show_text_family_in_options_bar`, the one place
  MainWindow sets the combo's family) parks the combo on an installed row while `currentFont()`
  keeps the missing name, and `FontPickerCombo::set_missing_family` makes the closed control
  name it: italic text, the missing-font warning badge as a leading line-edit action
  (`textFontMissingBadge`, `@icon_warning` color, rebuilt on theme and language changes), and the
  tooltip "Font is missing on this computer: <name>". Any pick (popup, a name typed and
  entered, any edit of the text), a layer in an installed font, no text layer and a session that
  moved onto a substitute all clear it. Test: `ui_options_bar_names_a_missing_font_family`.
- **A missing font's face goes in the style field** (issue 669, Photoshop's "[MuseoSansRounded]"
  + "[500]"). `split_font_family_and_style` (core/font_substitutes) splits the name: a
  PostScript-style name without spaces at its first hyphen, any other name by dropping trailing
  face words (weights Thin to Black with a Semi/Demi/Extra/Ultra prefix, numeric weights 100 to
  1000, Regular/Normal/Plain, Italic, Oblique; never widths or "Roman", and one family word
  always stays). The PSD reader humanizes an uninstalled `MuseoSansRounded-500` to "Museo Sans
  Rounded 500", so the family field shows "Museo Sans Rounded" (tooltip and layer keep the
  whole name) and `refresh_text_style_combo` shows "500" as the style combo's placeholder at
  index -1, italic, with the same tooltip; with no face in the name (or a recorded style no row
  matches) the combo keeps its row and is only marked. `current_text_style_name()` reads empty
  then, so new type starts Regular. Display only: the stored name and the PSD are unchanged; an
  installed family is never split. `FontPickerCombo::set_missing_family` sets that shorter text
  with the combo's signals blocked: QFontComboBox reads an edit-text change as a pick and would
  apply it to the layer. Test: `ui_options_bar_splits_a_missing_font_into_family_and_style`, core
  `font_substitutes_split_font_names_into_family_and_style`.
- **New type takes the installed row, never the missing name** (`text_options_bar_family`, issue
  180). It reads the row the combo is parked on (`FontPickerCombo::row_family`), not the line
  edit, which shows the missing name; with no text layer to mirror,
  `sync_text_options_from_active_layer` puts `currentFont()` back on that row, and `add_text_at`
  seeds new sessions from it. So new type beside a selected missing-font layer starts in the
  installed family the popup also opens on.
- **Generic names resolve to the face they draw in** (`installed_text_family`,
  `default_text_family`, issue 442). Linux under a plain Qt theme (the Flatpak) reports the
  application font as "Sans Serif": Qt's fontconfig database lists that name as a family, yet it
  draws in DejaVu Sans or Noto Sans (Windows does not list it). New type seeds from the face
  `QFontInfo` resolves the name to (the options bar's starting font and every "no family named"
  fallback), and `text_family_draws_any_of` probes a listed name against that face, so a layer
  storing a listed alias is not missing. An unlisted name is never resolved to its substitute.
- **Type > Resolve Missing Fonts** (issue 169; hotkey id `type.resolve_missing_fonts`;
  `src/ui/missing_fonts_dialog.*`, `main_window_missing_fonts.cpp`). One row per family any text
  layer cannot draw, hidden and grouped layers included, deduped by `text_font_replacement_key`
  (the compact key), with a `@dlg_divider` line between rows. A row's name is one bold label: the
  family, then the style in brackets, split by `split_font_family_and_style` as the options bar
  does (issue 669: "Museo Sans Rounded (500)"; a plain literal pattern, since every catalog keeps
  ASCII brackets; display only, the whole name stays the row's key and the label's
  `patchyMissingFontFullName` property). The row list is as wide as its widest row (it never
  scrolls sideways, so a narrower one cut the pickers off). Every row starts ticked (Seth,
  issue 463 follow-up) with its picker on the font chosen before ("Remembered replacements"
  below), else the suggested stand-in ("Substitute table" below), else the face drawing the text
  now. "Resolve Fonts" is enabled while at least one row is ticked;
  there is no "Don't Resolve" button: Cancel or Escape keeps every font (the file keeps the
  names, so a machine with the fonts shows the originals) and still opens the document. On
  Resolve and on Cancel the "Don't show" box is saved. Resolve runs one hidden session per affected layer with `text_font_replacements_` set:
  only the mapped families change (base family and runs), other missing families keep their
  names, and all commits share one "Resolve Missing Fonts" undo step. A commit regenerates the
  layer, so its TySh and the Txt2 object name the replacement on the next save. Locked layers and
  protected text geometry are skipped and counted in the status message.
- **Type > Replace All Missing Fonts** (issue 686; `type.replace_all_missing_fonts`,
  `replace_all_missing_fonts`): the dialog's answer with no dialog. `missing_font_entry` builds
  each row's recommendation for both paths, a remembered replacement first (read, never
  written: only a pick in the dialog is a choice); a row whose suggestion is `fallback` (no table
  pick, no same-type default) takes `default_text_family()`, not the face Qt falls back to, and a
  `no_symbol_match` symbol font keeps its name, as its dialog row starts unticked. Then
  `resolve_missing_fonts` as above: one "Resolve Missing Fonts" step, locked layers counted.
  Nothing missing: the status bar says "This document has no missing fonts".
- **Type > Update All Text Layers** (issue 686; `type.update_all_text_layers`,
  `update_all_text_layers`): every text layer whose fonts are installed (hidden, grouped and
  artboard layers included) gets the same hidden session and a commit marked changed, so an
  imported `psd_raster_preview` layer is laid out and rendered by Patchy (`patchy_raster`) and
  the next save writes Patchy's TySh and Txt2 for it (for PSD text compatibility checks). One
  "Update All Text Layers" step; layers with a missing or glyphless font (Replace All Missing
  Fonts is for those) and locked or protected layers are skipped and counted on the status bar.
  Both passes share `commit_text_layers_in_hidden_sessions`, which defers the Layers panel,
  layer controls and options bar refreshes its commits ask for and runs each once at the end
  (`begin_deferred_panel_refreshes`/`end_deferred_panel_refreshes`, main_window.hpp). A rebuild
  per commit made the pass quadratic: each row asks for its fonts, and the replaced rows stay
  alive until the command returns (a 144-layer file took minutes). The scripted text edits
  (`edit_text_layer_session`, `rerenderText` included) defer the same way per call and retire
  the old rows. `available_text_family_match` answers from an index rebuilt only when
  `QFontDatabase::families()` changes. Pinned by `ui_update_all_text_layers_refreshes_the_layers_panel_once`
  and `ui_script_rerender_text_rebuilds_the_layers_panel_a_fixed_number_of_times_per_call`.
- Every interactive open (`open_document_path`, never unattended automation) shows the dialog while
  `imports/showMissingFontsDialog` (default true) allows. Its "Don't show when opening documents"
  box (its label names Preferences > Application as the way back) and Preferences > Application
  set that key. The UI test main sets it false; tests that need
  the dialog turn it on. Tests: the `missing_font` filter (tests/ui/missing_fonts_tests.cpp),
  `ui_update_all_text_layers_`, `ui_replace_all_missing_fonts_`.

### Remembered replacements

Resolve Missing Fonts remembers what was chosen for each missing font (issue 692), so the same
or another document naming that font starts on it. `src/ui/font_replacement_memory.{hpp,cpp}`.

- **Store.** The settings key `text/missingFontReplacements` (a persisted identifier, never
  renamed) holds compact JSON: `[{"font": <text_font_replacement_key>, "picks": [{"family",
  "count"}, ...]}, ...]`. The member names are part of the contract. Both arrays are ordered most
  recently chosen first, so a font's first pick is the last one made. At most
  `kMaxRememberedReplacementFonts` (100) fonts with `kMaxRememberedReplacementsPerFont` (6)
  families each; the entry chosen longest ago goes first. A damaged value reads as empty.
- **Recording** (`remember_font_replacements`, after Resolve Fonts only). A ticked row's pick is
  recorded when it was picked by hand (the picker's `activated`: a pick in its list or a name typed
  into it, even the font the row started on, so Wingdings chosen for Wine Wingdings counts;
  `MissingFontsDialogResult::picked_by_hand`), when it differs from the built-in recommendation the
  row would start on (`MissingFontEntry::recommended`), when the font is a `no_symbol_match`
  symbol font (its row starts unticked, so any pick is by hand), or when the font already has remembered picks (going
  back to the recommendation becomes the last pick; accepting a remembered font counts once
  more). An untouched recommendation for a font with no history records nothing, so those rows
  keep their "Suggested:" line. Cancel and unticked rows record nothing: "keep the missing font"
  is not remembered.
- **Use** (`missing_font_entry`, so the dialog and Replace All Missing Fonts agree). The row
  starts on the font chosen last that is installed and draws the row's text
  (`append_missing_text_family`); a pick that fails either test is skipped for the next most
  recent one, then for the built-in recommendation. The row then shows "Chosen before: X"
  (`missingFontChosenBefore<N>`) in place of its "Suggested:" line or its symbol-font note, and a
  symbol font's row starts ticked on it.
- **Picker sections.** Each row hands its picker two sections
  (`FontPickerCombo::set_extra_sections`, [font-picker.md](font-picker.md#extra-sections-issue-692)):
  "Chosen before", the remembered families still installed, most often chosen first and the most
  recent first among equals, then "Suggested", every installed candidate of
  `installed_text_font_substitutes` with its reason ("same character widths", "similar design",
  "also a serif font" and its sans-serif and monospaced forms) minus the fonts the first section
  lists. The full list with search, filters, Recent, Favorites and the preview stays below them
  (Seth chose that over a plain list).
- **Forgetting** (issue 692 follow-up). Each "Chosen before" row has the Recent x ("Remove from
  Chosen before"): `forget_font_replacement` drops every remembered spelling of that family for
  this missing font (a font left with no pick leaves the stored list), the row leaves the open list
  and the next popup, and the font stays in the full list. The row's "Chosen before:" line and its
  starting font change only when the dialog opens again.
- Tests: `ui_missing_fonts_dialog_remembers_the_replacement_chosen`,
  `ui_missing_fonts_dialog_skips_a_remembered_font_that_is_not_installed`,
  `ui_missing_fonts_picker_`, `ui_replace_all_missing_fonts_uses_the_remembered_replacement`,
  `ui_missing_fonts_untouched_suggestion_and_cancel_remember_nothing`,
  `ui_missing_font_replacement_memory_counts_orders_and_caps`,
  `ui_missing_fonts_dialog_remembers_a_hand_pick_of_the_suggestion`,
  `ui_missing_fonts_picker_chosen_before_x_forgets_the_pick`,
  `ui_missing_font_replacement_memory_forgets_one_pick`. A test that resolves a hand-picked
  font holds a `ReplacementMemoryReset`, or its pick starts a later test's row; the UI test main
  clears the key at startup.

### Substitute table

`src/core/font_substitutes.{hpp,cpp}` (issue 463, Qt-free) lists well-known fonts with the
fonts that stand in for them: each candidate is `metric` (the substitute's own project states the
same character widths: Arial to Liberation Sans or Arimo, Calibri to Carlito, Cambria to Caladea)
or `lookalike` (similar design, text can reflow: Myriad Pro to Source Sans 3, which is NOT
metric-compatible), plus a font-type column (serif, sans, mono, script, display, symbol; "Font
types" below). Names and project links only: no font data, and every pair comes
from the substitute's own documentation, never another program's substitution list
([fonts.md](fonts.md), licensing rules).

- **Keys** are `font_substitute_key`: ASCII case and punctuation ignored, non-ASCII kept (the
  `photoshop_font_names_match` rule). An entry matches by family or alias (PostScript stems such as
  `ArialMT`, `TimesNewRomanPSMT`). A miss is retried with trailing face words dropped
  ("Myriad Pro Semibold", "Arial-BoldMT"); the dropped words come back as the lookup's style.
- **Symbol fonts** (issue 463 follow-up 2) pair only with a font that maps the same codes to the
  same pictures. Wine installs its Wingdings, Webdings and Symbol under the Windows family names
  with PostScript names `WineWingdings`, `WineWebdings`, `WineSymbol`; a document saved with one
  names that (shown "Wine Wingdings"). Wine Wingdings and Wine Webdings to the Windows fonts are
  metric (measured from the fonts' cmap and hmtx: same width at every code Wine draws); Wine Symbol
  to Symbol, Standard Symbols PS and Standard Symbols L is a look-alike (two widths differ); Symbol
  (`SymbolMT`) to URW's Standard Symbols PS / Standard Symbols L and ITC Zapf Dingbats to D050000L /
  Dingbats are metric (URW's own statement; the Windows Symbol measures the same). Wingdings,
  Webdings, Marlett and MT Extra are listed with no candidates, only for their type: Wine's copies
  already answer to "Wingdings". Never OpenSymbol (Unicode code points, not Symbol's codes). An
  entry with no candidates is allowed only for the symbol type.
- **Ranking** (`rank_font_substitutes`): metric before look-alike, table order within each, never
  the requested font itself. `installed_text_font_substitutes` (text_font_service) keeps the
  installed candidates that draw the text, then appends one installed default of the same font
  type (`same_type` set; "Font types" below). A face the flags cannot express ("Semibold") is asked of
  the substitute as `family + " " + face`; when the substitute lacks it the match is demoted to
  look-alike.
- **Uses.** `substituted_text_family` asks the table before Qt's fallback, so an edit session on a
  missing font (both the base family and `substitute_missing_document_font_families`) moves onto
  the table's substitute, and the raster-preview prompt's informative text names it ("Helvetica
  will be replaced with Arial."). Resolve Missing Fonts starts each listed font's picker on the
  first substitute with a "Suggested: X (same character widths)" or "(similar design)" line. The stored name changes only on a commit or a resolved row, so the PSD
  still names the original font for Photoshop.
- **Rendering.** `render_text_families_for_display_family` appends the first installed substitute
  after a family that is not installed (cached per family, cleared on `fontDatabaseChanged`, locked
  for the PDF worker), so Patchy-made type in a missing font draws in it with the same line breaks
  while `QFont::family()` and the stored run still name the original.
- The wasm alias table (`kWasmFamilyAliases`, [fonts.md](fonts.md)) stays separate: its targets
  are the bundled fonts.
- Tests: `font_substitutes_` (core; symbol pairs:
  `font_substitutes_symbol_fonts_pair_only_fonts_with_the_same_codes`),
  `ui_missing_fonts_dialog_offers_the_installed_substitute_first`,
  `ui_missing_font_edit_session_uses_the_table_substitute`,
  `ui_missing_font_render_uses_the_table_substitute_and_keeps_the_name` (skip where Helvetica is installed or
  none of its substitutes is registered).

### Font types

A missing font the table does not list still gets a stand-in of its own type instead of Qt's
fallback (Tahoma on Windows): Resolve Missing Fonts says "Suggested: X (also a serif font)" (or
sans-serif, monospaced), and the edit and render paths use it like a table pair.

- **The type** (`text_font_class`): the table's column; then a symbol font's name words (before
  fontconfig, which has no symbol generic); then, on Linux, the generic family the
  user's fontconfig rules give the name (45-latin.conf and its kind; `fontconfig_font_class`, read
  from the pattern after `FcConfigSubstitute`); then the name's own words
  (`classify_font_name_words`: mono words before sans words before serif words, so "Microsoft Sans
  Serif" is sans and "Noto Sans Mono" mono; whole words only, split at spaces, hyphens, underscores
  and lower-to-upper changes). fontconfig's sans-serif alone is NOT a type: 49-sansserif.conf
  appends it to every family no rule names. Script and display types get no default.
- **Symbol type**: the table's symbol entries, or the name words "wingdings", "webdings",
  "dingbats" and "marlett" (checked before the text words). Not "symbol": "Segoe UI Symbol" and
  "Noto Sans Symbols" are Unicode text fonts. A symbol font gets no same-type default, so with no
  installed copy Resolve Missing Fonts shows "Symbol font: no installed font has the same
  symbols" (`missingFontNoSymbolMatch<N>`, `MissingFontSuggestion::no_symbol_match`), the row
  starts unticked with its picker disabled on the UI font, and Resolve Fonts leaves it alone
  unless the user ticks it and picks a font. Editing and rendering keep Qt's own fallback, as for
  any missing font the table cannot answer.
- **The default** (`same_type_text_font_substitute`): on Linux fontconfig's own order of installed
  families for the generic (`FcFontSort`, cached per type for the process), then the built-in list
  (`same_class_default_families`, one list per type mixing Windows, macOS and Linux families); the
  first installed family that draws the text and is not the requested font.
- **fontconfig is loaded with dlopen** (`src/ui/fontconfig_font_class.cpp`, Linux only,
  `PATCHY_HAVE_FONTCONFIG_QUERY`), like libheif: Qt's Linux font database already loads the same
  library, so the Flatpak, the AppImage and the native presets need no fontconfig headers and link
  nothing new. Only the fontconfig 2.0 ABI subset is mirrored.
- Tests: `font_substitutes_classify_font_types_from_name_words`,
  `font_substitutes_same_class_defaults_cover_each_platform` (core); the `missing_font_type_tests`
  group, registered LAST in tests/ui/main.cpp because it registers Georgia and Courier New, which
  earlier text tests must not see (a missing serif font would draw in them):
  `ui_missing_fonts_dialog_offers_a_same_type_default_for_unlisted_fonts`,
  `ui_missing_fonts_dialog_offers_symbol_fonts_only_a_copy` (registers Wingdings; without it the
  Wine Wingdings row is checked unresolved),
  `ui_missing_font_same_type_default_reaches_edit_and_render`,
  `ui_missing_font_type_psds_suggest_a_same_type_font`, and
  `ui_fontconfig_font_types_follow_the_system_rules` (Linux; skips elsewhere).
