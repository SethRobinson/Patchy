# Vector tools: pen paths, shape layers, vector masks, Paths panel

Feature deep-dive for Patchy's vector workflows. This document carries the PSD
vector-data encoding notes (all pinned by observation of Photoshop 27.8 via COM
scripting, July 2026 - never from Adobe's specification text), the fixture
inventory, and the patent record. Implementation status: capture phase complete;
model/rasterizer/PSD/UI phases land incrementally (see git history).

Probe and fixture-generation scripts live in `local-test-fixtures/vector-probe/`
(untracked), including `psd_dump.py`, a standalone PSD structure dumper used for
all byte-level analysis below. Fixtures are self-authored via COM per the repo
method rules.

## Photoshop file encodings (observed, PS 27.8 / July 2026)

### Shape and fill layer structure

- A shape layer is an ordinary layer record carrying a fill content block
  (`SoCo` solid / `GdFl` gradient / `PtFl` pattern) plus a `vmsk` vector mask
  block; live shapes add `vogk` (+ a 4-byte `vowv` = u32 2 beside it; PS wrote
  vowv for rect and line kinds but not ellipse); stroked shapes add `vstk`.
- Channel data is EMPTY: layer bounds (0,0,0,0) and every channel (including
  transparency id -1) is 2 bytes (just the compression marker). Readers must
  rasterize from the vector data.
- Layer record flags: bit 3 + **bit 4** (0x18). Bit 4 = "pixel data irrelevant";
  write it on shape/fill layers.
- `lnsr` is `'cont'` for content layers (`'bgnd'` for Background). PS names:
  "Color Fill 1" (path-created), "Rectangle 1", "Ellipse 1", "Line 1".
- A plain fill layer (no path) is the same structure with an empty-path or no
  `vmsk`.
- `vscg` was never written by PS 27.8 in any probe (solid/gradient/pattern
  fills, all stroke variants). It is a legacy key: read if present, never
  regenerate. PS's own resave migrates old files to the vstk-only shape.

### vmsk / vsms (vector mask path)

- Payload: u32 version = 3, u32 flags (bit 0 invert, bit 1 not-linked,
  bit 2 disabled), then 26-byte path records, padded to even length.
- `vsms` is an alternate legacy key with the same payload; PS 27.8 always
  writes `vmsk` (a 240-knot path still used vmsk). Read both, write vmsk.
- Record stream order: one selector-6 record (fill rule; payload observed all
  zeros), one selector-8 record (initial fill; u16 value observed 0), then per
  subpath one length record followed by its knot records.
- Length record (selector 0 = closed, 3 = open), bytes after the u16 selector:
  u16 knot count; u16 combine op (**0 = xor, 1 = add/union, 2 = subtract,
  3 = intersect**); u16 constant 1; 4 zero bytes; u32 subpath/origination index
  (0,1,2,... in file order; ties the subpath to its `vogk` keyOriginIndex);
  10 zero bytes.
- Knot records: selector 1 (closed smooth/linked), 2 (closed corner),
  4 (open smooth), 5 (open corner). Three coordinate pairs, each pair is
  (y then x), each value i32 8.24 fixed point expressed as a FRACTION of the
  canvas dimension (y/height, x/width; matches `fixed_path_delta` in
  layer_metadata.cpp). Pair order: **control toward the PREVIOUS anchor (in),
  then the anchor, then the control toward the NEXT anchor (out)** - pinned
  numerically against PS's own live-ellipse knots and the donut-probe render
  (the rule-distinguishing point (49,15) rendered empty, rejecting the
  out-first reading).
- Corner knots store all three pairs equal when no handles.
- Capture-script gotcha: ExtendScript's PathPointInfo.leftDirection lands in
  the file's OUT slot and rightDirection in the IN slot (reversed from the
  DOM documentation), so DOM-authored probe curves carry visually swapped
  handles. Harmless for record-layout probing and for render-parity fixtures
  (both readers see the same bytes), but do not treat DOM-authored curved
  fixtures as the intended visual shapes.

### Render semantics (pinned by fixture BMPs)

- Within one subpath the fill rule is EVEN-ODD (pentagram center is hollow).
- Subpaths combine SEQUENTIALLY by their op over accumulated coverage:
  add = union, subtract = remove, intersect = keep common, xor = toggle
  (overlap of two add rects stays filled, so ops act between subpath groups -
  coverage does not even-odd across groups).
- First-subpath op semantics: Subtract first = full canvas minus the shape
  (accumulator starts full); Add/Intersect/Xor first = exactly the shape.
- Open subpaths fill their implied closing chord.
- Multi-subpath groups sharing one keyOriginIndex (custom-shape stamps) are
  expected to even-odd within the group; not yet pinned by a capture - the
  reader keeps per-subpath raw op fields as a fallback.

### SoCo / GdFl / PtFl (fill content)

All are u32 descriptorVersion 16 + a descriptor with class `null`:

- `SoCo`: `Clr ` object, class `RGBC`, keys `Rd  `/`Grn `/`Bl  ` doubles.
- `GdFl` (defaults omitted; captured non-default set): `gradientsInterpolationMethod`
  enum `gradientInterpolationMethodType`=`Gcls`, `Angl` UntF #Ang, `Type` enum
  `GrdT`, `noisePreSeed` long, `Grad` object class `Grdn` name "Gradient"
  {`Nm  ` TEXT, `GrdF` enum `CstS`, `Intr` doub 4096, `Clrs` list of `Clrt`
  {`Clr ` RGBC, `Type` enum `Clry`=`UsrS`, `Lctn` long 0..4096, `Mdpn` long},
  `Trns` list of `TrnS` {`Opct` UntF #Prc, `Lctn`, `Mdpn`}} - the same Grad
  shape the layer-style parser (`parse_gradient`) already reads.
- `PtFl`: `Ptrn` object {`Nm  ` TEXT, `Idnt` TEXT guid}; scale/phase omitted at
  defaults. Pattern tiles ride the document-global `Patt` block (existing
  pattern infrastructure decodes them).

### vstk (stroke)

u32 descriptorVersion 16 + descriptor class `strokeStyle`, 16 items in this
exact order (PS-canonical):

1. `strokeStyleVersion` long 2
2. `strokeEnabled` bool
3. `fillEnabled` bool
4. `strokeStyleLineWidth` UntF #Pxl
5. `strokeStyleLineDashOffset` UntF #Pnt
6. `strokeStyleMiterLimit` doub 100
7. `strokeStyleLineCapType` enum (strokeStyleButtCap/RoundCap/SquareCap)
8. `strokeStyleLineJoinType` enum (strokeStyleMiterJoin/RoundJoin/BevelJoin)
9. `strokeStyleLineAlignment` enum (strokeStyleAlignInside/AlignCenter/AlignOutside)
10. `strokeStyleScaleLock` bool
11. `strokeStyleStrokeAdjust` bool
12. `strokeStyleLineDashSet` VlLs of UntF `#Nne` (dash lengths in stroke-width multiples)
13. `strokeStyleBlendMode` enum `BlnM` as FULL stringID ("normal")
14. `strokeStyleOpacity` UntF #Prc
15. `strokeStyleContent` Objc (solidColorLayer/gradientLayer/patternLayer, same
    shapes as the fill content blocks)
16. `strokeStyleResolution` doub (72; converts point-based widths)

### vogk (vector origination / live shapes)

u32 version 1 + u32 descriptorVersion 16 + descriptor class `null` holding
`keyDescriptorList` (VlLs), one entry per live subpath group. Entry items in
captured order (kind-dependent):

- Rect (keyOriginType 1): keyOriginType long, keyOriginResolution doub,
  keyOriginShapeBBox {unitValueQuadVersion long 1, `Top `/`Left`/`Btom`/`Rght`
  UntF #Pxl}, keyOriginBoxCorners {rectangleCornerA..D, each `Pnt ` {Hrzn,Vrtc
  doubles}}, `Trnf` {name "Transform", class `Trnf`, xx,xy,yx,yy,tx,ty doubles},
  keyOriginIndex long.
- Rounded rect (type 2): rect set plus keyOriginRRectRadii {unitValueQuadVersion,
  **topRight, topLeft, bottomLeft, bottomRight** UntF #Pxl - note the order}
  inserted after keyOriginResolution.
- Ellipse (type 5): keyOriginType, keyOriginResolution, keyOriginShapeBBox,
  Trnf, keyOriginIndex (no boxCorners).
- Line (type 4): keyOriginType, keyOriginResolution, keyOriginShapeBBox, Trnf,
  keyOriginLineEnd {Pnt}, keyOriginLineStart {Pnt}, keyOriginLineWeight doub,
  keyOriginLineArrowSt bool, keyOriginLineArrowEnd bool, keyOriginLineArrWdth
  doub, keyOriginLineArrLngth doub, keyOriginLineArrConc long,
  keyOriginLineWidthArrowUnitPixels bool, keyOriginLineLengthArrowUnitPixels
  bool, keyOriginBoxCorners, keyOriginIndex.
- Arrowed lines could not be authored headlessly (the Mk shape descriptor
  rejects arrow keys); the key names/types above come from the plain line's
  defaults. Patchy-authored arrows are acceptance-verified by reopening in PS.
- App-level (`executeActionGet` keyOriginType) path-drawn subpaths report
  keyActionMode entries instead of live-shape data.

### Vector masks on layers (mask data section, channels)

- A vector-mask-ONLY layer has NO mask data section and NO baked mask channel;
  the path is the only representation.
- Raster + vector masks together: the ordinary 20-byte mask data section holds
  the raster mask (rect, default color, flags) and the vector mask stays purely
  in vmsk; the raster plane is channel id -2.
- Vector mask density/feather (Properties panel) use the mask-parameters form:
  section flags bit 4 set, then a parameter flags byte (bit 0 user density u8,
  bit 1 user feather f64, bit 2 vector density u8 raw 0..255, bit 3 vector
  feather f64 BE), values in that order. When any vector parameter is set, PS
  ALSO bakes a derived coverage plane into channel -2 and sets section flags
  bit 3 ("mask came from rendering other data") with the baked rect as the
  section rect. Reading: use the baked plane for rendering until the first
  vector edit, then re-derive.
- COM authoring gotchas: vectorMaskFeather/Density setd requires the vector
  mask path to be SELECTED (slct Path->vectorMask) first, and feather must be
  set in its OWN setd call (combining density+feather in one descriptor drops
  the feather).

### Document path image resources

- Saved paths: resources 2000..2997, resource NAME = path name, payload = raw
  26-byte record stream (selector 6, selector 8, then subpaths - identical
  grammar to vmsk WITHOUT the version/flags header).
- Work path: resource 1025, same payload, no name.
- Clipping path selector: resource 2999: pascal path name (even-padded) +
  4 zero bytes + 0x01 (trailing bytes recorded verbatim; re-emit as captured).
- PS re-sorts/upserts these like any resource; Patchy preserves unknown ones
  wholesale already.

### PSB

All vector keys use the 8BIM signature + 4-byte length form in PSB (none are
in the 8-byte LARGE_KEYS set). Fixture: photoshop-shape.psb.

## Fixture inventory (test-fixtures/psd, self-authored via COM, July 2026)

Each .psd has a sibling .bmp = Photoshop's own flatten (24-bit, white
background layer included in every file) for render-parity tests. The embedded
PSD composites are headless-stale (see ps-compat.md) - always compare against
the BMPs.

- photoshop-shape-solid.psd/bmp - asymmetric curved shape, SoCo red; pins knot
  in/out order via render.
- photoshop-shape-gradient.psd/bmp - GdFl linear 37 deg, 3 color stops (midpoint
  30 on the first), 3 transparency stops (42% middle).
- photoshop-shape-pattern.psd/bmp - PtFl with a defined 8x8 checker (tiles in
  the global Patt block).
- photoshop-shape-strokes.psd/bmp - six stroked layers: center/inside/outside
  w6 butt+miter; dashed [2,1] round+round open curve (fillEnabled false);
  square+bevel triangle; stroke-only rect (fillEnabled false).
- photoshop-shape-boolean.psd/bmp - one layer, four subpaths: add, subtract,
  intersect, xor (sequential-combine ground truth).
- photoshop-shape-first-ops.psd/bmp - three 40%-opacity layers whose single
  subpath op is subtract / intersect / xor (initial-accumulator semantics).
- photoshop-shape-live-rect.psd/bmp - live rounded rect (radii 4/8/12/16),
  live ellipse, live line w4 (vogk per kind; vowv presence).
- photoshop-vector-mask-on-pixel.psd/bmp - pixel layer + curved-triangle
  vector mask; no mask channel, no mask-data section.
- photoshop-both-masks.psd/bmp - layer 1: raster mask (top strip) + vector
  mask; layer 2: vector mask with density 60% + feather 1.5 px (mask
  parameters + baked derived -2 channel, section flags 0x18).
- photoshop-saved-paths.psd/bmp - saved paths "Alpha Path" (rect, clipping
  path) and "Beta Path" (donut, 2 subpaths), work path from a selection;
  resources 2000/2001/1025/2999.
- photoshop-shape.psb/photoshop-shape-psb.bmp - PSB variant of the solid shape.

## Patents and trademarks (assessed July 2026)

Cleared as expired prior art (reasoning, not legal advice):

- Classic pen-tool bezier editing (anchors, in/out handles, corner/smooth
  conversion, rubber band) ships in Illustrator 88 (1988) and PostScript-era
  tooling; any patents are long expired.
- Shape layers with editable fill + vector clipping mask and combine ops
  shipped in Photoshop 6 (2000); patents filed around that generation expired
  by ~2021-2024. Vector masks per se (PS 6/7 era) likewise.
- Boolean path combine modes, even-odd/nonzero fills, stroke dashing,
  caps/joins are decades-old published techniques.

Excluded from implementation pending their own review (do NOT build without a
new patent check):

- Curvature Pen tool (2018+) and any auto-fitting curve-through-points UX
  beyond the classic pen.
- Content-aware/image tracing to vectors, edge-magnetic vector snapping.
- Snap-to-pixel "align edges" automatic pixel-grid fitting of vector renders
  (plain user-invoked grid/guide snapping of anchors is classic and fine).
- On-canvas live-shape gizmo widgets (in-canvas radius handles etc.) - plain
  options-bar/dialog parameter editing is the cleared route (Apple US 8971623
  showed non-Adobe UI patents bite; see docs/smart-objects.md).
- Variable-width strokes / art brushes on paths (out of scope anyway).

Method rules binding for this feature (same as all PSD work): ground truth is
observed output of licensed Photoshop via COM byte-diffing; no Adobe
specification text in the repo; self-authored fixtures only; referential
"compatible with Adobe Photoshop" phrasing; original tool icons with
non-Photoshop geometry.
