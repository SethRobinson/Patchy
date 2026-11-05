# Files and formats

Split from [features.md](features.md): files and multiple documents, the formats Patchy reads and writes, camera raw and HDR photos, PDF documents, and what layered files from other editors keep.

## Files, photographs, and multiple documents

- Multiple document interface: tabs that can float in their own windows, Tile and Cascade, a Window menu of open documents, and layers that drag or duplicate between documents. Opening a file that is already open switches to its tab; Revert (F12, or the tab's right-click menu) reloads it. Open a folder of images as tabs (or drop it on the window); export every open document as a numbered image set to a folder
- New Document templates in seven groups (screens, phones and tablets, social, web and ads, print paper, photo prints, icons and game art) showing paper sizes in mm or inches, a search across groups, a document name, color mode and bit depth (8 by default), an optional empty layer above the Background, and saved presets
- The Home Screen (shown when no document is open) has New..., Clipboard and Open... buttons and a filterable list of recent files. Clipboard opens the copied image as a new document, like File > Open from Clipboard, and is grayed out while the clipboard holds no image
- Import multiple files as layers, preserving a layered file's contents in a group, through File > Import, a drop on the Layers panel, or pasted files. Paste a path copied as text to open it (Paste in Place: as a layer)
- Scan and Divide Photos splits prints into images with editable crop regions, straightening and perspective correction. Divide an existing scan or photo too; scanner acquisition and the scanner-to-printer Photocopy command run on every desktop build
- Automatic document recovery saves copies of modified documents at a set interval and reopens them after a crash
- Export for Web: JPEG, JPEG XL, WebP, AVIF, PNG/APNG, GIF, live sizes, 4-up view, presets
- Reads and writes PSD/PSB, [PDF](#pdf-documents), PNG, JPEG (12-bit files open at 16 bits), TIFF (also float, 12 and 32-bit integer and planar files with extra channels, which open as alpha channels; multi-page files open their pages as layers, as separate documents, or the first page only), WebP, BMP, TGA, GIF, PCX, Amiga IFF/LBM, Windows icons and cursors (ICO/CUR), OpenRaster (.ora), Aseprite files, JPEG XL (.jxl, 16/32-bit stills and animation), JPEG 2000 (.jp2), OpenEXR (.exr), Radiance HDR (.hdr/.rgbe), AVIF (desktop), JPEG XR (.jxr, Windows), Proton SDK textures (.rttex), DDS textures (.dds: reads BC1 to BC7, cubemaps, arrays and mipmaps; writes uncompressed, DXT1, DXT5, BC4, BC5 or BC7, mipmaps optional), and SVG (opens as editable shapes, exports vectors)
- Import notes (what a file could not keep) flash yellow in the status bar or in a popup with "Don't show this again"
- Imports Affinity, Paint.NET, Krita and GIMP (.xcf) documents, and reads and writes OpenRaster, layered TIFF, and Aseprite documents. See [layered format interchange](#layered-format-interchange) for what stays editable and what each format cannot preserve
- Opens camera raw files (CR2/CR3/NEF/ARW/RAF/DNG and more) with 16-bit development processing, a Natural rendering profile, ISO-based noise reduction, and per-photo settings saved beside the original. The result is 8-bit. HEIC/HEIF photos open through platform codecs, with supported high-bit-depth and HDR input depending on the decoder
- The Develop Raw dialog shows a live RGB and luminance histogram of the preview: pointing at it names the tone setting that part adjusts (Shadows, Exposure, Highlights) and dragging there changes it. Shift-click a slider or its label for an automatic value and Alt-click to reset it. The preview zooms from a zoom menu, 100% and Fit buttons, and an optional Scrubby Zoom drag
- Filter > Camera Raw Filter (Shift+Ctrl+A) brings the Develop Raw white balance, exposure, tone and color controls to any RGB pixel layer at 8, 16 or 32 bits, with the same histogram, slider clicks and zoomable preview. Temperature and tint are relative to the layer's colors, an Auto button neutralizes the average color, and an active selection limits the change. Applies as one undo step; scripts call `layer.applyCameraRawFilter`
- Opens floating-point JPEG XR HDR screenshots as 32-bit linear documents on Windows, keeping highlight values. With deep editing disabled they tone map to 8-bit; JPEG XR saves are always 8-bit. OpenEXR and Radiance HDR also open as 32-bit documents and export flattened HDR images; see [HDR format support](hdr-formats.md)

## PDF documents

- Import selected PDF pages on desktop as editable text, vector shapes and images, or as flattened images at a chosen resolution (72 to 600 ppi presets or any value). Pages can open as separate documents, as one document with a folder, layer or artboard per page. Choose RGB, Grayscale, CMYK Proof, or Indexed Color, 8, 16, or 32 bits per channel, a white or transparent page background, and reverse page order
- Export a single document or assemble a multi-page PDF from open documents, top-level layer groups, or artboards, with page ordering and editable or flattened output. A document with artboards saves as one PDF page per artboard, each its own size
- Editable export preserves supported text and vectors. Effects and unsupported combinations are rasterized with notices; it does not preserve every PSD layer property. Imported PDFs save as a new copy so the original is not overwritten by a normal Save
- Lossless and JPEG quality options; supported, unchanged scanned pages keep their original compressed data (no re-encoding). PDF export also works in the browser; PDF import requires the desktop app. See [PDF support and limitations](pdf.md)

## Layered format interchange

These formats preserve different kinds of content. Import-only formats can be edited
in Patchy, then saved as PSD or another supported format; Patchy cannot write back to
their original format.

| Format | Open / save | What carries over and the main limits |
| --- | --- | --- |
| **Affinity** (.af, .afphoto, .afdesign, .afpub) | Import only | Supported text, vector shapes, masks, groups, adjustments, effects, artboards, and placed images. Raster content imports at 8 bits; multi-page files open the first spread. [Details below](#affinity-import) |
| **OpenRaster** (.ora) | Open and save | Pixel layers, groups, names, positions, visibility, opacity, supported blend modes, and image-pixel locks, at 8 or 16 bits. Text, vectors, masks, effects, and Smart Objects bake into pixels; adjustments and some combinations merge with layers below. Clipping and extra blend modes depend on the receiving app. [Interchange details](ora.md) |
| **Krita** (.kra, .krz) | Import only | Pixel layers at 8, 16 or 32 bits (grayscale and CMYK convert to RGB), groups and pass-through groups, names, positions, visibility, opacity, locks, blend modes, transparency masks, Inherit Alpha (as clipping), color-adjustment filter layers and masks (as adjustment layers), color fill layers, vector shapes and text (through the SVG importer), layer styles, clone and file layers (as pixel copies), guides and resolution. Filters, fills and masks Patchy has no equivalent for are left out with a notice. [Import details](kra.md) |
| **Paint.NET** (.pdn) | Import only | Pixel layers, names, visibility, opacity, blend modes, resolution, EXIF, XMP, and the saved palette. Text is already pixels in the source file. Paint.NET-specific blend modes can require baking when saving to another layered format; damaged or oversized files may fall back to their small preview with a notice. [Import details](pdn-format.md) |
| **GIMP** (.xcf, also .xcf.gz/.bz2/.xz) | Import only | Layers, groups (pass-through included), masks, opacity, visibility, locks, color tags, blend modes (GIMP-only ones become Patchy modes a PSD save bakes), every precision at 8, 16 or 32 bits, gray and indexed images (indexed can open in Palette mode), editable text (painted-on text can open as text), vector layers as shapes, drop shadows as styles, most GIMP filters applied, channels, paths, guides, resolution, the color profile, comment, Exif, XMP and IPTC. Curves and Color Balance are skipped (notice); soft edges can differ (GIMP blends in linear light). [Import details](xcf.md) |
| **Layered TIFF** (.tif, .tiff) | Open and save | Photoshop-style layer data at 8, 16, and 32 bits, including supported text, masks, and effects. Choose Keep Layers when saving; other TIFF readers may show only the merged image. [TIFF details](tiff.md) |
| **Aseprite** (.ase, .aseprite) | Open and save | The first frame's pixel layers, groups, opacity, and supported blend modes. Saves are a single frame at 8 bits, with procedural content rasterized. [Format details](file-formats.md#per-format-catalogue) |

Keep a PSD master when you need
Patchy's editable text, effects, masks, or Smart Objects. Import notices and save
warnings describe conversions that affect a particular document.

### Affinity import

Patchy imports Affinity documents: current .af ("Affinity by Canva"), Affinity 2 (.afphoto, .afdesign, .afpub) and most Affinity 1.x files. Raster layers (16-bit, float, CMYK and Lab become 8-bit RGB), groups, masks (raster and vector), clipping, blend modes, opacity, editable text with per-run styles, vector curves, parametric shapes, compound-shape booleans, Designer symbols, artboards, layer effects, supported adjustment layers, and placed images (as embedded Smart Objects) import. Strokes keep their width, alignment, dashes and miter limit, crop-to-shape containers become masked groups, and the Erase blend mode becomes an isolated group with an inverse-alpha mask. A file that can't import as layers opens as its embedded preview, saying what was skipped.

Affinity features not supported yet, or only partly supported:

- Saving to Affinity formats (import only; save your edits as PSD)
- A few parametric shape kinds (callouts, spirals, QR codes, circle-rounded stars, and exotic arrow ends) import as named placeholders
- Adjustment layers beyond the eight kinds the importer maps (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold), and live filters, import as named empty placeholders; Brightness/Contrast and Color Balance import approximately
- Affinity-only blend modes render through their closest Photoshop-compatible equivalent with a notice (Average matches exactly at half opacity; Negation, Reflect, Glow, and Pigment approximate; Contrast Negate falls back to Normal)
- Bevel/Emboss and glow effects are approximated; Gaussian blur layer effects bake into the layer pixels (they render correctly but are no longer live)
- Rotated or sheared frame text renders without its rotation (artistic text rotates correctly)
- Multi-page documents open the first spread only
- Affinity 1.x-era files: embedded-document placement can land slightly off
