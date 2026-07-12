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
