# Testy scoring: what each cell measures

Companion to [testy.md](testy.md) (setup, running, machine specifics).

## The measurements

For every (PSD, editor) pair, the editor opens a staged COPY (corpus files are never
touched; a SHA check at the end of every run proves it), and Testy records:

- **Opens** - did the file load at all.
- **Render accuracy** - the editor's flattened PNG vs Photoshop's, composited over
  white at document size. Two comparisons always run, labeled **byte match** and
  **perceptual** in the report. Byte match counts pixels off by more than 6/255 per
  channel (plus RMSE); honest about raw data, but a subtle color-management shift
  can mark a visually identical render ~100% different. Perceptual counts pixels
  that actually look wrong: SSIM's contrast-structure term combined with CIEDE2000
  deltaE, both computed on lightly blurred copies so anti-aliasing jitter stays
  quiet, with the deltaE threshold scaled up under strong local contrast. A global
  8/255 shift scores ~0% perceptually while byte match reports ~100%; a genuinely
  missing, misplaced, or recolored object fires both. Each metric also gets a
  per-object breakdown using ground-truth layer bounds; an object "renders ok"
  while under 25% of its region's pixels are off (text legitimately differs on
  glyph edges; a bbox also contains what renders behind it, so one error can hit
  several objects). Worst offenders are named in the detail panel, ranked by the
  run's comparison mode. Byte match runs at document resolution; perceptual costs
  about a second and 150 MB of numpy temporaries per megapixel, so it runs on
  copies area-averaged down to `PERCEPTUAL_MAX_PIXELS` (4 MP) and is skipped when
  the renders match pixel for pixel. Above 4 MP the downsample can shift the
  perceptual `badFraction` in relative terms; it drives a 10% triage threshold, not
  a pinned number, and the byte-match figure is unchanged.
  `python testy\analyze.py --selftest` pins all of it against synthetic renders; no
  Photoshop or corpus needed.
- **Honest rendering (trap)** - the editor also opens a byte-patched variant whose
  embedded flat composite is replaced with magenta (`psd_sections.py` rewrites only
  the trailing image-data section; all layer data stays byte-identical). Magenta in
  the render means the editor displayed Photoshop's baked composite instead of
  compositing layers itself. Flattened files (zero layer records) get no trap: the
  composite is the only image data, so reading it is correct and even Photoshop
  would trip the sentinel (noted in the detail panel; cached cells are corrected
  on reuse). Photoshop tripping its own trap means even the ground
  truth could not re-render the layers (missing fonts etc.) and fell back to the
  baked composite; another editor matching that is not a cheat (a neutral note says
  so) and does not flag in scan mode. Only sentinel coverage more than 5 points
  beyond Photoshop's own counts as a cheat.
- **Native preservation** (labeled "data kept in .psd save" in the report and CLI
  summary; the results.json/history.jsonl keys stay `native`/`nativeScore`) - the
  editor's re-saved PSD is reopened in Photoshop and its layer manifest compared
  against the original's: text still `TEXT`, each adjustment still its exact kind,
  smart objects still smart, groups/masks/vector masks/live effects/clipping/blend
  modes intact. This is the "23/40 objects survived" number; a resave Photoshop
  refuses to open scores as rejected. Layers are paired by name first; what
  that leaves over on both sides is paired in stack order, because a rename is not a
  loss (PhotoDemon renames the background, and Photoshop names the one layer of a
  file saved without layer records in its interface language). The kind still has
  to hold. `MATCHING_VERSION` re-compares cached cells from their stored manifests.
  Photoshop's Action Manager distinguishes artboards from ordinary groups and
  reports each layer's own visibility switch, including children of hidden groups.
  A conversion between an artboard and an ordinary group counts as a changed
  object. The attribute counts show how many paired layers kept their visibility
  setting. Pixel and text checks use effective visibility, including the parents.
  New runs give the updated Photoshop manifest its own reference, cell and reopen
  cache keys. Adding a column to an older run keeps its recorded reference and
  compares the properties that both manifests measured.
  A run can also reopen each resave in other Photoshop versions; a save any of them
  rejects is a bad save ([testy-photoshop-versions.md](testy-photoshop-versions.md)).
- **Round-trip render** - Photoshop's render of the editor's resave vs the
  original's render.
- **Text kept after a text update** (Patchy, PhotoCraft, Photopea; files with type
  layers) - a second save made after the editor re-lays out its text, reopened in
  Photoshop: each type layer still text, drawn the same, settings unchanged. See
  [The save after a text update](testy-scoring-text.md#the-save-after-a-text-update).
- **Missing fonts** - Photoshop checks every text layer's style ranges against its
  installed fonts (`textFontProblems`). If a font the text needs is missing or cannot
  be inspected, Photoshop cannot draw that text faithfully either: the reference keeps
  the baked pixels, and no editor's own text render is scored for that file: in the
  cache-free leg its type layers keep their cache (the `*_fontkept` staged copies),
  are not re-rendered by any script and are listed as not measured, while its shapes,
  fills, smart objects and masks are still scored. The file is left out of the
  Standing card's psd text handling, and the detail panel names the fonts. No font is
  silently replaced. The font inventory is part of the ground-truth and Patchy cache
  keys, so installing fonts invalidates old text results on the next run.
- **Bit depth kept** (`cell.saveDepth`) - the depth in the resaved .psd's header
  against the original's (`save_depth_record` in testy.py; no Photoshop needed, so
  cached resaves are read too). A 16 or 32-bit file saved at a lower depth shows
  "lost: 1/1 bit depth (16 to 8-bit)" even when every layer survived; a deeper save
  counts as kept. It is not folded into `nativeScore` (history stays comparable);
  the summary card and history line carry `depthKept`/`depthTotal` over deep files.
- **16-bit precision** (16-bit files only; `analyze.compare_deep_renders`). The
  Photoshop probe saves a second render at 16 bits (`render16.png`, `roundtrip16.png`,
  written by `normalizeForPng` just before its drop to 8 bits, so both are the same
  picture). `deepRender` compares the editor's render to the 16-bit reference (its own
  `render16.png` when it wrote one, as the Photoshop column does, else its 8-bit
  render); `deepRoundtrip` compares Photoshop's render of the editor's resave to it
  (`roundtrip16.png` when the resave is still 16-bit, else the 8-bit `roundtrip.png`,
  whose rounding is the loss), which needs no 16-bit export from the editor. A pixel is precise within
  `DEEP_TOLERANCE` (64 levels, a quarter 8-bit step); RMSE and worst error are in
  8-bit steps. An 8-bit render or save of a smooth 16-bit image misses on about 7 of 8
  pixels by rounding alone, which the 8-bit metrics cannot see. 16-bit PNGs decode
  through OpenCV when installed, else a built-in decoder (Pillow reads 16-bit RGB as
  8-bit). Patchy exports a 16/32-bit document as a 16-bit PNG, which its driver splits
  into `render16.png` and an 8-bit `render.png` (`analyze.split_deep_png`). 32-bit files
  have no precision leg yet. 16-bit files carry `-deep1` in their cache keys.

The Photoshop column doubles as a control: ~100% render accuracy and full native
preservation validate the pipeline itself.


## The reference render

The selected Adobe Photoshop installation supplies the PSD reference for every corpus. Testy enables Photoshop's error dialogs during open and records acknowledged warnings. A refused source, an open warning, or a render that Photoshop cannot produce or repeat excludes the source from compatibility scores and the scan's list of editor defects. The report keeps the reason and counts the excluded files. When two of three Photoshop opens agree within the render tolerance, Testy scores against the agreed image. An older run can identify only the warnings it recorded. New runs use separate caches for warning-enabled opens.

The Photoshop driver records when each alert first appears. A notice raised later while exporting the reference stays in the file details and does not count as an open warning.

For an eligible source, a Photoshop warning on an editor's saved PSD counts against that save. The report counts files with save warnings separately from files whose saves Photoshop refuses. An additional Photoshop version can judge saves only when it opens the original cleanly.

- Photoshop's scored reference PNG is always 8-bit sRGB (16-bit files also get the
  `render16.png` precision reference above). `normalizeForPng` in
  `drivers/photoshop.py` converts Bitmap to Grayscale, any non-RGB mode to RGB, the
  document profile to sRGB (relative colorimetric, black point compensation; skipped
  for 32-bit), and 16-bit to 8-bit. Without it a grayscale or CMYK file's reference
  would be in the document's own space and every editor would score against the wrong
  numbers.
- The comparison honors an embedded ICC profile in either render
  (`analyze.load_srgb_rgba`), so an editor that exports in the document space with
  the profile attached is not marked down for it.
- Type layers are laid out afresh for the reference: `refreshText` sets each type
  layer's own `textClickPoint`, alone (`setd` on `textLayer`; a layer without one gets
  its anti-aliasing switched away and back), which makes Photoshop compose the text
  again and changes nothing. An old file's cached text pixels can differ from what
  today's Photoshop draws. Skipped when a font is missing (the cache is then the only
  faithful picture). The write must carry no paragraph or character style
  ([txt2-kept-settings.md](txt2-kept-settings.md#line-composer)): writing the whole
  `textKey` back turns Adobe Every-line Composer on
  in every paragraph that does not state its composer and re-breaks its lines (all four
  paragraphs of GitHub issue 672's file), so an editor that kept the file's real
  Single-line setting was marked down; a `textStyle` set (tracking to a value and
  back) levels every run's own tracking. Checked in Photoshop 2026 on a layer with
  five runs (three faces, four sizes, tracking 15, -25, 0, 60, 0) in an Every-line and a
  Single-line paragraph, and on warped, rotated, vertical and right-to-left layers:
  every paragraph and run reads back identical, the layout equals the tracking round
  trip's on single-run layers, and a layer whose stored pixels another program drew is
  drawn again. `ProbeNeverSetsTextSettings` (tests/testy_text_sheets_tests.py) pins it.
- Embedded smart objects are rendered afresh too (`refreshSmartObjects`): the
  contents are opened, a layer is added and removed, and the save makes Photoshop
  render the layer from its contents, smart filters included. Verified by giving
  Photoshop copies with those caches removed: the refreshed render is identical to
  the refreshed original. Each embedded document is opened once (layers sharing it
  are all updated by the first), within a 30 second budget. Linked files, vector
  contents and lossy formats (a save would recompress a JPEG) keep their cache.
- Shape and fill layers need nothing: Photoshop's render is identical with and
  without their cached pixels on all 133 corpus files that have them.
- `reference_space_key` adds `-srgb1`, `-freshtext3` and `-freshsmart1` to the
  ground-truth and cell cache keys for the files these rules change, so older cache
  entries are not reused.

## Files an editor refuses

A file Photoshop opens and an editor does not counts as a 0% match for that editor
in every average (`refusedWithReference` in the report, `_aggregate` in testy.py).
Harness failures (a wedged Photoshop, a skipped editor) stay out of the averages.
So does a Photopea or Affinity leg that failed on Testy's side: the driver returns
`"opens": None` for a driver crash, a dead browser, a connector that is off, or a host
page that never got the file into Photopea (`_HARNESS_ERRORS`, `_HARNESS_STALL_STEPS`
in `drivers/photopea.py`), and the cell keeps no "opens" verdict. A stall after the file
reached Photopea is Photopea's own and still scores 0 (three psd-tools files hang there
on every run).

## Scoring without Photoshop's cached pixels

A PSD stores a second copy of every type layer, shape or fill layer and smart
object: the pixels Photoshop last drew for it. An editor that shows those pixels
has not rendered the layer, and a reader of the scores cannot tell. So for files
with such layers the scored render comes from a copy with the caches removed.

- `psd_sections.strip_cached_pixels` empties the stored rectangle and the color and
  transparency channels of each such layer (keys in `CACHED_LAYER_KEYS`); records,
  blocks, masks and layer order stay byte-identical, and the flat composite becomes
  the sentinel. This is the state Photoshop itself writes for fill layers in 16-bit
  files. Linked smart objects (`SoLE`) are left alone: nothing in the file can
  redraw them. Testy writes it natively, with no third-party PSD library. 16/32-bit files keep
  their records in an `Lr16`/`Lr32` block behind an empty standard block, and that
  block is rewritten the same way (Photoshop opens the result; in this corpus those
  files' fill layers already carry no cached pixels, which is why only their text
  layers change).
- A layer with both a pixel mask and a vector mask stores the two already combined
  into one raster (channel -2) beside the pixel mask itself (channel -3), so a reader
  can show the right picture without rasterizing the vector mask. The copies
  overwrite the combination with the pixel mask alone and give the mask its "real"
  rectangle and background (kind "mask" in the stripper's list). Photoshop renders
  all 11 such corpus files identically afterwards. Nothing is labeled for these
  layers; an editor that leaned on the combination simply scores what it draws.
- The "plain" copy also renames the defining blocks to an unknown key (`tsTY`),
  leaving ordinary empty pixel layers. The two copies differ in nothing else, so a
  difference between an editor's two renders inside a layer's box is what the editor
  drew for that layer; no difference means it drew nothing. Do not hide the layers
  instead: GIMP exports a different canvas when nothing is visible.
- `_no_cache_leg` renders the stripped copy (`nocache.png`), keeps the normal render
  as `render_as_opened.png`, and writes the scored `render.png`: the stripped render,
  with each layer the editor drew nothing for outlined and labeled ("Cannot render
  Photoshop text objects", and the same for shape or fill layers and smart objects).
  The wording names Photoshop on purpose: several of these editors render their own
  text objects perfectly well, and the claim is only about the ones in a PSD. The cell's `noCache` block lists `notRendered` and `notMeasured` layers.

The leg must never mark an editor down for the harness's own mistake:

- **A blank type layer or smart object is not proof of a missing engine.** Photoshop
  itself shows nothing for either once the cache is gone, until the layer is edited.
  A blank one counts against an editor only where `BLANK_IS_FAILURE` says the editor
  is known to draw that kind from the layer's data (or to have no engine for it).
  Otherwise the box keeps the as-opened pixels and the layer is reported as "not
  measured (cache shown)". No editor in the roster is in that state: PhotoDemon's
  source settles its case (pdPSD.cls creates every PSD layer as `PDL_Image` and never
  reads `TySh`, so it has no PSD text to lay out). Blank shape and fill
  layers always count: Photoshop draws those from the layer's data.
- **Patchy's type layers and smart objects keep their cache and are re-rendered by
  script** (`TEXT_CACHE_KEPT`; the `*_textkept` staged copies strip everything else).
  Patchy, like Photoshop, shows the stored pixels until the layer is edited and takes
  the layer's placement from them, so on a fully stripped copy its text comes out
  small and misplaced and a smart object not at all, which says nothing about its
  engines. `patchy.render_text_afresh` runs `drivers/patchy_text_afresh.js`, which
  calls `layer.rerenderText()` on every type layer and `layer.rerenderSmartObject()`
  once per embedded source, then exports. This is how Photoshop's own reference is
  produced. A layer the script did not reach (a smart object Patchy keeps locked,
  for one) is not measured.
- **Photopea** shows cached text until a text layer is edited, so its stripped render
  goes through `photopea.render_text_afresh`: the host page assigns each text
  layer's `kind` to itself, one layer per script with its own timeout. Only
  `LayerKind.TEXT` layers are touched; reading `textItem` on any other layer makes
  the script engine hang without answering. A text layer the edit did not reach is
  not measured.
- **PhotoCraft** 0.5.0 shows cached text and draws nothing for a type layer without
  pixels, so its stripped render goes through `photocraft.render_text_afresh`
  (Type > Update All Text Layers, then export; one CLI call).
- **Photopea is handed the fonts the text uses.** It runs in a browser with only its
  own web fonts, so text in a font installed here (the one Photoshop drew the
  reference with) was laid out in a substitute. `fonts.py` finds this machine's file
  for each PostScript name in Photoshop's manifest (a face inside a .ttc is written
  out as its own .ttf), and the host page posts the files to Photopea before the
  document opens (`fonts` URL parameter). Nothing is uploaded: Photopea reads the
  bytes inside the local browser. Measured on `layer_effects.psd` (Arial Black): 17.1%
  of pixels off without the font, 5.5% with it. The manifest lists every face a
  layer's style ranges use (`fonts`), so a mixed-font layer gets all of them. Photopea text cells carry `-fonts1` in their key.
- **The extra renders must be of the same document.** A stripped or plain render that
  comes back at another size, or differs from the as-opened render outside the
  cached layers' boxes (grown by a quarter plus 8 px) on more than 5% of those
  pixels, voids the leg after one retry: `noCache.state` is "not measured" with the
  reason, and the cell stays scored as opened. So does an editor that cannot open the
  stripped copy. This caught Affinity exporting the previous file's document when
  every staged copy shared one name, which is why the driver stages each copy under a
  per-file name.
- **An editor that falls back to the flat composite** when no layer has pixels
  (PhotoDemon) shows the sentinel. That is read as "drew nothing for the cached
  layers" (`showedComposite`), and the scored image is the as-opened render with
  those boxes emptied and labeled, not a magenta canvas.
- A layer invisible in the as-opened render too (covered, zero fill) is no finding.
- The "plain" copy is artificial, and a reader can trip on it: psd-tools fails to
  composite the plain copy of `masks.psd` (a broadcast error inside its own code).
  That voids the leg for that cell, as above; it is never read as "cannot render".

Measured on open, caches removed: Affinity redraws text, shapes and
fills; Patchy redraws shapes and fills (text and smart objects through its script); Krita redraws text and gradient fills but nothing for
vector-masked solid fills; Photopea redraws shapes, fills and smart objects, and
text after the scripted edit; PhotoCraft redraws shapes and fills, and text after
Update All Text Layers; psd-tools redraws shapes and fills only; GIMP and
PhotoDemon draw nothing. Cell cache keys carry `-nocache11`.

## Related documents

- [Text scoring](testy-scoring-text.md) covers the save after a text update and the Standing card.
