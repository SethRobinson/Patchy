# Third-Party Notices

Patchy's current Windows release package uses the Qt 6 desktop runtime and
app-local Microsoft Visual C++ runtime DLLs. Planned libraries that are not
linked into the application are intentionally not listed here.

## Qt 6

The Windows application dynamically links Qt 6 modules from the local Qt
installation used for the release build:

- Qt Core
- Qt GUI
- Qt Widgets
- Qt PrintSupport
- Qt SVG
- Qt ImageFormats

The package also includes the Qt plugins needed by the current app:

- `platforms/qwindows.dll`
- `styles/qmodernwindowsstyle.dll`
- `iconengines/qsvgicon.dll`
- `imageformats/qjpeg.dll`
- `imageformats/qsvg.dll`
- `imageformats/qtiff.dll`
- `imageformats/qwebp.dll`

Qt is available under a commercial Qt license or under open-source licenses.
This local zip uses dynamic Qt DLLs and includes the Qt module SPDX notice files
under `licenses/qt/` so downstream review can see the exact Qt build metadata,
third-party components, and license text extracted by Qt.

## Microsoft Visual C++ Runtime DLLs

The Windows package includes the app-local Microsoft Visual C++ runtime DLLs
from the local Visual Studio redistributable CRT directory. These DLLs are
provided under Microsoft's runtime redistribution terms.

## Noto Naskh Arabic

Patchy includes Noto Naskh Arabic Regular and Bold from the Noto Arabic fonts
project for Photoshop text-layer compatibility. The font files are distributed
under the SIL Open Font License, Version 1.1; the license text is included at
`fonts/noto_naskh_arabic/OFL.txt` in the release package.

## miniz

`src/formats/miniz/` vendors miniz 3.0.2 (https://github.com/richgel999/miniz),
the single-source zlib/deflate implementation used by the Aseprite file format's
compressed cels. MIT License; the license text is included at
`src/formats/miniz/LICENSE`.

## Little CMS

`src/color/lcms2/` vendors the Little CMS 2.17 core library
(https://github.com/mm2/Little-CMS), used to convert CMYK PSD documents through
their embedded ICC color profile on import. Only the MIT-licensed core is
vendored (the optional lcms2 speed plugins are GPL-licensed and are not
included); the license text is included at `src/color/lcms2/LICENSE`.

## LibRaw

`src/formats/libraw/` vendors LibRaw 0.22.1 (https://www.libraw.org,
Copyright (C) 2008-2025 LibRaw LLC), the camera raw decoder and develop
pipeline behind opening CR2/CR3/NEF/ARW/RAF/DNG and other camera raw files.
LibRaw is dual-licensed and may be used under either the GNU LGPL 2.1 or the
CDDL 1.0; Patchy uses it under the CDDL 1.0. Only the stock release tarball is
vendored: the separate LibRaw demosaic-pack repositories are GPL-licensed and
are not included. LibRaw itself incorporates code from dcraw (Dave Coffin,
non-restricted portions), the BSD-licensed DCB demosaic and FBDD denoise
(Jacek Gozdz), the BSD-licensed X3F tools (Roland Karlsson), and MIT-licensed
Adobe DNG SDK fragments. The license texts are included at
`src/formats/libraw/LICENSE.CDDL`, `src/formats/libraw/LICENSE.LGPL`, and
`src/formats/libraw/COPYRIGHT`.

## Bundled photo-texture pattern presets (Poly Haven, CC0)

The 20 photo-based pattern presets under the "Textures" folder (embedded as
512x512 PNG tiles in `src/ui/textures/`, ids in `src/core/pattern_presets.cpp`)
are downscaled diffuse maps from Poly Haven (https://polyhaven.com), published
under CC0 ("You can use our assets for any purpose, including commercial work.
You do not need to give credit or attribution"). All are real photo-scanned
surfaces created by named human artists (no AI generation, per the AGENTS.md
sourcing rule); the 1K PNG masters were fetched from dl.polyhaven.org on
2026-07-12 and downscaled losslessly (no JPEG step) with a seam-preserving
3x3-tile filter.

| Patchy preset | Poly Haven asset | Author(s) |
| --- | --- | --- |
| Fine Wood Grain | fine_grained_wood | Rob Tuytel |
| Dark Walnut | dark_wood | Dario Barresi, Dimitrios Savva, Rico Cilliers |
| Oak Veneer | oak_veneer_01 | Jenelle van Heerden |
| Weathered Wood | rough_wood | Rob Tuytel |
| Old Planks | old_planks_02 | Rob Tuytel |
| Medieval Wood | medieval_wood | Rob Tuytel |
| Tree Bark | bark_brown_01 | Rob Tuytel |
| Weathered Marble | marble_rock_01 | (Poly Haven) |
| Slate Slabs | slab_tiles | (Poly Haven) |
| Granite Blocks | japanese_stone_wall | (Poly Haven) |
| Rock Face | rock_face | (Poly Haven) |
| Coarse Rust | rust_coarse_01 | Dimitrios Savva, Rico Cilliers |
| Steel Plate | metal_plate | Rob Tuytel |
| Brown Leather | brown_leather | Rob Tuytel |
| Denim Weave | denim_fabric | (Poly Haven) |
| Burlap | hessian_230 | (Poly Haven) |
| Rippled Sand | damp_sand | (Poly Haven) |
| Snow | snow_02 | (Poly Haven) |
| Cracked Earth | mud_cracked_dry_03 | (Poly Haven) |
| Mossy Forest Floor | forest_leaves_02 | (Poly Haven) |

Each asset page is `https://polyhaven.com/a/<asset id>`. CC0 requires no
attribution; the authors are credited here voluntarily (authors marked
"(Poly Haven)" are listed on the asset pages).

## Test fixtures (not distributed with the application)

Files under `test-fixtures/` are used only by the automated test suites and are
not part of any release package.

- `test-fixtures/readme/san_francisco_cityscape_cc0.jpg`: "Cityscape of San
  Francisco," photographed by Vladimir Mokry and published under the Creative
  Commons CC0 1.0 Universal Public Domain Dedication. The exact Wikimedia
  Commons original was downloaded on 2026-07-15 from
  https://commons.wikimedia.org/wiki/File:Cityscape_of_San_Francisco.jpg
  (SHA-256 `8B09FA97853316DC4C548C7A1631D4C0955148338DF7CA2E75BAA5BC576DDE0D`).
  It is used only to regenerate the README Tilt-Shift Blur screenshot.
- `test-fixtures/svg/hot_air_balloons_cc0.svg`: "Hot Air Balloon Scene," a
  vector illustration published under the Creative Commons CC0 1.0 Universal
  Public Domain Dedication (uploader OpenClipart). The exact file was
  downloaded on 2026-07-18 from https://freesvg.org/hot-air-balloon-scene
  (SHA-256 `72F141E0F46C71E646349B56B28812C17653AF045739C89F73C0D02B44021DA4`).
  It is used only to regenerate the README SVG-import screenshot and by the
  SVG import tests.
- `test-fixtures/ico/cpython-py.ico`: the CPython `py.ico` application icon
  from https://github.com/python/cpython (`PC/icons/py.ico`), included under the
  Python Software Foundation License 2.0 as a real-world multi-size icon sample.
- `test-fixtures/ico/vscode-code.ico`: the Visual Studio Code application icon
  from https://github.com/microsoft/vscode (`resources/win32/code.ico`),
  included under the MIT License as a real-world PNG-entry icon sample.
- `test-fixtures/ico/pillow-*.ico` / `pillow-cursor.cur`,
  `test-fixtures/tga/pillow-*.tga`, and `test-fixtures/gif/pillow-animated.gif`:
  generated locally with the Pillow imaging library (self-authored art; no
  third-party content).
- `test-fixtures/aseprite/*.aseprite` and
  `aseprite-blend-modes-reference.png` (Aseprite's own flattened render of the
  blend-mode fixture): authored locally with Aseprite 1.3.17 via a batch script
  (self-authored art; no third-party content).
- `test-fixtures/pat/hue.pat`: a real Photoshop pattern fixture from Jaroslav
  Bereza's `jardicc/pat-parser` repository, included under the MIT License.
  The pinned source URL, checksum, copyright notice, and full license text are
  in `test-fixtures/pat/NOTICE.txt`.
- `test-fixtures/heif/quadrants.heic`: encoded from a self-authored
  quadrant-color PNG with Apple's `sips` tool on macOS (self-authored art; no
  third-party content).

## Built-in color palette presets

The palette presets bundled for the palettized editing mode fall into two
groups. Hardware palettes (NES/2C02, Commodore 64 in Pepto's calibrated
rendering, Game Boy, CGA/EGA, the DOS/VGA mode-13h default DAC table, ZX
Spectrum, MSX/TMS9918, Amstrad CPC) are RGB renderings of hardware color
generation and are factual data, not copyrightable works. Community palettes
are included only where the author allows free use: the PICO-8 palette
(Lexaloffle explicitly permits using the PICO-8 palette in any work),
DawnBringer's DB16/DB32 palettes (published freely by their author on the
Pixelation forums and mirrored on Lospec), and the Dink Smallwood game palette
(RTsoft's own title, included by its author). All preset tables are generated
in code at `src/core/palette_presets.cpp`; no palette files are redistributed.
