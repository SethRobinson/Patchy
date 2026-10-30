# Features and compatibility

[Back to Patchy](../README.md) | [Screenshot gallery](screenshots.md)

## Photoshop documents and non-destructive editing

- Open and save layered PSD and PSB files with groups, masks, clipping masks, saved alpha and spot channels, text objects, Fill Opacity, the full Photoshop blend mode set, layer styles and more
- 8, 16, and 32-bit (linear HDR) editing: 16 and 32-bit PSD/PSB files edit and save at their depth, Image > Mode converts between depths (leaving 32 bits offers HDR Toning), and New Document starts at 8 bits with a Bit Depth choice. 16-bit PNG and TIFF open and export at 16 bits; 32-bit documents export float TIFF. The browser build stops at 16 bits and opens 32-bit files converted to 16
- CMYK, Grayscale, Lab, Bitmap, Indexed, Duotone, and Multichannel Photoshop documents convert to RGB on open (unedited CMYK PSDs save as CMYK; untagged Grayscale is Dot Gain 20%)
- An opaque Background layer locks position and transparency like Photoshop's, so the Eraser paints the background color, and Image > Flatten Image merges the visible layers into it
- Non-destructive adjustment layers (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold, Exposure, Channel Mixer, Photo Filter, Gradient Map, Black & White, Color Lookup from embedded tables or a .cube or .3dl file, Selective Color, and Vibrance) with live preview, editable settings, native Photoshop PSD data, and .acv Curves preset import and export
- One-step Image > Adjustments commands with no dialog: Invert (Ctrl+I), Auto Tone, Auto Contrast, Auto Color, and Auto All, on the active layer or the selection
- Smart Objects: place or convert layers to embedded or linked smart objects, edit or replace their contents, transform them non-destructively, and build editable native Smart Filter stacks (blur, sharpen, noise, distort, stylize, pixelate and render filters plus Image > Adjustments commands) with paintable shared masks and per-filter blending
- Photoshop-compatible layer style, pattern, and gradient preset libraries with .asl, .pat, and .grd import/export, 39 built-in styles, and 20 bundled CC0 photo textures; Edit > Presets (or Preferences) opens all managers, Shapes Manager (.csh, grids, resizable picker)
- Continuous long shadows for type and shapes. This Patchy-specific layer-style setting falls back to a normal drop shadow in Photoshop; see the [example](screenshots.md).
- Warp Transform tool and Warp Text with all 15 Photoshop warp styles and live preview

## Painting, selections, and retouching

- Raster editing tools: Brush with Flow and timed Airbrush buildup, Healing Brush, Spot Healing, Patch, Red Eye, Clone Stamp, Remove Object, Dodge, Burn, Sponge, Blur, Sharpen, Smudge, Eraser, selections, transforms, gradients, and shapes
- Free Transform, Skew, Distort and Perspective work on the selected pixels when a selection is active: the box fits them and the selection follows on Enter
- Ctrl-drag a corner or side of the Move tool's transform box to distort it; Ctrl mid-drag toggles distort and resize
- Retouch sampling from Current Layer, Current & Below, or All Layers, with Ignore Adjustment Layers. Healing Brush blends the stroke on release and offers Replace and blend modes; Red Eye works with a click or a box over the eye
- Clone source settings on the Clone Stamp gear: four saved sources, offset, scale, rotation, flips, and a source overlay
- Brush tips (.abr import from the last folder, from selections, a name filter) with texture, dual brush, wet edges and pen-driven size, opacity, flow, scatter and color dynamics; the tip and dynamics windows can stay open; the Brush gear previews the dab or hides the outline
- Mixer Brush with Wet, Load, Mix, Flow and Sample All Layers controls; stroke smoothing with pulled-string and catch-up options
- Painting tools show their mouse gestures in the status bar; Ctrl+click (Command+click on macOS) selects the layer under the pointer and Ctrl+drag moves the selected layer without switching tools; Alt+right-drag sets brush size and softness (Quick Select: size) with a readout on the canvas; Shift+[/] step hardness; holding Shift previews the line a Shift+click paints
- Rectangular and elliptical marquees, lassos including Magnetic Lasso, Magic Wand (Sample can average a square around the click), Quick Select, and paintable Quick Mask selections, with feathering and selection adjustments; a drag inside a selection moves its outline
- Object Selection tool: drag a loose box around an object, or click it, to select just that object, even overlapping others. Shift adds, Alt subtracts; a click outside the canvas deselects. It runs locally on the CPU. On desktop builds a High Quality model (a one-time 234 MB download) is much more accurate. Right-click inside a selection and choose Select Object to tighten it to its object
- Move tool layer selection: drag a box over layers, Shift-click to toggle one, or right-click to choose among the layers under the pointer, with a count in the status bar; Select > All Layers (Ctrl+Alt+A)
- Guides, grids, snapping, and layer alignment/distribution (New Guide Layout builds column and row guides with gutters and margins), interactive cropping with aspect-ratio presets and straightening, Trim (plain or transparent edges, with a tolerance), and a Ruler tool that measures distances and angles and straightens a layer or the whole image
- View > Extras (Ctrl+H) toggles selection edges, paths, guides, grids, alignment lines and artboard names at once; Show Extras Options picks which
- G'MIC Filter Library on desktop: nearly 400 standard G'MIC filters, with controls built from each filter's own description, live preview on the layer, multi-layer input and output, draggable point markers on the canvas, and script access
- Filter > Last Filter (Ctrl+Alt+F) repeats the last filter with its settings on the current layer and selection, for built-in filters, Filter Gallery looks, G'MIC filters, and plug-ins
- Filter Gallery with live full-resolution preview, stacks, favorites and Saved Looks; Stylize and Render add angled Wind, Tiles, Extrude, Solarize, Find Edges, Trace Contour, Diffuse, Fibers, Difference Clouds, Lens Flare and Flame (14 presets, path mode); Liquify with warp, twirl, pucker, bloat and freeze brushes
- Perspective Planes: put layers, pastes, and paint on flat surfaces in perspective
- Photoshop-compatible document resolution, physical measurement units, rulers, image sizing, and printing
- Pen/stylus pressure and size dynamics, GUI scaling, scanner import (Windows and macOS), camera import (Windows), and command line options

## Vectors and typography

- Vector tools: Pen paths, editable shape layers (Rectangle, Ellipse, Line, Polygon, Custom Shape) with solid, gradient, or pattern fills and strokes, vector masks edited like paths (Density, Feather, a link chain), path selection and anchor editing, and a Paths panel with fill, stroke, and make-selection commands, all round-tripping through PSDs that open in Photoshop
- Dynamic Vector Preview keeps native shapes and vector masks sharp when zoomed in, alongside pixel layers, masks, adjustments, and effects. Merge Layers can preserve editable vectors and keep bitmap runs separate, with options for merging within groups or making a merged copy (Ctrl+Alt+E: of the selected layers only)
- Trace Image to Shapes: converts a pixel layer (logo, scan, photo) into a group of editable shape layers, one per color, with Illustrator-style presets, color, grayscale, and black-and-white modes, abutting or overlapping shapes, noise removal, and a live preview; export the result as SVG
- Point text, paragraph (box) text, and area text that flows inside a closed shape or path, saved as native Photoshop text frames
- Rich text with per-run color, font, size, and style, a searchable font picker, suggested stand-ins for missing fonts, and Character controls (leading, tracking, glyph scale, caps, small caps, super/subscript, underline, strikethrough, ordinals, fractions) that edit selected text layers outside text editing
- Paragraph alignment and justification, first-line and left/right indents, spacing before and after paragraphs, vertical text, and left-to-right or right-to-left paragraph direction

## Pixel art and game assets

- Palettized (indexed color) editing mode for pixel art: paint constrained to a palette, quantize with optional dithering, built-in retro palettes (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), and exact indexed PNG-8 and 2/4/8-bit BMP export. Layers, layer styles, and effects all keep working (Photoshop's indexed mode flattens and disables them)
- Named palette colors appear in the Palette panel, color picker, Info panel, and eyedropper readout. Rename swatches, preserve names through GPL, PSD, and indexed PNG round trips, and manage palettes through scripts
- Pixel-art and game-dev extras: seamless texture authoring (live tile preview window, in-canvas tiling mode, seam shifting), sprite sheet export/import, image sequence export/import (numbered files become layers and back), animated GIF, WebP and PNG import/export (frames become layers with timings in their names, visible layers save as a looping animation, previewed by the film button or Image > Animation Preview, with delay presets), and flat and web exports with pixel scaling, resizing, edge trimming and background fill

## Color and print

- CMYK ink percentages in the color picker (with a gamut warning and the closest printable color) and the Info panel, converted through an ICC profile: a bundled ISO Coated v2 profile by default, or an installed or chosen one (Preferences > Application > CMYK). CMYK swatches in `.aco` and `.ase` files import through the same profile
- View > Proof Colors shows the canvas as that profile would print it (and lists the Cyan, Magenta, Yellow and Black plates in the Channels panel), and View > Gamut Warning paints colors it cannot reproduce flat gray. Both are views only: pixels, saves and exports are unchanged
- Image > Mode > CMYK Proof keeps a document in that proof view and makes JPEG, TIFF, and flattened PDF saves CMYK by default. CMYK JPEG and TIFF files open through their embedded profile into CMYK Proof. Documents are still edited in RGB
- File > Export writes JPEG, TIFF and flattened PDF in CMYK through that profile, with the profile embedded, for print shops, optionally with black text and grays on the black plate alone
- Channels panel: New Layer from Channel turns any channel or ink plate into a grayscale layer, and New Layer from Channel Color turns components and plates into colored layers that stack back into the image
- Channels panel eyes show or hide Red, Green and Blue (or the CMYK proof's plates) on the canvas, one channel in grayscale or several in color, pixels untouched; Ctrl+2 shows the composite and Ctrl+3 to Ctrl+5 one channel alone (to Ctrl+6 for CMYK Proof plates), keys shown on each row. Each row's thumbnail shows that channel of the image

## Files, photographs, and multiple documents

- Multiple document interface: tabbed documents that can float in their own windows, Tile and Cascade arrangement, a Window menu listing every open document, and layers that drag or duplicate between documents. Open a folder of images as tabs (or drop the folder on the window), and export every open document as a numbered image set to a folder
- New Document templates in seven groups (screens, phones and tablets, social, web and ads, print paper, photo prints, icons and game art) showing paper sizes in mm or inches, a search across groups, a document name, color mode and bit depth, an optional empty layer above the Background, and saved presets
- Import multiple files as layers, preserving a layered file's contents in a group, through File > Import, a drop on the Layers panel, or pasted files. Paste a path copied as text to open it (Paste in Place: as a layer)
- Scan and Divide Photos splits prints into images with editable crop regions, straightening, and perspective correction. Divide an existing scan or photo too; scanner acquisition and the scanner-to-printer Photocopy command are available on Windows and macOS
- Automatic document recovery saves copies of modified documents at a set interval and reopens them after a crash
- File > Export for Web: JPEG, WebP, AVIF, JPEG XL, PNG, APNG, GIF, live sizes, playing previews, play count, fill color
- Reads and writes many formats: PSD/PSB, [PDF](#pdf-documents), PNG, JPEG (12-bit files open at 16 bits), TIFF, WebP, BMP, TGA, GIF, PCX, Amiga IFF/LBM, Windows icons and cursors (ICO/CUR), OpenRaster (.ora), Aseprite files, JPEG XL (.jxl, 16/32-bit stills and animation), JPEG 2000 (.jp2), OpenEXR (.exr), Radiance HDR (.hdr/.rgbe), AVIF (desktop), JPEG XR (.jxr, Windows), Proton SDK textures (.rttex), DDS textures (.dds: reads BC1 to BC7, cubemaps, arrays and mipmaps; writes uncompressed, DXT1, DXT5, BC4, BC5, or BC7 with optional mipmaps), and SVG (opens as editable shapes, exports vectors)
- Import notes (what a file could not keep) flash yellow in the status bar or show in a popup
- Imports Affinity, Paint.NET, Krita and GIMP (.xcf) documents, and reads and writes OpenRaster, layered TIFF, and Aseprite documents. See [layered format interchange](#layered-format-interchange) for what stays editable and what each format cannot preserve
- Opens camera raw files (CR2/CR3/NEF/ARW/RAF/DNG and more) with 16-bit development processing, a Natural rendering profile, ISO-based noise reduction, and per-photo settings saved beside the original. The resulting document is currently 8-bit. HEIC/HEIF photos open through platform codecs, with supported high-bit-depth and HDR input depending on the decoder
- The Develop Raw dialog shows a live RGB and luminance histogram of the preview: pointing at it names the tone setting that part adjusts (Shadows, Exposure, Highlights) and dragging there changes it. Shift-click a slider or its label for an automatic value and Alt-click to reset it. The preview zooms from a zoom menu, 100% and Fit buttons, and an optional Scrubby Zoom drag
- Filter > Camera Raw Filter (Shift+Ctrl+A) brings the Develop Raw white balance, exposure, tone and color controls to any RGB pixel layer at 8, 16 or 32 bits, with the same histogram, slider clicks and zoomable preview. Temperature and tint are relative to the layer's colors, an Auto button neutralizes the average color, and an active selection limits the change. Applies as one undo step; scripts call `layer.applyCameraRawFilter`
- Opens floating-point JPEG XR HDR screenshots as 32-bit linear documents on Windows, keeping highlight values. With deep editing disabled they tone map to 8-bit; JPEG XR saves are always 8-bit. OpenEXR and Radiance HDR also open as 32-bit documents and export flattened HDR images; see [HDR format support](hdr-formats.md)

## PDF documents

- Import selected PDF pages on desktop as editable text, vector shapes, and images, or as flattened images at a chosen resolution (72 to 600 ppi presets or any value). Pages can open as separate documents, as one document with a folder, layer or artboard per page. Choose RGB, Grayscale, CMYK Proof, or Indexed Color, 8, 16, or 32 bits per channel, a white or transparent page background, and reverse page order
- Export a single document or assemble a multi-page PDF from open documents, top-level layer groups, or artboards, with page ordering and editable or flattened output. A document with artboards saves as one PDF page per artboard, each its own size
- Editable export preserves supported text and vectors. Effects and unsupported combinations are rasterized with notices; it does not preserve every PSD layer property. Imported PDFs save as a new copy so the original is not overwritten by a normal Save
- Lossless and JPEG quality options, with original compressed image data retained for supported, unchanged scanned pages to avoid re-encoding. PDF export also works in the browser; PDF import requires the desktop app. See [PDF support and limitations](pdf.md)

## Artboards

- Photoshop-style artboards: several canvases of different sizes in one document, each with a white, black, transparent, or custom background, and layers that show only inside their artboard. Layers outside every artboard still render
- The Artboard tool (in the Move tool's flyout) draws, moves, copies (Alt-drag), resizes, and nudges artboards, and its + buttons add one beside the selected artboard. Size presets, Auto-Nest Layers, the Properties panel, and Layer > New commands (Artboard, Artboard from Group, Artboard from Layers, Convert Artboard to Group) cover the rest, and New Document can start with one
- Artboards open and save in Photoshop's own PSD format, export to PDF one page per artboard, export as separate files (File > Export > Artboards to Files), and print one sheet per artboard, optionally on paper the artboard's size. Affinity artboards import as artboards, and scripts can create and edit them

## Plug-ins, scripting, and AI control

- Classic Photoshop filter plug-ins (.8bf, 32-bit and 64-bit plug-in builds) on Windows, on 8 and 16-bit documents (Plugins > Repeat Last Plug-in is Ctrl+F; Last Plug-in Settings has no default key, since Ctrl+Alt+F is Filter > Last Filter): see [Photoshop plug-ins](../README.md#photoshop-plug-ins-8bf-windows-only)
- JavaScript scripting: a Script Manager (File > Scripts) with a folder tree of bundled and user scripts, a code editor with live run status, per-script hotkeys (an `@hotkey` default or your own in Preferences), a documented API covering documents, layers, text, selections, pixels, filters, form dialogs, file pickers, and batch processing, bundled examples from data merge and contact sheets to glitch effects and games, editable bundled scripts (a saved copy overrides the original), and a --run-script flag with script arguments so other tools and AI agents can drive Patchy (--headless for servers and CI). See the [scripting guide](../scripts/bundled/scripting-guide.md) (Help menu)
- Local AI control through the bundled MCP connector: native pressure-aware brush strokes, reusable brush presets, editable vector shapes and paths, palette controls, image previews, and persistent document sessions. Help > Set up AI Control provides setup instructions and example prompts; see the [AI control guide](ai-control.md)

## Platforms, privacy, and interface

- Cross-platform: Windows (x64, and native ARM64 without PDF import) leads, with native macOS (Apple Silicon), Linux (Flatpak and AppImage) and a browser build; see the [platform support table](../README.md#platform-support)
- Native C++ and Qt with CPU rendering. Large documents, high bit depths, and Object Selection need more memory and time
- No telemetry, tracking or image uploads. Optional update checks contact GitHub; one-click updates on Windows and the Linux AppImage and Object Selection's optional High Quality model download files when requested. Settings live in a plain local file; the installer leaves your file associations unchanged
- File > File Info shows and edits a document's metadata (EXIF, XMP, IPTC, thumbnails, AI generation data), flags information you may not want to share and strips it. PSD and JPEG saves record Patchy, the save dates, color mode and document IDs as Photoshop does, with a fresh thumbnail
- Group sidebar panels into tabs (drag out, reorder) and collapse them. The Color panel edits colors in a hue cube or RGB, HSB or CMYK sliders (gear picks); the Layers panel previews blend modes while browsing (Shift+Plus/Minus step them or a painting tool's Mode; Shift+Alt+letter sets one) and has four thumbnail sizes aligned by nesting level. Digits set layer opacity off painting tools; "/" locks transparency. Alt-click (Option-click on macOS) New Layer for its dialog (with neutral fills), Ctrl-click (Cmd-click) to add below; Alt-click Add Layer Mask for a hiding mask, Ctrl-click (Cmd-click) for a vector mask. Drag along eyes to toggle layers; right-click one for this, other or all. Row, thumbnail and badge menus hold layer, mask, style and Smart Object commands; a badge flags custom blending. Ctrl-click (Cmd-click) thumbnails to select (Shift adds, Alt subtracts, both intersect). Delete removes a targeted mask, offering to apply it
- Screen modes (F, View menu, toolbox): full screen, with or without menus and panels. F6, F7, F8 toggle the Color, Layers, Info panels
- Customize Tool Palette: hide tools, color, Quick Mask and screen mode buttons; Layers buttons (Layers gear menu): hide, reorder, presets, size, optional move and merge; two columns, wheel scroll, icon size, no tool groups; groups open on hold, click or hover
- Bring to Front, Send to Back (Shift+Ctrl+], [)
- Localized in twelve languages: English, German, Spanish, French, Italian, Brazilian Portuguese, Russian, Polish, Japanese, Korean, and Chinese (Simplified and Traditional), following the system or Preferences, which also set the interface scale and a separate text size (80% to 125%)
- UI themes: Dark, Light, eight bundled themes (Darkest, Medium Gray, Solarized Dark, Nord, Dracula, Gruvbox Dark, High Contrast, Dink Smallwood), or your own: a `.patchytheme` file of interface colors, or a theme package (folder or .zip) adding SVG, PNG, BMP icons, a tiled backdrop, event sounds and effects (fireworks, confetti, sprites), and sandboxed scripts for a theme menu, cursors and a splash. Preferences > Colors and Themes previews each theme, its startup sound, version, author and description, warns if it needs a newer Patchy, keeps script trust per theme, imports and exports. Start from [themes/example-package](../themes/example-package/theme.json)

## Current Status

Patchy is not Photoshop-compatible across the full PSD surface yet, but a round-trip from/to Photoshop mostly works with RGB/RGBA documents (8, 16, or 32 bits per channel) that use basic pixel layers, text objects, groups, masks, blend modes, layer styles, and the currently supported adjustment layers.

Important Photoshop features that are not supported yet, or are only partially supported:

- Editable Smart Filters cover many classic filters and Image > Adjustments commands with paintable shared masks and per-filter opacity and blend modes; unsupported imported filter types (including the Blur Gallery and Liquify smart filters) remain preview-locked and byte-preserved
- Full Photoshop adjustment-layer compatibility beyond Patchy's current adjustment support: the newer **Color and Vibrance** adjustment opens as a preserved placeholder without applying its effect. Legacy **Vibrance** is supported
- True CMYK and Lab editing, CMYK saves of edited PSD files, editable spot separations and RGB component channels, multi-channel overlays, and full color-management parity (Patchy edits in RGB and converts CMYK, Lab, Grayscale, and the other non-RGB modes to RGB on open; CMYK Proof mode, Proof Colors, and CMYK export of JPEG, TIFF, and flattened PDF cover print output, see [Color and print](#color-and-print))
- OpenEXR imports single-part scanline or tiled RGB/RGBA or luminance images with supported compression; multipart, deep, and some compression types are unsupported. AVIF is desktop-only and exports still images, although sequences can be imported
- 16 and 32-bit gaps: filters without a deep version run on an 8-bit copy at 16 bits (unchanged pixels keep full precision) and are disabled at 32 bits, as are Liquify and the Filter Gallery; Smart Object contents and their Smart Filters render at 8 bits; Spot Healing, Remove Object, and Patch heal at 8-bit precision
- Layer comps, timeline/video editing, generative tools (animated GIF, WebP, and PNG import, preview, and export are supported)
- Photoshop's own automation surfaces: Actions (.atn), UXP/JSX panels, and scripts written for Photoshop (Patchy has its own JavaScript scripting and batch processing instead, see above)
- High-fidelity PSD/PSB edge cases and byte-perfect preservation of every Photoshop-only metadata block
- Patchy is slower than Photoshop, especially on large documents and it doesn't support any GPU acceleration. (Like, layer styles being done in pixel shaders, etc)  However, being CPU only helps with porting, consistent output, and stability so kind of a trade-off that makes sense, for now.  That said, certain operations have been optimized for multicore - canvas compositing and image flattening are multithreaded, splitting large images (4 Mpx+) into strips rendered on all CPU cores.

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
| **GIMP** (.xcf, also .xcf.gz/.bz2/.xz) | Import only | Layers, groups (pass-through included), masks, opacity, visibility, locks, color tags, GIMP's blend modes (its HSV, LCh, luminance, grain and linear-light modes as Patchy modes that a PSD save bakes), every precision at 8, 16 or 32 bits, gray and indexed images (indexed ones can open in Palette mode), editable text, drop shadows as layer styles, Gaussian Blur effects applied, channels, paths, guides, resolution, the color profile, comment, Exif, XMP and IPTC. Other GIMP filters are left out with a notice; GIMP's linear-light blending of soft edges can differ by a few levels. [Import details](xcf.md) |
| **Layered TIFF** (.tif, .tiff) | Open and save | Photoshop-style layer data at 8, 16, and 32 bits, including supported text, masks, and effects. Choose Keep Layers when saving; other TIFF readers may show only the merged image. [TIFF details](tiff.md) |
| **Aseprite** (.ase, .aseprite) | Open and save | The first frame's pixel layers, groups, opacity, and supported blend modes. Saves are a single frame at 8 bits, with procedural content rasterized. [Format details](file-formats.md#per-format-catalogue) |

OpenRaster can exchange layered paintings with applications such as Krita and GIMP
in both directions; Krita and GIMP files open directly. Keep a PSD master when you need
Patchy's editable text, effects, masks, or Smart Objects. Import notices and save
warnings describe conversions that affect a particular document.

### Affinity import

Patchy imports Affinity documents: the current .af format ("Affinity by Canva"), the Affinity 2 formats (.afphoto, .afdesign, .afpub), and most Affinity 1.x files. Raster layers (16-bit, float, CMYK and Lab become 8-bit RGB), groups, masks (raster and vector), clipping, blend modes, opacity, editable text with per-run styles, vector curves, parametric shapes, compound-shape booleans, Designer symbols, artboards, layer effects, supported adjustment layers, and placed images (as embedded Smart Objects) import. Strokes keep their width, alignment, dashes and miter limit, crop-to-shape containers become masked groups, and the Erase blend mode becomes an isolated group with an inverse-alpha mask. A file that can't import as layers opens as its embedded preview, noting what was skipped.

Affinity features that are not supported yet, or are only partially supported:

- Saving to Affinity formats (import only; save your edits as PSD)
- A few parametric shape kinds (callouts, spirals, QR codes, circle-rounded stars, and exotic arrow ends) import as named placeholders
- Adjustment layers beyond the eight kinds the importer maps (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold), and live filters, import as named empty placeholders; Brightness/Contrast and Color Balance import approximately
- Affinity-only blend modes render through their closest Photoshop-compatible equivalent with a notice (Average matches exactly at half opacity; Negation, Reflect, Glow, and Pigment approximate; Contrast Negate falls back to Normal)
- Bevel/Emboss and glow effects are approximated; Gaussian blur layer effects bake into the layer pixels (they render correctly but are no longer live)
- Rotated or sheared frame text renders without its rotation (artistic text rotates correctly)
- Multi-page documents open the first spread only
- Affinity 1.x-era files: embedded-document placement can land slightly off
