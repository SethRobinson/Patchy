# SVG import and export

SVG opens as editable shape layers and saves/exports with vectors preserved;
everything the format cannot express degrades to embedded raster with an
import/export notice. SVG is an open W3C standard: consulting the spec text is
fine (the no-spec-text method rule is Adobe-specific), and there is no
byte-pinning against Photoshop output. Parsing and writing SVG adds no patent
surface: it is not image tracing, which is a separate feature with its own
boundary ([image-trace.md](image-trace.md), "Vector tracing" in
[legal-constraints.md](legal-constraints.md)).

## Code map

All Qt-free, in src/formats/ (patchy_formats):

- `svg_xml.{hpp,cpp}`: minimal XML DOM: elements/attributes, comments, CDATA,
  processing instructions, DOCTYPE with internal-subset `<!ENTITY>` expansion
  (old Illustrator exports reference namespace URIs as `&ns_svg;`), the
  predefined + numeric character references, namespace resolution (SVG/xlink
  collapse to bare local names, `xlink:href` -> `href`, foreign prefixes stay
  verbatim so consumers can skip them), UTF-8/UTF-16/declared-Latin-1 input.
  Hand-written deliberately: lightweight parsers do not expand DTD entities.
  Hard caps: 250k nodes, depth 512, 8 MB entity expansion.
- `svg_document_io.hpp`: public API (`svg::DocumentIo::read/write/write_file`,
  the `baked_content` dry run, `svg_extensions()` = {.svg, .svgz}, `sniff`).
- `vector_export_plan.{hpp,cpp}`: the target-independent export policy shared
  with editable PDF export (docs/pdf.md): combine classification, the common
  shape/group representability checks, the sibling-unit walk with the barrier
  rule (parameterized by which blend modes the target expresses), gradient
  geometry + stop sampling, and the alpha-bounds crop. The SVG writer adds only
  its own rules (inside-stroke vs vector-mask clip slot, disabled raster masks).
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
  mapping is identity, with no reversal anywhere.
- **Canvas**: physical width/height units (in/cm/mm/pt/pc) -> CSS 96 px/in and
  96 PPI print metadata; unitless/percent/absent -> viewBox user units at the
  untagged-import 72 PPI; neither -> 300x150 (the CSS replaced-element
  default) with a notice. Oversized canvases clamp to 30000 px, scaling
  content. viewBox honors the full preserveAspectRatio grammar. svg is
  deliberately NOT in load_document_from_path's kDensitylessFormats: the
  reader sets its own PPI.
- **Structure**: `<g>`/nested `<svg>` -> Group folders (opacity, blend,
  display:none -> hidden); `<a>` is a transparent container; `<use>` clones
  (cycle-guarded, depth 32, at most 20000 instantiations per file so
  sibling references cannot grow exponentially; symbol/svg targets instantiate
  like `<g>`);
  `<switch>` takes the first child whose conditionals pass (requiredExtensions/
  -Features fail when present; systemLanguage passes on an "en" entry). Names:
  id, else `<title>`, else Photoshop-style counters ("Rectangle 1", ...).
- **Styling**: real cascade order: presentation attributes < stylesheet rules
  (type < .class < #id, later wins ties; the flat-selector subset Illustrator
  emits) < inline style. fill/stroke/etc. inherit; opacity, display, and
  mix-blend-mode reset per element. Colors: #hex 3/4/6/8, rgb()/rgba(),
  hsl()/hsla(), the CSS named colors (rebeccapurple included), transparent,
  currentColor. Paint values keep their
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
  even-odd semantics, the one approximation).
- **Opacity**: layer opacity = element opacity x fill-opacity x solid-fill
  alpha; stroke opacity divides that back out (a stroke more opaque than its
  fill clamps, with a notice).
- **Gradients**: linear/radial with href template inheritance, objectBoundingBox
  and userSpaceOnUse units, gradientTransform, stop-opacity. Geometry maps
  onto the calibrated GdFl model (span = center chord; docs/vector-tools.md);
  smoothness = 0 so stops interpolate linearly, matching SVG.
  Coordinates are read in gradient space and carried to the document by one
  matrix: element transform x gradientTransform for userSpaceOnUse, element
  transform x (unit square -> the element's own pre-transform box) x
  gradientTransform for objectBoundingBox. The element transform is the whole
  chain (viewBox mapping, ancestor groups, the element's own), passed to the
  paint servers as `PaintSpace`; a paint server that skips it misplaces the
  ramp under any viewBox scale or group transform. A non-uniform matrix keeps
  the mapped stripes and measures the ramp across them (a diagonal ramp on a
  non-square box); a radial keeps the larger radius. userSpaceOnUse sets
  align_with_layer off (placed against the canvas) and resolves percentages
  against the outermost viewport in user units.
  spreadMethod=reflect -> Reflected (scale doubles; the export halves it
  back). Focal points and repeat spreads are approximated with a notice.
- **Patterns**: `<pattern>` with href template inheritance, userSpaceOnUse and
  objectBoundingBox patternUnits (x/y/width/height; a user-space percentage is
  a share of the viewport), patternContentUnits, viewBox, patternTransform.
  The tile becomes a document PatternStore tile and the fill is placed against
  the document (pattern_linked = false). Pattern space is the painted
  element's user space, so the grid goes through the same `PaintSpace` matrix
  as the gradients: element transform x patternTransform, with the tile's
  corner at x/y (objectBoundingBox: fractions of the element's own
  pre-transform box, measured from its corner).
  That matrix is split into what a tile's pixels can carry and what the
  placement model has to:
  - Scale along each pattern axis, and a mirror, are baked into the tile: it is
    rasterized at document resolution (20 user units under a 2x viewBox is a
    40 px tile at pattern_scale 1), so scaled patterns stay sharp. A mirror
    reverses the tile's rows.
  - Rotation becomes pattern_angle. The model's angle is counterclockwise-
    positive (the Photoshop dial, `PatternTileSampler`), an SVG rotate() is
    clockwise on the y-down canvas, so the sign flips in both directions.
  - pattern_scale carries only the remainder: a tile whose document size is not
    a whole number of pixels is rasterized at the nearest whole size and scaled
    by the ratio (the period stays exact), and a tile past 4096 px is
    rasterized at that cap and scaled up.
  - pattern_phase is the document position of the tile's corner.
  - Skew is the one approximation (notice).
  Content is either shapes (solid or gradient fills; strokes are not drawn) or
  one embedded PNG/JPEG `<image>` that fills the tile unrotated, which is the
  form the export writes: the decoded image is the tile at its own resolution
  (stb_image, in the reader) and pattern_scale maps it to the cell. Other
  content, or an image the pattern or the element would stretch, degrades to
  gray + notice. A pattern child painted with a pattern (nested, or the
  pattern itself) paints gray without being resolved. Elements that share a
  pattern at the same tile size share one PatternStore tile.
- **clip-path** (userSpaceOnUse, shape children) -> vector mask; **mask**
  (shape children) -> raster mask from fill-luminance-weighted coverage.
- **Text**: basic `<text>`/tspan -> text layers (Pixel kind + patchy.text.*
  metadata, the standard text pattern) with font family/size/bold/italic/
  color; the Qt-free reader stores the baseline point + text-anchor under
  patchy.svg.* keys plus kLayerMetadataSvgPendingText, and
  `MainWindow::render_pending_svg_text_layers` (main_window.cpp; it needs
  the text pipeline) renders and positions them post-open on the main thread.
  textPath/x-arrays/textLength reduce to plain text with a notice.
- **Images**: data-URI PNG/JPEG -> pixel layers; bytes ride
  kLayerMetadataSvgPendingImage and `decode_pending_svg_images`
  (main_window_shared.cpp) decodes on the open worker (QImage decode is
  thread-safe; fonts are not, hence the two-pass split). External file
  references are skipped with a notice.
- **Raster fallback**: unparseable XML or > 2000 drawables throws; the
  existing QImageReader fallback (the qsvg plugin, shipped by
  scripts\release\build-release.bat) rasterizes, and the open path adds an "imported as
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
- Gradients invert the import mapping (center-chord span math, against the
  path bounds, or the canvas when align_with_layer is off); plain ramps
  emit their real stops (merged ascending union of color+alpha locations,
  reverse via 1-x), while Classic easing (smoothness > 0), non-50% midpoints,
  and noise gradients resample into 65 dense stops. Angle/Diamond -> rasterize.
- Pattern fills -> a userSpaceOnUse `<pattern>` whose cell is the tile size x
  pattern_scale, holding the tile PNG stretched over the cell, with
  patternTransform = translate(phase) rotate(-angle). The reader takes that
  form back to the same tile, scale, angle, and phase. Layer-linked anchoring
  is approximated (notice).
- Vector masks -> `<clipPath>` (inverted via canvas-rect + evenodd; density/
  feather/disabled -> rasterize). Raster masks -> luminance `<mask>` with a
  default_color backing rect.
- Everything else rasterizes through the real compositor into cropped 8-bit
  base64-PNG `<image>` chunks with notices (a 16/32-bit document's chunks are
  composited at 8 bits): text/pixel/smart-object layers
  individually (blend/opacity/display reapplied as CSS so compositing stays
  correct); clipping runs as one chunk; adjustment layers and CSS-inexpressible
  blend modes are barriers that merge everything below them at that sibling
  level into one flattened chunk (a pass-through group containing a barrier
  propagates it to its parent level; non-pass-through groups isolate theirs
  and emit style="isolation:isolate" to match Photoshop's group isolation).
- Output is deterministic (two writes are byte-identical): classic-locale
  %.15g numbers, index-ordered walks, sequential def ids, layer names as sanitized unique element ids
  (which is how names round-trip).

## UI behavior

- Open lists *.svg and *.svgz; Save As/Export list *.svg. svg stays OUT of
  save_extension_preserves_layers on purpose, but it does not inherit the
  generic flat-format warning either: `save_discards_layers` (main_window_files.cpp)
  asks the writer's dry run, `svg::DocumentIo::baked_content`, which walks the
  document with the writer's own representability rules and reports what
  `write` would bake (`BakedContentKind` + layer name) without compositing or
  encoding anything. Empty means shape layers, folders, clipPath vector masks,
  gradient and pattern paint servers only: Save writes in place with no
  warning and no Save As redirect, for a plain document and for a linked
  smart-object child alike (`ui_svg_shape_only_save_writes_vectors_without_warning`,
  `ui_smart_object_linked_svg_child_saves_vectors_without_warning`). Anything
  baked (text, pixel and smart-object layers, adjustment layers and
  CSS-inexpressible blend modes with the layers merged under them, styled or
  intersect/xor shapes, styled or masked groups, clipping runs, raster masks
  written as luminance `<mask>` images) keeps the warning, which names the
  first six items by kind and layer name and counts the rest, and keeps
  Photoshop's save-a-copy semantics; a modified svg-opened
  document with baked content still routes Save to Save As (.psd default). A
  linked child gets the "bake it into the linked file?" wording because its
  save is a real save (`ui_svg_save_with_text_layer_warns_and_names_it`,
  core `svg_baked_content_dry_run_matches_writer`). The single-plain-pixel-layer
  exemption of `flat_save_discards_layers` still applies first. Writer notices
  ride the save/export status message.
- File > Export > Flat Image routes svg to the same structure-preserving writer and
  skips the raster options prompt (vectors scale client-side). Divide Scanned
  Photos and Export Documents to Folder leave svg out of their format lists.
- Edit > Define Custom Shape from SVG File: one stampable library shape per
  file (geometry merged, paint ignored, unit-normalized, combine ops
  preserved so holes keep cutting) named from the file stem (the Photoshop
  Shapes-panel behavior). Needs no open document.
- Edit > Paste detects clipboard SVG (image/svg+xml data or `<svg>` text)
  and pastes editable shape layers (one "Paste shape" undo entry, names kept
  unless colliding, shapes re-baked against the target canvas). Parse
  failures fall through to the raster rendition most apps also provide.
- Edit > Copy as SVG (`edit.copy_svg`, `editCopySvgAction`, no default key)
  writes the selected layers through the same structure-preserving writer
  into a sub-document at the canvas size (PPI, pattern and smart-object
  stores copied) and puts the bytes on the clipboard as image/svg+xml plus
  text (the Illustrator/Figma/Inkscape convention); the internal layer
  clipboard is dropped so Paste reads it back in place. Notices ride the
  status message. Test: `ui_svg_copy_as_svg_round_trips_shape_layer`.
- File > Place Embedded, File > Place Linked, Layer > Smart Objects >
  Relink to File, and `doc.addSmartObject` accept svg: it becomes a smart object with Photoshop's `SVG ` filetype and
  Type 1 (vector) placement, classified ReadOnly for editing and rasterized
  through the qsvg plugin at the placement's own scale, so every size stays
  sharp (`render_smart_object_vector_contents`; see
  [smart-object-editing.md](smart-object-editing.md)).

## Tests and fixtures

- tests/core/svg_tests.cpp: XML parser edge cases, d-grammar, cascade,
  gradients (placement under viewBox scale, group and element transforms, and
  the canvas-anchored re-export), patterns (tile size, anchor, and baked pixels
  under viewBox scale, group and element transforms, both unit modes, rotation,
  mirror, and the export round trip), fill-rule decomposition, clip/mask, units/PPI, svgz (gzip built
  in-test), the 2000-element fallback, export determinism/round-trip/raster
  chunking. tests/ui/svg_ui_tests.cpp: editable open, a QSvgRenderer
  cross-check (independent renderer, mean-delta tolerance), the text
  positioning pass, data-URI images, the no-warning in-place save of a
  shape-only file plus reopen parity, the named flatten warning for a text
  layer, paste,
  shape-library import, place. Fixtures: test-fixtures/svg/basic-shapes.svg
  (self-authored) and test-fixtures/svg/hot_air_balloons_cc0.svg (CC0 clip
  art, NOTICE-THIRD-PARTY.md; drives the README SVG-import screenshot scene).

## Photoshop parity notes

Photoshop 27.8 (COM-probed, dialogs suppressed) opens an SVG through
its classic Rasterize-SVG path: one flat ArtLayer at the rasterize-dialog
size (a Patchy-exported 240x160 file opened as a single 1000x667 raster).
Patchy's editable-shape-layer import is deliberately richer than that. SVG
files are not byte-pinned against Photoshop: the format is an open standard
and fidelity is judged against independent renderers (the qsvg cross-check
test) instead. The acceptance check is that Patchy-exported SVG opens in
Photoshop without error; `svg_fixture_reexport_writes_artifact` writes
`test-artifacts/svg-roundtrip.svg` next to the core-test binary as the file
to probe with.

Known approximations (all noticed): nonzero self-intersecting single
subpaths, radial focal points, spreadMethod=repeat, anisotropic stroke
transforms, skewed patterns, complex text layout, objectBoundingBox clip paths, pass-through
group opacity, and Photoshop's Classic gradient easing exports as resampled
stops.
