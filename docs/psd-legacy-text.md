# Photoshop 5.x type layers (the `tySh` record)

Photoshop 5.0 and 5.5 stored a type layer's text in the per-layer tagged block `tySh` ("Type tool info" in Adobe's specification). Photoshop 6 replaced it with the descriptor plus EngineData block `TySh` that the rest of Patchy's text codec handles ([text-render-calibration.md](text-render-calibration.md), [txt2.md](txt2.md)). Modern Photoshop still opens the old record as editable text; Patchy does since September 2026 through `src/psd/psd_text_legacy.cpp`. Before that the reader matched the key but fed the payload to the TySh extractors, which found no EngineData, so every PS 5 type layer imported as plain pixels (Title02.psd, the Cockpit Master title screen, local fixture only).

## Record layout

Decoded byte for byte from all six Title02.psd blocks; big-endian throughout. The only departure from Adobe's text is the style section, which carries no version word.

| Section | Fields |
|---|---|
| Header | u16 version (1); six f64 `xx xy yx yy tx ty`. The transform maps text space to document pixels. `tx`/`ty` is the FIRST line's baseline anchor at the alignment point: the line's left edge for left-aligned text, its center for centered, its right edge for right-aligned (Photoshop 2026 reports the same point as `textItem.position`). |
| Font info | u16 version (6), u16 face count; per face: u16 mark, u32 font type, Pascal PostScript name (`FuturaBT-BoldCondensed`), Pascal family (`Futura BdCn BT`, the GDI name), Pascal style (`Bold`), u16 script, u32 design-axes count, that many i32 axis values. Pascal strings are unpadded. |
| Style info | u16 count (no version word in real files; the reader also accepts the specification's `u16 version, u16 count` form, see below). 26-byte records: u16 mark, u16 face mark, i32 size, i32 tracking, i32 kerning, i32 leading, i32 base shift (all signed 16.16 fixed point), u8 auto kern, u8 rotate. Marks are arbitrary ids (Title02 numbers them 4, 3, 2, 1); characters reference styles by mark and styles reference faces by mark. |
| Text info | u16 type (0, point text; PS 5 had no paragraph text), u32 scaling (16.16, 1.0), u32 character count, u32 horizontal placement, u32 vertical placement, u32 selection start, u32 selection end, u16 line count; per line: u32 count, u16 orientation (0 horizontal), i16 alignment (0 left, 1 center, -1 right), then count pairs of u16 UTF-16 code unit and u16 style mark. Every line except the last ends in `\r` (U+000D), which is one of its counted units. |
| Color | Photoshop's 10-byte color: u16 color space and four u16 components (0 RGB 0..65535 per channel; 1 HSB with hue 0..65535 spanning 0..360 degrees and saturation and brightness 0..65535; 2 CMYK stored inverted; 8 grayscale with the level in the first component on a 0..10000 scale), read by `read_legacy_effect_color`, the same helper the PS 5 `lrFX` effects use. Then u8 anti-alias (0 off, 1 on) and up to three pad bytes to the declared block length. |

The style-section ambiguity is resolved by validation, not assumption: the reader parses the count-first layout and requires every style to name a known face and the text section behind it to be consistent (line unit sums within the character count, the color and anti-alias byte inside the block). When that fails it retries with a version word skipped, then finally accepts the count-first layout with unknown face marks, which fall back to the first face.

## Units and calibration (Photoshop 2026 over COM, September 2026)

- **Size** is in engine units; the transform's scale turns it into document pixels, exactly the modern TySh convention. Title02's `WWW.COCKPITMASTER.COM` stores 23.31 under a 0.7722 transform and Photoshop reports 23.31 px for it; the copyright line stores 9 at identity. The run keeps the stored size and `patchy.psd.text.transform` keeps the matrix. At 72 ppi (Title02) PS 5 points equal pixels; files at other resolutions are unverified (no local sample), and the layer reader applies no resolution scaling.
- **Tracking** is an em fraction in the record; Photoshop's Character panel and Patchy's run model use thousandths of an em, so 0.1 becomes 100 (COM read 100, 200 and 400 for Title02's 0.1, 0.2 and 0.4).
- **Leading** 0 means auto (Photoshop's 120 percent rule, the paragraph run's default fraction); any other value is fixed leading in the same units as size (the nine-line menu stores 34 for a 32 px face, and Photoshop reports `useAutoLeading = false`, leading 34).
- **Anti-alias**: byte 1 reads as Photoshop 2026's Sharp, whose EngineData `/AntiAlias` value is 4 (the same value the committed `photoshop-text-anchor-*.psd` Sharp fixtures carry); byte 0 stays None (0). `legacy_type_tool_anti_alias` owns the mapping.
- **Color**: the HSB copyright color (39835, 65535, 30583) decodes to `#002a77`, which is the raster's own pixel color and what Photoshop reports.

## What the reader produces

`extract_legacy_type_tool` returns a `LegacyTypeToolInfo` and the `tySh` branch of `read_layer_record` (psd_layer_records.cpp) fills the same `LayerRecord` fields the modern path does, so everything downstream (metadata keys, the layers panel, the text editor, the PSD writer) is shared:

- `patchy.text` is the unit stream as UTF-8 with `\r` mapped to `\n`; a non-final line that lacks its terminator gets one, and a trailing terminator on the last line is dropped, so run indices computed on the units apply to the text unchanged.
- Style runs coalesce consecutive units with the same style mark. Each run carries the face, the stored size, the block color (PS 5 has one color per layer), tracking in thousandths, and either fixed leading or the auto flag. Kerning, base shift, auto kern, rotate, placement, selection, scaling factor and design axes are not modeled.
- Paragraph runs: one per non-empty line with that line's alignment (justification 0 left, 1 right, 2 center).
- Fonts: the PostScript name goes through `resolve_photoshop_font_name` (DirectWrite, registry, heuristic on Windows; the app's font-database resolver elsewhere). When only the suffix heuristic answered, the record's own family and style strings win, because they are the GDI names Photoshop 5 wrote and the names Windows lists the face under; bold and italic then come from the style string (`Bold`, `Black`, `Heavy`, `Italic`, `Oblique`).
- Geometry: only the transform. `bounds`, `boundingBox` and the tail stay zero, the degenerate form CS-era TySh descriptors also produce, and the UI's document-space fallback (`psd_point_text_document_bounds_transform_for_pixels`, main_window.cpp) pins a re-render to the imported raster's visible box. `patchy.psd.text.index` is deliberately absent: the record has no TextIndex, and a stored 0 on several edited layers would make them all claim object 0 of the rebuilt `Txt2` block.
- Photoshop's raster is kept (`patchy.text.raster_status = psd_raster_preview`) and the layout mode is `photoshop`, like any Photoshop-authored type layer.
- A line with a non-horizontal orientation (PS 5 vertical type) leaves the whole layer a pixel layer. A malformed or truncated record does the same; nothing throws.

## Saving

An untouched legacy layer re-emits its original `tySh` bytes verbatim (no generated block is written for a `psd_raster_preview` layer whose transform is unchanged), so a round trip through Patchy leaves the file's text records byte-identical. An edited layer is written like any other regenerated type layer: a modern `TySh`, with the `tySh` dropped beside it.

**A document that still carries a verbatim `tySh` gets no `Txt2` block** (`build_text_engine_block`, psd_document_io.cpp), unless the file already had one. Photoshop reads a `tySh` only through its old-text path, and the mere presence of a document-level text engine block switches the whole document to the new engine: a Title02 resave with the template block opened in Photoshop 2026 with every untouched type layer demoted to a plain NORMAL layer, and stripping that one block restored all six (COM readback, September 28, 2026). Without the block Photoshop reads the kept records and the regenerated `TySh` alike from their own bytes; it shows its usual "Some text layers might need to be updated" prompt, exactly as it does for the original file. Once every legacy layer has been edited, no `tySh` remains and the normal Txt2 authoring applies again.

## Tests

`tests/core/psd_legacy_text_tests.cpp` builds the record from a spec that mirrors the table above (`legacy_type_tool_payload`) and pins the import, the verbatim untouched save, the edited-layer upgrade with two edited layers getting distinct `Txt2` objects, every truncation prefix and an absurd character count staying pixel layers, the version-word retry, and the RGB, gray and none anti-alias readings. `psd_title02_legacy_text_layers_import_if_available` pins the six Title02 layers (text, sizes, colors, alignments, the scaled transform, the menu's leading and tracking) against the COM readings and skips when the local fixture is absent. The fixture itself is Seth's and stays out of the repository.
