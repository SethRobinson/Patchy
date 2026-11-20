# Testy text scoring

Companion to [Testy scoring](testy-scoring.md).

## The save after a text update

The save test above saves each file as the editor opened it. An editor that, like
Photoshop, keeps a type layer as the file had it until the text is edited can pass the
layer's data through untouched (Patchy does), so that save says little about the text
data the editor writes itself. Two Patchy bugs showed only after an edit: justified
paragraphs saved as justify left (GitHub issue 672) and Hyphenate turned on (issue 683).
So for files with type layers three editors get a second save (`_text_save_leg`,
`cell.textSave`):

- **Editors**: Patchy, PhotoCraft and Photopea (`TEXT_SAVE_EDITORS`), the ones whose
  text render score already rests on a scripted text update. Krita and Affinity lay text
  out on open and save their own on a plain save; GIMP and PhotoDemon have no text
  engine; psd-tools is not an editor.
- **Steps**: the editor opens the ORIGINAL staged file again (not a cache-stripped copy),
  runs its text update and saves `textsave.psd`. Photoshop opens that save through the
  probe every resave goes through, which lays the text out afresh as the reference does,
  and writes `textsave.png` and `textsave_manifest.json`.
- **Per type layer of the original** (`score_text_save`, `manifest.compare_text_layers`):
  *lost* when it is no longer a type layer; *changed* when it is still text but Photoshop
  draws it differently or reports a changed setting; otherwise *same*. The cell shows
  `same`/`measured`, and the summary card adds the layers up as "text kept after a text
  update".
- **Drawn the same**: the original layer's box (grown 2 px) in Photoshop's render of the
  save against the reference render, so a difference elsewhere in the picture, already in
  the round-trip render score, is not counted twice. Both pictures are Photoshop's own,
  so the measure is `analyze.compare_boxes`' `movedFraction`: the share of pixels showing
  something with no counterpart within 2 px in the other render, after a light blur. On
  a 12 px justified paragraph of six lines it reads 0.2% for the same text laid out a
  fraction of a pixel differently (24% of the pixels change), 3.8% for one line set flush
  left instead of centered and 16% for different line breaks; over 1%
  (`TEXT_SAVE_BOX_LIMIT`) counts as drawn differently. A box that the as-opened save's
  render differs in too, the same way, reads "differs as opened too" and is not counted.
  Two known limits (November 2026): after the re-layout Photoshop keeps a layer effect's
  gradient aligned to the pixels it loaded until the file is saved and reopened, so a
  gradient stroke shifts its colors (psd-tools `effect-stroke-gradient.psd`: 19% and 23%
  of the pixels, nothing moved, read as the same); and the reference of psd-tools
  `pen-text.psd` draws "Vodka vodka" 74 px wide where Photoshop lays the same data out
  79 px wide (eleven Courier advances at 12 px) in every editor's save, current text
  included. The cause is not established; the layout cached in the file's old named-key
  `Txt2` object is the suspect.
- **Settings**: each paragraph's justification, Hyphenate, direction and line composer
  (every-line or single-line), the layer's orientation, anti-aliasing and kind (point or
  paragraph text), the text itself and the fonts it names, from Photoshop's manifest of
  both files. The layer's descriptor names a value only where the layer states it, and
  no set may be used to make it say more (the same rule as for the reference render: a
  set of one paragraph property to the value it has changes the composer). So Hyphenate
  and the line composer that a paragraph leaves out are read from the file itself, the
  way Photoshop resolves them (`text_sheets.fill_paragraph_settings`): the layer's
  `Txt2` paragraph run, its parent ParagraphSheetSet entry, the block's default sheet;
  for a layer without a `Txt2` object the TySh EngineData run and its normal sheet. The
  layer is matched by its index path and must have the same name and paragraph count
  in the file. A value neither Photoshop nor the file gives is never read as a change.
- **Missing fonts**: when Photoshop lacks a font the original's text needs, the leg is
  not measured, as with the text render score. A save in which Photoshop finds a font
  missing that the original had keeps the editor's pixels, so its render is not
  compared; the changed font is reported as a setting.
- **Not measured, never a failure**: a layer the editor's update did not reach
  (`notMeasured`, with the reason: Patchy's script or Photopea's edit failed on it, or
  Patchy lacks a font Photoshop has, a layer Type > Update All Text Layers skips too);
  an editor that wrote no save; a Photoshop that did not start (the cell is then not
  cached). `state` is then "not measured" with `reason`.
- **A save Photoshop refuses** has `state: "rejected"`, and dialogs Photoshop shows while
  opening the save are in `textSave.dialogs`, as `resaveRejected` and `resaveDialogs` are
  for the as-opened save. A dialog Photoshop raised for the original too (old text
  layers that "might need to be updated") is left out: it is no news about the save.
- Nothing is folded into `nativeScore`, `roundtripRender` or an existing history key.
  The run summary and `history.jsonl` gain `textSave` `{same, measured, files, rejected}`
  per editor that has the leg. Cells of the three editors carry `-textsave2` on text
  files, and the ground truth of text files `-textattrs2` (its manifest lists the
  settings).

Acceptance (November 11, 2026, Photoshop 2026): the four justified paragraphs of the
issue 672 file (Single-line Composer, Hyphenate off, both left to the parent sheet),
saved after the text update by three Patchy builds. Patchy 1.29 (both bugs):
justification changed on three layers and Hyphenate turned on in all four, three drawn
differently (2.1% to 3.6% of the box moved). Patchy 1.31 (672 fixed): Hyphenate on in
all four, drawn pixel for pixel the same. A build with both fixes: all four the same,
pixel for pixel. Tests: `tests/testy_text_save_tests.py`,
`tests/testy_text_sheets_tests.py`.

## The two text rules that score 0%

Text is the thing people assume survives, so two failures zero a file's score
outright, and the report says why on the cell (a red flag) and at the top of the
detail panel (`textZeroReasons` in report.py):

- **Cannot render a Photoshop text object: render 0%.** When the cache-free leg finds a type
  layer the editor draws nothing for (it can only show the pixels Photoshop cached),
  the file's byte-match and perceptual scores become 0% (`_apply_text_render_rule`).
  The measured numbers stay in `renderMetrics.measured`; `textNotRendered` names the
  layers. A text layer that is "not measured" is not zeroed: that verdict means
  Testy could not make the editor's engine run, not that it has none.
- **Cannot save a Photoshop text object as text: data kept 0%.** When any type layer of the
  original is not a type layer in the editor's resave (rasterized, converted or
  dropped), `nativeScore` becomes 0 (`manifest.apply_text_save_rule`). The object
  counts stay as measured, the replaced score is `nativeScoreMeasured`, and
  `textNotSaved` holds `{lost, total}`.

Both rules are applied to cached cells on reuse. An editor that writes no .psd at
all has no "data kept" score to zero (open: it is simply absent from that average).

## psd text handling in the Standing card

Strict on purpose (`psdTextStanding` in report.py). Over the files with type layers,
a file scores 0 for an editor that cannot render its Photoshop text objects (the
cache-free leg found a type layer it draws nothing for, or `TEXT_RENDER_BASIS` says
"replay": the editor only ever shows the baked pixels) or cannot save them back into
the .psd as text (`textNotSaved`). Otherwise the file scores what the editor's own
text render scored: the scored render of that file, for editors that lay text out on
open or after Testy's scripted re-render ("open": Patchy, Krita, Affinity, Photopea,
PhotoCraft).
Every editor is measured with the same metric. An editor that fails every file reads "0% (FAIL *)", and the
marks are explained under the list ("Cannot save text objects back out into the .psd
as text", "Cannot render psd text objects, only uses the baked pixels saved in the
file"); editors with the same failure share a mark. An editor that fails some files
keeps its average, with a footnote giving the count.
