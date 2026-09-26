# Text font resolution

The text engine's font lookup and the PSD reader's naming rules. The session machinery and the Character panel are in [text-tool.md](text-tool.md); the Photoshop layout model in [text-render-calibration.md](text-render-calibration.md).

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
  exotic glyph missing is ordinary per-glyph fallback. `missing_text_families_for_layer`
  (main_window_shared.hpp) is the shared entry point; the layer panel draws a warning triangle on
  the "T" tile and names the font in the thumbnail tooltip.
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
- **Every run carries its exact size through an inline session** (`kTextExactSizeFormatProperty`,
  editor units, set by the runs applier, the new-session typing format, the size spin and the
  script path). The editor font is whole editor pixels; recovering the size as round(px / zoom)
  turned an untouched 60 px layer into 58 px at a 15% zoom. A whole-pixel exact size does not opt
  the runs into the Photoshop-layout columns. Test: `ui_script_text_size_survives_low_zoom_reedit`.
- On wasm, `available_text_family_match` also resolves common system families through the bundled
  metric-compatible alias table, and every text render appends a Noto Sans JP fallback family. See
  [fonts.md](fonts.md).
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
  `face_name_is_flag_expressible` and main_window.cpp's `text_style_is_flag_expressible` must
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
