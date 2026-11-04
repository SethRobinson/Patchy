# Missing fonts and substitutes

Split from [font-resolution.md](font-resolution.md): what happens when a document names a font this machine cannot draw (the substitution prompt, the layer badge, the Free Transform question, Type > Resolve Missing Fonts), the built-in substitute table, and same-type stand-ins for fonts the table does not list.

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
- **New type takes the family the bar shows** (`text_options_bar_family`, issue 180). Mirroring a
  layer set in a missing family leaves the combo's `currentFont()` on that name while it shows
  another row; with no text layer to mirror, `sync_text_options_from_active_layer` puts
  `currentFont()` back on the shown row, and `add_text_at` seeds new sessions from that row.
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
  (the compact key), with a `@dlg_divider` line between rows. Every row starts ticked (Seth,
  issue 463 follow-up) with its picker on the suggested stand-in ("Substitute table" below), else
  the face drawing the text now. "Resolve Fonts" is enabled while at least one row is ticked;
  there is no "Don't Resolve" button: Cancel or Escape keeps every font (the file keeps the
  names, so a machine with the fonts shows the originals) and still opens the document. On
  Resolve and on Cancel the "Don't show" box is saved. Resolve runs one hidden session per affected layer with `text_font_replacements_` set:
  only the mapped families change (base family and runs), other missing families keep their
  names, and all commits share one "Resolve Missing Fonts" undo step. A commit regenerates the
  layer, so its TySh and the Txt2 object name the replacement on the next save. Locked layers and
  protected text geometry are skipped and counted in the status message.
- Every interactive open (`open_document_path`, never unattended automation) shows the dialog while
  `imports/showMissingFontsDialog` (default true) allows. Its "Don't show when opening documents"
  box (its label names Preferences > Application as the way back) and Preferences > Application
  set that key. The UI test main sets it false; tests that need
  the dialog turn it on. Tests: the `missing_font` filter (tests/ui/missing_fonts_tests.cpp).

### Substitute table

`src/core/font_substitutes.{hpp,cpp}` (issue 463, Qt-free) lists well-known fonts with the
fonts that stand in for them: each candidate is `metric` (the substitute's own project states the
same character widths: Arial to Liberation Sans or Arimo, Calibri to Carlito, Cambria to Caladea)
or `lookalike` (similar design, text can reflow: Myriad Pro to Source Sans 3, which is NOT
metric-compatible), plus a font-type column (serif, sans, mono, script, display; "Font types"
below). Names and project links only: no font data, and every pair comes
from the substitute's own documentation, never another program's substitution list
([fonts.md](fonts.md), licensing rules).

- **Keys** are `font_substitute_key`: ASCII case and punctuation ignored, non-ASCII kept (the
  `photoshop_font_names_match` rule). An entry matches by family or alias (PostScript stems such as
  `ArialMT`, `TimesNewRomanPSMT`). A miss is retried with trailing face words dropped
  ("Myriad Pro Semibold", "Arial-BoldMT"); the dropped words come back as the lookup's style.
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
- Tests: `font_substitutes_` (core), `ui_missing_fonts_dialog_offers_the_installed_substitute_first`,
  `ui_missing_font_edit_session_uses_the_table_substitute`,
  `ui_missing_font_render_uses_the_table_substitute_and_keeps_the_name` (skip where Helvetica is installed or
  none of its substitutes is registered).

### Font types

A missing font the table does not list still gets a stand-in of its own type instead of Qt's
fallback (Tahoma on Windows): Resolve Missing Fonts says "Suggested: X (also a serif font)" (or
sans-serif, monospaced), and the edit and render paths use it like a table pair.

- **The type** (`text_font_class`): the table's column; then, on Linux, the generic family the
  user's fontconfig rules give the name (45-latin.conf and its kind; `fontconfig_font_class`, read
  from the pattern after `FcConfigSubstitute`); then the name's own words
  (`classify_font_name_words`: mono words before sans words before serif words, so "Microsoft Sans
  Serif" is sans and "Noto Sans Mono" mono; whole words only, split at spaces, hyphens, underscores
  and lower-to-upper changes). fontconfig's sans-serif alone is NOT a type: 49-sansserif.conf
  appends it to every family no rule names. Script and display types get no default.
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
  `ui_missing_font_same_type_default_reaches_edit_and_render`,
  `ui_missing_font_type_psds_suggest_a_same_type_font`, and
  `ui_fontconfig_font_types_follow_the_system_rules` (Linux; skips elsewhere).
