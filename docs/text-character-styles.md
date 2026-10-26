# Character styles (caps, super/subscript, underline, strikethrough, ordinals, fractions)

The Character panel's style row (GitHub issue 346): All Caps, Small Caps, Superscript,
Subscript, Underline, Strikethrough and the OpenType Ordinals and Fractions. Session and
panel plumbing are in [text-tool.md](text-tool.md); the Txt2 keys in [txt2.md](txt2.md).

## Model

- Per style run: `PsdTextStyleRun::font_caps` (Photoshop `/FontCaps`: 1 Small Caps, 2 All Caps),
  `font_baseline` (`/FontBaseline`: 1 superscript, 2 subscript), `underline`, `strikethrough`,
  `ordinals`, `fractions` (src/psd/psd_text_runs.hpp).
- `patchy.text.runs` v9 adds column 16, a comma-separated token list (`allcaps`, `smallcaps`,
  `superscript`, `subscript`, `underline`, `strikethrough`, `ordinals`, `fractions`, or `-`).
  The tokens are a file-format contract. The column (and the v9 header) is written only when a
  run carries a style, so other text keeps its bytes; a reader skips any `vN` header line.
- The text keeps its own case and size: All Caps never rewrites the characters, and a
  superscript run's FontSize stays the nominal size, as in Photoshop.
- In editor and render documents the styles are QTextCharFormat flags (`kTextFontCapsFormatProperty`
  and the five after it, ui/text_layout.hpp), like faux bold.

## PSD

- TySh: `/FontCaps n`, `/FontBaseline n`, `/Underline true /YUnderline 0` (Photoshop pairs them),
  `/Strikethrough true`, each written only when set. Ordinals and Fractions have no TySh key;
  Photoshop CC 2018 and 2026 keep them only in Txt2.
- Txt2 style sheet: `/12` caps, `/13` baseline, `/15` strikethrough (1), `/16` underline (2),
  `/23` Fractions, `/24` Ordinals. The reader takes `/23` and `/24` from the layer's Txt2 object
  (`TextEngineBlock::object_style_features`) and splits TySh runs where they change.
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
- **OpenType features per run.** QTextCharFormat cannot carry font features in Qt 6.8, so a
  fragment whose features change its glyphs gets `kTextRenderFeaturesFormatProperty` and extra
  letter spacing equal to (featured advance - plain advance) / characters, so the layout reserves
  the featured width; the draw path (`fragment_glyph_runs`, text_render.cpp) replaces Qt's glyphs
  with the fragment's own featured shaping, anchored at Qt's first glyph. Lines holding such a
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

- `textCharacterStyleRow`: eight checkable `NoFocus` buttons (`textCharacterAllCapsButton` ...
  `textCharacterFractionsButton`, icons `text_character_style_icon`). The caps pair and the
  baseline pair exclude each other. Each toggle is one Type step on the selection, the whole
  object with a bare caret, or every selected text layer.
- Scripting: text runs take `caps` (`"all"`, `"small"`, `"none"`), `script` (`"superscript"`,
  `"subscript"`, `"none"`), `underline`, `strikethrough`, `ordinals`, `fractions`; `textRuns`
  reads them back ([scripting-api-changes.md](scripting-api-changes.md)).

## Known gaps

Vertical text takes the caps only (no bars, no shifted baselines, no features). Fractions and
ordinals do not shape across run boundaries. A superscript's fractional pixel size rounds to a
whole pixel.

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
`Character - Styling and Extras.psd` in local-test-fixtures/psd). UI
(tests/ui/text_character_styles_tests.cpp): the Photoshop-calibrated bar, script and caps renders
in both layout modes, ordinals and fractions, the panel toggles, and the sample re-render.
