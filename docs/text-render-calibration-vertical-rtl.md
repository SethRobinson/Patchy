# Vertical type and paragraph direction calibration

Split from [text-render-calibration.md](text-render-calibration.md): the Photoshop calibration of vertical type (tategaki) and right-to-left paragraph direction; the editing session for both is in [text-tool.md](text-tool.md).

## Vertical type (tategaki)

Photoshop 2026 captures: `local-test-fixtures/psd/ps2026_vtext/` (`capture_vtext.jsx`, PSD + PNG +
`manifest.jsonl` with DOM bounds; `dump_tysh.py` prints a TySh). Four are committed as
`test-fixtures/psd/photoshop-text-vertical-{point,box,rotated-roman}.psd` and
`photoshop-text-rtl-hebrew.psd` (corpus digests pinned). Pinned by
`ui_vertical_text_matches_photoshop_capture` (ink within 1 px of PS's 67x92 point raster; skips
without MS Gothic) and `psd_vertical_*captures*` in tests/core.

- **Every glyph is upright, Latin included, by default**: "Hello" stacks H, e, l, l, o. Photoshop's
  "Standard Vertical Roman Alignment" lies Roman glyphs on their side instead: per run,
  `/BaselineDirection` 1 = upright (what PS writes by default), 2 = rotated; a run WITHOUT the
  key re-lays out rotated, so every vertical run Patchy writes carries it. Patchy: runs v7
  column 14 (`2` = rotated, written only then), `kTextRotatedRomanFormatProperty`, the
  Character panel's "Rotate Latin (vertical text)"; a rotated cell advances by the glyph's
  horizontal width and is drawn turned 90 degrees clockwise with its ascent+descent box centred
  on the axis; CJK clusters stay upright either way. Cell pitch = FontSize x
  VerticalScale (MS Gothic and Arial both advance 32 px per glyph at 32 px); whitespace advances
  by its horizontal width (a 32 px Arial space is 8.89 px of column, so "Hello World" spans
  10 x 32 + 8.89). Tracking adds FontSize x tracking/1000 after every cell except a column's
  last (three cells at +200 = 96 + 2 x 6.4).
- **Glyph placement in the cell**: centred on the column axis horizontally; the font's
  ascent + descent box centred vertically, baseline = cell top + (em - (asc + desc)) / 2 + asc.
  MS Gothic's box is exactly 1 em (ink 3 px inside the cell top), Arial's is 1.117 em (caps
  4-5 px below the cell top, ~0.84 em baseline).
- **Columns advance left by the entered column's max effective leading** (auto 1.2 x 32 =
  38.4: second column left edge at -54.4; fixed 48: at -64), the horizontal per-line rule
  transposed.
- **Anchors** (`bounds` in the TySh, transform = the click): point text x in [-em/2, em/2]
  around the first column's axis; y in [0, h] for left (top), [-h/2, h/2] for center,
  [-h, 0] for right (bottom). Box text: transform at the frame's top-left, `/BoxBounds`
  [0 0 w h], the first column against the frame's RIGHT edge, wrapping by whole cells at the
  frame height (7 cells of 32 in a 250 px frame), overflow columns hidden.
- **Patchy raster = cell union + bleed** `vertical_text_bleed_for_size(size)` = ceil(0.25 x
  base size) on every side (psd/psd_text_runs.hpp, shared by the renderer and the Qt-free PSD
  writer), so `text_geometry_for_layer` recovers the anchor from the raster rect alone:
  tx = right - bleed - em/2, ty = top + bleed + fraction x (height - 2 x bleed).
- **TySh encoding**: descriptor `Ornt` enum `Vrtc`; engine data `/WritingDirection 2` in
  both the Shapes and Lines dictionaries and `/Procession 1` (horizontal: 0, 0). Nothing else
  in the engine data differs between a vertical and a horizontal save.
- **OpenType `vert`** rides on the render document's default font (`QFont::setFeature`; Qt 6.8
  char formats cannot carry features), so fonts with the table get their vertical brackets and
  long-vowel marks.
- **Acceptance (COM, September 2026)**: Photoshop 2026 opened the Patchy-authored
  `test-artifacts/vertical_text_check.psd` (written by
  `ui_vertical_text_recommit_keeps_origin_and_round_trips_psd`, dialogs suppressed), read the
  layer back as `orientation:vertical` with bounds [-54.4, -32, 16, 32] (its own convention for
  centred two-column text), and a forced type re-render landed the two columns within 4 px of
  Patchy's ink. Photoshop's reading of an authored paragraph direction is unverified.

## Paragraph direction (right-to-left)

`patchy.text.paragraph_runs` v4 appends column 9 (`auto`/`ltr`/`rtl`), written only when a
paragraph carries an explicit direction. Photoshop keeps its `directionType` OUTSIDE the TySh
(captures `rtl_hebrew_dir_rtl.psd` and `_ltr.psd` differ only in bounds): the Txt2 paragraph
sheet's `/33` (0 left to right, 1 right to left; [txt2.md](txt2.md)). Photoshop's default engine
already reorders Hebrew and shapes Arabic (captures `rtl_mixed.png`, `rtl_arabic_left.png`), as
Qt does.

**Photoshop has no auto direction, and its alignment follows the direction** (GitHub issue 683,
PS 2026 COM grid on This PC, November 2026: Hebrew and Arabic, point and 300 px box text, each
direction with left, center and right). A paragraph is left to right unless `/33` says 1, even
when it is all Arabic, and `/Justification` is relative to that direction: in a right-to-left
paragraph 0 ("left") sets the text against the box's RIGHT edge and 1 ("right") against the left,
and point text extends left of its anchor. That is Patchy's own model (alignment is logical, Qt
mirrors it in a right-to-left block) EXCEPT for auto, which resolves by the first strong
character: an Arabic paragraph Photoshop lays out left to right and right aligned came out right
to left, so its right alignment landed on the left and its runs reordered. So:

- **The reader** (`apply_paragraph_composers`, psd_layer_records.cpp) takes the direction of the
  Txt2 paragraph run (own sheet, parent sheet, `/1/3`); `/33 1` reads `rtl`. A TySh key is used
  only when the layer has no Txt2 object: Patchy's `/ParagraphDirection`, or Photon Studio's
  `/Direction` (Photoshop ignores both; a Photon Studio 1.70 file with `/Direction 1` beside
  `/33 0` opens left to right in Photoshop, so it does in Patchy). A paragraph left without a
  direction stays `auto` when its first strong character is left to right (auto lays it out the
  same, and Latin files keep their metadata) and becomes `ltr` when it is right to left.
- **The writer** (`text_engine_inputs_for_layer`) resolves an auto paragraph whose first strong
  character is right to left to `rtl`, in the TySh key and `/33` alike, so Photoshop draws it the
  way Patchy did and a reopen keeps it. `first_strong_text_direction` (psd_text_runs.hpp) is the
  shared bidi default: a table of strong letters, not the full Unicode database.
- Tests: `psd_issue_683_direction_reads_txt2_and_photoshop_default`,
  `psd_issue_683_reporter_files_read_direction_if_available` (`local-test-fixtures/psd/issue-683/`:
  the reporter's Photoshop file, his Patchy 1.29 resave with right to left set, his Photon Studio
  1.70 file), `ui_issue_683_arabic_psd_rerenders_right_aligned_if_available`.
