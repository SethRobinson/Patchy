<img src="packaging/branding/patchy-logo-folded.svg" width="96" height="96" alt="Patchy logo">

# Patchy Image Editor

A free, open-source image editor for Windows, macOS, Linux, and the browser.
Built with a focus on accurate PSD compatibility, keeping text, vectors, masks,
layer styles, and Smart Objects editable when working with layered Photoshop files.
Work in limited palettes without flattening your layers, bring in Affinity,
OpenRaster, Paint.NET, and PDF artwork, and extend the editor with classic plug-ins
and JavaScript.

**[Download](#download)** · **[Try in your browser](https://www.patchyimageeditor.com)** · **[Features](#features)** · **[Full gallery](docs/screenshots.md)**

For bug reports and feature requests, please [open an issue](https://github.com/SethRobinson/Patchy/issues/new) or post in [Discussions](https://github.com/SethRobinson/Patchy/discussions). These are the best places to post because search engines can index them, helping others find the questions and answers.  Want to chat? Join [Seth's Discord](https://discord.gg/QwV5VaZ).

<a href="docs/images/screenshots/smart_filters.png"><img src="docs/images/screenshots/smart_filters.png" width="1000" alt="Cyclist photo with color-wheel and Levels controls, an editable Levels adjustment layer, a Smart Filter stack and shared mask, grouped text, and a clipping mask"></a>

*Designed to feel familiar if you're used to Photoshop's workflows and keyboard shortcuts.*

## Download

**Latest release: 1.19** · October 26, 2026 · [Release notes](#whats-new) · [All releases](https://github.com/SethRobinson/Patchy/releases)

Windows releases are code signed by Seth A. Robinson; the macOS app is signed and
notarized (Robinson Technologies Corporation); the Linux Flatpak repository is GPG
signed. Every release is published on the
[GitHub Releases page](https://github.com/SethRobinson/Patchy/releases) with SHA-256
checksums.

| Platform                  | Package                        | Download                                                                                                                             |
| ------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------ |
| Windows 10/11 (64-bit)    | Installer                      | [PatchyWindowsInstaller.exe](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyWindowsInstaller.exe) (113 MB)     |
| Windows 10/11 (64-bit)    | ZIP file (no installer)    | [PatchyWindowsNoInstaller.zip](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyWindowsNoInstaller.zip) (113 MB) |
| macOS 12+ (Apple Silicon) | DMG - drag to Applications     | [PatchyMacOS.dmg](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyMacOS.dmg) (120 MB)                           |
| Linux (x86_64)            | Flatpak, from our repository   | The [command below](#linux), or [com.rtsoft.patchy.flatpakref](https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref)            |
| Linux (x86_64)            | Flatpak bundle (single file)   | [PatchyLinux.flatpak](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.flatpak) (49 MB)                   |
| Linux (x86_64)            | AppImage (Ubuntu 22.04+)       | [PatchyLinux.AppImage](https://github.com/SethRobinson/Patchy/releases/latest/download/PatchyLinux.AppImage) (97 MB, [details](#appimage)) |
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
    <td valign="top" width="50%"><a href="docs/images/screenshots/artboards.png"><img src="docs/images/screenshots/artboards.png" width="460" alt="Patchy's Light theme with four artboards for a car-weekend campaign: a white story, a black banner, a cream post selected with the Artboard tool, and a transparent icon, with the Properties panel showing the post's position, size and background."></a><br><strong>Every size in one document</strong><br>Artboards keep a story, banner, post and icon side by side, each with its own size and background. Export them to separate files or PDF pages. This screenshot uses the Light theme.</td>
    <td valign="top" width="50%"><a href="docs/images/screenshots/gmic.png"><img src="docs/images/screenshots/gmic.png" width="460" alt="The G'MIC Filter Library with Rodilius selected among the Artistic filters, its settings beside the list, and a live preview on the right half of a Havana street photo, with the original on the left."></a><br><strong>Nearly 400 G'MIC filters</strong><br>Browse the G'MIC library, adjust the settings, and watch the result render live on your canvas. Here Rodilius runs inside a selection, with the original photo on the left.</td>
  </tr>
  <tr>
    <td align="center" valign="top" colspan="2"><a href="docs/images/screenshots/layered_import.png"><img src="docs/images/screenshots/layered_import.png" width="920" alt="Why settle for only .psd? An introduction above six labeled artboards in a 3 by 2 grid: Photoshop game artwork, Affinity game tips, an OpenRaster city poster, a Paint.NET landscape, a layered TIFF print design, and Aseprite pixel art, with imported layers visible in the Layers panel."></a><br><strong>Six formats, one document</strong><br>PSD/PSB, Affinity, OpenRaster, Paint.NET, layered TIFF, and Aseprite artwork, each imported onto its own artboard with its layer stack intact. Editable SVG and PDF imports are available too. See the <a href="docs/features.md#layered-format-interchange">format support and limitations</a>.</td>
  </tr>
</table>

[See the full gallery](docs/screenshots.md) for painting, palette mode, seamless textures, Camera Raw, long shadows, scripting, and more.

## Features

| Workflow | What you get |
| --- | --- |
| **Layered Photoshop files** | PSD and PSB, editable text, groups, masks, clipping, blend modes, and layer styles. |
| **Pixel art and game assets** | Paint in a fixed palette while keeping layers, text, and effects editable. Use retro or custom palettes, named colors, and indexed PNG/BMP export, alongside seamless tiling, sprite sheets, image sequences, DDS textures, and animated GIF/WebP/PNG. |
| **Layered formats from other editors** | Import Affinity (.af, .afphoto, .afdesign, .afpub) with supported text, vectors, masks, adjustments, and effects; open Paint.NET (.pdn) with its pixel layers and blend modes; open and save OpenRaster (.ora), layered TIFF, and Aseprite files. [Format support and limitations](docs/features.md#layered-format-interchange). |
| **PDF documents** | Import pages as editable text, vectors, and images on desktop; export single or multi-page PDFs with editable or flattened content. |
| **Non-destructive editing** | Adjustment layers, embedded and linked Smart Objects, editable Smart Filters, and shared filter masks. |
| **Paint and retouch** | Pressure-aware brushes, Mixer Brush, stroke smoothing, healing, cloning, Patch, Red Eye, Remove Object, Liquify, and selections, including Object Selection that picks out an object from a dragged box. |
| **Text and vectors** | Rich and vertical text, paragraph controls, Warp Text, Pen paths, shape layers, vector masks, SVG, and image tracing. |
| **Photos and print** | 16-bit and 32-bit (HDR) editing, Camera Raw development, HEIC/HEIF, AVIF, JPEG XL, JPEG 2000, OpenEXR and Radiance HDR, plus CMYK proofing and CMYK JPEG, TIFF, and PDF export for print. |
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

## Platform support

The desktop and browser builds share the editor, with these differences:

| Capability | Windows | macOS | Linux | Browser |
| --- | --- | --- | --- | --- |
| Available build | x64 | Apple Silicon | x86-64, Flatpak / AppImage | WebAssembly |
| 8-bit and 16-bit editing | Yes | Yes | Yes | Yes |
| 32-bit HDR editing | Yes | Yes | Yes | Opens converted to 16-bit |
| PSD/PSB, Affinity, OpenRaster, Paint.NET, layered TIFF, Aseprite | Yes | Yes | Yes | Yes |
| PDF import / export | Both | Both | Both | Export only |
| G'MIC Filter Library | Yes | Yes | Yes | No |
| Photoshop filter plug-ins (.8bf) | 32-bit and 64-bit | No | No | No |
| AVIF import / export | Both | Both | Both | No |
| JPEG XR import / export | Both | No | No | No |
| HEIC/HEIF import | With OS codecs | With OS decoder | With system decoder | With browser HEVC support |
| Printing | Yes | Yes | Yes | No |
| Scanner / camera acquisition | Both, via WIA | Scanners | No | No |
| Built-in JavaScript editor | Yes | Yes | Yes | Yes |
| Command line and local MCP connector | Yes | Yes | Yes | No |

Format support does not imply every source feature stays editable; see the
[format limits](docs/features.md#layered-format-interchange) and
[current limitations](docs/features.md#current-status). PDF import requires Qt PDF
when building a custom desktop version. HEIC/HEIF availability depends on the
[installed decoder](docs/heif.md). The browser build also has tighter memory limits.

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

### 1.19 - October 26, 2026

- Files: open GIMP .xcf files as layered documents, with GIMP's own blend modes (Grain Extract, Grain Merge and the LCh modes), its color filters and effect masks; open Krita .kra documents with their layers, masks, filters, fill and vector layers, layer styles and Krita's blend modes; 12-bit and lossless JPEGs open ([issue 438](https://github.com/SethRobinson/Patchy/issues/438)); PSD saves describe the file in XMP and EXIF and write a fresh thumbnail, and JPEG saves write the save stamp, minimal EXIF and an EXIF thumbnail ([issue 418](https://github.com/SethRobinson/Patchy/issues/418), [issue 419](https://github.com/SethRobinson/Patchy/issues/419)); File Info's metadata notice has Don't Show Again and a Preferences checkbox ([issue 417](https://github.com/SethRobinson/Patchy/issues/417)); New Document's print and photo templates show and open in mm, inches or cm, with an A3+ template
- Develop Raw: Filter > Camera Raw Filter brings its tone and color controls to any layer ([issue 392](https://github.com/SethRobinson/Patchy/issues/392)); a histogram with draggable tone regions ([issue 393](https://github.com/SethRobinson/Patchy/issues/393)), Shift-click auto values and Alt-click resets on the sliders ([issue 394](https://github.com/SethRobinson/Patchy/issues/394)), and a zoom bar ([issue 395](https://github.com/SethRobinson/Patchy/issues/395))
- Layers and channels: Alt-click on the footer's mask and new layer buttons, and New Layer gains a neutral-color Fill and Clip to the layer below ([issue 400](https://github.com/SethRobinson/Patchy/issues/400), [issue 408](https://github.com/SethRobinson/Patchy/issues/408), [issue 410](https://github.com/SethRobinson/Patchy/issues/410)); the clipping arrow points at the base layer ([issue 397](https://github.com/SethRobinson/Patchy/issues/397)); drag across the eyes to show or hide many layers, past the list's edge too, and the eye menu shows or hides this layer, the others or all ([issue 407](https://github.com/SethRobinson/Patchy/issues/407)); Layer Via Copy with no selection duplicates text and shape layers as they are ([issue 401](https://github.com/SethRobinson/Patchy/issues/401)); duplicating hundreds of layers is fast ([issue 402](https://github.com/SethRobinson/Patchy/issues/402)); thumbnail and mask context menus, Ctrl-click selection modifiers, and the footer Delete removes a targeted mask ([issue 431](https://github.com/SethRobinson/Patchy/issues/431), [issue 432](https://github.com/SethRobinson/Patchy/issues/432), [issue 433](https://github.com/SethRobinson/Patchy/issues/433), [issue 441](https://github.com/SethRobinson/Patchy/issues/441)); Channels panel eyes show chosen channels, Ctrl+2 to Ctrl+6 pick them, and rows show real thumbnails ([issue 412](https://github.com/SethRobinson/Patchy/issues/412)); resizing a clipped shape stays smooth ([issue 398](https://github.com/SethRobinson/Patchy/issues/398))
- Selection and transform: Free Transform transforms only the selected pixels when there is a selection ([issue 399](https://github.com/SethRobinson/Patchy/issues/399)); dragging inside a selection moves its outline, Quick Select sizes with Alt+right-drag, and a click on the pasteboard deselects ([issue 436](https://github.com/SethRobinson/Patchy/issues/436), [issue 428](https://github.com/SethRobinson/Patchy/issues/428), [issue 429](https://github.com/SethRobinson/Patchy/issues/429)); Select > Inverse keeps a feathered edge ([issue 430](https://github.com/SethRobinson/Patchy/issues/430)); Magic Wand gains a Sample setting and a clearer Intersect icon ([issue 435](https://github.com/SethRobinson/Patchy/issues/435), [issue 434](https://github.com/SethRobinson/Patchy/issues/434)); Q toggles Quick Mask once per press ([issue 437](https://github.com/SethRobinson/Patchy/issues/437)); Move, Free Transform and text drags keep up with the pointer ([issue 207](https://github.com/SethRobinson/Patchy/issues/207), [issue 443](https://github.com/SethRobinson/Patchy/issues/443), [issue 444](https://github.com/SethRobinson/Patchy/issues/444))
- Painting and retouching: status bar hints for the painting, clone and Move tools, Ctrl+click selects a layer, a size readout while Alt-resizing, and a two-tone center cross ([issue 421](https://github.com/SethRobinson/Patchy/issues/421), [issue 422](https://github.com/SethRobinson/Patchy/issues/422), [issue 423](https://github.com/SethRobinson/Patchy/issues/423), [issue 425](https://github.com/SethRobinson/Patchy/issues/425), [issue 446](https://github.com/SethRobinson/Patchy/issues/446)); holding Shift previews the straight line from the last stroke ([issue 445](https://github.com/SethRobinson/Patchy/issues/445)); Clone Source settings (four sources, offset, scale, rotation and overlay) in a gear popup, and a Show Overlay checkbox on the Clone Stamp and Healing Brush bars ([issue 424](https://github.com/SethRobinson/Patchy/issues/424), [issue 426](https://github.com/SethRobinson/Patchy/issues/426))
- Text: fonts with CJK or localized names keep their face through save and reopen, held Enter redraws live, and the wheel over a text box moves the canvas ([issue 413](https://github.com/SethRobinson/Patchy/issues/413)); Esc drops placeholder text and commits by default ([issue 415](https://github.com/SethRobinson/Patchy/issues/415)); new type in a generic font no longer shows a missing-font badge ([issue 442](https://github.com/SethRobinson/Patchy/issues/442)); the font box shows a family name from its start ([issue 416](https://github.com/SethRobinson/Patchy/issues/416))
- Interface: Image > Adjustments > Invert applies at once with no dialog ([issue 411](https://github.com/SethRobinson/Patchy/issues/411)); the path tools' right-click menu offers the Paths panel commands ([issue 396](https://github.com/SethRobinson/Patchy/issues/396)); the Color panel has Hue Cube, RGB Sliders and HSB Sliders views ([issue 404](https://github.com/SethRobinson/Patchy/issues/404)); New Guide Layout has presets, gutters, margins and saved layouts ([issue 403](https://github.com/SethRobinson/Patchy/issues/403)); View > Extras (Ctrl+H) hides or shows the overlays picked in Show Extras Options ([issue 420](https://github.com/SethRobinson/Patchy/issues/420)); blend mode lists scroll and stay on screen ([issue 414](https://github.com/SethRobinson/Patchy/issues/414)); in the options bar, Enter and Esc return to the canvas, Tab shows a focus outline, wraps within the bar and treats each mode button group as one stop ([issue 439](https://github.com/SethRobinson/Patchy/issues/439), [issue 440](https://github.com/SethRobinson/Patchy/issues/440)); clearer tool palette separators, Move bar distribute icons and Pick bar labels ([issue 406](https://github.com/SethRobinson/Patchy/issues/406), [issue 405](https://github.com/SethRobinson/Patchy/issues/405), [issue 409](https://github.com/SethRobinson/Patchy/issues/409))

### 1.18 - October 25, 2026

- Files: open and save OpenRaster (.ora), with Lock Image Pixels and clipping mapped both ways and 16-bit files Krita opens without asking ([issue 375](https://github.com/SethRobinson/Patchy/issues/375)); open Paint.NET (.pdn) files with their layers, blend modes, EXIF, XMP and palette, including canvases past 268 million pixels ([issue 379](https://github.com/SethRobinson/Patchy/issues/379)); a layered save to a format without a layer's blend mode asks whether to keep the look or save it as Normal ([issue 379](https://github.com/SethRobinson/Patchy/issues/379)); File > Export > Layers as Animated PNG ([issue 366](https://github.com/SethRobinson/Patchy/issues/366)); New Document gains a Name field, Save Preset and Color Mode, and a preference skips the empty layer above the Background ([issue 374](https://github.com/SethRobinson/Patchy/issues/374), [issue 360](https://github.com/SethRobinson/Patchy/issues/360), [issue 363](https://github.com/SethRobinson/Patchy/issues/363)); the open report no longer counts Curves and other native adjustment blocks as unknown PSD data
- Healing and retouching: the Healing Brush heals the whole stroke on release, so wide dark spots blend in, and gains a Mode dropdown with Replace and six blend modes ([issue 382](https://github.com/SethRobinson/Patchy/issues/382), [issue 381](https://github.com/SethRobinson/Patchy/issues/381)); the retouch tools sample Current Layer, Current & Below or All Layers, with Ignore Adjustment Layers ([issue 382](https://github.com/SethRobinson/Patchy/issues/382), [issue 383](https://github.com/SethRobinson/Patchy/issues/383)); Clone and Healing show Alt, Ctrl and not-allowed cursors, Ctrl is a temporary Move, and the source crosshair follows live ([issue 380](https://github.com/SethRobinson/Patchy/issues/380)); the Red Eye tool joins the healing flyout and leaves real eyes without a red rim or glint halo ([issue 370](https://github.com/SethRobinson/Patchy/issues/370))
- Text: rotated Photoshop paragraph text edits rotated and keeps its place ([issue 373](https://github.com/SethRobinson/Patchy/issues/373)); the caret spans the drawn line ([issue 384](https://github.com/SethRobinson/Patchy/issues/384)); the text box outline shows on unrotated text and reads over any ink ([issue 385](https://github.com/SethRobinson/Patchy/issues/385)); the size field keeps the points typed, the Text Color window follows the selection, and the Color and Swatches panels recolor selected text ([issue 371](https://github.com/SethRobinson/Patchy/issues/371)); committing an unchanged edit adds no History step ([issue 387](https://github.com/SethRobinson/Patchy/issues/387))
- Panels: drag panels together into tab groups, drag a tab out to separate it, and drag tabs sideways to reorder them; a panel dropped off the column goes back where it was, collapsed panels stack at the top of the column ([issue 391](https://github.com/SethRobinson/Patchy/issues/391)), and a toolbar right-click lists the panels; a Color panel in the sidebar, and Alt-click or Alt-drag in it, in the Foreground Color window or on a swatch sets the background color ([issue 365](https://github.com/SethRobinson/Patchy/issues/365))
- Layers and tools: the Layers panel previews the highlighted blend mode while its list is open ([issue 367](https://github.com/SethRobinson/Patchy/issues/367)) and has a Thumbnail Size choice of None, Small, Medium or Large ([issue 386](https://github.com/SethRobinson/Patchy/issues/386)); Merge Down works inside a hidden folder ([issue 389](https://github.com/SethRobinson/Patchy/issues/389)); holding, right-clicking or double-clicking a flyout tool selects the tool it shows ([issue 368](https://github.com/SethRobinson/Patchy/issues/368)), and right-clicking a tool without a flyout selects it ([issue 388](https://github.com/SethRobinson/Patchy/issues/388)); a clearer Crop tool icon ([issue 390](https://github.com/SethRobinson/Patchy/issues/390)) and a tidier brush options bar ([issue 364](https://github.com/SethRobinson/Patchy/issues/364))
- Linux: the desktop entry lists every image type Patchy opens, the README has Flatpak install commands for Fedora and openSUSE, and a clearer FUSE 3 note ([@philipzae](https://github.com/philipzae)); following the system theme picks Light when the desktop reports no color scheme ([issue 334](https://github.com/SethRobinson/Patchy/issues/334))

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

There are thousands of regression and benchmarking tests. AGENTS.md links agents to the testing guide that explains how to select and run them.  Due to the size of some of the test files I use they aren't all included, it will skip tests that require missing files.\
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
