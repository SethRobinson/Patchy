# Document channels

Patchy keeps Photoshop document channels separate from layer masks. `Layer::mask()` remains the one raster mask applied to a layer. Saved alpha and spot channels live in `Document::channels()` as ordered, full-canvas, 8-bit grayscale `DocumentChannel` objects.

## Model and editing rules

- Every channel has an app-stable `ChannelId`, name, `Alpha` or `Spot` kind, COW pixel buffer, content revision, optional Photoshop alpha identifier, normalized display options, and the original PSD display record when one was imported.
- Mutable pixel access bumps the channel revision. Read-only code must use a const channel so thumbnail and overlay caches stay valid.
- Alpha channels use black for new canvas area. Spot channels use white, which means no spot ink. Image resize, canvas resize, crop, and canvas rotation transform every channel with the document.
- Saved alpha channels do not affect the normal layer composite. The Channels dock can show one as grayscale or as a colored overlay, and the mask-capable paint tools can edit it.
- Imported spot channels are preserved and previewable but read-only. Their display record and pixel plane must survive PSD/PSB saves even when Patchy cannot interpret every field.
- Composite, Red, Green, and Blue rows are derived views of the rendered document. They are not stored channels and are read-only.

## PSD and PSB layout

The final image-data section is written in this order:

1. RGB component planes.
2. The derived merged-transparency plane, only when the layered composite has transparent pixels.
3. Stored document channels in document order.

A negative layer count identifies plane 2 as merged transparency. It is never added to `Document::channels()`. Positive-count layered files and flat files have no structural merged-transparency plane, so all planes after the source color components are saved channels.

Channel names and display records are aligned by extra-plane order across image resources 1006 (legacy Pascal names), 1045 (Unicode names), and 1007/1077 (display information). Resource 1006 gets an ASCII fallback with one `?` per non-ASCII character; the exact UTF-8 name is carried through 1045. Resource 1053 is different: Photoshop writes identifiers for saved alpha channels only, skipping merged transparency and spot channels. Import and export therefore advance its index only for alpha channels. The Unicode name is authoritative. Opaque display records travel with their channel so reordering does not detach spot metadata from its pixels.

The writer enforces Photoshop's 56-total-channel limit and errors instead of dropping data. The no-saved-channel path stays byte-identical to the historical writer.

## Photoshop 2026 ground truth

`test-fixtures/psd/photoshop-saved-channels.psd` is authored by Photoshop 2026. It contains two duplicate-named alpha channels with different inversion modes and one Unicode-named spot channel, each with distinct display metadata and pinned pixel samples. The core regression test imports it and writes a Patchy counterpart.

Photoshop 2026 opens that counterpart with the same channel count, order, names, kinds, colors, opacity or solidity, and sample bytes. It can save and reopen the file without changing those values. The fixture also pins Photoshop's resource-1053 behavior: the two alpha identifiers are present and the spot channel has no identifier entry.

## Flat-image alpha

PNG, BMP, TIFF, TGA, and WebP alpha keeps the existing flat-import behavior: it becomes a marked layer mask so it affects the canvas and can be exported non-destructively without erasing covered RGB values. That marker is not a PSD saved channel. Layered PSD saves keep it as a real layer mask and write merged transparency from the rendered document.

A saved alpha read from PSD/PSB always becomes a document channel, including channels named `Alpha 1`, uniform channels, and channels in multi-layer documents. Name-based mask recovery is forbidden because it confuses saved selections with merged transparency.

## UI and performance

- Layers and Channels share top tabs in the right dock, with Layers active initially.
- Channel actions use a compact icon row with localized tooltips, and the same actions appear when a channel row is right-clicked.
- Ctrl-click (Command-click on macOS) loads any channel row as a new selection without changing the active edit target. Saved alpha and spot channels keep their exact 0-255 coverage. RGB component rows use Photoshop's white-backed composite values; Composite uses Patchy's fixed 30/59/11 grayscale conversion because Photoshop's luminosity conversion depends on its color-management setup.
- Selecting an alpha channel is mutually exclusive with editing layer content or a layer mask. Clicking a Layers thumbnail returns to the corresponding layer target.
- Brush/Eraser, Fill/Clear, Gradient, Line, Rectangle/Ellipse, and Invert share the grayscale edit path. Content-only operations stay disabled while a document channel is active.
- Channel-only edits mark the document modified and participate in normal COW undo snapshots, but they do not invalidate the layer compositor. Only the dirty overlay and revision-keyed thumbnail are refreshed.
- Nothing scans a full channel during repaint. Full-size grayscale and overlay images are built only when their target or revision changes.
- Ctrl-clicking a layer or layer-mask thumbnail keeps its exact soft alpha. Marching ants follow the 50% boundary, while saving the selection copies mask rows or hard-region spans directly instead of probing a complex region once per canvas pixel.
- Save and Save As warn before a non-PSD/PSB format discards saved channels. Export is always an explicitly flattened operation and does not warn.

Deferred work: editable component channels, multiple simultaneous overlays, channel-options editing, Quick Mask, spot separations, multichannel/CMYK/Lab document modes, 16/32-bit channel editing, vector masks, and PSD real-user-mask channel `-3`.
