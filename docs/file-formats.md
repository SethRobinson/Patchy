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
(`patchy.formats.af`, sniff on the magic) with a read-only filter-table row;
only `.af` is claimed, not the older `.afphoto/.afdesign/.afpub` generations
(same container family, deferred to keep the version matrix small).

- **Tier 2 (current)**: parses the serialized document tree (`doc.dat`) and
  builds real Patchy layers - the layer tree (groups nested with pass-through
  by default), each raster layer's full-resolution pixels decoded from its
  tiles and placed by its bounds/transform, plus name, visibility, opacity,
  fill-opacity, and blend mode. The importer builds `Layer` objects and lets
  Patchy's compositor do the blending (it never composites itself), so the
  blend math stays Patchy's calibrated implementation. On any structural
  problem the reader falls back to the **tier-0** embedded-preview layer rather
  than failing the open (env `PATCHY_AF_TRACE=1` prints why the tree walk
  bailed). The reverse-engineered format record, generated corpus, and Python
  reference/verification tooling live in `local-test-fixtures/af-spike/`
  (machine-local; FINDINGS.md there is the running format spec).
- **What imports faithfully**: raster layers in RGBA8/16, Gray8/16, and
  RGBA-float32 (16-bit down-converts value/257, float linearizes to sRGB),
  whether untransformed, translated, or under a full scale/rotate affine
  (`Xfrm` = `[a,b,tx,c,d,ty]`, dest = A*src + t; the importer rasterizes
  through the affine with bilinear premultiplied accumulation, pinned RMSE
  0.003 against Affinity's own render of a rotated+scaled raster); groups
  (nested, pass-through by default); layer masks (the M8/M16 mask plane in a
  node's `AdCh` enclosure becomes a `LayerMask`; transformed masks resample
  through their affine too); clipping (Affinity nests clipped layers inside
  their base's child list; Patchy models them as clipped siblings above the
  base); embedded documents (the `EmbR`/`EmbC` reference to an `edc/<n>`
  nested container is parsed recursively and flattened); placed/opened images
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
  .af channels are straight ink, not PSD-inverted. Blend modes Patchy lacks
  (Pigment, Average, Negation, Reflect, Glow, Erase, ...) map to Normal with
  a notice.
- **Lab documents (LABA16, format 5)** decode natively: the wire is the ICC
  v4 Lab16 PCS encoding (L 0..65535 = 0..100, a/b with 0x8080 = 0), converted
  through lcms2's built-in D50 Lab profile (`LabToRgbTransform`,
  src/color/color_management). Pinned July 2026 against a saturated
  calibration doc at RMSE ~0.5 vs Affinity's own render; the earlier
  "compressed a/b scale" mystery was a desaturated probe document.
- **Multi-page/artboard documents** import the first spread with a notice
  naming the total count.
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
  table in `map_effect_blend_mode`. Gaussian blur and unknown kinds skip with
  a notice; group effects import but do not render (same caveat as PSD).
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
  placeholder path. Placement note: `BitI` is the bitmap's used/dirty
  sub-rect, NEVER a placement source - untransformed layers sit at the
  origin, translated/transformed ones go through `Xfrm`.
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
  `Wght`); cap/join/alignment keep Patchy defaults (approximate) and the
  node `Xfrm` applies as a full affine (no axis-aligned approximation).
  Probe scores: rect/donut 0.00 RMSE, ellipse 1.6, stroked rect 3.5; the
  all-vector snes corpus doc dropped 196 -> 46 (the rest is rotated text).
  Compound-shape (`Comp`) booleans still import as groups of their children.
- **Honest degradation (notice + named empty layer)**: undecodable vector
  curves, unmapped adjustment kinds and live-filter nodes (their bitmap is
  a mask plane, not content), and text whose story shape is missing. These
  keep their name and position in the tree so the structure survives, but are
  not rendered. If NOTHING in a document decodes to pixels or pending text,
  the importer prefers the tier-0 embedded preview over an all-placeholder
  blank canvas.
- **Blend enum -> Patchy `BlendMode`** and the RasterFormat ids are in
  FINDINGS.md; the Affinity `Blnd` field's enum id is the BlendMode value
  directly (absent = Normal).
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
  are self-authored via scripted Affinity (NOTICE entry); regenerate them and
  the machine-local corpus through `testy/affinity_js.py` (the token-free
  MCP/JS client) if the app's format moves.
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
- Every modeled adjustment kind reads and writes a native Photoshop block: Levels, Curves, Hue/Saturation, Color Balance, Invert, Posterize, Threshold, and Brightness/Contrast (`levl`, `curv`, `hue2`, `blnc`, `nvrt`, `post`, `thrs`, `brit`). A native block overrides a stale `plAD` on load. Fresh Patchy Curves emits native `curv` only: writing private `plAD` beside it makes Photoshop show an unknown-data warning. `nvrt` has an empty payload (Invert has no settings; any payload length is accepted on read, and `photoshop-invert.psd` pins the import, byte-exact composite match, and round trip). `post` and `thrs` are 4 bytes each (u16 value + 2 zero pad bytes); unedited imported payloads re-emit byte-for-byte, an edit regenerates the 4-byte shape. Posterize models levels 2-255 (the destructive dialog keeps its historical 2-16), Threshold models level 1-255 (destructive keeps 0-255); both share their pixel formula with the destructive filters via core/adjustment_layer (`posterize_channel_value`, `threshold_luminance`), so the adjustment rendering is Patchy's calibrated-destructive math, not a claim of byte-identical Photoshop output for Posterize. `photoshop-posterize.psd` (levels 6) and `photoshop-threshold.psd` (level 96, masked) pin import, parameter recovery, and round trips.
- Color Balance (migrated to native `blnc` July 2026 after a plAD-only layer opened in Photoshop as an opaque white NORMAL raster and triggered the unknown-data warning): the 20-byte payload is shadows i16 x3, midtones i16 x3, highlights i16 x3 (cyan/red, magenta/green, yellow/blue each), preserve-luminosity u8, pad u8. Patchy models the MIDTONES triple only and writes patch-in-place: an edit rewrites the six midtone bytes and keeps the imported shadows/highlights/preserve-luminosity bytes; fresh layers write PS's midtones-only zero template with preserve luminosity off. Files carrying nonzero shadows/highlights or preserve luminosity get an import notice ("preserves but does not render"). Rendering stays Patchy's flat per-channel shift, an approximation of Photoshop's tonal-weighted midtones math. Legacy `plAD`-only Color Balance files still read and migrate to `blnc` on save (old builds then see an opaque preserved layer, the Curves-migration trade). Fixtures: `photoshop-color-balance.psd` (midtones only) and `photoshop-color-balance-full.psd` (all ranges + preserve luminosity).
- Brightness/Contrast: legacy-mode PS 2026 writes ONLY the 8-byte `brit` (brightness i16, contrast i16, mean u16 = 127, lab u8 = 0, pad u8 = 0); modern mode writes an all-zero compatibility `brit` plus a `CgEd` descriptor (version 16, class "null", items Vrsn=1, Brgh, Cntr, means=127, "Lab "=false, useLegacy, Auto=false). On read a parseable `CgEd` is authoritative over `brit`; modern values (-150..150 / -50..100) clamp into Patchy's legacy model (-100..100 both) with an import notice ("modern algorithm ... legacy semantics"). Fresh and edited Patchy layers write legacy `brit` only, and an edit DROPS a preserved `CgEd` (a stale descriptor would win over the regenerated `brit` in Photoshop, the lmfx precedent); unedited layers keep both original blocks byte-for-byte. The rendering formula is calibrated to PS legacy mode within +/-1 (see [ps-compat.md](ps-compat.md) "Brightness/Contrast legacy calibration") and deliberately differs from the byte-pinned destructive `patchy.filters.brightness_contrast`. Fixtures: `photoshop-brightness-contrast-legacy.psd` (30/-20) and `photoshop-brightness-contrast-modern.psd` (40/25, useLegacy=false). The native payload matches Photoshop 2026's one-byte prefix, version 1 u32 bitmap records, indexed `Crv ` version 4 extension, output-before-input points, and four-byte padding. An untouched imported `curv` payload is emitted byte-for-byte; a point edit regenerates the known Photoshop shape. A malformed or unsupported native block remains opaque and authoritative, so Patchy preserves both raw blocks rather than exposing controls based on a stale private fallback. `photoshop-curves-masked.psd` and `photoshop-curves-clipped.psd` pin native points, masks, clipping, raw preservation, and edited regeneration.
- `plAD` is now written ONLY for Levels and Hue/Saturation (as the private fallback beside their native blocks; `patchy_plad_supports_kind`). Curves and Color Balance migrated to native-only writes, and its kind byte is FROZEN at the original four kinds: old shipped parsers read an unknown kind byte as Levels, so kinds added after v4 (Invert, Posterize, Threshold, Brightness/Contrast, ...) must never be written into `plAD`. All four original kind bytes remain readable for legacy imports. Hue/Saturation colorize appends four i32s after the original 30. Historical rich Curves geometry may contain a length-delimited `CRV2` v1 record with ordered RGB/Red/Green/Blue points, while identity and old three-anchor curves have no tail. Current Patchy still reads all of those forms; a missing, unknown, oversized, or malformed `CRV2` tail falls back to the three legacy Composite anchors. Saving an editable `plAD`-only Curves layer migrates the modeled result to native `curv` and drops `plAD`, including any unmodeled private tail, so Photoshop opens it without the warning. Older Patchy builds therefore no longer receive an editable private fallback after a current save.
- `hue2` writes are patch-in-place: the imported payload (preserved in `unknown_psd_blocks`, suppressed from raw re-emission via `should_skip_layer_block`) keeps its per-hextant band records and the undocumented 36-byte trailer byte-identically; only the 16-byte header is rewritten from the model, so unedited layers round-trip exactly. Fresh Patchy layers emit the byte-exact Photoshop fresh-layer template (`kPhotoshopHueSaturationDefaultTail`). Band records are preserved but not rendered. Hue is stored -180..180 in the file, 0..360 in the model.
- Hue/Saturation colorize renders through calibrated tables (`kColorizeHueInterp`, `kColorizeSaturationScale` in core/adjustment_layer.cpp); formula, provenance, and the accuracy envelope live in [ps-compat.md](ps-compat.md).
- The layer-record clipping byte round-trips (`Layer::clipped()`; group/divider records always write 0) and clipping masks RENDER: a base pixel layer plus the consecutive clipped siblings above it composite in isolation (`IsolatedClipGroupTarget` in render/layer_compositor.hpp) and merge into the canvas with the base's blend mode and opacity - Photoshop's default "Blend Clipped Layers as Group". Clipping groups composite via `composite_sibling_layers`: every sibling-iteration site must go through it, never a raw children loop, or clipped runs render as independent layers. A clipped adjustment layer adjusts only its group. The `clbl` block is preserved raw (`clbl=false` files render as if true); clipped flags above groups/adjustment layers or at the bottom of a sibling list render unclipped defensively. `.aseprite` saves drop the flag (the format has no clipping concept).
- Photoshop Fill Opacity reads and writes the four-byte `iOpa` layer block. The first byte is the 0-255 value and the other three bytes are padding; authored 100% Fill omits the block. Fill affects base content and adjustment strength but not layer effects, and group Fill is ignored. Color Burn, Linear Burn, Color Dodge, Linear Dodge, and Difference use Photoshop's special Fill blend kernels rather than treating Fill as another master-opacity multiplier. Nondefault-Fill clipping bases record their content coverage separately so effects do not become the clipping shape. Aseprite cannot store this property and Patchy warns before discarding it.

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
