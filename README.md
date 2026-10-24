<img src="packaging/branding/patchy-logo-folded.svg" width="96" height="96" alt="Patchy logo">

# Patchy Image Editor

A free, open-source image editor for Windows, macOS, Linux, and the browser.
Built with a focus on accurate PSD compatibility, keeping text, vectors, masks,
layer styles, and Smart Objects editable when working with layered Photoshop files.
Work in limited palettes without flattening your layers, import editable Affinity
and PDF artwork, and extend the editor with classic plug-ins and JavaScript.

**[Download](#download)** · **[Try in your browser](https://www.patchyimageeditor.com)** · **[Features](#features)** · **[Full gallery](docs/screenshots.md)**

For bug reports and feature requests, please [open an issue](https://github.com/SethRobinson/Patchy/issues/new) or post in [Discussions](https://github.com/SethRobinson/Patchy/discussions). These are the best places to post because search engines can index them, helping others find the questions and answers.  Want to chat? Join [Seth's Discord](https://discord.gg/QwV5VaZ).

<a href="docs/images/screenshots/smart_filters.png"><img src="docs/images/screenshots/smart_filters.png" width="1000" alt="Cyclist photo with color-wheel and Levels controls, an editable Levels adjustment layer, a Smart Filter stack and shared mask, grouped text, and a clipping mask"></a>

*Designed to feel familiar if you're used to Photoshop's workflows and keyboard shortcuts.*

## Download

**Latest release: 1.17** · October 24, 2026 · [Release notes](#whats-new) · [All releases](https://github.com/SethRobinson/Patchy/releases)

Windows releases are code signed by Seth A. Robinson; the macOS app is signed and
notarized (Robinson Technologies Corporation); the Linux Flatpak repository is GPG
signed. Every release is published on the
[GitHub Releases page](https://github.com/SethRobinson/Patchy/releases) with SHA-256
checksums.

| Platform                  | Package                        | Download                                                                                                                             |
| ------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------ |
| Windows 10/11 (64-bit)    | Installer                      | [PatchyWindowsInstaller.exe](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyWindowsInstaller.exe) (100 MB)     |
| Windows 10/11 (64-bit)    | ZIP file (no installer)    | [PatchyWindowsNoInstaller.zip](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyWindowsNoInstaller.zip) (100 MB) |
| macOS 12+ (Apple Silicon) | DMG - drag to Applications     | [PatchyMacOS.dmg](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyMacOS.dmg) (111 MB)                           |
| Linux (x86_64)            | Flatpak, from our repository   | The [command below](#linux), or [com.rtsoft.patchy.flatpakref](https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref)            |
| Linux (x86_64)            | Flatpak bundle (single file)   | [PatchyLinux.flatpak](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.flatpak) (68 MB)                   |
| Linux (x86_64)            | AppImage (Ubuntu 22.04+)       | [PatchyLinux.AppImage](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.AppImage) (119 MB, [details](#appimage)) |
| Any modern browser        | Nothing to install             | [patchyimageeditor.com](https://www.patchyimageeditor.com) or [rtsoft.com/patchy](https://www.rtsoft.com/patchy/)                    |

### Linux

Paste this into a terminal. It installs Patchy for your user from the Patchy Flatpak
repository and pulls the shared KDE runtime from Flathub. No root needed.

```sh
flatpak install --user https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref
```

Patchy then updates with `flatpak update` or through your software center, and starts
from your application menu or with `flatpak run com.rtsoft.patchy`.

This needs the `flatpak` tool. Fedora and openSUSE desktops usually have it already. If
needed, install it first with `sudo apt install flatpak` (Ubuntu, Debian),
`sudo dnf install flatpak` (Fedora) or `sudo zypper install flatpak` (openSUSE).

If you already use flatpak system-wide, you can use `--system` in place of `--user`.
Patchy then reuses the runtime extensions you already have (graphics drivers, codecs)
instead of downloading them again. Installing that way may ask for an admin password.

The `.flatpakref` link in the table downloads a small file that a Flatpak-aware
software center (GNOME Software, KDE Discover) can open to install Patchy. Ubuntu has
none by default, and on Ubuntu 24.04 GNOME Software's Open button fails to start
Flatpak apps, so use the terminal command above instead.

The single-file bundle is the same build, for when you would rather download a file
first. Install it with `flatpak install --user PatchyLinux.flatpak`. It still fetches
the KDE runtime from Flathub, and it updates from the same repository afterwards.

My flatpak knowledge is limited, so I hope all this makes sense.  I hope to submit Patchy to Flathub eventually, but for now we have our own repository.

#### AppImage

`PatchyLinux.AppImage` is one file that runs without installing anything, on Ubuntu
22.04 or newer (or another distro from 2022 on: Debian 12, Fedora 36, Mint 21 and so on).
It updates itself, so keep it somewhere you can write to, such as `~/Applications`:

```sh
mkdir -p ~/Applications && cd ~/Applications
wget -O PatchyLinux.AppImage https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.AppImage
chmod +x PatchyLinux.AppImage
./PatchyLinux.AppImage
```

(`curl -LO` works instead of `wget -O PatchyLinux.AppImage`.) You can also make it
executable from your file manager (Properties > Permissions, "Allow executing file as
program") and double-click it.

The AppImage needs the FUSE 3 tools and libraries (not `libfuse2`), which most desktops
already have. If it says it cannot mount itself, install it:

| Distro | Command |
| --- | --- |
| Ubuntu, Mint, Debian, Pop!_OS | `sudo apt install fuse3` |
| Fedora | `sudo dnf install fuse3` |
| openSUSE | `sudo zypper install fuse3` |
| Arch, Manjaro | `sudo pacman -S fuse3` |

Without FUSE (some containers and minimal systems), run it with
`./PatchyLinux.AppImage --appimage-extract-and-run`.

To add Patchy to your application menu, use an AppImage integration tool:
[Gear Lever](https://flathub.org/apps/it.mijorus.gearlever) (a Flatpak app: open the
AppImage with it), [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher)
(offers to integrate an AppImage the first time you open it),
[AM](https://github.com/ivan-hc/AM) (command line: `am --launcher ./PatchyLinux.AppImage`),
or [AppImage Manager](https://github.com/tunjayoff/appimagemanager).

When a new version is out, Patchy's update prompt can download it, replace the AppImage
file and start again (if the file sits in a folder you can write to; otherwise it offers
the download). AppImage tools such as Gear Lever or AppImageUpdate can also update it.
For AI control, the connector command is the AppImage file with the argument
`patchy-mcp` (Help > Set up AI Control shows the exact line). The AppImage opens HEIC
images through your distribution's libheif (Ubuntu and Debian: the `libheif1` package,
plus `libheif-plugin-libde265` on Ubuntu 24.04 and later); the Flatpak brings its own.

## Screenshots

See it in action.  Click an image for the full-size capture.

<table>
  <tr>
    <td valign="top" width="50%"><a href="docs/images/screenshots/layer_styles.png"><img src="docs/images/screenshots/layer_styles.png" width="460" alt="Layer Style and gradient color controls over layered artwork."></a><br><strong>Build up layer effects</strong><br>Layer styles with multiple effects, blending controls, and Photoshop-compatible presets.</td>
    <td valign="top" width="50%"><a href="docs/images/screenshots/plugin_kpt5.png"><img src="docs/images/screenshots/plugin_kpt5.png" width="460" alt="Kai's Power Tools 5 running in its own window over Patchy. Classic 32-bit and 64-bit 8BF filters work on Windows."></a><br><strong>Bring your old plug-ins</strong><br>Kai's Power Tools 5 running in its own window over Patchy. Classic 32-bit and 64-bit 8BF filters work on Windows.</td>
  </tr>
  <tr>
    <td valign="top" width="50%"><a href="docs/images/screenshots/object_select.png"><img src="docs/images/screenshots/object_select.png" width="460" alt="Object Selection tool with a vintage car selected in a busy Havana street from one loose box drag, the rest of the photo muted."></a><br><strong>Select the object, not the box</strong><br>Drag a loose box around anything and Object Selection picks out just the object. Runs on your CPU.</td>
    <td valign="top" width="50%"><a href="docs/images/screenshots/vector_tools.png"><img src="docs/images/screenshots/vector_tools.png" width="460" alt="Editable paths, gradient and pattern paint, dashed strokes, rounded corners, and a Paths panel."></a><br><strong>Edit the paths</strong><br>Editable paths, gradient and pattern paint, dashed strokes, rounded corners, and a Paths panel.</td>
  </tr>
  <tr>
    <td valign="top" width="50%"><a href="docs/images/screenshots/warp_text.png"><img src="docs/images/screenshots/warp_text.png" width="460" alt="Editable warped text, a paragraph with inline bold, italic, color and mixed fonts, tracked lettering, and vertical Japanese columns."></a><br><strong>Give type its own voice</strong><br>Warped text, mixed fonts and styles in one paragraph, letter spacing, and vertical Japanese.</td>
    <td valign="top" width="50%"><a href="docs/images/screenshots/tilt_shift.png"><img src="docs/images/screenshots/tilt_shift.png" width="460" alt="Tilt-Shift Blur with on-image controls and live preview in the Filter Gallery."></a><br><strong>Shape the focus</strong><br>Comes with many filters and a filter gallery, like Tilt-Shift Blur with live previews.</td>
  </tr>
  <tr>
    <td valign="top" width="50%"><a href="docs/images/screenshots/artboards.png"><img src="docs/images/screenshots/artboards.png" width="460" alt="One document holding four artboards for a car-weekend campaign: a white story, a black banner, a cream post selected with the Artboard tool, and a transparent icon, with the Properties panel showing the post's position, size and background."></a><br><strong>Every size in one document</strong><br>Artboards keep a story, banner, post and icon side by side, each with its own size and background. Export them to separate files or PDF pages.</td>
    <td valign="top" width="50%"><a href="docs/images/screenshots/gmic.png"><img src="docs/images/screenshots/gmic.png" width="460" alt="The G'MIC Filter Library with Rodilius selected among the Artistic filters, its settings beside the list, and a live preview on the right half of a Havana street photo, with the original on the left."></a><br><strong>Nearly 400 G'MIC filters</strong><br>Browse the G'MIC library, adjust the settings, and watch the result render live on your canvas. Here Rodilius runs inside a selection, with the original photo on the left.</td>
  </tr>
</table>

[See the full gallery](docs/screenshots.md) for painting, palette mode, seamless textures, Camera Raw, long shadows, scripting, and more.

## Features

| Workflow | What you get |
| --- | --- |
| **Layered Photoshop files** | PSD and PSB, editable text, groups, masks, clipping, blend modes, and layer styles. |
| **Pixel art and game assets** | Paint in a fixed palette while keeping layers, text, and effects editable. Use retro or custom palettes, named colors, and indexed PNG/BMP export, alongside seamless tiling, sprite sheets, image sequences, DDS textures, and animated GIF/WebP/PNG. |
| **Affinity documents** | Import .af, .afphoto, .afdesign, and .afpub files with editable text, vectors, masks, supported adjustments, and layer effects, then continue editing or save as PSD. [Support and limitations](docs/features.md#affinity-import). |
| **PDF documents** | Import pages as editable text, vectors, and images on desktop; export single or multi-page PDFs with editable or flattened content. |
| **Non-destructive editing** | Adjustment layers, embedded and linked Smart Objects, editable Smart Filters, and shared filter masks. |
| **Paint and retouch** | Pressure-aware brushes, Mixer Brush, stroke smoothing, healing, cloning, Patch, Remove Object, Liquify, and selections, including Object Selection that picks out an object from a dragged box. |
| **Text and vectors** | Rich and vertical text, paragraph controls, Warp Text, Pen paths, shape layers, vector masks, SVG, and image tracing. |
| **Photos and print** | 16-bit and 32-bit (HDR) editing, Camera Raw development, HEIC/HEIF, AVIF and JPEG XL, CMYK proofing and CMYK JPEG, TIFF, and PDF export for print, and common image formats. |
| **Extend your workflow** | The G'MIC filter library (nearly 400 filters with live preview, desktop), Last Filter to repeat any filter, classic 32-bit and 64-bit Photoshop filter plug-ins on Windows, a built-in JavaScript editor, batch processing, command-line tools, and local MCP control. |

Patchy's C++ foundation makes these integrations more straightforward: we can build
on established C and C++ libraries such as G'MIC and implement classic Photoshop
plug-in interfaces directly.

[Full feature list and format support](docs/features.md) · [Scripting guide](scripts/bundled/scripting-guide.md) · [AI control setup](docs/ai-control.md)

**Local by design.** No telemetry, tracking, or uploads of your images. The browser build
runs the same editor locally through WebAssembly. Desktop builds have more memory
available and add printing, scanner/camera import, and command-line automation.
Optional update checks contact GitHub, as do the one-click updates (Windows and the Linux AppImage) and Object Selection's High Quality model, each downloaded only if you ask for it. Twelve interface languages, dark and light
schemes, and importable themes are included.

## PSD compatibility, measured

In the [October 12, 2026 Testy v2 run](https://www.rtsoft.com/testy/2026-10-12/),
Patchy opened **all 309 files** of the psd-tools test collection, and Photoshop
reopened **every Patchy save** with **all 43 text objects, 183 adjustment layers,
172 Smart Objects, and 181 live effects** kept. Patchy's perceptual render match
was **97.2%**, the highest of the eight programs tested against Photoshop's
reference, using Patchy 1.10 (commit `d00225bb`).

Testy v2 scores each program on what it draws itself: the baked pixels Photoshop
stores for text, shapes, fills, and Smart Objects are removed first. That is
stricter than the August run, so the two sets of numbers are not comparable.

These are dated, corpus-specific results. The linked report has every file's
renders and difference maps for all nine columns. Read the
[full comparison and methodology](docs/psd-compatibility-benchmark.md) for the
tables, per-folder results, scoring rules, and limitations.

**Know the limits:** editing is RGB at 8, 16, or 32 bits per channel (the browser
build stops at 16). CMYK work is proofing and export: CMYK Proof mode shows the print
colors and saves CMYK JPEG, TIFF, and PDF, but pixels stay RGB. Lab and the other color
modes convert to RGB on open, and there is no GPU acceleration. Unsupported Smart Filters can remain preview-locked,
and Affinity import has format-specific limitations. See [current compatibility](docs/features.md#current-status).

## What's New

### 1.17 - October 24, 2026

- Color and watermarks: images with another RGB profile, such as Adobe RGB or Display P3, convert to sRGB on open, and Edit > Convert to Color Profile converts on demand; File Info lists watermarks it finds (C2PA declarations, an on-demand Stable Diffusion pixel scan, and TrustMark through an optional model download), with Scan for Watermarks on the document tab menu; Image > Add Invisible Watermark and Preferences > Watermarks (watermark text, add on save, check opened images); a Saved metadata preference writes nothing, a basic save stamp, or the stamp plus your name and copyright
- PSD and files: a file with the wrong extension opens by its content, and Save then goes to Save As ([issue 362](https://github.com/SethRobinson/Patchy/issues/362)); Photoshop 2025 opens Patchy's type layers and grayscale PSDs ([issue 341](https://github.com/SethRobinson/Patchy/issues/341)); warped text opens in Photoshop without the update prompt; text sizes and font smoothing open as Photoshop set them ([issue 345](https://github.com/SethRobinson/Patchy/issues/345), [issue 344](https://github.com/SethRobinson/Patchy/issues/344)); Smart Filters from older Photoshop files render and their eyes toggle ([issue 348](https://github.com/SethRobinson/Patchy/issues/348)); the Smart Filters list collapses in the Layers panel, and PSD keeps the open state of layer styles and Smart Filters ([issue 361](https://github.com/SethRobinson/Patchy/issues/361))
- Tools: the Perspective Crop tool in the Crop flyout ([issue 328](https://github.com/SethRobinson/Patchy/issues/328)); Image > Trim crops to content by transparency or a corner color ([issue 297](https://github.com/SethRobinson/Patchy/issues/297)); tools can be picked with no document open; foreground and background colors carry across documents and restarts ([issue 339](https://github.com/SethRobinson/Patchy/issues/339)); big round brushes paint faster ([issue 135](https://github.com/SethRobinson/Patchy/issues/135)); marquee drags stay fast after an anti-aliased Lasso selection ([issue 340](https://github.com/SethRobinson/Patchy/issues/340)); the Move bar is regrouped ([issue 350](https://github.com/SethRobinson/Patchy/issues/350)), the transform bar's W and H follow Distort and Perspective ([issue 353](https://github.com/SethRobinson/Patchy/issues/353)), and handle drags no longer leave a trail of the size readout ([issue 352](https://github.com/SethRobinson/Patchy/issues/352))
- Interface: Interface scale takes any whole percent from 50 to 300 ([issue 333](https://github.com/SethRobinson/Patchy/issues/333)), with a Toolbar icon size preference beside it; bolder tool icons in a uniform box ([issue 338](https://github.com/SethRobinson/Patchy/issues/338)); options-bar menu buttons show a gear ([issue 326](https://github.com/SethRobinson/Patchy/issues/326)); the status bar shows the selection's X, Y, W and H ([issue 355](https://github.com/SethRobinson/Patchy/issues/355)); New Document has five card columns with Recent in the template list ([issue 342](https://github.com/SethRobinson/Patchy/issues/342)); the Text bar gains separators and the shape bar's color swatch ([issue 343](https://github.com/SethRobinson/Patchy/issues/343)); in the Layers panel, footer buttons outline only on hover ([issue 354](https://github.com/SethRobinson/Patchy/issues/354)), Smart Filter eyes line up with the layer style eyes ([issue 349](https://github.com/SethRobinson/Patchy/issues/349)) and their tooltips are solid again ([issue 347](https://github.com/SethRobinson/Patchy/issues/347)); a click on a collapsed panel title expands it and a double click collapses it ([issue 335](https://github.com/SethRobinson/Patchy/issues/335)); right-clicking a flyout tool no longer also opens the toolbar menu ([issue 356](https://github.com/SethRobinson/Patchy/issues/356)); Canvas Size drop-downs show their arrows ([issue 358](https://github.com/SethRobinson/Patchy/issues/358)); quit and close ask again after a cancelled Save As ([issue 359](https://github.com/SethRobinson/Patchy/issues/359)); the Smart Objects menus follow the active layer
- Linux: one app binary, with `patchy --mcp-connector` replacing `patchy-mcp` ([issue 336](https://github.com/SethRobinson/Patchy/issues/336)); the README's Flatpak install commands ask before downloading ([issue 205](https://github.com/SethRobinson/Patchy/issues/205))

### 1.16 - October 22, 2026

- Shapes: the Stroke checkbox is gone, a width of 0 means no stroke ([issue 311](https://github.com/SethRobinson/Patchy/issues/311)); Ctrl is a temporary Path Select and Shift at the press adds to the shape layer ([issue 312](https://github.com/SethRobinson/Patchy/issues/312), [issue 313](https://github.com/SethRobinson/Patchy/issues/313)); dragging a gradient-filled shape stays smooth ([issue 314](https://github.com/SethRobinson/Patchy/issues/314)), and deleting a layer's last shape deletes the layer ([issue 315](https://github.com/SethRobinson/Patchy/issues/315)); the Create Rectangle, Ellipse, Polygon and Custom Shape dialogs are tidier, with one linked corner Radius ([issue 309](https://github.com/SethRobinson/Patchy/issues/309), [issue 310](https://github.com/SethRobinson/Patchy/issues/310)); Shift, Alt and the W/H link keep a custom shape in proportion while it is drawn; unit fields look like the other number fields ([issue 327](https://github.com/SethRobinson/Patchy/issues/327), [issue 329](https://github.com/SethRobinson/Patchy/issues/329)), and the Properties panel hides Edit Appearance when nothing applies ([issue 273](https://github.com/SethRobinson/Patchy/issues/273))
- Crop: Overlay choices with turn and show-while-dragging ([issue 325](https://github.com/SethRobinson/Patchy/issues/325)), a Crop Options popup with Show Cropped Area and the shield's color and opacity ([issue 326](https://github.com/SethRobinson/Patchy/issues/326)), a Size link that keeps proportions ([issue 332](https://github.com/SethRobinson/Patchy/issues/332)), a bar grouped with separators ([issue 324](https://github.com/SethRobinson/Patchy/issues/324)), and Apply and Cancel no longer stay colored after a cancel ([issue 318](https://github.com/SethRobinson/Patchy/issues/318))
- Pick: Current & Below and No Adjustments sample choices ([issue 321](https://github.com/SethRobinson/Patchy/issues/321)), the sample square outlined at the pointer ([issue 319](https://github.com/SethRobinson/Patchy/issues/319)), a sample loupe while picking with a Show Sample Ring option ([issue 320](https://github.com/SethRobinson/Patchy/issues/320)), and Foreground/Background lead the bar ([issue 322](https://github.com/SethRobinson/Patchy/issues/322))
- Liquify and Warp: a faster Liquify preview and Apply, Rate for held brushes and stylus pressure, Show Mesh, a Preview toggle, Reconstruct by amount, Undo and Redo inside the dialog, and tool letters with [ and ] for brush size ([issue 296](https://github.com/SethRobinson/Patchy/issues/296)); Warp drags render once per frame and stay responsive on large files ([issue 298](https://github.com/SethRobinson/Patchy/issues/298))
- Transform and navigation: Edit > Transform holds Scale, Rotate and Warp, with transform type buttons on the Move and transform bars ([issue 299](https://github.com/SethRobinson/Patchy/issues/299)); a shorter Transform Controls label ([@philipzae](https://github.com/philipzae)); a Zoom tool right-click menu and View > 200% ([issue 300](https://github.com/SethRobinson/Patchy/issues/300)); a Hand tool options bar with 100%, Fit Screen and Fill Screen ([issue 303](https://github.com/SethRobinson/Patchy/issues/303)), and Ctrl+click and Alt+click zoom with the Hand tool ([issue 304](https://github.com/SethRobinson/Patchy/issues/304))
- Selection, layers and artboards: the Polygonal Lasso tool, and Alt for straight segments with the Lasso ([issue 287](https://github.com/SethRobinson/Patchy/issues/287)); the Elliptical Marquee keeps its shape past the canvas edge ([issue 307](https://github.com/SethRobinson/Patchy/issues/307)); Quick Select swaps its cursor as soon as Alt or Shift is pressed ([issue 289](https://github.com/SethRobinson/Patchy/issues/289)); Merge Visible and a New Layer dialog ([issue 294](https://github.com/SethRobinson/Patchy/issues/294), [issue 275](https://github.com/SethRobinson/Patchy/issues/275)); Background layer rules and a color label menu on the eye ([issue 274](https://github.com/SethRobinson/Patchy/issues/274), [issue 276](https://github.com/SethRobinson/Patchy/issues/276)); Delete removes an artboard, a blank click deselects, no document outline, and size presets with search ([issue 277](https://github.com/SethRobinson/Patchy/issues/277), [issue 278](https://github.com/SethRobinson/Patchy/issues/278), [issue 279](https://github.com/SethRobinson/Patchy/issues/279), [issue 281](https://github.com/SethRobinson/Patchy/issues/281), [issue 285](https://github.com/SethRobinson/Patchy/issues/285))
- Files and color: HDR AVIF (PQ and HLG), high-bit-depth HEIF and Windows JPEG XR float captures open at full depth; 16 and 32-bit documents keep their colors in Free Transform and Move previews and copy display colors to the clipboard; PSD saves update the EXIF Software and DateTime and the XMP instance ID ([issue 219](https://github.com/SethRobinson/Patchy/issues/219)); type layers without pixels open at Photoshop's position, and a PSD without resolution opens at 72 PPI ([issue 272](https://github.com/SethRobinson/Patchy/issues/272)); a styled pass-through group's stroke matches Photoshop at soft edges ([issue 265](https://github.com/SethRobinson/Patchy/issues/265))
- Interface: options bars follow one set of grouping and separator rules, with one swap width/height glyph ([issue 284](https://github.com/SethRobinson/Patchy/issues/284), [issue 285](https://github.com/SethRobinson/Patchy/issues/285), [issue 286](https://github.com/SethRobinson/Patchy/issues/286), [issue 290](https://github.com/SethRobinson/Patchy/issues/290), [issue 291](https://github.com/SethRobinson/Patchy/issues/291), [issue 292](https://github.com/SethRobinson/Patchy/issues/292), [issue 311](https://github.com/SethRobinson/Patchy/issues/311)); the Shape flyout leads with Rectangle ([issue 308](https://github.com/SethRobinson/Patchy/issues/308)); Rename File on the document tab menu; 20 built-in Looks in the Filter Gallery; clicking a menu separator leaves the menu open ([issue 317](https://github.com/SethRobinson/Patchy/issues/317)); the cursor comes back after a popup closes ([issue 293](https://github.com/SethRobinson/Patchy/issues/293)); Dynamic Vector Preview works on artboards and during Free Transform of a shape; each layer style row's eye lines up under the Layer Styles eye ([issue 256](https://github.com/SethRobinson/Patchy/issues/256)); on macOS, switching to another app keeps text editing open ([issue 330](https://github.com/SethRobinson/Patchy/issues/330))
- Linux: picking a screen color through the Wayland portal works ([issue 128](https://github.com/SethRobinson/Patchy/issues/128)); the AppImage offers its menu entry in a bar inside the window, and its catalog screenshots are refreshed ([issue 208](https://github.com/SethRobinson/Patchy/issues/208))

[Older releases](RELEASE-HISTORY.md)

## Photoshop plug-ins (.8bf, Windows only)

Choose **Plugins > Open Plug-ins Folder**, copy your `.8bf` files and their supporting
files into it, then choose **Plugins > Rescan Plug-in Folders**. Both 32-bit and 64-bit
filters appear under **Plugins > Legacy Photoshop Plug-ins**.

Filters run on the active pixel layer, respect the selection, and apply as one undoable
step. A separate helper isolates plug-in crashes. Plug-ins still run with your user
permissions, so install only ones you trust. KPT and other formerly full-screen filters
get a bounded, movable window. [Plug-in details](docs/plugins.md).

## Building it yourself

See [build instructions](docs/building.md) for the core, Qt desktop app, macOS, Linux,
and Windows packaging. Contributors and automation tools should read [AGENTS.md](AGENTS.md) first.

## License

Patchy is released under the MIT License. Third-party runtime notices are tracked in `NOTICE-THIRD-PARTY.md`.

## Trademark Notice

Adobe, Illustrator, and Photoshop are either registered trademarks or trademarks of Adobe in the United States and/or other countries. Patchy is an independent project and is not authorized, endorsed, or sponsored by Adobe. References to Photoshop, its file formats (PSD, Smart Objects, Smart Filters) and its plug-in format (8BF) are only there to describe compatibility.

Affinity is a trademark of Canva and its subsidiary Serif (Europe) Ltd. Patchy is not affiliated with, authorized, endorsed, or sponsored by Canva or Serif; Affinity is named only to describe which files Patchy can import.

All other product and company names, including Aseprite, Kai's Power Tools, Windows, macOS, and Linux, are trademarks of their respective owners. They are used only to identify file formats, plug-ins, and platforms Patchy works with, and no endorsement is implied.

## AI Disclosure

This project was developed with significant assistance from AI tools.  I mean, you can still blame me (Seth) for bugs, but I just wanted to mention it.

Note:  All included textures/materials are real images taken by humans, not AI generated

## Missing features / contributions?

AI has reached the point where you can use your favorite AI agent to add the missing feature you want.

Just give it the link to this repo and tell it "read AGENTS.md and add feature X to this project".  It will read the code, read the tests, and add the feature.

I mean, that's kind the power of open source, amplified.

Whatever AI you use (the smarter the better), tell it (or more likely, it's reading this right now) to read AGENTS.md before doing anything else. It contains the build and repository-wide rules, then routes feature work to the relevant document under `docs/`.

There are 2800+ regression and benchmarking tests. AGENTS.md links agents to the testing guide that explains how to select and run them.  Due to the size of some of the test files I use they aren't all included, it will skip tests that require missing files.\
\
I probably don't want any major features coming from outside, as there are wrong and right ways to do things, some of it a bit subjective. Remember, you can always go crazy in your own fork, have some fun!\
\
If you have a bug fix or feature you think fits this project's scope please open an issue or tweet/etc at me.  If you want to submit a pull request, please look at the actual code and fully TEST IT YOURSELF before submitting, and if possible include screenshots of the actual changes so it's clear what you're doing.  If you're using AI, use a good one (Fable/Astra+ class), we don't want barely working slop.

Don't trust AI to create and submit PRs with no oversight, I'll delete ones that have too much AI smell.  Smell human.  This is starting to sound weird but you know what I mean.

## Independent development and compatibility

Patchy is an independently developed image editor. Our goal is to build something powerful, fast, free of unnecessary bloat, and ultimately a tool that fits what I'm doing, especially for game-dev use cases. Recreating Photoshop feature for feature is not the goal.

Our PSD compatibility work uses publicly available format documentation and observations of files and rendered output produced by a licensed copy of Photoshop. We have not decompiled or disassembled Photoshop. Patchy also implements the legacy 8BF plug-in interface for interoperability, using its own declarations without copying Adobe SDK code. This compatibility work helps people keep their documents editable and use existing plug-ins across applications.

We also take patents seriously. Some features are deliberately limited or omitted because of patent concerns. For example, Quick Select calculates the selection when you finish a stroke instead of updating it continuously as you paint. Our [development constraints](docs/legal-constraints.md) document these boundaries, which we revisit as relevant patents expire or our research changes.

## Credits

Created by Seth A. Robinson - [Homepage](https://www.rtsoft.com/) | [Blog](https://www.codedojo.com/) | [Twitter](https://twitter.com/rtsoft) | [Bluesky](https://bsky.app/profile/rtsoft.com) | [Mastodon](https://mastodon.gamedev.place/@rtsoft)

Incredible people who donated suggestions, bug reports, and code: [mcapogna](https://github.com/mcapogna), [csbun](https://github.com/csbun), [ifloppy](https://github.com/ifloppy), [lucastucious](https://github.com/lucastucious), [c-sanchez](https://github.com/c-sanchez), [egofree71](https://github.com/egofree71), [PorkingMane](https://github.com/PorkingMane), [alexanderadam](https://github.com/alexanderadam), [danielmigueltejedor](https://github.com/danielmigueltejedor), [ProShi](https://github.com/ProShi), [Kevdoy](https://github.com/Kevdoy), [popkc3](https://github.com/popkc3), [WinterTreat](https://github.com/WinterTreat), [jackpini](https://github.com/jackpini), [fivetenth](https://github.com/fivetenth), [revae](https://github.com/revae), [RDR77](https://github.com/RDR77), [Oppa-YA](https://github.com/Oppa-YA), [YuFu-Onship](https://github.com/YuFu-Onship), [malnaanah](https://github.com/malnaanah), [CHJ85](https://github.com/CHJ85), [DavidLee1115](https://github.com/DavidLee1115), [philipzae](https://github.com/philipzae), and [Uthopik](https://github.com/Uthopik)

Photo "akiko_cycling_okinawa" (seen in the screenshots) by Seth A. Robinson
