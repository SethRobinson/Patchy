# Features and compatibility

[Back to Patchy](../README.md) | [Screenshot gallery](screenshots.md)

Related documents:

- [Painting, selections, and retouching](features-painting.md): painting and retouching tools, brushes, transforms, selections, filters, guides and measurement, and pen input
- [Files and formats](features-files.md): multiple documents, templates, import, recovery, Export for Web, the formats Patchy reads and writes, camera raw and HDR photos, PDF documents, and what layered files from other editors keep
- [Platforms, privacy, and interface](features-interface.md): supported platforms, privacy and file metadata, panels, Search Commands, screen modes, customization, menus, languages and themes

## Photoshop documents and non-destructive editing

- Open and save layered PSD and PSB files with groups, masks, clipping masks, saved alpha and spot channels, text objects, Fill Opacity, the full Photoshop blend mode set, layer styles and more
- 8, 16, and 32-bit (linear HDR) editing: 16/32-bit PSD/PSB edit and save at depth, Image > Mode converts (leaving 32 bits offers HDR Toning). 16-bit PNG and TIFF open and export at 16 bits; 32-bit documents export float TIFF. At 32 bits the color windows and Color panel show linear values and Intensity (stops, swatches); paint, gradients and the Eyedropper keep values over 1.0. The browser build stops at 16 bits, converting 32-bit files to 16
- CMYK, Grayscale, Lab, Bitmap, Indexed, Duotone and Multichannel Photoshop documents convert to RGB on open (unedited CMYK PSDs save as CMYK; untagged Grayscale is Dot Gain 20%)
- An opaque Background layer locks position and transparency like Photoshop's, so the Eraser paints the background color, and Image > Flatten Image merges the visible layers into it
- Transparent flat images (PNG, WebP...) open as a transparent layer; a Preferences option opens their alpha as a layer mask
- Non-destructive adjustment layers (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold, Exposure, Channel Mixer, Photo Filter, Gradient Map, Black & White, Color Lookup (.cube, .3dl or embedded), Selective Color, and Vibrance) with live preview, editable settings, native PSD data, presets with .acv, .ahu, .alv and .blw import/export; each also applies from Image > Adjustments
- One-step Image > Adjustments with no dialog: Invert (Ctrl+I), Auto Tone, Auto Contrast, Auto Color and Auto All
- Smart Objects: place or convert layers to embedded or linked smart objects, edit or replace their contents (SVG and the like in your default app), transform them non-destructively, and build native Smart Filter stacks (blur, sharpen, noise, distort, stylize, pixelate, render, adjustments) with paintable shared masks and per-filter blending
- Linked files and Krita file layers on a network server open only from servers you allow, so a downloaded document cannot make Patchy connect to an unknown server
- Layer style, pattern and gradient preset libraries with .asl, .pat and .grd import/export, 39 built-in styles (grid or list; Make Default per effect; round angle dials), 20 CC0 photo textures; Edit > Presets opens all managers, Shapes Manager (.csh, grids, resizable picker, 878 game-icons.net shapes)
- Continuous long shadows for type and shapes (a normal drop shadow in Photoshop; [example](screenshots.md))
- Warp Transform tool and Warp Text with all 15 Photoshop warp styles and live preview

## Vectors and typography

- Vector tools: Pen paths, shape layers (Rectangle, Ellipse, Line, Polygon, Custom Shape) with solid, gradient or pattern paint, editable dash and stroke presets with offsets, path-edited vector masks, point edits (add by click, break, join, weld or merge by drag, delete segment), Merge and Split Shapes, per-shape cut, copy, paste, color, align, new layer, editable radius, sides, star inset, line weight and arrowheads, a Paths panel (fill, stroke, selection), switchable fill layers (badge or double-click opens settings), in PSDs Photoshop opens
- Dynamic Vector Preview keeps shapes and vector masks sharp at any zoom. Merge Layers keeps vectors and bitmap runs apart, merges within groups or makes a merged copy (Ctrl+Alt+E: selected layers only). Merge Visible bakes a layer's mask or effects
- Trace Image to Shapes: turns a pixel layer into editable shape layers, one per color, with presets, color, gray and black-and-white modes, abutting or overlapping shapes, noise removal, live preview
- Point text, paragraph (box) text and area text that flows inside a closed shape or path, saved as native Photoshop text frames
- Rich text with per-run color, font, size and style, a searchable font picker (recent fonts, style/language filters), stand-ins for missing fonts, and Character controls (leading, tracking, glyph scale, caps, super/subscript, underline, strikethrough, OpenType features) that edit selected layers
- Paragraph alignment and justification, first-line and left/right indents, spacing before and after paragraphs, vertical text, and left-to-right or right-to-left paragraphs
- Type > Convert to Shape and Create Work Path turn any text, warped too, into vector outlines

## Pixel art and game assets

- Palettized (indexed color) editing mode for pixel art: paint constrained to a palette, quantize with optional dithering, built-in retro palettes (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), and exact indexed PNG-8 and 2/4/8-bit BMP export. Layers, styles and effects keep working, unlike Photoshop's indexed mode
- Named palette colors appear in the Palette panel, color picker, Info panel and eyedropper readout. Rename swatches, preserve names through GPL, PSD, and indexed PNG round trips, and manage palettes through scripts
- Pixel-art and game-dev extras: seamless texture authoring (live tile preview, in-canvas tiling, seam shifting), sprite sheet and numbered image sequence export/import, animated GIF, WebP and PNG import/export (frames become layers with timings in their names, visible layers save as a looping animation, previewed by the film button or Image > Animation Preview, with delay presets), and flat and web exports with pixel scaling, resizing, edge trimming and background fill

## Color and print

- CMYK ink percentages in the color picker (with a gamut warning and the closest printable color) and the Info panel, converted through an ICC profile: a bundled ISO Coated v2 profile by default, or an installed or chosen one (Preferences > Application > CMYK). CMYK swatches in `.aco` and `.ase` files import through the same profile
- View > Proof Colors shows the canvas as that profile would print it (and lists the Cyan, Magenta, Yellow and Black plates in the Channels panel), and View > Gamut Warning paints colors it cannot reproduce flat gray. Both are views only: pixels, saves and exports are unchanged
- Image > Mode > CMYK Proof keeps a document in that proof view and makes JPEG, TIFF and flattened PDF saves CMYK by default. CMYK JPEG and TIFF files open through their embedded profile into CMYK Proof. Documents are still edited in RGB
- File > Export writes JPEG, TIFF and flattened PDF in CMYK through that profile, with the profile embedded, for print shops, optionally with black text and grays on the black plate alone
- Channels panel: New Layer from Channel turns any channel or ink plate into a grayscale layer, and New Layer from Channel Color turns components and plates into colored layers that stack back into the image
- Channels panel eyes show or hide Red, Green and Blue (or the CMYK proof's plates) on the canvas, one channel in grayscale or several in color, pixels untouched; Ctrl+2 shows the composite and Ctrl+3 to Ctrl+5 one channel alone (to Ctrl+6 for CMYK Proof plates), keys shown on each row. Row thumbnails show each channel

## Artboards

- Photoshop-style artboards: several canvases of different sizes in one document, each with a white, black, transparent or custom background, and layers that show only inside their artboard. Layers outside every artboard still render
- The Artboard tool (in the Move tool's flyout) draws, moves, copies (Alt-drag), resizes, and nudges artboards, and its + buttons add one beside the selected artboard. Size presets, Auto-Nest Layers, the Properties panel, and Layer > New commands (Artboard, Artboard from Group, Artboard from Layers, Convert Artboard to Group) cover the rest, and New Document can start with one
- Artboards open and save in Photoshop's own PSD format, export to PDF one page per artboard, export as separate files (File > Export > Artboards to Files), and print one sheet per artboard, optionally on paper the artboard's size. Affinity artboards import as artboards, and scripts can create and edit them

## Plug-ins, scripting, and AI control

- Classic Photoshop filter plug-ins (.8bf, 32 and 64-bit) on Windows, 8 and 16-bit (Filter > Last Filter, Ctrl+Alt+F, repeats the last one): see [Photoshop plug-ins](../README.md#photoshop-plug-ins-8bf-windows-only)
- Over a hundred GEGL filters and About GEGL (Filter > GEGL Filters): [GEGL filters](gegl-filters.md)
- JavaScript scripting: a Script Manager (File > Scripts) with a tree of bundled and user scripts, an editor with live run status, per-script hotkeys (`@hotkey` or your own), a documented API for documents, layers, text, selections, pixels, filters, form dialogs, file pickers, and batch processing, editable bundled examples from data merge to glitch effects and games, and --run-script with arguments for tools and AI agents (--headless for servers and CI). See the [scripting guide](../scripts/bundled/scripting-guide.md) (Help menu)
- Local AI control through the bundled MCP connector: pressure-aware brush strokes, brush presets, editable vector shapes and paths, palette controls, image previews, and persistent document sessions. Help > Set up AI Control has setup steps and prompts ([guide](ai-control.md)). Plugins > Companions starts apps that use it ([companions.md](companions.md))

## Current Status

Patchy does not cover the full PSD surface yet, but a Photoshop round-trip mostly works with RGB/RGBA documents (8, 16, or 32 bits per channel) that use basic pixel layers, text objects, groups, masks, blend modes, layer styles, and supported adjustment layers.

Photoshop features not supported yet, or only partly:

- Editable Smart Filters cover many classic filters and Image > Adjustments commands with paintable shared masks and per-filter opacity and blend modes; unsupported imported filter types (including the Blur Gallery and Liquify smart filters) remain preview-locked and byte-preserved
- Photoshop's newer **Color and Vibrance** adjustment opens as a preserved placeholder without applying its effect (legacy **Vibrance** is supported)
- True CMYK and Lab editing, CMYK saves of edited PSD files, editable spot separations and RGB component channels, multi-channel overlays, and full color-management parity (Patchy edits in RGB and converts the other modes on open; for print output see [Color and print](#color-and-print))
- OpenEXR imports single-part scanline or tiled RGB/RGBA or luminance images with supported compression; multipart, deep, and some compression types are unsupported. AVIF is desktop-only and exports still images, although sequences can be imported
- 16 and 32-bit gaps: filters without a deep version run on an 8-bit copy at 16 bits (unchanged pixels keep full precision) and are disabled at 32 bits, as are Liquify and the Filter Gallery; Smart Object contents and their Smart Filters render at 8 bits; Spot Healing, Remove Object, and Patch heal at 8-bit precision
- Layer comps, timeline/video editing, generative tools (animated GIF, WebP and PNG are supported)
- Photoshop's own automation surfaces: Actions (.atn), UXP/JSX panels, and scripts written for Photoshop (Patchy has its own JavaScript scripting and batch processing)
- High-fidelity PSD/PSB edge cases and byte-perfect preservation of every Photoshop-only metadata block
- Patchy is slower than Photoshop, especially on large documents and it doesn't support any GPU acceleration. (Like, layer styles being done in pixel shaders, etc) However, being CPU only helps with porting, consistent output, and stability so kind of a trade-off that makes sense, for now. That said, certain operations have been optimized for multicore - canvas compositing and image flattening are multithreaded, splitting large images (4 Mpx+) into strips rendered on all CPU cores.
