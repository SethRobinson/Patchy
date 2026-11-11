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
- An opaque Background layer locks position and transparency like Photoshop's, so the Eraser paints the background color, and Image > Flatten Image merges the visible layers into it; a click on its padlock in the Layers panel makes it an ordinary layer
- Transparent flat images (PNG, WebP...) open as a transparent layer; a Preferences option opens their alpha as a layer mask
- Non-destructive adjustment layers (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold, Exposure, Channel Mixer, Photo Filter, Gradient Map, Black & White, Color Lookup (.cube, .3dl or embedded), Selective Color, and Vibrance) with live preview, editable settings, native PSD data, presets with .acv, .ahu, .alv and .blw import/export; each also applies from Image > Adjustments
- Shadow and highlight clipping shown on the canvas while you adjust: in Levels, hold Alt and drag the black or white input slider; in Curves, use the Shadows, Highlights and Both buttons. A single color channel tab shows that channel's clipping
- One-step Image > Adjustments with no dialog: Invert (Ctrl+I), Auto Tone, Auto Contrast, Auto Color and Auto All
- Smart Objects: place or convert layers to embedded or linked smart objects, edit or replace their contents (SVG and the like in your default app), transform them non-destructively, and build native Smart Filter stacks (blur, sharpen, noise, distort, stylize, pixelate, render, adjustments) with paintable shared masks and per-filter blending; scripts can list, add, hide, reorder and edit Smart Filters too
- Linked files and Krita file layers on a network server open only from servers you allow, so a downloaded document cannot make Patchy connect to an unknown server
- Layer style, pattern and gradient preset libraries with .asl, .pat and .grd import/export, 39 built-in styles (grid or list; Make Default per effect; round angle dials), 20 CC0 photo textures; Edit > Presets opens all managers, Shapes Manager (.csh, grids, resizable picker, 878 game-icons.net shapes)
- Continuous long shadows for type and shapes (a normal drop shadow in Photoshop; [example](screenshots.md))
- Warp Transform tool and Warp Text with all 15 Photoshop warp styles and live preview

## Vectors and typography

- **Vector tools**
  - Pen paths and shape layers (Rectangle, Ellipse, Line, Polygon, Custom Shape) with solid, gradient or pattern paint, and editable dash and stroke presets with offsets
  - Point edits (add by click, break, join, weld or merge by drag, delete segment), path-edited vector masks, and Merge and Split Shapes
  - Per-shape cut, copy, paste, color, align and new layer; editable radius, sides, star inset, line weight and arrowheads (width, length and concavity in Shape Appearance, up to 1000 times the weight, so a thin line can carry a large head); a line's stroke makes the line thicker
  - The shape tools show the active shape's box with eight handles: drag one to resize it without leaving the tool (Shift keeps the proportions, Alt resizes from the center), and drag anywhere else to draw the next shape. A polygon or star shows its center and one vertex instead (drag the vertex to turn and resize it, the center to move it), and a line shows its two ends, so it can be re-aimed and keeps its weight and arrowheads
  - A shape dragged with Path Select, or with Ctrl held in a shape tool, snaps its edges and center to guides, the grid, the canvas and other layers and shows the alignment guide, as the Move tool does (View > Snap turns it off)
  - Shape Appearance Geometry (position and size) for every single shape, custom shapes and Pen outlines included, with a picker that swaps a custom shape in its box
  - A Paths panel (fill, stroke, selection) and switchable fill layers (badge or double-click opens settings), all saved in PSDs Photoshop opens
- Dynamic Vector Preview keeps shapes and vector masks sharp at any zoom. Merge Layers keeps vectors and bitmap runs apart, merges within groups or makes a merged copy (Ctrl+Alt+E: selected layers only). Merge Visible bakes a layer's mask or effects
- Trace Image to Shapes: turns a pixel layer into editable shape layers, one per color, with presets, color, gray and black-and-white modes, abutting or overlapping shapes, noise removal, live preview
- Point text, paragraph (box) text and area text that flows inside a closed shape or path, saved as native Photoshop text frames that keep each run's Photoshop hyphenation and spelling language, each paragraph's Hyphenate setting and line composer (Adobe Every-line or Single-line) and Photoshop's Middle Eastern settings (Kashida width, digits, Justification Alternates, diacritic position); a box or area frame being edited has corner and side handles (a side handle moves one edge); while you edit a box that hides lines, a plus on its bottom right handle says so, with a tooltip on hover
- Rich text with per-run color, font, size and style, a searchable font picker (recent fonts you can remove one by one, favorite fonts with a Favorites-only view, style/language filters with a count of search matches they hide, and a Preferences option to show fonts named in another script under their English names), stand-ins for missing fonts (a symbol font such as Wingdings gets only a font with the same symbols, never a text font; Resolve Missing Fonts remembers the replacement chosen for each missing font, starts on it the next time and lists earlier choices (each removable) and every suggestion with its reason at the top of the font list; the Font and style fields name a selected layer's missing font and its face in italics, with a warning badge), and Character controls (leading, tracking, glyph scale, caps, super/subscript, underline, strikethrough, OpenType features) that edit selected layers
- Paragraph alignment and Photoshop's four justify modes (last line left, centered or right, or justify all), first-line and left/right indents, spacing before and after paragraphs, vertical text, and left-to-right or right-to-left paragraphs
- The Type tool's options bar has Bold, Italic and Underline buttons, a Justify toggle that works with Left, Center and Right, and icon toggles for Vertical and RTL; anti-aliasing is in the Character panel and the Type menu
- Type > Convert to Shape and Create Work Path turn any text, warped too, into vector outlines
- Type Mask tool: type over a tinted canvas and the letters become a selection (new, add, subtract or intersect), horizontal or vertical, with no layer made
- The Type menu opens the Character and Paragraph panels and sets alignment, direction, orientation, anti-aliasing and OpenType features for the selected text, with Rasterize Type Layer and Warp Text; Update All Text Layers re-renders every text layer, Replace All Missing Fonts swaps each missing font for the font chosen for it before, or else its suggested stand-in, without a dialog, and Paste Lorem Ipsum adds placeholder text

## Pixel art and game assets

- Palettized (indexed color) editing mode for pixel art: paint constrained to a palette, quantize with optional dithering, built-in retro palettes (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), and exact indexed PNG-8 and 2/4/8-bit BMP export. Layers, styles and effects keep working, unlike Photoshop's indexed mode
- Named palette colors appear in the Palette panel, color picker, Info panel and eyedropper readout. Rename swatches, preserve names through GPL, PSD, and indexed PNG round trips, and manage palettes through scripts
- Pixel-art and game-dev extras: seamless texture authoring (live tile preview, in-canvas tiling, seam shifting), sprite sheet and numbered image sequence export/import, animated GIF, WebP and PNG import/export (frames become layers with timings in their names, visible layers save as a looping animation, a layer named "all" is drawn in every frame as a shared background or overlay, a group named "all" makes each layer inside it a frame while its effects, mask and clipping apply to all of them, previewed by the film button or Image > Animation Preview, with delay presets and a play in reverse option), and flat and web exports with pixel scaling, resizing, edge trimming and background fill

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

- Classic Photoshop filter plug-ins (.8bf, 32 and 64-bit) on Windows, 8 and 16-bit (Filter > Last Filter, Ctrl+Alt+F, repeats the last one): see [installing plug-ins](#installing-photoshop-plug-ins-windows)
- Over a hundred GEGL filters and About GEGL (Filter > GEGL Filters): [GEGL filters](gegl-filters.md)
- JavaScript scripting: a Script Manager (File > Scripts) with a tree of bundled and user scripts, an editor with live run status, per-script hotkeys (`@hotkey` or your own), a documented API for documents, layers, text, selections, pixels, filters, form dialogs, file pickers, view zoom and position for window captures, and batch processing, editable bundled examples from data merge to glitch effects and games, and --run-script with arguments for tools and AI agents (--headless for servers and CI). See the [scripting guide](../scripts/bundled/scripting-guide.md) (Help menu)
- Local AI control through the bundled MCP connector: pressure-aware brush strokes, brush presets, editable vector shapes and paths, palette controls, image previews, and persistent document sessions. Help > Set up AI Control has setup steps and prompts ([guide](ai-control.md)). Plugins > Companions starts apps that use it ([companions.md](companions.md))
- **OpenAI Image Tools via API Key** (Plugins > Companions, built-in). It uses your own OpenAI API key and is billed to your OpenAI account; nothing is sent until you click one of its action buttons ([openai-images.md](openai-images.md))
  - Create: type a prompt, optionally add reference images (the active layer, the visible image, files dropped or chosen, or the clipboard; numbered so the prompt can say "image 2", and used by Create, Edit and Animate), and each picture OpenAI returns becomes a new layer, with a transparent background if you ask for one
  - Edit and Remove redraw the selected area (from a prompt, or from its surroundings) on a new layer masked to the selection, or straight into the active layer. An Edge blend slider (0 to 100%) sets how far past the selection the result shows and how soft its edge is, adjustable after the result arrives; with several variations you click through them on the one result
  - Expand grows the canvas by the pixels you type or drag for each side and fills the new area on a new layer
  - Animate makes a short GIF animation from a prompt (optionally starting from a layer) and opens it with one layer per frame, in OpenAI's 256-color GIF mode or in full color from one generated sheet
  - Describe writes a detailed text description of the visible image or the active layer

### Installing Photoshop plug-ins (Windows)

Choose **Plugins > Open Plug-ins Folder**, copy your `.8bf` files and their supporting
files into it, then choose **Plugins > Rescan Plug-in Folders**. Both 32-bit and 64-bit
filters appear under **Plugins > Legacy Photoshop Plug-ins**.

Filters run on the active pixel layer, respect the selection, and apply as one undoable
step. A separate helper isolates plug-in crashes. Plug-ins still run with your user
permissions, so install only ones you trust. KPT and other formerly full-screen filters
get a bounded, movable window. [Plug-in details](plugins.md).

## Current Status

Patchy does not cover the full PSD surface yet, but a Photoshop round-trip mostly works with RGB/RGBA documents (8, 16, or 32 bits per channel) that use basic pixel layers, text objects, groups, masks, blend modes, layer styles, and supported adjustment layers.

Photoshop features not supported yet, or only partly:

- Editable Smart Filters cover many classic filters and Image > Adjustments commands with paintable shared masks and per-filter opacity and blend modes; unsupported imported filter types (including the Blur Gallery and Liquify smart filters) remain preview-locked and byte-preserved
- Photoshop's newer **Color and Vibrance** adjustment opens as a preserved placeholder without applying its effect (legacy **Vibrance** is supported)
- True CMYK and Lab editing, CMYK saves of edited PSD files, editable spot separations and RGB component channels, multi-channel overlays, and full color-management parity (Patchy edits in RGB and converts the other modes on open; for print output see [Color and print](#color-and-print))
- OpenEXR imports single-part scanline or tiled RGB/RGBA or luminance images with supported compression; multipart, deep, and some compression types are unsupported. AVIF is desktop-only and exports still images, although sequences can be imported
- 16 and 32-bit gaps: filters without a deep version run on an 8-bit copy at 16 bits (unchanged pixels keep full precision) and are disabled at 32 bits, as are Liquify and the Filter Gallery; Smart Object contents and their Smart Filters render at 8 bits; Spot Healing, Remove Object, and Patch heal at 8-bit precision
- Layer comps, timeline/video editing, generative fill and expand inside a selection (animated GIF, WebP and PNG are supported; OpenAI Image Tools via API Key makes new layers from a prompt)
- Photoshop's own automation surfaces: Actions (.atn), UXP/JSX panels, and scripts written for Photoshop (Patchy has its own JavaScript scripting and batch processing)
- High-fidelity PSD/PSB edge cases and byte-perfect preservation of every Photoshop-only metadata block
- Performance: Patchy is slower than Photoshop, especially on large documents, and has no GPU acceleration (layer styles are not rendered in pixel shaders, for example). Rendering only on the CPU helps porting, keeps output the same on every platform, and adds stability, so it is a deliberate trade-off for now. Canvas compositing and image flattening are multithreaded, splitting large images (4 Mpx and up) into strips rendered on all CPU cores
