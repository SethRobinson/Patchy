# Character styles (caps, super/subscript, underline, strikethrough, OpenType features)

The Character panel's style rows (GitHub issue 346): All Caps, Small Caps, Superscript,
Subscript, Underline, Strikethrough and the OpenType Standard Ligatures, Contextual Alternates,
Discretionary Ligatures, Swash, Stylistic Alternates, Titling Alternates, Ordinals and
Fractions. Session plumbing is in [text-tool.md](text-tool.md), panel plumbing in
[text-tool-panels.md](text-tool-panels.md); the Txt2 keys in [txt2.md](txt2.md).

## Model

- Per style run: `PsdTextStyleRun::font_caps` (Photoshop `/FontCaps`: 1 Small Caps, 2 All Caps),
  `font_baseline` (`/FontBaseline`: 1 superscript, 2 subscript), `underline`, `strikethrough`,
  `ordinals`, `fractions`, `ligatures` (on by default, as in Photoshop), `contextual_alternates`,
  `discretionary_ligatures`, `swash`, `stylistic_alternates`, `titling_alternates`
  (src/psd/psd_text_runs.hpp).
- `patchy.text.runs` v9 adds column 16, a comma-separated token list (`allcaps`, `smallcaps`,
  `superscript`, `subscript`, `underline`, `strikethrough`, `ordinals`, `fractions`,
  `noligatures`, `contextualalternates`, `discretionaryligatures`, `swash`,
  `stylisticalternates`, `titlingalternates`, or `-`), plus `language:<n>` for a run's
  Photoshop hyphenation and spelling language (Txt2 numbering, kept from Photoshop files only,
  no control; [txt2.md](txt2.md#languages)), and `digits:<n>`, `diacx:<v>`, `diacy:<v>`,
  `marky:<v>` and `justalt` for Photoshop's Middle Eastern settings, kept the same way
  ([txt2.md](txt2.md#settings-patchy-keeps-and-does-not-draw)).
  The tokens are a file-format contract. The column (and the v9 header) is written only when a
  run carries a style, so other text keeps its bytes; a reader skips any `vN` header line.
- The text keeps its own case and size: All Caps never rewrites the characters, and a
  superscript run's FontSize stays the nominal size, as in Photoshop.
- In editor and render documents the styles are QTextCharFormat flags (`kTextFontCapsFormatProperty`
  and the five after it, `kTextLigaturesOffFormatProperty` and the five after it,
  ui/text_layout.hpp), like faux bold. Standard Ligatures is stored as "off".

## PSD

- TySh: `/FontCaps n`, `/FontBaseline n`, `/Underline true /YUnderline 0` (Photoshop pairs them),
  `/Strikethrough true`, `/Ligatures false`, `/DLigatures true`, each written only when it is
  not the default. The other OpenType toggles have no TySh key; Photoshop CC 2018 and 2026 keep
  them only in Txt2.
- Txt2 style sheet: `/12` caps, `/13` baseline, `/15` strikethrough (1), `/16` underline (2),
  `/18` Ligatures (true by default), `/19` Discretionary Ligatures, `/20` Contextual Alternates,
  `/23` Fractions, `/24` Ordinals, `/25` Swash, `/26` Titling, `/28` Stylistic Alternates
  (Photoshop 2026 COM captures, Action Manager `textStyle` keys `ligature`, `altligature`,
  `contextualLigatures`, `swash`, `titling`, `stylisticAlternates`). The reader takes the
  ligature pair from the TySh and `/20`, `/23` to `/26` and `/28` from the layer's Txt2 object
  (`TextEngineBlock::object_style_features`), splitting TySh runs where they change.
- The document ratios Patchy writes (`/SuperscriptSize .583 /SuperscriptPosition .333
  /SubscriptSize .583 /SubscriptPosition .333 /SmallCapSize .7`) are Photoshop's defaults; the
  renderer uses the same constants (`kTextScriptSizeFraction` & co.), never a file's values.

## Rendering (Photoshop 2026 COM captures, five faces at 100 px, 18 faces at 200 px)

`apply_text_character_styles_to_document` (text_document.cpp) prepares the render document
before faux bold and layout; underline and strikethrough are drawn by the plan.

- **Superscript / subscript:** glyphs at 58.3% of the pixel size, baseline moved 33.3% of the
  run's em (FontSize x vertical scale) up or down; the shift adds to any baseline shift. Tracking
  scales with the drawn size ("script" at tracking 100 is 11 px narrower in the issue's sample than
  at full size). Leading still uses the nominal size. Lines holding such a run draw glyph run by
  glyph run with the exact engine-pixel shift: `QTextLine::draw` reads Qt's baseline offset, a
  percentage of the font height, and landed a subscript a pixel high. Horizontal text only.
- **All Caps:** `QFont::AllUppercase`.
- **Small Caps:** the face's own `smcp` glyphs when it has them (Arial, Times New Roman, Calibri:
  Arial's small capitals stand 0.56 em, not the 0.50 em a 70% capital would), else Qt's synthesized
  small caps, 70% (Photoshop's SmallCapSize). Myriad Pro has none and Photoshop synthesizes it.
- **Ordinals / Fractions:** the face's `ordn` / `frac`; a face without the feature leaves the text
  unchanged (Arial has `frac` but no `ordn`).
- **The other OpenType toggles:** `liga` (turned off), `calt`, `dlig`, `swsh`, `salt`, `titl`,
  each only where it changes the face's glyphs, like ordinals. **Contextual Alternates are off by
  default**, as in Photoshop (its default sheets hold `/20 false`; Segoe Script at 72 px draws
  22 px wider without them, matching HarfBuzz's calt-off advance), while HarfBuzz turns `calt` on:
  `build_text_render_document` sets `calt` 0 on the document's default font, which reaches every
  run like `vert`. Arial, Calibri, Times New Roman and Segoe UI draw Latin text the same either
  way; connecting script faces change. No face shipped with Windows has `titl`. Photoshop files
  can turn it on per run: every run of `restaurant-menu-inside.psd` has `/20 true`, read as
  `contextualalternates`, so those runs keep drawing with `calt`.
- **OpenType features per run.** QTextCharFormat cannot carry font features in Qt 6.8, so a
  fragment whose features change its glyphs gets `kTextRenderFeaturesFormatProperty` and extra
  letter spacing equal to (featured advance - plain advance) / characters, so the layout reserves
  the featured width; the draw path (`fragment_glyph_runs`, text_render.cpp) replaces Qt's glyphs
  with the fragment's own featured shaping, anchored at Qt's first glyph; the property lists `tag`
  (on) and `-tag` (off) entries, led by the document font's defaults (`-calt`), which a format
  cannot carry. Lines holding such a
  fragment draw glyph run by glyph run even in Qt-natural layout (`format_needs_glyph_run_draw`). Caret stops inside the fragment
  are spread evenly. Shaping is per fragment, so a feature does not see context across a run
  boundary.
- **Underline:** centred halfway between the face's underline position (post table; a CFF face
  uses its CFF value, post minus half the thickness) and its lowest point (head yMin), with the
  post table's thickness (`text_decoration_metrics`). Arial at 100 px: 18.0 to 25.25 px below the
  baseline; Courier New sits far lower and Photoshop agrees. Fits all 18 captured faces within
  0.3 px at 200 px.
- **Strikethrough:** centred at half the x-height (OS/2 sxHeight), same thickness as the underline.
- Bars run from the segment's first to last caret stop (tracking after the last letter is
  underlined, as Photoshop does), follow the run's own baseline and font (a superscript's bar
  rides with it), skip trailing spaces where a box line wraps, merge touching same-colour
  segments, and are drawn after the glyphs, upright under faux italic. Point text grows its raster
  to hold them; box text grows only its bottom. Qt-natural point text with a bar or a feature is
  drawn through `natural_line_items` instead of `drawContents`.

## Panel and scripting

- `textCharacterStyleRow`, at the head of the dialog with no label: two centred rows of eight
  checkable `NoFocus` buttons, split in fours by an 8 px gap, so they never widen the fields
  (philipzae's layout, issue 346). Row 1: Faux Bold, Faux Italic (`textCharacterFauxBold`,
  `textCharacterFauxItalic`, icons 8 and 9), Underline, Strikethrough | All Caps, Small Caps,
  Superscript, Subscript. Row 2: Standard Ligatures, Contextual Alternates, Discretionary
  Ligatures, Swash | Stylistic Alternates, Titling Alternates, Ordinals, Fractions
  (`textCharacter...Button`, icons `text_character_style_icon` 0 to 7 and 10 to 15, Patchy's
  own letter marks). Standard Ligatures shows checked on new text. The caps pair and the
  baseline pair exclude each other. Each
  toggle is one Type step on the selection, the whole object with a bare caret, or every selected
  text layer. A selection's toggle never reaches the runs outside it, on or off, in the runs or
  the pixels (issue 466; `ui_text_character_styles_apply_only_to_the_selection` covers all but the
  first six toggles of row 2).
  Nor does the rest of the line move: a re-edit keeps the session-start pen and first baseline
  ("The text on screen is never missing" in [text-tool.md](text-tool.md)), so a superscript or faux bold stroke
  that raises one word's ink leaves the other word on its pixels
  (`ui_text_style_edits_keep_the_point_text_line_in_place`).
- Scripting: text runs take `caps` (`"all"`, `"small"`, `"none"`), `script` (`"superscript"`,
  `"subscript"`, `"none"`), `underline`, `strikethrough`, `ordinals`, `fractions`, `ligatures`,
  `contextualAlternates`, `discretionaryLigatures`, `swash`, `stylisticAlternates`,
  `titlingAlternates`; `textRuns` reads them back ([scripting-api-changes.md](scripting-api-changes.md)).

## Known gaps

Vertical text takes the caps only (no bars, no shifted baselines, no features). OpenType
features do not shape across run boundaries. A superscript's fractional pixel size rounds to a
whole pixel. Turning faux bold off on one word re-antialiases a faux bold neighbour by a few alpha
levels at the same transform and bounds (the draw, not the placement).

## Verification

Photoshop 2026 opened a Patchy-saved file (the re-rendered sample plus a Patchy-born layer with
all eight styles) with no dialog, every type layer `textWarningLevel 0` and not old text, and its
Action Manager `textStyleRange` read back each range's `fontCaps`, `baseline`, `underline`
(`underlineOnLeftInVertical`), `strikethrough` (`xHeightStrikethroughOn`), `ordinals` and
`fractions` as written. Re-rendered in Patchy, the issue's sample matches Photoshop's composite
within a pixel per line (bars within 0.7 px).

## Tests

Core: `psd_text_character_styles_round_trip_native_text`,
`psd_text_character_style_tokens_parse_and_serialize`,
`psd_text_character_styles_import_photoshop_sample_if_available` (philipzae's
`Character - Styling and Extras.psd` in local-test-fixtures/psd),
`psd_text_opentype_features_import_photoshop_captures_if_available` (local-test-fixtures/psd/
opentype-features, Photoshop 2026 captures of each toggle). UI
(tests/ui/text_character_styles_tests.cpp): the Photoshop-calibrated bar, script and caps renders
in both layout modes, ordinals and fractions, the other OpenType toggles and Segoe Script's
calt-off and calt-on widths against Photoshop (`ui_text_opentype_features_use_the_font_features_if_available`),
the two centred rows (`ui_text_character_style_rows_are_centred`), the panel toggles, ten of the sixteen toggles on a partial selection
(`ui_text_character_styles_apply_only_to_the_selection`), and the sample re-render.
