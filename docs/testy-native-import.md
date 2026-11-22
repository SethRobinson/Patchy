# Testy native import benchmark

Testy opens XCF, Aseprite, KRA and OpenRaster files in headless Patchy. Each file gets an open result, its import notes, a PNG export and independent checks for document properties, editable structure, appearance and animation timing. Other editors can open the same files as columns, and Photoshop can judge the PSD each editor converts a file to: see [Compare editors](#compare-editors). The [PSD benchmark](testy.md) uses Photoshop as its reference and measures what survives an editor's PSD save.

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

On the Testy dashboard, choose a corpus under **Native format import** and click **Run import checks**. The dashboard lists progress and links completed reports. **Stop after this file** writes a partial report. A stopped native run can be started again as a fresh run.

```bat
scripts\run-throttled.bat python testy\testy.py native-import testy\corpus\native-imports.json
```

The CLI measures the existing `build/opt-dev/patchy.exe`. To investigate one file, append `--case aseprite/tilemap` or repeat `--case` for several IDs. `--patchy` selects a different executable, `--timeout` sets the per-file limit in seconds and `--output` selects a new run directory. The default is a unique `testy/runs/native-...` directory. Each case opens a temporary copy with any declared sidecars in a dedicated Patchy process using isolated settings. The report records the binary and manifest SHA-256 hashes, the measured source hashes and Patchy's reported version.

Open `report.html` to inspect individual files, import notes, failed checks and renders. `results.json` contains the same measurements for scripts. A completed benchmark can contain compatibility failures. The exit code reports whether the runner finished. Each check shows passes out of eligible comparisons and how many files were unscored. A missing input, an absent reference and a measured mismatch have different outcomes.

## Compare editors

```bat
scripts\run-throttled.bat python testy\testy.py native-import testy\corpus\native-imports.json --editors gimp,krita,photodemon,photocraft --psd-judge --reference installed
```

Patchy is always measured. `--editors` adds GIMP, Krita, PhotoDemon, PhotoCraft or Photopea as columns; each opens the staged file and exports a PNG (`testy/native_editors.py`, on the PSD benchmark's [drivers](testy-editor-drivers.md)). The dashboard offers the same choices as checkboxes. Photopea needs the dashboard server, so a command-line run names it with `--server-url`. Photoshop and psd-tools read none of these formats and are not columns. A plain run without these options measures and reports exactly what it did before. Each case gains a `cells` object in `results.json`, and the report an "Editors by format" table per format.

- **Opened.** Every file is attempted in every editor, including a format an editor has no reader for: a probe file cannot tell "no reader" from "cannot read these files", and a reader can depend on the build (the Krita 5.3.4 Windows package has no XCF import plug-in), so the table states what was measured, such as 0 / 57, and marks a format an editor opened none of. A refusal, a crash and a hang count against the editor. A failure to start the editor, or one on Testy's side, gives no verdict. Three crashes, hangs or launch failures in a row skip the rest of that editor's column; refusals never do.
- **Render match** compares the editor's PNG with the case's reference under the rules below. For an animation only Patchy exports every frame; another editor's single PNG is compared with the first frame, and the cell says so.
- **Converted PSD** (`--psd-judge`). Each editor that opened a file also saves a PSD, and Photoshop opens it (`testy/native_psd_score.py`). "Photoshop opens the PSD" fails on a refusal; dialogs it raised are listed as warnings; a Photoshop that hung, crashed or never started gives no verdict. "PSD render match" holds Photoshop's render of the PSD to the same reference by the still-image rule, with both over white and no alpha comparison (Photoshop renders a flattened copy, which has no transparency); an animation has no single picture and is unscored. A 32-bit PSD's picture is Photoshop's copy-merged render (`fallback` in the probe) and is scored like any other. "All layers kept" flattens the manifest's `expect.layers` tree and pairs it with Photoshop's layer list by name, then stack order: a group must still be a group, text still a type layer, any other layer still a layer. Nesting, visibility, masks, opacity (within 1%) and blend mode are counted beside it. A source blend mode Photoshop has no name for is never expected, so an editor may keep such a layer or bake its look: the render and layer columns show which it chose. Cases without a layer tree are unscored. Patchy's PSD comes from its scripted save, which keeps layers and writes an unsupported mode as Normal.
- **References.** `--reference pinned` (the default) scores against what the manifest supplies. `--reference installed` has the GIMP on this machine render each XCF (the render [xcf.md](xcf.md#corpus) describes) and Aseprite each sprite frame, which also gives the upstream sprites a reference. The supplied PNG becomes a cross-check: when the two disagree by the rule that scores an editor, the file's appearance is unscored for everyone and the case says why. A file the app cannot render, or renders with a different frame count, keeps its supplied reference. KRA and OpenRaster always use the composite saved in the file. The report names each format's reference source and marks GIMP and Krita as their format's own app.
- **Patchy save reopened in Aseprite.** With installed references and Aseprite present, Patchy also saves each still sprite as .aseprite, and Aseprite's render of that save must match the reference exactly.

The Photoshop leg follows the PSD benchmark's rules for the reference Photoshop. Run it only where that Photoshop may be driven: the driver restarts a wedged Photoshop.

Aseprite is found through `editors.aseprite` in `config.local.json` or its standard install folders. It is never a column, because it has no PSD support. GIMP, Krita and Aseprite run only as command-line tools; nothing in Testy reads their source.

Editor output is cached in `testy/cache/native` by source hash (sidecars included) and editor version, with Photoshop's probe of each cached PSD; reference renders go to `testy/cache/native-ref`. A cell is cached when it opened, rendered and saved what was asked, or when the editor cleanly refused the file; a crash or a hang is measured again. Scores are computed again every run, and Patchy is always measured afresh. `--fresh` ignores the cache.

## Scores

A cross-editor run ends with scores (`testy/native_scoring.py`), computed from the stored measurements: `python testy\testy.py native-rescore testy\runs\<run>` scores a finished run again and rewrites its report, so a rule change never needs the editors. `results.json` holds the averages under `scores` (per format and for all files) and each file's entries under its case's `scores`.

Each file gives an editor up to four components from 0 to 1:

| Component | Value |
|---|---|
| `render` | Share of the editor's own picture that matches the reference. Stills: 1 minus the worse of the perceptual and alpha bad fractions. Sprites: 1 minus the share of visible pixels that differ at all. Averaged over the frames that have a reference; a frame the editor never rendered counts 0. |
| `psdValid` | 1 when the editor saved a PSD and Photoshop opened it without a warning, else 0. |
| `psdRender` | Share of Photoshop's picture of that PSD that matches the reference. |
| `layers` | Share of the source's layers that are still layers of their kind in the PSD. |

Two headline scores are means over files. **Import** is `render`: how well the editor shows the file. **To PSD** is the mean of the PSD components eligible for the file: how well the editor converts it. They stay separate because an editor can do one well and not the other.

Each kind of failure has one place:

- **Did not open, crashed or hung:** 0 on every eligible component, as a refused file is a 0% match in the [PSD benchmark](testy-scoring.md#files-an-editor-refuses).
- **Opened, no picture:** `render` 0.
- **Picture differs:** `render` drops by the share of wrong pixels, so a file 16% off keeps 84%.
- **No PSD saved, or Photoshop rejects it:** 0 on all three PSD components.
- **PSD opens with a warning:** `psdValid` 0; the picture and the layers are still scored.
- **Layers lost or the PSD's picture differs:** the component drops by the share lost.
- **Not measured** (the editor did not start, a skipped column, a Photoshop that could not be asked): out of that editor's averages. The tables show how many files each average covers.
- **A check no editor could be scored on** (no usable reference, no layer tree, an animation's PSD picture): not eligible for that file, so it marks nobody down, including an editor that did not open the file.

The report also puts every file in one outcome column per editor, its first problem in pipeline order: not measured, crashed or hung, did not open, opened without a picture, picture differs, no PSD saved, Photoshop rejects the PSD, PSD opens with a warning, PSD picture differs, layers lost, no problem found. A picture counts as differing by the pass rule of the checks above (10%), while the score uses the measured share.

The all-files average weights every file equally, so the format with the most files dominates it; compare editors per format first. `SCORING_VERSION` in the module changes with the rules.

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
