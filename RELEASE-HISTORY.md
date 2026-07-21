# Release History

Older Patchy release notes are collected here. The two most recent releases
remain in [README.md](README.md#whats-new).

## 0.80 - July 18, 2026

- Vector tools are here: the Rectangle, Ellipse, Line, Polygon, and Custom Shape tools draw editable shape layers, the Pen builds bezier paths point by point with cursor badges that show what each click will do, and the Path Select and Direct Select tools move whole shapes or individual anchors with live re-rendering. Shape layers, vector masks, and saved paths round-trip through PSD and PSB files that open correctly in Photoshop
- Shapes carry a full appearance: no fill, solid color, gradient, or pattern fills and strokes, with stroke width, inside/center/outside alignment, caps, joins, and dashes. Edit them from the options bar's new Fill and Stroke paint pickers or the Shape Appearance dialog, where live rectangles, ellipses, and lines can also be re-edited numerically (bounds, per-corner radii, endpoints, weight). Layer > New Fill Layer creates solid, gradient, and pattern fill layers that can clip to a path
- A Photoshop-style Paths panel manages saved paths and the work path: fill a path with color or a pattern, stroke it through the real brush engine with an optional pressure taper, convert paths to selections and selections to work paths, free-transform a path or just its selected anchors with Ctrl+T, drag rows to reorder, and mark a saved path as the print clipping path
- SVG files open as editable shape layers: live shapes, groups as folders, gradients, stylesheet classes, clip paths, simple patterns, embedded images, and basic text all survive (files past the supported subset fall back to a flattened raster import, and .svgz works too). Save As and Export write SVG back out with shape layers kept as real vectors. You can also paste SVG from the clipboard as shapes, place an SVG as an embedded smart object, and turn an SVG file into a stampable custom shape
- New Liquify workspace (Filter > Liquify) with Warp, Reconstruct, Smooth, Twirl, Pucker, Bloat, Freeze, and Thaw brushes over a live preview
- New Lens Blur (aperture blade count, curvature, and rotation) and Iris Blur (elliptical focus region) filters, plus Add Noise with uniform or Gaussian distribution, bringing the Filter Gallery to 32 effects
- Emboss, Box Blur, Radial Blur, Add Noise, and Mosaic now work as editable native Smart Filters, bringing the roster to 13 filter types. The Filter Gallery marks which effects can apply as Smart Filters on the current layer, and Photoshop now opens Patchy-authored Smart Filter files correctly
- Four new adjustment layers: Brightness/Contrast, Invert, Posterize, and Threshold, all reading and writing native Photoshop PSD data. Color Balance adjustment layers now save natively too, so Photoshop no longer opens them as flat white layers
- Redesigned New Document dialog with a preset card grid, and Patchy now starts with a clean workspace instead of an empty untitled document
- The tool palette is reorganized into clusters with new Stamp and Gradient/Fill flyouts, options-bar number fields gained slider popups with common values, and document tabs offer Reopen, Reveal in Explorer/Finder, and Copy File Path

## 0.20 - July 15, 2026

- New classic Healing Brush transfers detail from an Alt-clicked source while adapting it to the destination tone. Aligned sampling, adjustable Diffusion, selections, palette mode, and ordinary PSD/PSB pixel round-trips are supported
- New Dodge, Burn, Sponge, Blur, and Sharpen brushes provide local tone, color, and detail corrections. They share Size, Softness, and Strength controls, include tonal-range and vibrance options, respect selections and palette mode, and save as ordinary Photoshop-compatible layer pixels
- Unsharp Mask and Motion Blur now work as editable native Smart Filters, with Photoshop-compatible settings, PSD descriptors, shared masks, blending, and stack controls. Their destructive Filter menu versions use the same calibrated renderers
- Brush painting now has separate Opacity and Flow controls plus timed Airbrush buildup while the pointer is held still. Flow uses fixed spatial dabs, respects the per-stroke opacity ceiling, works on grayscale mask targets, and saves as ordinary PSD/PSB pixels
- New Plastic Wrap filter adds adjustable highlight strength, detail, and smoothness. It is available destructively, in the Filter Gallery's Artistic category, and as an editable Photoshop-compatible Smart Filter in PSD files
- New Filter Gallery with 29 effects across photo looks, blur, sharpen, distort, noise, pixelate, stylize, render, and artistic categories. Search and favorites make effects easy to find, while full-resolution live preview, reorderable effect stacks, per-effect opacity and blending, and reusable Saved Looks support more involved recipes
- Smart Filters now use Photoshop-compatible native PSD data. Smart Objects can carry editable stacks of Gaussian Blur, High Pass, Median, Dust & Scratches, Surface Blur, Unsharp Mask, Motion Blur, and Plastic Wrap, with per-filter visibility and blending plus one shared paintable mask. Supported stacks survive PSD round-trips and rebuild from the original Smart Object contents after edits and transforms
- Camera raw support opens CR2, CR3, NEF, ARW, RAF, DNG, and more through a 16-bit develop dialog with white balance, exposure, highlight recovery, contrast, highlights, shadows, saturation, vibrance, demosaic, and noise-reduction controls
- HEIC and HEIF photos now open read-only through platform codecs, including orientation and color-profile handling. Windows offers Store links when a required codec is missing, while Linux explains how to install its optional Flatpak codec extension
- Layer styles gained Pattern Overlay, Satin, gradient midpoints, expanded Bevel & Emboss controls, and Photoshop-compatible pattern data. A new Styles page and Style Manager add 39 built-in presets plus .asl import/export
- Pattern and gradient libraries now support Photoshop .pat and .grd files. The Pattern Manager can also import ordinary images, while 20 bundled CC0 photo textures and 13 matching material styles provide ready-to-use wood, stone, metal, fabric, and ground surfaces
- The Filter Gallery collection adds High Pass, Median, Dust & Scratches, Surface Blur, and Tilt-Shift Blur. Tilt-Shift includes a draggable on-image focus control, while the supported classic blur and sharpen filters can be added directly to native Smart Filter stacks
- Text editing gained a searchable font picker with live type specimens and a Character panel for leading, tracking, and horizontal or vertical glyph scaling. Imported PSD text now follows Photoshop's leading, tracking, scaling, transform, and first-baseline behavior more closely
- Curves has a new point editor and .acv preset support, CMYK PSD files now use their embedded ICC profiles for pixels, text, and effect colors, and gradients gained Classic, Perceptual, and Linear interpolation with Photoshop-compatible alignment
- New Quick Mask mode lets brushes and other paint tools edit a selection through a red overlay before converting it back to marching ants
- Resolution and measurement handling now matches Photoshop more closely: image resolution is independent metadata, rulers can use pixels, inches, centimeters, millimeters, points, or percent, and Image Size, New Document, Smart Object placement, and printing share the same PPI model
- Photoshop Fill Opacity now reads, renders, edits, and writes through PSD files, including the special Fill behavior used by Color Burn, Linear Burn, Color Dodge, Linear Dodge, and Difference
- Fixed Stroke styles on masked layers, filter-stack reordering, Smart Filter conversion state, macOS scanner flow, text Bold/Italic shortcuts, gradient alignment, Blend If controls, and several session-close and revision-cache bugs

## 0.16 - July 11, 2026

- New Channels panel with editable saved alpha channels, read-only RGB component previews, selection save/load, colored overlays, and lossless PSD/PSB preservation of spot channels
- Smart Objects now round-trip through PSD and PSB files, including embedded and linked content. Place or convert layers, edit or replace contents, update or relink external files, embed linked objects, duplicate them independently, and rasterize them when needed
- New Warp Transform tool with a draggable 4x4 cage, live preview, and Photoshop-compatible style presets. Smart Objects keep the warp non-destructive, while pixel layers apply it in one undoable step
- Warp Text supports all 15 Photoshop warp styles plus horizontal and vertical distortion, with a live dialog preview and editable text preserved through Photoshop round-trips
- Documents can float in separate OS windows: drag tabs out to float them, drop windows on the tab bar to dock them, or use Window > Tile, Cascade, Float All in Windows, and Consolidate All to Tabs
- Clipping masks now render and round-trip through PSD files, with Ctrl+Alt+G, clickable row badges, and Photoshop-style Alt-click between layer rows. View Layer Mask shows a mask in grayscale and selects it for painting
- Hue/Saturation Colorize now renders, loads, and saves as a native PSD adjustment. CMYK PSD fixes restore effect and text colors, accept empty layer channels, and improve imported adjustment-layer clipping behavior
- Text edits now show apply and cancel buttons in the options bar, new text layers appear in the Layers panel as soon as editing starts, and layer badges open their matching Smart Object or Layer Style controls
- Fixed transparent Smart Object pixels turning black after PSD/PSB saves, phantom masks appearing on some files, and slow zooming or panning on very large documents

## 0.15 - July 8, 2026

- New file formats, all reading and writing: Windows icons and cursors (ICO/CUR, every embedded size opens as a layer), Targa (TGA), GIF, Aseprite (.aseprite/.ase, layers with blend modes and opacity round-trip), PCX, and Amiga IFF/ILBM
- Sprite sheet workflow for game development: File > Export Layers as Sprite Sheet renders each visible top-level layer into a padded grid, and File > Import > Sprite Sheet to Layers slices a sheet back into layers
- View > Seamless Tile Preview: a live tiled preview window for authoring seamless textures, with drag panning and a resizable, remembered window
- Import images directly from a scanner or camera (File > Import, Windows)
- Export at 2x-8x nearest-neighbor scale for crisp pixel-art upscaling
- Six new blend modes - Linear Dodge (Add), Subtract, Divide, Exclusion, Hue, and Color - completing the Photoshop set; Hue/Saturation/Color/Luminosity now use the exact math Photoshop and Aseprite share
- Open Recent Folder now remembers up to 200 folders in paginated submenus

## 0.14 - July 8, 2026

- The color picker's swatch column is now palette-driven: a dropdown switches between Basic colors, the current document palette, a loaded palette file, and built-in presets, and you can load and save palette files (.pal/.gpl/.hex/.act/.aco/.ase) right from the picker
- Drag and drop colors between the palette grid, the current-color preview, and custom slots, with focus-aware Edit > Cut/Copy/Paste; edit the current and loaded-file palettes in place (built-in presets stay read-only) and set a custom slot with one button
- New built-in DOS / VGA 256 and Dink Smallwood palettes, and the Palette panel can copy hex codes and push a clicked swatch straight into an open color picker
- Convert to Indexed's preview now zooms and pans at full resolution and shows a progress bar while applying, and Image > Mode > RGB Color can keep the palettized look by snapping off-palette layers
- Large documents are dramatically faster to edit: dirty-rectangle undo/redo, parallel compositing, and smarter caches make the standard large-document stress test roughly seven times faster, and big canvases keep the previous frame during recomposites and undo instead of flashing a checkerboard
- Drop Shadow Spread and Inner Shadow/Glow Choke now expand the shape geometrically before blurring, matching Photoshop (high Spread/Choke no longer produces square chunks or fringes)
- macOS dialogs now match the Windows layout (compact dynamics popup, growing form fields, classic scrollbars, dark tab bars) and no longer drop behind the main window or overlap label text with checkboxes
- Crop to Selection now recenters the cropped image in the viewport

## 0.13 - July 6, 2026

- Patchy is now cross-platform: a native macOS build (Apple Silicon, signed and notarized DMG) and a Linux build (Flatpak bundle), with the same features and byte-identical file formats on all three platforms
- New palettized (indexed color) editing mode for pixel art and retro game development: Image > Mode > Indexed constrains painting to a palette with a WYSIWYG canvas, and a Palette panel offers built-in retro presets (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), drag-to-swap entries, and swatch copy/paste
- Convert existing art to a palette with optional dithering, snap stray pixels back with Image > Snap to Palette, round-trip the palette through PSD files, and export exact indexed PNG-8 and 2/4/8-bit BMP
- Unlike Photoshop's indexed mode, palettized editing doesn't flatten or lock anything: layers, layer styles, effects, text, and filters keep working, previewed through the palette live
- macOS feels native: global menu bar with standard About/Settings placement (Cmd+,), Photoshop-style Cmd shortcuts, delete key clears layers, trackpad pinch-to-zoom with two-finger panning, Finder double-click opens into the running app, titlebar document proxy and dirty-dot conventions
- Linux integrates properly: portal file dialogs, desktop entry with file associations, Wayland and X11 support, and update notices that offer a one-line copy-paste install command
- Delete now pops magnetic-lasso anchors mid-trace (matching Backspace) instead of clearing the layer, on every platform
- Fixed a crash when closing the window while an inline text edit was still open

## 0.12 - July 5, 2026

- New Magnetic Lasso sub-tool traces object edges live, supports manual correction anchors, Backspace/Enter/double-click editing, and anti-aliased selection commits
- Stroke layer styles now match Photoshop's inside/center/outside edge bands more closely, including correct center width and a new Stroke blend mode control
- Brush dynamics gained per-setting control sources like Fade, Pen Pressure, Pen Tilt, Pen Rotation, and Stylus Wheel, with broader `.abr` import support
- Soft bitmap brush tips now blend overlapping stamps without light seams, improving pattern brushes and self-crossing strokes ([@mcapogna](https://github.com/mcapogna))
- Rectangle and ellipse tools gained Fixed Ratio, Fixed Size, Alt draw-from-center, and live size readouts while dragging
- Brush-size hotkeys now scale proportionally with the current brush size, so large brushes resize quickly while small brushes keep fine control
- Eyedropper and color-picker workflows are smoother, with a shared eyedropper cursor, better picker window behavior, and live color updates
- Fixed Windows snap/maximize edge cases ([@mcapogna](https://github.com/mcapogna)) and shape-option syncing across new or switched documents

## 0.11 - July 4, 2026

- Bitmap brush tips: import Photoshop `.abr` brush sets, organize them in a brush manager with folders, define a tip from a selection, and pick from 36 built-in tips (natural media plus stamp and pattern brushes); brush size now goes up to 512px with edge softness
- Brush dynamics (Photoshop-compatible): per-dab size, angle, roundness, scatter, and count jitter, imported from `.abr` presets and editable per tip or on the Round brush
- New Quick Select tool (Shift+W): brush over an object and Patchy selects it, with Add/Subtract modes, Sample All Layers, and Enhance Edge
- Gradient Overlay layer effect rebuilt around a two-track editor with draggable color and opacity stops, blend mode, reverse, and style controls
- Fresh hand-authored tool palette icons that stay crisp at any display scale, with Photoshop-style flyout corner indicators that open on a short hold
- Editable zoom percentage box in the status bar; Zoom In/Out and Actual Pixels now stay centered instead of panning the canvas off screen
- Eyedropper shows the picked R, G, B and hex values and live-updates an open Foreground Color panel
- Delete on a text layer removes the whole text object instead of erasing its pixels (matches Photoshop)
- Smoother, non-blinking marching-ants selection outline
- Fixed feathered Add/Subtract selections deleting the whole selection, and added a corner-radius option to the rectangular marquee

## 0.10 - June 29, 2026

- Zoom tool improvements: clearer zoom-in/zoom-out cursor badges, point zooming from the grey canvas area, and edge-clamped marquee zoom ([@mcapogna](https://github.com/mcapogna))
- Gradient tool improvements: gradient fills preview live while dragging, and the gradient stop editor is easier to edit and adjust ([@mcapogna](https://github.com/mcapogna))
- Toolbar sliders now drag smoothly and jump directly to the clicked spot instead of stepping there ([@mcapogna](https://github.com/mcapogna))
- Eyedropper can sample colors by dragging from Patchy onto the screen ([@mcapogna](https://github.com/mcapogna))
- Marquee and lasso selections have better undo/redo, mode handling, and previews ([@mcapogna](https://github.com/mcapogna)); selection history now has Japanese translations
- Fixed frameless window border artifacts and maximize regressions

## 0.9 - June 22, 2026

- Merge Down now flattens folders and any multi-selection, discarding hidden layers (matches Photoshop)
- Single-instance: double-clicking a file in Explorer opens it in the existing window instead of launching a new copy
- 32-bit BMPs (and other flat images) import their per-pixel alpha as an editable layer mask
- Selection tools: drag the outline to move it, arrow-key nudge, click-to-deselect, grey-area selection, lasso improvements, and combine-mode cursor badges ([@mcapogna](https://github.com/mcapogna))
- Shape tools: antialiased soft/thick outlines and fills, rounded-corner rectangles, Shift for 1:1, and dedicated Fill opacity/softness
- Open dialog remembers the last folder; new Open Recent Folder menu with copy-path and open-in-explorer actions
- Fixed Ctrl+T transform nudge so arrow keys move the bounding box with the pixels
- Splash screen dismisses faster
- Improved color picker with new sliders and wheels
