# Liquify

This document is the implementation, PSD-interoperability, and legal contract for Patchy's manual Liquify workspace.

## Product and UI contract

`Filter > Liquify...` opens a dedicated brush workspace. Liquify is not a `FilterRegistry` entry: it is an ordered sequence of gestures rather than one named invocation with stable parameters. It therefore does not appear in the Filter Gallery, cannot be saved in a Look, and has no private filter identifier. Its persisted hotkey command ID is `filter.liquify`; the default is Ctrl+Shift+X.

The tools are Warp, Reconstruct, Smooth, Twirl, Pucker, Bloat, Freeze, and Thaw. Twirl rotates clockwise; Alt/Option reverses it. Brush Size is 5 through 2000 px; Pressure and Density are 1 through 100 percent. The freeze mask is an edit-time protection field shown as a blue overlay. Restore All clears both displacement and protection. Cancel changes nothing. Apply renders once from the immutable original pixels, respects the document selection, keeps the original layer bounds, and creates one undo entry. Pixels outside the source bounds are edge-clamped, so a deformation at a layer edge does not create a transparent seam.

The workspace edits a bounded proxy whose longest edge is 720 px. `LiquifyMesh` stores inverse displacement in signed 24.8 fixed point on a grid capped at 129 nodes per axis. Rendering normalizes the grid to the target dimensions, so accepting the proxy applies the same relative deformation to the full-resolution layer. Final resampling uses deterministic fixed-point bilinear weights and premultiplied RGBA. Do not replace this with `std::uniform_*`, toolchain-specific interpolation, or a per-repaint full-layer render.

Workspace controls (GitHub #296 follow-ups). The workspace keeps its own history: every stroke (Freeze and Thaw included), Restore All, and each accepted Reconstruct is one step, up to 100 (a mesh copy each, about 55 KB at the 720 px proxy). Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y (Command on macOS) and the Undo and Redo buttons walk it; a new step drops the redo steps; the document's undo stack still sees only Apply. Keys are window shortcuts on the modal dialog, which blocks the main window's application shortcuts, so none reaches the document: Photoshop's tool letters W R E C S B F D (each tool tooltip names its letter), `[` and `]` for brush size by `proportional_brush_step` (Shift for the coarse step, as on the canvas), and P for the Preview checkbox. Rate (1 to 100, default 80; Rate 100 is 60 stationary dabs per second) makes Twirl, Pucker, Bloat and Reconstruct keep adding a dab at the last point while the button is held without moving; Warp, Smooth, Freeze and Thaw act only on motion. With Stylus Pressure on (default), a pen's pressure from the last tablet event scales Pressure for the mouse events Qt synthesizes from it; the tablet event itself is left unaccepted. Show Mesh draws the grid where the image moved (a node at p drawn at p minus its inverse displacement), every node along each line and about one line per 16 screen pixels, as a dark and a light line like the brush outline. Preview off shows the original layer. Reconstruct... opens an Amount (0 to 100 percent, default 100) that `LiquifyMesh::reconstruct` applies live: each node keeps `1 - amount * (1 - freeze / 255)` of its displacement, so frozen areas stay; OK records one step and Cancel restores the mesh.

Interactive cost (GitHub #296). A brush move only updates the mesh (about 0.03 ms); the proxy renders in the preview's `paintEvent`, once per painted frame, so a burst of pointer events (high-rate mice and tablets, Wayland compositors that deliver every motion frame) never queues one render per event. Hovering repaints only the brush outline's old and new rectangles. `LiquifyMesh::render` keeps the original integer arithmetic term for term but hoists repeated work: each field is interpolated along x once per grid row, a sample at a whole-pixel position copies that pixel, and four equal alphas skip the per-channel division (it cancels exactly). Outputs of 512 Kpx or more (the full-resolution Apply) render in parallel row strips, with progress and cancellation checked between bands on the calling thread. `liquify_strokes_and_render_are_pinned` holds digests recorded before the speedup; any change to them is a change to Liquify output. The preview's blue freeze overlay clamps a bright blue at 255 (it used to leave such pixels untinted).

Freeze and Thaw modify only the protection field. Warp adds the inverse of the pointer motion. Reconstruct tends displacement toward identity. Smooth tends each protected node toward its four-neighbor displacement average. Twirl, Pucker, and Bloat add bounded inverse offsets around each brush dab. Completed displacement, not the gesture history, is the full-resolution render input.

Selection clipping never searches a `QRegion` per pixel. The preview caches a
proxy-sized byte mask when the dialog opens, mapping the region's spans through
the existing endpoint-aligned, floor-rounded source samples (including one-pixel
axes and offset layers). Every gesture reuses that mask. Final Apply restores
unselected row spans from the original pixels in one region walk. Both paths keep
the existing hard-selection behavior and remain usable with fragmented wand
selections; no full-resolution selection mask is allocated for the proxy.

## PSD and Smart Object behavior

Liquify is a destructive pixel edit on an editable RGB/RGBA pixel layer. The workspace always edits 8-bit pixels: a 16-bit layer is warped on an 8-bit copy and the change folds back at depth (`apply_eight_bit_edit_at_depth`, so untouched samples keep full precision), and 32-bit documents disable the command (`filter.liquify`; [high-bit-depth.md](high-bit-depth.md)). A PSD stores the resulting pixels through the normal layer channel path; no private Patchy resource or filter descriptor is added. Photoshop therefore sees the same raster result. Selection clipping, undo, palette compliance warnings, and subsequent PSD saves behave like other destructive pixel edits.

Do not synthesize Photoshop's native Liquify Smart Filter descriptor. Its descriptor and render-cache shape have not been calibrated from clean-room output. Patchy refuses Liquify on a Smart Object and asks the user to rasterize first. An imported Smart Object containing an unsupported Photoshop Liquify entry remains on the existing byte-preserved, preview-locked path. This prevents a direct or partial edit from dropping unknown native fields or replacing Photoshop's stored preview.

If native Smart Filter support is added later, first capture Photoshop-authored before/after PSDs that differ in one manual setting at a time, document the descriptor and FEid behavior in [smart-filters-native.md](smart-filters-native.md), and make the whole-stack support decision fail closed. Do not infer a descriptor from UI labels or copy Adobe specification prose.

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
- `liquify_strokes_and_render_are_pinned` (every tool, RGBA with zero and partial alpha, RGB, scaled, capped-grid and one-pixel outputs)
- `liquify_reconstruct_by_amount_respects_freeze`
- `ui_liquify_undo_redo_steps_strokes_and_restore`, `ui_liquify_keys_choose_tools_and_size_without_reaching_the_main_window`, `ui_liquify_rate_and_pen_pressure`, `ui_liquify_mesh_preview_toggle_and_reconstruct_amount`, and `ui_liquify_dialog_captures_in_both_schemes` (issue shots in `test-artifacts/liquify`)
- `patchy_perf_tests liquifydrag`: dialog open, each Warp move plus its preview repaint at the default and the largest brush, and Apply on 4000x3000 and 6000x4000 layers, then the proxy's per-stroke and per-render cost. Ceilings: frame median under 10 ms, Apply under 400 ms. October 2026 on studiomac (M2 Ultra): frames 15 to 18 ms and Apply 0.47 s / 0.95 s before #296, 2 to 4 ms and 14 / 20 ms after.
- `ui_filter_catalog_and_menu_contracts_are_stable`
- `ui_liquify_dialog_exposes_manual_tools_and_brush_controls`
- `ui_liquify_action_applies_selection_as_one_undo_step`
- `ui_liquify_fragmented_selection_preview_and_apply`
- `ui_liquify_preview_selection_preserves_sample_mapping` (offset layers, clipped
  regions, downsampled proxies, and one-pixel axes compared with scalar sampling)

Keep an exact identity-render test. A zero-displacement mesh must return byte-identical pixels. Keep cancellation and monotonic progress covered independently from tool math.
