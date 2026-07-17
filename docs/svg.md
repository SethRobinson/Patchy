# SVG import and export

Feature deep-dive for the SVG interchange path (July 2026). SVG opens as
editable shape layers and saves/exports with vectors preserved; everything the
format cannot express degrades to embedded raster with an import/export note.
Unlike PSD work, SVG is an open W3C standard: consulting the spec text is fine
(the no-spec-text method rule is Adobe-specific), and there is no byte-pinning
against Photoshop output. No new patent surface: parsing and writing SVG is
not image tracing (which stays excluded per docs/vector-tools.md).

## Code map

All Qt-free, in src/formats/ (patchy_formats):

- `svg_xml.{hpp,cpp}` - minimal XML DOM: elements/attributes, comments, CDATA,
  processing instructions, DOCTYPE with internal-subset `<!ENTITY>` expansion
  (old Illustrator exports reference namespace URIs as `&ns_svg;`), the
  predefined + numeric character references, namespace resolution (SVG/xlink
  collapse to bare local names, `xlink:href` -> `href`, foreign prefixes stay
  verbatim so consumers can skip them), UTF-8/UTF-16/declared-Latin-1 input.
  Hand-written deliberately: lightweight parsers do not expand DTD entities.
  Hard caps: 250k nodes, depth 512, 8 MB entity expansion.
- `svg_document_io.hpp` - public API (`svg::DocumentIo::read/write/write_file`,
  `svg_extensions()` = {.svg, .svgz}, `sniff`).
- `svg_document_read.cpp` + `svg_document_write.cpp` share
  `svg_io_internal.hpp` (Affine helpers, number I/O, and the BlendMode <->
  CSS mix-blend-mode map; the enum switch is append-only). Number I/O is
  deliberately NOT <charconv>: Apple's libc++ marks the floating-point
  overloads unavailable, so formatting is classic-locale %.15g (correctly
  rounded everywhere = deterministic) and parsing is a hand-written
  prefix parser (locale-free, backtracks a bare "e" so "5em" reads as 5,
  exact for the <= 15-digit numbers the writer emits).

UI side: the registry row (format_registry.cpp), the `file_format_entries()`
row, the svg branches in `save_document_to_path` / `export_flat_image`, the
post-open passes (below), `MainWindow::define_custom_shape_from_svg_*`
(main_window_vector.cpp), and `MainWindow::paste_svg_from_clipboard`
(main_window_layer_ops.cpp).

## Import (what maps to what)

- **Order**: SVG paints first-to-last; `layers()[0]` composites first. The
  mapping is identity - no reversal anywhere.
- **Canvas**: physical width/height units (in/cm/mm/pt/pc) -> CSS 96 px/in and
  96 PPI print metadata; unitless/percent/absent -> viewBox user units at the
  untagged-import 72 PPI; neither -> 300x150 (the CSS replaced-element
  default) with a notice. Oversized canvases clamp to 30000 px, scaling
  content. viewBox honors the full preserveAspectRatio grammar. svg is
  deliberately NOT in load_document_from_path's kDensitylessFormats: the
  reader sets its own PPI.
- **Structure**: `<g>`/nested `<svg>` -> Group folders (opacity, blend,
  display:none -> hidden); `<a>` is a transparent container; `<use>` clones
  (cycle-guarded, depth 32; symbol/svg targets instantiate like `<g>`);
  `<switch>` takes the first child whose conditionals pass (requiredExtensions/
  -Features fail when present; systemLanguage passes on an "en" entry). Names:
  id, else `<title>`, else Photoshop-style counters ("Rectangle 1", ...).
- **Styling**: real cascade order - presentation attributes < stylesheet rules
  (type < .class < #id, later wins ties; the flat-selector subset Illustrator
  emits) < inline style. fill/stroke/etc. inherit; opacity, display, and
  mix-blend-mode reset per element. Colors: #hex 3/4/6/8, rgb()/rgba(),
  hsl()/hsla(), the 147 named colors, currentColor. Paint values keep their
  case (url(#SVGID_1_) ids are case-sensitive).
- **Shapes**: rect (+uniform rx) / circle / ellipse -> live shapes; a plain
  stroked line -> the live Line quad with the stroke paint as fill (the
  Photoshop Line construction; dashes or non-butt caps keep it a stroked
  path). Live parameters survive translate + positive axis-aligned scale (the
  keyShapeInvalidated rule; rounded rects also need uniform scale), otherwise
  the exact path remains. Full d= grammar (relative forms, implicit repeats,
  run-together arc flags, quadratics elevated, arcs -> <=90-degree cubics).
- **Fill rules**: evenodd -> one shape group (Patchy's exact within-group
  rule). nonzero (the SVG default) -> winding decomposition: each subpath its
  own Add group, opposite-winding contained subpaths become Subtract groups
  (holes and unions both correct; a self-intersecting single subpath keeps
  even-odd semantics - the one approximation).
- **Opacity**: layer opacity = element opacity x fill-opacity x solid-fill
  alpha; stroke opacity divides that back out (a stroke more opaque than its
  fill clamps, with a notice).
- **Gradients**: linear/radial with href template inheritance, objectBoundingBox
  and userSpaceOnUse units, gradientTransform, stop-opacity. Geometry maps
  onto the calibrated GdFl model (span = center chord; docs/vector-tools.md);
  smoothness = 0 so stops interpolate linearly, matching SVG.
  spreadMethod=reflect -> Reflected (scale doubles; the export halves it
  back). Focal points and repeat spreads are approximated with a notice.
- **Patterns**: shape-only `<pattern>` content rasterizes once into a
  document PatternStore tile -> pattern fill anchored to the document origin
  (pattern_linked = false - exactly SVG's user-space anchoring); richer
  content degrades to gray + notice.
- **clip-path** (userSpaceOnUse, shape children) -> vector mask; **mask**
  (shape children) -> raster mask from fill-luminance-weighted coverage.
- **Text**: basic `<text>`/tspan -> text layers (Pixel kind + patchy.text.*
  metadata, the standard text pattern) with font family/size/bold/italic/
  color; the Qt-free reader stores the baseline point + text-anchor under
  patchy.svg.* keys plus kLayerMetadataSvgPendingText, and
  `MainWindow::render_pending_svg_text_layers` (main_window.cpp - it needs
  the text pipeline) renders and positions them post-open on the main thread.
  textPath/x-arrays/textLength reduce to plain text with a notice.
- **Images**: data-URI PNG/JPEG -> pixel layers; bytes ride
  kLayerMetadataSvgPendingImage and `decode_pending_svg_images`
  (main_window_shared.cpp) decodes on the open worker (QImage decode is
  thread-safe; fonts are not, hence the two-pass split). External file
  references are skipped with a notice.
- **Robustness fallback**: unparseable XML or > 2000 drawables throws; the
  existing QImageReader fallback (the qsvg plugin, shipped by
  build-release.bat) rasterizes, and the open path adds an "imported as
  flattened raster" notice naming the reason. The alpha-promotion pass is
  skipped for svg like it is for PSD (layers own their transparency; it would
  also clobber a lone placed image layer's offset).

## Export (representability rules)

Vector shape layers export as real vectors; a layer stays vector when it has
no styles, default fill opacity, a supported combine structure, and
Linear/Radial/Reflected gradients. Live shapes with one covering origination
emit native `<rect>`/`<ellipse>`/`<line>` (round-trips back to live).

- Combine structure: one group -> one evenodd path (exact); adds-then-
  subtracts with holes inside disjoint outlines -> one evenodd path (exact);
  overlapping all-Add unions -> sibling paths in a `<g>` (opaque paint only;
  re-imports as a folder, renders identically); intersect/xor -> rasterize.
- Strokes: Center is native; Inside doubles the width under a self-clip;
  Outside doubles under paint-order="stroke" (opaque fill required). Both
  carry `data-patchy-stroke-align`/`-width` hints so a Patchy round trip
  restores the true alignment and width (the reader also skips the trick clip
  rather than importing it as a vector mask). Dashes convert width-multiples
  -> absolute user units.
- Gradients invert the import mapping (center-chord span math); plain ramps
  emit their real stops (merged ascending union of color+alpha locations,
  reverse via 1-x), while Classic easing (smoothness > 0), non-50% midpoints,
  and noise gradients resample into 65 dense stops. Angle/Diamond -> rasterize.
- Pattern fills -> `<pattern>` with the tile PNG scaled into the cell +
  patternTransform; layer-linked anchoring is approximated (notice).
- Vector masks -> `<clipPath>` (inverted via canvas-rect + evenodd; density/
  feather/disabled -> rasterize). Raster masks -> luminance `<mask>` with a
  default_color backing rect.
- Everything else rasterizes through the real compositor into cropped
  base64-PNG `<image>` chunks with notices: text/pixel/smart-object layers
  individually (blend/opacity/display reapplied as CSS so compositing stays
  correct); clipping runs as one chunk; adjustment layers and CSS-inexpressible
  blend modes are barriers that merge everything below them at that sibling
  level into one flattened chunk (a pass-through group containing a barrier
  propagates it to its parent level; non-pass-through groups isolate theirs
  and emit style="isolation:isolate" to match Photoshop's group isolation).
- Output is deterministic (two writes are byte-identical): std::to_chars
  numbers, sequential def ids, layer names as sanitized unique element ids
  (which is how names round-trip).

## UI behavior

- Open lists *.svg and *.svgz; Save As/Export list *.svg. svg stays OUT of
  save_extension_preserves_layers on purpose: layered saves warn with
  svg-specific wording ("keeps shape layers as vectors, but ... baked") and
  keep Photoshop's save-a-copy semantics, and a modified svg-opened document
  routes Save to Save As (.psd default). Writer notices ride the save/export
  status message.
- Export Flat Image routes svg to the same structure-preserving writer and
  skips the raster options prompt (vectors scale client-side).
- Edit > Define Custom Shape from SVG File: one stampable library shape per
  file (geometry merged, paint ignored, unit-normalized, combine ops
  preserved so holes keep cutting) named from the file stem - the Photoshop
  Shapes-panel behavior. Needs no open document.
- Edit > Paste detects clipboard SVG (image/svg+xml data or `<svg>` text)
  and pastes editable shape layers (one "Paste shape" undo entry, names kept
  unless colliding, shapes re-baked against the target canvas). Parse
  failures fall through to the raster rendition most apps also provide.
- File > Place Embedded accepts svg: it becomes an embedded smart object
  (classified ReadOnly, rendered through the qsvg plugin like other
  Qt-decodable sources).

## Tests and fixtures

- tests/core/svg_tests.cpp - XML parser edge cases, d-grammar, cascade,
  gradients, fill-rule decomposition, clip/mask, units/PPI, svgz (gzip built
  in-test), the 2000-element fallback, export determinism/round-trip/raster
  chunking. tests/ui/svg_ui_tests.cpp - editable open, a QSvgRenderer
  cross-check (independent renderer, mean-delta tolerance), the text
  positioning pass, data-URI images, save-a-copy + reopen parity, paste,
  shape-library import, place. Fixture: test-fixtures/svg/basic-shapes.svg
  (self-authored).

## Photoshop parity notes

Photoshop 27.8 (COM-probed July 2026, dialogs suppressed) opens an SVG through
its classic Rasterize-SVG path: one flat ArtLayer at the rasterize-dialog
size (a Patchy-exported 240x160 file opened as a single 1000x667 raster).
Patchy's editable-shape-layer import is deliberately richer than that. SVG
files are not byte-pinned against Photoshop - the format is an open standard
and fidelity is judged against independent renderers (the qsvg cross-check
test) instead. The acceptance check is that Patchy-exported SVG opens in
Photoshop without error; `svg_fixture_reexport_writes_artifact` writes
`test-artifacts/svg-roundtrip.svg` next to the core-test binary as the file
to probe with.

Known approximations (all noticed): nonzero self-intersecting single
subpaths, radial focal points, spreadMethod=repeat, anisotropic stroke
transforms, complex text layout, objectBoundingBox clip paths, pass-through
group opacity, and Photoshop's Classic gradient easing exports as resampled
stops.
