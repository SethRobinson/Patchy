# Features and compatibility

[Back to Patchy](../README.md) | [Screenshot gallery](screenshots.md)

## Photoshop documents and non-destructive editing

- Open and save layered PSD and PSB files with groups, masks, clipping masks, saved alpha and spot channels, text objects, Fill Opacity, the full Photoshop blend mode set, layer styles and more
- 8, 16, and 32-bit (linear HDR) editing: 16 and 32-bit PSD/PSB files open, edit, and save at their depth, Image > Mode converts between depths (leaving 32 bits offers HDR Toning), and New Document starts at 8 bits with a Bit Depth choice. 16-bit PNG and TIFF open and export at 16 bits; 32-bit documents export float TIFF. The browser build stops at 16 bits and opens 32-bit files converted to 16
- CMYK, Grayscale, Lab, Bitmap, Indexed, Duotone, and Multichannel Photoshop documents convert to RGB on open
- Non-destructive adjustment layers (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold, Exposure) with live preview, editable settings, native Photoshop PSD data, and .acv Curves preset import and export
- Smart Objects: place or convert layers to embedded or linked smart objects, edit or replace their contents, transform them non-destructively, and build editable native Smart Filter stacks (13 filter types) with paintable shared masks and per-filter blending
- Photoshop-compatible layer style, pattern, and gradient preset libraries, including .asl, .pat, and .grd import/export, 39 built-in styles, and 20 bundled CC0 photo textures
- Continuous long shadows for type and shapes. This Patchy-specific layer-style setting falls back to a normal drop shadow in Photoshop; see the [example](screenshots.md).
- Warp Transform tool and Warp Text with all 15 Photoshop warp styles and live preview

## Painting, selections, and retouching

- Common raster editing tools, including Brush with Flow and timed Airbrush buildup, Healing Brush, Spot Healing, Patch, Clone Stamp, Remove Object, Dodge, Burn, Sponge, Blur, Sharpen, Smudge, Eraser, selections, transforms, gradients, and shapes
- Brush tip libraries with Photoshop .abr import, custom tips from selections, spacing, angle, roundness, texture, dual-brush effects, wet edges, and dynamics for size, opacity, flow, scatter, and color. Supported dynamics can respond to pen pressure, tilt, rotation, or stroke direction
- Mixer Brush with Wet, Load, Mix, Flow, and Sample All Layers controls, plus stroke smoothing with pulled-string and catch-up options
- Rectangular and elliptical marquees, lassos including Magnetic Lasso, Magic Wand, Quick Select, and paintable Quick Mask selections, with feathering and selection adjustments
- Move tool layer selection: drag a rectangle to select overlapping layers, Shift-click to toggle individual layers, or right-click to choose among the layers under the pointer, with a selected-layer count in the status bar
- Guides, grids, snapping, and layer alignment/distribution, plus interactive cropping with aspect-ratio presets and straightening
- Filter Gallery with 32 effects, live full-resolution preview, ordered effect stacks, favorites, and reusable Saved Looks, plus a manual Liquify workspace with warp, twirl, pucker, bloat, and freeze brushes
- Photoshop-compatible document resolution, physical measurement units, rulers, image sizing, and printing
- Pen/stylus pressure and size dynamics, GUI scaling, scanner import (Windows and macOS), camera import (Windows), and command line options

## Vectors and typography

- Vector tools: Pen paths, editable shape layers (Rectangle, Ellipse, Line, Polygon, Custom Shape) with solid, gradient, or pattern fills and strokes, vector masks, path selection and anchor editing, and a Paths panel with fill, stroke, and make-selection commands, all round-tripping through PSD files that open correctly in Photoshop
- Dynamic Vector Preview keeps native shapes and vector masks sharp when zoomed in, alongside pixel layers, masks, adjustments, and effects. Merge Layers can preserve editable vectors and keep bitmap runs separate, with options for merging within groups or making a merged copy
- Trace Image to Shapes: converts a pixel layer (logo, scan, photo) into a group of editable shape layers, one per color, with Illustrator-style presets, color, grayscale, and black-and-white modes, abutting or overlapping shapes, noise removal, and a live preview; export the result as SVG
- Point text, paragraph (box) text, and area text that flows inside a closed shape or path, saved as native Photoshop text frames
- Rich text with per-run color, font, size, and style, plus a searchable font picker and Character controls for leading, tracking, and horizontal or vertical glyph scaling, editable on the selected text layer without entering text-editing mode
- Paragraph alignment and justification, first-line and left/right indents, spacing before and after paragraphs, vertical text, and left-to-right or right-to-left paragraph direction

## Pixel art and game assets

- Palettized (indexed color) editing mode for pixel art: paint constrained to a palette, quantize with optional dithering, built-in retro palettes (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), and exact indexed PNG-8 and 2/4/8-bit BMP export. Layers, layer styles, and effects all keep working (Photoshop's indexed mode flattens and disables them)
- Named palette colors appear in the Palette panel, color picker, Info panel, and eyedropper readout. Rename swatches, preserve names through GPL, PSD, and indexed PNG round trips, and manage palettes through scripts
- Pixel-art and game-dev extras: seamless texture authoring (live tile preview window, in-canvas tiling mode, seam shifting), sprite sheet export/import, image sequence export/import (numbered files become layers and back), animated GIF and animated WebP import/export (frames become layers with their timings in the layer names, visible layers save back as a looping animation, and the layers panel's film button previews the animation in-app), and an Export Flat Image dialog with nearest-neighbor scaling (2x-8x), smooth resize, transparent-edge trimming, and background fill

## Files, photographs, and multiple documents

- Multiple document interface: tabbed documents that can float in their own windows, Photoshop-style Tile and Cascade arrangement, a Window menu that lists every open document, and layers that drag or duplicate between documents. Open a whole folder of images as tabs (or drop the folder on the window), and export every open document as a numbered image set to a folder
- Import multiple files as layers, preserving a layered file's contents in a group, through File > Import, a drop on the Layers panel, or pasted files
- Scan and Divide Photos separates prints into individual images with editable crop regions, straightening, and perspective correction. Divide an existing scan or photo too; scanner acquisition and the scanner-to-printer Photocopy command are available on Windows and macOS
- Automatic document recovery periodically saves separate recovery copies of modified documents and reopens them after a crash; the interval is configurable
- Reads and writes a wide range of formats: PSD/PSB, [PDF](#pdf-documents), PNG, JPEG, TIFF, WebP, BMP, TGA, GIF, PCX, Amiga IFF/LBM, Windows icons and cursors (ICO/CUR), Aseprite files, JPEG XR (.jxr, on Windows), Proton SDK textures (.rttex), DDS textures (.dds: reads BC1 to BC7, cubemaps, arrays and mipmaps; writes uncompressed, DXT1, DXT5, BC4, BC5, or BC7 with optional mipmaps), and SVG (opens as editable shape layers, exports with vectors preserved)
- Imports Affinity documents as layered files: the current .af format, Affinity 2 .afphoto/.afdesign/.afpub, and most Affinity 1.x-era files, bringing across rasters, groups, masks, clipping, blend modes, editable text layers, vector shapes, adjustment layers, layer effects, and placed images (which become embedded Smart Objects)
- Opens camera raw files (CR2/CR3/NEF/ARW/RAF/DNG and more) through a 16-bit develop dialog with a Natural rendering profile, ISO-based noise reduction, and per-photo settings saved beside the original, and HEIC/HEIF photos through platform codecs
- Opens HDR screenshots saved as JPEG XR (.jxr), the format NVIDIA's in-game capture uses, tone mapping the high dynamic range down to 8-bit so highlights keep their detail instead of clipping to white

## PDF documents

- Import selected PDF pages on desktop as editable text, vector shapes, and images, or as flattened images at a chosen resolution. Pages can open as separate documents; flattened pages can also become layers in one document
- Export a single document or assemble a multi-page PDF from open documents or top-level layer groups, with page ordering and editable or flattened output
- Editable export preserves supported text and vectors. Effects and unsupported combinations are rasterized with notices; it does not preserve every PSD layer property. Imported PDFs save as a new copy so the original is not overwritten by a normal Save
- Lossless and JPEG quality options, with original compressed image data retained for supported, unchanged scanned pages to avoid re-encoding. PDF export also works in the browser; PDF import requires the desktop app. See [PDF support and limitations](pdf.md)

## Plug-ins, scripting, and AI control

- Classic Photoshop filter plug-ins (.8bf, 32-bit and 64-bit plug-in builds) on Windows, on 8 and 16-bit documents: see [Photoshop plug-ins](../README.md#photoshop-plug-ins-8bf-windows-only)
- JavaScript scripting: a built-in Script Manager (File > Scripts) with a folder tree over the bundled and user scripts, a code editor with live run status, per-script hotkeys (an `@hotkey` default or your own binding in Preferences), a documented API covering documents, layers, text, selections, pixels, filters, form dialogs, file pickers, and batch processing, bundled examples ranging from CSV data merge, contact sheets, icon export, and versioned saves to glitch/duotone effects and playable Breakout and Pong (scripts can call other scripts), safe editing of bundled scripts (your saved copy overrides the original and can be reverted), and a --run-script command line flag with script arguments so external tools and AI agents can drive Patchy (add --headless to run with no display, on a server or in CI). See the [scripting guide](../scripts/bundled/scripting-guide.md) (also under Help inside the app)
- Local AI control through the bundled MCP connector: native pressure-aware brush strokes, reusable brush presets, editable vector shapes and paths, palette controls, image previews, and persistent document sessions. Help > Set up AI Control provides setup instructions and example prompts; see the [AI control guide](ai-control.md)

## Platforms, privacy, and interface

- Cross-platform: Windows is the lead platform, with native macOS (Apple Silicon) and Linux (Flatpak) builds
- Built with C++ and Qt for a native desktop experience. The default remains CPU-authoritative; desktop builds can opt into the experimental Qt RHI and Dawn/WebGPU tiers, with unsupported features falling back atomically to CPU
- Privacy: YES! Absolutely no telemetry, no tracking, no data collection (if update checks are enabled, it contacts GitHub only to check for a newer version). Settings live in a plain local file, and the installer doesn't screw with your file extension preferences
- Localized in twelve languages: English, German, Spanish, French, Italian, Brazilian Portuguese, Russian, Polish, Japanese, Korean, and Chinese (Simplified and Traditional); the language follows your system or can be changed in File->Preferences
- UI themes: Dark, Light, seven bundled themes (Darkest, Medium Gray, Solarized Dark, Nord, Dracula, Gruvbox Dark, High Contrast), or your own. A theme is a small JSON file (`.patchytheme`) that names a base scheme and overrides any of the interface colors, icon tints included; File->Preferences imports it and keeps it in your app-data themes folder (Open Themes Folder shows where). Start from [themes/example-high-contrast.patchytheme](../themes/example-high-contrast.patchytheme) or export the current look, which writes every color so you can see the role names; colors you leave out keep the base scheme's value, and Reload Themes applies an edit without restarting

## Current Status

Patchy is not Photoshop-compatible across the full PSD surface yet, but a round-trip from/to Photoshop mostly works with RGB/RGBA documents (8, 16, or 32 bits per channel) that use basic pixel layers, text objects, groups, masks, blend modes, layer styles, and the currently supported adjustment layers.

Important Photoshop features that are not supported yet, or are only partially supported:

- Editable Smart Filters cover 13 filter types with paintable shared masks and per-filter opacity and blend modes; unsupported imported filter types (including the Blur Gallery and Liquify smart filters) remain preview-locked and byte-preserved
- Full Photoshop adjustment-layer compatibility beyond Patchy's current adjustment support
- CMYK/Lab editing and export, editable spot separations and RGB component channels, multi-channel overlays, OpenEXR/Radiance HDR files, and full color-management parity (Patchy converts CMYK, Lab, Grayscale, and the other non-RGB modes to RGB on open, but does not edit or save in those color modes)
- 16 and 32-bit gaps: filters without a deep version run on an 8-bit copy at 16 bits (unchanged pixels keep full precision) and are disabled at 32 bits, as are Liquify and the Filter Gallery; Smart Object contents and their Smart Filters render at 8 bits; Spot Healing, Remove Object, and Patch heal at 8-bit precision
- Layer comps, timeline/video editing, generative tools (animated GIF and WebP import, preview, and export are supported)
- Photoshop's own automation surfaces: Actions (.atn), UXP/JSX panels, and scripts written for Photoshop (Patchy has its own JavaScript scripting and batch processing instead, see above)
- High-fidelity PSD/PSB edge cases and byte-perfect preservation of every Photoshop-only metadata block
- Patchy is slower than Photoshop, especially on large documents. CPU compositing, PSD output, and compatibility tests remain authoritative for portability and deterministic output. Optional Qt RHI and Dawn/WebGPU tiers can accelerate supported presentation and document-compositing paths; unsupported features and export fall back atomically to CPU. See [GPU canvas presentation](gpu-canvas.md). Canvas compositing and image flattening are also optimized for multicore execution.

### Affinity import

Patchy opens Affinity documents read-only: the current .af format ("Affinity by Canva"), the Affinity 2 formats (.afphoto, .afdesign, .afpub), and most Affinity 1.x-era files. Raster layers (including 16-bit, float, CMYK, and Lab documents), groups, masks (raster and vector), clipping, blend modes, opacity, editable text with per-run styles, vector curves, parametric shapes (rectangles, ellipses, polygons, stars, triangles, diamonds, trapezoids, pies, segments, crescents, hearts, tears, arrows, double and square stars, cogs, clouds), compound-shape booleans, Designer symbols, artboards, layer effects, supported adjustment layers, and placed images (which become embedded Smart Objects) all import, verified against Affinity's own renders and a wild-file corpus that includes real Affinity 2 documents from all three apps. Vector fills and strokes keep their width, alignment, dash pattern, and miter limit, crop-to-shape containers come in as masked groups, and the Erase blend mode imports as an isolated group with an inverse-alpha mask, which is how PSD stores that construction natively. If a file can't be imported as layers, Patchy falls back to its embedded preview instead of failing the open, and an import notice explains what was skipped.

Affinity features that are not supported yet, or are only partially supported:

- Saving to Affinity formats (import only; save your edits as PSD)
- A few parametric shape kinds (callouts, spirals, QR codes, circle-rounded stars, and exotic arrow ends) import as named placeholders
- Adjustment layers beyond the eight kinds the importer maps (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold), and live filters, import as named empty placeholders; Brightness/Contrast and Color Balance import approximately
- Affinity-only blend modes render through their closest Photoshop-compatible equivalent with a notice (Average matches exactly at half opacity; Negation, Reflect, Glow, and Pigment approximate; Contrast Negate falls back to Normal)
- Bevel/Emboss and glow effects are approximated; Gaussian blur layer effects bake into the layer pixels (they render correctly but are no longer live)
- Rotated or sheared frame text renders without its rotation (artistic text rotates correctly)
- Multi-page documents open the first page only
- Affinity 1.x-era files: embedded-document placement can land slightly off
