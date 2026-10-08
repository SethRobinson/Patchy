# High bit depth: 16-bit and 32-bit (HDR) editing

Status (October 9, 2026): Phase 0 done; Phase 1 next. Patchy still edits in 8 bits: deep files
convert at decode (docs/file-formats.md, "16-bit and 32-bit PSD/PSB import") and every
writer emits 8 bits. This document is the plan of record and the rules the work must
follow. Update it as each phase lands; keep it current-state.

## Goal and acceptance

True 16-bit and 32-bit float documents, with no feature lost and Testy scores equal or
better. 16-bit lands first as a complete milestone; 32-bit linear HDR follows on the
same float path.

Baseline to beat: Testy run 2026-10-06 (psd-tools corpus, 309 files, Patchy 57ba855c):
opened 309/309, render 0.8786, visual 0.8980, native 1.0, bad saves 0. The 29 deep
files matched (perceptual bad fraction at most 10%) on 21 of 29. Every deep file was
saved at 8 bits, which Testy could not see until Phase 0 (`saveDepth`, `deepRender`,
`deepRoundtrip`; docs/testy-scoring.md).

The deep subset list for quick runs is `testy/corpus/deep-local.txt` (gitignored;
regenerate by filtering `corpus/psd-tools.txt` on `testy.file_traits(...)["depth"]`).
Run it with `python testy\testy.py --corpus corpus\deep-local.txt --editors
photoshop,patchy` (corpus paths are relative to `testy/`).

## The deep fixture corpus

`python scripts\dev\deep\make_deep_fixtures.py` builds 46 single-feature scenes
(plain ramp, all 27 blend modes, opacity/fill, alpha ramp, gradient layer mask,
clipping, pass-through group, nine adjustment layers, solid and gradient fill layers,
smart object, drop shadow plus stroke, saved alpha channel) with Photoshop COM, each
directly at 8, 16 and 32 bits, into `local-test-fixtures/deep/<scene>/<depth>/`
(gitignored): the PSD, Photoshop's flatten as `render.png` (8-bit sRGB),
`render16.png` (16-bit documents) and `render32.tif` (32-bit documents: uncompressed
little-endian float, linear, no profile). `manifest.json` records each build.
`python scripts\dev\deep\score_patchy.py --label <name>` opens every document in a
Patchy build, exports a render and a resave, and scores them (8-bit metrics, 16-bit
precision, saved depth) into `build/test-output/deep-score/<name>/summary.json`.

Photoshop facts the corpus pinned (Photoshop 27.10):

- A 32-bit document takes descriptor and `SolidColor` values as LINEAR light on the
  0..255 scale (250 is stored as 0.980); the generator linearizes scene colors there so
  every depth shows the same picture. Gradients interpolate in linear light at 32 bits,
  so 32-bit midtones legitimately differ from 8/16-bit ones.
- Not available at 32 bits: the Color Burn, Linear Burn, Screen, Color Dodge, Overlay,
  Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix and Exclusion
  blend modes, and the Invert, Posterize and Threshold adjustment layers. Everything
  else in the corpus builds at 32 bits (Dissolve, Lighter/Darker Color, Subtract and
  Divide included).

Baseline (Patchy 089007f7 release build, `baseline-089007f7`), mean over documents:

| Depth | Docs | Byte match | Perceptual | 16-bit precise | Saved at depth |
|---|---|---|---|---|---|
| 8 | 46 | 0.9964 | 0.9963 | - | 46 |
| 16 | 46 | 0.9959 | 0.9973 | 0.2979 | 0 |
| 32 | 31 | 0.4794 | 0.6611 | - | 0 |

The 32-bit row is the cost of compositing converted 8-bit sRGB layers where Photoshop
composites linear light: opacity, masks, gradients, adjustments and most blend modes
come out wrong. The psd-tools corpus barely shows it because its 32-bit files are simple.

## Decisions (agreed with Seth, October 8, 2026)

- **One depth per document**, as in Photoshop. Layers, masks, saved channels and
  smart-filter masks all match it. Image > Mode gets 8 / 16 / 32 Bits/Channel; the
  conversion is an undoable document operation.
- **The 8-bit path is not touched.** Its integer math, calibrated rounding and every
  byte-stability canary stay as they are. Never re-pin an 8-bit canary for this work.
- **Storage vs compute.** 16-bit stores full-range u16 (0..65535, lossless file round
  trip). 32-bit stores linear-light f32, unbounded like Photoshop. Both deep depths
  compute through ONE float path: rows widen to float, process, narrow on store.
  16-bit floats are display-encoded 0..1; 32-bit floats are linear scene values.
- **Typed access only.** Deep code goes through a typed row layer (load/store per
  depth, deterministic conversions); it never indexes `PixelBuffer::row()` bytes.
- **Developed behind a gate** (`PATCHY_DEEP_EDITING`, environment plus a hidden
  preference). Off by default until the parity checklist passes; main stays
  releasable at every commit.
- **Unsupported means visibly disabled, never silently wrong.** Each filter,
  adjustment and tool declares the depths it supports; deep documents show the rest
  disabled with a tooltip, as Photoshop does in 32-bit. The gate does not flip while a
  16-bit capability is missing.
- **Display.** The canvas stays an 8-bit image. 16-bit narrows at the display
  boundary. 32-bit gets a view exposure/gamma control that changes the display only.
  Native HDR monitor output is a later, separate phase.
- **32-bit scope.** At least Photoshop's 32-bit feature set, more wherever the math is
  well defined on unbounded linear values; anything else disabled.
- **Web build.** 16-bit only; 32-bit files convert to 16-bit on open there with a
  notice (wasm32's 4 GB ceiling and 256 MB history budget).
- **GPU later, not in tandem.** Prepare for it: deep float kernels are branch-light
  row functions with plain formulas (shader-portable), the deep path is verified by
  tolerance rather than byte identity (so a GPU path can be checked against it), and
  the canvas draws through a small display seam. Optional later phases: a QRhi display
  presenter (native HDR on Windows), then a GPU compositor for deep documents.

## What exists to build on

- `BitDepth {UInt8, UInt16, Float32}` and `PixelFormat::rgb16()/rgbf32()`
  (src/core/pixel_buffer.hpp); untyped copy-on-write byte storage.
- `DocumentColorState::bit_depth` (src/core/document.hpp), set but unused.
- Depth-generic helpers: `read_channel`/`write_channel`/`alpha_scale`
  (src/core/resample.cpp); photo_divide and image_trace handle deep buffers.
- PSD deep decode, zip prediction, `Lr16`/`Lr32` parsing (src/psd/psd_channel_data.cpp).
- Float alpha, coverage, mask and effect planes inside src/render/layer_compositor.hpp;
  only color planes and targets are 8-bit.
- The raw developer runs at u16 internally (raw_tone).

## Phases

Each phase lands as verified commits; the gate stays off until Phase 9.

0. **Measurement** (done, October 9, 2026). Testy measures saved depth and 16-bit
   precision (docs/testy-scoring.md); the deep fixture corpus and its scorer exist
   (below).
1. **Core primitives.** Typed rows and conversions; `convert_document_depth` across
   layers, masks, channels, smart-filter masks, vector-mask caches, the flat composite
   and descriptor colors (32-bit colors are linear); `Document` depth authoritative,
   with an invariant check.
2. **PSD/PSB at depth.** Read native samples when the gate is on; write header depth,
   the `Lr16`/`Lr32` block behind an empty standard layer section, and the deep
   composite; must open in Photoshop without prompts (docs/ps-compat.md). Recovery
   writes deep PSB. PNG 16 export comes here too, so Testy's `deepRender` can measure
   Patchy's own render (the cache-free leg's composed `render.png` must then keep 16
   bits).
3. **Deep compositor.** Color type becomes a template parameter of the compositor;
   float targets, float blend math (32-bit follows Photoshop's mode list), float
   adjustments (no 256-entry LUTs), float effects, Blend If on deep values. Display
   narrows to RGBA8888; the eyedropper composites at depth.
4. **Layer operations and transforms.** Merge, flatten, duplicate, rasterize at depth,
   transforms, warp, liquify, crop, canvas size, copy/paste and Files as Layers across
   depths (convert on entry).
5. **Painting and retouch tools.** The pixel_tools write path, the canvas brush engine,
   fill, gradient (no 8-bit dither), clone/heal/spot heal/patch/Remove Object, smudge,
   mixer, dodge/burn/sponge, blur/sharpen, history brush, pattern stamp.
6. **Adjustments and filters.** Every filter, smart filters, Filter Gallery, liquify,
   destructive adjustments. 8BF plug-ins at 16 bits where the host contract allows.
   Surface Blur, Median and Dust & Scratches keep their no-histogram designs at every
   depth (docs/patent-research.md).
7. **UI.** Image > Mode depth items (new permanent action ids), New Document depth,
   picker/Info/histogram precision, deep Levels/Curves histograms, 32-bit preview
   exposure, conversion dialog for 32 to lower depths. All text through tr() and every
   catalog.
8. **Other formats and the script API.** PNG 16 read, TIFF 16/32, JXR float native,
   HEIF 10-bit, raw at 16 bits, deep .af import, OpenEXR and Radiance .hdr (licensing
   check first). `document.bitDepth`, `document.convertBitDepth()`, deep
   `getPixels`/`setPixels` (d.ts, guide, change log).
9. **Memory, performance, platforms, flip the gate.** History budget and style caches
   under 2x/4x pixels, a deep stress preset (new step ids appended), wasm, mac/linux
   builds, then both full suites and a full Testy run against the baseline.

## Verification rules

1. **Differential oracle.** For every blend mode, adjustment, filter, tool and layer
   operation: run it on an 8-bit document, and on the same document converted to 16
   (and 32) bits; convert back and require agreement within 1/255 unless a documented
   exception says otherwise (with the reason). Drive it through `patchy.*` scripting
   where possible so the same test runs against the real build.
2. **Photoshop ground truth** on the deep fixture corpus via COM.
3. **8-bit canaries stay pinned** (composite corpus digests, PSD writer bytes, tool
   write paths, filter contact sheet, all `*_matches_photoshop` fixtures).
4. **Testy** after Phases 2, 3 and 9, gate off and on: opened, render, visual, native
   and bad saves at least the baseline; deep files saved at full depth; the deep
   subset at least 21/29 (target 29/29).
