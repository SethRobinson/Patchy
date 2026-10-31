# GIMP .xcf import

Read-only import of GIMP's native documents on every platform (local desk card local-011):
`src/formats/xcf_document_io.{hpp,cpp}` (structure, pixels, blend and composite modes,
effects), `xcf_text.cpp` (text layers) and `xcf_unwrap.cpp` (gzip, bzip2 and xz wrappers),
all Qt-free. The registry row is `patchy.formats.xcf` ("GIMP Image", extensions `.xcf`,
`.xcfgz`, `.xcfbz2`, `.xcfxz`, sniff on `gimp xcf ` or a wrapper holding it). The Open dialog
row adds `*.xcf.gz`, `*.xcf.bz2` and `*.xcf.xz`; their last extension says nothing, so
`xcf::is_compressed_xcf_file_name` routes them in `load_document_from_path`,
`is_supported_open_path` (drops), `extension_for_path` and the Save guard. Save on an .xcf
is Save As (no writer), defaulting to `<name>.psd`.

## Clean-room rule

The format comes only from GIMP's published XCF description
([developer.gimp.org/core/standards/xcf](https://developer.gimp.org/core/standards/xcf/);
link it, never copy its text) and from files GIMP writes, read as data. GIMP's source
(`app/xcf`, GEGL, babl) and other XCF readers (xcftools, libxcf, ImageMagick's and Krita's
importers, the GimpFormats Python package) are never read, copied or searched. GIMP runs
only as a command-line tool for fixtures and reference renders. The text parasite keys,
the HSV/LCh mode formulas and the effect parameters were found by observation and fitting
against GIMP's renders. Legal record: [patent-research-formats.md](patent-research-formats.md),
binding rule in [legal-constraints.md](legal-constraints.md). bzip2 and xz decoders are
Patchy's own, written from the published format descriptions.

## Container

- Big-endian throughout. Header `gimp xcf ` + `file` (version 0) or `vNNN` + NUL, then width,
  height, base type (RGB, gray, indexed), the precision from version 4, image properties,
  then pointer lists for layers (top first), channels and, from version 18, paths. Pointers
  are 32-bit up to version 10 and 64-bit from 11. Versions 100 and up are CinePaint's
  dialect: refused with their own message.
- A property is type, length, payload. PROP_COLORMAP and PROP_USER_UNIT carry wrong length
  words in many files, so their payloads are measured; every other length is trusted,
  bounds-checked. A repeated parasite name keeps the last one.
- Every layer of the tree is in the one top-level list, depth first; PROP_ITEM_PATH's length
  gives its depth. A path deeper than the open groups is clamped with a damage notice.
  Group layers carry GIMP's projection pixels; they are not read.
- Tiles are 64 x 64, row-major, raw, RLE (each byte of the pixel as its own run stream) or
  zlib (one stream per tile). A tile's data runs to the next tile's start. A zero tile
  pointer ends the list early (GIMP's own test files do this): the rest is transparent,
  not damage. A tile that fails to decode opens transparent with a notice.
- Wrappers: gzip (miniz, concatenated members), bzip2 (own decoder, block CRCs checked,
  randomized blocks refused) and xz (own LZMA2 decoder; LZMA2 is the only filter accepted,
  check fields skipped). A sniff decodes only the first chunk.

### Limits

Refused before allocation with "too large": a canvas or drawable side over 300000, a
drawable over 2^30 pixels, more than 24 GiB of decoded drawables in a file, 100000 layers,
channels or paths, an unwrapped size over 16 GiB. Property lists over 100000 entries, group
nesting past 64 and hostile counts read as damage. Truncated and mutated files are swept by
`xcf_hostile_input_never_crashes`.

## Precision and color

| GIMP precision | Patchy document |
|---|---|
| 8-bit non-linear or perceptual | 8 bits, samples copied |
| 8-bit linear | 16 bits (linear 8-bit values do not survive sRGB encoding at 8 bits) |
| 16 and 32-bit integer, any curve | 16 bits, encoded with the sRGB curve |
| half, float and double | 32-bit linear float; 16 bits with a notice when a layer uses a mode Patchy offers only at 8 and 16 bits |

With deep editing off every precision opens at 8 bits with a notice. Gray layers widen to
RGB; indexed layers expand through the colormap (at most 256 entries), alpha on or off at
128 as GIMP renders it, and the colormap becomes the document's attached palette in file order,
so the open path offers Palette mode as for an indexed PNG ([palette-mode.md](palette-mode.md)). Masks,
channels and the selection are coverage: their values are kept whatever the curve. The
`icc-profile` parasite becomes the embedded profile of an RGB image, which the open path
converts to sRGB like every format ([rgb-profiles.md](rgb-profiles.md)). Known gap: 8 and
16-bit images in GIMP's linear precision with a non-sRGB profile are encoded with the sRGB
curve before that conversion.

## Layers

- Name, offsets, size, opacity (PROP_FLOAT_OPACITY over PROP_OPACITY), visibility, the
  active layer, group expanded state. Locks: content to Lock Image Pixels, alpha to Lock
  Transparent Pixels, position to Lock Position; GIMP's visibility lock and linked flags are
  dropped. Color tags map blue, green, yellow, orange, red, violet and gray to the same
  label, brown to orange.
- Layer and group masks become Patchy masks at the layer's bounds; PROP_APPLY_MASK off
  disables the mask.
- GIMP has no Background layer: an alpha-less bottom layer opens as an ordinary opaque layer
  with only the locks GIMP saved, never as Patchy's Background (#547).
- The floating selection opens as an ordinary layer above the layer it belongs to (notice);
  one attached to a channel or mask is dropped (notice).
- GIMP 3.2 vector layers open as shape layers ("Vector layers" below); link layers open as
  their pixels (notice).
- GIMP's active layer stays active; when the import replaced it (an Erase or Split layer), its
  group stands in, else the top layer (#487: such a file failed to open).

## Vector layers

PROP_VECTOR_LAYER (XCF 24, GIMP 3.2), as the published description lays it out: modified,
the path's tattoo, filled, stroked, then the fill (style 0 solid or 1 pattern, antialias, a
GeglColor, a pattern RESOURCE) and the stroke (the same four, then width in pixels, cap 0 butt
1 round 2 square, join 0 miter 1 round 2 bevel, miter limit, dash count and dashes in stroke
widths). `vector_layer_shape` makes a shape layer from the path record with that tattoo
(PROP_TATTOO in its properties): all of its strokes in one shape group, since GIMP fills a
path even-odd; the solid fill color; a centered stroke with GIMP's width, cap, join, miter limit
(below 1 read as 1, which bevels as GIMP does) and dashes, its color's alpha as the stroke
opacity. Patchy draws the shape and drops GIMP's raster; the path also stays in the Paths panel.

GIMP's pixels stay, with the notice that the path is in the Paths panel, for a pattern fill or
stroke, a fill color with alpha, a color in a format `gegl_color_to_srgb` does not read, the
modified flag (pixels painted on after GIMP drew the layer), any layer effect, a text parasite,
or no path with the tattoo. Antialias off is not kept: the shape draws smooth. Against GIMP
3.2.6 (`xcf_vector_layers_match_gimp_renders`): `test-fixtures/xcf/vector-layers.xcf`
(`scripts/dev/xcf/make_vector_layer_fixture.py`) 1.4 levels mean, at antialiased edges;
philipzae's #548 file (`local-test-fixtures/xcf-issue-548`) 2.2, mostly one stroke GIMP drew
aliased, whose last dashes GIMP's aliased stroker places a few pixels away.

## Blend modes

GIMP's modes and Patchy's; Exact means within a level of GIMP's render on every opaque pixel
of the mode fixtures. The GIMP-only modes are Patchy modes with the rules of
[blend-modes.md](blend-modes.md#gimp-modes) (hidden from menus, PSD saves bake them or write
Normal).

| GIMP mode (PROP_MODE) | Patchy |
|---|---|
| Normal (0, 28), Dissolve (1) | Normal, Dissolve |
| Multiply (3; 30 in linear light) | Multiply; Multiply (Linear) |
| Addition (7; 33 linear), Subtract (8; 34 linear), Divide (15; 41 linear) | Linear Dodge, Subtract, Divide; Addition, Subtract, Divide (Linear) |
| Screen, Difference, Darken/Lighten Only, Dodge, Burn, Hard Light, Overlay (23), Vivid/Pin/Linear Light, Hard Mix, Exclusion, Linear Burn | the same-named Photoshop mode |
| Soft Light (19, 45; the old Overlay 5) | Soft Light, within six levels |
| Grain Extract, Grain Merge (20, 21, 46, 47) | Grain Extract, Grain Merge |
| HSV Hue, HSV Saturation, HSL Color, HSV Value (11 to 14, 37 to 40) | the same GIMP modes |
| LCh Hue, Chroma, Color, Lightness (24 to 27) | the same GIMP modes |
| Luminance (56), Luma Darken/Lighten Only (54, 55) | the same GIMP modes |
| Erase (58), Split (60) | the layers below in a group masked by its inverse coverage (notice); Normal with nothing visible below it in the image or an isolated group, as GIMP draws it (#487) |
| Pass Through (61) | Pass Through on a group |
| Behind (2, 29), Color Erase (22, 57), Merge (59) | Normal, with a notice |

### GIMP's own modes

Each of the seventeen is within one level of GIMP 3.2's renders on every opaque pixel of the mode fixtures, with `s` the layer and `d` the backdrop in display-encoded sRGB:

- Grain Extract `d - s + 128` and Grain Merge `d + s - 128`, clamped (blend_math.cpp; bit-exact on the legacy modes; the 16/32-bit kernel uses 0.5 for 128).
- HSV Hue, HSV Saturation, HSV Value and HSL Color: the textbook HSV and HSL models (Python's `colorsys`). Hue keeps `d` when `s` has no saturation; HSL Color takes hue and saturation from `s` and lightness from `d`.
- LCh Hue, LCh Chroma, LCh Color and LCh Lightness: CIE Lab with a D50 white through the Bradford-adapted sRGB matrix, in polar form; LCh Hue keeps `d` when `s` has no chroma.
- Luminance: `d` in linear light scaled by `Y(s) / Y(d)`, Y from that matrix's middle row. Luma Darken Only and Luma Lighten Only pick the whole pixel with the lower or higher Y.
- Multiply, Addition, Subtract and Divide (Linear): the operation on linear-light values, encoded back and clamped; Divide gives 1 over a zero layer value unless the backdrop is 0.

Script ids: `grain-extract`, `grain-merge`, `hsv-hue`, `hsv-saturation`, `hsl-color`, `hsv-value`, `lch-hue`, `lch-chroma`, `lch-color`, `lch-lightness`, `luminance`, `luma-darken-only`, `luma-lighten-only`, `linear-multiply`, `linear-addition`, `linear-subtract`, `linear-divide`.

PROP_BLEND_SPACE picks the linear forms: Multiply, Addition, Subtract and Divide blend in
linear light by default in GIMP 2.10 and later, in the profile curve in legacy files. A mode
set to blend in another space than its default opens in its usual form with a notice.

## Composite modes and spaces

- GIMP renders the bottom visible layer of the image and of every isolated group as Normal.
- Clip to backdrop (PROP_COMPOSITE_MODE 2, Auto for most non-Normal modes): over an opaque
  backdrop Patchy's union draws the same. Where the backdrop is not opaque, a layer whose
  list holds only its base pixel layer and layers already clipped to it becomes a clipping
  mask on that base (exact); otherwise it gets a mask made from the coverage below it,
  which approaches GIMP's result (notices). Clip to layer and intersection are treated as
  clip to backdrop (notice).
- Composite space: GIMP 2.10 and later mix partly covered pixels and opacity in linear
  light; Patchy mixes 8 and 16-bit documents in display values and 32-bit ones in linear
  light. This is the main remaining difference: soft edges, faded layers and glows differ
  by a few levels on average and up to about 70 at single pixels.

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
- A layer GIMP marks as painted on (PROP_TEXT_LAYER_FLAGS bit 2) opens as pixels (notice).
  Patchy's redraw of an edited layer starts within a few pixels of GIMP's
  (`ui_xcf_text_layers_stay_editable`); its glyphs are Patchy's.

## Layer effects (GIMP 3 filters)

- `gegl:dropshadow` becomes a Drop Shadow style (#507): GEGL's x/y offset as distance and
  angle, size three times its radius plus the grow radius (spread from the grow share, fitted
  on effects.xcf), its color in any GeglColor model (`gegl_color_to_srgb`: RGB and gray in
  any curve, HSV, HSL, CIE Lab and LCH; an embedded profile is read as sRGB), and its opacity
  times the color's alpha and the filter's opacity (the effect record's FLOAT_OPACITY, the
  Opacity under the filter's blending options). Layer Knocks Out is off: GEGL draws the
  layer over its shadow.
- The filter's Clipping (FILTER_CLIP: 1 Clip, 0 Adjust) is not reproduced: the style always
  draws past the layer's bounds, as Adjust does. Clip differs only where the shadow reaches
  past the layer's edge (not in the reporter's blend-* files). A rectangle mask over the layer
  bounds with Layer Mask Hides Effects would match it, but would also cut later edits off.
- The filter's own mode mixes its result (the layer over the shadow) with the layer, so GIMP
  3.2 shows the shadow only in Normal, Replace, Behind and Merge (Patchy Normal) and Dissolve
  (Dissolve). Every other mode draws no shadow and changes only the layer's own pixels: the
  shadow opens switched off with the nearest Photoshop mode (GIMP-only modes map to the
  Photoshop mode of the same family); the change to the layer is not reproduced.
- GEGL mixes the shadow in linear light, Patchy's 8 and 16-bit documents in display values
  (85% black over white: 108 against 38). A Normal shadow there gets the contour "GIMP
  linear light" (`linear_light_shadow_contour`, 9 smooth points): at each mask level the
  display coverage that best gives GEGL's mix of its color over white, 75% and 50% gray,
  divided by the opacity, so the dialog keeps GIMP's opacity. Exact for black over white;
  a colored shadow keeps some difference, since one coverage cannot serve all three
  channels. 32-bit documents (linear already) and Dissolve (opaque pixels) get none.
  Against GIMP 3.2 (`xcf_drop_shadow_issue_507_matches_gimp_if_available`, the reporter's
  file and one file per layer in local-test-fixtures/xcf-issue-507): black shadows within
  0.1 level on average, a blurred magenta one 3.5, Dissolve 7.6 (its random pattern);
  effects.xcf 0.21. The blend-* files (filter opacity 50% and 75%): Normal 0.01, Dissolve
  9.9 with GIMP's dot count (502 against 520).
- `gegl:gaussian-blur` is applied to the layer's pixels: premultiplied linear light, clamp or
  transparent edges, the layer grown when clip-extent is off. Sigmas over 3 use three box
  passes. Within about six levels of GIMP's own blur.
- Color filters are applied to the pixels with formulas fitted on GIMP 3.2's renders of a
  4096-color sweep (`scripts/dev/xcf/make_effect_fixtures.py`, `fit_effects.py`; worst
  difference 1 level unless noted): `gegl:invert-gamma` (1 - x) and `invert-linear`;
  `gimp:desaturate` (lightness and average on display values, luma with the D50 Y weights on
  display values, luminance in linear light, value); `gimp:threshold` on the value channel;
  `gimp:posterize`; `gimp:brightness-contrast` (in linear light: brightness/2 lifts toward
  white or scales toward black, then contrast pivots at 0.5 with slope tan((c + 1) pi / 4));
  `gimp:hue-saturation` with only the master range set (HSL on display values: hue in half
  turns, saturation scaled by 1 + s, lightness toward white or black); `gimp:colorize`
  (linear luminance as HSL lightness in linear light; the lightness setting is unverified);
  `gimp:levels` (value then color channels, in linear light or display values by its trc,
  values below the input black kept linear rather than raised to the gamma; worst 2);
  `gegl:exposure` (worst 2) and `gegl:mono-mixer` in linear light.
- Every applied effect mixes with the original in linear light by the effect's opacity and,
  when its region is the selection, by its mask (when that mask has the layer's size; an empty one, no selection at the time, means the whole layer). One
  notice covers them. Text layers lose their text when an effect changes their pixels.
- Everything else (per-range hue-saturation, color balance, curves, noise, bloom, ...) is
  left out with a notice; hidden effects are ignored.

## Other content

- Channels open as alpha channels with their color and opacity; the selection opens as a
  channel under its GIMP name.
- Paths from PROP_PATHS (GIMP writes anchors 1 and controls 2 there; the spec's 0 and 1 hold
  when no point is 2), PROP_VECTORS and the version 18 path records become saved paths.
  Controls before the first anchor shape its incoming handle; after an anchor, the first
  control is its outgoing handle and a second the next anchor's incoming one.
- Guides (off-canvas ones kept from version 15), resolution (72 ppi without one), unit
  (millimeters show as centimeters, points and picas as themselves).
- Parasites: `gimp-comment` to the File Info comment, `exif-data` to Exif, `gimp-metadata`
  (`GIMP_XMP_1` + packet) to XMP, then `gimp-image-metadata` (GIMP 2.10 and 3.x keep the
  image's metadata there) fills what those left out: Exif only without `exif-data`, XMP
  properties the packet lacks, IPTC-IIM always. The open path keeps this File Info (it does
  not rescan the file, as it does flat formats).
- Application (#450): a file with no xmp:CreatorTool and no EXIF Software gets xmp:CreatorTool
  from the first xmpMM:History entry's stEvt:softwareAgent (GIMP 3 writes "GIMP 3.2.6
  (Linux)"), else "GIMP" (a packet is created if needed; the header names only the XCF
  version). A PSD save keeps it as xmp:CreatorTool; an unparseable packet is left alone.
- GIMP writes no thumbnail into the XCF (none of the issue 450 samples has one); it keeps
  thumbnails in the freedesktop cache (`~/.cache/thumbnails`).

### gimp-image-metadata

`metadata::parse_gimp_image_metadata` (`src/formats/metadata/gimp_image_metadata.cpp`) reads
the XML list of exiv2 keys, `<tag name="Xmp.dc.title">lang="x-default" Title</tag>`, with
`<namespace prefix url>` entries (exiv2's own prefixes as fallback) and `encoding="base64"`
values. A malformed list yields nothing.

- `Xmp.*` becomes an XMP packet: `lang="..."` values are lang-alt items, a repeated tag adds
  array items, `type="Bag"` (or Seq/Alt) declares an array, `[n]` and `/prefix:Field` paths
  build arrays of structs and struct fields, and properties the schema makes arrays
  (dc:subject, dc:creator, photoshop:SupplementalCategories, the IPTC Extension and PLUS
  lists) are arrays even with one tag. Empty containers are dropped, and so are the
  top-level `Xmp.iptc.Ci*` copies GIMP writes beside CreatorContactInfo. Unknown namespaces
  (DICOM) keep their prefix.
- `Exif.Image`, `Exif.Photo`, `Exif.GPSInfo` and `Exif.Iop` become a big-endian TIFF stream
  by tag name (exiv2's names are the specification's): ASCII as is, numbers and `n/d`
  rationals by the tag's type, UNDEFINED bytes from exiv2's decimal list, UserComment with
  its character code. Image-structure tags, pointers, thumbnails and the MakerNote are
  left out, as are values whose type cannot be told.
- `Iptc.Application2.*` becomes IPTC-IIM by exiv2's dataset names; dates lose their
  hyphens (CCYYMMDD) and times their colons (HHMMSS+HHMM).

## Rendering against GIMP

`scripts/dev/xcf/make_xcf_fixtures.py` builds the fixture set with GIMP 3.2 into
`local-test-fixtures/xcf/` with GIMP's merge of the visible layers beside each file
(`manifest.txt` lists what each covers); `render_with_gimp.py` renders any folder of .xcf files
the same way; `tile_report.py` breaks a mode fixture's difference down per mode;
`xcf_dump.py` prints a file's structure. The small GIMP-made files in `test-fixtures/xcf/`
(text, structure, modes, modes-legacy, groups, indexed, vector-layers, the compressed trio)
are committed.

Differences in 8-bit levels on premultiplied RGBA (mean over the canvas, worst pixel),
October 25, 2026: legacy modes 0.37 (worst 45 at soft tile edges); default modes 3.8 (the
Dissolve pattern, Color Erase and linear-light edges); every precision of the structure
scene 1.1 (8 and 16-bit) and 0.03 (32-bit, where Patchy also mixes in linear light);
groups 2.6; text 1.7; indexed 0.00; modes over a half-transparent backdrop 12 to 15 (clip to
backdrop under masks).

## Corpus

`local-test-fixtures/xcf-corpus/` (sources and licences in its README.txt, never committed):
79 files from GIMP's repositories and manual, KDE kimageformats, other readers' test data,
OpenGameArt and Wikimedia Commons, versions 0 to 23, plain and wrapped, every precision and
color mode, up to 6692 x 3000. `gimp-renders/` holds GIMP 3.2's render of each. The October
25, 2026 run: 77 open, 2 refused (files a third-party library rewrote; GIMP refuses them
too). Against GIMP's renders (profiled images converted to sRGB on both sides): 65 within a
mean of 1 level, 5 within 3, 5 within 6, and 2 beyond: a file whose filters Patchy leaves
out (bloom, shift, canvas texture) and a semi-transparent texture over a background, where
GIMP's linear-light mixing differs by 17 levels on average. The rest of the differences are
the same linear-light mixing of faded layers and soft edges, and random noise filters.
Found and fixed on the corpus: GIMP's own test files ending a tile list early, effect masks
that are empty when no selection existed, and wrapped files that sniffed as damaged.

Layer trees: `scripts/dev/xcf/dump_gimp_trees.py DIR` writes GIMP 3.2's view of every file's
tree to `DIR/gimp-trees/` (one tab-separated line per layer: depth, kind, mode, visibility,
opacity, bounds, mask, name). `xcf_layer_trees_match_gimp_if_available` opens every file in
`local-test-fixtures/xcf`, `xcf-corpus`, `xcf-indexed`, `xcf-metadata` and `xcf-issue-487` that
has a tree and compares names, nesting, order, visibility, opacity, modes, masks and bounds; files
with a visible Erase or Split layer are skipped (their groups are deliberate). To refresh after
adding files: `render_with_gimp.py DIR` and `dump_gimp_trees.py DIR` (both skip what exists).

## Tests

Core (`tests/core/xcf_tests.cpp`, filter `xcf_`, files built by `tests/xcf_test_builder.hpp`):
structure, every tile compression and pointer size, every precision, gray and indexed,
every mode number, the text parasite forms and text metadata (color models, markup fonts,
outlines; philipzae's #508/#509 files in `local-test-fixtures/xcf-issue-508`), the floating selection,
channels, paths, guides and metadata, the wrappers, hostile input, the registry and
open-by-content, a Unicode path, the committed GIMP files against GIMP's renders, the local
fixture set and corpus when present, every local file's layer tree against GIMP's, the #487
samples (Erase and Split layers, an active Split layer), the #547 bottom layer's locks, vector
layers as shapes and their fallbacks (#548); `blend_math_gimp_*` pin the GIMP mode kernels. UI
(`tests/ui/xcf_ui_tests.cpp`, filter `ui_xcf_`): opening by name, compressed name and content,
GIMP modes in the Layers panel, editable text and its PSD save, outlined text redrawn with a
Stroke style that an edit keeps.
