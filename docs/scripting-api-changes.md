# Scripting API compatibility

`app.apiVersion` is 1. Bump it only for breaking API changes. Every addition and
behavioral change is recorded here, newest first, in the same change that makes it; the
entries are a permanent record. Entries through 2026-09-30 (the July 2026 API, MCP
attachment, palettes, vector automation, brushes, Slow mode and Pause, text direction,
Remove Object, Align/Distribute and smart objects) are in
[scripting-api-changes-through-2026-09-30.md](scripting-api-changes-through-2026-09-30.md),
those of 2026-10-01 to 2026-10-19 in
[scripting-api-changes-2026-10-01-to-19.md](scripting-api-changes-2026-10-01-to-19.md), and
those of 2026-10-20 to 2026-10-28 in
[scripting-api-changes-2026-10-20-to-28.md](scripting-api-changes-2026-10-20-to-28.md).
those of 2026-10-20 to 2026-10-29 in
[scripting-api-changes-2026-10-20-to-29.md](scripting-api-changes-2026-10-20-to-29.md).
Those of 2026-10-28 to 2026-11-10 are in
[scripting-api-changes-2026-10-28-to-11-10.md](scripting-api-changes-2026-10-28-to-11-10.md).

Rollover: once this file passes 27,000 bytes, move its oldest entries, unchanged and in order,
out of it until about 12,000 bytes remain, so the next rollover is weeks away. They go into the
newest archive (the last one named above) until that archive nears 27,000 bytes, then into a new
archive. Renaming the newest archive for its new end date changes one link in each file that
links it. Archives are frozen records: when entries move into one, links to them from other
documents point at the archive file without a `#anchor`, and an archived file is edited only
when `python scripts/check-docs.py` reports a broken link in it.

Every entry below is API 1.

2026-11-17 (behavioral change): `recognizeText` with `layers: "replace"` and the sampled color
(`matchStyle` on, no `color`) gives a line whose sampled text color has under 3 to 1 contrast
with the Paper the line's own background color instead, or black or white when that does not
read either ([ocr.md](ocr.md#text-layers), Color). `"visible"` and `"hidden"` are unchanged.

2026-11-17 (behavioral change): with the `ppocr` engine, `recognizeText` reports each line and
word about where its ink is, not as the padded rectangle the recognizer reads: short words
were a third too wide and every line about half again too tall
([ocr-engines.md](ocr-engines.md#pp-ocr), "Line boxes"). Text layers made from them are sized
and grouped from those boxes, and two lines now join a block when their centres are at most
2.2 times their mean thickness apart (it was the thicker line's)
([ocr-blocks.md](ocr-blocks.md#measurements)).

2026-11-17 (addition): `layer.eraseBackground` option `protectColor` (a color string): the
Background Eraser's Protect Foreground Color for scripts
([eraser-tools.md](eraser-tools.md#protect-foreground-color)). A pixel at least as close to
that color as to the sampled color is not erased, and one only slightly closer to the sampled
color is erased in part. Without the option nothing is protected, as before. The key is permanent, as is the settings key
`tools/backgroundEraserProtectForeground`.
2026-11-17 (behavioral change): `recognizeText` with `grouping: "blocks"` never puts two lines of
one row into one text layer. Lines that sit side by side (a title and a label beside it, a row of
buttons, two columns under a line that spans them) used to become separate lines of one layer
when each was close to the same line above or below; now each such line is its own layer on its
own place ([ocr.md](ocr-blocks.md#one-line-per-row)). With `layers: "replace"` the Paper layer is white as before,
except over a dark image, where it takes that image's background color so the light text sampled
from it shows.

2026-11-17 (addition): `app.buildBookPdf` options `searchable` (boolean; default: the choice
saved in the book project), `ocrEngine` and `ocrLanguage` (strings; default: the stored picks,
then the first available engine and its default language). A searchable book PDF recognizes
the text of every page at export and writes it invisibly over the page image
([book-scan.md](book-scan.md#searchable-pdf)). The call throws when `searchable` is on and no
engine can run, or the named engine or language does not exist.

2026-11-17 (addition): `searchableText` (boolean, default true) on `app.exportPdf` and on
`doc.saveAs`/`doc.exportAs` for `.pdf`. Text in the document's Recognized Text groups (made by
`recognizeText` with `layers` other than `"none"`, the Type menu command or Import PDF), shown or
hidden, is written invisibly over each page that is an image, one run per line, so PDF readers
find, select and copy it ([pdf-searchable-text.md](pdf-searchable-text.md#from-a-document)).
False leaves it out. A document without recognized text writes the same bytes as before. With
`editableLayers` a page of several layers holds only the text it shows.

2026-11-17 (addition): text recognition (OCR, [ocr.md](ocr.md#scripting)). `patchy.ocr.available`
is true when an engine can run, and `patchy.ocr.engines()` lists them:
`{id, name, available, unavailableReason, defaultLanguage, languages: [{tag, name}]}` (`"windows"`
is the engine built into Windows 10 and 11). `layer.recognizeText(options?)` reads one layer
rendered on its own and `doc.recognizeText(options?)` the whole image as composited; both return
`{engine, language, text, angle, lines, group}`, each line
`{text, x, y, width, height, angle, vertical, words: [{text, x, y, width, height}]}` in document
pixels. Options: `engine`, `language` (default: the stored choices, else the first available
engine and its default language), `rect` (`{x, y, width, height}`), `layers` (`"none"` the default,
`"visible"`, `"hidden"`, `"replace"`), `grouping` (`"blocks"` the default, `"lines"`), `font`
(default: the Type tool's), `matchStyle` (default true) and `color` (`"sample"` or `"#rrggbb"`).
With `layers` other than `"none"` the recognized text becomes editable text layers in a group named
Recognized Text, one undo step: over the image, hidden, or shown on a white Paper layer with the
original hidden (`"replace"`; the original is never erased). The calls throw on an unknown option,
engine or language, when no engine is available, and when the layer or rectangle holds nothing to
read; a call never downloads anything and never picks an engine or language from the image. The
option values and the engine ids are permanent.

2026-11-16 (addition): `layer.eraseBackground(options)` and `layer.magicErase(options)` are the
Background Eraser and the Magic Eraser (GitHub issue 727,
[eraser-tools.md](eraser-tools.md#scripting)). `eraseBackground` erases one stroke with a round tip
through `points` (`{x, y}` in document pixels, 1 to 100000 of them): the pixels matching a sampled
color lose their alpha, the rest under the tip stay. Options: `size` (1 to 1024, default 40),
`hardness` (0 to 100, default 100), `sampling` (`"continuous"` the default, `"once"`,
`"background"`, which needs `backgroundColor`), `limits` (`"discontiguous"`, `"contiguous"` the
default, `"findEdges"`) and `tolerance` (0 to 100, default 30). `magicErase` is one click at `x`,
`y` (document pixels) that erases the area the Fill tool would flood from there. Options:
`tolerance` (0 to 255, default 32), `antiAlias` (default true), `contiguous` (default true),
`sampleAllLayers` (default false) and `opacity` (0 to 100, default 100). For both, the layer must
be the document's active layer, a selection limits the erase, and a layer whose transparency is
locked is painted with `backgroundColor` (default white) and keeps its alpha, as the Eraser does;
an unlocked layer without alpha gains one. Both return the changed bounds `{x, y, width, height}`,
all zero when nothing changed, throw before erasing on an unknown option, a wrong type or a value
out of range, and never change the artist's tool or options. Permanent identifiers: the two
methods, their keys and tokens, the hotkey command ids `tools.background_eraser`,
`tools.magic_eraser` and `tools.cycle.eraser`, and the settings keys
`tools/backgroundEraserSampling`, `tools/backgroundEraserLimits`, `tools/backgroundEraserTolerance`,
`tools/magicEraserTolerance`, `tools/magicEraserAntiAlias`, `tools/magicEraserContiguous`,
`tools/magicEraserSampleAllLayers`, `tools/magicEraserOpacity`. Works at 8, 16 and 32 bits. Pinned
by `ui_script_erase_background_and_magic_erase`.

2026-11-13 (addition): `layer.replaceColor(options)` is the Color Replacement tool (GitHub issue 720,
[color-replacement.md](color-replacement.md#scripting)): one stroke with a round tip through
`points` (`{x, y}` in document pixels, 1 to 100000 of them) that repaints the pixels matching a
sampled color with `color`. Options: `size` (1 to 1024, default 40), `hardness` (0 to 100, default
100), `mode` (`"hue"`, `"saturation"`, `"color"` the default, `"luminosity"`), `sampling`
(`"continuous"` the default, `"once"`, `"background"`, which needs `backgroundColor`), `limits`
(`"discontiguous"`, `"contiguous"` the default, `"findEdges"`), `tolerance` (0 to 100, default 30)
and `antiAlias` (default true). The layer must be the document's active layer; a selection limits
the change and alpha never changes. Returns the changed bounds `{x, y, width, height}`, all zero
when no pixel matched. It throws before painting on an unknown option, a wrong type or a value out
of range, and never changes the artist's tool or options. Permanent identifiers: the method, its
keys and tokens, the hotkey command ids `tools.color_replacement` and `tools.cycle.brush`, and the
settings keys `tools/colorReplaceMode`, `tools/colorReplaceSampling`, `tools/colorReplaceLimits`,
`tools/colorReplaceTolerance`, `tools/colorReplaceAntiAlias`. Works at 8, 16 and 32 bits. Pinned by
`ui_script_replace_color_strokes_and_validation`.
2026-11-13 (addition): text on a path (GitHub issue 717, [path-text.md](path-text.md#scripting)).
`doc.addTextLayer(text, {path, pathRange})` sets the text along a copy of one open or closed
`PatchyVectorPath` contour in document pixels; `pathRange` is `{start, end}` in segment units
(the whole part names a segment of the contour, the fraction the place inside it) and defaults
to the whole contour. `path` excludes `area` and `box`. `layer.textPath` reads a detached
snapshot of the contour or `null` and is assignable (`null` leaves a rectangular text box);
`layer.textPathRange` reads and sets `{start, end}` (`null` on other layers);
`layer.flipTextPath()` reverses the contour. Each throws before any change on a contour that is
not one subpath of at least two anchors, on a range outside the contour, and on a layer that is
not editable text. Pinned by `ui_text_path_script_creation_range_flip_and_undo`.
2026-11-12 (addition): `doc.selection.selectColorRange(options)` is Select > Color Range (GitHub
issue 709, [color-range.md](color-range.md)): a soft selection from each pixel's own color.
Options: `range` (`"sampled"`, the default, or `"reds"`, `"yellows"`, `"greens"`, `"cyans"`,
`"blues"`, `"magentas"`, `"highlights"`, `"midtones"`, `"shadows"`; permanent tokens), `colors` and
`subtractColors` (arrays of `"#rrggbb"` strings, at most 32 in all; `"sampled"` needs at least one
color), `fuzziness` (whole number 0 to 200, default 40), `invert`, `operation` (`"replace"`, `"add"`,
`"subtract"`, `"intersect"`) and `sampleAllLayers` (default true). It throws on an unknown option,
range or color and on a value out of range, before the selection changes. Works at 8, 16 and 32
bits. Pinned by `ui_script_select_color_range_selects_by_color`.
2026-11-11 (behavioral): `doc.resizeCanvas` no longer leaves the Background's name and locks on a
layer that stopped being the Background (GitHub issue 701,
[background-layer.md](background-layer.md#canvas-changes)). With options, a resize that leaves the
Background pixels beyond the canvas makes it an ordinary unlocked layer named "Layer 0" (the
name in the user's language) that keeps every pixel, as Image > Canvas Size does; a resize that
only grows the canvas keeps the Background and fills as before, now whatever the Background is
named. Without options, any new size releases it the same way. Documents made by
`app.newDocument` have no Background and are unchanged. Pinned by
`ui_script_resize_canvas_keeping_pixels_turns_background_into_layer_0`.
2026-11-10 (behavioral): `addShape` with a `line` geometry centers the line's stroke unless the
appearance names an `alignment`; it was inside, where the stroke never showed past the line's
weight (local-051, [vector-tools-lines.md](vector-tools-lines.md#a-lines-stroke)). The other shapes keep
inside. A line's `arrowWidth` and `arrowLength` keep their 0 to 30000 px range; `updateShape`'s
weight scaling now stops a head at 30000 px, and a head longer than its line is drawn at its size
([vector-tools-lines.md](vector-tools-lines.md#a-head-longer-than-its-line)). Pinned by
`ui_script_vector_line_stroke_centers_and_heads_grow`.
