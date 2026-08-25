# Vector tools: pen paths, shape layers, vector masks, Paths panel

Feature reference for Patchy's vector workflows: tool/UI behavior contracts,
PSD vector-data encodings, fixtures, and the patent record. Encoding facts
were pinned by observing Photoshop 27.8 via COM (July 2026; method rules at
the end). PSD read/write round-trips per the dirty-or-verbatim rule with
COM-verified Photoshop acceptance. Probe and fixture scripts (including the
`psd_dump.py` structure dumper) live in `local-test-fixtures/vector-probe/`
(untracked). Binding constraints: docs/legal-constraints.md.

## Shape tools (Line / Rectangle / Ellipse)

Draw tools carry a Shape | Path | Pixels mode combo (persisted
`tools/vectorToolMode`, default Shape). Shape-mode drags preview the actual
options-bar fill and stroke read at draw time; only the Content edit target
previews this way, and mask/channel/quick-mask targets always take the
raster path. Release creates a shape layer: live-shape parameters (rect,
rounded rect via Radius, ellipse, line with Weight) generate the path, and
the options-bar paints become the appearance (stroke alignment defaults to
Inside, PS's default). The Combine option (New Layer / Add / Subtract /
Intersect / Exclude) instead appends the drag to the active shape layer as a
new shape group with that op. Path mode appends the same subpaths to the
work path; Pixels mode is the legacy raster commit, byte-identical.

The Fill and Stroke swatches are popup pickers (No Fill / Solid / Gradient /
Pattern) backed by app-wide `VectorFill` mirrors. Gradient picks resolve the
preset's FG/BG stops at pick time; pattern picks adopt into the document
store at commit (`ensure_vector_fill_patterns`, honoring the Patt-block
refusal rule below). Kind and preset ids persist under
vectorFill*/vectorStrokePaint* keys; gradient/pattern PLACEMENT resets each
launch. Selecting an editable shape layer syncs the controls (also shown for
Path Select / Direct Select); edits apply live (one "Shape appearance" undo
per gesture, width spin debounced) and stick as next-shape defaults.

## Pen tool

The Pen (P) draws bezier paths anchor by anchor: click places a corner,
click-drag pulls symmetric smooth handles (Alt breaks the pair), clicking
the first anchor closes and commits, Enter commits the open path (it fills
its implied chord, the PS open-subpath rule), Backspace pops the last
anchor, Escape cancels; tool switches commit, document switches cancel.
Shape mode turns the committed path into a shape layer (or extends the
active one per Combine); other modes route to the work path. The
construction overlay draws in canvas_widget_vector_tools.cpp
(canvas_widget_pen.cpp is TABLET input, not this tool).

A badge crosshair cursor advertises the click action (insert/delete/convert/
close); one classifier (`pen_hover_hit_raw`, narrowed per tool and by the
Auto Add/Delete option) drives cursor, click editor, and right-click menu so
they never disagree; details in [vector-commands.md](vector-commands.md).
Holding Ctrl acts as Direct Select: with no session it latches the gesture
onto the path-edit handlers (one "Edit path" undo entry); mid-session it
drags an in-progress anchor without adding one. Ctrl clicks never insert,
delete, close, or extend; Delete removes a Ctrl-selected anchor.

## Polygon, Custom Shape, and line arrowheads

Polygon drags center-out with Sides and a Star inset percent (0 = plain);
Custom Shape stamps a library shape into the drag rect (Shift keeps it
square). Both are vector-only: the mode combo greys out Pixels for them
(and the Pen) and shows the effective mode (Path), leaving the setting
untouched. They write plain paths (PS's polygon/custom origination
descriptors were not probed), which PS opens as regular path shapes. The
Line tool gains arrow start/end checkboxes (head width 5x, length 10x the
weight, PS's proportions) encoded through the probed keyOriginLine arrow
keys. The CustomShapeLibrary (JSON sidecars
under settings/shapes, unit-box paths, v1 text codec) ships 17 builtins:
ids shape.builtin.* are append-only, geometry is code-authoritative
(restore_default_shapes rewrites drifted builtin sidecars, keeping user
renames). Edit > Define Custom Shape from Path adds a user entry.

## Path editing (Path Select / Direct Select)

Path Select (A, black arrow) selects and drags whole shape groups. Direct
Select (Shift+A, white arrow) works per anchor: click or marquee selects,
drag moves anchors or handle knobs (smooth pairs mirror; a collapsed handle
on its corner anchor is not grabbable), Shift adds, arrows nudge (1 px,
Shift 10 px, coalesced per burst), Delete removes selected anchors (subpaths
under two anchors disappear), Escape deselects. With a selection, the
options-bar Combine box rewrites the selected shapes' op in place. The Pen
doubles as the point editor: click a segment to insert an anchor (exact
de Casteljau split), click an anchor to delete it, Alt+click toggles
corner/smooth. Any direct edit drops the touched groups' live-shape
annotations (PS's keyShapeInvalidated rule) and re-rasterizes; the target is
the active shape layer, else the work path.

## Vector mask UI

Layers with a vector mask grow a third row thumbnail (grayscale coverage,
density and disabled-cross conventions). Click targets the mask path for
pen/path tools (raster painting refuses), Ctrl-click loads the coverage as
a selection, Alt-click toggles the grayscale view, Shift-click disables.
Layer > Vector Mask: Reveal All (empty path = full coverage), Hide All
(inverted empty path), Current Path (copies the work path), Delete,
Disable, Rasterize (bakes coverage, density and any raster mask multiplied
in, into the layer mask). While the vector-mask target is active, shape
drags and pen commits append subpaths to the mask path.

## Paths panel

Tabifies with Channels. Rows: saved paths (filled coverage thumbnails so
boolean holes read, 1 px outline), the work path (italic, last), and a
transient row for the active layer's shape or vector-mask path. Selecting a
row targets it for pen/path tools (outranking the layer/work-path fallback);
empty-space click deselects. Double-click saves the work path: inline
rename, row moves to the END (DocumentPath::set_kind drops the stale 1025
resource source so the writer allocates a saved-range id;
psd_work_path_saved_as_named_round_trips). Ctrl-click (Cmd on macOS) loads a
row's path as a selection without changing targeting; Ctrl+Enter on the
CANVAS does the same for the targeted row (deliberately a canvas key, not an
app shortcut).

Saved rows drag-reorder among themselves (frame-breaking drops revert); the
writer assigns the sorted saved-range id set by document order so reorders
round-trip with verbatim payloads (psd_saved_paths_reorder_round_trips).
Writer invariants: new paths allocate ABOVE the highest stored id, ids
outside 2000..2997 never enter the saved set, path-range stream entries
normalize to ascending id order after upserts.

While any row is selected its outline draws with EVERY tool;
anchors/handles stay path-tool-only. Under a path tool the overlay also
outlines every Layers-panel-selected shape layer with hollow anchors
(`set_panel_selected_layer_ids`, pushed from `refresh_layer_controls`);
only the target path gets filled anchors, handles, and edits. View > Show Target Path (Ctrl+Shift+H,
view.target_path) hides the overlay without touching targeting (not
persisted; a path-transform session always draws its box). Ctrl+H
(view.selection_edges, PS's Extras toggle) hides it with the selection
edges; a new selection re-shows both
(ui_ctrl_h_hides_path_points_with_selection_edges). Canvas-side edits
refresh rows and thumbnails live. The context menu's Clipping Path entry
designates ONE saved path as the document clipping path (resource 2999;
name underlines; exclusive). Work-path draws and layer activation
auto-select/target their rows; activating a layer with its own path drops
a stale work/saved-path target (vector-commands.md). Dismissal (empty
click, or Escape after clearing the anchor selection) sticks per layer
until the layer changes, the row is re-clicked, or a new drag commits; a
path tool still draws its edit-target fallback afterward.

Footer commands (row commands need a selected row; the panel refreshes on
selectionChanged AND currentItemChanged;
ui_paths_panel_actions_follow_row_selection pins it):

- New Path: empty, immediately targeted.
- Fill Path: persisted dialog; FG/BG color or a PATTERN with
  Scale/Angle/Offset/Align-with-layer rows (shared PatternTileSampler;
  defaults match the historical document-origin tiling; Align anchors at the
  layer's effects reference point) plus opacity; raster-only; palette mode
  snaps via snap_pixel_to_palette.
- Stroke Path: replays the flattened path through the BRUSH ENGINE as
  synthetic input (one "Stroke path" undo); Simulate Pressure sends tablet
  events with a sine taper; open subpaths do NOT gain the fill-only implied
  chord.
- Make Selection: feather (triple box blur), anti-alias, combine ops.
- Make Work Path from Selection: tolerance 0.5-10 px (persisted, default
  2.0); traces the hard selection, fits via core/path_fit (Douglas-Peucker
  corners + Schneider cubics); outer loops Add, holes Subtract.
- Delete Path; Duplicate Path in the row context menu ("<name> copy").

## Path free transform

Ctrl+T with Path Select or Direct Select active (and a targetable path)
starts a PATH transform session instead of the layer one: a rotated-box
overlay over the path, or over the Direct Select anchor subset (PS's Free
Transform Points), with the usual move/scale/rotate gestures, arrow nudges,
Enter/Esc, tool-switch commit, document-switch cancel. The commit is ONE
apply_path_edit undo entry ("Transform path") routed to the active target
(panel path, vector mask, shape layer with live annotations dropped, or
work path), then re-rasterizes. Lives in canvas_widget_vector_tools.cpp
(path_transform_*), separate from the pixel session; begin_path_transform
is called ONLY from transform_active_layer_dialog. Corner-handle aspect
locking and the Shift modifier follow the same rules as the pixel session
and share its predicate; see [tools.md](tools.md).

## Geometry operations

Every document-geometry op transforms the vector data alongside the pixels
and re-rasterizes at the new canvas: Image Size scales anchors and stroke
width, Canvas Size/crop translate (canvas-relative PSD records depend on
this), 90-degree rotates map edge coordinates, per-layer flips mirror about
the pixel-bounds center. Free Transform applies its affine delta to the
path model and re-rasterizes instead of resampling, so scaled shapes stay
sharp; Move translates the model. Live-shape annotations survive positive
axis-aligned scale + translate and drop otherwise (keyShapeInvalidated
rule). Saved and work paths ride document ops too. Warp refuses on vector
layers.

## Appearance editing and fill layers

Shape/fill layers carry a vector badge on their layer row (pixels are a
baked cache, so the thumbnail cannot reveal the vector content). The badge,
double-clicking the row, or the context menu's "Edit Shape Appearance..."
(after Edit Layer Styles, which stays first) opens the Shape Appearance
dialog: fill kind with per-kind rows, plus the full stroke set (width; a
Paint combo choosing solid/gradient/pattern content, so PSD-authored
gradient and pattern strokes display truthfully and stay editable;
alignment, caps, joins; dash presets plus a Custom entry preserving
PSD-authored dash arrays). Align-with-layer maps `pattern_linked`: anchored
at the layer's effects reference point when on, the document origin when
off, offsets adding on top either way (the PatternTileSampler rule).

Single-live-shape layers also get a Geometry section (rect/rounded bounds
and per-corner radii, a radius promotes a plain rect to rounded; ellipse
bounds; line endpoints/weight): edits regenerate the subpaths via
generate_live_shape_subpaths and the shape STAYS live (a parameter edit;
dialogs are the patent-cleared route, on-canvas gizmos stay excluded).
Shown only when one modeled origination covers every subpath.

Edits preview live and restore on cancel; a PSD-read gradient/pattern
stroke paint stays untouched unless explicitly re-picked. The preview
rasterizes on a background worker: the vector MODEL applies synchronously,
baked pixels lag, requests coalesce, the pattern anchor rides a scratch
layer; accept commits the in-flight result (60s timeout fallback). Layer >
New Fill Layer creates Solid Color, Gradient (FG-to-BG linear), and Pattern
fill layers as shape layers with an empty path (= whole canvas); a TARGETED
Paths-panel row becomes the new layer's shape path (PS's "current path"
rule, build_fill_layer), and an active selection still becomes the raster
mask. Library patterns adopt into the document PatternStore on use.

## Photoshop file encodings (observed, PS 27.8 / July 2026)

Codec: src/psd/psd_vector.cpp (vmsk/vsms, SoCo/GdFl/PtFl, vstk, vogk, path
resources); model: src/core/vector_shape.hpp.

### Shape and fill layer structure

- A shape layer is an ordinary layer record carrying a fill content block
  (`SoCo` solid / `GdFl` gradient / `PtFl` pattern) plus a `vmsk` vector
  mask block; live shapes add `vogk` (+ a 4-byte `vowv` = u32 2 beside it;
  PS wrote vowv for rect and line kinds but not ellipse); stroked shapes add
  `vstk`.
- **Two hard open-refusal rules** (pinned by byte bisection of a rejected
  user file with COM open tests; both produce "Could not open ... because of
  a program error"; regression-test names in ps-compat.md):
  1. Every pattern id referenced by a `PtFl` fill or a `vstk` pattern
     stroke paint MUST resolve to pattern data in the file's
     `Patt`/`Pat2`/`Pat3` blocks. PS falls back to its OWN loaded presets
     by GUID (which can mask the bug) and hard-refuses the file when the id
     resolves nowhere. The writer collects vector fill/stroke pattern ids
     alongside style ids (`collect_referenced_pattern_ids`) and writes a
     1x1 fully transparent placeholder tile for any id with no usable tile
     (renders as no paint; `PatternStore::adopt` heals such tiles on
     re-pick).
  2. A `vogk` covering only SOME of the vmsk subpath groups is rejected. A
     mixed live/non-live layer therefore writes NO vogk/vowv at all
     (`origination_covers_path_groups` gates the writer; the reader keeps
     partial raw vogk/vowv out of the preserved blocks so damaged files
     heal on resave). The shapes open as plain paths, PS's own fallback;
     only live editability is lost. PS's own encoding of mixed layers
     remains unprobed.
- Channel data is EMPTY: layer bounds (0,0,0,0) and every channel (including
  transparency id -1) is 2 bytes (just the compression marker). Readers must
  rasterize from the vector data. (Writer in src/psd/psd_layer_records.cpp.)
- Layer record flags: bit 3 + **bit 4** (0x18). Bit 4 = "pixel data
  irrelevant"; write it on shape/fill layers.
- `lnsr` = 'cont' for content layers ('bgnd' for Background). PS names:
  "Color Fill 1" (path-created), "Rectangle 1", "Ellipse 1", "Line 1".
- A plain fill layer is the same structure with an empty or absent `vmsk`.
- `vscg`: legacy key, never written by PS 27.8 in any probe; read if
  present, never regenerate (PS's own resave migrates to vstk-only).

### vmsk / vsms (vector mask path)

- Payload: u32 version = 3, u32 flags (bit 0 invert, bit 1 not-linked, bit 2
  disabled), then 26-byte path records, padded to even length. `vsms` is a
  legacy alternate with the same payload; PS 27.8 always writes `vmsk`. Read
  both, write vmsk.
- Record order: one selector-6 record (fill rule; observed all zeros), one
  selector-8 record (initial fill; u16 observed 0), then per subpath a
  length record followed by its knot records.
- Length record (selector 0 = closed, 3 = open), after the u16 selector: u16
  knot count; u16 combine op (**0 = xor, 1 = add/union, 2 = subtract,
  3 = intersect**); u16 constant 1; 4 zero bytes; u32 subpath/origination
  index (0,1,2,... in file order; ties the subpath to its `vogk`
  keyOriginIndex); 10 zero bytes.
- CS4-era files leave the combine op UNSET (0xFFFF; 0 in the constant-1
  field). Legacy shapes fill by subpath parity: the reader maps 0xFFFF to
  xor, which the sequential-combine renderer reproduces exactly. Pinned by
  `psd_legacy_vmsk_unset_combine_op_fills_by_parity`; real-file coverage
  rides `psd_16_bit_flat_filter_list_loads_if_available`.
- Knot records: selector 1 (closed smooth/linked), 2 (closed corner), 4
  (open smooth), 5 (open corner). Three coordinate pairs, each (y then x),
  each value i32 8.24 fixed point as a FRACTION of the canvas dimension
  (y/height, x/width). Pair order: **control toward the PREVIOUS anchor
  (in), then the anchor, then the control toward the NEXT anchor (out)**,
  pinned numerically against PS's live-ellipse knots and a
  rule-distinguishing donut render. Corner knots store all three pairs equal
  when no handles.

### Render semantics (pinned by fixture BMPs)

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
- Multi-subpath groups sharing one keyOriginIndex (custom-shape stamps) are
  expected to even-odd within the group; not yet pinned by a capture, so the
  reader keeps per-subpath raw op fields as a fallback.

### SoCo / GdFl / PtFl (fill content)

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

### vstk (stroke)

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

### vogk (vector origination / live shapes)

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
  LengthArrowUnitPixels, keyOriginBoxCorners, keyOriginIndex. Arrowed lines
  could not be authored headlessly, so the arrow key names/types come from
  the plain line's defaults; Patchy-authored arrows are acceptance-verified
  by reopening in PS.
- App-level (`executeActionGet`) path-drawn subpaths report keyActionMode
  entries instead of live-shape data.

The knot constructions Patchy uses to regenerate live-shape paths (kappa
handles, corner orders) are recorded in src/core/vector_live_shapes.hpp.

### GdFl gradient fill geometry (calibrated July 2026, probe5c/5d/5e)

- Linear span = the CENTER CHORD of the aligned bounds:
  min(w/|cos a|, h/|sin a|), centered on the bounds center (measured within
  0.5 px at angles 0/20/37/60/75/90). This intentionally differs from the
  corner-to-corner projection layer-style overlays use
  (GradientSpanBasis::LayerProjection keeps its own calibration); the two
  bases agree at exact axis angles.
- Classic easing applies even to TWO-stop ramps: per-segment catmull-rom
  with duplicated virtual endpoints (f(t) = 0.5t + 1.5t^2 - t^3 for a plain
  2-stop ramp), scaled by smoothness/4096. The OPACITY ramp eases
  identically. Midpoints are the piecewise-linear law through
  (midpoint, 50%) and apply BEFORE the ease.
- gradient_color/gradient_stop_opacity expose this via the
  endpoint_smoothing flag; the vector fill painter passes it, layer styles
  keep the historical default.

### Known render divergences (July 2026)

- GdFl with UNEVENLY spaced stops: PS parametrizes its smoothness spline
  non-uniformly by stop location; Patchy's uniform per-segment catmull
  differs by a few /255 there (gradient fixture: mean 1.2, max 8); no
  closed form found.
- Stroke dashes: boundaries land where each renderer's arc-length
  integration puts them; a handful of dash-edge pixels flip (mean ~0.3 on
  the strokes fixture).
- ROTATED pattern fills: the placement mapping is pinned exactly
  (R(angle) @ (p - anchor) / scale), but PS resamples rotated tiles with its
  own soft per-cell filter, so cell-edge deltas are large while the
  structure matches; psd_pattern_params_probe_render_parity_if_available
  checks confident-cell agreement (>= 97%), not pixel means. Patchy's
  crisper render is deliberate.

### Stroke rasterization (winding, lattice, bounds)

- The stroker builds the band as a union of per-segment quads plus join/cap
  wedges under the nonzero rule; every loop must carry the SAME orientation
  (append_outline_loop normalizes by signed area), or an opposite-winding
  wedge cancels the quads it overlaps
  (stroke_arc_band_has_no_winding_notches).
- subpath_polyline snaps every vertex (anchors included) to the flattener's
  1/256 lattice so sub-quantum micro-segments cannot seed miter spikes
  (limit 100 admits turns to 178.85 degrees). The coverage band is sized
  from the emitted outline's true hull, and stroke curves flatten through
  the same adaptive flatten_cubic as fills. Pinned by
  stroke_bezier_circle_is_translation_stable,
  stroke_miter_spike_stays_in_bounds,
  stroke_curve_is_insensitive_to_sub_quantum_anchor_jitter, stroke golden 3.
- Known gap: whether PS consumes `strokeStyleMiterLimit` 100 as an SVG-style
  ratio (Patchy's reading; a bare doub, not #Prc) or a percentage; settling
  it needs a COM probe of an acute mitered corner at limit 100 vs 4.

### Interior effects vs the vector stroke (probed July 2026)

The fx-sofi-center/outside/nofill and fx-drsh-outside probes pinned where
layer effects sit relative to a shape layer's vector stroke: interior
overlays (Color/Gradient/Pattern Overlay) apply to the FILL plane only and
the VECTOR STROKE composites above them; on a stroke-only shape (fill
disabled) the overlay covers the stroke itself; drop shadows (and the
silhouette generally) key off the full fill+stroke coverage; the Stroke
EFFECT (frFX) stays above the vector stroke. Implementation: the shape bake emits split fill/stroke planes
(ShapeRasterResult::fill_pixels/stroke_pixels; empty when inapplicable,
then the compositor keeps the combined plane); overlay passes read the
fill plane and re-stamp the stroke after Color Overlay
(compositor_interior_overlay_stays_under_vector_stroke plus the
_if_available probe test; src/render/layer_compositor.hpp). Blend-If layers
and transform-preview overrides keep the legacy combined-plane behavior.
Inner effects keep their full-silhouette geometry.

PS's baked derived mask plane (mask flags bit 3) holds UNFEATHERED path
coverage; the feather applies at render time. Patchy bakes its own feathered
cache (triple box blur, radius ~ feather/2): close but not gaussian-exact.

### Vector masks on layers (mask data section, channels)

- A vector-mask-ONLY layer has NO mask data section and NO baked mask
  channel; the path is the only representation.
- Raster + vector masks together: the ordinary 20-byte mask data section
  holds the raster mask (rect, default color, flags) and the vector mask
  stays purely in vmsk; the raster plane is channel id -2.
- Vector mask density/feather (Properties panel) use the mask-parameters
  form: section flags bit 4 set, then a parameter flags byte (bit 0 user
  density u8, bit 1 user feather f64, bit 2 vector density u8 raw 0..255,
  bit 3 vector feather f64 BE), values in that order. With any vector
  parameter set, PS ALSO bakes a derived coverage plane into channel -2 and
  sets section flags bit 3 ("mask came from rendering other data") with the
  baked rect as the section rect. Reading: use the baked plane until the
  first vector edit, then re-derive.
- COM authoring gotchas: vectorMaskFeather/Density setd requires the vector
  mask path selected first, and feather needs its OWN setd call.

### Document path image resources and PSB

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
  in the 8-byte LARGE_KEYS set). Fixture: photoshop-shape.psb.

## Fixture inventory (test-fixtures/psd, self-authored via COM, July 2026)

Each .psd has a sibling .bmp: Photoshop's own flatten (24-bit, white
background layer in every file) for render-parity tests. The embedded PSD
composites are headless-stale (see ps-compat.md); compare against the BMPs.

- photoshop-shape-solid.psd/bmp: curved shape, SoCo red; pins knot in/out
  order via render.
- photoshop-shape-gradient.psd/bmp: GdFl linear 37 deg, 3 color + 3
  transparency stops with midpoints.
- photoshop-shape-pattern.psd/bmp: PtFl, 8x8 checker in the Patt block.
- photoshop-shape-strokes.psd/bmp: six stroked layers (alignments, caps,
  joins, dashed open curve, stroke-only / fillEnabled false).
- photoshop-shape-boolean.psd/bmp: four subpaths add/subtract/intersect/xor
  (sequential-combine ground truth).
- photoshop-shape-first-ops.psd/bmp: single-subpath layers with op
  subtract/intersect/xor (initial-accumulator semantics).
- photoshop-shape-live-rect.psd/bmp: live rounded rect (radii 4/8/12/16),
  live ellipse, live line w4 (vogk per kind; vowv presence).
- photoshop-vector-mask-on-pixel.psd/bmp: pixel layer + vector mask; no mask
  channel or mask-data section.
- photoshop-both-masks.psd/bmp: raster + vector masks on one layer; second
  layer at density 60% + feather 1.5 px (mask parameters + baked derived -2
  channel, section flags 0x18).
- photoshop-saved-paths.psd/bmp: "Alpha Path" (rect, clipping path), "Beta
  Path" (donut), work path; resources 2000/2001/1025/2999.
- photoshop-shape.psb/photoshop-shape-psb.bmp: PSB variant of the solid
  shape.

## Patents and trademarks (assessed July 2026)

Claim-level record: docs/patent-research.md. docs/legal-constraints.md binds
this section.

Cleared as expired prior art (reasoning, not legal advice): classic
pen-tool bezier editing (Illustrator 88 era); shape layers with editable
fill, vector clipping masks, combine ops, and vector masks (PS 6/7,
patents expired ~2021-2024); boolean path combines, even-odd/nonzero
fills, stroke dashing, caps/joins (decades-old published techniques);
selection-to-path conversion via boundary tracing, Douglas-Peucker (1973),
and Schneider cubic fitting (1990; shipped in Photoshop 3, 1994).

Excluded pending their own review (do NOT build without a new patent
check):

- Curvature Pen tool (2018+) and any auto-fitting curve-through-points UX.
- Edge-magnetic vector snapping. (Image tracing itself was cleared with
  boundaries on 2026-08-23: docs/image-trace.md and the "Vector tracing"
  bullet in docs/legal-constraints.md.)
- Snap-to-pixel "align edges" automatic pixel-grid fitting of vector renders
  (plain user-invoked grid/guide snapping of anchors is fine).
- On-canvas live-shape gizmo widgets (in-canvas radius handles etc.);
  options-bar/dialog parameter editing is the cleared route (Apple
  US 8971623; docs/patent-research.md).
- Variable-width strokes / art brushes on paths (out of scope anyway).

Method rules (same as all PSD work): ground truth is observed output of
licensed Photoshop via COM byte-diffing; no Adobe specification text in the
repo; self-authored fixtures only; referential "compatible with Adobe
Photoshop" phrasing; original tool icons with non-Photoshop geometry.
