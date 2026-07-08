# Patchy

Open source free image editing.

Think classic Photoshop 5.x/6.x-style layer editing, modernized: PSD layers, masks, text, blend modes, layer styles, legacy plugins, and current formats like WebP, without subscriptions or telemetry.

## Patchy in action

[![Patchy in action video preview](docs/images/patchy-youtube-preview.png)](https://www.youtube.com/watch?v=DSbMqp2cXig)

## Screenshots

Click a thumbnail for the full-size image.

<table>
  <tr>
    <td align="center">
      <a href="docs/images/screenshots/levels.png"><img src="docs/images/screenshots/levels.png" width="270" alt="Levels adjustment over a photo with rulers and grid, and the color wheel picker choosing the DOS / VGA 256 swatch palette"></a>
      <br><sub>Photo retouching: Levels histogram with live preview, and the color picker's palette dropdown</sub>
    </td>
    <td align="center">
      <a href="docs/images/screenshots/layer_styles.png"><img src="docs/images/screenshots/layer_styles.png" width="270" alt="Layer Style dialog with gradient overlay editor and the color picker opened from a gradient stop"></a>
      <br><sub>Photoshop-style layer styles: two-track gradient editor, stops open the color picker</sub>
    </td>
    <td align="center">
      <a href="docs/images/screenshots/brush_tips.png"><img src="docs/images/screenshots/brush_tips.png" width="270" alt="Brush tip picker with built-in brush tips"></a>
      <br><sub>36 built-in brush tips, plus Photoshop .abr import</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="docs/images/screenshots/brush_dynamics.png"><img src="docs/images/screenshots/brush_dynamics.png" width="270" alt="Brush dynamics panel and scattered strokes"></a>
      <br><sub>Brush dynamics: jitter, scattering and pen controls</sub>
    </td>
    <td align="center">
      <a href="docs/images/screenshots/palette_mode.png"><img src="docs/images/screenshots/palette_mode.png" width="270" alt="Convert to Indexed dialog with retro palette presets over a photo constrained to the Commodore 64 palette, and the color picker pinned to the document palette"></a>
      <br><sub>Palettized (indexed color) editing with built-in retro palettes, from NES to Commodore 64</sub>
    </td>
    <td align="center">
      <a href="docs/images/screenshots/hue_saturation.png"><img src="docs/images/screenshots/hue_saturation.png" width="270" alt="Hue/Saturation adjustment with live preview"></a>
      <br><sub>Adjustments with live canvas preview</sub>
    </td>
  </tr>
</table>

## Download

Windows releases are code signed by Seth A. Robinson; the macOS app is signed and
notarized (Robinson Technologies Corporation).

| Platform                  | Package                       | Download                                                                                        |
| ------------------------- | ----------------------------- | ----------------------------------------------------------------------------------------------- |
| Windows 10/11 (64-bit)    | Installer                     | [PatchyWindowsInstaller.exe](https://rtsoft.com/files/PatchyWindowsInstaller.exe) (16 MB)       |
| Windows 10/11 (64-bit)    | Portable ZIP (no installer)   | [PatchyWindowsNoInstaller.zip](https://rtsoft.com/files/PatchyWindowsNoInstaller.zip) (15 MB)   |
| macOS 12+ (Apple Silicon) | DMG — drag to Applications    | [PatchyMacOS.dmg](https://rtsoft.com/files/PatchyMacOS.dmg) (27 MB)                             |
| Linux                     | Flatpak bundle                | [PatchyLinux.flatpak](https://rtsoft.com/files/PatchyLinux.flatpak) (2 MB)                      |

Linux one-line install (paste into a terminal; fetches the bundle and installs it,
pulling the shared KDE runtime from Flathub automatically):

```sh
curl -L -o /tmp/PatchyLinux.flatpak https://rtsoft.com/files/PatchyLinux.flatpak && flatpak install -y /tmp/PatchyLinux.flatpak
```

## Features

- Open and save layered PSD files with groups, masks, text objects, the full Photoshop blend mode set, layer styles and more
- Common raster editing tools (brush, eraser, selection, transform, etc.)
- Palettized (indexed color) editing mode for pixel art: constrain painting to a palette, quantize with optional dithering, built-in retro palettes (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), and indexed PNG-8 export. Layers, layer styles, and effects all keep working in indexed mode (Photoshop's indexed mode flattens and disables them)
- Palettized saving of low-color bitmaps (2/4/8 bit)
- Pixel-art and game-dev extras: seamless tile preview window, sprite sheet export/import, and nearest-neighbor scaled export (2x-8x)
- Cross-platform: Windows is the lead platform, with native macOS (Apple Silicon) and Linux (Flatpak) builds
- Rich text allowing color, font, size, and style changes within a single text layer
- Reads and writes a wide range of formats: PSD/PSB, PNG, JPEG, TIFF, WebP, BMP, TGA, GIF, PCX, Amiga IFF/LBM, Windows icons and cursors (ICO/CUR), and Aseprite files
- Supports dynamical sensitivity/size for pen/stylus, printing options, GUI scaling, scanner/camera import (Windows), legacy .8bf plugins, command line options
- Built with C++ and Qt for performance and a native desktop experience.  No GPU used, should run on a potato.
- Privacy: YES! Absolutely no telemetry, no tracking, no data collection. (If update checks are enabled, it contacts GitHub only to check for a newer version) 
- App settings are stored locally in a plain file (AppData on Windows, `~/Library/Preferences` on macOS, `~/.config` on Linux)
- Localized in English and Japanese (can change language in File->Preferences)
- Installer just installs, it doesn't screw with your file extension preferences

## What's New

### 0.15 — July 8, 2026

- New file formats, all reading and writing: Windows icons and cursors (ICO/CUR, every embedded size opens as a layer), Targa (TGA), GIF, Aseprite (.aseprite/.ase, layers with blend modes and opacity round-trip), PCX, and Amiga IFF/ILBM
- Sprite sheet workflow for game development: File > Export Layers as Sprite Sheet renders each visible top-level layer into a padded grid, and File > Import > Sprite Sheet to Layers slices a sheet back into layers
- View > Seamless Tile Preview: a live tiled preview window for authoring seamless textures, with drag panning and a resizable, remembered window
- Import images directly from a scanner or camera (File > Import, Windows)
- Export at 2x-8x nearest-neighbor scale for crisp pixel-art upscaling
- Six new blend modes — Linear Dodge (Add), Subtract, Divide, Exclusion, Hue, and Color — completing the Photoshop set; Hue/Saturation/Color/Luminosity now use the exact math Photoshop and Aseprite share
- Open Recent Folder now remembers up to 200 folders in paginated submenus

### 0.14 — July 8, 2026

- The color picker's swatch column is now palette-driven: a dropdown switches between Basic colors, the current document palette, a loaded palette file, and built-in presets, and you can load and save palette files (.pal/.gpl/.hex/.act/.aco/.ase) right from the picker
- Drag and drop colors between the palette grid, the current-color preview, and custom slots, with focus-aware Edit > Cut/Copy/Paste; edit the current and loaded-file palettes in place (built-in presets stay read-only) and set a custom slot with one button
- New built-in DOS / VGA 256 and Dink Smallwood palettes, and the Palette panel can copy hex codes and push a clicked swatch straight into an open color picker
- Convert to Indexed's preview now zooms and pans at full resolution and shows a progress bar while applying, and Image > Mode > RGB Color can keep the palettized look by snapping off-palette layers
- Large documents are dramatically faster to edit: dirty-rectangle undo/redo, parallel compositing, and smarter caches make the standard large-document stress test roughly seven times faster, and big canvases keep the previous frame during recomposites and undo instead of flashing a checkerboard
- Drop Shadow Spread and Inner Shadow/Glow Choke now expand the shape geometrically before blurring, matching Photoshop (high Spread/Choke no longer produces square chunks or fringes)
- macOS dialogs now match the Windows layout (compact dynamics popup, growing form fields, classic scrollbars, dark tab bars) and no longer drop behind the main window or overlap label text with checkboxes
- Crop to Selection now recenters the cropped image in the viewport

### 0.13 — July 6, 2026

- Patchy is now cross-platform: a native macOS build (Apple Silicon, signed and notarized DMG) and a Linux build (Flatpak bundle), with the same features and byte-identical file formats on all three platforms
- New palettized (indexed color) editing mode for pixel art and retro game development: Image > Mode > Indexed constrains painting to a palette with a WYSIWYG canvas, and a Palette panel offers built-in retro presets (NES, C64, Game Boy, PICO-8, and more), palette files (.pal/.gpl/.hex/.act/.aco/.ase), drag-to-swap entries, and swatch copy/paste
- Convert existing art to a palette with optional dithering, snap stray pixels back with Image > Snap to Palette, round-trip the palette through PSD files, and export exact indexed PNG-8 and 2/4/8-bit BMP
- Unlike Photoshop's indexed mode, palettized editing doesn't flatten or lock anything: layers, layer styles, effects, text, and filters keep working, previewed through the palette live
- macOS feels native: global menu bar with standard About/Settings placement (Cmd+,), Photoshop-style Cmd shortcuts, delete key clears layers, trackpad pinch-to-zoom with two-finger panning, Finder double-click opens into the running app, titlebar document proxy and dirty-dot conventions
- Linux integrates properly: portal file dialogs, desktop entry with file associations, Wayland and X11 support, and update notices that offer a one-line copy-paste install command
- Delete now pops magnetic-lasso anchors mid-trace (matching Backspace) instead of clearing the layer, on every platform
- Fixed a crash when closing the window while an inline text edit was still open

### 0.12 — July 5, 2026

- New Magnetic Lasso sub-tool traces object edges live, supports manual correction anchors, Backspace/Enter/double-click editing, and anti-aliased selection commits
- Stroke layer styles now match Photoshop's inside/center/outside edge bands more closely, including correct center width and a new Stroke blend mode control
- Brush dynamics gained per-setting control sources like Fade, Pen Pressure, Pen Tilt, Pen Rotation, and Stylus Wheel, with broader `.abr` import support
- Soft bitmap brush tips now blend overlapping stamps without light seams, improving pattern brushes and self-crossing strokes ([@mcapogna](https://github.com/mcapogna))
- Rectangle and ellipse tools gained Fixed Ratio, Fixed Size, Alt draw-from-center, and live size readouts while dragging
- Brush-size hotkeys now scale proportionally with the current brush size, so large brushes resize quickly while small brushes keep fine control
- Eyedropper and color-picker workflows are smoother, with a shared eyedropper cursor, better picker window behavior, and live color updates
- Fixed Windows snap/maximize edge cases ([@mcapogna](https://github.com/mcapogna)) and shape-option syncing across new or switched documents

### 0.11 — July 4, 2026

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

### 0.10 — June 29, 2026

- Zoom tool improvements: clearer zoom-in/zoom-out cursor badges, point zooming from the grey canvas area, and edge-clamped marquee zoom ([@mcapogna](https://github.com/mcapogna))
- Gradient tool improvements: gradient fills preview live while dragging, and the gradient stop editor is easier to edit and adjust ([@mcapogna](https://github.com/mcapogna))
- Toolbar sliders now drag smoothly and jump directly to the clicked spot instead of stepping there ([@mcapogna](https://github.com/mcapogna))
- Eyedropper can sample colors by dragging from Patchy onto the screen ([@mcapogna](https://github.com/mcapogna))
- Marquee and lasso selections have better undo/redo, mode handling, and previews ([@mcapogna](https://github.com/mcapogna)); selection history now has Japanese translations
- Fixed frameless window border artifacts and maximize regressions

### 0.9 — June 22, 2026

- Merge Down now flattens folders and any multi-selection, discarding hidden layers (matches Photoshop)
- Single-instance: double-clicking a file in Explorer opens it in the existing window instead of launching a new copy
- 32-bit BMPs (and other flat images) import their per-pixel alpha as an editable layer mask
- Selection tools: drag the outline to move it, arrow-key nudge, click-to-deselect, grey-area selection, lasso improvements, and combine-mode cursor badges ([@mcapogna](https://github.com/mcapogna))
- Shape tools: antialiased soft/thick outlines and fills, rounded-corner rectangles, Shift for 1:1, and dedicated Fill opacity/softness
- Open dialog remembers the last folder; new Open Recent Folder menu with copy-path and open-in-explorer actions
- Fixed Ctrl+T transform nudge so arrow keys move the bounding box with the pixels
- Splash screen dismisses faster
- Improved color picker with new sliders and wheels

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
build-release.bat
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

- Vector/path workflows, including pen paths, editable shape layers, vector masks, and editable stroke/fill appearance
- Smart Objects, linked assets, Smart Filters, and broad non-destructive filter stacks
- Full Photoshop adjustment-layer compatibility beyond Patchy's current adjustment support
- CMYK/Lab editing and export, spot channels, extra alpha-channel workflows, 16/32-bit editing, HDR/EXR, and full color-management parity (patchy will convert CMYK/Lab to RGB on open, but doesn't support editing or saving in those color modes)
- Layer comps, timeline/video/animation workflows, Camera Raw, Liquify/warp, content-aware tools, and generative tools
- Actions, batch processing, scripting, UXP/JSX panels, and other automation workflows
- High-fidelity PSD/PSB edge cases, including layered PSB writing and byte-perfect preservation of every Photoshop-only metadata block
- Not tested much yet; expect bugs
- Patchy is much slower than Photoshop, especially on large documents and it doesn't support GPU stuff

## License

Patchy is released under the MIT License. Third-party runtime notices are tracked in `NOTICE-THIRD-PARTY.md`.

## Trademark Notice

Patchy is not affiliated with, endorsed by, or sponsored by Adobe. Adobe Photoshop is a trademark of Adobe in the United States and/or other countries.

## AI Disclosure

This project was developed with significant assistance from AI tools.  I mean, you can still blame me (Seth) for bugs, but I just wanted to mention it.

## Missing features?

AI has reached the point where you can use Codex, Claude to add the missing feature you want.

Just give it the link to this repo and say "Add .tiff support" and in about 10 minutes it will be done and working on your computer.  Amazing, right?

I mean, that's kind of the power of open source.

Whatever AI you use, tell it (or more likely, it's reading this right now) to read AGENTS.md first at all times before doing anything else - that's where the build commands, conventions, and project knowledge live. This will help your AI to properly test things itself.

If you have a bug fix or feature you think should be in this repo, please look at the actual code and fully TEST IT YOURSELF before submitting the PR.  If you're using AI, use a good one, we don't want barely working slop.

If you decide to try to add linux/mac support, that's good but be sure to test the Windows version for regression too.

Don't trust AI to create and submit PRs with no oversight, I'll delete ones that have too much AI smell.  Smell human.  This is starting to sound weird but you know what I mean.

## Credits

Created by Seth A. Robinson - [Homepage](https://www.rtsoft.com/) | [Blog](https://www.codedojo.com/) | [Twitter](https://twitter.com/rtsoft) | [Bluesky](https://bsky.app/profile/rtsoft.com) | [Mastodon](https://mastodon.gamedev.place/@rtsoft)

Photo "akiko_cycling_okinawa" (seen in the screenshots) by Seth A. Robinson
