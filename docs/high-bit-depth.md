# High bit depth: 16-bit and 32-bit (HDR) editing

Deep editing is on by default: 16 and 32-bit files open at their depth, and File > New
and Image > Mode offer 8, 16 and 32 bits (16 only on the web build). The gate is
`deep_editing_enabled()` (core/pixel_depth.hpp): an override
(`set_deep_editing_override`: tests, the deep stress run) beats `PATCHY_DEEP_EDITING`,
which beats the default (`set_deep_editing_default`, true in the app). There is no
preference. `PATCHY_DEEP_EDITING=0` turns it off: deep files then convert to 8 bits at
decode (docs/file-formats-psd.md, "16-bit and 32-bit PSD/PSB import") and the Bit Depth
choices are hidden. Both test mains call `set_deep_editing_default(false)` so tests
written for 8-bit opening keep their meaning; deep tests override the gate. Open items
are listed per phase. This document holds the rules deep work must follow, with Phases
2, 3 and 8 in the documents below; code comments cite the phase numbers, so keep them.

Related documents:

- [high-bit-depth-formats.md](high-bit-depth-formats.md): Phase 2 (PSD/PSB at the file's depth, Photoshop 2026's rules for deep PSDs, native-color saves) and Phase 8 (other formats, export, HDR sources, the script API).
- [high-bit-depth-compositor.md](high-bit-depth-compositor.md): the deep fixture corpus (its Photoshop facts, baseline and scores) and Phase 3, the deep compositor with the Photoshop rules the corpus pinned.
- [high-bit-depth-color.md](high-bit-depth-color.md): color choice at 32 bits (a color's Intensity in stops, its controls, the Eyedropper and the Gradient tool).

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
  Native HDR monitor output is a later, separate phase; its capability probe exists
  (`app.displayInfo()`, `core/hdr_display.hpp` with the eligibility policy and the reserved
  `PATCHY_HDR_DISPLAY` switch; docs/platform-sites.md).
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
   (docs/testy-scoring.md); the deep fixture corpus and its scorer are described in
   [high-bit-depth-compositor.md](high-bit-depth-compositor.md#the-deep-fixture-corpus).
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
2. **PSD/PSB at depth** (done; `tests/core/psd_deep_io_tests.cpp`): the reader and writer
   at the file's depth, Photoshop 2026's rules for deep PSDs and native-color saves, in
   [high-bit-depth-formats.md](high-bit-depth-formats.md#phase-2-psdpsb-at-depth).
3. **Deep compositor** (compositor done; `tests/core/deep_compositor_tests.cpp`): the
   compositor templated on the target's color type, deep blend and adjustment math, and
   the Photoshop 2026 rules the corpus pinned, in
   [high-bit-depth-compositor.md](high-bit-depth-compositor.md#phase-3-deep-compositor).
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
   `ui_script_bit_depth_and_deep_filters`). Destructive Levels, Curves, Hue/Saturation,
   Color Balance and the adjustment-layer kinds of Image > Adjustments
   ([adjustment-kinds.md](adjustment-kinds.md#image--adjustments)) run `DeepAdjuster` at
   depth (`apply_adjustment_to_deep_pixels`).
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
   row while deep editing is on (`newDocumentBitDepthCombo`); the preset cards start at
   8 bits and the Last Used card restores the last accepted depth
   (`newDocument/lastBitsPerChannel`; the old `newDocument/lastBitDepth` key is no longer read).
   32-bit documents show a status-bar preview exposure
   (`hdrPreviewExposureSpin`, `DocumentColorState::view_exposure_stops`): it scales
   linear values whenever a 32-bit composite narrows to display values (canvas,
   thumbnails, 8-bit exports), is never saved, and undo keeps the current value. The
   Eyedropper reads the linear values instead
   ([Color choice at 32 bits](high-bit-depth-color.md#color-choice-at-32-bits)). The Info
   readout adds the linear values on 32-bit documents.
   Histograms (Levels, Curves) and every `flatten_rgb8` consumer read a deep document's
   display values: `flatten_rgb8` returns the deep flatten narrowed (the 8-bit walk
   skips deep layers). Still to do: Info/picker values in 16-bit units, a histogram
   panel at depth. All text through tr() and every catalog.
8. **Other formats and the script API** (partly done): loaders, exports and HDR sources at
   depth, the script API, and the formats still to do, in
   [high-bit-depth-formats.md](high-bit-depth-formats.md#phase-8-other-formats-and-the-script-api).
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
   where possible so the same test runs against the real build. The standing form is
   `tests/ui/deep_operation_audit_tests.cpp` (October 2026): about 45 user-level
   operations (transforms, resampling, rotate/flip, crop, copy/paste, merges, flatten,
   masks, fills, strokes, fill layers, save and reopen in every deep format) and every
   built-in filter, each run through scripting at 8, 16 and 32 bits on a picture of flat
   color blocks, comparing display composites: 16 bits everywhere within 3 levels, 32
   bits on flat interiors (linear-light blurs, resampling, blends and gradients differ at
   edges by design; opacity, blend modes, gradients and Radial Blur compare 16 bits only).
   Interactive previews, which scripting cannot reach, have their own canvas tests
   (`ui_move_scaled_preview_matches_settled_render_at_every_depth`,
   `ui_free_transform_preview_and_commit_keep_colors_at_every_depth`). The rule they
   enforce: a 32-bit image is linear floats, so nothing may hand it to QPainter or a Qt
   8-bit conversion without sRGB-encoding it first (`display_image_from_deep_qimage`,
   `display_qimage_from_pixel_buffer`, or a display render).
2. **Photoshop ground truth** on the deep fixture corpus via COM
   ([high-bit-depth-compositor.md](high-bit-depth-compositor.md#the-deep-fixture-corpus)).
3. **8-bit canaries stay pinned** (composite corpus digests, PSD writer bytes, tool
   write paths, filter contact sheet, all `*_matches_photoshop` fixtures).
4. **Testy** after Phases 2, 3 and 9, gate off and on: opened, render, visual, native
   and bad saves at least the baseline; deep files saved at full depth; the deep
   subset at least 21/29 (target 29/29).
