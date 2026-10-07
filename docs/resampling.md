# Image Size resampling

Owner: `src/core/resample.hpp` / `resample.cpp`. Image > Image Size (`request_image_size_settings`
and `MainWindow::resize_document_image` in `src/ui/main_window_document_dialogs.cpp`),
`doc.resizeImage` and the MCP connector all resize through `resize_image_and_layers`
(`core/document_geometry.cpp`), which hands every layer, mask and document channel to
`resample_pixels` with one method.

## Methods and permanent ids

`ResampleMethod` is append-only. The ids are compatibility contracts: the dialog stores the
last choice under the settings key `imageSize/lastResampleMethod` (an id string, saved on
accept only; missing or unknown reads as Automatic) and `doc.resizeImage(w, h, {method})`
accepts them. The dialog shows them in this order.

| Id | Dialog label | Kernel |
|---|---|---|
| `automatic` | Automatic | Bicubic Sharper when the target area is smaller than the source, Bicubic Smoother when larger, Bicubic when equal. Resolved once per resize from the document dimensions (`resolve_automatic_resample_method`), so every layer uses the same kernel. |
| `nearest` | Nearest Neighbor (hard edges) | Pixel copy of the source pixel under each output center, `floor((i + 0.5) * source / target)`. Integer enlargements replicate exactly. |
| `bilinear` | Bilinear | Triangle (tent), support 1. |
| `bicubic` | Bicubic (smooth gradients) | Catmull-Rom: Mitchell-Netravali B 0, C 0.5. The same kernel as Free Transform's Bicubic. |
| `bicubicSmoother` | Bicubic Smoother (enlargement) | Mitchell: B 1/3, C 1/3. Softer, least ringing. |
| `bicubicSharper` | Bicubic Sharper (reduction) | Keys a = -0.75: B 0, C 0.75. Highest acutance, most overshoot. |

The three cubics are one function, `cubic_bc_weight(distance, B, C)`. `cubic_weight` is
Catmull-Rom spelled with its literal coefficients because Free Transform's bicubic output is
pinned on that exact expression (`gray8_resample_identity_and_default_fill`,
`ui_group_transform_resamples_linked_masks`); keep both.

## The driver (`resample_pixels`)

Separable two-pass filter with per-axis weight tables (`build_axis_weights`):

- Output index i samples the source around `center = (i + 0.5) * source / target`.
- On a reduction the kernel widens by the scale (`filter_scale = max(1, source / target)`), so
  the output averages its footprint instead of aliasing. That is what makes "Sharper
  (reduction)" a real choice; the old point-sampled bilinear kept pure alternating columns
  through a 2:1 reduction.
- Taps outside the buffer are dropped and the remaining weights renormalized. For a bilinear
  enlargement this is byte-identical to clamp-to-edge sampling, which keeps the export resize
  pin (`ui_export_resize_resamples_bilinear_to_target`: 2 -> 4 of 0/100 gives 0, 25, 75, 100).
- Every bit depth: channels are read as doubles from UInt8, UInt16 (native memcpy) or Float32
  and written back rounded and clamped for the integer depths (floats unclamped). Deep
  documents used to fall back to nearest.
- Alpha: a buffer with one channel more than its color mode's color channels (RGBA, gray +
  alpha) interpolates premultiplied and un-premultiplies against the stored alpha on write; a
  fully transparent result is black, like Free Transform's commit. The old straight-alpha
  bilinear darkened fringes toward transparent black.
- The vertical pass streams: horizontally resampled source rows live in a window that advances
  with the output row, so memory is `max_taps` rows, never the whole image.
- Deterministic: fixed summation order, no threads inside. The Image Size caller already runs
  it on a worker (`resize_document_image`).

Other callers pass an explicit method and keep Bilinear: the export Resize option
(`transform_export_buffer`, `src/ui/image_document_io.cpp`) and the Proton texture writer's
stretch mode (`rttex_document_io.cpp`).

## Free Transform

Free Transform (`src/ui/canvas_widget_transform.cpp`) keeps its own inverse-mapping samplers
over a QImage (arbitrary affine, 8-bit RGBA and gray8, point-sampled; no kernel widening on
a reduction, unlike `resample_pixels`). Its options-bar combo offers Photoshop's six choices
in Photoshop's order: Nearest Neighbor, Bilinear, Bicubic, Bicubic Smoother, Bicubic Sharper,
Bicubic Automatic. `CanvasWidget::TransformInterpolation` is persisted as an integer under
`tools/transformInterpolation`, so the enum is append-only (Smoother, Sharper and Automatic
were appended in October 2026; an unknown value loads as Bicubic). The cubic taps come from
`cubic_tap_weight`: Bicubic keeps `patchy::cubic_weight` (the pinned literal Catmull-Rom),
Smoother and Sharper read their (B, C) from `resample_kernel`, so the kernels are Image
Size's. Automatic is resolved per resample by output area over source area
(`resolve_automatic_interpolation`: the affine determinant for a transform, the output
extent over the source extent for a warp): Sharper below 1, Smoother above, Bicubic at 1:1.
Test: `transform_resample_cubic_variants_and_automatic` (group_transform_tests.cpp).

## Dialog behavior

The preview (`imageSizePreview`) shows the method, not just the size: the flattened document
(reduced once to at most four times the box when larger; a small document keeps its exact
pixels) is resampled by `resample_pixels` to the target size fitted into the box with the
chosen method on every size or method change (`image_size_preview_pixmap`, called from
`update_summary`). Automatic resolves from the real document and target sizes. Nearest
Neighbor therefore previews as blocks and the cubics as their blur; a Qt smooth scale of
the current pixels used to show every method as a blur (`ui_image_size_dialog_preview_follows_method`).

Resample on: the method combo is enabled and the choice is applied and remembered. Resample
off: pixel dimensions lock to the document (Photoshop semantics, docs/resolution-units.md),
the combo disables, and the hint label `imageSizeResampleHintLabel` ("Pixel dimensions are
locked...") appears; the checkbox tooltip says the same. OK with Resample off is a
metadata-only "Print resolution" undo step.

## Tests

Core (`tests/core/pixel_tools_tests.cpp`): `resample_nearest_replicates_pixels_at_every_depth`,
`resample_bilinear_enlargement_matches_clamped_edge_ramp`, `resample_reduction_widens_the_kernel`,
`resample_cubic_variants_are_distinct_and_keep_flat_color`, `resample_interpolates_premultiplied_alpha`,
`resample_sixteen_bit_and_float_interpolate`, `resample_automatic_resolves_by_direction`,
`resample_method_ids_round_trip`; `document_image_resize_scales_layers_and_writes_artifact`
covers the method reaching layers. UI (`tests/ui/import_print_resolution_tests.cpp`):
`ui_image_size_dialog_method_is_applied_and_remembered`, `ui_script_resize_image_method_option`,
the Resample-off phase of `ui_image_size_dialog_unit_and_resolution_links_work`, and the export
pins `ui_export_resize_resamples_bilinear_to_target` and
`ui_export_transforms_apply_trim_resize_scale_matte_in_order` (whose fringe values encode the
premultiplied rule).
