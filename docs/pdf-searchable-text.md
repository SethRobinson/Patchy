# Searchable text in image PDFs

Split from [pdf.md](pdf.md): invisible text over a page that is an image, so a reader can find, select and copy the words a scan shows. This doc owns the writer side (`formats/pdf_image_writer`, `formats/pdf_hidden_text_font`). Where the text comes from is [ocr.md](ocr.md).

## Rules

The binding entry is "Text recognition" in [legal-constraints-scanning.md](legal-constraints-scanning.md#text-recognition).

- **One run per line.** `TextLine` is a whole line and is written as one `Tj`, stretched to the line's length. A line is never split into separately placed words or characters, there is no `TJ` array, and no per-word positioning may be added here. Searching inside a long line therefore highlights approximately: the price of the rule, stated to users where the option is.
- **The font is Patchy's own.** `hidden_text_font()` builds a TrueType file byte by byte. No other project's font file is read, copied or adapted.
- **A page without text keeps its bytes.** Nothing about the font or the text is written, reserved or numbered until a page actually carries a usable line, so every image PDF Patchy wrote before this still comes out byte for byte (`pdf_image_writer_without_text_writes_the_bytes_it_always_did` pins a file; a flat document saved by the app was compared before and after the change).

## The writer

`ImagePage::text` (last member) is a list of `TextLine{utf8, x, y, width, size, angle_degrees}`: where the line starts on its baseline in page points before `/Rotate` (x right, y up), its length along its reading direction, its text size, and the counterclockwise turn of that direction (0 reads left to right; -90 reads down the page, which is how a column of vertical text is written).

- **Operators.** After the image (`q ... cm /Im0 Do Q`) the page gets one text object: `BT 3 Tr`, then per line `/F0 <size> Tf <scale> Tz <a b c d x y> Tm <codes> Tj`, then `ET`. Render mode 3 neither fills nor strokes. The horizontal scale is `100 x width / (characters x size)`: every glyph of the font is one em wide, so that stretches the run to exactly the line's length whatever its characters.
- **Lines that are left out**: no characters after control characters are dropped, a width or size that is not positive, a position that is not finite. A page whose lines are all left out is a page without text.
- **Codes.** Each distinct character gets a two-byte code in order of first use across the whole file, from 1 (`hidden_characters_`), written as a hex string. At most 65,534 distinct characters; later ones are skipped.
- **Font objects**, written once by `finish()` when any page had text (the Type0 object's number is reserved when the first such page is written, so pages can name it): the font file (`/FontFile2`), its descriptor, a `/CIDToGIDMap` stream that sends every code to the one glyph, the `CIDFontType2` descendant (`/DW 1000`, `/W [1 N 1000]`), a `/ToUnicode` CMap (`bfchar` entries in blocks of 100; a character outside the basic plane maps to its surrogate pair), and the Type0 font `/PatchyHiddenText` with `/Identity-H`.
- **The font file** (`pdf_hidden_text_font.cpp`): 276 bytes, the six tables an embedded TrueType CID font needs (`glyf`, `head`, `hhea`, `hmtx`, `loca`, `maxp`), 1000 units per em, one glyph (a square one em wide), no dates, so it is the same bytes on every run. Table checksums and the file's checksum adjustment are computed, not pasted.

## Reading it back

`ContentSink::on_hidden_text` (default: ignore) receives runs in render mode 3 or 7. Sinks that build a picture ignore them, and such a run is not counted as a primitive: a scan plus its text layer still probes as one image, so `PageProbe::source_page` is set and the page's original image data still passes through an export. The tests read the writer's output through this hook (`execute_page`), which shares no code with the writer.

## Tests

`tests/core/pdf_hidden_text_tests.cpp`: the font is a well-formed TrueType file (directory order, every table's checksum, the file checksum, one glyph); a three-page file with Latin, Cyrillic, Japanese, a character outside the basic plane and a column comes back through the reader with every line's text, position and length, the font objects in place, the page without text untouched and the page still probing as a pass-through source; and the no-text byte pin.
