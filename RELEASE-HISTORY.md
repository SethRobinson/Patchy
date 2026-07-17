# Release History

Older Patchy release notes are collected here. The two most recent releases
remain in [README.md](README.md#whats-new).

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
