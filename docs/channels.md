# Document channels

Patchy keeps Photoshop document channels separate from layer masks. `Layer::mask()` remains the one raster mask applied to a layer. Saved alpha and spot channels live in `Document::channels()` as ordered, full-canvas grayscale `DocumentChannel` objects at the document's bit depth (8, 16, or 32 bits; [high-bit-depth.md](high-bit-depth.md)).

## Model and editing rules

- Every channel has an app-stable `ChannelId`, name, `Alpha` or `Spot` kind, COW pixel buffer, content revision, optional Photoshop alpha identifier, normalized display options, and the original PSD display record when one was imported.
- Mutable pixel access bumps the channel revision. Read-only code must use a const channel so thumbnail and overlay caches stay valid.
- Alpha channels use black for new canvas area. Spot channels use white, which means no spot ink. Image resize, canvas resize, crop, and canvas rotation transform every channel with the document.
- Saved alpha channels do not affect the normal layer composite. The Channels dock can show one as grayscale or as a colored overlay, and the mask-capable paint tools can edit it.
- Quick Mask appears in the same dock as a temporary fixed row, but remains canvas-owned selection state. It is never added to `Document::channels()` and never affects PSD channel counts or output.
- Imported spot channels are preserved and previewable but read-only. Their display record and pixel plane must survive PSD/PSB saves even when Patchy cannot interpret every field.
- Composite, Red, Green, and Blue rows are derived views of the rendered document. They are not stored channels and are read-only.
- While the active canvas simulates the print (View > Proof Colors, or the document is in Image > Mode > CMYK Proof), Cyan, Magenta, Yellow and Black rows follow Blue: the ink plates of the working CMYK separation, read-only like the components ([cmyk.md](cmyk.md)). `CanvasWidget::is_component_preview` covers all seven views; every read-only gate uses it. Row lookups go by `RowKind`, never by position.

## Channel visibility

The Channels panel's eyes choose which color channels the canvas shows (GitHub issue 412). `ChannelView` (`core/channel_view.hpp`, Qt-free, tested by `channel_view_eyes_follow_the_composite_rules`) holds the rules; each `CanvasWidget` keeps its own view, so it is per document. It is a view only: it never changes pixels, pushes no undo step, marks nothing modified and is never saved.

- Groups: Color (Red, Green, Blue) and Ink (the proof plates, only while they are listed). One group shows at a time; all of a group showing is the composite, all four plates included.
- Every eye is on while the composite shows. Hiding a channel turns the Composite eye off. The last visible channel's eye does nothing. The Composite eye, the Composite row (also when already selected) and Ctrl+2 turn every channel on and target the composite.
- Rows select, eyes show. Clicking a row (or Ctrl+3 to Ctrl+5: Red, Green, Blue; in a CMYK Proof document with plates listed, Ctrl+3 to Ctrl+6: Cyan, Magenta, Yellow, Black) shows that channel alone and targets it, a hidden one included; clicking the targeted row again does the same. Command ids `channel.view_composite`, `channel.view_first` to `channel.view_fourth`; the actions sit on the main window, so they work with any panel tab in front. Each row picked by one shows that action's current key at its right in native text (`MainWindow::refresh_channel_row_shortcuts`, rerun on the actions' `changed`, so a rebinding shows at once; plates show none under Proof Colors in an RGB document, where the keys still pick colors). The row delegate elides the name before the key and drops a key that cannot fit (`ui_channel_rows_show_their_shortcuts`).
- An eye click changes what shows, never what is targeted, unless the target stops being valid. It never selects its own row or a hidden channel. One channel left showing becomes the target (a single channel is a read-only view); when the composite was targeted, showing a second color channel again returns to it (`ChannelView::composite_target_on_reshow`). With two or more showing, the target stays when still shown, a composite target included (Color only: painting goes on into every channel with one hidden from view). Plates are read-only views, so a composite or hidden plate target moves to the first visible plate. The other group's eyes read off; clicking one shows that channel alone. Component and plate targets stay read-only.
- The panel keeps an eye press, a Ctrl-click press and the pointer moves while the button is held from the list: its drag selection would select the row under the pointer, the one whose eye was clicked, on the smallest move (the #412 follow-up). `ChannelPanel::set_rows` updates rows in place when only their state changed, so eye clicks do not rebuild the list.
- Display: one channel uses the existing grayscale component or plate view. Two colors draw the frame, then multiply the exposed document rect by the shown channels' color (`CompositionMode_Multiply`), so the cost follows screen pixels and every render path (stroke patches, drag previews) is covered; transparent areas tint the checkerboard. Two or three plates multiply each visible ink alone on paper (`plate_ink_colors_`, 256 levels per plate per working space), as New Layer from Channel Color stacks them.
- Other targets: a layer, a layer mask or a saved channel overlay keeps a two-color view (`channel_view_for_target`); anything else resets to the composite. A saved alpha or spot channel shown alone in grayscale shows every color eye off; clicking a color eye or the Composite eye then brings the colors back under it, the saved channel staying selected as an overlay. Saved channels' eyes are their overlay switch (what was the overlay checkbox). Quick Mask has no eye.
- The status-bar chip names the visible channels ("Viewing channels: Red, Green") and returns to the composite when clicked.
- Grayscale, CMYK and other non-RGB files open as RGB (features.md), so they show the Red, Green and Blue rows; a real single-channel document has no eyes to toggle.

## PSD and PSB layout

The wire layout (final image-data plane order, the negative-layer-count merged-transparency marking, image-resource alignment for 1006/1045/1053/1007/1077, the 56-channel cap, and the byte-stable no-channel path) lives in the saved-channels section of [file-formats.md](file-formats.md). Model-side rule: the merged-transparency plane is never added to `Document::channels()`.

## Photoshop 2026 ground truth

`test-fixtures/psd/photoshop-saved-channels.psd` is authored by Photoshop 2026. It contains two duplicate-named alpha channels with different inversion modes and one Unicode-named spot channel, each with distinct display metadata and pinned pixel samples. The core test `psd_photoshop_saved_channels_fixture_imports_and_resaves` imports it and writes a Patchy counterpart.

Photoshop 2026 opens that counterpart with the same channel count, order, names, kinds, colors, opacity or solidity, and sample bytes. It can save and reopen the file without changing those values. The fixture also pins Photoshop's resource-1053 behavior: the two alpha identifiers are present and the spot channel has no identifier entry.

## Flat-image alpha

PNG, BMP, TIFF, TGA, and WebP alpha keeps the existing flat-import behavior: it becomes a marked layer mask so it affects the canvas and can be exported non-destructively without erasing covered RGB values. That marker is not a PSD saved channel. Layered PSD saves keep it as a real layer mask and write merged transparency from the rendered document.

A saved alpha read from PSD/PSB always becomes a document channel, including channels named `Alpha 1`, uniform channels, and channels in multi-layer documents. Name-based mask recovery is forbidden because it confuses saved selections with merged transparency.

## UI and performance

- Layers and Channels share top tabs in the right dock, with Layers active initially.
- Channel actions use a compact icon row with localized tooltips, and the same actions appear when a channel row is right-clicked.
- The context menu adds New Layer from Channel (`channelNewLayerAction`, any row but Quick Mask: an opaque grayscale layer of the row's values as Load as Selection reads them, Normal, named "<channel> channel") and New Layer from Channel Color (`channelNewColorLayerAction`, components and ink plates only: a component in its own color over the composite's alpha in Screen, a plate as that ink alone prints on paper in Multiply, so the stacked set rebuilds the image). Both go above the active layer and return the canvas to layer content; `MainWindow::channel_row_pixels` computes the row for both and for Load as Selection (`ui_channel_rows_become_new_layers`).
- Ctrl-click (Command-click on macOS) loads any channel row as a new selection without changing the active edit target. Saved alpha and spot channels keep their exact 0-255 coverage. RGB component rows use Photoshop's white-backed composite values; Composite uses Patchy's fixed 30/59/11 grayscale conversion because Photoshop's luminosity conversion depends on its color-management setup.
- Selecting an alpha channel is mutually exclusive with editing layer content or a layer mask. Clicking a Layers thumbnail returns to the corresponding layer target.
- Brush/Eraser, Fill/Clear, Gradient, Line, Rectangle/Ellipse, and Invert share the grayscale edit path. Content-only operations stay disabled while a document channel is active.
- Channel-only edits mark the document modified and participate in normal COW undo snapshots, but they do not invalidate the layer compositor. Only the dirty overlay and revision-keyed thumbnail are refreshed.
- Nothing scans a full channel during repaint. Full-size grayscale and overlay images are built only when their target or revision changes.
- The Composite, component and plate rows show real thumbnails (`MainWindow::refresh_derived_channel_thumbnails`): each thumbnail pixel averages 3x3 samples of the canvas's current composite over white, so the cost is fixed whatever the document size, and 16/32-bit documents read the already display-encoded composite. They are cached on the composite's `cacheKey` and the working space; a 120 ms timer retries while the canvas recomposites (document changes and finished strokes schedule it).
- Ctrl-clicking a layer or layer-mask thumbnail keeps its exact soft alpha. Marching ants follow the 50% boundary, while saving the selection copies mask rows or hard-region spans directly instead of probing a complex region once per canvas pixel.
- Save and Save As warn before a non-PSD/PSB format discards saved channels. Export is always an explicitly flattened operation and does not warn.

Not implemented: editable component channels, multiple simultaneous overlays, channel-options editing, spot separations, and multichannel/CMYK/Lab document modes (the CMYK plates are a proof view, not stored inks).

## Layer mask planes in PSD

- The real-user-mask channel `-3` is sized by the mask record's own real-mask rect, never the layer bounds (the two can differ). A `-3` plane without that rect is skipped by its declared byte length, which also accepts the compression-marker-only `-3` records of older files. Raster and parameterized vector mask layout is in [vector-tools.md](vector-tools.md).
- Zero-area `-2`/`-3` masks (the empty white mask on every Photoshop adjustment layer) load as empty LayerMasks whose default color supplies the coverage and are written back; see [vector-tools.md](vector-tools.md).
- Group raster masks: PSD import stores a group's mask, the layered writer emits the folder record's mask block and `-2` channel (mask-less groups keep their zero-channel records, so writer canaries are unaffected), Add Layer Mask works on groups, and the compositor applies group masks on the default pass-through path: the mask attenuates each child contribution in place via `GroupMaskedTarget` (layer_compositor.hpp), so interior adjustments still reach the backdrop below the group and nested group masks multiply. Apply Layer Mask stays pixel-only.
