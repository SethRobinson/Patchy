# PSD compatibility benchmark

Patchy's Testy harness measures PSD interoperability against a licensed copy of Adobe Photoshop. It tests visual rendering and the editability of a PSD after another editor saves it. Those are separate questions: a file can look correct while text, effects, masks, or other native data have been flattened or changed.

This page records a paused snapshot from run `20260727-211731`. The run remains unfinished.

## Current snapshot

- Snapshot date: July 28, 2026
- Corpus size: 2,175 PSD files
- Completed at pause: 258 to 260 files per editor
- Reference: Adobe Photoshop 27.8.0
- Patchy build: `d5a5ed0`
- Comparison mode: perceptual

| Editor and tested build | Type | Rendered files | Perceptual render match | PSD saves rejected by Photoshop | Editable text kept | Native objects kept | Photoshop round-trip render |
|---|---|---:|---:|---:|---:|---:|---:|
| Photoshop 27.8.0 | Proprietary reference | 260 | 100.00% | 0 / 260 | 1,439 / 1,439 | 6,325 / 6,325 | 100.00% |
| Photopea, web build in Chrome | Proprietary web app | 259 | 99.77% | 1 / 259 | 1,417 / 1,439 | 6,090 / 6,323 | 99.59% (n=257) |
| **Patchy `d5a5ed0`** | **Open source** | **260** | **99.57%** | **0 / 260** | **1,438 / 1,439** | **6,308 / 6,325** | **99.82%** |
| GIMP 3.2.4 | Open source | 259 | 95.78% | 0 / 259 | 0 / 1,439 | 2,646 / 6,323 | 96.43% |
| Affinity 3.2.3.4646 | Proprietary | 258 | 95.24% | 0 / 258 | 0 / 1,434 | 4,023 / 6,291 | 94.44% (n=257) |
| PhotoDemon 2026.01.0251, Testy CLI build | Open source | 258 | 94.56% | 0 / 258 | 0 / 1,403 | 2,916 / 6,253 | 93.71% |
| Krita 5.3.2.1, git `0619060` | Open source | 259 | 89.54% | 97 / 259 | 663 / 1,439 | 2,706 / 6,042 | 92.58% (n=161) |

The pause landed between editor jobs, so editors completed slightly different file sets. Preservation denominators therefore differ. A round-trip sample count is shown when it is smaller than the render count.

## What the snapshot says about Patchy

- Patchy had the highest perceptual render match among the open-source editors tested.
- Photoshop reopened all 260 PSD files saved by Patchy.
- 1,438 of 1,439 Photoshop text objects remained text objects after the Patchy save.
- All 1,148 tested live effects were retained.
- 1,290 of 1,321 user and vector mask structures were retained. Mask preservation is a remaining compatibility gap.
- When Photoshop reopened and rendered Patchy's saved PSDs, the mean perceptual match was 99.82%.

The other editors have different strengths. Photopea had the closest non-Adobe render in this snapshot. GIMP and PhotoDemon produced no rejected saves, but converted the tested text to raster data and retained none of the tested live effects. Affinity retained 924 of 1,146 live effects and 1,134 of 1,320 mask structures, but none of its 1,434 tested text objects remained editable text in the exported PSD. Krita opened every tested source PSD, but Photoshop rejected 97 of its 259 saved PSDs.

## Methodology

### Source safety and staging

For every PSD and editor pair, Testy copies the source into a run directory and gives the editor that private copy. It verifies the original file's hash after the test. The source corpus is never opened for writing.

### Photoshop reference render

Photoshop 27.8.0 renders each source PSD to a flattened PNG over white at the document's declared size. Each tested editor renders the same PSD independently. Testy then compares the editor PNG with the Photoshop PNG.

The perceptual score combines structural similarity with CIEDE2000 color difference on lightly blurred images and applies contrast masking. Images larger than four megapixels are downsampled to four megapixels for this comparison. The table reports the mean of the per-file accuracy scores, so every completed file has equal weight.

Testy also records a stricter byte-oriented pixel comparison. The README uses the perceptual result because it better represents visible differences while still penalizing missing or incorrect content.

### Baked-composite trap

Many PSD files contain a flattened preview in addition to their editable layers. A renderer can appear accurate by reading that preview without interpreting the layer stack.

Testy creates a trapped copy whose embedded flattened preview is replaced with magenta while the layer data remains byte-identical. A renderer that reconstructs the document from layers should still produce the original image. No applicable renderer in this snapshot hit the trap. The Affinity trap leg is skipped because Affinity re-renders imported PSD layers by design, so this test would add no useful signal.

### PSD save and native data preservation

Each editor saves the staged document as a new PSD. Photoshop then reopens that saved file. If Photoshop refuses it, Testy marks the save as rejected.

For files Photoshop can reopen, Testy compares a layer manifest from the original PSD with a manifest from the saved PSD. It checks layer kinds such as text, adjustment, Smart Object, group, fill, and raster layers. It also checks attributes including live effects, masks, clipping, and blend settings.

The aggregate preservation counts on this page use the original Photoshop manifest as the denominator for every completed editor file. A rejected or uninspectable save contributes zero retained objects. This prevents rejected files from making preservation percentages look better by disappearing from the denominator.

### Photoshop round-trip render

After Photoshop reopens an editor's saved PSD, Photoshop renders it again. Testy compares that image with Photoshop's original render. This measures what a Photoshop user actually receives after the round trip, independent of the tested editor's own PNG exporter.

### Editable text checks

The editable-text count comes from the manifest comparison, not from visual appearance. Text must remain a text object in the saved PSD.

Where an automation surface supports it, Testy also changes text content to force a fresh text render. Photoshop and Patchy support this mutation. Krita and Affinity already re-render text when opening a PSD. GIMP imports PSD text as raster data. Photopea's mutation leg is disabled because scripted text assignment can hang on some documents.

## Tested automation paths

- Photoshop: Windows COM automation
- Patchy: command-line render and save interface
- Krita: headless command line
- GIMP: Script-Fu
- PhotoDemon: locally patched `/testy-export` command-line build
- Photopea: official `postMessage` API hosted in headless Chrome
- Affinity: built-in JavaScript and MCP automation

Photopea is a rolling web build loaded from `photopea.com` at run time. The PhotoDemon result uses a local test-only command-line patch, not the stock application. Affinity is included in the benchmark, but it is not a command-line renderer.

## Limits

- This is a paused snapshot, not the final 2,175-file result.
- The results describe this corpus, these application builds, the installed fonts, and this Windows test environment.
- A mean score can hide individual failures. Testy retains per-file metrics and flags files with more than 10% perceptual difference.
- Rendering accuracy does not imply editability. The preservation and round-trip columns cover different failure modes.
- The benchmark does not measure general editing features, workflow, startup time, memory use, or export speed.
- Product updates can change the result, especially for Photopea's rolling web build.

For harness configuration, dashboard use, command examples, and implementation details, see the [Testy developer documentation](testy.md).
