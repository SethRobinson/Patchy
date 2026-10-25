# Krita (.kra)

Read-only import of Krita's native documents (and `.krz`, the same container):
`src/formats/kra_document_io.{hpp,cpp}` (container, tree, pixels, build), `kra_tiles.cpp`
(LZF and the tile format), `kra_layers.cpp` (filters, fills, vector layers),
`kra_internal.hpp`. Qt-free. Registry row `patchy.formats.kra`, filter row "Krita Document";
the open path calls `kra::DocumentIo::read` itself with the file's folder so file layers
load ([file-formats.md](file-formats.md)).

## Clean-room rule

Krita has no published specification, and Krita and libkra are GPL. Everything here comes
from files Krita 5.3 writes (made by `scripts/dev/kra/make_kra_fixtures.py`, which drives
Krita through `kritarunner` and its Python scripting) and from openly licensed files found
online, unzipped and inspected as data. Never read, search or copy Krita's or libkra's
source, and never link or ship anything of Krita. Blend mode and filter behavior is measured
on Krita's own renders. Legal record: [patent-research-formats.md](patent-research-formats.md)
("Krita .kra read"); binding rule in [legal-constraints.md](legal-constraints.md).

## Container

- A zip. `mimetype` (stored, first, `application/x-krita`, so the text sits at byte 38),
  `maindoc.xml`, `documentinfo.xml`, `preview.png`, `mergedimage.png` (Krita's own render,
  8-bit; files from about 2013 and older have none), and a folder named after the image
  (`<IMAGE name>`; found by its `layers/` entries when the name does not match) holding
  `layers/`, `annotations/` (`icc`, `layerstyles.asl`, PSD resource blocks Krita keeps from
  an imported PSD) and `animation/`.
- Zip reading is the shared `formats::ZipEntryReader` (`zip_entry_reader.{hpp,cpp}`, also
  used by OpenRaster) with the OpenRaster limits ([ora.md](ora.md) "Limits"); every name
  read from the XML goes through `safe_zip_entry_path`. A pixel budget of 2^31 is taken from
  the tile counts in file order before anything decodes; past it the file is refused.
- `sniff` and `sniff_openable_image_format` look for `mimetypeapplication/x-krita` at byte 30.

## maindoc.xml

`<DOC><IMAGE width height x-res y-res colorspacename profile name>` with `<layers>`. The
first child is the uppermost, so every list reverses. Each `<layer>`: `name`, `uuid`,
`filename` (its entries under `layers/`), `nodetype`, `x`/`y` (offset added to the tile
positions), `opacity` (0..255), `visible`, `locked`, `compositeop`, `colorlabel`,
`colorspacename`, `channelflags`, `channellockflags`, `collapsed`, `selected`, `layerstyle`,
plus per-type ones below. `<masks>` holds a layer's masks; a group lists its masks among its
`<layers>`. Resolution is pixels per inch.

- Guides: `<guides><horizontalGuides|verticalGuides><item_N value>` in points: pixels =
  value x ppi / 72.
- Animation: `<animation><currentTime value>`; an animated layer has
  `keyframes="layerN.keyframes.xml"` (`<channel name="content"><keyframe time frame>` with an
  `<offset x y>`). The keyframe showing at the current time opens, with a notice.
- Channel flags: one character per channel in the color space's order, alpha last.
  `channelflags` with alpha `0` is Inherit Alpha; color `0`s are Patchy's channel
  restrictions (integer RGB is stored blue, green, red; float RGB red, green, blue).
  `channellockflags` with alpha `0` is the alpha lock (Lock Transparency); `locked="1"` is
  Lock All.
- Color labels in Krita's menu order: 1 blue, 2 green, 3 yellow, 4 orange, 5 brown (opens
  orange), 6 red, 7 purple, 8 gray.

## Tiles

A paint device file: text header lines `VERSION 2`, `TILEWIDTH 64`, `TILEHEIGHT 64`,
`PIXELSIZE n`, `DATA count`, then per tile a line `x,y,LZF,size` and `size` bytes: a flag
byte (1 LZF, 0 stored) and the tile's samples, planar by byte (every pixel's first byte,
then every second byte, ...). LZF is liblzf's stream format, decoded by
`detail::lzf_decompress`. The `.defaultpixel` entry fills everything no tile covers; a
non-transparent one (Krita's white "Background") makes the layer cover the canvas. A
layer's bounds are the union of its tiles.

Color spaces (`colorspacename`): `RGBA` (8-bit, stored blue, green, red, alpha), `RGBA16`,
`RGBAF16`, `RGBAF32` (red, green, blue, alpha), `GRAYA`, `GRAYAU16`, `GRAYAF16`,
`GRAYAF32`, `CMYK`/`CMYKA16` (ink amounts, 0 = no ink, then alpha), `LABA` (16-bit ICC Lab).
Masks and selections are one byte per pixel.

## Depth and color

- Integer samples whose profile has a linear tone curve (Krita's `-g10` profiles, the default
  for its 16-bit scripting documents) and float samples are linear light: they open in a
  32-bit document, where Patchy composites in linear light as Krita does. Otherwise 16-bit
  samples open at 16 bits, 8-bit at 8. With deep editing off everything opens at 8 bits.
  A profile is linear when it maps mid gray to more than 160 in sRGB.
- An RGB image profile that is not sRGB becomes the document's embedded profile; the open
  path converts it ([rgb-profiles.md](rgb-profiles.md)).
- Grayscale opens as RGB; CMYK converts through the layer's own profile
  (`CmykToRgbTransform`, the working profile when it has none); Lab through Little CMS.

## Mapping

- Paint layers: pixel layers. Groups: groups; `passthrough="1"` is Pass Through.
- Transparency masks: the layer mask (several multiply into one, with a notice). Selection,
  transform and colorize masks are left out with a notice.
- Inherit Alpha keeps a layer inside the alpha of everything below it in its stack. When the
  stack below is one clipping run it opens as a clipping mask; otherwise the layers below are
  grouped as "Inherit Alpha base" first, so the clip takes them all. Above an opaque
  full-canvas layer (white Background, or a painted layer opaque over the whole canvas) it
  changes nothing and opens unclipped; inside a pass-through group the stack below the group
  counts too. Patchy never clips a group, so an Inherit Alpha group is composited into one
  clipped pixel layer. A pass-through group at full opacity without masks whose bottom layer
  inherits alpha (Krita clips it to the layers below the group) opens as its layers in place.
- Filter layers (`adjustmentlayer`, compositeop `copy`) and filter masks: adjustment layers
  where one maps, with the selection as their mask; a filter mask becomes a clipped
  adjustment right above its layer (on a group: the group's top child). Sharpen, unsharp and
  gaussian blur, which have no adjustment, are drawn: a filter mask filters its layer's
  pixels within its selection, and a filter layer opens as a Normal pixel layer holding the
  layers below it, composited and filtered, with the filter layer's opacity and selection.
  See "Filters".
- Fill layers (`generatorlayer`): `color` becomes a fill layer with the selection as its
  mask; pattern, gradient, screentone, multigrid, simplex noise and SeExpr open as empty
  layers with a notice (Krita stores no pixels for them).
- Vector layers: `layerN.shapelayer/content.svg`, in points with a matching viewBox. The root
  is resized to the canvas in pixels and the SVG importer makes shape and text layers
  ([svg.md](svg.md)); one shape opens as that layer, several as a group. Text renders after
  the open like SVG text.
- Clone layers: a copy of the source layer's pixels at the clone's offset (`clonefromuuid`,
  else `clonefrom`). File layers: the PNG/JPEG at `source` (relative to the .kra) as pixels;
  without the file, or without the folder (registry reads), an empty layer and a notice.
- Layer styles: `annotations/layerstyles.asl` is Photoshop's ASL; each style's id is the
  `layerstyle` uuid. Read with `psd::read_asl`, patterns adopted into the document; the
  style's blending options give the layer's Fill and Blend If. Effects set to use global
  light take the angle and altitude Krita keeps from an imported PSD
  (`annotations/PSD Resource Block_ 1037` and `_ 1049`, big-endian degrees), else 120 and 30.
  Krita renders effects with its own engine; Patchy renders Photoshop's look, so the
  fixtures made from Patchy's PSDs are judged against Patchy's render of the PSD.

## Blend modes

Krita's ids (`compositeop`) map to Patchy's: `normal`, `multiply`, `screen`, `overlay`,
`darken`, `lighten`, `dodge` (Color Dodge), `burn` (Color Burn), `hard_light`, `diff`,
`color`, `hue`, `saturation`, `luminize` (Luminosity), `add` and `linear_dodge` (Linear
Dodge), `xor` and `negation` (Paint.NET's formulas exactly), and every id OpenRaster's table
knows as `krita:<id>` ([ora.md](ora.md)). Near modes, with a notice: `soft_light_svg`
(Krita's menu "Soft Light", the W3C formula: up to 17 levels from Photoshop's where the
backdrop is below a quarter), `reflect` and `glow` (Paint.NET's, within 11 levels),
`luminosity_sai` (SAI's Luminosity/Shine adds the layer times its alpha: Linear Dodge wherever
the sum stays below white). Krita 5.3 accepts 125 ids; they were found by rendering each
candidate against the Normal fallback it uses for unknown ids. Unknown ones open as Normal
with a notice. Krita's HSV, HSL, HSI and HSY hue, saturation and color modes are not
Python's colorsys formulas (measured), so GIMP's HSV modes do not stand in for them.

## Filters

Settings are `.filterconfig` XML: `<params version><param name>value</param>`.

| Krita filter | Patchy |
|---|---|
| `invert` | Invert |
| `levels` | Levels from `lightness` ("black;white;gamma;outblack;outwhite", 0..1) or the 0..255 keys of version 1. Krita's lightness mode keeps the colors: the adjustment layer gets Luminosity (approximate) |
| `perchannel` | Curves. Krita 5 stores 8 curves for RGB: composite, red, green, blue, alpha, hue, saturation, lightness (measured); the last four have no Curves twin. Older files' first curve goes to the composite |
| `hsvadjustment` | Hue/Saturation (approximate) |
| `desaturate` | type 0 (lightness) Hue/Saturation -100; types 1 to 3 (BT.709, BT.601, average) a monochrome Channel Mixer; 4 and 5 (min, max) approximate |
| `colorbalance` | Color Balance (approximate). The second slider (`magenta_blue`, Krita 4 `magenta_green`) moves green; the third (`yellow_green`, Krita 4 `yellow_blue`) moves blue the other way round |
| `threshold` | Threshold (approximate) |
| `posterize` | Posterize with steps + 1 levels (approximate) |

Drawn by Patchy (premultiplied, edges clamped, at the document depth in its own domain),
measured on impulse renders: `sharpen` is the 3 x 3 kernel 1/3 [0 -2 0; -2 11 -2; 0 -2 0];
`gaussian blur` uses sigma = 0.3 x radius + 0.3, truncated at the radius; `unsharp` adds
`amount` x (pixel - that gaussian of `halfSize`) where the difference passes `threshold`, on
lightness only when `lightnessOnly` (the default). Everything else (box blur, noise,
pixelize, emboss, gradient map, ...) is left out with a notice.

## Fixtures and corpus

`scripts/dev/kra/make_kra_fixtures.py [out_dir] [names]` writes `local-test-fixtures/kra/`:
structure at 8/16/half/float bits, grayscale, CMYK, one tile per blend mode, masks, filter
layers and masks, filter settings, curves, channel flags, fill layers, clone and file layers,
vector shapes and text, guides and resolution, a ClayRGB profile, a 3000 x 2000 canvas, and
Krita's saves of Patchy's own layer-style PSDs (`psd-*`). `<name>.png` is Krita's
`mergedimage.png`: its command-line export renders fill and text layers blank and waits on a
question for file layers. Mode tables (`table-*`, a mode's whole function over gray levels)
go elsewhere and are analysis data.

The corpus (`local-test-fixtures/kra-corpus/`, sources and licences in its README.txt, not
committed): 65 files under CC0, CC-BY, CC-BY-SA, MIT and Apache licences from
Pepper&Carrot, GDQuest, OpenGameArt and GitHub, Krita 2.x to 6.1.

## Tests

Core (`tests/core/kra_tests.cpp`, filter `kra_`): LZF, a synthetic file built in the test
(tree, offsets, BGRA order, stored and LZF tiles, masks, guides, Inherit Alpha, locks,
labels), sniffing and the registry, the Unicode path with a file layer beside the file,
hostile and damaged input, the Krita fixtures and the corpus against Krita's merged image
(mean and worst in 8-bit units on premultiplied RGBA; `psd-*` also against Patchy's render of
the source PSD).
