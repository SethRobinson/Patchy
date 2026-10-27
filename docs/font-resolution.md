# Text font resolution

The text engine's font lookup and the PSD reader's naming rules. The session machinery and the Character panel are in [text-tool.md](text-tool.md); the Photoshop layout model in [text-render-calibration.md](text-render-calibration.md).

- **Authored area frames use PostScript face names in both native font lists.** On
  platforms without DirectWrite, the area writer asks the installed Qt resolver for
  the rendered face's OpenType name id 6. `Arial` is a display family; Photoshop
  requires `ArialMT` to recompose it without a missing-font prompt. The resolver is
  shared by `TySh` and `Txt2`; ordinary point/box serialization keeps its established
  output. Test: `ui_text_area_psd_uses_postscript_font_names` covers all four flag faces.

- **Characters the run's face cannot draw move to the face that draws them** on every edit
  (`substitute_uncovered_characters_in_editor`, the textChanged hook): kana typed into Arial
  commit as their own run in the Japanese family `QRawFont::fromFont(font, system)` resolves.
  Only writing systems a registered face covers are probed (the empty-family crash,
  docs/testing.md).

- **A family that resolves but covers none of the layer's characters counts as MISSING.** The
  bundled Noto Naskh Arabic has no Latin glyphs, so an installed-only check rendered a Latin
  layer in the fallback with no warning.
  `text_family_draws_any_of` probes per writing system with `QRawFont::fromFont(font, system)`
  and requires the face that comes back to BE the requested family (asking without the writing
  system resolves through the default script and reports full coverage). "Any", not "all": one
  exotic glyph missing is ordinary per-glyph fallback. The plain probe asks for a Regular face;
  a family with none (only Noto Naskh Arabic Bold registered) loses that request to another
  family covering the script, so before calling a family glyphless the check asks every style
  `QFontDatabase::styles` lists for it. `missing_text_families_for_layer`
  (main_window_shared.hpp) is the shared entry point; the layer panel draws a warning triangle on
  the "T" tile and names the font in the thumbnail tooltip.
- **Messages name the cause.** `text_font_problems_for_layer` splits the same families into
  `not_installed` and `no_glyphs`. The thumbnail tooltip, the Missing Font dialog, the
  `addTextLayer` console warning and the PDF export note each say "has no glyphs for this text"
  for an installed font, never "missing" or "not available" (that wording sends users looking
  for a font file that is already registered). Tests:
  `ui_text_layer_font_without_glyph_coverage_counts_as_missing`,
  `ui_script_text_font_without_glyphs_warns_with_the_real_cause`. The script warning comes from
  `ScriptEngineHost::edit_text_layer_session`, so every text setter reports it, including a
  missing family the session substituted on the way in (the problems are read before the
  session opens; `ui_script_text_setters_warn_about_fonts`).
  Warning comparisons use the same compact family keys as font lookup, so changing
  `LiberationSans` to its canonical `Liberation Sans` spelling is not a substitution.
- `render_text_font_for_display_family` resolves a display name first as a family, then as
  family + style ("Arial Black" -> "Arial"/"Black"). If BOTH fail on Windows,
  `try_register_missing_system_font_family` registers installed fonts from the machine and
  per-user CurrentVersion\Fonts registry keys and retries. Desktop: the entries whose display
  name starts with the family (Qt's Windows database can miss registered fonts: Arial Narrow,
  on disk and registered, fell to Tahoma). `--headless` (offscreen sees no system fonts): the
  first miss loads EVERY installed font once, so requests resolve by real family and style
  names; display names are full face names ("Futura Extra Black BT"), so prefix-matching a
  family against them is guesswork. `append_missing_text_family` asks the same rescue before
  calling a family missing. Attempted families are cached per run; application fonts are never
  removed (removeApplicationFont can crash live font users).
- **A name the database lists neither as a family nor as family + face is asked of the
  platform** (`platform_installed_family_style_match`, behind `available_text_family_style_match`):
  a full name ("Futura Extra Black BT", also what older documents stored), a PostScript name, or
  DirectWrite's family for a legacy face. On Windows `psd::installed_font_for_name` answers in
  the GDI family + subfamily the database lists ("Futura XBlk BT" + "Extra Black"), mapped back
  onto the database; a face it lacks falls to the flag face. Cached per name, cleared on
  fontDatabaseChanged; nothing answers off Windows. The writer runs the same lookup before its
  prefix split. Tests: `ui_script_text_full_face_name_resolves_like_its_family`,
  `psd_text_heavy_legacy_face_keeps_the_gdi_family_if_available` (skip unless Futura Extra
  Black BT is installed and copied to `local-test-fixtures/fonts/FUTURAXK.TTF`).
- **A name nothing above answers is looked for inside the registered faces' own name tables**
  (`font_face_for_name_table_name`, `src/ui/font_face_name_index.{hpp,cpp}`, the last step of
  `platform_installed_family_style_match`). Platform databases name a face by different
  records of the same font: CoreText reads the Macintosh records, so Bitstream's FUTURABC.TTF
  is family "Futura" + style "Bold" on macOS, while DirectWrite, GDI and FreeType read the
  Windows records and list "Futura BdCn BT" + "Bold", which is also what Photoshop stores. The
  index opens each database face once (`QRawFont::fontTable("name")`, faces whose font
  resolves to another family are skipped) and maps its PostScript name (id 6), full name (id 4),
  Windows family (id 1) and typographic family (id 16), compared as compact keys, to the
  database family + style whose QFont loads it. A PostScript or full name names one face; a
  family name picks the plainest subfamily (Regular before Bold before Bold Italic), and the
  caller's bold/italic flags cannot then reach a sibling face (known gap). Families whose
  listed name shares a prefix with the request are opened first; off Windows a miss then opens
  every family once (Windows skips that: DirectWrite has already searched the system
  collection and Qt's Windows database lists application fonts by GDI name). Cached until
  `fontDatabaseChanged`. The PSD reader's resolver (`font_database_resolved_photoshop_font`)
  passes `consult_name_tables = false`, so imported metadata keeps the same family string on
  every platform and only rendering, the missing-font check and the style picker learn the
  platform's name for the face. Tests: `ui_text_face_name_table_parser_prefers_windows_records`,
  `ui_text_name_table_names_resolve_to_the_registered_face`, and with the local fixtures
  `ui_title02_tracked_legacy_text_caret_matches_glyphs_if_available` on macOS.
- **Fonts Patchy registers itself are registered under their Windows names on macOS**
  (`add_application_font_by_windows_names`, same module; used by the user-fonts store and the
  fixture-registering tests). Qt keeps ONE style per family + style name: the last face
  registered as "Futura" + "Bold" takes the slot. CoreText lists the Bitstream face under
  Apple's family, so a user-font FUTURABC.TTF holds the slot only until CoreText's lazy alias
  population (the first request for any missing family in the process) re-registers every
  system face and Apple's Futura Bold takes it back; the index then finds no face carrying
  "Futura BdCn BT" and the layer falls to the substitute. `windows_named_font_data` copies the font
  with its Macintosh `name` records removed when they name a different family than the Windows
  records (glyphs and every other table byte-identical, directory checksum and `head`
  adjustment refreshed), and the copy registers from memory, so CoreText derives
  "Futura BdCn BT" + "Bold" and the face never shares a slot. Collections (`ttcf`) and fonts
  whose names agree register from the file unchanged, on every platform. A system-installed
  (Font Book) copy is outside Patchy's control and still relies on the index; in CoreText's
  enumeration order the Bitstream face happens to win the slot. Test:
  `ui_text_windows_named_font_data_drops_macintosh_records_if_available`.
- **Every run carries its exact size through an inline session** (`kTextExactSizeFormatProperty`,
  editor units, set by the runs applier, the new-session typing format, the size spin and the
  script path). The editor font is whole editor pixels; recovering the size as round(px / zoom)
  turned an untouched 60 px layer into 58 px at a 15% zoom. A whole-pixel exact size does not opt
  the runs into the Photoshop-layout columns. Test: `ui_script_text_size_survives_low_zoom_reedit`.
- On wasm, `available_text_family_match` also resolves common system families through the bundled
  metric-compatible alias table, and every text render appends the bundled CJK families in the UI
  language's order (`wasm_cjk_fallback_families`). See [fonts.md](fonts.md).
- **Font names compare with every non-ASCII character kept** (`photoshop_font_names_match`, the
  reader's, writer's and registry matcher): ASCII case, spaces and punctuation are ignored, and a
  name with nothing left to compare matches nothing. Dropping non-ASCII bytes compacted every
  all-CJK name to the empty key, so the first such font answered for all of them (issue 413:
  DingMao came back as KeYan). Test: `psd_text_font_names_keep_non_ascii_characters`,
  `psd_text_engine_data_keeps_an_uninstalled_cjk_font_name`.
- **Qt's Windows database lists fonts under the user's locale**: a Chinese Windows shows DingMao.ttf
  only as its Chinese typographic family, which no en-us string carries. After the en-us lookups,
  `directwrite_font_for_localized_name` matches full names and family names (DirectWrite, WIN32
  and typographic) in every locale: the writer turns such a family into its PostScript name, the
  reader resolves it (after the registry) to the en-us names it always stores, and
  `platform_installed_family_style_match` maps a stored en-us family onto whichever localized
  name the database lists (`installed_font_family_names`).
- **Only Regular (400) and Bold (700) survive being flattened into a family plus a bold flag.**
  The PSD reader keeps the real face for every other weight (`psd_text_read.cpp`): DirectWrite
  on Windows, the font database elsewhere (`src/ui/psd_font_resolver.hpp`), the suffix
  heuristic last (wasm stays heuristic-only so alias families never reach imported
  metadata). Flattening Demi/Semi (600) onto the family's BOLD face renders heavier
  and taller than Photoshop (ITC Lubalin Graph Demi: 995x868 vs Photoshop's 982x826 on the
  entry_poster.psd body copy; the real face matches the width exactly). Gated on the face NAME,
  not the raw weight: a face whose name the flags can express is never baked into the family,
  whatever weight it declares (Bookman Old Style ships its whole family at weight 500, so
  "BookmanOldStyle-Italic" must resolve to the plain family plus the italic flag).
  "Plain" and "Roman" (older fonts' upright regular face) are flag-expressible too; the reader's
  `face_name_is_flag_expressible` and text_font_service.cpp's `text_style_is_flag_expressible` must
  change together. Three further rules make the kept faces work:
  - The kept name is `family + " " + faceName`, what Qt calls such a face when it splits it into
    its own family ("ITC Lubalin Graph Demi"). The DirectWrite FULL_NAME can be a PostScript-style
    name ("LubalinGraphITCbyBT-Demi") that matches nothing in the database and falls to a substitute.
  - **The stored family must be a name Qt's Windows database lists, which is the GDI name**
    (DirectWrite's WIN32_FAMILY_NAMES / WIN32_SUBFAMILY_NAMES), not the weight-stretch-style
    family and face DirectWrite derives. When the WIN32 family differs from the name built above,
    the reader stores it, keeps the WIN32 subfamily as the style only when the flags cannot
    express it, and reads bold/italic from that subfamily's words. Issue 16: Balmoral LET (one
    "Plain" face, OS/2 weight class 5) is DirectWrite family "Balmoral LET Plain" plus a
    synthesized "Medium" face; Franklin Gothic Medium is DirectWrite "Franklin Gothic" +
    "Medium" (weight 400), a nonexistent GDI family. Simulated DirectWrite faces are
    skipped. The writer looks a GDI family DirectWrite lacks up by
    the same WIN32 strings before its prefix split, so the PostScript name round-trips.
  - The bold flag is NOT set alongside it: the name already carries the weight, and Qt would
    synthesise bold on top of the face, the same "heavier and wider" bug by another route.
    Black/Heavy (>= 800) is the deliberate exception, keeping the flag as an uninstalled-face
    fallback; its calibration is pinned by the SNES box-blurb probe. Heavy faces take the same
    `family + face` path and WIN32 normalization as every other face (Black/Heavy subfamily
    words set the flag); returning DirectWrite's FULL_NAME early stored "Futura Extra Black BT",
    a family no database lists, so an unchanged edit re-rendered in a substitute.

Font lookup, style matching, fallback coverage and system-font registration live in `ui/text_font_service.{hpp,cpp}`. Its API has no MainWindow dependency; the PSD resolver, font picker and text document builder share it.

## The style picker (faces bold + italic cannot name)

The options bar's style picker (`textStyleCombo`) is the ONLY face control, like Photoshop (no B/I buttons).
`PsdTextStyleRun::style` / runs v5 column 12 persist a face the flags cannot express;
`render_text_font_for_display_family` takes it as its last argument, applying `setStyleName`
when the family offers it and falling back to the flags otherwise (a stray style must
not render nothing).

- **A style is recorded only when the flags cannot express it.** The DirectWrite resolver and the
  picker both drop Regular/Bold/Italic/Bold Italic/Oblique variants
  (`text_style_is_flag_expressible` mirrors the reader's list), so ordinary imports and picks
  stay on runs v1-v4 and the byte-stability canaries do not move. The picker derives those four
  rows from `bold`/`italic` and previews each row in its own face (per-item `Qt::FontRole`).
- **A face name's flags union the database's answer; the database never vetoes the name.**
  (`text_style_flags_for_style`): `QFontDatabase::bold` calls only weight >= 700 bold;
  Bookman Old Style declares Bold at 600, which collapsed a "Bold Italic" pick to plain italic;
  the database still adds axes for localized face names.
- **Ctrl+B / Ctrl+I toggle the face axis during a session** (`toggle_text_bold_face` /
  `toggle_text_italic_face`): the real face when `family_offers_face_axis` says the family ships
  one, FAUX bold/italic otherwise (Ctrl+I on Century Gothic, which has no italic, slants). The axis state folds the faux flag in, so a second press always turns the
  axis off, and the toggle clears any recorded exotic style (Ctrl+B on a Black run selects the
  Bold face, as Photoshop does). `family_offers_face_axis` is asked ONE AXIS AT A TIME and
  `real_face_style_name` masks the flags the same way, so bold + italic on Century Gothic renders
  the real Bold face plus a synthetic slant.
- **Never ask `QFontDatabase::styles()` with an unresolved display family.** A face-baked name
  ("ITC Lubalin Graph Demi") lists its faces only under the SPLIT base family; the unsplit name
  answers nothing, which emptied the picker and diverted Ctrl+B/Ctrl+I to faux.
  `available_text_family_styles` resolves through `text_style_query_family`, probes the four flag
  combinations with `QRawFont` (forcing Qt's lazy per-family population), re-queries for faces
  only the database knows (Light, Semibold, Black), orders the four standard faces first, and
  caches per family with `fontDatabaseChanged` invalidation. A family that is not installed still
  offers the four standard faces so the toggles never synthesize on top of a substituted face.
- New text seeds from the picker (`add_text_at`).
- On export, `photoshop_font_name_for_run` resolves the recorded style (or the face split off a
  compound display family) to that face's PostScript name: "Arial" + "Black" writes `Arial-Black`
  instead of flattening onto `Arial-BoldMT`. Style-empty runs keep the weight-based lookup
  byte-identical; a split whose remainder names no real face exports verbatim like any unknown
  family.
- `textStyleCombo` needs the `is_text_option_widget` exemption or focusing it auto-commits.

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
  re-renders in the substitute with the scale folded into the point size, unasked, as before.
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
  (the compact key), with a `@dlg_divider` line between rows. Rows start unchecked, Photoshop's
  "Don't Resolve": the file keeps the name, so a machine with the font still shows the original.
  The picker starts on the face drawing the text now. The accept button is always enabled and
  reads "Don't Resolve" until a row is ticked (then "Resolve Fonts"); either way, and on Cancel,
  the "Don't show" box is saved. Resolve runs one hidden session per affected layer with `text_font_replacements_` set:
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
metric-compatible), plus a class column (serif, sans, mono, script, display) for the class-based
default that is not built yet. Names and project links only: no font data, and every pair comes
from the substitute's own documentation, never another program's substitution list
([fonts.md](fonts.md), licensing rules).

- **Keys** are `font_substitute_key`: ASCII case and punctuation ignored, non-ASCII kept (the
  `photoshop_font_names_match` rule). An entry matches by family or alias (PostScript stems such as
  `ArialMT`, `TimesNewRomanPSMT`). A miss is retried with trailing face words dropped
  ("Myriad Pro Semibold", "Arial-BoldMT"); the dropped words come back as the lookup's style.
- **Ranking** (`rank_font_substitutes`): metric before look-alike, table order within each, never
  the requested font itself. `installed_text_font_substitutes` (text_font_service) keeps the
  installed candidates that draw the text. A face the flags cannot express ("Semibold") is asked of
  the substitute as `family + " " + face`; when the substitute lacks it the match is demoted to
  look-alike.
- **Uses.** `substituted_text_family` asks the table before Qt's fallback, so an edit session on a
  missing font (both the base family and `substitute_missing_document_font_families`) moves onto
  the table's substitute, and the raster-preview prompt's informative text names it ("Helvetica
  will be replaced with Arial."). Resolve Missing Fonts starts each listed font's picker on the
  first substitute with a "Suggested: X (same character widths)" or "(similar design)" line; rows
  stay unticked. The stored name changes only on a commit or a resolved row, as before, so the PSD
  still names the original font for Photoshop.
- The wasm alias table (`kWasmFamilyAliases`, [fonts.md](fonts.md)) stays separate: its targets
  are the bundled fonts.
- Tests: `font_substitutes_` (core), `ui_missing_fonts_dialog_offers_the_installed_substitute_first`,
  `ui_missing_font_edit_session_uses_the_table_substitute` (skip where Helvetica is installed or
  none of its substitutes is registered).

## Photoshop 5.x `tySh` faces

A PS 5 type record names each face three ways: PostScript name, GDI family and style string
([psd-legacy-text.md](psd-legacy-text.md)). The PostScript name goes through
`resolve_photoshop_font_name` like a modern TySh font; when only the suffix heuristic answers
(neither DirectWrite nor the registry knows the name), the record's own family and style strings
are used instead, with bold and italic parsed from the style string, because those are the names
Windows lists the face under and the heuristic's humanized guess is not.

Those GDI names are Windows names: CoreText lists `FUTURABC.TTF` as "Futura" + "Bold", so on
macOS the GDI family "Futura BdCn BT" is missing and a session would substitute a regular-width
face. The name-table index above resolves the record's family (and its PostScript name) to the
face Qt really holds, so the layer renders with the condensed face on every platform; the reader
still stores the GDI names. Pinned with `local-test-fixtures/fonts/FUTURABC.TTF` by
`ui_title02_tracked_legacy_text_caret_matches_glyphs_if_available` (Title02.psd,
`FuturaBT-BoldCondensed` / `Futura BdCn BT`).
