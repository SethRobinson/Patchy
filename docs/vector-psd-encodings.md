# Vector PSD encodings (observed, Photoshop 27.8)

Split from [vector-tools.md](vector-tools.md): the on-disk shapes of shape and fill layers, vector masks, strokes, live shapes and document paths, pinned by the fixtures in [vector-fixtures.md](vector-fixtures.md). Compatibility contract: [ps-compat.md](ps-compat.md).

Codec: src/psd/psd_vector.cpp (vmsk/vsms, SoCo/GdFl/PtFl, vstk, vogk, path
resources); model: src/core/vector_shape.hpp.

## Shape and fill layer structure

- A shape layer is an ordinary layer record carrying a fill content block
  (`SoCo` solid / `GdFl` gradient / `PtFl` pattern) plus a `vmsk` vector
  mask block; live shapes add `vogk` (+ a 4-byte `vowv` = u32 2 beside it;
  PS wrote vowv for rect and line kinds but not ellipse; Patchy writes it
  with every vogk); stroked shapes add `vstk`.
- **Two hard open-refusal rules** (byte bisection with COM open tests; the
  rule text and regression-test names live in ps-compat.md):
  1. Every `PtFl` fill or `vstk` stroke pattern id MUST resolve in the file's
     `Patt`/`Pat2`/`Pat3`. `collect_referenced_pattern_ids` collects vector
     and style ids; missing tiles get 1x1 transparent placeholders.
     `PatternStore::adopt` heals placeholders on re-pick.
  2. Partial `vogk` group coverage is rejected. `origination_covers_path_groups`
     gates writing vogk/vowv; import drops partial blocks. Resaves use PS's
     plain-path fallback.
- Color channels are EMPTY: bounds (0,0,0,0), 2-byte compression markers
  including transparency -1. Readers rasterize vectors; writer:
  src/psd/psd_layer_records.cpp. Raster masks retain their own channels.
- Layer record flags: bit 3 + **bit 4** (0x18; bit 4 = "pixel data
  irrelevant"); write both on shape/fill layers.
- `lnsr` = 'cont' for content layers ('bgnd' for Background). PS names:
  "Color Fill 1" (path-created), "Rectangle 1", "Ellipse 1", "Line 1".
- A plain fill layer is the same structure with an empty or absent `vmsk`.
- `vscg`: content key (SoCo/GdFl/PtFl, 4 bytes), descriptorVersion 16, paint.
  Without a fill block, vstk `fillEnabled=true` uses this paint as FILL,
  independent of stroke paint. Otherwise Fill is None; vscg supplies stroke
  paint only when vstk lacks strokeStyleContent. Untouched vscg/vmsk/vsms
  stay verbatim. Edits write SoCo/GdFl/PtFl + vstk + vmsk, retaining gradient
  settings and unmodeled paint fields. Missing vstk/path or unparseable
  required paint locks as "unparsed". Tests: `psd_legacy_vscg_*`.

## vmsk / vsms (vector mask path)

- Payload: u32 version = 3, u32 flags (bit 0 invert, bit 1 not-linked, bit 2
  disabled), then 26-byte path records, padded to even length. `vsms` is a
  legacy alternate (same payload; PS 27.8 writes `vmsk`): read both, write
  vmsk.
- Record order: one selector-6 record (fill rule; observed all zeros), one
  selector-8 record (initial fill; u16 observed 0), then per subpath a length
  record followed by its knot records.
- Length record (selector 0 = closed, 3 = open), after the u16 selector: u16
  knot count; u16 combine op (**0 = xor, 1 = add/union, 2 = subtract,
  3 = intersect**); u16 fill-rule field (+6); 4 zero bytes; u32 shape-group
  index (0,1,2,... in file order; ties the group to its `vogk`
  keyOriginIndex); 10 zero bytes.
- **Compound groups** (several contours sharing one group index: a donut,
  Convert to Shape glyphs, custom-shape stamps; pinned by byte-patched
  probes): the group's LEAD record carries the op and the +6 field (1 = fill
  the group even-odd, 2 = nonzero winding; PS's own compound shapes write 2);
  every CONTINUATION record carries op 0xFFFF and +6 field 0. A continuation
  written with its own op and +6 field 1 is a separate united contour: PS
  fills such a donut solid. The writer emits the continuation form for any
  subpath sharing the previous subpath's `shape_group` (lead: its op, +6 = 1,
  matching the renderer's even-odd group rule); the reader gives a 0xFFFF
  continuation its lead's op. PS resaves the +6 = 1 lead unchanged. Nonzero
  is not modeled: an untouched +6 = 2 layer re-emits verbatim, an edited one
  regenerates as even-odd (differs only for same-winding nested contours).
  Fixtures: [vector-fixtures.md](vector-fixtures.md).
- CS4-era files use the same grammar: leads op 1 / +6 = 2, cutouts UNSET
  (0xFFFF, +6 field 0), group index 0 on every record (Flat-filter-list.psd).
  A 0xFFFF record whose group differs from the previous record's maps to xor
  (parity fill). Pinned by `psd_legacy_vmsk_unset_combine_op_fills_by_parity`;
  real-file coverage rides `psd_16_bit_flat_filter_list_loads_if_available`.
- Knot records: selector 1 (closed smooth/linked), 2 (closed corner), 4
  (open smooth), 5 (open corner). Three coordinate pairs, each (y then x),
  each value i32 8.24 fixed point as a FRACTION of the canvas dimension
  (y/height, x/width). Pair order: **control toward the PREVIOUS anchor (in),
  then the anchor, then the control toward the NEXT anchor (out)**, pinned
  numerically against PS's live-ellipse knots and a rule-distinguishing donut
  render. Corner knots store all three pairs equal when no handles.

## Render semantics (pinned by fixture BMPs)

Implemented by src/core/vector_raster.hpp:

- Within one subpath the fill rule is EVEN-ODD (pentagram center hollow).
- Subpaths combine SEQUENTIALLY by their op over accumulated coverage:
  add = union, subtract = remove, intersect = keep common, xor = toggle. Ops
  act between subpath groups; coverage does not even-odd across groups
  (overlap of two add rects stays filled).
- First-subpath op: Subtract first = full canvas minus the shape
  (accumulator starts full); Add/Intersect/Xor first = exactly the shape.
- Open subpaths fill their implied closing chord. An empty path means "cover
  everything" (the fill-layer rule).
- Contours sharing one group index fill EVEN-ODD together (pinned by
  `patchy-compound-group.bmp`: holes for same- and opposite-winding inner
  contours, inner-first order, and a nested island).

## SoCo / GdFl / PtFl (fill content)

All are u32 descriptorVersion 16 + a descriptor with class `null`:

- `SoCo`: `Clr ` object, class `RGBC`, keys `Rd  `/`Grn `/`Bl  ` doubles.
- `GdFl` (defaults omitted; captured non-default set):
  `gradientsInterpolationMethod` enum
  `gradientInterpolationMethodType`=`Gcls`, `Angl` UntF #Ang, `Type` enum
  `GrdT`, `noisePreSeed` long, `Grad` object class `Grdn` name "Gradient":
  the same Grad shape the layer-style parser (`parse_gradient`) reads (Nm,
  GrdF=CstS, Intr doub 4096, Clrs list of Clrt, Trns list of TrnS).
- `PtFl`: `Ptrn` object {`Nm  ` TEXT, `Idnt` TEXT guid}; scale/phase omitted
  at defaults. Pattern tiles ride the document-global `Patt` block.

## vstk (stroke)

u32 descriptorVersion 16 + descriptor class `strokeStyle`, 16 items in this
exact order (PS-canonical): strokeStyleVersion (long 2), strokeEnabled,
fillEnabled, strokeStyleLineWidth (#Pxl), strokeStyleLineDashOffset (#Pnt),
strokeStyleMiterLimit (doub 100), strokeStyleLineCapType,
strokeStyleLineJoinType, strokeStyleLineAlignment (Butt/Round/Square,
Miter/Round/Bevel, Inside/Center/Outside enums), strokeStyleScaleLock,
strokeStyleStrokeAdjust, strokeStyleLineDashSet (VlLs of UntF `#Nne`; dash
lengths in stroke-width multiples), strokeStyleBlendMode (enum `BlnM` as
FULL stringID "normal"), strokeStyleOpacity (#Prc), strokeStyleContent
(Objc solidColorLayer/gradientLayer/patternLayer, same shapes as the fill
content blocks), strokeStyleResolution (doub 72; converts point-based
widths). Full enum spellings in src/psd/psd_vector.cpp.

## vogk (vector origination / live shapes)

u32 version 1 + u32 descriptorVersion 16 + descriptor class `null` holding
`keyDescriptorList` (VlLs), one entry per live subpath group. Entry items in
captured order (kind-dependent):

- Rect (keyOriginType 1): keyOriginType, keyOriginResolution,
  keyOriginShapeBBox (unitValueQuadVersion 1 + Top/Left/Btom/Rght UntF
  #Pxl), keyOriginBoxCorners (rectangleCornerA..D points), Trnf (xx,xy,yx,
  yy,tx,ty doubles), keyOriginIndex.
- Rounded rect (type 2): the rect set plus keyOriginRRectRadii
  {unitValueQuadVersion, **topRight, topLeft, bottomLeft, bottomRight** UntF
  #Pxl; note the order} inserted after keyOriginResolution.
- Ellipse (type 5): the rect set minus keyOriginBoxCorners.
- Line (type 4): keyOriginType, keyOriginResolution, keyOriginShapeBBox,
  Trnf, keyOriginLineEnd, keyOriginLineStart, keyOriginLineWeight,
  keyOriginLineArrowSt/ArrowEnd, keyOriginLineArrWdth/ArrLngth,
  keyOriginLineArrConc, keyOriginLineWidthArrowUnitPixels/
  LengthArrowUnitPixels, keyOriginBoxCorners, keyOriginIndex. Arrow keys
  come from the plain line's defaults (not COM-authored); Patchy-authored
  arrows were verified by reopening in PS.
- App-level (`executeActionGet`) path-drawn subpaths report keyActionMode
  entries instead of live-shape data.

Live-shape knot constructions (kappa handles, corner orders):
src/core/vector_live_shapes.hpp.

## GdFl gradient fill geometry (probe5c/5d/5e)

- Linear span = the CENTER CHORD of the aligned bounds:
  min(w/|cos a|, h/|sin a|), centered on the bounds center (measured within
  0.5 px at angles 0/20/37/60/75/90). Layer-style overlays deliberately keep
  their corner-to-corner projection (GradientSpanBasis::LayerProjection);
  the two agree at exact axis angles. Pixel-corner sampling, the whole-pixel
  ends, and a maskless fill layer aligning to its user mask's visible bounds:
  [gradients.md](gradients.md), "Gradient fill layer geometry".
- Classic easing applies even to TWO-stop ramps: per-segment catmull-rom
  with duplicated virtual endpoints (f(t) = 0.5t + 1.5t^2 - t^3 for a plain
  2-stop ramp), scaled by smoothness/4096. The OPACITY ramp eases
  identically. Midpoints are the piecewise-linear law through
  (midpoint, 50%) and apply BEFORE the ease.

## Stroke rasterization (winding, lattice, bounds)

- Multi-group closed paths stroke their resolved Boolean boundary, removing
  interior seams. Split intersections and side-fill tests form directed contour
  walks for the existing stroker. Editable paths and PSD blocks stay intact.
- Aligned dashes retain original-width caps; zero-length dots carry their
  path tangent. Geometry, tests and residuals:
  [vector-fixtures.md](vector-fixtures.md).
- The stroker builds the band as a union of per-segment quads plus join/cap
  wedges under the nonzero rule; every loop must carry the SAME orientation
  (append_outline_loop normalizes by signed area), or an opposite-winding
  wedge cancels the quads it overlaps
  (stroke_arc_band_has_no_winding_notches).
- subpath_polyline snaps every vertex (anchors included) to the flattener's
  1/256 lattice so sub-quantum micro-segments cannot seed miter spikes
  (limit 100 admits turns to 178.85 degrees). The coverage band is sized
  from the emitted outline's true hull; stroke curves flatten through the
  same adaptive flatten_cubic as fills. Pinned by
  stroke_bezier_circle_is_translation_stable,
  stroke_miter_spike_stays_in_bounds,
  stroke_curve_is_insensitive_to_sub_quantum_anchor_jitter, and the
  circle-nonlattice entry of stroke_golden_digests_are_stable.
- Known gap: PS's `strokeStyleMiterLimit` 100 (a bare doub, not #Prc) may
  be a percentage rather than the SVG-style ratio Patchy reads; settle with
  a COM probe of an acute mitered corner at limit 100 vs 4.

## Interior effects vs the vector stroke

Overlays cover the fill plane only and the vector stroke composites above
them; the calibration and renderer notes live in
[layer-effects-render.md](layer-effects-render.md).

PS's baked derived plane (mask flags bit 3) holds UNFEATHERED path coverage;
the feather applies at render. Patchy bakes its own feathered cache: the
raster mask's gaussian (mask_feather_blur) but NOT canvas-clamped: a path
ending on the canvas edge fades there (photoshop-vector-mask-feather.psd).

## Vector masks on layers (mask data section, channels)

- Vector-mask-ONLY layer: no mask section or channel.
- Raster + vector masks: the 20-byte section holds the raster mask
  (channel -2); the vector mask stays in vmsk.
- A SHAPE layer's own path carries the same parameters
  (photoshop-shape-feather.psd, PS 27.9): section flags 0x18, derived plane =
  plain path coverage (hull +-1), and only the SET parameter bits (feather
  alone 27 -> 28 bytes padded, density alone 20). Model:
  `VectorShapeContent::feather/density` (reader drops the derived plane).
  Render (BMP-pinned): feather blurs the WHOLE rendered shape, stroke
  included, unclamped at the canvas (`feather_shape_raster`); density shows
  the fill everywhere at (255 - density)/255 (`apply_shape_density`).
- Density/feather use the parameters form: section flags bit 4, then a
  parameter flags byte (bit 0 user density u8, bit 1 user feather f64, bit 2
  vector density u8 raw, bit 3 vector feather f64 BE), values in that order.
  With a vector parameter set PS ALSO bakes a derived plane into -2 (section
  flags bit 3, section rect = baked rect).
- Raster + PARAMETERIZED vector mask (photoshop-both-masks-params.psd): a
  48-byte section. -2 is PS's COMBINED render (flags 0x18); the painted mask
  is channel -3, sized by the real-user-mask fields (flags u8, default u8,
  rect), which sit BEFORE the parameter byte (Adobe's spec says after).
  Patchy loads -3 as the mask and drops -2. Real fields need bit 3 or no
  bit 4 (PS reads Patchy's 40-byte painted all-four form parameters-first).
- Raster-mask density/feather (photoshop-user-mask-params.psd): PS sets bits
  0/1 singly, pads the section to a multiple of 4, keeps -2 the PAINTED
  plane. Both apply at render (LayerMask::density/feather): density as the
  vector mask's; feather = gaussian sigma = feather px
  (feathered_layer_mask), edge-clamped at the canvas (feather_canvas).
- Zero-area -2/-3 raster masks remain empty gray8 LayerMasks; default color
  supplies coverage everywhere. Never substitute derived vector coverage.
  Save the mask record and empty raw -2 channel on pixel, shape, adjustment,
  and group layers, retaining bounds, density, feather and disabled/link
  state. Tests: `psd_empty_user_masks_*`, `psd_empty_real_user_mask_*`,
  `psd_testy_legacy_fills_and_masks_round_trip_if_available`.
- COM gotchas: vectorMaskFeather/Density setd needs the vector mask path
  selected first; feather needs its OWN setd call.

## Document path image resources and PSB

Resource-id constants live in src/core/document_path.hpp.

- Saved paths: resources 2000..2997, resource NAME = path name, payload =
  raw 26-byte record stream (selector 6, selector 8, then subpaths; the vmsk
  grammar WITHOUT the version/flags header).
- Work path: resource 1025, same payload, no name.
- Clipping path selector: resource 2999: pascal path name (even-padded) +
  4 zero bytes + 0x01 (trailing bytes recorded verbatim; re-emit as
  captured).
- PS re-sorts/upserts these like any resource; Patchy preserves unknown ones
  wholesale.
- PSB: all vector keys use the 8BIM signature + 4-byte length form (none are
  in the 8-byte-length set, `tagged_block_length_is_u64` in
  src/psd/psd_io_common.cpp). Fixture: photoshop-shape.psb.
