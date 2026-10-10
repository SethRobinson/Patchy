# High bit depth: 16-bit and 32-bit (HDR) editing

Deep editing is on by default: 16 and 32-bit files open at their depth, and File > New
and Image > Mode offer 8, 16 and 32 bits (16 only on the web build). The gate is
`deep_editing_enabled()` (core/pixel_depth.hpp): an override
(`set_deep_editing_override`: tests, the deep stress run) beats `PATCHY_DEEP_EDITING`,
which beats the default (`set_deep_editing_default`, true in the app). There is no
preference. `PATCHY_DEEP_EDITING=0` turns it off: deep files then convert to 8 bits at
decode (docs/file-formats.md, "16-bit and 32-bit PSD/PSB import") and the Bit Depth
choices are hidden. Both test mains call `set_deep_editing_default(false)` so tests
written for 8-bit opening keep their meaning; deep tests override the gate. Open items
are listed per phase below. This document holds the rules deep work must follow;
code comments cite its phase numbers, so keep them.

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

The scorer inherits the environment, and `--deep` only forces `PATCHY_DEEP_EDITING=1`:
with the gate on by default, a run without it is deep too, so reproduce the 8-bit
baseline with `PATCHY_DEEP_EDITING=0` set. With the deep compositor (gate on): 8-bit byte 0.9993
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
  Floats run on the "deep scale" (255 = full scale, so 8-bit formulas carry over;
  16-bit v reads as v / 257): display-encoded for 16 bits, linear scene values for 32
  bits (1.0 stored = 255 on the deep scale).
- **Typed access only.** Deep code goes through a typed row layer (load/store per
  depth, deterministic conversions); it never indexes `PixelBuffer::row()` bytes.
- **The gate stays.** `PATCHY_DEEP_EDITING=0` must keep giving the old 8-bit opening
  path, and main stays releasable at every commit.
- **Unsupported means visibly disabled, never silently wrong.** Each filter,
  adjustment and tool declares the depths it supports; deep documents show the rest
  disabled with a tooltip, as Photoshop does in 32-bit.
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

## Phases

Code comments cite these phase numbers. Each phase lands as verified commits.

0. **Measurement** (done). Testy measures saved depth and 16-bit precision
   (docs/testy-scoring.md); the deep fixture corpus and its scorer are described above.
1. **Core primitives** (done). `BitDepth {UInt8, UInt16, Float32}` and
   `PixelFormat::rgb16()/rgbf32()` (core/pixel_buffer.hpp) over untyped copy-on-write
   bytes; the document's depth is `DocumentColorState::bit_depth`. Depth-generic
   helpers: `read_channel`/`write_channel`/`alpha_scale` (core/resample.cpp).
   `core/pixel_depth.hpp`: the gate, exact sample
   conversions, typed float rows (`load_rgba_row`/`store_rgba_row`, coverage rows),
   `convert_pixel_buffer_depth`. `core/document_depth.hpp`: `convert_document_depth`
   and `document_depth_problems` (authoritative buffers: pixel layers without vector
   content, masks, smart filter masks, saved channels; derived rasters may be any
   depth and convert on read). Layer masks and saved channels accept gray at any
   depth. Never use `Layer::set_pixels` to swap depth: it also resets kind and bounds.
2. **PSD/PSB at depth** (done; `tests/core/psd_deep_io_tests.cpp`). `ReadOptions::
   keep_bit_depth` (unset follows the gate) keeps the file's samples: RGB and unprofiled
   gray layers, masks, the composite, merged transparency and saved channels store
   natively. At 16 bits, CMYK and profiled gray convert through full-range u16 ICC
   transforms, Lab through the D50 Lab-to-sRGB transform, and multichannel planes
   retain their samples. Color conversion never touches transparency or masks.
   32-bit non-RGB conversion still widens the 8-bit result (gap). RGB and unprofiled
   gray narrow exactly to the historical 8-bit reading; transformed modes retain
   more precision than that old path. The writer takes the
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
   the originals (16-bit precision 100%).
   The 12 precision-scored files from Testy's 20261009-231617 run score 67.2% after
   save with native 16-bit color conversion (the published build scored 58.8%).
   Multichannel is 100%; the five RGB cases are also 100%. Remaining gaps are the
   non-RGB gradients and adjustments whose source color space is lost on RGB save,
   plus their rendering approximations. These files all reopen under Photoshop's
   error-enabled dialog mode. The synthetic regression
   `psd_deep_color_conversions_keep_sub_byte_samples_and_alpha` checks a fine ramp
   through layered and composite reads, RGB saves, gray/CMYK profiles, Lab and
   multichannel, including unchanged transparency.
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
4. **Layer operations and transforms** (done except Liquify, which is Phase 6;
   `pixel_depth_geometry_operations_keep_deep_buffers_at_depth`,
   `ui_layer_operations_keep_deep_documents_at_depth`,
   `ui_free_transform_keeps_deep_layers_at_depth`). Rules:
   - Pixel buffers move as whole pixels (`bytes_per_pixel`), never a byte per channel,
     and new masks, channels and layers take the source's or the document's depth
     (`coverage_at_document_depth`, `with_bit_depth`). Rotate, crop, rotated crop
     (float bilinear), Rotate Arbitrary, Canvas Size, Shift Seams and flips cover
     layers, masks and channels.
   - Renders that become layer pixels (merge, Merge Visible, rasterize, multi-layer
     copy, Copy Merged, the merge dialog) build their scratch document at the
     document's depth, render under `ScopedDocumentDepthRender` (deep renders then
     return `Format_RGBA64` or linear `Format_RGBA32FPx4`) and convert with
     `pixels_from_image_at_depth`. `pixels_from_image_rgba` keeps its 8-bit contract
     for loaders; `pixels_from_image_native` is the depth-preserving inverse of
     `qimage_from_pixel_buffer`. Display and export callers use
     `display_qimage_from_pixel_buffer` (32-bit sRGB-encoded).
   - Free Transform and Warp resample deep layer images in float through the same
     kernels (`premultiplied_pixel` reads `Format_RGBA32FPx4`) and return the source
     format; masks resample on float samples. Bicubic undershoot clamps at 0; linear
     values above 1 are kept.
   - Layers entering another document convert (`convert_layer_depth`): cross-document
     drags and copies, Files as Layers, layer pastes, raster pastes. Selections stay
     8-bit: deep channels, masks and composites load narrowed.
   - Clear and Cut erase deep layers on float rows (alpha times one minus the
     foreground alpha times coverage); Apply and Invert Layer Mask work at depth.
5. **Painting and retouch tools** (done; `pixel_depth_painting_writes_deep_layers_at_depth`,
   `ui_brush_paints_deep_layers_and_masks_at_depth`, `ui_retouch_tools_edit_deep_layers_at_depth`).
   `write_pixel` dispatches deep layers to `write_pixel_blend_deep` (the 8-bit blend in
   float; 32-bit blends linear light with decoded paint colors) and the brush stroke
   compositor to `render_brush_stroke_pixel_deep`; callers address pixels by
   `bytes_per_pixel`, never `channels`. Brush, Pencil, Mixer, Pattern Stamp, Eraser,
   Paint Bucket (tolerance on display-encoded values), Gradient (float colors through
   `EditOptions::deep_primary`, no banding), Line/Rectangle/Ellipse, Edit > Fill and
   Stroke Selection, Smudge (`SmudgeState::sample_deep`), Dodge/Burn/Sponge/Blur/Sharpen
   (`local_adjustment_brush_segment_deep`; 32-bit adjusts display-encoded values),
   Clone and Healing (`clone_source_deep_`, `retouch_source_deep`; healed 16-bit values
   clamp at full scale like the 8-bit path) and every mask or saved-channel edit
   (`blend_mask_at`) run at depth. Spot Healing, Remove Object and Patch heal a
   narrowed copy (`NarrowedLayerEdit`): only the healed pixels come back at 8-bit
   precision, every other pixel keeps its depth. Known gap: a float membrane solver
   would make those fills full precision.
6. **Adjustments and filters** (partly done; `tests/core/deep_filter_tests.cpp`,
   `ui_script_bit_depth_and_deep_filters`). Destructive Levels, Curves, Hue/Saturation
   and Color Balance run `DeepAdjuster` at depth (`apply_adjustment_to_deep_pixels`).
   `deep_filter_support` (filters/filter_engine.hpp) decides how a filter runs:
   - Deep kernels (`filters/deep_filters.cpp`: the 8-bit math in float without its
     intermediate rounding; Gaussian, High Pass and Unsharp share the calibrated line
     kernels through `filter_plane_with_photoshop_kernel`). Both depths: Gaussian, Box,
     Radial and Motion Blur (the exact tap kernel at every distance), Unsharp Mask,
     Pixel Mosaic, Twirl, Wave, Pinch/Bloat, Clouds (mixed in the display encoding). 16 bits only, since their constants assume
     encoded values: Invert, Brightness/Contrast, Grayscale, Desaturate, Sepia,
     Threshold, Posterize, Vignette, High Pass, Sharpen, Emboss, Add Noise.
   - Every other filter on 16 bits runs on an 8-bit copy and folds the change back
     (`apply_eight_bit_edit_at_depth`, core/pixel_depth): unchanged samples keep their
     precision, changed ones move by the 8-bit change (an edit to 0 or 255 lands
     exactly). Liquify and Auto All take the same path.
   - 32-bit documents disable the rest, Liquify and Auto All (menu actions off in
     `update_document_action_state` through the actions' `patchy.filterIdentifier`;
     script `applyFilter` throws). 8BF plug-ins run on 16-bit layers through the same
     8-bit copy and refuse 32-bit ones. The Filter Gallery takes 16-bit layers (its
     previews draw from display values: `make_filter_preview_proxy` and
     `exact_render_to_proxy` narrow) and is disabled at 32 bits.
   - Oracle: every filter on a 16-bit copy of an 8-bit image narrows back within 3
     levels (exactly, on the 8-bit-copy path); offered 32-bit filters keep a uniform
     HDR color, values above 1.0 included.
   Smart objects render their contents at 8 bits in every document, so their Smart
   Filters run at 8 bits. Still to do: Smart Filters at depth, native 16-bit
   8BF (check the SDK's 16-bit sample range), 32-bit Add Noise, deep
   kernels for the 8-bit-copy filters. Surface Blur, Median and Dust & Scratches keep
   their no-histogram designs at every depth (docs/patent-research.md).
7. **UI.** Image > Mode > 8/16/32 Bits/Channel (`image.mode_8_bit`,
   `image.mode_16_bit`, `image.mode_32_bit`; shown while deep editing is on; undoable;
   Indexed only at 8 bits). Leaving 32 bits asks for HDR Toning (`hdrToningDialog`,
   Photoshop's Exposure and Gamma method: `tone_map_linear_document` maps pixel layers'
   linear color to (v * 2^exposure)^(1/gamma) before the conversion; Patchy's own
   reading of the method, not probed against Photoshop). New Document has a Bit Depth
   row while deep editing is on (`newDocumentBitDepthCombo`); like Photoshop it starts
   at 8 bits every time (the old `newDocument/lastBitDepth` key is no longer read).
   32-bit documents show a status-bar preview exposure
   (`hdrPreviewExposureSpin`, `DocumentColorState::view_exposure_stops`): it scales
   linear values whenever a 32-bit composite narrows to display values (canvas,
   thumbnails, 8-bit exports, the eyedropper), is never saved, and undo keeps the
   current value. The Info readout adds the linear values on 32-bit documents.
   Histograms (Levels, Curves) and every `flatten_rgb8` consumer read a deep document's
   display values: `flatten_rgb8` returns the deep flatten narrowed (the 8-bit walk
   skips deep layers). Still to do: Info/picker values in 16-bit units, a histogram
   panel at depth. All text through tr() and every catalog.
8. **Other formats and the script API** (partly done). With the gate on, a 16-bit or
   float QImage from a loader (PNG 16, TIFF) opens at 16 or 32 bits
   (`document_from_qimage`, `deep_import_depth`), and the flat-alpha promotion keeps
   the mask and color at depth. Script API: `doc.bitDepth`, `doc.convertBitDepth(bits)`
   (throws for other values, with the gate off, or on Indexed documents);
   `getPixels`/`setPixels` stay RGBA8 and convert at the boundary. Export: PNG and TIFF
   keep 16 bits (`deep_export_qimage`); a 32-bit document writes float TIFF with its
   linear values (`float_export_qimage`); the other writers are 8-bit. JPEG, WebP and
   PDF narrow the deep composite (`flat_export_qimage`); the native flat writers (BMP,
   TGA, DDS, GIF, PCX, ICO/CUR, IFF, RTTEX, JXR), animated GIF/WebP, any export
   transform, and the layered SVG and Aseprite writers receive a copy converted like
   Image > Mode > 8 Bits/Channel (`document_for_8bit_writer`, ui/image_document_io.hpp),
   because they composite at 8 bits and would otherwise fail.
   Still to do: JXR float native, DDS 16-bit and float sources at depth (they open tone
   mapped or narrowed: docs/dds.md), HEIF 10-bit, raw at 16 bits, deep .af import,
   OpenEXR and Radiance .hdr (licensing check first).
9. **Memory, performance, platforms** (gate on by default; in progress). The history budget
   counts real buffer bytes (`accumulate_unique_pixel_bytes`), so deep layers weigh 2x
   or 4x. `patchy.exe --stress-test=quick --stress-depth 16|32` runs the whole stress
   scenario on deep documents (filters a 32-bit document lacks are skipped and listed
   in the report's warnings). Offscreen quick preset (October 2026): 8 bits 42.6 s,
   16 bits 92.9 s, 32 bits 93.2 s, no failures; the 16-bit scene PSD opens in
   Photoshop without a prompt. The web build stops at 16 bits
   (`depth_supported_on_platform`): 32-bit files open converted to 16 with an import
   note, and Image > Mode, New Document and `convertBitDepth` offer no 32. Still to
   do: deep compositor speed, a full Testy run against the baseline.

### Photoshop's 32-bit blend modes

Photoshop 2026 refuses to open a 32-bit PSD ("open options are incorrect") with a layer
or group in Color Burn, Linear Burn, Screen, Color Dodge, Overlay, Soft, Hard, Vivid,
Linear or Pin Light, Hard Mix or Exclusion; its own conversion to 32 bits (Don't Merge)
sets those layers to Normal. Layer-effect blend modes are not affected. Patchy follows
it (`blend_mode_supported_at_depth`): `convert_layer` substitutes Normal on the way to
32 bits, the Layers panel and Layer Style blend menus disable the modes on 32-bit
documents, script `blendMode` throws, and the PSD writer writes Normal for any that
slip through (`writable_layer_blend_mode`). Pinned by
`psd_deep_32_bit_blend_modes_follow_photoshop`.

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
