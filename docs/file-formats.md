# File formats: registry, per-format quirks, PSB, document alpha

Deep reference for file-format work. The cross-cutting rules (how to add a format, the filter table, import-notice behavior, byte-stable serialization) also appear in AGENTS.md; read this before touching a reader/writer, PSD/PSB internals, or alpha/mask import.

## Registry and dispatch

- **FormatRegistry**: `builtin_format_registry()` (format_registry.cpp, function-local static) is the single instance; `load_document_from_path` (main_window.cpp) consults it BEFORE the QImageReader fallback (a registry read that throws still falls back to Qt where a Qt plugin exists, but the REGISTRY error is reported when Qt fails too: it names the real problem). Handlers may be read-only (`write == nullptr`) and may carry a `sniff` content check (used to disambiguate `.ase`: Aseprite magic 0xA5E0@4 vs Adobe `ASEF` swatches: the Aseprite reader throws a message pointing at the Palette panel for swatch files).
- **One filter table**: `file_format_entries()` in main_window.cpp generates open/save/export filters, `is_supported_image_extension`, `save_file_filter_for_path`, and `path_with_default_extension`. Display names sit in `QT_TRANSLATE_NOOP("QObject", ...)`; update patchy_ja.ts when adding one.
- New formats slot in with one table row + one registry row + one writer branch.

## Per-format catalogue

All read AND write (camera raw below is the one read-only entry); modules in src/formats/, Qt-free, explicit-endian via `binary_le.hpp` (LE) or `psd_binary.hpp` (BE).

- **PSD/PSB** — see the PSB section below and docs/ps-compat.md.
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
extension carry the HEVC patent licenses** (AGENTS.md "Legal constraints" has the rule;
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
- Levels, Curves, and Hue/Saturation adjustment layers read and write native Photoshop blocks (`levl`, `curv`, `hue2`); Color Balance still rides only the private `plAD` block. A native block overrides a stale `plAD` on load. Fresh Patchy Curves emits native `curv` only: writing private `plAD` beside it makes Photoshop show an unknown-data warning. The native payload matches Photoshop 2026's one-byte prefix, version 1 u32 bitmap records, indexed `Crv ` version 4 extension, output-before-input points, and four-byte padding. An untouched imported `curv` payload is emitted byte-for-byte; a point edit regenerates the known Photoshop shape. A malformed or unsupported native block remains opaque and authoritative, so Patchy preserves both raw blocks rather than exposing controls based on a stale private fallback. `photoshop-curves-masked.psd` and `photoshop-curves-clipped.psd` pin native points, masks, clipping, raw preservation, and edited regeneration.
- `plAD` stays version 4 for private adjustment types and legacy imports: Hue/Saturation colorize appends four i32s after the original 30. Historical rich Curves geometry may contain a length-delimited `CRV2` v1 record with ordered RGB/Red/Green/Blue points, while identity and old three-anchor curves have no tail. Current Patchy still reads all of those forms; a missing, unknown, oversized, or malformed `CRV2` tail falls back to the three legacy Composite anchors. Saving an editable `plAD`-only Curves layer migrates the modeled result to native `curv` and drops `plAD`, including any unmodeled private tail, so Photoshop opens it without the warning. Older Patchy builds therefore no longer receive an editable private fallback after a current save.
- `hue2` writes are patch-in-place: the imported payload (preserved in `unknown_psd_blocks`, suppressed from raw re-emission via `should_skip_layer_block`) keeps its per-hextant band records and the undocumented 36-byte trailer byte-identically; only the 16-byte header is rewritten from the model, so unedited layers round-trip exactly. Fresh Patchy layers emit the byte-exact Photoshop fresh-layer template (`kPhotoshopHueSaturationDefaultTail`). Band records are preserved but not rendered. Hue is stored -180..180 in the file, 0..360 in the model.
- Hue/Saturation colorize renders through calibrated tables (`kColorizeHueInterp`, `kColorizeSaturationScale` in core/adjustment_layer.cpp); formula, provenance, and the accuracy envelope live in [ps-compat.md](ps-compat.md).
- The layer-record clipping byte round-trips (`Layer::clipped()`; group/divider records always write 0) and clipping masks RENDER: a base pixel layer plus the consecutive clipped siblings above it composite in isolation (`IsolatedClipGroupTarget` in render/layer_compositor.hpp) and merge into the canvas with the base's blend mode and opacity - Photoshop's default "Blend Clipped Layers as Group". Clipping groups composite via `composite_sibling_layers`: every sibling-iteration site must go through it, never a raw children loop, or clipped runs render as independent layers. A clipped adjustment layer adjusts only its group. The `clbl` block is preserved raw (`clbl=false` files render as if true); clipped flags above groups/adjustment layers or at the bottom of a sibling list render unclipped defensively. `.aseprite` saves drop the flag (the format has no clipping concept).

## Import notices

Readers report dropped/approximated features via `FormatReadResult::notices` (plain English, like reader error strings: the formats lib is Qt-free). `open_document_path` shows them in the STATUS BAR by default (first note plus a "+N more" suffix); the consolidated `importNoticesMessageBox` popup appears only when `imports/showPsdWarningsAndInfo` is enabled (the same preference that gates the PSD compatibility report; Seth: no info popups by default). Animated GIFs note "first frame only" from the Qt path. Tests that open notice-raising files assert `statusBar()->currentMessage()`; only tests that ENABLE the preference need the REPEATING QTimer dismisser (a one-shot fires during the open-progress phase and the suite hangs; see `ui_import_notices_dialog_shown_when_setting_enabled`).

PSD layer records keep their original blending-ranges payload in `Layer::raw_psd_blending_ranges()`. Patchy semantically decodes Photoshop's native 40-byte RGB shape: Gray, Red, Green, and Blue each contain four This Layer split values followed by four Underlying Layer split values, with an identity fifth transparency pair. Valid RGB ranges render and edit on pixel, adjustment, and folder records. Opening a dialog or changing an unrelated style leaves the imported bytes untouched; a range edit patches the known 32 bytes and preserves the identity tail. Fresh identity settings still write the historical zero-length payload, so the default writer canary remains unchanged. Malformed, partial, non-RGB, and nonidentity-tail shapes remain preview-locked and byte-preserved until the user explicitly replaces them with editable RGB defaults.

A folder's synthetic closing record has its own `raw_psd_group_boundary_blending_ranges()` payload. Photoshop writes a 40-byte identity there, but Patchy has no proven editing/rendering semantics for nonidentity boundary data, so it always remains preservation-only and receives a precise compatibility/import warning. Folder controls edit the visible folder record only. Normal layers, native adjustments, folder records, nested children, and group boundaries all retain exact record association through PSD and PSB writes.

Imported `lfx2`/`lrFX` blocks remain byte-identical until a user edits that layer's style. Satin is parsed, rendered, and editable on rendered layers, including disabled Satin records, and edited Satin regenerates Photoshop's native 12-field `ChFX` descriptor with its non-anti-aliased Linear contour. Photoshop custom contour curves and contour anti-aliasing remain byte-preserved while untouched; the Layer Style dialog and compatibility report warn that editing normalizes them. Group layer effects remain preservation-only because the group compositor does not apply styles; the dialog, compatibility report, and recursive import notice state that limitation. Pattern Overlay is also preservation-only. Gradient Overlay and gradient Stroke preserve, render, and edit each color and transparency stop's native `Mdpn` value without changing the private `plFX` version.

## Fixtures and verification

Committed fixtures live under `test-fixtures/<format>/` (provenance in NOTICE-THIRD-PARTY.md: CPython + VS Code icons, Pillow-authored ICO/CUR/TGA/PCX/GIF, Aseprite-CLI-authored .aseprite); synthesized adversarial files are built byte-by-byte in-test. The PSD set includes `photoshop-satin-default.psd` for Photoshop's native `ChFX` shape, `photoshop-layer-style-4a-roundtrip.psd` for the Photoshop-resaved Patchy Satin/midpoint acceptance file, and `photoshop-blend-if-4b-roundtrip.psd` plus `photoshop-blend-if-4b-render.bmp` for native Blend If record and rendering acceptance. Writers were verified with independent decoders (Pillow, Qt, real Aseprite, a from-scratch Python ILBM reader, and Photoshop COM): keep doing that for format changes.

## PSB (large document format) read + write

PSB support threads `Header::large_document` / `WriteOptions::large_document` through psd_document_io: u64 section/layer-info/channel lengths, u32 RLE row byte counts, header version 2, Save As offers `.psb`, and writing a >30k px document as `.psd` errors ("use .psb"; the PSB cap is 300k). Facts pinned against Photoshop 2026 (COM byte-diffs) that the spec gets wrong or omits:

- **Tagged-block length width on read = '8B64' signature OR (PSB and the key is in the documented 8-byte list)** — BOTH rules, not either alone. PS writes 'cinf' as 8B64+u64 in PSBs (not in the spec's list), but PS 2023 also writes 'lnk2' as plain '8BIM' + u64 (spec-list key, no 8B64 signature); honoring the signature alone misreads that length and silently derails the rest of the global block walk (the linked-smart-object regression; `psb_linked_smart_objects_parse_lnke_if_available` pins it). `UnknownPsdBlock::long_length` records each preserved block's WIDTH for re-emit; the writer's upgrade list (`tagged_block_length_is_u64`) = spec set + 'cinf'.
- PS pads the PSB layer-info section to 2 bytes (same as PSD), not 4.
- Old Photoshop writes EMPTY layers (0x0 rect) with zero-length channel data: no payload and no 2-byte compression marker at all. The reader treats a zero-length channel as empty instead of erroring (`psd_zero_length_layer_channels_read_as_empty`; interface_mock2.psd is a real 2018 file with one).
- CMYK-mode documents carry CMYK colors in three places, all converted to sRGB through ONE shared path so effect/text colors keep their relationship to the converted pixels: pixel channels (stored inverted), lfx2 effect colors as 'CMYC' descriptors of ink percentages (`descriptor_rgb_color`), and text engine `/FillColor << /Type 2 >>` values as 0-1 ink fractions (`rgb_color_from_engine_values`). Missing any of these reads black (the restaurant-menu bug: brown color overlays rendered black). When the file embeds a usable CMYK ICC profile (resource 1039, which real CMYK files almost always do), all three convert through it via the vendored lcms2 core (`CmykToRgbTransform` in src/color/color_management, relative colorimetric + black point compensation, Photoshop's defaults; the `CmykColorConverter` threaded through the descriptor/text parsers quantizes ink fractions to inverted 8-bit and runs the SAME transform as pixels). Without one, the naive ink mix (`rgb = 255*(1-ink)*(1-black)`) is the fallback; no default profile is bundled (Adobe profiles may only ship embedded in image files). The CMYK profile is never promoted into `color_state()` and is stripped from RGB re-exports. Accuracy vs Photoshop's ACE engine: max per-channel delta 2, mean 0.13 over a 20-patch SWOP probe (see ps-compat.md). Known gap: legacy 'lrFX' blocks ignore the color-space id (PS5-era CMYK effect colors read as RGB). Pinned by `photoshop-cmyk-style-colors.psd` (embeds SWOP v2) + `psd_cmyk_document_converts_style_and_text_colors`, `color_cmyk_transform_matches_pinned_swop_values`, `color_cmyk_transform_rejects_garbage_profile`; the profile-less synthetic CMYK tests pin the naive fallback unchanged.
- The default-false PSD paths are pinned byte-identical by `psd_layered_writer_bytes_are_stable` (FNV hash canary; re-pin only for deliberate format changes).

## Saved PSD channels and flat-image alpha

PSD/PSB saved alpha and spot channels are ordered, full-canvas `DocumentChannel` planes. They are not layer masks and do not change the normal composite. Names, Unicode names, alpha identifiers, display records, ordering, uniform planes, and pixel values round-trip through the final image-data section; see [channels.md](channels.md) for the model and UI rules. Photoshop's identifier resource skips spot channels.

- A negative layer count structurally marks the first extra composite plane as merged transparency. The reader never exposes that plane in the Channels dock. Every remaining plane after the source color components becomes a saved channel, regardless of its name.
- Layered writes emit RGB, optional merged transparency, then every document channel. The header's total stays at or below 56; excess data is an error, never silently discarded. Opaque documents with no saved channels keep the historical byte-stable writer path.
- Image resources 1006, 1045, 1053, and 1007/1077 travel in the same order as their planes. Imported spot display records remain opaque-preserved while spot editing is unavailable.
- Raster layer masks stay layer channel `-2`, including masks that originated as flat PNG/BMP/TIFF/TGA/WebP alpha. The old PSD `"Alpha 1"` name heuristic and marked-mask promotion are not used for layered PSD saves.

A non-PSD flat image's meaningful per-pixel alpha still becomes an editable grayscale layer mask through `patchy::ui::promote_flat_alpha_to_layer_mask`. It fires only for one ordinary pixel layer, skips uniform alpha, and refuses text and smart-object layers. `kLayerMetadataDocumentAlpha` remains the non-destructive flat-export marker: `document_alpha_rgba8` keeps covered RGB values intact when writing formats with one alpha plane. PSD imports bypass this promotion because their saved channels are decoded directly.

Layered saves with canvas transparency continue to write Photoshop's merged-alpha shape: straight RGB plus coverage, resource name `"Transparency"`, and a negative layer count. `psd_layered_write_keeps_merged_transparency_in_composite` and the real Content.psb regression ensure it never becomes a phantom saved channel or layer mask.
