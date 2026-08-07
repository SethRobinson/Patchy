# PSD compatibility benchmark

Patchy's Testy harness measures PSD interoperability against a licensed copy of Adobe Photoshop. It tests visual rendering and the editability of a PSD after another editor saves it. Those are separate questions: a file can look correct while text, effects, masks, or other native data have been flattened or changed.

This page records completed run `20260807-153700`. The
[image-free public export](../testy/published-runs/20260807-153700/) includes the
aggregate tables, GitHub-formatted per-file results, and one CSV row for every file
and editor. It excludes source documents, renders, heatmaps, resaved PSDs, local
paths, hashes, and layer names.

## Current snapshot

- Run date: August 7, 2026
- Corpus size: 64 PSD files, 1,056,362,619 bytes
- Completed: 64 files per editor
- Reference: Adobe Photoshop 27.8.0
- Patchy build: `879a3a8`
- Comparison mode: perceptual
- Scan result: 45 files flagged, 19 passed at the 10% perceptual-difference threshold

| Editor and tested build | Type | Opened | Rendered | Perceptual render match | PSD saves rejected by Photoshop | Editable text kept | Native objects kept | Photoshop round-trip render |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Photoshop 27.8.0 | Proprietary reference | 64 / 64 | 63 | 100.00% | 0 / 64 | 312 / 312 | 1,788 / 1,788 | 100.00% (n=63) |
| **Patchy `879a3a8`** | **Open source** | **64 / 64** | **63** | **98.83%** | **0 / 64** | **312 / 312** | **1,788 / 1,788** | **99.98% (n=63)** |
| Photopea, web build in Chrome | Proprietary web app | 63 / 64 | 62 | 97.13% | 0 / 64 | 306 / 312 | 1,777 / 1,784 | 99.76% (n=62) |
| Affinity 3.2.3.4646 | Proprietary | 61 / 64 | 60 | 88.30% | 0 / 64 | 0 / 305 | 1,039 / 1,752 | 88.02% (n=60) |
| GIMP 3.2.4 | Open source | 62 / 64 | 61 | 88.11% | 0 / 64 | 0 / 312 | 611 / 1,736 | 85.45% (n=61) |
| PhotoDemon 2026.01.0251, Testy CLI build | Open source | 64 / 64 | 63 | 81.97% | 0 / 64 | 0 / 312 | 678 / 1,788 | 80.81% (n=63) |
| Krita 5.3.2.1, git `0619060` | Open source | 56 / 64 | 56 | 81.17% | 9 / 64 | 187 / 205 | 737 / 1,001 | 88.47% (n=47) |

The perceptual score is the mean over files that produced a render comparison. Native-object and text denominators cover the saves Photoshop could inspect, so they differ when an editor could not open a source or Photoshop rejected its save. A round-trip sample count is shown because it can be smaller than the render count.

## What the snapshot says about Patchy

- Patchy had the highest perceptual render match among the non-reference editors tested.
- Photoshop reopened all 64 PSD files saved by Patchy.
- All 312 Photoshop text objects remained text objects after the Patchy save.
- All 412 tested live effects, 264 Smart Objects, and 406 vector masks were retained.
- 122 of 133 user masks were retained. User-mask preservation is the remaining structural gap measured by this run.
- When Photoshop reopened and rendered Patchy's saved PSDs, the mean perceptual match was 99.98%.

The other editors have different strengths. Photopea had the next-closest non-reference render and retained 306 of 312 text objects. GIMP and PhotoDemon produced no rejected saves, but converted the tested text to raster data and retained none of the live effects Photoshop could inspect. Affinity retained 230 of 403 live effects and 413 of 518 user and vector masks, but none of its 305 tested text objects remained editable text in the exported PSD. Krita retained 187 of 205 inspected text objects, but Photoshop rejected 9 of its 64 save attempts.

## Methodology

### Source safety and staging

For every PSD and editor pair, Testy copies the source into a run directory and gives the editor that private copy. It verifies the original file's hash after the test. The source corpus is never opened for writing.

### Photoshop reference render

Photoshop 27.8.0 renders each source PSD to a flattened PNG over white at the document's declared size. Each tested editor renders the same PSD independently. Testy then compares the editor PNG with the Photoshop PNG.

The perceptual score combines structural similarity with CIEDE2000 color difference on lightly blurred images and applies contrast masking. Images larger than four megapixels are downsampled to four megapixels for this comparison. The table reports the mean of the per-file accuracy scores, so every completed file has equal weight.

Testy also records a stricter byte-oriented pixel comparison. The README uses the perceptual result because it better represents visible differences while still penalizing missing or incorrect content.

### Baked-composite trap

Many PSD files contain a flattened preview in addition to their editable layers. A renderer can appear accurate by reading that preview without interpreting the layer stack.

Testy creates a trapped copy whose embedded flattened preview is replaced with magenta while the layer data remains byte-identical. A renderer that reconstructs the document from layers should still produce the original image. Testy records the sentinel fraction and flags suspicious baked-composite use per file. The Affinity trap leg is skipped because Affinity re-renders imported PSD layers by design, so this test would add no useful signal.

### PSD save and native data preservation

Each editor saves the staged document as a new PSD. Photoshop then reopens that saved file. If Photoshop refuses it, Testy marks the save as rejected.

For files Photoshop can reopen, Testy compares a layer manifest from the original PSD with a manifest from the saved PSD. It checks layer kinds such as text, adjustment, Smart Object, group, fill, and raster layers. It also checks attributes including live effects, masks, clipping, and blend settings.

The aggregate preservation counts on this page sum the files for which Photoshop produced a native-data comparison. Rejected or uninspectable saves are reported separately in the opened and rejected-save columns and do not enter the native-data denominator. Testy's dashboard data-kept score is the mean of each inspectable file's retained-object fraction, while the table on this page also gives aggregate object counts.

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

- This is a completed 64-file curated run, not a claim about every PSD in the larger local corpus.
- The results describe this corpus, these application builds, the installed fonts, and this Windows test environment.
- A mean score can hide individual failures. Testy retains per-file metrics and flags files with more than 10% perceptual difference.
- Rendering accuracy does not imply editability. The preservation and round-trip columns cover different failure modes.
- The benchmark does not measure general editing features, workflow, startup time, memory use, or export speed.
- Product updates can change the result, especially for Photopea's rolling web build.
- Source documents and generated image artifacts are not published because their licenses vary. The public export contains measurements only.

For harness configuration, dashboard use, command examples, and implementation details, see the [Testy developer documentation](testy.md).
