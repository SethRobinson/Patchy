# High bit depth: 16-bit and 32-bit (HDR) editing

Status (October 9, 2026): Phases 0-2 done; Phase 3's compositor done, its display and
tool loose ends open (below). With the gate off (the default) Patchy still edits in 8
bits: deep files convert at decode (docs/file-formats.md, "16-bit and 32-bit PSD/PSB
import"). This document is the plan of record and the rules the work must follow.
Update it as each phase lands; keep it current-state.

## Goal and acceptance

True 16-bit and 32-bit float documents, with no feature lost and Testy scores equal or
better. 16-bit lands first as a complete milestone; 32-bit linear HDR follows on the
same float path.

Baseline to beat: Testy run 20261009-001848 (psd-tools corpus, 309 files, Photoshop
and Patchy, release build of 089007f7): opened 309/309, render 0.8786, visual 0.8978,
native 1.0, bad saves 0, deep saves kept depth 0/29, 16-bit precision 0.2825 (render)
and 0.3131 (resave). The published 2026-10-06 run (57ba855c) differs only in visual
0.8980: Photoshop's fresh 16-bit reference of `colormodes/4x4_16bit_rgb.psd` moved one
of its 16 pixels. The 29 deep files match (perceptual bad fraction at most 10%) on 21.
The Photoshop column is the control: 100% everywhere, depth kept 29/29.

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

With the deep compositor (`score_patchy.py --deep`, gate on): 8-bit byte 0.9993
(Levels' toe, below), 16-bit byte 0.9996 and 16-bit precise 0.9778, 32-bit byte
0.9995 and perceptual 1.0; every document keeps its depth. Remaining 16-bit precision
gaps: the gradient fill (interpolation within two 16-bit steps), the effects scene (the
same 148 pixels the 8-bit render misses: outside-stroke corners and the shadow beside
them; its byte match is 0.9819 against the baseline's 0.9822, two pixels) and
Exposure (98%).

## Decisions (agreed with Seth, October 8, 2026)

- **One depth per document**, as in Photoshop. Layers, masks, saved channels and
  smart-filter masks all match it. Image > Mode gets 8 / 16 / 32 Bits/Channel; the
  conversion is an undoable document operation.
- **The 8-bit path is not touched.** Its integer math, calibrated rounding and every
  byte-stability canary stay as they are. Never re-pin an 8-bit canary for this work.
  The one kind of exception: a Photoshop mismatch the deep corpus exposes in math both
  depths share is fixed for 8 bits too, with a test pinned to Photoshop's 8-bit output
  (Levels' gamma toe, October 2026; no canary moved).
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
1. **Core primitives** (done). `core/pixel_depth.hpp`: the gate, exact sample
   conversions, typed float rows (`load_rgba_row`/`store_rgba_row`, coverage rows),
   `convert_pixel_buffer_depth`. `core/document_depth.hpp`: `convert_document_depth`
   and `document_depth_problems` (authoritative buffers: pixel layers without vector
   content, masks, smart filter masks, saved channels; derived rasters may be any
   depth and convert on read). Layer masks and saved channels accept gray at any
   depth. Never use `Layer::set_pixels` to swap depth: it also resets kind and bounds.
2. **PSD/PSB at depth** (done; `tests/core/psd_deep_io_tests.cpp`). `ReadOptions::
   keep_bit_depth` (unset follows the gate) keeps the file's samples: RGB and unprofiled
   gray layers, masks, the composite, merged transparency and saved channels store
   natively; CMYK, Lab and profiled gray convert at 8 bits and widen (gap). The 8-bit
   reading of a deep file is exactly the deep reading narrowed. The writer takes the
   document's depth: header depth, layer records in `Lr16`/`Lr32` behind an empty
   standard section, deep composite and saved channels; descriptor colors linear for
   32 bits (`ScopedLinearDescriptorColors`). The stored composite (and its merged
   transparency) is the deep compositor's flatten at the document's depth
   (`deep_merged_flatten_composite`). Photoshop 2026 rules found by splicing sections:
   - 32-bit layer channels must be zip with prediction (raw and RLE are refused;
     16-bit RLE and raw open fine).
   - A 32-bit file needs its color mode data: the `hdrt`/`hdra` HDR toning record.
     An empty section is refused ("open options are incorrect"). It is kept from the
     source (`DocumentMetadata::raw_psd_color_mode_data`) or Photoshop 27.10's default
     is written.
   - The first HDR document a Photoshop session opens raises an informational "HDR
     display setting is off" alert; it is not about the file.
   `python scripts\dev\deep\ps_check_writes.py` re-saves every corpus document
   through patchy.exe and opens it in Photoshop: all 77 open and render identical to
   the originals (16-bit precision 100%). The recovery store at depth is still open
   (Phase 3's list).
3. **Deep compositor** (compositor done; `tests/core/deep_compositor_tests.cpp`).
   `render/layer_compositor.hpp` is templated on the target's color type
   (`render/composite_color.hpp`: `target_color_t`, `DeepRgb` floats on the deep scale);
   the 8-bit instantiation is the historical code, byte for byte. Deep blend math is
   `core/blend_math_deep`, deep adjustments `core/adjustment_deep` (continuous
   transfers, no 256-entry LUTs; Curves as splines). Effects and gradient colors stay
   8-bit parameters, widened per target (`deep_from_byte`); gradient fill layers render
   a deep raster (`deep_gradient_fill_raster`, linear-light interpolation at 32 bits).
   Encoded targets clamp to 0..255 on store; Linear targets keep any finite value,
   negatives included. `Compositor::flatten_rgba_deep` feeds the display
   (`render_document_rect` narrows) and PNG/TIFF export (16-bit RGBA64). Photoshop
   2026 rules the corpus pinned:
   - Levels above gamma 1 is not the plain power near black: below
     t = 2^(-g - 1/(g-1)) it is a cubic Hermite toe leaving black at slope 2^g and
     meeting x^(1/g) in value and slope at t (`levels_gamma_curve`, all depths; ramps
     at gamma 1.2 to 9.99 match within 3/65535 up to gamma 3).
   - 32-bit Levels maps linear values: (v - black)/(white - black) through a signed
     power, unclamped (negative below the black point). 32-bit Curves applies to the
     linear value times 255.
   - 16-bit Posterize buckets floor(v16 * levels / 65536).
   - Deep Hue/Saturation (`hue_saturation_transfer`): the lightness percent is exact,
     the hue rotation unrounded (h * 4.25 wheel steps), and saturation uses its own
     multipliers (`kDeepSaturationScale`, probed per percent; +20 is 318/256 where 8
     bits use 1.2473). At 32 bits there is no gamut limit and no clamp.
   - 32-bit Luminosity is the PDF SetLum without ClipColor (negatives survive).
   The eyedropper picks deep documents from the deep render; layer thumbnails read
   any depth (`display_rgba8_at`). Large deep renders and flattens split into strips
   under the 8-bit rules (`PATCHY_RENDER_SINGLE_THREADED` included). The recovery
   store writes the same PSB writer and reopens through the normal loader, so it keeps
   depth whenever the gate is on. Testy's Patchy driver splits a 16-bit PNG export into
   `render16.png` (the precision metric) and an 8-bit `render.png`
   (`analyze.split_deep_png`). A deep target caches one `DeepAdjuster` per adjustment
   pass, keyed by `adjustment_pass_serial` as well as the settings address: stacked
   adjustment layers reuse one stack slot for their settings.
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
7. **UI.** Image > Mode > 8/16/32 Bits/Channel is in (`image.mode_8_bit`,
   `image.mode_16_bit`, `image.mode_32_bit`; shown while the gate is on; undoable;
   Indexed only at 8 bits). Still to do: New Document depth,
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
