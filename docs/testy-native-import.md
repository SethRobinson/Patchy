# Testy native import benchmark

Testy opens XCF, Aseprite, KRA and OpenRaster files in headless Patchy. Each file gets an open result, its import notes, a PNG export and independent checks for document properties, editable structure, appearance and animation timing. The [PSD benchmark](testy.md) uses Photoshop as its reference and measures what survives an editor's PSD save.

## Prepare a corpus

From the repository root on Windows, use a Python environment with [Testy's dependencies](../testy/requirements.txt).

```bat
scripts\run-throttled.bat python testy\fetch_native_corpora.py
scripts\run-throttled.bat python testy\local_import_corpus.py
```

The first command downloads pinned test data into `local-test-fixtures/testy-native`, verifies the fixture digests and writes three lists under `testy/corpus`. Run it with `--offline` to verify the local copies and regenerate the lists.

- `native-imports.json` covers 54 GIMP XCF documents plus three compression variants, 43 asefile sprites, one Aseprite 1.3.17 blend-mode swatch document and three Krita documents. The PhotoCraft XCFs have author-provided GIMP PNGs. Each asefile reference identifies the frame it describes. The six swatches have a separate PNG exported by Aseprite 1.3.17. Three 2021 asefile Hue and Saturation PNGs preserve the older treatment of colors with equal channels. The report identifies those historical references. [Aseprite 1.3.13 release notes](https://github.com/aseprite/aseprite/releases/tag/v1.3.13). Timing and layer expectations come from the source file. The Krita references are saved renders inside the documents. Pins, attribution and reference limitations are recorded in the manifest or fetcher. [PhotoCraft fixtures](https://github.com/storytold/photocraft-corpus), [asefile test data](https://github.com/alpine-alpaca/asefile/tree/2274c354cea6764f85597252a0d2228e64709348/tests/data), [Krita fixtures](https://github.com/KDE/krita/tree/v5.2.6/plugins/impex/libkra/tests/data).
- `native-aseprite-upstream.json` covers 20 of Aseprite's own test sprites. It checks editable layer trees and document properties, plus timing for animations. These fixtures have no paired frame PNGs, so their appearance stays unscored. The case notes identify tag and slice data outside the measured properties. [Official fixture descriptions](https://github.com/aseprite/aseprite/blob/eccf4ec292f66c0b8a4f814954ce7db160a8eb43/tests/sprites/README.md).
- `photocraft-psd.txt` lists 258 Photoshop-authored PSDs, including adjustment layers, text, effects and Smart Filters. Run `testy/testy.py --editors patchy --corpus corpus\photocraft-psd.txt` to compare Patchy directly with Adobe Photoshop. The corpus supplies the input files. Photoshop supplies the reference images and judges Patchy's saved PSDs.

The optional second command writes `native-local.json` from installed [XCF](xcf.md#corpus), [KRA](kra.md#fixtures-and-corpus) and [OpenRaster](ora.md#corpus) collections. It pairs XCFs with GIMP's renders and layer trees and lists files GIMP refused. For KRA and ORA, it reads size-limited document XML to check canvas dimensions. It also includes Patchy's GIMP- and Krita-authored calibration collections, with the GIMP merges saved beside their sources and Krita's embedded merged images. Plain paint layers and groups get checks for names, order, nesting, visibility, and any recorded opacity and edit locks. The manifest records why a tree was omitted or a referenced companion is missing. Appearance uses the embedded composite when its dimensions match the canvas. The existing OpenRaster sweep also tests saves and exchange with other editors.

## Run and inspect results

On the Testy dashboard, choose a corpus under **Patchy native import** and click **Run import checks**. The dashboard lists progress and links completed reports. **Stop after this file** writes a partial report. A stopped native run can be started again as a fresh run.

```bat
scripts\run-throttled.bat python testy\testy.py native-import testy\corpus\native-imports.json
```

The CLI measures the existing `build/opt-dev/patchy.exe`. To investigate one file, append `--case aseprite/tilemap` or repeat `--case` for several IDs. `--patchy` selects a different executable, `--timeout` sets the per-file limit in seconds and `--output` selects a new run directory. The default is a unique `testy/runs/native-...` directory. Each case opens a temporary copy with any declared sidecars in a dedicated Patchy process using isolated settings. The report records the binary and manifest SHA-256 hashes, the measured source hashes and Patchy's reported version.

Open `report.html` to inspect individual files, import notes, failed checks and renders. `results.json` contains the same measurements for scripts. A completed benchmark can contain compatibility failures. The exit code reports whether the runner finished. Each check shows passes out of eligible comparisons and how many files were unscored. A missing input, an absent reference and a measured mismatch have different outcomes.

## Write a manifest

Paths in the JSON file are local and relative to the manifest directory unless absolute. Give each case a unique `id` when the displayed names would otherwise be ambiguous. `assets` lists companion files below the source's directory. Testy preserves their relative positions when staging.

```json
{
  "name": "Import checks",
  "cases": [
    {"id": "masked", "source": "fixtures/masked.xcf", "reference": "gimp/masked.png",
     "expect": {"width": 64, "layers": [{"name": "Backdrop"}, {"name": "Ink", "hasMask": true}]}},
    {"id": "walk", "source": "fixtures/walk.aseprite",
     "frames": ["frames/walk-1.png", "frames/walk-2.png"], "durationsMs": [80, 120]},
    {"id": "painting", "source": "fixtures/painting.kra", "reference": "@mergedimage"}
  ]
}
```

- **Document properties** compare the supplied width, height, depth and profile. **Editable structure** checks the remaining supplied properties. Objects can specify only the fields to check. Arrays check exact length and order. `layers` and `children` are bottom-to-top. A layer can specify its name, kind, visibility, opacity in percent, lock, bounds, blend mode, children and presence of a raster mask. `layerCount` counts top-level layers. `paths` records names and kinds. Dimension checks alone leave editable structure unscored.
- **Appearance** accepts a single PNG, an ordered `frames` array or `@mergedimage` for the saved KRA or ORA composite. A null frame entry records an unavailable reference. A reference is never resized to fit. A saved composite with dimensions inconsistent with its source is ineligible. A render with the wrong size against a valid reference fails. An 8-bit preview can measure appearance only at that precision. Missing features can also show up in import notes even when pixels match.
- **Appearance after reopen** optionally compares the same still image with a PNG an independent editor exported after opening the saved file. Supply `"reopenReference": {"png": "reopen/modes.png", "credit": "Krita 5.3 reopening the GIMP ORA"}`. The local generator pairs the three GIMP structure and blend-mode fixtures with their saved Krita reopen PNGs. The authoring composite keeps its own result and denominator. An editor can preserve more in its merged preview than it wrote into the editable layer stack. [OpenRaster measurements](ora.md#tolerances).
- **Sprites and timing** compare visible RGBA pixels exactly in every referenced frame. RGB differences behind fully transparent pixels are ignored. To show the magnitude of each discrepancy, the report also lists the largest channel difference and the number of pixels exceeding 6/255, both for straight RGBA and for RGB weighted by alpha. Alpha itself keeps its full weight in both measurements. A multi-frame case uses Patchy's animated PNG export to check the exported frame count, order and millisecond delays. For a still sprite Testy measures the composite of its visible layers. A Krita animation with a saved composite provides a reference for its saved frame only.
- **Linear HDR samples** compare numeric values from a 32-bit TIFF exported by Patchy. A manifest can supply `"linearSamples": [{"x": 0, "y": 1, "rgba": [-0.125, 0.5, 4, 1]}]` in canvas coordinates. RGB is straight linear color and alpha is coverage. Each channel must agree within an absolute tolerance of 0.0000001 or a relative tolerance of 0.000002. RGB at zero expected alpha is ignored. The public corpus samples four XCFs against the generator’s original values, including negative colors, values above one, and partial opacity. A depth downgrade fails this check. An unavailable float export remains unscored. [Fixture definitions](https://github.com/storytold/photocraft-corpus/blob/aeb450e694d0238dd317cb5c36ddca757d0acf44/tools/gimp-xcf/xcf_fixtures.py).
- **Still images** use Testy's perceptual comparison over white and a separate alpha comparison. An alpha difference above 6/255 counts toward that comparison. A file passes when each comparison flags at most 10% of the pixels. Differences and reasons remain visible per file. The result describes the supplied preview under its color assumptions. Native HDR values use the separate linear sample check.
