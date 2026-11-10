# Txt2: settings Patchy keeps and does not draw

Part of [txt2.md](txt2.md) (the block's format, key map and writer). These are Photoshop
paragraph and character settings Patchy reads, carries through its editor and writes back
without drawing anything from them.

## The settings

Hyphenation and the Middle Eastern settings change how Photoshop lays text out; Patchy has no
hyphenation, kashida insertion, digit substitution or diacritic positioning, so it only keeps
the values a file has through an edit and a save (GitHub issue 683; COM captures on This PC,
November 2026: one throwaway box layer per setting, set through Action Manager on the
`textLayer` `paragraphStyle` or `textStyle` property, then both blocks read). There is no UI.

- **Hyphenate** (`hyphenate`): Txt2 `/9`, TySh `/AutoHyphenate` on every paragraph run.
  Photoshop 2026 writes it off for a new layer (its "Basic Paragraph" sheet has `/9 false`;
  the normal sheets keep `true`), so a paragraph Patchy writes has it off unless the file had it
  on. Patchy used to write `true` into every paragraph it rewrote, which turned Hyphenate on in
  Photoshop.
- **Hyphenation settings** (`hyphenateWordSize`, `hyphenatePreLength`, `hyphenatePostLength`,
  `hyphenateLimit`, `hyphenationZone`, `hyphenateCapitalized`): Txt2 `/10` to `/15`, TySh
  `/HyphenatedWordSize`, `/PreHyphen`, `/PostHyphen`, `/ConsecutiveHyphens`, `/Zone` (no TySh key
  for capitalized words). The TySh run always carries the effective values (`/ConsecutiveHyphens
  8`) while the default sheets say `/13 0`, so the reader takes the TySh run and only the Txt2
  run's OWN sheet over it, never the parent sheets.
- **Kashida width**: Txt2 paragraph `/40` only.
- **Line composer** (`textEveryLineComposer`; the Paragraph panel menu's Adobe Every-line
  Composer or Adobe Single-line Composer): Txt2 `/29`, TySh `/EveryLineComposer` on every
  paragraph run. Photoshop 2026 writes false (Single-line) for a new layer and in every default
  sheet. Patchy breaks lines one at a time and wrote false into every paragraph it rewrote,
  which switched an Every-line paragraph to Single-line in Photoshop (its lines then break
  elsewhere on the next layout). See [Line composer](#line-composer) for where Photoshop takes
  the value from.
- **Digit set, diacritic offsets, mark distance, Justification Alternates**: Txt2 style `/70`,
  `/72`, `/73`, `/87`, `/75`. Photoshop 2026 writes no TySh key for any of them; older versions
  wrote `/HindiNumbers` (true for Hindi and Farsi digits), which Patchy writes back for a run
  with a digit set and reads only for a layer without a Txt2 object.

Model: `PsdTextParagraphRun` (`auto_hyphenate`, the five numbers, `hyphenate_capitalized`,
`kashida_width`, `every_line_composer`), paragraph runs v6 column 11 tokens `hyphenate`,
`hyphenation:<word>:<before>:<after>:<limit>:<zone>:<capitalized 0|1>`, `kashida:<n>`, `everyline`
(`serialize_paragraph_run_settings`), the editor's `kTextBlockPhotoshopSettingsProperty`; and
`PsdTextStyleRun` (`digit_set`, `diacritic_x_offset`, `diacritic_y_offset`, `mark_y_distance`,
`justification_alternates`), runs v9 column 16 tokens `digits:<n>`, `diacx:<v>`, `diacy:<v>`,
`marky:<v>`, `justalt`, the editor's `kTextMiddleEasternFormatProperty`. Each token is written
only off its default, so other text keeps its metadata. Hyphenate, the Kashida width, the line
composer and the style keys resolve through the run's sheet, its parent sheet and the document's
default sheet, as the composer does.

Acceptance (Photoshop 2026 over COM, `photoshop-open-check.ps1` and a `textKey` readback): the
reporter's Hebrew and Arabic files re-rendered and saved by Patchy, a Patchy-born file with
Latin, Hebrew and Arabic boxes, and the two round-trip tests' files all open clean
(`textWarningLevel 0`, no `oldText`). Hyphenate reads off on every layer that had it off and on
new layers (the same Hebrew file saved by the build before this fix read on), and the
hyphenation numbers, `kashidaWidthNone`, `hindiDigits` / `farsiDigits` / `arabicDigits`,
`diacXOffset -100`, `diacYOffset 25.5`, `markYDistFromBaseline 0` and Justification Alternates
read back on the paragraphs and runs that carry them.

Tests: `text_engine_hyphenation_round_trips`, `text_engine_middle_eastern_settings_round_trip`,
`text_engine_issue_683_reporter_settings_survive_an_edit_if_available`
(`local-test-fixtures/psd/issue-683/`: the reporter's Hebrew and Arabic Photoshop files),
`ui_text_keeps_photoshop_paragraph_and_middle_eastern_settings_through_edits`.

## Line composer

Where Photoshop 2026 (27.11) takes a paragraph's line composer from, by render probes on
GitHub issue 672's file (four justified Arial paragraphs; the two composers break all four
differently, and a `textStyle` set that changes nothing makes Photoshop lay the text out again
without touching the paragraph style):

- The Txt2 paragraph run's own `/29`; a run that leaves it out takes its parent
  ParagraphSheetSet entry's (`/0/0/6`, "Basic Paragraph" in Photoshop's files). With only that
  sheet edited to true the layout is pixel-identical to an explicit Every-line one; the other
  set entry and `/1/3` change nothing while the parent names the key. The TySh run's value is
  not consulted when the layer has a Txt2 object. The reader (`object_paragraph_composers`)
  goes run, parent, `/1/3`; the TySh run (then its normal paragraph sheet) serves a layer
  without an object.
- Issue 672's file is Single-line: its runs name only `/0`, every sheet and TySh run says
  false, and Photoshop's fresh layout matches the stored pixels and an explicit Single-line one
  exactly. A Patchy save of it (false stated on each run) lays out the same in Photoshop.
- **An Action Manager `set` whose `paragraphStyle` leaves `textEveryLineComposer` out turns
  Every-line on** (and `burasagari` to `burasagariStandard`, `/24 1`). The `textKey` getter
  reports only what the run states, so writing a layer's own `textKey` back, or the DOM's
  `textItem.justification = textItem.justification`, changes the composer of such a layer and
  re-breaks its lines. Never use either to read a setting or to refresh a reference render;
  re-lay text out with a `textStyle` property set instead (tracking to another value and back).

Acceptance (Photoshop 2026 over COM on This PC, November 2026): the Every-line version of
issue 672's file, a layer with one Every-line and one Single-line paragraph, and issue 672's
file itself, each run through Type > Update All Text Layers and saved by Patchy, open clean
(`textWarningLevel 0`, no `oldText`), read `textEveryLineComposer` back as in the source on
every paragraph, and after Photoshop lays them out again match Photoshop's layout of the source
file pixel for pixel. The saves of the build before this change read Single-line everywhere and
laid out differently on every Every-line paragraph.

Tests: `text_engine_line_composer_round_trips`, `text_engine_line_composer_reads_photoshop_sheets`,
`text_engine_photoshop_line_composer_survives_an_edit_if_available` and
`ui_update_all_text_layers_keeps_the_line_composer_if_available`
(`local-test-fixtures/psd/every-line-composer/`, Photoshop 2026 captures),
`ui_update_all_text_layers_keeps_the_line_composer`.

## Paragraph settings not kept yet

`engine_paragraph_properties` and `paragraph_sheet` still write fixed values for the rest of
the paragraph sheet, so a paragraph Patchy rewrites loses a file's own:

- Top-to-top Leading (TySh `/LeadingType 1`, Txt2 `/8 1`) and Roman Hanging Punctuation (TySh
  `/Hanging true`, Txt2 `/21 true`): confirmed with a Photoshop 2026 capture
  (`local-test-fixtures/psd/every-line-composer/justify-modes-leading-above-hanging-photoshop.psd`).
  After a Patchy text update and save both read back off, and Photoshop's next layout pulls
  the hanging punctuation back inside the box. Patchy writes `/Hanging true` only for a
  hanging indent of its own.
- The Justification dialog's word, letter and glyph spacing (TySh `/WordSpacing`,
  `/LetterSpacing`, `/GlyphSpacing`; Txt2 `/17` to `/19`), `/Burasagari` (`/24`) and
  `/KinsokuOrder` (`/25`): written fixed too, not yet checked against a Photoshop file (an
  Action Manager `paragraphStyle` set of the `justificationWord...` doubles is refused; set
  them through a whole `textKey`).
