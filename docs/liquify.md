# Liquify

This document is the implementation, PSD-interoperability, and legal contract for Patchy's manual Liquify workspace.

## Product and UI contract

`Filter > Liquify...` opens a dedicated brush workspace. Liquify is not a `FilterRegistry` entry: it is an ordered sequence of gestures rather than one named invocation with stable parameters. It therefore does not appear in the Filter Gallery, cannot be saved in a Look, and has no private filter identifier. Its persisted hotkey command ID is `filter.liquify`; the default is Ctrl+Shift+X.

The tools are Warp, Reconstruct, Smooth, Twirl, Pucker, Bloat, Freeze, and Thaw. Twirl rotates clockwise; Alt/Option reverses it. Brush Size is 5 through 2000 px; Pressure and Density are 1 through 100 percent. The freeze mask is an edit-time protection field shown as a blue overlay. Restore All clears both displacement and protection. Cancel changes nothing. Apply renders once from the immutable original pixels, respects the document selection, keeps the original layer bounds, and creates one undo entry. Pixels outside the source bounds are edge-clamped, so a deformation at a layer edge does not create a transparent seam.

The workspace edits a bounded proxy whose longest edge is 720 px. `LiquifyMesh` stores inverse displacement in signed 24.8 fixed point on a grid capped at 129 nodes per axis. Rendering normalizes the grid to the target dimensions, so accepting the proxy applies the same relative deformation to the full-resolution layer. Final resampling uses deterministic fixed-point bilinear weights and premultiplied RGBA. Do not replace this with `std::uniform_*`, toolchain-specific interpolation, or a per-repaint full-layer render.

Freeze and Thaw modify only the protection field. Warp adds the inverse of the pointer motion. Reconstruct tends displacement toward identity. Smooth tends each protected node toward its four-neighbor displacement average. Twirl, Pucker, and Bloat add bounded inverse offsets around each brush dab. Completed displacement, not the gesture history, is the full-resolution render input.

## PSD and Smart Object behavior

Liquify is currently a destructive pixel edit on an ordinary RGB/RGBA UInt8 layer. A PSD stores the resulting pixels through the normal layer channel path; no private Patchy resource or filter descriptor is added. Photoshop therefore sees the same raster result. Selection clipping, undo, palette compliance warnings, and subsequent PSD saves behave like other destructive pixel edits.

Do not synthesize Photoshop's native Liquify Smart Filter descriptor. Its descriptor and render-cache shape have not been calibrated from clean-room output. Patchy refuses Liquify on a Smart Object and asks the user to rasterize first. An imported Smart Object containing an unsupported Photoshop Liquify entry remains on the existing byte-preserved, preview-locked path. This prevents a direct or partial edit from dropping unknown native fields or replacing Photoshop's stored preview.

If native Smart Filter support is added later, first capture Photoshop-authored before/after PSDs that differ in one manual setting at a time, document the descriptor and FEid behavior in `docs/ps-compat.md`, and make the whole-stack support decision fail closed. Do not infer a descriptor from UI labels or copy Adobe specification prose.

## Patent boundary

Patchy's implementation is limited to explicit manual brush gestures over a generic displacement grid. Adobe's classic brush-warp family, including US 6,765,589, US 7,098,932, and US 7,567,263, is expired. The implementation uses independently written fixed-grid math and no Photoshop SDK code or Adobe asset.

Face-aware Liquify is deliberately excluded. US 9,646,195 remains active and claims face-landmark meshes, automatic facial feature regions, combined deformation fields, and feature-specific controls. Never add face detection, face landmarks, automatic eye/nose/mouth regions, face-specific sliders, or a UI that derives deformation controls from a detected face without a new legal review. A generic user-painted mask or manual brush is not permission to add an automatic face path behind it.

Relevant public records:

- [Adobe classic brush-warp patent family (US 7,098,932)](https://patents.google.com/patent/US7098932B2/en)
- [Adobe face-aware Liquify patent (US 9,646,195)](https://patents.google.com/patent/US9646195B1/en)
- [Adobe Liquify user documentation](https://helpx.adobe.com/photoshop/desktop/effects-filters/artistic-stylize-filters/use-liquify-to-distort-an-image.html)

## Regression coverage

- `liquify_mesh_is_deterministic_and_reconstructable`
- `liquify_freeze_mask_protects_the_deformation_field`
- `liquify_render_preserves_identity_and_scales_the_field`
- `ui_filter_catalog_and_menu_contracts_are_stable`
- `ui_liquify_dialog_exposes_manual_tools_and_brush_controls`
- `ui_liquify_action_applies_selection_as_one_undo_step`

Keep an exact identity-render test. A zero-displacement mesh must return byte-identical pixels. Keep cancellation and monotonic progress covered independently from tool math.
