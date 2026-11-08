# Filter Gallery and filter dialog previews

Split from [filters.md](filters.md): the Filter Gallery dialog (items, search, favorites, settings keys, object names, outcome surfacing, overlay controls, preview pipeline) and the bounded preview inside the direct filter dialogs.

## Filter Gallery

`Filter > Filter Gallery...` is the shared browsing entry point; persisted hotkey command ID `filter.gallery`, no default shortcut. Direct Filter-menu actions keep their IDs, defaults and output.

- Original is a UI sentinel (no ID or invocation), listed first. Real items carry their ID in `Qt::UserRole + 1` (Original: empty) and a session-only SF badge flag in `Qt::UserRole + 6`. Items are pre-created in catalog order and filtered in place. Thumbnail readiness is the ready role (`Qt::UserRole + 2`), never icon nullity.
- Search: case-insensitive, translated and English filter and category names. Hiding the selected filter selects Original without clearing the recipe; clicking Original clears it.
- Favorites persist by ID in catalog order; loading drops missing/duplicate IDs; a toggle saves at once (Cancel keeps it).
- Settings keys (prefix `filters/gallery/`): `favorites` (ordered list of valid IDs), `category` (one of the eleven tokens; unknown falls back to `all`), `lastFilterId` (empty = Original; restored only when still visible), `liveCanvasPreview` (bool), `size` (880x560..3200x2400, default 1120x720). Search, zoom, pan and parameters are session-only.

Widget object names are test contracts prefixed `filterGallery` (list in the dialog source); catalog controls keep their catalog roots. The center/radius overlay lives inside `filterGalleryPreview`, not a separate child widget.

Outcome surfacing (before Apply): an unlocalized "SF" thumbnail chip, explained in the row tooltip, marks filters with a native mapping (`native_smart_filter_kind_for`). `filterGalleryOutcomeLabel` states what Apply will do; for a Smart Object it mirrors the caller's whole-recipe predicate (`smart_filter_stack_with_recipe`, which adds the 64-entry native stack cap); an empty recipe keeps the positive text. Rows whose single entry fails the real mapper (parameter gates like Emboss Amount 0 included) get a warning icon and tooltip; mapping is all-or-nothing, disabled entries included. A recipe rejected only by the entry cap flips the outcome line but marks no row.

The angle dial syncs with the numeric controls; its hand wraps but Twirl keeps the full -720..720 value. The Wave graph drags amplitude (vertical) and phase (horizontal), wheel for wavelength. Both appear in the direct dialogs too (`filterAngleDial`, `filterWaveformControl`).

Overlay controls:

- Radial Blur, Twirl, Pinch/Bloat, and Lens Vignette declare `center_x`/`center_y` with the center roles: a draggable crosshair (values quantize to the declared step). Twirl and Pinch/Bloat also mark integer `radius` as `EffectRadiusPercent`, adding a draggable radius circle. Drags update overlay and values at once; a size-changing center proxy is adopted only on release, so it cannot jump under the pointer.
- Tilt-Shift Blur adds center, rotation and width handles: a filled dot per focus boundary and a hollow dot per full-blur boundary, with no line or bar along either boundary (Apple US 8971623, docs/patent-research-audit.md section 3, decided 2026-10-20). The overlay clips inside the image perimeter, including tiny, rotated and panned previews. `ui_filter_gallery_tilt_shift_overlay_has_no_boundary_marks` pins both. Width edits stay symmetric; rendering waits for release.
- Iris Blur uses the center and angle roles (crosshair and dial); Width, Height and Focus stay numeric. No editable iris boundary widget and no multiple pins: the one ellipse is edited numerically and makes one scalar blend mask (docs/patent-research.md).

Preview pipeline:

- All preview work starts from an immutable copy of the active layer. Center and thumbnail proxies are bounded (`kFilterProxyMaximumDimension`, `kMaximumThumbnailDimension`), premultiplied bilinear, selection scaled alike. Only pixel-distance parameters scale (`FilterRegistry::scale`); canvas preview and Apply use the unscaled recipe.
- Thumbnails are single-filter previews; center and live-canvas previews render the whole recipe from the immutable original. Rendering traces input bounds for every entry (disabled and zero-opacity included); the active center/radius control maps from its entry's traced input rectangle into the displayed bounds. A selection fixes every entry to the original local bounds (canvas-filling exception: [filters-spatial-bounds.md](filters-spatial-bounds.md)).
- A plain-layer recipe with an enabled, nonzero-opacity Clouds entry previews (center and canvas) through the full-resolution exact path from the embedded document-wide source, as Smart Object targets do, so preview matches Apply; other recipes keep the bounded proxy. Only Smart Object thumbnails use the exact renderer.
- Thumbnails generate lazily, one visible filter per timer turn; editing a filter refreshes its row icon. The session cache favors newly visible missing thumbnails, never invalidating finished ones.
- Live-canvas preview requests carry increasing generations: only the newest finished worker updates the canvas, only while the dialog is open; one runs at a time, the newest pending wins. Past the standard overlay delay an in-flight worker shows the "Rendering preview..." canvas badge (`CanvasWidget::begin/end_preview_render`, as in the adjustment dialogs). The center preview follows the same rule on its own debounced worker.
- The momentary Before button shows the immutable source aligned to the current result bounds (zoom and pan kept), leaving the live canvas preview alone. Live Canvas Preview defaults on; re-enabling reapplies the current result.
- Cancel restores the original pixels and bounds exactly with no undo entry; a clean document stays clean. Apply renders the recipe once more from the immutable original as one undo entry. Original or a recipe with no enabled nonzero-opacity entry closes without one.

## Direct filter dialog preview

`MainWindow::apply_filter` passes a `FilterDialogPreviewSource` (immutable pixels, bounds, selection, registry); `request_filter_settings` then shows a bounded proxy preview above the generated controls. A canvas-filling source carries the embedded document-wide buffer and union bounds, so proxy, canvas preview and Apply agree; the pristine snapshot drives Cancel. The smart-filter editing dialogs pass no source and show no preview. The selection stays editable meanwhile ([selection-tools.md](selection-tools.md#selection-edits-under-a-filter-dialog)).

The preview reuses the gallery machinery: `src/ui/filter_preview_proxy.{hpp,cpp}` (the bounded proxy, `render_filter_proxy`, the latest-generation worker) and `src/ui/filter_overlay_sync.{hpp,cpp}` (overlay geometry). The invocation renders as a debounced one-entry recipe; the traced input rectangle drives the same crosshair, radius circle, tilt-shift handle dots, and iris controls as the gallery. Overlay DRAWING stays solely in `zoomable_image_preview.cpp` (the handle-only tilt-shift overlay is a reviewed design; never duplicate or alter that rendering).

A filter with `kFilterDialogTwoColumnControls` or more controls (Flame) puts the preview on the left and the controls in two columns on the right (`FilterParameterPanelOptions::two_columns_from`), to fit 720 logical pixels; others keep the preview above one column. A `RandomSeed` control shows its spin box and a framed Randomize push button, no slider.

Object names: `filterDialogPreview` (a `ZoomableImagePreview`; `filterDialogRenderedFilterId` mirrors the gallery's) and the zoom row centered under it, `add_preview_zoom_controls` with prefix `filterDialog` (the row Export for Web uses: [web-export.md](web-export.md#preview-engine)). OK is as wide as Cancel (`match_ok_cancel_widths`). The preview's minimum height is `kFilterDialogPreviewMinimumHeight` (220 px).

`filterPreviewCheck` gates only the live canvas preview; the in-dialog proxy always renders, display-only. It sits at the left of the button row, just before Reset, in a footer layout outside the `QDialogButtonBox` (as Shape Appearance does), so no platform's button order moves it and toggling it never accepts or rejects; About follows Reset in the Reset role. Every dialog `request_filter_settings` shows, Smart Filter dialogs included, starts it from the persisted key `filters/dialogPreview` (bool, absent means on) and writes the key on each toggle, so the next filter dialog opens as the user last left it. The canvas shows the original when the dialog opens: a dialog opened with Preview off sends no request at all, and one turned off after a canvas preview sends one preview-off request (the caller puts the original back and drops a render in flight), then nothing until Preview is on again. Reset leaves Preview as it is (GitHub issue 667; `ui_filter_settings_dialog_preview_off_sends_no_canvas_requests`, `ui_filter_settings_dialog_preview_beside_reset_and_remembered`, `ui_filter_dialog_opened_with_preview_off_never_touches_canvas`).
