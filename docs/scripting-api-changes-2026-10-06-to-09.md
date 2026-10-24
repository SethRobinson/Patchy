# Scripting API changes: archive 2026-10-06 to 2026-10-09

Entries of the permanent scripting API change log, moved here unchanged when
[scripting-api-changes.md](scripting-api-changes.md) reached its size limit. Newer entries
live there, older ones in
[scripting-api-changes-2026-10-01-to-05.md](scripting-api-changes-2026-10-01-to-05.md).
`app.apiVersion` stayed 1 throughout. Newest first.

2026-10-09 (additive): `patchy.scripts` (`PatchyScripts` in patchy.d.ts): `userFolder`,
`bundledFolder`, `list()` / `rescan()` (every script with its effective `hotkey`,
`defaultHotkey` and `commandId`), `install(relativePath, source, {hotkey})` (writes below the
user folder, rescans, binds the Preferences override), `setHotkey` / `getHotkey`. The File >
Scripts entries gain the Script Manager's right-click menu. Pinned by
`ui_script_library_installs_and_binds_hotkey` and `ui_scripts_menu_context_menu_offers_script_actions`.

2026-10-09 (additive): `doc.saveAs(path, options?)` and `doc.exportAs(path, options?)`
take the format's save options (`PatchySaveOptions` in patchy.d.ts: JPEG/WebP/JXR/RTTEX
`quality`, WebP/JXR/PDF `lossless`, DDS `compression` and `mipmaps`, ICO/CUR `sizes`,
`resample` and `hotspot`, BMP `encoding`, `paletteMode` and `palettePath`, RTTEX `encoding`,
`powerOfTwo`, `forceSquare`, `forceAlpha` and `compress`, the exportPdf keys, GIF `animate` and
`frameDelayMs`). Unspecified keys follow the user's Save Options defaults; an unknown key, a
wrong type, an out-of-range value, or a key for another extension throws before anything is
written. A scripted save no longer rewrites the user's persisted saveOptions defaults, and
scripted saves skip the saved-channels and Aseprite fill-opacity prompts (the script decided).
Pinned by `ui_script_save_as_options_write_dds_and_jpeg` and `ui_script_save_as_options_are_strict`.

2026-10-09 (behavioral correction): `doc.exportAs` is a copy export. It used to be
identical to `saveAs`, so a flat document exported as .dds became the .dds session; now the
document's path, title and modified state never change, whatever the format. Scripts that
relied on exportAs retargeting the document should call saveAs.

2026-10-09 (additive): per-script hotkeys. A `// @hotkey Ctrl+Alt+D` header line is a
script's default shortcut; Preferences > Hotkeys lists scripts under "Scripts" and its
override wins. The command id is `script.` plus the percent-encoded relative path
(`script.Utilities%2Fquick-export-dds.js`), listed by `app.commandIds()`; `app.runCommand`
refuses those ids because one script runs at a time. New bundled example
`Utilities/quick-export-dds.js`. See [scripting.md](scripting.md) "Script hotkeys".

2026-10-09 (additive): `addTextLayer(text, {area})` creates area text from one
closed `PatchyVectorPath` in document coordinates. `area` and `box` are mutually
exclusive. `layer.textArea` reads a detached boundary snapshot or `null`; assigning
a boundary reflows through the text session, and assigning `null` converts it to
box text. Source shapes remain independent. See [area-text.md](area-text.md).

2026-10-09 (behavioral): 16 and 32-bit editing is on by default (`PATCHY_DEEP_EDITING=0`
turns it off). A 16 or 32-bit file opened with `app.open` keeps its depth (`doc.bitDepth`
reports it; with the gate off it still decodes to 8 bits) and `doc.convertBitDepth` works
without opting in; `app.newDocument` stays 8-bit. `layer.applyPlugin` runs on 16-bit layers
through an 8-bit copy whose change folds back at depth, and still throws for a 32-bit
layer. On a 32-bit document the `layer.blendMode` setter throws for the modes Photoshop
refuses at 32 bits (Color Burn, Linear Burn, Screen, Color Dodge, Overlay, the Light modes,
Hard Mix, Exclusion), and `convertBitDepth(32)` sets layers in those modes to Normal. The
web build throws for `convertBitDepth(32)`. See [high-bit-depth.md](high-bit-depth.md);
pinned by `ui_script_bit_depth_and_deep_filters`.

2026-10-09 (additive): `doc.bitDepth` (8, 16 or 32) and `doc.convertBitDepth(bits)`
(Image > Mode's conversion; throws for other values, while 16 and 32-bit editing is off,
or on an Indexed document). On 16 and 32-bit documents `getPixels` and `setPixels` stay
RGBA8 and convert at the boundary, `fill` and `fillRect` write at the document's depth,
and `applyFilter` runs at depth, throwing for a filter a 32-bit document does not offer.
See [high-bit-depth.md](high-bit-depth.md); pinned by `ui_script_bit_depth_and_deep_filters`.

2026-10-07 (additive plus behavioral correction): `doc.resizeImage(width, height, {method})`
takes a resampling method id: `"automatic"` (default), `"nearest"`, `"bilinear"`, `"bicubic"`,
`"bicubicSmoother"`, `"bicubicSharper"`; an unknown id throws. A resize without a method is
Automatic (Bicubic Sharper for a reduction, Bicubic Smoother for an enlargement) instead of
the old fixed bilinear, 16-bit and float documents are filtered instead of nearest-sampled,
reductions average their footprint, and alpha interpolates premultiplied. The Image Size
dialog now honors its method combo and remembers the choice. See
[resampling.md](resampling.md); pinned by `ui_script_resize_image_method_option` and the
`resample_*` core tests.

2026-10-06 (behavioral): `ui.zoom` reads and writes the view zoom in percent, where
100 is one document pixel per device pixel (the status-box number). On a HiDPI or scaled
display the value therefore differs from the logical widget scale by the device pixel
ratio; at ratio 1 nothing changes. Matches Photoshop's 100% (GitHub issue 75).
