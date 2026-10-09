# PSD compatibility benchmark

Patchy's Testy harness measures PSD interoperability against a licensed copy of Adobe Photoshop. It tests visual rendering and the editability of a PSD after another program saves it. Those are separate questions: a file can look correct while text, effects, masks, or other native data have been flattened or changed.

This page records the Testy v2 run `20261006-125404`. The
[published report](https://www.rtsoft.com/testy/2026-10-06/) has every file's renders,
difference maps, and per-cell details for all eight columns. It excludes the source
documents and the resaved PSDs.

The benchmark measures one thing: how faithfully each program loads, renders, and saves Photoshop files. A low score means that keeping documents as PSDs and moving them between that program and Photoshop will lose things. It is not a rating of the program at anything else.

## Current snapshot

- Run date: October 6, 2026
- Corpus: the [psd-tools](https://github.com/psd-tools/psd-tools) test collection (MIT license), commit `605ee1284952`: 309 small single-feature PSD and PSB files, 49.6 MB
- Reference: Adobe Photoshop 27.10.0, which opened 307 of the 309 files
- Patchy build: `57ba855c`
- Comparison mode: perceptual
- Thanks to the psd-tools authors for the collection ([issue 65](https://github.com/SethRobinson/Patchy/issues/65))

| Program and tested build | Type | Opened | Files within 10% of Photoshop | Perceptual match | Byte match | Data kept in PSD save | Saves rejected by Photoshop |
|---|---|---:|---:|---:|---:|---:|---:|
| Photoshop 27.10.0 | Proprietary reference | 307 / 309 | 307 | 100.0% | 100.0% | 100.0% | 0 |
| **Patchy `57ba855c`** | **Open source** | **309 / 309** | **258** | **89.8%** | **87.9%** | **100.0%** | **0** |
| Photopea, web build in Chrome | Proprietary web app | 306 / 309 | 238 | 85.0% | 81.9% | 99.7% | 3 |
| psd-tools 1.17.0 | Open source Python library | 306 / 309 | 212 | 78.9% | 78.6% | 100.0% | 0 |
| Affinity 3.3.0.4850 | Proprietary | 304 / 309 | 206 | 77.4% | 75.1% | 84.1% | 0 |
| Krita 5.3.2.1, git `0619060` | Open source | 273 / 309 | 92 | 48.6% | 55.8% | 79.4% | 7 |
| GIMP 3.2.6 | Open source | 309 / 309 | 75 | 43.7% | 47.3% | 46.7% | 0 |
| PhotoDemon 2026.01.0251, Testy CLI build | Open source | 309 / 309 | 78 | 43.4% | 49.4% | 43.1% | 0 |

What Photoshop found in each program's saved PSDs:

| Program | Text objects kept | Adjustment layers kept | Smart Objects kept | Live effects kept |
|---|---:|---:|---:|---:|
| Photoshop | 43 / 43 | 183 / 183 | 172 / 172 | 181 / 181 |
| **Patchy** | **43 / 43** | **183 / 183** | **172 / 172** | **181 / 181** |
| Photopea | 43 / 43 | 183 / 183 | 168 / 172 | 181 / 181 |
| psd-tools | 43 / 43 | 183 / 183 | 172 / 172 | 181 / 181 |
| Affinity | 0 / 43 | 171 / 183 | 0 / 172 | 157 / 181 |
| Krita | 12 / 12 | 0 / 165 | 0 / 172 | 134 / 135 |
| GIMP | 0 / 43 | 0 / 183 | 0 / 172 | 0 / 181 |
| PhotoDemon | 0 / 43 | 0 / 183 | 0 / 172 | 0 / 173 |

The match percentages are means over the files Photoshop produced a reference for, and a file a program could not open counts as 0% for it. The kept-object denominators cover the saves Photoshop could inspect, so they shrink when a program could not open a source or Photoshop rejected its save. psd-tools is a library, not an editor: its save is a load-and-save of the file it read.

### Without known limitations

37 of the 309 files are 16-bit, 32-bit, or artboard documents, which several programs do not claim to support. Leaving them out (272 files), the perceptual match is: Patchy 90.9%, Photopea 86.3%, Affinity 82.0%, psd-tools 79.9%, Krita 52.8%, GIMP 43.1%, PhotoDemon 41.3%.

### Photoshop text

Ten files contain type layers. A file scores 0% for a program that cannot draw Photoshop text itself or cannot save it back as text; otherwise it scores that program's own render. Photopea 95%, Patchy 91%, Krita 59%. Affinity, GIMP, PhotoDemon, and psd-tools score 0%: Affinity, GIMP, and PhotoDemon do not save text back as text, and GIMP, PhotoDemon, and psd-tools show only the pixels Photoshop cached. Photopea is handed this machine's font files for the fonts each document uses, because a browser has only its own web fonts.

## What the snapshot says about Patchy

- Patchy opened all 309 files, including the two Photoshop itself refused.
- Its perceptual render match was the highest of the seven programs tested against the reference, with 258 files within 10% of Photoshop's pixels.
- Photoshop reopened every PSD Patchy saved, and every text object, adjustment layer, Smart Object, and live effect Photoshop could inspect was still there.

The gaps are concentrated in a few folders of the collection:

| Folder | Patchy files within 10% | Best other program |
|---|---:|---:|
| colormodes | 5 / 17 | 4 / 17 |
| transparency | 7 / 16 | 7 / 16 |
| path-operations | 7 / 12 | 10 / 12 |
| gradients | 0 / 4 | 0 / 4 |
| colorprofiles | 0 / 1 | 0 / 1 |
| (top level) | 114 / 125 | 112 / 125 |
| effects | 14 / 17 | 14 / 17 |
| blend-modes | 28 / 32 | 27 / 32 |
| adjustments | 37 / 39 | 34 / 39 |

The colormodes folder holds non-RGB documents, which Patchy converts to RGB on open. Path operations are the one folder where another program (Photopea) did better.

Every file in the descriptors, group-clipping, issues, layers, layers-minimal, masks, and third-party-psds folders was within 10%.

## Methodology

### Source safety and staging

For every file and program pair, Testy copies the source into a run directory and gives the program that private copy. It verifies the original file's hash after the test. The source corpus is never opened for writing.

### Photoshop reference render

Photoshop renders each source to a flattened 8-bit sRGB PNG over white at the document's declared size. Non-RGB documents are converted to RGB, the document profile to sRGB, and 16-bit to 8-bit, so every program is compared in the same space. Type layers and embedded Smart Objects are rendered afresh for the reference, since an old file's cached pixels can differ from what today's Photoshop draws.

When a font a type layer needs is not installed, Photoshop cannot draw that text faithfully either. The reference then keeps the stored pixels, and no program's text render is scored for that file.

### Scoring what a program draws itself

A PSD stores a second copy of every type layer, shape or fill layer, and Smart Object: the pixels Photoshop last drew for it. A program that shows those pixels has not rendered the layer. Testy v2 therefore scores each program on a copy with those cached pixels removed, and outlines and labels any layer the program drew nothing for.

Programs that, like Photoshop, show the stored pixels until a layer is edited get the same treatment Photoshop's reference gets: a script asks the program to lay the text out again without changing the document. Patchy and Photopea are measured this way. A layer Testy could not make a program re-render is reported as not measured and does not count against it.

This is the main difference from the August 2026 run, which compared renders of the files as opened. The two sets of numbers are not comparable.

### Render comparison

Two comparisons always run. Byte match counts pixels off by more than 6/255 per channel. The perceptual score combines structural similarity with CIEDE2000 color difference on lightly blurred images and applies contrast masking, so anti-aliasing jitter stays quiet while a missing, misplaced, or recolored object still counts. Images larger than four megapixels are downsampled to four megapixels for the perceptual comparison. Every file has equal weight in the mean.

A file counts as a match when its render is within 10% of Photoshop's pixels by the perceptual measure.

### Baked-composite trap

Many PSD files contain a flattened preview in addition to their editable layers. A renderer can appear accurate by reading that preview without interpreting the layer stack.

Testy creates a trapped copy whose embedded flattened preview is replaced with magenta while the layer data remains byte-identical. A renderer that reconstructs the document from layers still produces the original image. The Affinity trap leg is skipped because Affinity re-renders imported PSD layers by design.

### PSD save and native data preservation

Each program saves the staged document as a new PSD. Photoshop then reopens that saved file. If Photoshop refuses it, Testy marks the save as rejected.

For files Photoshop can reopen, Testy compares a layer manifest from the original with a manifest from the saved PSD. It checks layer kinds such as text, adjustment, Smart Object, group, fill, and raster layers, and attributes including live effects, masks, clipping, and blend settings. The data-kept score is the mean of each inspectable file's retained-object fraction.

Testy now also records whether a save keeps a 16 or 32-bit file's bit depth and, for 16-bit files, how precisely the render matches Photoshop's 16-bit render. Those measurements came after this run and are not part of the tables above.

### The two text rules

Text is the thing people assume survives, so two failures zero a file's score. When a program draws nothing for a Photoshop type layer and can only show the cached pixels, that file's render scores 0%. When any type layer of the original is no longer a type layer in the program's save, that file's data-kept score is 0%. The report shows the measured numbers alongside.

The full scoring rules, including the safeguards that keep a harness mistake from counting against a program, are in [testy-scoring.md](testy-scoring.md).

## Tested automation paths

- Photoshop: Windows COM automation
- Patchy: command-line render and save, plus a script that re-renders text and Smart Objects
- Krita: its own Python runner, exporting after the rendering has settled
- GIMP: headless Script-Fu
- PhotoDemon: locally patched `/testy-export` command-line build
- Photopea: official `postMessage` API hosted in headless Chrome
- Affinity: built-in JavaScript and MCP automation
- psd-tools: the Python library's compositor and a load-and-save

Photopea is a rolling web build loaded from `photopea.com` at run time. The PhotoDemon result uses a local test-only command-line patch, not the stock application.

## Limits

- The collection is several hundred small single-feature files written to test a PSD parser. It names the feature behind a failing cell well, and it says less about large real-world documents.
- The results describe this corpus, these builds, the installed fonts, and this Windows test environment.
- A mean score can hide individual failures. The published report keeps every file's metrics.
- Rendering accuracy does not imply editability. The preservation columns cover a different failure mode.
- The benchmark does not measure general editing features, workflow, startup time, memory use, or export speed.
- Product updates can change the result, especially for Photopea's rolling web build.
- Testy v2 is still being developed, and its scoring rules may change between runs.

## Earlier run

The August 7, 2026 run used a 64-file curated corpus of larger documents and the earlier scoring, which compared renders of the files as opened. Its [image-free export](../testy/published-runs/20260807-153700/) remains in the repository. In that run Photoshop reopened all 64 Patchy saves, all 312 text objects stayed text, and Patchy's perceptual render match was 98.83% on build `879a3a8`.

For harness configuration, dashboard use, command examples, and implementation details, see the [Testy developer documentation](testy.md).
