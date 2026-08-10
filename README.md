# Patchy Image Editor

### [Download Patchy for Windows, macOS, and Linux, or try in your browser](#download)

Patchy is a free, open-source image editor for Windows, macOS, and Linux, built for accurate PSD and PSB editing and round trips with Adobe Photoshop. It supports editable text and vectors, masks, layer styles, Smart Objects and Smart Filters, legacy 8BF plug-ins, JavaScript scripting, and command-line automation.

The browser version is the same editor compiled to WebAssembly. It runs entirely on your machine and nothing you open or make is sent online. The desktop builds are the faster and more capable way to use Patchy: they are not limited to a browser tab's 4 GB of memory, and they add printing, scanner and camera import, and command-line automation.

## Screenshots

Click a thumbnail for the full-size image.

<table>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/levels.png"><img src="docs/images/screenshots/levels.png" width="270" alt="Levels adjustment dialog over an image with a live histogram and input and output controls"></a>
      <br><sub>Non-destructive adjustment layers with live preview and editable settings</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/layer_styles.png"><img src="docs/images/screenshots/layer_styles.png" width="270" alt="Layer Style dialog applying bevel, stroke, glow, and shadow effects to text"></a>
      <br><sub>Layer styles with multiple effects, blending controls, and Photoshop-compatible presets</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/brush_tips.png"><img src="docs/images/screenshots/brush_tips.png" width="270" alt="Brush Tip Manager showing a collection of textured and shaped brush presets"></a>
      <br><sub>Brush tip presets, import, management, spacing, angle, roundness, and texture controls</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/brush_dynamics.png"><img src="docs/images/screenshots/brush_dynamics.png" width="270" alt="Brush Dynamics dialog with pressure and random controls beside a varied brush stroke"></a>
      <br><sub>Pressure-aware brush dynamics for size, opacity, flow, angle, scatter, and color</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/palette_mode.png"><img src="docs/images/screenshots/palette_mode.png" width="270" alt="Palette editing mode with a limited color palette and indexed-looking pixel art"></a>
      <br><sub>Palette mode constrains painting and editing to a document color set</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/hue_saturation.png"><img src="docs/images/screenshots/hue_saturation.png" width="270" alt="Hue and Saturation adjustment dialog editing a colorful photograph"></a>
      <br><sub>Live color adjustments, including targeted Hue/Saturation ranges</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/warp_text.png"><img src="docs/images/screenshots/warp_text.png" width="270" alt="Warp Text dialog with the style list open over a poster with arced rainbow text and flag, fisheye and twist warped words"></a>
      <br><sub>Warp Text with live preview: all 15 Photoshop warp styles on editable rich text</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/tile_preview.png"><img src="docs/images/screenshots/tile_preview.png" width="270" alt="Pixel-art grass and path tile repeating across the whole canvas in seamless tiling mode, with a painted black line wrapping across every tile edge"></a>
      <br><sub>Seamless tiling mode for game textures: paint on the canvas and strokes wrap live across every tile</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/smart_objects.png"><img src="docs/images/screenshots/smart_objects.png" width="270" alt="Game title art with a smart object mid warp transform showing the Bezier cage, and its embedded contents open in a second tab"></a>
      <br><sub>Smart Objects: Warp Transform bends them non-destructively, Edit Contents opens the embedded file in its own tab</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/pattern_manager.png"><img src="docs/images/screenshots/pattern_manager.png" width="270" alt="Pattern Manager showing the Textures folder with the bundled CC0 Weathered Marble photo texture selected in the large preview"></a>
      <br><sub>Photo textures in the Pattern Manager, with full-resolution preview, import, organization, and editing</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/smart_filters.png"><img src="docs/images/screenshots/smart_filters.png" width="270" alt="Photo with a Smart Filter stack containing Surface Blur, Dust and Scratches, and Gaussian Blur plus a shared mask"></a>
      <br><sub>Editable native Smart Filters with one paintable shared mask and per-filter controls</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/camera_raw.png"><img src="docs/images/screenshots/camera_raw.png" width="270" alt="Camera Raw develop dialog showing a snowy mountain photo with white balance, tone, color, and detail controls"></a>
      <br><sub>16-bit Camera Raw development with white balance, tone, color, demosaic, and denoise controls</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/tilt_shift.png"><img src="docs/images/screenshots/tilt_shift.png" width="270" alt="Tilt-Shift Blur in the Filter Gallery over a real San Francisco city photograph with draggable focus controls"></a>
      <br><sub>Tilt-Shift Blur with live on-image focus, angle, and transition controls</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/material_styles.png"><img src="docs/images/screenshots/material_styles.png" width="270" alt="Layer Style dialog applying Riveted Steel to large text from the built-in Materials preset folder"></a>
      <br><sub>Material layer styles backed by bundled CC0 wood, stone, metal, fabric, and ground textures</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/quick_mask.png"><img src="docs/images/screenshots/quick_mask.png" width="270" alt="Quick Mask mode showing a clean feathered portrait-frame selection as a red overlay with the temporary Quick Mask channel visible"></a>
      <br><sub>Quick Mask turns a selection into a brush-editable red overlay, then back into marching ants</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/vector_tools.png"><img src="docs/images/screenshots/vector_tools.png" width="270" alt="Flat vector sunset poster built from shape layers, with Direct Select showing a mountain ridge's anchors and the Paths panel floating beside the canvas"></a>
      <br><sub>Vector shape layers: gradient fills, pen paths, anchor editing, and the Paths panel</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/shape_appearance.png"><img src="docs/images/screenshots/shape_appearance.png" width="270" alt="Shape Appearance dialog editing a rounded-rectangle badge with a Golden Hour gradient fill, dashed stroke, and per-corner radius controls, beside a star with a rust pattern stroke"></a>
      <br><sub>Shape fills and strokes: solid, gradient, or pattern paint, dashes, and live corner radii</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/svg_import.png"><img src="docs/images/screenshots/svg_import.png" width="270" alt="CC0 hot air balloon SVG clip art opened as editable shape layers with group folders in the Layers panel and the big balloon's bezier anchors selected on canvas"></a>
      <br><sub>SVG files open as editable shape layers: groups become folders, paths stay live vectors</sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/script_manager.png"><img src="docs/images/screenshots/script_manager.png" width="270" alt="Script Manager running the bundled Breakout script, with breakout.js code, live run status, and stop button beside the game playing on a real document canvas with brick, paddle, and ball layers"></a>
      <br><sub>Script Manager running the bundled Breakout: the game plays on a real canvas, with live run status and one-click stop</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/script_options.png"><img src="docs/images/screenshots/script_options.png" width="270" alt="Duotone script options dialog with instructions, shadow and highlight color fields, and a contrast slider over a photo already remapped to navy and amber"></a>
      <br><sub>Scripts ask with real options dialogs; the same script runs unattended from the command line with the same defaults</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <a href="docs/images/screenshots/affinity_import.png"><img src="docs/images/screenshots/affinity_import.png" width="270" alt="Affinity Photo document open in Patchy as a layered file, the Layers panel showing groups, text layers with effect badges, and raster layers"></a>
      <br><sub>Affinity Photo, Designer, and Publisher files open as layered documents: groups, editable text, effects, and rasters come through</sub>
    </td>
  </tr>
</table>

## PSD compatibility, measured

Patchy is tested against Adobe Photoshop 2026 on a mixed PSD corpus. In the completed August 7, 2026 Testy run, Patchy had the strongest non-Adobe render result, Photoshop reopened all 64 Patchy saves, and all 312 text objects remained editable.

| Tested editor           | Files opened | Perceptual render match | PSD saves rejected by Photoshop | Text still editable in resaved PSD |
| ----------------------- | -----------: | ----------------------: | ------------------------------: | ---------------------------------: |
| **Patchy `879a3a8`**    | **64 / 64**  | **98.83% (n=63)**       | **0 / 64**                      | **312 / 312**                      |
| Photopea, web build     | 63 / 64      | 97.13% (n=62)           | 0 / 64                          | 306 / 312                          |
| Affinity 3.2.3.4646     | 61 / 64      | 88.30% (n=60)           | 0 / 64                          | 0 / 305                            |
| GIMP 3.2.4              | 62 / 64      | 88.11% (n=61)           | 0 / 64                          | 0 / 312                            |
| PhotoDemon 2026.01.0251 | 64 / 64      | 81.97% (n=63)           | 0 / 64                          | 0 / 312                            |
| Krita 5.3.2.1           | 56 / 64      | 81.17% (n=56)           | 9 / 64                          | 187 / 205                          |

The text column counts original text objects that Photoshop still recognizes as editable text after reopening the editor's PSD save. It does not identify whether conversion happened during import or export.

These are corpus-specific results, not universal product ratings. See the [full results and methodology](docs/psd-compatibility-benchmark.md), including the image-free per-file run data, tested versions, native PSD data preservation, Photoshop round-trip rendering, and known limitations.

## Download

Windows releases are code signed by Seth A. Robinson; the macOS app is signed and
notarized (Robinson Technologies Corporation).

| Platform                  | Package                     | Download                                                                                      |
| ------------------------- | --------------------------- | --------------------------------------------------------------------------------------------- |
| Windows 10/11 (64-bit)    | Installer                   | [PatchyWindowsInstaller.exe](https://rtsoft.com/files/PatchyWindowsInstaller.exe) (35 MB)     |
| Windows 10/11 (64-bit)    | Portable ZIP (no installer) | [PatchyWindowsNoInstaller.zip](https://rtsoft.com/files/PatchyWindowsNoInstaller.zip) (35 MB) |
| macOS 12+ (Apple Silicon) | DMG - drag to Applications  | [PatchyMacOS.dmg](https://rtsoft.com/files/PatchyMacOS.dmg) (44 MB)                           |
| Linux                     | Flatpak bundle              | [PatchyLinux.flatpak](https://rtsoft.com/files/PatchyLinux.flatpak) (14 MB)                   |
| Any modern browser        | Nothing to install          | [rtsoft.com/patchy](https://www.rtsoft.com/patchy/) (slower and less capable)                 |

The browser version is the same editor compiled to WebAssembly, and everything runs locally:
nothing you open or make is sent online. Use a desktop build if you can. The browser version is
slower, it is capped at a browser tab's 4 GB of memory so large documents run out of room sooner,
and it leaves out printing, scanner and camera import, and command-line automation.

Linux one-line install (paste into a terminal; fetches the bundle and installs it,
pulling the shared KDE runtime from Flathub automatically):

```sh
curl -L -o /tmp/PatchyLinux.flatpak https://rtsoft.com/files/PatchyLinux.flatpak && flatpak install -y /tmp/PatchyLinux.flatpak
```

Optional: opening iPhone HEIC photos on Linux uses the shared Freedesktop codec
extension, which bundle installs do not fetch on their own. Patchy will show this
command if it is needed:

```sh
flatpak install -y flathub org.freedesktop.Platform.ffmpeg-full//24.08
```

## Features

- Open and save layered PSD and PSB files with groups, masks, clipping masks, saved alpha and spot channels, text objects, Fill Opacity, the full Photoshop blend mode set, layer styles and more
- Common raster editing tools, including Brush with Flow and timed Airbrush buildup, Healing Brush, Spot Healing, Patch, Clone Stamp, Dodge, Burn, Sponge, Blur, Sharpen, Smudge, Eraser, selections, transforms, gradients, and shapes
- Vector tools: Pen paths, editable shape layers (Rectangle, Ellipse, Line, Polygon, Custom Shape) with solid, gradient, or pattern fills and strokes, vector masks, path selection and anchor editing, and a Paths panel with fill, stroke, and make-selection commands, all round-tripping through PSD files that open correctly in Photoshop
- Non-destructive adjustment layers (Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize, Threshold) with live preview, editable settings, native Photoshop PSD data, and .acv Curves preset import and export
- Smart Objects: place or convert layers to embedded or linked smart objects, edit or replace their contents, transform them non-destructively, and build editable native Smart Filter stacks (13 filter types) with paintable shared masks and per-filter blending
- Filter Gallery with 32 effects, live full-resolution preview, ordered effect stacks, favorites, and reusable Saved Looks, plus a manual Liquify workspace with warp, twirl, pucker, bloat, and freeze brushes
- Photoshop-compatible layer style, pattern, and gradient preset libraries, including .asl, .pat, and .grd import/export, 39 built-in styles, and 20 bundled CC0 photo textures
- Warp Transform tool and Warp Text with all 15 Photoshop warp styles and live preview
- Multiple document interface: tabbed documents that can float in their own windows, with Photoshop-style Tile and Cascade arrangement
- Rich text with per-run color, font, size, and style, plus a searchable font picker and Character controls for leading, tracking, and horizontal or vertical glyph scaling
- Palettized (indexed color) editing mode for pixel art: paint constrained to a palette, quantize with optional dithering, built-in retro palettes (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), and exact indexed PNG-8 and 2/4/8-bit BMP export. Layers, layer styles, and effects all keep working (Photoshop's indexed mode flattens and disables them)
- Pixel-art and game-dev extras: seamless texture authoring (live tile preview window, in-canvas tiling mode, seam shifting), sprite sheet export/import, image sequence export/import (numbered files become layers and back), and nearest-neighbor scaled export (2x-8x)
- Reads and writes a wide range of formats: PSD/PSB, PNG, JPEG, TIFF, WebP, BMP, TGA, GIF, PCX, Amiga IFF/LBM, Windows icons and cursors (ICO/CUR), Aseprite files, and SVG (opens as editable shape layers, exports with vectors preserved)
- Imports Affinity documents as layered files: the current .af format, Affinity 2 .afphoto/.afdesign/.afpub, and most Affinity 1.x-era files, bringing across rasters, groups, masks, clipping, blend modes, editable text layers, vector shapes, adjustment layers, layer effects, and placed images (which become embedded Smart Objects)
- Opens camera raw files (CR2/CR3/NEF/ARW/RAF/DNG and more) through a 16-bit develop dialog, and HEIC/HEIF photos through platform codecs
- Photoshop-compatible document resolution, physical measurement units, rulers, image sizing, and printing
- Pen/stylus pressure and size dynamics, GUI scaling, scanner import (Windows and macOS), camera import (Windows), legacy .8bf plugins, and command line options
- JavaScript scripting: a built-in Script Manager (File > Scripts) with a folder tree over the bundled and user scripts, a code editor with live run status, a documented API covering documents, layers, text, selections, pixels, filters, form dialogs, file pickers, and batch processing, bundled examples ranging from CSV data merge, contact sheets, icon export, and versioned saves to glitch/duotone effects and playable Breakout and Pong (scripts can call other scripts), safe editing of bundled scripts (your saved copy overrides the original and can be reverted), and a --run-script command line flag with script arguments so external tools and AI agents can drive Patchy. See the [scripting guide](scripts/bundled/scripting-guide.md) (also under Help inside the app)
- Cross-platform: Windows is the lead platform, with native macOS (Apple Silicon) and Linux (Flatpak) builds
- Built with C++ and Qt for a native desktop experience. No GPU used, should run on a potato
- Privacy: YES! Absolutely no telemetry, no tracking, no data collection (if update checks are enabled, it contacts GitHub only to check for a newer version). Settings live in a plain local file, and the installer doesn't screw with your file extension preferences
- Localized in English and Japanese (change language in File->Preferences)

## What's New

### 0.88 - August 6, 2026

- Free Transform works on folders and multi-layer selections. Ctrl+T takes the whole selection as one target set with linked masks riding along, the Move tool frames the same union it would take, and preview-locked smart objects transform in a multi-target session instead of refusing. Corner drags scale proportionally by default, with a preference that restores the old pairing where a plain corner drag distorts and Shift keeps the aspect ratio
- Floating and docked panels behave. Dragging a tabbed panel's title detaches that one panel, a floating panel gets a visible resizable frame you can drag by its chrome, re-docking snaps a collapsed panel's slot back to its strip instead of leaving a gap that grew with every cycle, dock dividers are visible, and every right dock has a width handle along its full height with panel contents inset clear of it
- Layer thumbnails zoom to their content by default, and a preference turns that off. Double-clicking one fits the canvas view to that layer's pixels, with folders using their children's union, and Ctrl+Alt-clicking a folder arrow expands or collapses every folder the way Photoshop does
- Painting on a smart object offers to rasterize it or open its contents instead of doing nothing, filters and destructive adjustments on a text or shape layer ask to rasterize or convert it first instead of silently losing their result on the next edit, and Rasterize reaches the layers inside a selected folder
- The web build applies the interface scale: the preference used to be saved and ignored there, steps now run from 67% to 200%, and the browser starts at 75%. The scaling happens in the shell page, so clicks land where you aimed them
- More web work: the layer panel scrolls by rows instead of jumping the whole list on one wheel notch, input arriving during a processing wait no longer corrupts move drags or leaves ghost undos and dead hotkeys after a file picker, and preset import and export use the browser's own picker and download path. Memory is budgeted too, with a history byte budget that accounts for shared data, a heap size and ceiling chosen per device, and live memory use shown in About
- Affinity import: adjustments attached to a layer arrive as clipped adjustment layers rather than being misread as masks, minified placed images are box-filtered instead of aliasing, opening a file with Image layers can ask whether to keep smart objects or convert them to pixel layers, and resized canvases, lazily decoded placed images, layer names, collapsed groups, and group adjustment scope all match Affinity
- Text layers keep their transform through Image Size, Canvas Size, Crop, Rotate Canvas, layer flips, and Shift Seams, warped text layers can be edited again without breaking their warp, and the free-transform drag preview applies flip signs
- Fixes: the document tab bar's scroll arrows are no longer clipped, a committed Free Transform holds its frame until the refresh lands instead of flashing the old geometry, and a filter dialog that unwinds by exception disarms its in-flight preview renders

### 0.87 - August 4, 2026

- Patchy runs in a web browser now, at [rtsoft.com/patchy](https://www.rtsoft.com/patchy/). It is the same editor compiled to WebAssembly with real threads: files open from your disk, save back as downloads, and drag straight onto the canvas, and nothing you make is ever sent online. The web build bundles 23 MB of open fonts including Japanese coverage, takes dropped font files and font zips that persist between visits, decodes HEIC photos through the browser, and remembers your settings
- The Bold and Italic buttons in the text options bar are replaced by a Style picker that lists a font family's real faces and resolves them the way Photoshop does. Families that declare their Bold at a sub-Bold weight now pick Bold Italic correctly, and when a family has no such face, Patchy synthesizes a faux bold or faux italic from the face you are using instead of jumping to a different family
- Missing fonts behave: a font with no glyph coverage for the text counts as missing and badges the layer, and editing past the warning substitutes a real font instead of redrawing to nothing. Options-bar changes made with no text selected now apply to the whole layer, like Photoshop
- Text editing on transformed and scaled layers is accurate. Clicks land on the glyphs you can see rather than on an untransformed layout, the caret and selection read the renderer's own line plan, re-editing box text renders live instead of shifting as you enter it, and a commit repaints the text bounds instead of the whole document
- The History panel is interactive. It lists the real states of the session, oldest first with the current one highlighted, one click jumps to any state in either direction, and a right-click action opens any past state as a new document
- Large documents composite and transform much faster. A 7 megapixel reference PSD composites in 371 ms instead of 566, Move and Free Transform previews patch the regions that changed rather than recompositing the canvas (a stress step drops from 381 ms to 104 ms), heavy drags latch onto a low-resolution proxy and re-render accurately on release, previews composite at display resolution while you are zoomed out, dragging the same selection again reuses the snapshots the last drag built, and the Windows release build links with link-time optimization
- Auto Tone and Auto Color join Image > Adjustments, and Auto Contrast now works on the composite the way Photoshop's does
- Advanced Blending's per-channel restrictions render and can be edited, so a layer that blends through only some channels looks right instead of ignoring the setting
- Zoom follows Photoshop's view rules with the same preset steps, and geometry operations like rotate and canvas resize recenter the view instead of leaving it scrolled off the image
- Fixes: Clouds fills the whole canvas, Dust & Scratches reaches a 500 px radius, strokes no longer grow nubs along anti-aliased fringes on transformed text, the Layer Style dialog's Stroke page scrolls instead of stretching the dialog, undo and redo refresh the Paths panel, the start panel lists up to 200 recent files with a right-click menu and a name filter beside the Recent Files label, Open Recent gained a filter box, PSD text layers that record a font by its PostScript name resolve it on macOS and Linux instead of falling back, and the About screen has a light surface in Light mode

[Older releases](RELEASE-HISTORY.md)

## Building it yourself

Build the dependency-light core and tests without the Qt app:

```sh
cmake --preset dev -DPATCHY_BUILD_APP=OFF
cmake --build --preset dev
ctest --preset dev
```

Build the Qt desktop app:

```sh
cmake --preset qt-local
cmake --build --preset qt-local
```

The local Qt app preset writes `patchy.exe` under `build/app`.

Run the standard local test script:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1
```

### macOS and Linux

Install Qt 6.8.3 into `.deps/Qt` (for example `pip install aqtinstall && aqt install-qt
mac desktop 6.8.3 -m qtimageformats -O .deps/Qt`, or `linux desktop 6.8.3 linux_gcc_64`
on Linux), then build the matching preset:

```sh
cmake --preset mac-release      # or linux-release
cmake --build --preset mac-release
```

macOS produces `build/mac-release/Patchy.app`; Linux produces
`build/linux-release/patchy`. `packaging/macos/make-dmg.sh` and
`packaging/linux/make-flatpak.sh` create the distributable artifacts. Both test suites
run offscreen on all three platforms (`QT_QPA_PLATFORM=offscreen`).

## Windows Release Package

Create local Windows release artifacts:

```bat
scripts\release\build-release.bat
```

The script configures and builds the `release` preset, signs `build\release\patchy.exe`, the installer helper executables, and the installer when the local signing environment is available, deploys the minimum Qt runtime needed by the current app, copies third-party notices, and creates:

```text
build\package\PatchyWindowsNoInstaller.zip
build\package\PatchyWindowsInstaller.exe
```

The zip contains a top-level `Patchy` folder so it can be dragged anywhere and does not include installer-only helpers. The installer is a local per-user installer that installs to `%LOCALAPPDATA%\Programs\Patchy`, creates a Start Menu shortcut, offers a desktop shortcut, and registers an uninstall entry.  `latest_version.json` is the update metadata file.

## Current Status

Patchy is not Photoshop-compatible across the full PSD surface yet, but a round-trip from/to Photoshop mostly works with RGB/RGBA 8-bit documents that use basic pixel layers, text objects, groups, masks, blend modes, layer styles, and the currently supported adjustment layers.

Important Photoshop features that are not supported yet, or are only partially supported:

- Editable Smart Filters cover 13 filter types with paintable shared masks and per-filter opacity and blend modes; unsupported imported filter types (including the Blur Gallery and Liquify smart filters) remain preview-locked and byte-preserved
- Full Photoshop adjustment-layer compatibility beyond Patchy's current adjustment support
- CMYK/Lab editing and export, editable spot separations and RGB component channels, multi-channel overlays, 16/32-bit editing, HDR/EXR, and full color-management parity (Patchy converts CMYK/Lab to RGB on open, but does not edit or save in those color modes)
- Layer comps, timeline/video/animation workflows, content-aware tools, and generative tools
- Photoshop's own automation surfaces: Actions (.atn), UXP/JSX panels, and scripts written for Photoshop (Patchy has its own JavaScript scripting and batch processing instead, see above)
- High-fidelity PSD/PSB edge cases, including layered PSB writing and byte-perfect preservation of every Photoshop-only metadata block
- Patchy is slower than Photoshop, especially on large documents and it doesn't support GPU acceleration at all.  However, being CPU only helps with porting and stability so kind of a trade-off that makes sense, for now.  That said, certain operations have been optimized for multicore - canvas compositing and image flattening are multithreaded, splitting large images (4 Mpx+) into strips rendered on all CPU cores.

### Affinity import

Patchy opens Affinity documents read-only: the current .af format ("Affinity by Canva"), the Affinity 2 formats (.afphoto, .afdesign, .afpub), and most Affinity 1.x-era files. Raster layers (including 16-bit, float, CMYK, and Lab documents), groups, masks (raster and vector), clipping, blend modes, opacity, editable text with per-run styles, vector curves, parametric shapes (rectangles, ellipses, polygons, stars, triangles, diamonds, trapezoids, pies, segments, crescents, hearts, tears, arrows, double and square stars, cogs, clouds), compound-shape booleans, Designer symbols, artboards, layer effects, supported adjustment layers, and placed images (which become embedded Smart Objects) all import, verified against Affinity's own renders and a wild-file corpus that includes real Affinity 2 documents from all three apps. Vector fills and strokes keep their width, alignment, dash pattern, and miter limit, crop-to-shape containers come in as masked groups, and the Erase blend mode imports as an isolated group with an inverse-alpha mask, which is how PSD stores that construction natively. If a file can't be imported as layers, Patchy falls back to its embedded preview instead of failing the open, and an import notice explains what was skipped.

Affinity features that are not supported yet, or are only partially supported:

- Saving to Affinity formats (import only; save your edits as PSD)
- A few parametric shape kinds (callouts, spirals, QR codes, circle-rounded stars, and exotic arrow ends) import as named placeholders
- Adjustment layers beyond the eight kinds Patchy models, and live filters, import as named empty placeholders; Brightness/Contrast and Color Balance import approximately
- Affinity-only blend modes render through their closest Photoshop-compatible equivalent with a notice (Average matches exactly at half opacity; Negation, Reflect, Glow, and Pigment approximate; Contrast Negate falls back to Normal)
- Bevel/Emboss and glow effects are approximated; Gaussian blur layer effects bake into the layer pixels (they render correctly but are no longer live)
- Rotated or sheared frame text renders without its rotation (artistic text rotates correctly)
- Multi-page documents open the first page only
- Affinity 1.x-era files: embedded-document placement can land slightly off

## License

Patchy is released under the MIT License. Third-party runtime notices are tracked in `NOTICE-THIRD-PARTY.md`.

## Trademark Notice

Adobe and Photoshop are either registered trademarks or trademarks of Adobe in the United States and/or other countries. Patchy is an independent project and is not authorized, endorsed, or sponsored by Adobe. References to Photoshop and its file formats (PSD, Smart Objects, Smart Filters) are only there to describe compatibility.

## AI Disclosure

This project was developed with significant assistance from AI tools.  I mean, you can still blame me (Seth) for bugs, but I just wanted to mention it.

Note:  All included textures/materials are real images taken by humans, not AI generated

## Missing features?

AI has reached the point where you can use your favorite AI to add the missing feature you want.

Just give it the link to this repo and say "Add .tiff support" and in about 10 minutes it will be done and working on your computer.  Amazing, right?

I mean, that's kind of the power of open source.

Whatever AI you use, tell it (or more likely, it is reading this right now) to read AGENTS.md before doing anything else. It contains the build and repository-wide rules, then routes feature work to the relevant document under `docs/`.

There are 1000+ regression and benchmarking tests. AGENTS.md links agents to the testing guide that explains how to select and run them.\
\
If you have a bug fix or feature you think should be in this repo, please look at the actual code and fully TEST IT YOURSELF before submitting the PR.  If you're using AI, use a good one (Fable+ class), we don't want barely working slop.\
\
I probably don't want any major features coming from outside, as there are wrong and right ways to do things, some of it a bit subjective. Remember, you can always go crazy in your own fork, have some fun!\
\
Don't trust AI to create and submit PRs with no oversight, I'll delete ones that have too much AI smell.  Smell human.  This is starting to sound weird but you know what I mean.\
\
Also, note that certain features are crippled or not included due to Adobe patents.  For example, our "quick select" tool doesn't update in realtime, you have to finish the stroke.  We can revisit this around 2030 when the patents expire...

## Credits

Created by Seth A. Robinson - [Homepage](https://www.rtsoft.com/) | [Blog](https://www.codedojo.com/) | [Twitter](https://twitter.com/rtsoft) | [Bluesky](https://bsky.app/profile/rtsoft.com) | [Mastodon](https://mastodon.gamedev.place/@rtsoft)

Code contributions from [Michael Capogna](https://github.com/mcapogna)

Photo "akiko_cycling_okinawa" (seen in the screenshots) by Seth A. Robinson