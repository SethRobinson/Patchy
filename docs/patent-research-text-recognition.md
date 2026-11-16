# Text recognition claim comparison (November 2026)

Engineering claim comparison for text recognition ([ocr.md](ocr.md)): the engines, the model pipeline, recognized-text layers and their styling, searchable PDF text, and block grouping. Binding boundary: "Text recognition" in [legal-constraints-scanning.md](legal-constraints-scanning.md#text-recognition). Claims marked "read in full" were read from the granted text at the USPTO Patent Public Search service on 2026-11-16 ([method](patent-research.md#reading-patent-text)); status labels and end dates are Google Patents' (filing plus 20 years plus term adjustment, before any terminal disclaimer). Not read: prosecution histories, non-US family members, published-application claims. A keyword sweep of US documents cannot show that nothing else exists.

## Patchy's design as compared

- **Engines.** The operating system's recognizer (Windows.Media.Ocr) is called through its public API and is not Patchy's implementation. The downloadable engine runs PaddlePaddle's PP-OCRv6 small models through ONNX Runtime with Patchy's own pre- and post-processing. The user picks engine and language; nothing is chosen from the image.
- **Model pipeline.** A detector network outputs a per-pixel probability of "text line region". That map (never the image) is thresholded at a fixed value. Each connected region is one text line; its minimum-area rotated rectangle is taken, low-scoring ones are dropped, and each rectangle is enlarged by a fixed ratio of its area to its perimeter. Each rectangle is cropped by the rotation, scale and translation that map its four corners upright (the source is always a rectangle). A recognizer network reads each crop (CTC decoding). The mask is never dilated, the image is never binarized to find text, no free quadrilateral or perspective correction is used, and no dewarp model is shipped.
- **Recognized-text layers.** On the user's request the lines become editable text layers in a font the user picked. Size comes from fitting the line's width against that font, one size per block; color is sampled; an italic face is chosen from a measured slant. The source pixels are never erased, inpainted or filled; "hide original" hides a layer.
- **Blocks.** Lines join into blocks by the spacing, overlap and thickness of two lines at a time. No line is classified as starting or continuing a paragraph; no page type, margin, indentation or layout input exists.
- **Searchable PDF text.** Each recognized line is written as one invisible run scaled to the line's width. A line is never split into separately placed pieces.

## Model pipeline

| Patent | Status | Independent claims require | Patchy's design |
|---|---|---|---|
| Adobe US 9576348 B2, claim 1 (read in full) | Active, to 2035-03-04 | Identifying a text region by "generating a thresholded version of the image" and "expanding boundaries of individual characters to form a plurality of text blocks in the thresholded image", at least one block holding several characters; a transformation matrix "to correct distortion of the region"; transforming the pixels; recognizing characters from them | The thresholded thing is a network's probability map, in which each region is already a whole line: there are no individual characters and nothing is expanded in the thresholded map (the rectangle enlargement acts on a fitted rectangle after regions exist). The specification's thresholded image is an intensity binarization in which characters stay readable, expanded by repeated dilation (paragraphs 37 to 42). Its "distortion" is a surface "at some non-zero angle relative to an image capture plane" giving a non-rectangular region (paragraphs 13, 48); Patchy's crop source is always a rectangle |
| Adobe US 9576348 claims 9 and 13; US 9928439 B2 claims 1, 10, 19 (read in full) | Active, to 2035-03-04 and 2034-11-14 | A font classifier or recognition "based on a detected font" (all but US 9928439 claim 19); a user input to replace or remove a character; "removing the character from the image based on a content aware fill process" | No font is detected, nothing in the image is replaced or removed, and no fill runs on recognized text |
| Baidu US 10762376 B2 (read in full) | Active, to 2039-02-28 | Per-pixel probability of a character pixel plus position "relative to a bounding box of a character"; bounding boxes of candidate characters; a character mapping network producing character vectors; linking neighbouring characters into sets by the vectors' difference | The detector outputs no per-pixel box offsets, no character boxes and no character vectors; lines are connected regions of one map. PaddleOCR is published by the same owner under Apache-2.0, whose section 3 carries a contributor patent grant |

Facts that would change the comparison: binarizing the image itself and dilating or merging character shapes to find text (including as a fallback detector); turning on the detector post-processing's mask dilation option; polygon or free-quadrilateral detector output with a four-point perspective warp, or any dewarp or perspective correction before recognition; a font identification step; any command that removes or replaces recognized text in the pixels by fill or inpainting.

## Recognized-text layers and their styling

Published earlier work on measuring text attributes without a learned typeface classifier: Xerox US 5253307 and US 5513277 (filed 1991: typeface, point size, stroke width from distance distributions); Zramdini and Ingold, Electronic Publishing 6(3), 1993 (weight, slope, size from projection profiles) and IEEE PAMI 20(8), 1998; Xerox US 6741745 (filed 2000, term ended 2020: a scale factor that fits each rendered word to the scanned word's width, clustered into typeface and size); the ALTO XML schema 1.0 (2004: `TextStyle` FONTSIZE, FONTCOLOR, FONTSTYLE; `ParagraphStyle` ALIGN, LINESPACE); hOCR (2007: `x_fsize`, `baseline`, `ocr_par`); Tesseract 3.01 (2011, Apache-2.0: `WordFontAttributes` with is_bold, is_italic, is_serif, is_monospace, pointsize).

| What Patchy does | Nearest claims found | How they differ |
|---|---|---|
| Size from the line's width against the user's font, one size per block | Xerox US 8768061 (lapsed 2022), Fuji Xerox US 8391606 (lapsed 2021), Microsoft US 10025979 (to 2032) | No per-character box histograms over typographic lines, no per-character-code ratio table, no clustering or standardizing of sizes across paragraphs |
| Color by sampling | Canva US 2025/0218083 A1 (pending, as published: OCR, predicted color or font, an inpainted second image, a text box placed on it) | Nothing is inpainted and no font is predicted |
| Italic from a measured slant | none found by measurement; Adobe US 9875429 and Salesforce US 11610138 use trained models | A fixed formula on the line's ink; no model |
| The user picks the font family | every font claim read requires an automatic determination from the image | No estimation |

Not built, with the nearest claim recorded for a later comparison:

- **Bold from stroke width.** Wipro US 10956730 (active to 2039-06-04): a box per text item, scanning a predefined number of lines in each box for pixel run widths, the most frequent width compared with a threshold. Konica Minolta US 10169650 (to 2037): each line's density compared with the average across all boxes.
- **Serif, sans or monospace class.** Amazon US 9349202 claims 5 and 17 (to 2033-02-07): candidate fonts per glyph, the most common as the paragraph's dominant font, into a reflowable content file. Adobe US 10127199 (to 2034-05-27): per-letterform visual descriptors compared between fonts.
- **Rendering candidate fonts and choosing the closest.** Amazon US 9349202; Celartem US 8488886 (to 2031); Adobe US 7720318 (to 2028-08-20): rendered test text and line-width fingerprints; Adobe US 11126788 and the DeepFont family (US 9501724 and continuations, to 2035 and later).
- **Erasing the text under the new layers.** Adobe US 9576348 and US 9928439 (content-aware fill, above); Canva US 2025/0218083 A1.

## Searchable PDF text

Konica Minolta US 9442899 B2 (read in full; active, to 2034-11-18). All three independent claims recite an "image forming apparatus" and obtaining the image "by document scanning" (claim 21: a program for "a computer of an image forming apparatus"; the specification equates the term with a multi-function peripheral). They require, per recognized line: "splitting each text string into a plurality of short text strings in accordance with a predetermined rule", at least one with several characters; "a uniform font size for each text string" shared by its pieces; "an x-axis position for each of the short text strings ... based on x-coordinates of characters at a forefront of the respective short text strings"; and embedding the short strings at those positions in that size. Dependent claims split at commas, dots or spaces, or every N characters. The specification presents splitting as known and the uniform per-line size as its remedy for uneven search highlights.

Patchy writes one run per line: no short strings exist, so the splitting, per-piece position and per-piece embedding steps have nothing to apply to. Scan a Book to PDF is the path in which Patchy itself scans, so the one-run rule matters most there. Facts that would change the comparison: per-word runs with one font size per line (each word placed from its first character's position); a TJ array or other per-piece positioning inside a line.

Earlier work on invisible text over a scan: Adobe US 5625711, US 5729637, US 6385350 (filed 1994 to 1998, terms ended); Fuji Xerox US 8213748 (lapsed: transparent characters in a font with no drawing element); hocr2pdf and similar tools.

## Blocks

Microsoft US 8565474 B2 (read in full; active, to 2031-08-04). Claims 1 and 9 require "classifying the input image as a first or second page type" and "classifying all textual lines ... as a beginning paragraph line or a continuation paragraph line". Claim 16 has no page-type step and instead requires that line classification "based at least in part on a page layout of each page of the image"; the specification says lines are classified "based at least in part on a layout of each textual line relative to the other textual lines on the page" (paragraph 59), and dependents add poem or text page layouts.

Patchy classifies no line: a block is a connected set of lines under a pairwise spacing rule, and a first line is not told from a continuation line. Facts that would change the comparison: any test of indentation, a short last line, or margins relative to the page to find where a paragraph starts; any page-level mode feeding the grouping. Earlier work on block segmentation by spacing: the run-length smoothing algorithm (Wong, Casey and Wahl, 1982), recursive X-Y cuts (Nagy and Seth, 1984), docstrum (O'Gorman, 1993), ALTO `ParagraphStyle` and hOCR `ocr_par` above. Microsoft US 9946690 (single-line paragraph alignment against page margins) lapsed 2026-04-17; Patchy derives alignment only inside a multi-line block.

## Watch items

- Canva US 2025/0218083 A1 and US 2026/0024366 A1 (pending): read the granted claims when they issue.
- Twelfth-year fee dates of the two nearest patents: US 9442899 on 2028-03-13, US 9576348 on 2028-08-21 (each with a six-month grace period).
- Recent lapses to re-check for reinstatement: Microsoft US 9946690, Xerox US 8768061, Fuji Xerox US 8391606.
- Identified, claims not read: Konica Minolta US 12293143; Monotype US 11537262 and US 12650755; the non-first independent claims of most font-identification patents.
