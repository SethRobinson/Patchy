# GIMP .xcf text layers

Split from [xcf.md](xcf.md): GIMP's `gimp-text-layer` parasite as editable Patchy text (fonts, markup, colors, outlines) and the open-time choice for text layers painted on in GIMP. The [clean-room rule](xcf.md#clean-room-rule) there applies here.

## Text layers

The `gimp-text-layer` parasite (GIMP 2's `(font "Sans Bold")` description and GIMP 3's
font record with full name, family and style; `(color (color-rgb ...))` and GIMP 3's
GeglColor with its babl format) becomes patchy.text.* metadata over GIMP's raster, which
stays until the text is edited. Mapped: text or Pango markup, font family (the full name
minus its style words when the family is a generic alias; generic names become Qt's), size
and unit (pixels, points, inches, millimeters, picas at the image resolution), color,
justification (fill as justify), indent, letter spacing (as tracking), line spacing (added to
a 1.2 em leading), fixed boxes, right-to-left direction, antialiasing. The layer-wide font,
size and color follow the first run, since a span can restyle all of the text.

- Markup (#508): `b`, `i`, `u`, `s`, and spans with `foreground`, `weight`, `style`,
  `underline`, `strikethrough`, `font_family`/`face`, `size` (1024ths of a point at the image
  resolution: GIMP's 50 px at 300 ppi is `12288`), `rise` and `letter_spacing` (1024ths of a
  pixel; a rise on all of the text is dropped, since Pango grows the line by it and the glyphs
  stay put, and a partial rise is a baseline shift), and `font`: GIMP 3 writes an id there that a `(markupfont "font1136" (font ...))`
  form after the markup string resolves; other writers put a Pango description.
- Colors (#508): GIMP 3 keeps the model the color was picked in. RGB and gray (perceptual
  `'` or `~` formats as they are, linear ones encoded), HSV and HSL (textbook models over the
  encoded values; HSV checked against GIMP's own hex), CIE Lab and LCH(ab) (D50, Bradford to
  sRGB). Other models (CMYK) fall back to black, visible only after an edit.
- Outlines (#509): GIMP 3's `outline` (`stroke-only`, `stroke-fill`), `outline-direction`
  (outer, inner, else centered), `outline-width` and `outline-foreground` become a Stroke
  style (Outside, Inside, Center; size the width: an outer width-w outline is a band w wide
  outside the glyphs in GIMP 3.2's renders), with Fill opacity 0 for stroke only. GIMP's raster
  has the outline baked in, so the reader writes `patchy.xcf.pending_outline` and
  `MainWindow::render_pending_xcf_text_layers` redraws the text at open (origin at the layer's
  top-left) and adds the style (notice). Pattern and dashed outlines open as pixels (notice).
- A layer GIMP marks as painted on (PROP_TEXT_LAYER_FLAGS bit 2) opens as GIMP's pixels
  (notice) and keeps the text it would have opened with (#551): every key the editable layer
  gets (outline included) under `patchy.xcf.stored_text.`, plus `patchy.xcf.stored_text_box`,
  GIMP's layer rect, which a move carries along (`translate_moved_layer_metadata`). Once the
  file is a tab (Open, Reopen, files opened together), `offer_painted_gimp_text_conversion`
  asks once per file (`xcfPaintedTextMessageBox`, with the layer count): Keep as Pixel Layers
  (default), Convert to Text Layers (the keys go back and the text draws from the box's top-left
  like the outline redraw, dropping the paint as GIMP does on a text edit; locks and the rest
  stay), or Add Text-Layer Copies (a hidden, unlocked "<name> (text)" layer above each, so the
  file still looks as GIMP drew it). Don't ask again saves the choice in
  `imports/xcfPaintedTextLayers` (`ask`, `pixels`, `text`, `copies`; also a Preferences combo).
  Converting or copying is one undo step that marks the document modified, and the reader's
  notice gives way to one saying what was done. The question waits until the tab is fit on
  screen (#593, [file-formats.md](file-formats.md)); once answered, its notes stay out of the
  Import Notes box (the status bar keeps them), while a saved choice is still listed there. Unattended runs (scripts, MCP, batch, tests)
  never ask: `ask` keeps the pixels, a saved choice applies. Files as Layers keeps the pixels.
  The stored text is in memory only: no format writes it (a PSD has no Photoshop-safe place for
  it), so a saved and reopened file has plain pixels.
  Patchy's redraw of an edited layer starts within a few pixels of GIMP's
  (`ui_xcf_text_layers_stay_editable`); its glyphs are Patchy's.
