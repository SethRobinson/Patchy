# File formats: registry, per-format quirks, PSB, document alpha

Deep reference for file-format work. Read this before touching a reader/writer, open/save filters, PSD/PSB internals, import notices, or alpha/mask import.

## Registry and dispatch

- **FormatRegistry**: `builtin_format_registry()` (format_registry.cpp, function-local static) is the single instance; `load_document_from_path` (main_window.cpp) consults it BEFORE the QImageReader fallback (a registry read that throws still falls back to Qt where a Qt plugin exists, but the REGISTRY error is reported when Qt fails too: it names the real problem). Handlers may be read-only (`write == nullptr`) and may carry a `sniff` content check (used to disambiguate `.ase`: Aseprite magic 0xA5E0@4 vs Adobe `ASEF` swatches: the Aseprite reader throws a message pointing at the Palette panel for swatch files).
- **One filter table**: `file_format_entries()` in `main_window_files.cpp` generates open/save/export filters, `is_supported_image_extension`, `save_file_filter_for_path`, and `path_with_default_extension`. Display names sit in `QT_TRANSLATE_NOOP("QObject", ...)`; update patchy_ja.ts when adding one.
- New formats slot in with one table row + one registry row + one writer branch.

## Open-dialog filter contract

Open, sprite-sheet, and image-sequence dialogs pass `FilterNameDetails::Hidden` through `get_open_file_name`. Qt shows only the text left of the last `(` and uses the final parenthesized list as the machine filter. `open_file_filter()` therefore writes each row as `Name (patterns) (patterns)`; the duplication is deliberate and supplies both the visible name and machine-readable specification.

The visible portion must retain a `*.` token. The Windows 11 native dialog appends the complete semicolon-joined specification to any filter name without one, so the all-formats row uses the short `(*.psd *.png *.jpg and more)` hint instead of exposing roughly 50 patterns. `ui_open_dialog_hides_name_filter_details` pins this shape.

## Per-format catalogue

All read AND write, except camera raw, HEIF/HEIC, and .af, which are read-only; modules in src/formats/, Qt-free, explicit-endian via `binary_le.hpp` (LE) or `psd_binary.hpp` (BE).

- **PSD/PSB** — see the PSB section below and docs/ps-compat.md; 16/32-bit files import with conversion to 8-bit (see the deep-import section below).
- **BMP** — including 32-bit `BI_RGB`/compression 0, whose 4th byte Patchy keeps (feeds document-alpha import below).
- **ICO/CUR** — multi-size; every embedded size imports as a hidden layer named "WxH": the writer reuses a matching "WxH" pixel layer verbatim, so small sizes round-trip; 256px entries are PNG-compressed via an injected Qt codec, `ico::set_png_codec`, installed by `install_ico_png_codec()` in the MainWindow ctor; CUR hotspots ride layer metadata `patchy.cursor_hotspot` and prefill the export dialog.
- **TGA** — types 1/2/3/9/10/11, both origin flags; 15/16-bit rejected; palette-mode docs write type 1 indexed.
- **GIF** — write-only encoder gif_document_io.cpp: reading stays with the bundled qgif — so the Windows package must ship `imageformats/qgif.dll`; build-release.bat's `CopyRequiredImageFormatPlugins` list includes it explicitly (macdeployqt and the Flatpak KDE runtime bundle it on the other platforms); LZW width-growth uses the pre-increment check, verified against Qt + Pillow, and `gif_encoder_bytes_are_stable` pins the exact bytes by FNV hash.
- **Aseprite** — frame 1 only; layer tree/blend modes/opacity round trip; zlib cels via vendored `src/formats/miniz/`; verified by driving installed Aseprite CLI. Aseprite is the layered save in Save As (routed in save_document_to_path next to PSD).
- **PCX** — 8-bit indexed EOF-palette + 24-bit 3-plane RLE.
- **ILBM/PBM** — ByteRun1 via the shared `psd::decode_packbits`/`encode_packbits_row` (the encoder was promoted from psd_document_io.cpp to psd_descriptor.{hpp,cpp}); EHB supported, HAM rejected, writer emits planar ILBM with masking type 2 for transparency.
- PNG/JPEG/TIFF/WebP stay on Qt.
- **Camera raw** — read-only; see the section below.
- **HEIF/HEIC** — read-only, platform codecs only; see the section below.
- **.af (Affinity)** — read-only, tier-2 layer import (raster layers/groups/masks/clipping/CMYK-Lab/embedded docs); see the section below.

## Camera raw (CR2/CR3/NEF/ARW/RAF/DNG, ...)

Backed by vendored LibRaw 0.22.1 (`src/formats/libraw/`, static target `patchy_libraw`).
Licensing and build rules live in the CMake comment and NOTICE-THIRD-PARTY.md: Patchy elects
CDDL-1.0 from LibRaw's LGPL/CDDL dual license, only the stock tarball may be vendored (the
separate demosaic-pack repos are GPL), and the build defines neither USE_JPEG, USE_ZLIB, nor
LIBRAW_NOTHREADS — no new transitive deps, per-instance decoder state (two sessions may run
on different threads), and lossy-/deflate-compressed DNG variants fail with a clear message.

- **`formats/raw_document_io.{hpp,cpp}`** is the Qt-free wrapper (the public header is
  LibRaw-free; `patchy_libraw` links PRIVATE into `patchy_formats`). `DevelopParams` maps to
  LibRaw's output params (as-shot/auto/custom white balance, exposure EV within LibRaw's
  -2..+3 supported range, highlight-recovery clip/unclip/blend/rebuild, auto-brighten +
  brightness, demosaic algorithm, wavelet + FBDD noise reduction, half-size) plus Patchy's
  own tone/color controls below. LibRaw develops to 16-bit sRGB (sRGB gamma explicitly
  set; LibRaw's default is BT.709), then **`formats/raw_tone.{hpp,cpp}`** applies
  contrast/highlights/shadows (one composed 65536-entry LUT: bell-shaped shadow lift
  pinned at black, highlight ramp deliberately NOT pinned at white so -100 dims blown
  areas, smoothstep-blend S-curve and its exact inverse for contrast) and
  saturation/vibrance (Rec.709-luma scaling; vibrance weighted by how unsaturated the
  pixel is) before the final rounded 8-bit bake — every raw-precision decision happens
  before the editing pipeline's 8 bits. Defaults are neutral: auto-brighten is OFF (no
  surprise histogram stretch), all tone/color sliders 0. `DevelopSession` keeps the
  unpacked sensor data so previews re-run `dcraw_process` without re-decoding (LibRaw's
  documented multirender pattern); `read_camera_raw` is the one-shot headless path. All
  decoding goes through `open_buffer` (never file paths, avoiding Windows wide-path
  issues).
- **`formats/raw_white_balance.{hpp,cpp}`** converts temperature/tint to camera-space
  multipliers through the file's `cam_xyz` matrix (Planckian locus below 4000 K, CIE
  daylight above; tint = Duv offset, ~ACR slider scale) and back (bisection) so As Shot
  displays real kelvin values. Files without a usable matrix fall back to treating the
  camera as sRGB. Plain double math only — but LibRaw's own float pipeline is NOT
  byte-stable across toolchains, so raw tests assert statistics, never hashes.
- **The develop dialog** (`src/ui/raw_develop_dialog.{hpp,cpp}`) intercepts raw extensions
  in `open_document_path` when `imports/showRawDevelopDialog` (default true; Preferences
  checkbox) is set; Cancel aborts the open. Previews always develop at HALF size on a
  worker thread (one in-flight develop, one-deep latest-wins queue — the filter-gallery
  async pattern); the embedded JPEG thumbnail paints first. Accept develops at full
  resolution through the same serialized state machine and returns the finished document.
  Last-used settings persist under the `imports/rawDevelop*` keys (persisted contract —
  never rename; note `rawDevelopHighlights` stores the RECOVERY mode for historical
  reasons while the tonal slider uses `rawDevelopToneHighlights`). With the preference
  off (and for every headless path: tests, CLI opens, linked smart-object refresh) the
  format-registry handler develops neutral defaults: as-shot WB, AHD, sRGB, no
  auto-brighten, all tone/color sliders at 0.
- **Raws are read-only sources**: the registry handler has no writer and the
  `file_format_entries()` row has empty save_extensions, so open dialogs list raws but
  Save As/Export never do. `save_document()` routes a raw-backed session to Save As
  defaulting `<basename>.psd` (`is_read_only_source_extension`, next to the layered-flat
  guard). The session opens clean with its real path (Photoshop parity: the raw on disk is
  untouched source material).
- **Extension list** lives in `raw::camera_raw_extensions()` (single source of truth for
  the registry and the dialog filter); deliberately excludes ambiguous `.raw`, and
  TIFF-based raws saved as `.tif` stay with Qt's TIFF path.
- **Tests**: `tests/synthetic_dng.hpp` builds a minimal uncompressed 16-bit Bayer DNG
  byte-by-byte (sRGB ColorMatrix1, AsShotNeutral (1,1,1) = D65) shared by the core develop
  tests and the `ui_raw_*` dialog tests. Real camera samples (CC0, raw.pixls.us) live in
  untracked `local-test-fixtures/raw/` behind `raw_decodes_real_camera_samples_if_available`
  (remotes [SKIP]). Known gaps that surface as clean errors: lossy/deflate DNG (no
  jpeg/zlib), JPEG-XL DNG 1.7, Nikon Z8/Z9 High Efficiency NEF.

## HEIF/HEIC (iPhone photos; .heic/.heif/.hif)

Read-only, decoded by PLATFORM codecs only. **Never vendor an HEVC decoder or encoder
(libheif/libde265/x265): the whole design exists so Microsoft/Apple/the Flatpak codec
extension carry the HEVC patent licenses** ([legal-constraints.md](legal-constraints.md) has the binding rule;
the GIMP/Krita bundle-libde265 posture was researched and deliberately rejected, July
2026, decision by Seth). HEVC is heavily patent-encumbered: the Access Advance and
Via LA pools plus bilateral holders like Nokia, who also hold HEIF container patents
running to ~2035, so even the container is not safely reimplementable. Encoding stays impossible everywhere: the registry handler has no writer (so Save
routes to Save As .psd like camera raw) and `write_flat_image_file` rejects heif
extensions, because QImageWriter's platform plugins COULD silently HEVC-encode on
macOS/Linux but not Windows.

- **`formats/heif_document_io.{hpp,cpp}` + `heif_document_io_win.cpp`**: extensions
  (`heif::heif_extensions()`, single source of truth), ftyp-brand sniff (HEVC brands
  only; AVIF deliberately rejected), and per-OS `read_heif`:
  - **Windows** (the real decoder): WIC. The codecs are the Store's "HEIF Image
    Extensions" + "HEVC Video Extensions" packages (in-box on Windows 11 22H2+). A stub
    codec is ALWAYS registered, so availability cannot be enumerated -- attempt the
    decode and map the two failure shapes: `WINCODEC_ERR_COMPONENTNOTFOUND` at decoder
    creation = HEIF package missing; `MF_E_TOPO_CODEC_NOT_FOUND` (0xC00D5212) at pixel
    request = HEVC package missing (decoder creation and GetFrame SUCCEED in that state).
    Those errors carry marker prefixes (`heif::k*PackageMissingMarker`) that
    `show_open_failed_message_box` (main_window.cpp) strips and turns into an "Open
    Microsoft Store" button (`ms-windows-store://pdp/?ProductId=...`). WIC returns
    UNROTATED pixels; the container rotation arrives as an EXIF-style value at
    `/heifProps/Orientation` and is applied by `heif::apply_exif_orientation` (pure,
    pinned by codec-free unit tests). ICC profiles (iPhone = Display P3) convert to sRGB
    via `IWICColorTransform`, falling back to unmanaged pixels.
  - **macOS/Linux**: `read_heif` always throws, and the registry-error -> QImageReader
    fallback in `load_document_from_path` decodes instead -- qmacheif (Qt's Apple-only
    ImageIO plugin, already in the aqt install and deployed by macdeployqt; outputs sRGB,
    orientation via the existing `setAutoTransform(true)`) or the KDE runtime's
    kimg_heif. kimg_heif ATTACHES the P3 color space without converting, so the fallback
    branch bakes heif-family images to sRGB via `convertToColorSpace` (scoped to heif so
    PNG/JPEG opens keep their bytes). The stub's thrown message doubles as the
    missing-codec/corrupt-file text when Qt also fails.
- **Flatpak**: the KDE 6.8 runtime ships kimg_heif and libheif, but the HEVC decode
  plugin lives in `org.freedesktop.Platform.ffmpeg-full//24.08`, declared by the
  manifest's `add-extensions` block. Single-file BUNDLE installs never auto-pull it
  (verified 2026-07; repo/Flathub installs would): without it only HEIC opens are
  affected and the error dialog shows the exact install command (also in the README
  download section). packaging/linux/README.md has the details and when the block can
  be dropped. The remote Linux test machine uses aqt Qt (no kimageformats), so heif
  tests [SKIP] there while the extension-equipped Flatpak decodes (verified in-sandbox).
- **Tests**: statistics only, never byte pins (lossy HEVC + per-platform CMS).
  `test-fixtures/heif/quadrants.heic` was encoded from a Patchy-authored PNG with macOS
  `sips`; decoder-dependent tests [SKIP] on the known codec-unavailable messages and
  hard-fail on anything else. `ui_heif_open_is_read_only_if_available` needs the
  repeating-QTimer dismisser for the potential `openFailedMessageBox` (dismiss via
  `reject()` so the Store button can never fire in a test).

## .af (Affinity by Canva; read-only, tier 2)

`src/formats/af_document_io.{hpp,cpp}` opens Affinity's native unified format
(the 2025 "Affinity by Canva" app; magic `00 FF 4B 41`). Registered read-only
(`patchy.formats.af`, sniff on the magic) with a read-only filter-table row
claiming `.af` plus the Affinity 2.x generations `.afphoto/.afdesign/.afpub`.
Affinity 2.x writes the same container (version 11) and the same doc.dat wire
grammar (file_ver 2, document versions in the verified 20..32 range) as the
unified app, so one importer covers both; verified against documents authored
interactively in Affinity Photo 2.6.5 (`test-fixtures/af/tiny-v2-*.afphoto`
score 0 %-off against Affinity's own PNG exports). `.afdesign` and `.afpub`
are the identical format written by the sibling apps (Designer 2 on this
machine is installed but unlicensed and Publisher 2 absent, so those two are
claimed on format identity, not per-app renders). Pre-2.x (afread-era 1.x)
files that fail the tree parse fall back to the tier-0 embedded-preview
import with a notice.

A 14-file wild sweep (public GitHub design sources, 2026-07-28; files and
per-file notes in af-spike FINDINGS.md "Wild-file sweep", never committed)
confirmed the claim in practice: container v10-v12 and document versions
3..31 all import through the registered extensions, and 1.x-era files
(versions 3-9) parse fully rather than falling back; five files, spanning
versions 3 through 31, render pixel-identical to their own embedded
thumbnails. A truncated/corrupt wild file is rejected cleanly (the FAT walk
throws; no crash).

A second, 57-file sweep the same day (af-spike FINDINGS.md "2.x wild
sweep") targeted the Affinity 2 generation specifically: 30 genuine 2.x
documents (doc versions 20-26, container v11, including two real
Publisher .afpub files) plus 27 more 1.x-era files, all imported without a
crash. It shipped three fixes, pinned by
`af_reads_affinity2_wild_files_if_available` (skips without the local
samples): the canvas comes from the first spread's `SprB` bounds whenever
they are present and sane, with the document node's `DfSz` only a fallback
(2.x files routinely store the New Document preset size there; every
observed DfSz/SprB mismatch resolves to SprB by the file's own thumbnail
aspect); mask enclosures compose through their OWNER node's transform (the
adjunct is a child of its owner, so its plane lives in the owner's local
space - 2.x masks carry no adjunct Xfrm at all, while 3.x masks carry an
inverse-of-owner Xfrm that lands the plane in spread space); and Designer
SYMBOL instances resolve through their instance-link rings - a linked
instance stores no local geometry or paint, and its `SLnk`/`GLnk`/`DLnk`/
`CLnk` links hold `ILOb` lists naming every sibling instance, one of which
carries the defining `Crvs`/`Shpe`/fill records (the importer chases one
hop; per-instance transform and opacity stay on the instance node).

Old-generation wire variants that sweep surfaced, now handled (pinned by
`af_reads_old_generation_wild_files_if_available`, which skips without the
local samples): `edc/<n>` embedded-document streams carry an 8-byte `EmDc`
tag wrapper around the nested container in old AND current files (the
importer unwraps it; before this every `EmbR` flatten failed with a notice);
the oldest (v3-era) documents name the vector fill descriptor `BFil` instead
of `BFFl` (same FDsc payload); by v4 `BFil` and the `PFil` stroke paint carry
the Fill class DIRECTLY (no FDsc/FDeF wrapper) and the stroke width rides a
node field tagged `LSty` holding the same LDsc shape as modern `LILn` -
vh-check's stroke-only phone outline pins the trio, and a paint-less 1.x
container's children emit as PLAIN siblings, not clipped to its empty
placeholder (modern files keep clipping even to placeholders); gradient
stops and other colors can be `HSLA` classes
(hue in turns, standard HSL conversion) or five-float `CMYK` classes (the
same profile-less ink mix as the CMYK raster decoder - reading the first
four floats as RGBA painted embedded menu icons cyan). Unwrapping embeds
also rescued documents that previously fell to the tier-0 preview because
nothing decoded (a v3-era all-vector logo now imports for real at 4 RMSE
against its own thumbnail; restaurant-menu improved 27.6 -> 23.7).
Old-generation DyBms (observed on v6-v9) store NO `TWi<n>`/`THi<n>` tile-grid
fields (nor the mip variants): when both are absent the decoder derives the
grid from the plane's pixel dimensions (256-byte columns by 256-row bands,
`ceil(width*sample_bytes/256) x ceil(height/256)` - the derived counts match
every Sta list in the pinning document), but ONLY when the plane's Sta list
holds exactly that many codes. A plane with no Sta at all keeps the 1x1
default: 2.x DyBms whose pixels come from a placed original often store no
mip fields whatsoever, and an oversized empty mip plane would pass
`mip_source_planes`' size check and feed the code-5 tiles a blank source
instead of failing to the honest placeholder (the paulgessinger screens
mockup regressed exactly that way). Everything else about the old tile
wire matches 2.x: planar per-channel blocks (dedup'd via shared Blck refs
across channel Idx lists, which the tree parser already resolves) and
edge-partial tiles padded to the full 65536-byte stream. Before the
derivation every raster wider than 256 px in such files failed the
plane-size validation and imported as an empty placeholder (the dbacchet
osd-mux v9 schematic flattened to white plus text; it now imports all nine
rasters for real).

Closed since that sweep (July 2026, third session): parametric `ShpN`
shapes import as real shape layers, multi-artboard documents import every
artboard at its layout position, parent transforms compose down the node
tree (Affinity is a scene graph), modern embedded documents center-anchor,
and the old-file `Snap` snapshot render (a full-resolution PNG of the whole
document in a `c/<n>` stream) replaced the 512px thumbnail as the
nothing-decoded fallback for documents that carry one. Vector-mask adjuncts
closed in the fourth session: an `AdCh` entry with no `Bitm` can carry a
whole vector layer subtree (a `Grup`/`Scop` of shapes, a `PCrv`, a `ShpN`,
or a baked `Comp`) acting as the owner's clip mask; the importer walks the
subtree (owner-space composition, per-contributor `shape_group` union, the
SVG clipPath convention) into a native `LayerVectorMask` +
`update_vector_mask_raster`, which PSD saves round-trip as `vmsk`. Pinned
by tiny-vector-mask.af (rmse 1.1 vs Affinity's own render) and the
steam-logo wild file (its steam silhouette is a Grup-of-shapes mask; rmse
138 -> 51). A partly-decodable mask subtree drops WHOLE with a notice
(a partial mask would clip wrongly); adjunct kinds with no geometry stay
silently skipped as before. Remaining known
gaps: the
old-generation embed transform (one v9 sample suggests an extra ~0.766
uniform scale, but its only ground truth is a STALE snapshot cache, so the
mapping may not exist; 1.x embeds keep origin anchoring, which ns-splash
pins), embeds whose only pixel source is the node's `IRDS` `FlDS` original
file bytes (no `edc` stream - fladder-icon-general renders its embed
empty), and the shape
kinds Patchy still placeholders (callouts, spiral, QR, circle-rounded
CrcI/CrcO stars, and arrow end styles beyond flat/plain; the 2026-07-29
sweep closed the rest of the long tail - see the parametric-shapes bullet).

- **Tier 2 (current)**: parses the serialized document tree (`doc.dat`) and
  builds real Patchy layers - the layer tree (groups nested with pass-through
  by default), each raster layer's full-resolution pixels decoded from its
  tiles and placed by its bounds/transform, plus name, visibility, opacity,
  fill-opacity, and blend mode. The importer builds `Layer` objects and lets
  Patchy's compositor do the blending (it never composites itself), so the
  blend math stays Patchy's calibrated implementation. On any structural
  problem the reader falls back to the **tier-0** flat-render layer rather
  than failing the open (env `PATCHY_AF_TRACE=1` prints why the tree walk
  bailed): old files that saved a `Snap` snapshot subtree contribute their
  full-resolution baked render (the largest decodable DyBm original under
  the root's `Snap` field; possibly a stale cache), everything else the
  512px embedded thumbnail. The reverse-engineering working notes, generated
  corpus, and Python reference/verification tooling live in
  `local-test-fixtures/af-spike/` (machine-local and untracked; FINDINGS.md
  there holds the raw spike notes). This section is the committed .af format
  record, and `src/formats/format_registry.cpp` points here for the
  container spec.
- **Node transforms compose (scene graph)**: children of any content node
  live in the parent's LOCAL space, so the parent `Xfrm` composes onto
  every descendant (`LayerBuildContext::transform`; wild pin:
  arkanis-discord's scaled containers hold children whose transforms
  exactly invert the parent scale). The one deliberate exception: CLIPPED
  children of raster bases keep uncomposed coordinates (the corpus places
  them in spread space).
- **What imports faithfully**: raster layers in RGBA8/16, Gray8/16, and
  RGBA-float32 (16-bit down-converts value/257, float linearizes to sRGB),
  whether untransformed, translated, or under a full scale/rotate affine
  (`Xfrm` = `[a,b,tx,c,d,ty]`, dest = A*src + t; the importer rasterizes
  through the affine with bilinear premultiplied accumulation, pinned RMSE
  0.003 against Affinity's own render of a rotated+scaled raster); groups
  (nested, pass-through by default); layer masks (the M8/M16 mask plane in a
  node's `AdCh` enclosure becomes a `LayerMask`; transformed masks resample
  through their affine too); vector masks (a Bitm-less `AdCh` adjunct
  carrying a vector subtree becomes a native `LayerVectorMask` - see the
  vector-mask paragraph above); clipping (Affinity nests clipped layers inside
  their base's child list; Patchy models them as clipped siblings above the
  base); embedded documents (the `EmbR`/`EmbC` reference to an `edc/<n>`
  nested container is parsed recursively and flattened; modern files -
  document version >= 20 - anchor the node transform's translation on the
  flattened canvas's CENTER, pinned by restaurant-menu's swashes landing
  exactly on its canvas corners, while the 1.x generation anchors at the
  origin, pinned by ns-splash's layout); placed/opened images
  stored in the lazy layout (below); the spread background (`SprT` false ->
  a bottom "Background" fill layer of the `BgrC` color, matching Affinity's
  own composite); document DPI (root `UVCn`/`UPPI` -> print settings);
  visibility/opacity/fill-opacity/blend (all Photoshop-shared blend modes map
  natively as of July 2026, including Vivid/Linear Light, Hard Mix,
  Darker/Lighter Colour). Verified pixel-exact against Affinity's own PNG
  export on synthetic and real multi-layer documents (an 11-layer game mockup
  scores ~0 RMSE; a CMYK restaurant menu with embedded icon images and masks
  renders correctly).
- **The lazy/mip DyBm layout (placed and opened images)**: interactive
  Affinity does NOT materialize base tiles for placed/opened pictures. The
  base `Sta` codes are 5 ("pixels come from the placed original"), the
  untouched original file rides in a `c/<n>` stream named by the DyBm's
  `Bckg` field (a serialized `Blck` tree: `Data` = the file bytes, `TifO` =
  EXIF orientation, `DSrc`/`Filn` = source path), and a mip pyramid is stored
  under per-level tags `'M','W'|'H'|'I'|'T',<raw level byte>,<channel digit>`
  (level 1 = half resolution). The importer decodes the embedded original
  with the vendored stb_image (JPEG+PNG, decode-only; NOTICE entry) plus
  `heif::apply_exif_orientation`, falls back to a bilinear 2x upscale of mip
  level 1 for other embedded formats (notice), and degrades to a placeholder
  if neither works. Full Sta code set: 0/1 empty, 2 fill max, 3 fill float
  1.0, 4 stored 256-byte x 256-row tile (the grid is BYTE-pitch horizontally:
  a 16-bit channel spans width*2 bytes), 5 from-original; unknown codes make
  the bitmap honestly undecodable, never silent black. Stored `Blck` entries
  may carry a `Rect` sub-rect for partial tiles.
- **Pristine placed images become embedded smart objects**: when a
  lazy-layout raster has no hand-painted base tile (every base code is
  5/fill), the untouched original file additionally becomes a Patchy embedded
  smart-object source (uuid + `SoLd` authored like the convert flow, placed
  quad from the node transform, JPEG/png filetypes sniffed) so Edit/Replace
  Contents work and PSD saves embed the original. The decoded pixels stay
  the layer raster, so rendering is identical either way.
- **Embedded ICC profiles**: lazy-layout bitmaps carry a `Prof` -> `ICCP`
  class with real profile bytes per space (RGBP/CMYP/LABP/...). When a CMYK
  bitmap has `CMYP` bytes it converts through the PSD path's lcms2 transform
  (.af ink is straight, so channels invert into the transform's PSD-inverted
  convention). Script-materialized bitmaps still store no profile.
- **Approximate (notice, but rendered)**: profile-less CMYK raster layers
  convert through the naive ink mix - the PSD reader's profile-less fallback;
  .af channels are straight ink, not PSD-inverted. Affinity-only blend modes
  remap to the closest EXISTING BlendMode with a notice naming both modes
  (`approximate_blend_mode`; never new enum values - imports must stay
  PSD-savable). Chosen by RMSE over a 2026-07-29 full-gamut probe (af-spike
  blend_probes: every (s,d) byte pair once per channel, scored against
  Affinity's own renders; harness reproduces Normal at 0.0/Exclusion 0.3):
  Average = (s+d)/2 exactly -> Normal with the layer/group/effect opacity
  folded x0.5 (algebraically identical); Negation = 1-|1-s-d| -> Exclusion
  (RMSE 60 vs Normal's 109); Reflect = d^2/(1-s) -> Overlay (37 vs 101);
  Glow = s^2/(1-d) -> Linear Light (24 vs 51); Pigment (no classical formula
  fits) -> Overlay (45 vs 109). ContrastNegate (best candidate still 84 of
  128) stays Normal + the plain not-supported notice; groups with an
  unmapped explicit mode now fall to Normal + notice instead of silently
  keeping pass-through. Pinned by tiny-blend-affinity.af.
- **Erase blend mode folds into a masked group** (July 2026): Erase (wire
  25/v0 or 32/v6+) removes alpha from everything beneath it - not a color
  blend, so no BlendMode can carry it. `fold_erase_layers` drops the carrier
  layer and wraps the sibling layers beneath it in a new `LayerKind::Group`
  named "<carrier> (Erase)" with blend mode Normal and a gray8 group mask =
  255 - (carrier alpha x Opacity x Fill x its own masks), clipped to the
  canvas, `default_color` 255. The wrapper must be ISOLATED (Normal, not
  pass-through): a pass-through group mask attenuates each child separately
  (coverage 1-(1-m)^N over N children) while the isolated path masks the
  merged stack exactly once - and a Normal folder + raster group mask is
  native PSD, so the construction round-trips. Limits, each declining with a
  notice and rendering the carrier as Normal: carriers inside clipping runs,
  Group/adjustment carriers, clip bases, bottom-most carriers (nothing
  beneath; an Erase at the bottom of a pass-through group DOES reach below
  the group in Affinity, which PSD cannot express), empty carriers, and more
  than 64 folds per sibling list. A carrier's own layer effects are dropped
  (notice). The spread background is deliberately added outside any wrapper:
  Affinity's background is not a layer and Erase does not cut it. Pinned by
  tiny-blend-affinity.af (af_approximates_affinity_only_blend_modes,
  af_erase_blend_round_trips_through_psd) and the sprite wild file.
- **Lab documents (LABA16, format 5)** decode natively: the wire is the ICC
  v4 Lab16 PCS encoding (L 0..65535 = 0..100, a/b with 0x8080 = 0), converted
  through lcms2's built-in D50 Lab profile (`LabToRgbTransform`,
  src/color/color_management). Pinned July 2026 against a saturated
  calibration doc at RMSE ~0.5 vs Affinity's own render; the earlier
  "compressed a/b scale" mystery was a desaturated probe document.
- **Multi-page documents** import the first spread with a notice naming the
  total count.
- **Artboards import fully** (July 2026): an artboard is a `ShpN` rectangle
  marked `ABEn=true` (old generation) or carrying a non-null `phrp` ->
  `aprp` artboard-properties class (current files), possibly nested inside
  transformed groups. The canvas sizes to the spread bounds (`SprB`, or the
  computed union of the artboard boxes) and each artboard imports as a
  group at its layout position: the artboard's own rectangle paints as a
  bottom background layer, children compose through the artboard transform,
  and a rectangular `LayerMask` (default black) clips the group to the
  artboard box - Affinity clips artboard content, pinned by its own render
  of the tiny-artboards fixture (edge-clipped child; an out-of-bounds child
  disappears entirely). Pinned by `af_imports_multi_artboard_document`.
- **Parametric shapes (`ShpN`)** import as real Patchy shape layers for the
  kinds Patchy models; the rest keep a named placeholder (and a document
  whose only content is unmodeled shapes still prefers the tier-0 preview).
  The `Shpe` field's class tag is the shape kind: `ShNR` rectangle, `ShpE`
  ellipse (inscribed in the local `ShpB` box), `ShPy` regular polygon
  (`Side`-gon, JS default 5, inscribed in the box ellipse, first vertex up),
  `ShSt` star (`Pnts` outer vertices alternating with half-step inner
  vertices at the `IRad` fraction, first vertex up), and `ShpT` triangle
  (apex at the `"Pos "` fraction across the top edge). Smoothed polygons
  (`Smth` true) render smooth anchors whose
  tangent scales with `Curv`: the probe pins Curv=0 as EXACTLY the plain
  polygon; Curv=1 maps to the circle through the vertices (plausible,
  unpinned). `ShNR` carries `ShCR` per-corner radii in TL/TR/BR/BL order
  (fractions of min(w,h), or pixels when `AbSz`), `CTyp` per-corner types
  (0 Round, 1 Straight = chamfer, 2 RoundInverse = concave arc centered on
  the corner, 3 Cutout = square notch, 4 None), and `Lock` (absent/true =
  single-radius mode: corner 0's radius AND type render on all four
  corners). Fill/stroke/transform ride the same BFFl/LILn/LIFl/Xfrm
  handling as PCrv (the shared `build_vector_layer_from_path`). Semantics
  pinned by the af-spike shp-* one-toggle probes against Affinity's own
  renders (star 0.11 / triangle 0.05 / smooth polygon 0.10 RMSE, rects ~0);
  committed fixture tiny-shapes.af +
  `af_imports_parametric_shapes_as_shape_layers` (unlocked mixed corners,
  locked single-radius, ellipse, and the star as a real shape layer).
  The 2026-07-29 sweep added the long tail, each pinned against Affinity's
  own Convert-to-Curves geometry (af-spike shape-curves*.json; every kind
  authored via the JS bridge at several parameter settings) and scored at
  rmse ~0-2 against Affinity's renders: `ShpD` diamond (`Pos ` side-vertex
  height), `ShTz` trapezoid (top edge from `PosL` to `PosR`), `ShPi` pie
  (`AngS`/`AngE` radians in y-up math orientation, filled clockwise
  on-screen from AngE to AngS; `IRad` cuts an annulus), `ShSg` segment (the
  ellipse clipped to the band 2*Pos0-1..2*Pos1-1 along the `Angl`
  direction), `ShCr` crescent (two boundary cubics through (0,+/-1) bulging
  to `ArcL`/`ArcR`; endpoint handle (K*m, (1-|m|)/3), mid handle
  K*|m|+(1-|m|)/3 - ellipse half at |m|=1, straight line at 0), `ShHt`
  heart (fixed six-anchor template, `Sprd` = cleft depth), `ShTr` tear
  (ellipse with the top anchor collapsed to a corner at `Tail`; bottom bulb
  radius caps at half the width; `Curv` scales the upper handles, `Bend`
  curls the tip; `Fixd`/`Ball` are 3.x no-ops), `ShDA` arrow (straight
  polygon; head length `LPr1`/`RPr1` of the box HEIGHT, clamped
  proportionally when heads would overlap; `Thck` shaft; `LPr2`/`RPr2`
  barb offsets; end styles 0 flat / 1 arrowhead only, `LSty`/`RSty`),
  `ShDS` double star (4*`Pnts` vertices cycling radii 1/`IRad`/`PRad`/
  `IRad`), `ShSS` square star (flat-tipped arms; outer corners at
  (cos h, (1-`COut`)*sin h) tip-local, h = pi/Side), `ShCg` cog (tooth-top
  arcs `TtSz` and root arcs `NtSz` of the period at radii 1/`IRad`,
  straight flanks bowed by `Curv` (approximate), `Hole` centre ellipse as a
  second subpath), and `ShCl` cloud (`Bubl` bubbles; each half-bubble is a
  quarter-ellipse in the bump frame, radial semi-axis 1-IRad*cos(P/2),
  tangential IRad*sin(P/2)). Curved-edge stars (`Lgcy` true) take
  tangential tip handles scaled by `CrvL`/`CrvR` (pinned at 0.4; CrvL/CrvR
  without Lgcy are geometry no-ops). Committed fixture tiny-shapes-2.af +
  `af_imports_long_tail_parametric_shapes` (whole-fixture rmse 1.7 vs
  Affinity's own export). Still placeholders: callouts (`ShCE`/`ShCR`
  classes), spiral (`ShSp`), QR, circle-rounded stars (`CrcI`/`CrcO`), and
  arrow end styles beyond flat/plain.
- **Compound shapes (`Comp`)** import as ONE exact shape layer: Affinity
  bakes the already-booleaned result poly-curve into the node's own
  lowercase `crvs` field (same `PCvD` -> `Data` layout as PCrv's `Crvs`),
  and the node carries its own BFFl/LILn/LIFl paint. The per-child `ComO`
  operation enum stays unmined; when the baked path is missing the children
  still import as a group of operand shapes (subtract operands render
  opaque - the pre-July-2026 behavior). snes-box-a3 dropped 43.6 -> 33.7
  RMSE on this alone.
- **Text (`TxtA` artistic / `TxtF` frame)** imports as real Patchy text
  layers with **per-run styles**: the reader extracts each story (text with
  U+2029/U+2028 breaks, per-run font/size/weight/italic and brush-fill color
  from the `GlAS` glyph runs, paragraph alignment from the `PaAS` runs, the
  `TxtH` frame box, transform scale folded into every run's size) into the
  standard `patchy.text.*` metadata - mixed styles emit `patchy.text.runs` +
  `patchy.text.html` through the shared PSD serializers (public header
  `psd/psd_text_runs.hpp`), so imported text is fully editable in the
  Character panel and round-trips through the PSD writer - plus `patchy.af.*`
  placement markers; `MainWindow::render_pending_af_text_layers` renders
  post-open through the internal text pipeline (the SVG import pattern; same
  three call sites), anchoring the first line on the tallest run's ascent.
  Run boundaries: `GlAR.Indx` is the run's END (exclusive) counted in Unicode
  CODEPOINTS of the block text including its trailing NUL (pinned by the
  emoji fixture tiny-text-runs.af); sparse run items inherit the previous
  run's unset fields. Paragraph space-before/after (`PAtt` `Doub[5]`/`[6]`,
  document px) imports as paragraph-run v2 metrics (the leading paragraph
  keeps only its space-after). Paragraph indents (pinned July 2026 by the
  text-indent probe doc) ride the same array and import as the v2 indent
  metrics: `Doub[2]` = left indent, which positions CONTINUATION lines only,
  `Doub[3]` = right indent, `Doub[4]` = first-line indent, absolute from the
  column edge with negatives clamped to 0 at render. A left indent alone is
  therefore a hanging indent (tips.af's numbered lists; Affinity's PSD
  conversion turns PS StartIndent 24 / FirstLineIndent -24 into wire 24 / 0),
  and the importer re-expresses the first line in the PS/Qt relative
  convention (`first_line_indent = clamp0(Doub[4]) - Doub[2]`). Identified in
  the same array but not imported: `Doub[0]` relative-leading fraction,
  `Doub[7]` default tab stops (36), `Doub[8..10]` word-spacing
  min/desired/max (0.8/1.0/1.33). The All Caps attribute (the private
  `'CAP\x01'` OpenType feature setting in the item's `OtAt.Setn`) uppercases
  the imported text (ASCII + Latin-1); the small/petite-caps family
  (smcp/c2sc/pcap/c2pc/titl/unic) renders as typed with a notice. Frame text
  wraps via the box flow with its cap at the frame top (pinned against
  Affinity's render); a line straddling the frame bottom draws whole, matching
  Affinity (the shared boxed-clip rule in [text-tool.md](text-tool.md) —
  tips.af's last line was cut mid-glyph until July 2026). Affinity's default line pitch measures as the natural
  font leading plus COLLAPSED paragraph margins (max of space-after/next
  space-before) - exactly Qt's model, so no leading translation is needed
  (the once-suspected `PAtt Doub[10]` = 1.33 is the max word-spacing bound,
  not a line-pitch multiple).
  Runs metadata also emits for single-style text that carries paragraph
  layout: block alignment/spacing only apply on the rich-runs render path
  (the html body is a single <p> with <br/> breaks). Rotated/sheared
  ARTISTIC text renders exactly: the importer keeps the raw box and wire
  sizes and carries the full node Xfrm in `patchy.af.text_xfrm`; the
  post-open pass composes the local anchor with the affine, renders through
  `render_text_layer_pixels_through_transform`, and stamps the standard
  `patchy.text.transform` so later edits stay transform-aware. Non-normal
  font width classes (`DFnt Widh` != 5) resolve the display family from the
  PostScript name when it extends the wire family ("Arial" + Widh 3 ->
  "Arial Narrow"); face-specific wire families pass through untouched.
  Approximations (notice where user-visible): rotated/sheared FRAME text
  still renders axis-aligned (the box-flow renderer has no transform path).
- **Layer effects (`FiEf`)** import into `Layer::layer_style()` for the kinds
  Patchy models: outer/inner shadow (`Shad`/`InnS`; wire `Angl` is the
  direction the shadow FALLS, screen-clockwise from +x, so the PS light angle
  is 180 - deg), outline (`Strk`; `Alig` 0 outside / 1 centre / 2 inside;
  `Ftyp` 2 = gradient fill via the `GrFl` descriptor), colour overlay
  (`ColO`), gradient overlay (`GrdO`; stops, type - FilG Type 0 linear /
  1-2 radial / 3 conical -> Angle - and placement from the descriptor's
  `FDeX` [a,b,tx,c,d,ty] transform: the base gradient runs left->right, so
  the PS angle is atan2(-c, a) and hypot(a, c) the span scale), outer/inner
  glow (`OutG`/`InnG`;
  `Cntr` = centre source), Bevel/Emboss (`BevE`; `Beve` 0 inner / 1 outer /
  2 emboss / 3 pillow, `Azim`/`Elev` radians in the PS light convention,
  `Dept` px maps to PS depth as Dept/Radi; notice-approximate) and the 3D
  Phong bevel (`PhgB`; notice-approximated as a smooth inner bevel lit by its
  first `PLig` light). The effect `BlnM` enum is its OWN space (NOT the layer
  `Blnd` enum): base ids 0..21 with LATER-ADDED modes reusing ids under an
  enum-version bump (LinearBurn 5/v3, LinearLight 15/v1, Divide 21/v4) -
  table in `map_effect_blend_mode`. The Gaussian blur effect (`Gaus`) is a
  content blur PSD cannot store, so it BAKES into the layer pixels at the end
  of the read (`bake_pending_blur_effects`; the read-time-only
  patchy.af.pending_blur marker never survives into the document): the layer
  is padded with transparency so its alpha boundary softens and the bounds
  grow with the spill, the effect opacity blends back over the sharp
  original, and `PrAl` (Preserve Alpha) keeps the original coverage. The
  wire `Radi` measures 2 sigma on Affinity's renders while the
  PS-calibrated kernel radius is ~1 sigma, so the bake halves it
  (edge-profile fit of the fx-gaussian probe; rmse 0.48, and the committed
  tiny-fx-blur.af scores 0.46). Blur on pending TEXT layers keeps a notice
  (text renders post-open, after the bake). Unknown effect kinds skip with
  a notice; group effects import AND render (July 2026, through the same
  calibrated group pipeline as PSD group styles).
  Semantics pinned by the authored one-toggle docs in af-spike/corpus/fx-*
  (`author_fx_text.py`); outlines/shadows/overlays score RMSE ~0-2 against
  Affinity's own renders, bevels/glows are approximate by design.
- **Adjustment layers**: the eight kinds Patchy models import as real
  adjustment layers with their mask planes - Curves (spline control points;
  a corpus photo doc with heavy Curves renders pixel-identical, RMSE 0.00),
  Levels (master + per-channel), Invert, Threshold, Posterize,
  Hue/Saturation (wire `HueA` is turns, 1:1 with the visual shift),
  Brightness/Contrast and Colour Balance (both notice-approximate: the
  engines' math differs; Affinity's colour-balance full-scale maps to about
  a tenth of the PS range). Other adjustment kinds and live filters keep the
  placeholder path, and their clipped children (Affinity nests them in the
  node's `Chld` list) still import above the placeholder. Adjustment node
  tags follow the `*RA` suffix convention (ancestor chain `*RA/AdjR`); live
  filters all share the ONE concrete tag `FlRN` (FilterRasterNode) with the
  filter kind in its `Filt` child class (`is_adjustment_or_filter` matches
  both, so live filters get the honest placeholder notice, not the
  "unsupported pixel format" one; pinned by tiny-live-filter.af, whose
  Gaussian Blur stores UI radius 3 as `Filt` -> `Radi` 9.0). Placement note:
  `BitI` is the bitmap's used/dirty sub-rect, NEVER a placement source -
  untransformed layers sit at the origin, translated/transformed ones go
  through `Xfrm`.
- **Crop containers with group children become masked wrapper groups** (July
  2026): a painted `ShpN`/`PCrv` node with children is Affinity's crop-to-shape
  construct. Flat children keep the clipped-siblings model, but a child that is
  itself a GROUP can never be clipped (Photoshop's rule), so before this the
  run rendered unclipped and ignored the container's visibility - the wild
  Quintavius trading-card template (ten stacked hidden "* Preview" rect
  containers, one card group each) rendered the topmost HIDDEN card instead of
  Affinity's single visible one. Such containers now import as an ISOLATED
  (Normal) wrapper group named after the node, carrying the node's
  visibility/opacity/blend/effects and mask adjuncts, the shape's own paint as
  the bottom child, the children above it (their own Visi kept), and the shape
  outline baked into a raster group `LayerMask` (`default_color` 0) - the
  artboard/Erase construction, chosen over a group vector mask because folder
  records round-trip raster masks through PSD while group `vmsk` blocks are
  never written. A rare adjunct-occupied mask slot keeps the crop as a vector
  mask instead (renders identically; only a PSD save loses the crop then).
- **Vector curves (`PCrv`)** import as real Patchy shape layers (the SVG
  pattern: `VectorShapeContent` + baked pixels via `update_vector_shape_raster`,
  block-dirty so PSD saves regenerate). Wire: `Crvs` -> `PCvD` -> `Data` (an
  untagged inline class) = [u8 version][u32 subpath count] then per subpath
  [bool closed][point curve-array]; each 18-byte record is x f64 LE, y f64
  LE, u16 flags (0x0001 corner anchor, 0x0002 smooth anchor, 0x0100 the
  previous anchor's control-out, 0x0200 the next anchor's control-in; closed
  subpaths repeat anchor 0 at the end). All subpaths share one shape group:
  Affinity fills a poly-curve even-odd (nested same-winding rects cut a
  hole), exactly Patchy's within-group rule. Fill = `BFFl` FDsc (solid/
  gradient/none, single class or one-element list), stroke paint = `LIFl`
  with width from `LILn -> LDeL` (the field's value IS the `LSty` class,
  `Wght`). The `LSty` `Data` blob is a curve12_t (one f64 + four u8 bytes);
  byte 2 of the four is the stroke panel's line style, pinned July 2026:
  0 None (stroke defined but not drawn), 1 Solid, 2 Dashed (`Patn` values
  are stroke-width multiples like Patchy's dash entries, `Phse` the offset),
  3 textured brush (approximated as solid + notice). `LDSa` on the LDsc is
  the alignment: 0 Center, 1 Inside, 2 Outside. `LDSc` scales the width by
  the object transform's uniform scale. The node `CnML` maps to the miter
  limit (observed 4.0 everywhere, matching the SVG default). Cap and join
  keep Patchy defaults: their wire enums are unmined because every observed
  file carries the identical default tuple. The node `Xfrm` applies as a
  full affine (no axis-aligned approximation).
  Probe scores: rect/donut 0.00 RMSE, ellipse 1.6, stroked rect 3.5; the
  all-vector snes corpus doc dropped 196 -> 46 (the rest is rotated text).
  Compound-shape (`Comp`) booleans import their baked result path (see the
  Compound shapes bullet above); only baked-path-less compounds fall back to
  a group of their children.
- **Honest degradation (notice + named empty layer)**: undecodable vector
  curves, unmapped adjustment kinds and live-filter nodes (their bitmap is
  a mask plane, not content), and text whose story shape is missing. These
  keep their name and position in the tree so the structure survives, but are
  not rendered. If NOTHING in a document decodes to pixels or pending text,
  the importer prefers the tier-0 embedded preview over an all-placeholder
  blank canvas.
- **Blend enum -> Patchy `BlendMode`**: layer `Blnd` and effect `BlnM` share
  ONE versioned wire enum (`map_blend_mode` in af_document_io.cpp). The
  version-0 base table is 0 Normal, 1 Darken, 2 Multiply, 3 ColourBurn,
  4 Lighten, 5 Screen, 6 ColourDodge, 7 Add, 8 Overlay, 9 SoftLight,
  10 HardLight, 11 VividLight, 12 PinLight, 13 HardMix, 14 Difference,
  15 Exclusion, 16 Subtract, 17 Hue, 18 Saturation, 19 Luminosity, 20 Colour,
  21 Average, 22 Negation, 23 Reflect, 24 Glow, 25 Erase; later modes REUSE
  ids under an enum-version bump (DarkerColour 2/v1, LighterColour 6/v1,
  LinearLight 15/v1, ContrastNegate 28/v2, LinearBurn 5/v3, Divide 21/v4),
  and version >= 6 renumbers the space to the JS-facing BlendMode table
  (Pigment = 1/v6). Pinned by blend-sweep-v0.afphoto (a Photo 2.6 document
  with one layer per blend-dropdown entry, af-spike/v2_corpus) plus the
  fx-blend-sweep effect renders. The importer originally fed wire ids through
  the JS table alone, which misread every non-Normal version-0 layer -
  2.x documents AND 3.x PSD conversions (fixing it dropped the deko corpus
  doc from 131 to 32 RMSE and tlm-main-mockup from 33.5 to 2.0). An earlier
  record had 23 as Glow with Reflect at 24/v2; the 2026-07-29 JS probe
  (BlendMode.Reflect writes 23/v0, BlendMode.Glow 24/v0, matching the
  dropdown order of the sweep layers) pins the correct pairing. Absent
  `Blnd` = Normal; unmapped values approximate or fall to Normal + notice
  (see the Approximate bullet above).
- **Container**: little-endian; u16 container version (verified 7..12; newer
  versions still attempt the import plus a warning notice), "#Inf" block
  (stream-table offset, thumbnail offset, timestamps), "Prot" protocol tag,
  a "#FAT"/"#FT2"/"#FT3"/"#FT4" stream-table chain naming streams (doc.dat =
  the serialized document tree, d/<hex> = 64 KiB raster tiles, edc/<n> =
  embedded documents), per-stream compression byte (raw/zlib/zstd + byte or
  u16 delta predictors) and CRC32 (checked; mismatch = notice, not failure).
  The #Inf offset points at the NEWEST chain link and next_offset walks to
  OLDER save revisions, so stream resolution is two-phase with the head link
  winning per stream (regression 2026-07-20: a one-pass walk imported an
  incrementally-saved document's OLDEST doc.dat - stale text styles, missing
  effects; `af_head_fat_revision_wins` pins the fix on a spliced two-link
  fixture built by af-spike/make_incremental_fixture.py).
  zlib inflate reuses the vendored miniz; zstd uses the vendored decode-only
  `src/formats/zstd/zstddeclib.c` (Zstandard 1.5.7, BSD-3; NOTICE entry).
  The preview decoder is a deliberately minimal PNG reader (8-bit gray/RGB/
  RGBA, non-interlaced - the only variants Affinity writes) so the module
  stays Qt-free and core tests exercise the whole path.
- **Document tree** (`af_tree.{hpp,cpp}`): a schema-less parser for the tagged
  object graph. Fields carry a leading type byte that fully determines their
  layout (primitives, vectors, enums, strings, sized structs, nested/shared/
  linked classes), so the whole tree parses without class semantics and the
  importer queries fields by 4CC (`af::tag4`). Two v3 wire quirks vs the
  2020-era afread: class-type headers carry a u16 version (afread read u32),
  and some fields the old reader treated as mandatory are optional. Bounded
  against hostile input (class/field/recursion/array caps).
- **Robustness**: every offset/length is bounds-checked through
  `LittleEndianReader`, stream and layer sizes capped, table/tree chains capped,
  and `af_read_survives_truncation_sweep`/`af_read_survives_mutation_sweep`
  pin no-crash behavior on hostile input. Fixtures under `test-fixtures/af/`
  are self-authored (NOTICE entry): the `tiny-*.af` set via scripted Affinity
  3.x (regenerate through `testy/affinity_js.py`, the token-free MCP/JS
  client), the `tiny-v2-*.afphoto` set interactively in Affinity Photo 2.6.5
  (no scripting API in 2.x; ground-truth PNG exports and the blend-sweep
  probe live in af-spike/v2_corpus).
- **Legal record**: the format is proprietary and undocumented; Serif/Canva
  publish no spec or public SDK. Patchy's knowledge comes from byte-level
  observation of documents authored with the licensed Affinity install on this
  machine, the MIT-licensed afread project's notes on the 2020-era container,
  and the BSD-3-licensed JSLib scripting sources that ship inside the Affinity
  install (read as licensed source, used as the semantic map) - never from
  disassembling Affinity binaries, mirroring the PSD "clean by method" rule.
  The Affinity Terms (canva.com/policies/affinity-additional-terms/, section
  12, reviewed 2026-07-20) prohibit copying/deriving the software's code with
  express carve-outs for rights that cannot be excluded by law and for bundled
  open-source components; file formats are not addressed. Never commit
  Canva-authored sample files (the MSIX JSLib test documents stay local).

## Layered documents and flat formats (the Photoshop-style save guard)

A document opened from a flat format (JPEG, PNG, ...) that has since grown structure a flat save
would discard must never silently flatten back over its file. The policy copies Photoshop:

- `flat_save_discards_layers(document)` (main_window.cpp) is the "format cannot store the
  document's features" test: more than one layer, any group/adjustment/text/smart-object layer,
  a hand-authored mask, or layer styles. A single pixel layer whose only mask carries the
  document-alpha marker stays exempt (that mask IS the flat file's alpha plane).
- `save_extension_preserves_layers()` lists the formats the guard skips: psd/psb, aseprite/ase
  (layered writers), and ico/cur, which are exempt on purpose: a multi-size icon lives as one
  hidden "WxH" layer per size that its writer round-trips, so every icon save would otherwise
  false-positive.
- **Save** on a layered flat-backed document routes to **Save As** instead of writing, and Save
  As defaults the file name and filter to `.psd` (keeping the base name) whenever the document
  is layered and its current path is not layer-capable.
- Explicitly choosing a flat format in Save As (or any direct `save_document_to_path` call)
  raises `flattenLayersMessageBox` (default Cancel) and, on confirm, performs a **save-a-copy**:
  the flat file is written but the session keeps its path, title, and modified state, so closing
  still prompts and the next Save still offers PSD. Save As pre-confirms before the format
  options prompt and passes `flatten_confirmed` so the box appears once.
- Exception: a **linked smart-object child** session keeps real-save semantics after the same
  warning (the linked file on disk IS that document, and the parent refresh needs the write).
- Coverage: `ui_save_layered_flat_format_routes_to_save_as_with_psd_default`,
  `ui_flat_save_of_layered_document_warns_and_saves_copy`.

## Shared writer helpers

`formats/document_flatten.{hpp,cpp}`: `flatten_document_rgba8` (masked-aware: a document-alpha layer exports non-destructively), `indexed_flatten_for_palette_mode` (document palette in file order, exact-then-LUT, appended transparent slot: the PNG-8 semantics), and `indexed_flatten_quantized` (median-cut fallback for RGB docs; GIF + ILBM share it).

Everything except PSD/Aseprite flat-exports through `write_flat_image_file`, which also applies `ImageSaveOptions::export_scale` (nearest-neighbor 1-8x, EXPORT flow only: the combo persists its own `saveOptions/exportScale` key precisely so Save/Save As option defaults can never pick a stale scale up; `scaled_flat_document` keeps the doc-alpha mask structure and palette metadata so every writer path stays faithful).

## PSD adjustment layers and clipping masks

- **Curves presets (`.acv`)** use `formats/acv_curves_io`: explicit big-endian fields, 2 to 19 ordered byte-coordinate points per curve, and output-before-input point pairs. The reader accepts the documented version 4 counted shape, the legacy version 1 bitmap shape, and Photoshop 2026's version 1 32-bit bitmap plus indexed `Crv ` extension. RGB import maps Composite, Red, Green, and Blue in that order and validates any additional records. Export writes Photoshop 2026's version 4 five-curve RGB shape, including its trailing identity compatibility curve. The Curves dialog's Load and Save buttons use this parser/writer; native PSD Curves shares its version 1 body parser.
- Imported adjustment layers get canvas-sized bounds (Photoshop writes their records with an empty rect; empty bounds render as unbounded but starve rect-based canvas/undo invalidation, so the reader normalizes them to Patchy's authored-layer convention).
- Every modeled adjustment kind reads and writes a native Photoshop block: Levels, Curves, Hue/Saturation, Color Balance, Invert, Posterize, Threshold, and Brightness/Contrast (`levl`, `curv`, `hue2`, `blnc`, `nvrt`, `post`, `thrs`, `brit`). A native block overrides a stale `plAD` on load, and fresh saves emit native blocks only: writing the private `plAD` key beside them makes Photoshop show an unknown-data warning on open. `nvrt` has an empty payload (Invert has no settings; any payload length is accepted on read, and `photoshop-invert.psd` pins the import, byte-exact composite match, and round trip). `post` and `thrs` are 4 bytes each (u16 value + 2 zero pad bytes); unedited imported payloads re-emit byte-for-byte, an edit regenerates the 4-byte shape. Posterize models levels 2-255 (the destructive dialog keeps its historical 2-16), Threshold models level 1-255 (destructive keeps 0-255); both share their pixel formula with the destructive filters via core/adjustment_layer (`posterize_channel_value`, `threshold_luminance`), so the adjustment rendering is Patchy's calibrated-destructive math, not a claim of byte-identical Photoshop output for Posterize. `photoshop-posterize.psd` (levels 6) and `photoshop-threshold.psd` (level 96, masked) pin import, parameter recovery, and round trips.
- Color Balance (migrated to native `blnc` July 2026 after a plAD-only layer opened in Photoshop as an opaque white NORMAL raster and triggered the unknown-data warning): the 20-byte payload is shadows i16 x3, midtones i16 x3, highlights i16 x3 (cyan/red, magenta/green, yellow/blue each), preserve-luminosity u8, pad u8. Patchy models the MIDTONES triple only and writes patch-in-place: an edit rewrites the six midtone bytes and keeps the imported shadows/highlights/preserve-luminosity bytes; fresh layers write PS's midtones-only zero template with preserve luminosity off. Files carrying nonzero shadows/highlights or preserve luminosity get an import notice ("preserves but does not render"). Rendering stays Patchy's flat per-channel shift, an approximation of Photoshop's tonal-weighted midtones math. Legacy `plAD`-only Color Balance files still read and migrate to `blnc` on save (old builds then see an opaque preserved layer, the Curves-migration trade). Fixtures: `photoshop-color-balance.psd` (midtones only) and `photoshop-color-balance-full.psd` (all ranges + preserve luminosity).
- Brightness/Contrast: legacy-mode PS 2026 writes ONLY the 8-byte `brit` (brightness i16, contrast i16, mean u16 = 127, lab u8 = 0, pad u8 = 0); modern mode writes an all-zero compatibility `brit` plus a `CgEd` descriptor (version 16, class "null", items Vrsn=1, Brgh, Cntr, means=127, "Lab "=false, useLegacy, Auto=false — 'means' and 'useLegacy' are stringIDs, the rest charIDs). On read a parseable `CgEd` is authoritative over `brit`, and BOTH algorithms are modeled: `use_legacy` rides `BrightnessContrastAdjustment` with the mode's own ranges (modern -150..150 / -50..100, legacy -100..100 both; brit-only files load legacy, as do pre-July-2026 Patchy documents whose metadata lacks the flag). New Patchy adjustments default to modern like Photoshop. On save, unedited layers keep their original blocks byte-for-byte; edits regenerate `brit` (values for legacy, all-zero for modern) plus a PS-2026-shape `CgEd` carrying the values and mode ('means'/'Lab '/'Auto' preserved from an imported descriptor, else 127/false/false), except that legacy settings on a file that never carried a descriptor stay brit-only (the historical Patchy output; a dropped-not-regenerated stale `CgEd` would win over `brit` in Photoshop, the lmfx precedent). Both renderers are calibrated within +/-1 (see [ps-compat.md](ps-compat.md) "Brightness/Contrast legacy calibration" and "Modern Brightness/Contrast") and deliberately differ from the byte-pinned destructive `patchy.filters.brightness_contrast`. Fixtures: `photoshop-brightness-contrast-legacy.psd` (30/-20) and `photoshop-brightness-contrast-modern.psd` (40/25, useLegacy=false; its embedded composite pins the modern flatten within +/-1). The native `curv` write shape and its byte-for-byte preservation rules live in [ps-compat.md](ps-compat.md). `photoshop-curves-masked.psd` and `photoshop-curves-clipped.psd` pin native points, masks, clipping, raw preservation, and edited regeneration.
- `plAD` is read-only legacy since July 2026: no adjustment kind writes it anymore. Levels and Hue/Saturation were the last two writers. The unknown key made Photoshop warn about discarding unknown data on every open of a fresh Patchy save (the pinball poster report), and its odd 143-byte payload additionally desynced Photoshop's even-rounded per-layer block walk, turning the rest of that layer record into unknown data as well. Native `levl` and `hue2` carry every modeled value (hue2 includes colorize), so nothing modeled is lost except the Levels dialog's selected channel tab, which only ever rode `plAD`: it still restores from legacy files and resets to composite on fresh saves, matching Photoshop. The frozen v4 layout remains readable for legacy imports (`parse_patchy_adjustment`): kind byte limited to the original four kinds because old shipped parsers read an unknown kind byte as Levels, an optional four-i32 colorize tail after the original 30 i32s, and an optional length-delimited `CRV2` v1 rich-curves tail (a missing, unknown, oversized, or malformed tail falls back to the three legacy Composite anchors). Saving migrates the modeled result to the native block and drops `plAD` on adjustment layers (`should_skip_layer_block`), including any unmodeled private tail, so older Patchy builds no longer receive an editable private fallback after a current save.
- `hue2` preserves the imported raw payload in `unknown_psd_blocks` and suppresses it from raw re-emission via `should_skip_layer_block`. Fresh Patchy layers emit the byte-exact Photoshop fresh-layer template. The six per-hextant band records are MODELED and rendered (four i16 range stops plus an i16 hue/saturation/lightness triple each): they are read into `HueSaturationAdjustment::bands`, written back from the model, and only the undocumented 36-byte trailer stays patch-in-place, so an unedited layer still round-trips byte-identically. Hue is stored -180..180 in the file, 0..360 in the model.
- Hue/Saturation renders through calibrated tables in core/adjustment_layer.cpp: `kColorizeHueInterp` and `kColorizeSaturationScale` for colorize, `kMasterSaturationScale` for the master sliders. Both modes share the byte-quantized lightness stage, the 1530-step hue wheel, and the reconstruction helper. Master applies lightness per channel BEFORE anything else, scales saturation multiplicatively (neutrals are never tinted), and rotates by a whole number of wheel steps. The per-hue-range bands weight their hue/saturation/lightness by a four-stop trapezoid over the pixel's original hue; a band's Lightness collapses chroma toward the max or min channel rather than blending toward white or black. Formulas, provenance, refuted candidates, and the accuracy envelope live in [ps-compat.md](ps-compat.md). The destructive Image > Adjustments path and Affinity `HsRA` import share the same math.
- The layer-record clipping byte round-trips (`Layer::clipped()`; group/divider records always write 0) and clipping masks RENDER: a base pixel layer plus the consecutive clipped siblings above it composite in isolation (`IsolatedClipGroupTarget` in render/layer_compositor.hpp) and merge into the canvas with the base's blend mode and opacity - Photoshop's default "Blend Clipped Layers as Group". Clipping groups composite via `composite_sibling_layers`: every sibling-iteration site must go through it, never a raw children loop, or clipped runs render as independent layers. A clipped adjustment layer adjusts only its group. The `clbl` block is preserved raw (`clbl=false` files render as if true); clipped flags above groups/adjustment layers or at the bottom of a sibling list render unclipped defensively. `.aseprite` saves drop the flag (the format has no clipping concept).
- Photoshop Fill Opacity reads and writes the four-byte `iOpa` layer block. The first byte is the 0-255 value and the other three bytes are padding; authored 100% Fill omits the block. Fill affects base content and adjustment strength but not layer effects, and group Fill is ignored. Color Burn, Linear Burn, Color Dodge, Linear Dodge, and Difference use Photoshop's special Fill blend kernels rather than treating Fill as another master-opacity multiplier. Nondefault-Fill clipping bases record their content coverage separately so effects do not become the clipping shape. Aseprite cannot store this property and Patchy warns before discarding it.
- **Layer origins are preserved verbatim, however far off-canvas** (July 2026): Photoshop composites the off-canvas slice in place, and the reader's old `[-canvas, 2*canvas]` origin clamp silently SHIFTED such content without cropping the pixels (a 2155-tall frame layer at top -780 on a 240-tall canvas rendered a slice 540 px off, and a resave would have baked the shift in). Only a corruption guard remains (`|origin| <= 2^23`, against int overflow in later rect math). `psd_far_offcanvas_layer_keeps_true_origin` pins it.

## Damaged PackBits scanlines

Real legacy PSDs carry blocks of corrupt RLE scanlines, and Photoshop opens them, so the
image-channel readers must recover instead of failing the document. `decode_packbits_scanline`
(psd_descriptor.cpp) is the lenient decoder every layer, composite, and saved-channel row goes
through: a run that overruns the scanline is clipped, a scanline whose data ends early keeps
zeroes for the rest, and the result is always exactly the channel width. Each row's declared
byte count still positions the stream, so a damaged row cannot desync the rows after it. The
readers count recovered rows and `append_damaged_row_notice` turns a nonzero count into one
import notice for the whole document.

Everything else (patterns, brushes, filter effects, ILBM) keeps the strict `decode_packbits`,
whose exact-length contract catches genuine misparses of structures that carry no row table.

Ground truth (a 2017 Dink map PSD, 900x900, 58 corrupt rows in one hidden layer's blue channel):
outside the corrupt band Patchy's per-layer render matches Photoshop 2026 byte for byte; inside it
Patchy recovers strictly more (Photoshop abandons the channel at the first bad row and leaves 103
rows of stale buffer content, while Patchy resyncs on the row table and decodes the 46 valid rows
after the damage). Nothing pins the genuinely corrupt rows because they have no right answer.

## Import notices

Readers report dropped/approximated features via `FormatReadResult::notices` (plain English, like reader error strings: the formats lib is Qt-free). `open_document_path` shows them in the STATUS BAR by default (first note plus a "+N more" suffix); the consolidated `importNoticesMessageBox` popup appears only when `imports/showPsdWarningsAndInfo` is enabled (the same preference that gates the PSD compatibility report; Seth: no info popups by default). Animated GIFs note "first frame only" from the Qt path. Tests that open notice-raising files assert `statusBar()->currentMessage()`; only tests that ENABLE the preference need the REPEATING QTimer dismisser (a one-shot fires during the open-progress phase and the suite hangs; see `ui_import_notices_dialog_shown_when_setting_enabled`).

PSD layer records keep their original blending-ranges payload in `Layer::raw_psd_blending_ranges()`. Patchy semantically decodes Photoshop's native 40-byte RGB shape: Gray, Red, Green, and Blue each contain four This Layer split values followed by four Underlying Layer split values, with an identity fifth transparency pair. Valid RGB ranges render and edit on pixel, adjustment, and folder records. Opening a dialog or changing an unrelated style leaves the imported bytes untouched; a range edit patches the known 32 bytes and preserves the identity tail. Fresh identity settings still write the historical zero-length payload, so the default writer canary remains unchanged. Malformed, partial, non-RGB, and nonidentity-tail shapes remain preview-locked and byte-preserved until the user explicitly replaces them with editable RGB defaults.

A folder's synthetic closing record has its own `raw_psd_group_boundary_blending_ranges()` payload. Photoshop writes a 40-byte identity there, but Patchy has no proven editing/rendering semantics for nonidentity boundary data, so it always remains preservation-only and receives a precise compatibility/import warning. Folder controls edit the visible folder record only. Normal layers, native adjustments, folder records, nested children, and group boundaries all retain exact record association through PSD and PSB writes.

Imported `lfx2`/`lrFX` blocks remain byte-identical until a user edits that layer's style. Satin is parsed, rendered, and editable on rendered layers, including disabled Satin records, and edited Satin regenerates Photoshop's native 12-field `ChFX` descriptor with its non-anti-aliased Linear contour. Photoshop custom contour curves and contour anti-aliasing remain byte-preserved while untouched; the Layer Style dialog and compatibility report warn that editing normalizes them. Group layer effects remain preservation-only because the group compositor does not apply styles; the dialog, compatibility report, and recursive import notice state that limitation. Gradient Overlay and gradient Stroke preserve, render, and edit each color and transparency stop's native `Mdpn` value without changing the private `plFX` version.

## Fixtures and verification

Committed fixtures live under `test-fixtures/<format>/` (provenance in NOTICE-THIRD-PARTY.md: CPython + VS Code icons, Pillow-authored ICO/CUR/TGA/PCX/GIF, Aseprite-CLI-authored .aseprite); synthesized adversarial files are built byte-by-byte in-test. The PSD set includes `photoshop-satin-default.psd` for Photoshop's native `ChFX` shape, `photoshop-layer-style-4a-roundtrip.psd` for the Photoshop-resaved Patchy Satin/midpoint acceptance file, and `photoshop-blend-if-4b-roundtrip.psd` plus `photoshop-blend-if-4b-render.bmp` for native Blend If record and rendering acceptance. Writers were verified with independent decoders (Pillow, Qt, real Aseprite, a from-scratch Python ILBM reader, and Photoshop COM): keep doing that for format changes.

## PSB (large document format) read + write

PSB support threads `Header::large_document` / `WriteOptions::large_document` through psd_document_io: u64 section/layer-info/channel lengths, u32 RLE row byte counts, header version 2, Save As offers `.psb`, and writing a >30k px document as `.psd` errors ("use .psb"; the PSB cap is 300k). Facts pinned against Photoshop 2026 (COM byte-diffs) that the spec gets wrong or omits:

- **Tagged-block length width on read = '8B64' signature OR (PSB and the key is in the documented 8-byte list)** — BOTH rules, not either alone. PS writes 'cinf' as 8B64+u64 in PSBs (not in the spec's list), but PS 2023 also writes 'lnk2' as plain '8BIM' + u64 (spec-list key, no 8B64 signature); honoring the signature alone misreads that length and silently derails the rest of the global block walk (the linked-smart-object regression; `psb_linked_smart_objects_parse_lnke_if_available` pins it). `UnknownPsdBlock::long_length` records each preserved block's WIDTH for re-emit; the writer's upgrade list (`tagged_block_length_is_u64`) = spec set + 'cinf'.
- PS pads the PSB layer-info section to 2 bytes (same as PSD), not 4.
- Old Photoshop writes EMPTY layers (0x0 rect) with zero-length channel data: no payload and no 2-byte compression marker at all. The reader treats a zero-length channel as empty instead of erroring (`psd_zero_length_layer_channels_read_as_empty`; interface_mock2.psd is a real 2018 file with one).
- CMYK-mode documents carry CMYK colors in three places, all converted to sRGB through ONE shared path so effect/text colors keep their relationship to the converted pixels: pixel channels (stored inverted), lfx2 effect colors as 'CMYC' descriptors of ink percentages (`descriptor_rgb_color`), and text engine `/FillColor << /Type 2 >>` values as 0-1 ink fractions (`rgb_color_from_engine_values`). Missing any of these reads black (the restaurant-menu bug: brown color overlays rendered black). When the file embeds a usable CMYK ICC profile (resource 1039, which real CMYK files almost always do), all three convert through it via the vendored lcms2 core (`CmykToRgbTransform` in src/color/color_management, relative colorimetric + black point compensation, Photoshop's defaults; the `CmykColorConverter` threaded through the descriptor/text parsers quantizes ink fractions to inverted 8-bit and runs the SAME transform as pixels). Without one, the naive ink mix (`rgb = 255*(1-ink)*(1-black)`) is the fallback; no default profile is bundled (Adobe profiles may only ship embedded in image files). The CMYK profile is never promoted into `color_state()` and is stripped from RGB re-exports. Accuracy vs Photoshop's ACE engine: max per-channel delta 2, mean 0.13 over a 20-patch SWOP probe (see ps-compat.md). Known gap: legacy 'lrFX' blocks ignore the color-space id (PS5-era CMYK effect colors read as RGB). Pinned by `photoshop-cmyk-style-colors.psd` (embeds SWOP v2) + `psd_cmyk_document_converts_style_and_text_colors`, `color_cmyk_transform_matches_pinned_swop_values`, `color_cmyk_transform_rejects_garbage_profile`; the profile-less synthetic CMYK tests pin the naive fallback unchanged.
- The default-false PSD paths are pinned byte-identical by `psd_layered_writer_bytes_are_stable` (FNV hash canary; re-pin only for deliberate format changes).

## 16-bit and 32-bit PSD/PSB import (converted to 8-bit)

Patchy's pixel pipeline is 8-bit only, so 16- and 32-bit-per-channel files open by
converting every channel to 8 bits at decode time (`convert_channel_to_8bit` /
`ChannelDecodeInfo` in psd_channel_data.cpp); saving such a document writes an
ordinary 8-bit file, and an import notice states the conversion. Facts pinned July
2026 against Photoshop 2026 (COM-generated fixtures) and a CS4-era 16-bit file:

- **16-bit samples are full-range big-endian u16 (0..65535, NOT the 0..32768
  pattern-resource scale)**; conversion is value/257 with rounding, like the .af
  importer. **32-bit samples are big-endian linear-light floats**: color channels
  clamp to 0..1 and sRGB-encode, which reproduces Photoshop's own default
  (Exposure and Gamma) 32-to-8 conversion exactly on the probe colors;
  transparency, masks, and saved channels scale linearly instead.
- **Deep files keep an EMPTY standard layer-info section; the real layers live in
  the `Lr16`/`Lr32` document-global tagged block** (same body as layer info,
  starting at the layer-count i16 with no length prefix; `read_layer_info_records`
  parses both). The block is consumed, never preserved: re-emitting a stale deep
  layer block (or `Mt16`/`Mt32` merged-transparency data) from a converted 8-bit
  save would mislead Photoshop. An 8-bit file with an empty standard section and a
  `Layr` block parses the same way. The legacy top-to-bottom order heuristic is
  skipped for these blocks (legacy Patchy never wrote them). The
  merged-transparency flag is the sign of the block's layer count; the
  prefer-flat-composite path walks to it too
  (`read_merged_transparency_flag_and_skip_layer_mask`).
- **Layer channels decode zip (2) and zip-with-prediction (3)** in addition to
  raw/RLE; miniz is compiled into patchy_psd for the inflate. Prediction undo:
  16-bit rows are running sums over big-endian u16; 32-bit rows are byte-delta
  decoded then unshuffled from four per-row byte planes (MSBs first) back into
  interleaved floats; 8-bit rows are plain byte deltas. Photoshop writes
  zip-with-prediction for non-empty deep layer channels.
- **Composite-section RLE rows are plain PackBits at every depth, NO prediction**
  (empirically pinned; a predicted decode of a real 32-bit composite yields
  garbage). Photoshop writes raw or RLE composites for deep files; zip composites
  stay rejected. A deep file saved without Maximize Compatibility carries an
  all-white composite (Photoshop behavior, not a Patchy bug); the layers are
  authoritative.
- Local (untracked) fixtures: `ps2026-{16,32}bit[-flat].psd` (COM-generated:
  left-half fill 135,206,235; center square 255,64,0; the 32-bit expected values
  192,232,246 / 255,137,0 are Photoshop's own 8-bit conversion sampled via COM)
  and `Flat-filter-list.psd` (CS4-era 16-bit, raw composite + zip-pred Lr16; the
  test cross-checks Patchy's flatten against the file's own composite). Tests:
  `psd_16_bit_*`, `psd_32_bit_*`, `psd_photoshop_16_bit_fixtures_load_if_available`,
  `psd_photoshop_32_bit_fixtures_load_if_available` in psd_core_io_tests.cpp.

## Saved PSD channels and flat-image alpha

PSD/PSB saved alpha and spot channels are ordered, full-canvas `DocumentChannel` planes. They are not layer masks and do not change the normal composite. Names, Unicode names, alpha identifiers, display records, ordering, uniform planes, and pixel values round-trip through the final image-data section; see [channels.md](channels.md) for the model and UI rules. Photoshop's identifier resource skips spot channels.

- A negative layer count structurally marks the first extra composite plane as merged transparency. The reader never exposes that plane in the Channels dock. Every remaining plane after the source color components becomes a saved channel, regardless of its name.
- Layered writes emit RGB, optional merged transparency (present only when the layered composite has transparent pixels), then every document channel. The header's total stays at or below 56; excess data is an error, never silently discarded. Opaque documents with no saved channels keep the historical byte-stable writer path.
- Image resources 1006 (legacy Pascal names; ASCII fallback with one `?` per non-ASCII character), 1045 (Unicode names, authoritative), 1053 (identifiers for saved alpha channels only: Photoshop skips merged transparency and spot channels, so import and export advance its index only for alpha channels), and 1007/1077 (display information) travel in the same order as their planes. Opaque display records travel with their channel so reordering does not detach spot metadata from its pixels. Imported spot display records remain opaque-preserved while spot editing is unavailable.
- Raster layer masks stay layer channel `-2`, including masks that originated as flat PNG/BMP/TIFF/TGA/WebP alpha. The old PSD `"Alpha 1"` name heuristic and marked-mask promotion are not used for layered PSD saves. Group masks ride the folder divider record the same way (mask-data block plus `-2` channel, matching Photoshop's own layout); a mask-less group keeps its historical zero-channel record.

A non-PSD flat image's meaningful per-pixel alpha still becomes an editable grayscale layer mask through `patchy::ui::promote_flat_alpha_to_layer_mask`. It fires only for one ordinary pixel layer, skips uniform alpha, and refuses text and smart-object layers. `kLayerMetadataDocumentAlpha` remains the non-destructive flat-export marker: `document_alpha_rgba8` keeps covered RGB values intact when writing formats with one alpha plane. PSD imports bypass this promotion because their saved channels are decoded directly.

Layered saves with canvas transparency continue to write Photoshop's merged-alpha shape: straight RGB plus coverage, resource name `"Transparency"`, and a negative layer count. `psd_layered_write_keeps_merged_transparency_in_composite` and the real Content.psb regression ensure it never becomes a phantom saved channel or layer mask.
