# Vector tools: pen paths, shape layers, vector masks, Paths panel

Feature deep-dive for Patchy's vector workflows. This document carries the PSD
vector-data encoding notes (all pinned by observation of Photoshop 27.8 via COM
scripting, July 2026 - never from Adobe's specification text), the fixture
inventory, the tool/UI behavior contracts, and the patent record.
Implementation status: complete (July 2026) - shape tools with Shape/Path/
Pixels modes, pen and path-editing tools, the appearance dialog and fill
layers, vector masks, the Paths panel, polygon/custom shapes with the shape
library, and vector-aware geometry ops all shipped; PSD read/write round-trips
per the dirty-or-verbatim rule with COM-verified Photoshop acceptance.

Probe and fixture-generation scripts live in `local-test-fixtures/vector-probe/`
(untracked), including `psd_dump.py`, a standalone PSD structure dumper used for
all byte-level analysis below. Fixtures are self-authored via COM per the repo
method rules.

## Shape tools (Line / Rectangle / Ellipse)

The draw tools carry a Shape | Path | Pixels mode combo (persisted
`tools/vectorToolMode`, default Shape for Photoshop parity). Shape-mode drags
preview with the ACTUAL appearance (options-bar fill and stroke, pulled at
draw time through shape_preview_appearance_callback_ so no per-canvas mirror
can desync; pattern fills preview as textured QBrushes with the placement
composed onto the view transform, gradients as ObjectMode QGradient
approximations; stroke previews centered, arrowheads appear at commit) - but
only on the Content edit target: mask/channel/quick-mask targets keep their
raster previews and the vector-mask target keeps the outline. Shape mode creates
a shape layer from the released drag: the live-shape parameters (rect, rounded
rect via the Radius option, ellipse, line with Weight) generate the path, and
the options-bar fill and stroke paints become the layer's appearance (stroke
alignment defaults to Inside, Photoshop's new-shape default). The Combine
option (New Layer / Add / Subtract / Intersect / Exclude) appends the drag to
the active shape layer as a new shape group with that operation instead. Path
mode appends the same subpaths to the document work path. Pixels mode is the
legacy raster commit, byte-identical to the pre-vector behavior. Mask,
channel, and quick-mask edit targets always take the raster path, so shapes
remain usable as mask-painting tools.

The options-bar Fill and Stroke swatches (July 2026) are popup pickers: No
Fill (fill only) / Solid Color... / Gradient... / Pattern..., backed by two
application-wide `VectorFill` mirrors (`current_vector_fill_`,
`current_vector_stroke_paint_` in MainWindow). Gradient picks resolve a
GradientLibrary preset's FG/BG stops at pick time (shared
resolve_gradient_definition); pattern picks come from the Pattern Manager and
adopt into the document store at commit (`ensure_vector_fill_patterns` in
create_or_extend_shape_layer - the Patt-block hard-refusal rule). Persisted
keys: the historical vectorFillColor/vectorStrokeColor plus append-only
vectorFillKind/vectorFillPatternId/vectorFillGradientId and the
vectorStrokePaintKind/PatternId/GradientId trio; gradient and pattern
PLACEMENT deliberately resets each launch (the appearance dialog owns
per-layer tuning). Selecting an editable shape layer syncs the controls to
that layer, edits apply to it live (one "Shape appearance" undo entry per
gesture; the width spin debounces a burst into one entry via
vector_appearance_apply_timer_), and the synced values stick as the
next-shape defaults - Photoshop's behavior. The same controls register for
Path Select / Direct Select, visible there only while an editable shape layer
is active. A pending debounced apply outranks passive sync (the guard in
sync_shape_appearance_options_from_active_layer), and a stale debounced apply
no-ops through the fill/stroke equality check.

## Pen tool

The Pen (hotkey P) draws bezier paths anchor by anchor: click places a corner
anchor, click-drag pulls symmetric smooth handles (Alt while dragging breaks
the pair), clicking the first anchor closes and commits, Enter commits the
open path (it fills its implied chord, the Photoshop open-subpath rule),
Backspace/Delete pops the last anchor, and Escape cancels the session. Tool
switches and document switches commit/cancel respectively. In Shape mode a
committed path becomes a shape layer (or extends the active one per the
Combine option); any other mode routes to the work path. The construction
overlay draws in canvas_widget_vector_tools.cpp - note canvas_widget_pen.cpp
is TABLET input, not this tool.

Pen feedback (July 2026 UX pass): the cursor is a badge crosshair that
advertises what a click will do - plus over a segment (insert), minus over an
anchor (delete), a caret with Alt over an anchor (convert), a small ring over
the first anchor when closing is possible, plain otherwise. One classifier
(`pen_hover_hit`) drives both the cursor and the click editor so they can
never disagree; badge cursors are cached per state next to the selection-tool
cursors (canvas_widget_cursors.cpp), and modifier-only refreshes ride the
folded-modifier key filter (never live `keyboardModifiers()`). Holding Ctrl
temporarily acts as Direct Select (the arrow cursor shows it): with no session
it latches the gesture onto the path-edit handlers (drag anchors or handle
knobs, one "Edit path" undo entry); mid-session it drags an anchor of the
in-progress path without adding one. Ctrl clicks never insert, delete, close,
or extend. The first anchor of a session also emits a one-line status hint
with the close/commit/cancel keys.

## Polygon, Custom Shape, and line arrowheads

The Polygon tool drags center-out (the first vertex tracks the cursor) with a
Sides option and a Star inset percent (0 = plain polygon); Custom Shape stamps
a library shape into the drag rect (Shift keeps it square). Both are
vector-only, so the options-bar mode combo greys out its Pixels entry for
them (and for the Pen) and displays the effective mode (Path) when the
persisted `tools/vectorToolMode` is Pixels - the setting itself stays
untouched for the raster-capable Line/Rect/Ellipse. They write plain paths -
Photoshop's
polygon/custom origination descriptors were not probed, so per the fallback
policy no origination is invented and PS opens them as regular path shapes.
The Line tool gains arrow start/end checkboxes (head width 5x and length 10x
the weight, the Photoshop default proportions) encoded through the probed
keyOriginLine arrow keys. The CustomShapeLibrary (JSON sidecars under
settings/shapes, unit-box normalized paths via the v1 text codec) ships 17
code-generated builtins (arrows and symbols; ids shape.builtin.* are
append-only, and builtin geometry is code-authoritative: restore_default_shapes
runs at startup and rewrites any builtin sidecar whose stored path differs
from the code's, keeping user renames), and Edit > Define Custom Shape from
Path normalizes the current path into a new user entry.

## Path editing (Path Select / Direct Select)

Path Select (A, the black arrow) selects and drags whole shape groups; Direct
Select (Shift+A, the white arrow) works per anchor: click or marquee to
select, drag anchors or the handle knobs of selected anchors (smooth pairs
mirror; a collapsed handle sitting on its corner anchor is not grabbable -
the anchor drag wins), Shift adds to the selection, arrows nudge (1 px, Shift 10 px,
coalesced into one history entry per burst), Delete removes the selected
anchors (subpaths that drop under two anchors disappear), Escape deselects.
With a selection active the options-bar Combine box rewrites the selected
shapes' combine operation in place. The Pen doubles as the point editor:
clicking a segment of the target path inserts an anchor with an exact
de Casteljau split, clicking an anchor deletes it, and Alt+click toggles
corner/smooth. Any direct edit drops the touched groups' live-shape
annotations (Photoshop's keyShapeInvalidated rule) and re-rasterizes on the
spot; the target is the active shape layer, falling back to the work path.

## Vector mask UI

Layers with a vector mask grow a third row thumbnail (grayscale of the baked
coverage, with the density and disabled-cross conventions). Click targets the
mask path for the pen/path tools (the VectorMask edit target; raster painting
refuses), Ctrl-click loads the coverage as a selection, Alt-click toggles the
grayscale mask view, Shift-click disables the mask. Layer > Vector Mask offers
Reveal All (empty path = full coverage), Hide All (empty inverted path),
Current Path (copies the work path), Delete, Disable, and Rasterize (bakes the
coverage - density and any raster mask multiplied in - into the layer mask).
While the vector-mask target is active, shape-tool drags and pen commits
append subpaths to the mask path instead of creating layers.

## Paths panel

The Paths dock tabifies with Channels: filled coverage thumbnails (a
thumbnail-space rasterize so boolean subtract/intersect/xor holes read
correctly, under a 1 px outline) for every saved path, the work path (italic,
listed last), and a transient row for the active layer's shape or vector-mask
path. Selecting a row targets it for the pen and path tools (the explicit
selection outranks the layer/work-path fallback); clicking empty space
deselects. Double-click saves the work path under a generated name (the row
drops straight into inline rename, and the promoted path moves to the END of
the list - PS placement; DocumentPath::set_kind drops the stale 1025 resource
source so the writer allocates a saved-range id,
psd_work_path_saved_as_named_round_trips). Ctrl-click (Cmd on macOS) loads a
row's path as a selection without changing the targeting (the channel-panel
convention), and Ctrl+Enter on the CANVAS does the same for the targeted row
- deliberately a canvas key, not an app shortcut: the inline text editor owns
a window-scoped Ctrl+Return while it exists, and session commit keys
(transform/warp) keep priority in keyPressEvent. Saved rows drag-reorder among themselves (the layer row stays
first, the work path last; the panel reverts frame-breaking drops - the
channel-panel pattern); the PSD writer assigns the sorted saved-range id set
by document order so a reorder survives the round trip with verbatim payloads
(psd_saved_paths_reorder_round_trips). Three writer invariants keep that
safe: new paths allocate ABOVE the highest stored id (adding a path never
relocates a clean sibling into a gap), stored ids outside 2000..2997 never
enter the saved set, and the path-range stream entries are normalized to
ascending id order after upserts (upsert appends brand-new ids at the end,
which would otherwise diverge from the id-sorted order Patchy's reader and
Photoshop both reconstruct).

Targeting drives the canvas overlay (July 2026, Photoshop's target-path
display): while any row is selected, the path outline draws with EVERY tool
via CanvasWidget::panel_path_targeted_; anchors/handles stay path-tool-only.
View > Show Target Path (Ctrl+Shift+H, view.target_path) hides the whole
overlay without touching the targeting - deliberately NOT persisted, every
launch starts visible - though an active path-transform session always draws
its box. Canvas-side path mutations (direct-select edits, transforms,
vector-mask appends) fire path_edited_callback_ so the panel rows and
thumbnails follow live; the saved-path AND transient-layer-row thumbnails are
revision-keyed caches, keeping those refreshes cheap. The context menu's
checkable Clipping Path entry designates ONE saved path as the document
clipping path (resource 2999; the row's name underlines, and designating
another path clears the previous one).
Drawing into the work path auto-selects its row, and activating a shape or
vector-mask layer auto-targets the transient row. Dismissal is per layer:
empty-space click or Escape (path tools, second stage after clearing the
anchor selection, via the path-display dismiss callback) hides the row and
MainWindow::path_row_hidden_for_layer_ keeps it hidden until the layer
changes, the user re-clicks the row, or a new drag commits. Note a path tool
still displays its edit-target fallback after a dismissal - it shows what it
would edit; only non-path tools go outline-free.

Footer commands: New Path (empty, immediately targeted), Fill Path (a
persisted options dialog: foreground/background color or a PATTERN from the
document store + pattern library with Scale/Angle/Offset X-Y/Align-with-layer
placement rows - rendered through the shared PatternTileSampler, so the
default 100%/0deg/0-offset fill stays byte-identical to the historical
document-origin tiling, and Align with layer anchors at the target layer's
effects reference point - plus opacity; the placement rows grey out with
non-pattern contents, keys paths/fillPattern{Scale,Angle,OffsetX,OffsetY,
AlignLayer}; raster-only, zero PSD-format impact; palette mode writes hard
snapped pixels via snap_pixel_to_palette
with the coverage threshold), Stroke Path (replays the flattened
path through the BRUSH ENGINE as synthetic input - current tip, size,
opacity, dynamics, foreground color, one "Stroke path" undo entry via the
scripted_stroke_undo_suppressed_ flag; the dialog's persisted Simulate
Pressure option sends tablet events with a sine taper riding the user's
pen-input pressure mapping, and open subpaths do NOT gain the fill-only
implied chord), Make Selection (feather via triple box blur, anti-alias
toggle, New/Add/Subtract/Intersect operations), Make Work Path from Selection
(tolerance dialog 0.5-10 px persisted at paths/makeWorkPathTolerance, default
2.0; traces the hard selection region and fits it via core/path_fit -
Douglas-Peucker corners + Schneider least-squares cubics; outer loops become
Add subpaths, holes Subtract, in tracer order), and Delete Path. Duplicate
Path lives in the row context menu ("<name> copy", uniquified). The row
commands enable only while a row is selected (New Path and Make Work Path
just need a document); the panel refreshes those states on selectionChanged
as well as currentItemChanged because a real mouse click updates the current
item before the selection commits, and update_document_action_state
re-applies the row rule after its blanket document-action pass (the
channel-panel pattern; pinned by ui_paths_panel_actions_follow_row_selection).
Saved paths round-trip through the PSD path resources from phase 8.

## Path free transform

Ctrl+T with Path Select or Direct Select active (and a targetable path)
starts a PATH transform session instead of the layer one: a rotated-box
overlay over the path - or over the Direct Select anchor subset (Photoshop's
Free Transform Points) - with move (drag inside), scale (eight handles;
Shift on a corner keeps proportions; negative extents flip), and rotate
(drag outside; Shift snaps 15 degrees). Arrows nudge, Enter commits, Esc
cancels; tool switches commit and document switches cancel (the pen-session
rules). The commit is ONE apply_path_edit undo entry ("Transform path"), so
it routes to whichever target is active (panel path, vector mask, shape
layer - dropping touched groups' live annotations - or work path) and
re-rasterizes. The session lives in canvas_widget_vector_tools.cpp
(path_transform_*), deliberately separate from the pixel transform session;
begin_path_transform is called ONLY from transform_active_layer_dialog so
begin_free_transform's internal callers (options-bar numeric fields, warp
switch, move-tool double-click) stay layer-only, and the Pen falls through
to the layer transform (its mouse handlers would fight a session).

## Geometry operations

Every document-geometry op transforms the vector data alongside the pixels
and re-rasterizes crisply at the new canvas: Image Size scales anchor points
(and the shape stroke width), Canvas Size translates by the anchor offset,
crop translates by the crop origin (canvas-relative PSD records depend on
this), the 90-degree rotates map edge coordinates, and the per-layer flips
mirror about the layer's pixel-bounds center. Free Transform applies its
affine delta to the path model and re-rasterizes instead of resampling (the
text-layer pattern), so scaled shapes stay sharp; Move already translated the
model. Live-shape annotations survive positive axis-aligned scale + translate
and are dropped otherwise (the path stays exact), matching the
keyShapeInvalidated rule. Saved and work paths ride the document-level ops
too. Warp still refuses on vector layers (convert to a smart object or
rasterize first).

## Appearance editing and fill layers

Shape/fill layers carry a vector badge on their layer-list row
(layerVectorBadgeButton, the badge-fx pattern): their pixels are a baked
cache, so the thumbnail alone cannot reveal the vector content. Clicking the
badge opens the Shape Appearance dialog.
Double-clicking a shape layer's row (or an imported fill layer's) opens the
Shape Appearance dialog; the layer context menu offers the same editor as
"Edit Shape Appearance..." directly after Edit Layer Styles (which stays the
first item, per the standing rule). The dialog covers fill kind (none / solid
/ gradient / pattern with library presets, gradient style/angle/scale/reverse,
pattern scale/angle/offset X-Y/align-with-layer) and the full stroke set
(width, a Paint combo choosing solid color / gradient / pattern content with
the same per-kind rows as the fill - so PSD-authored gradient and pattern
strokes display truthfully and are editable - plus inside/center/outside
alignment, caps, joins, dash presets and a Custom entry preserving
PSD-authored dash arrays); the stroke rows grey out while the stroke checkbox
is off, and the per-paint-kind stroke rows hide like the fill section's. The
pattern align-with-layer checkbox maps `pattern_linked`: anchored to the
layer's effects reference point when on, the document origin when off, offsets
adding on top in both cases (the PatternTileSampler rule). Single-live-shape layers additionally get a Geometry
section (rect/rounded bounds + per-corner radii - a radius promotes a plain
rect to rounded - ellipse bounds, line endpoints/weight): edits regenerate
the group's subpaths via generate_live_shape_subpaths and the shape STAYS
live (a parameter edit, not a direct path edit; dialog-based editing is the
patent-cleared route, on-canvas gizmos stay excluded). The section only
appears when the layer has exactly one modeled origination and every subpath
belongs to that group. Pattern adoption self-heals: PatternStore::adopt
replaces a same-id entry whose tile is EMPTY (a poisoned earlier adopt would
otherwise render transparent forever and block re-adoption). Edits preview live on the layer and restore on cancel;
a gradient or pattern stroke paint read from a PSD is kept untouched unless
the stroke color is explicitly re-picked. The preview rasterizes on a
BACKGROUND worker (AsyncPixelPreviewState, promoted to main_window_shared.hpp;
pattern fills at small scales cost seconds) behind the canvas processing
overlay: the vector MODEL applies synchronously in start, only the baked
pixels lag, requests coalesce, and the pattern-anchor reference point rides a
scratch layer so the live Layer is never touched off-thread. Accepting drains
the in-flight render and commits ITS result (no second rasterize; a 60s
drain timeout falls back to the synchronous apply). The dialog's spins run
with keyboardTracking off so typing "10" into the pattern scale never renders
at "1" first. Edit > Define Custom Shape from Path
prompts for the shape name (prefilled with the generated fallback). Layer > New Fill Layer creates Solid Color (live color
picker), Gradient (foreground-to-background linear, then the dialog), and
Pattern fill layers as shape layers with an empty path (= whole canvas); a
TARGETED Paths-panel row becomes the new layer's shape path (Photoshop's
"current path" rule, build_fill_layer), and an active selection still becomes
the raster mask, Photoshop-style. Library
patterns adopt into the document PatternStore on use so the rasterizer and
PSD writer resolve them.

## Photoshop file encodings (observed, PS 27.8 / July 2026)

### Shape and fill layer structure

- A shape layer is an ordinary layer record carrying a fill content block
  (`SoCo` solid / `GdFl` gradient / `PtFl` pattern) plus a `vmsk` vector mask
  block; live shapes add `vogk` (+ a 4-byte `vowv` = u32 2 beside it; PS wrote
  vowv for rect and line kinds but not ellipse); stroked shapes add `vstk`.
- **Two hard open-refusal rules (July 2026, pinned by byte bisection of a
  rejected user file with COM open tests; both produce "Could not open ...
  because of a program error"):**
  1. Every pattern id referenced by a `PtFl` fill or a `vstk` pattern stroke
     paint MUST resolve to pattern data in the file's `Patt`/`Pat2`/`Pat3`
     blocks. Photoshop falls back to its OWN loaded presets by GUID (which
     masked this in early fixture tests - the fixture's checker lives in this
     machine's PS presets), and hard-refuses the whole file when the id
     resolves nowhere. The writer collects vector fill/stroke pattern ids
     alongside style ids (`collect_referenced_pattern_ids(Layer&)`), and a
     referenced id with no usable tile anywhere is written as a 1x1 fully
     transparent placeholder tile (renders as no paint, matching Patchy's
     missing-pattern render; `PatternStore::adopt` treats such tiles as
     heal-able so re-picking the real pattern recovers).
  2. A `vogk` whose keyDescriptorList covers only SOME of the vmsk subpath
     groups is rejected in every index permutation (partial-list, renumbered,
     resolution-patched variants all refused; full-coverage single-entry
     files open). A mixed live/non-live layer (e.g. polygon + live ellipse
     via Combine) therefore writes NO vogk/vowv at all
     (`origination_covers_path_groups` gates the writer, and the reader keeps
     partial raw vogk/vowv out of the preserved blocks so damaged files heal
     on resave); the shapes open as plain paths, PS's own fallback, and only
     live-parameter editability is lost. How PS itself encodes mixed layers
     in a FILE was not probed (headless authoring of the mixed state was not
     achieved); if that capture is ever made, matching it could restore
     mixed-layer liveness round-trips.
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
- CS4-era files write length records with the combine op UNSET (0xFFFF, and 0 in
  the constant-1 field; the first subpath observed as op 1 / constant 2 instead).
  Legacy shapes fill by subpath parity: the reader maps 0xFFFF to xor, which the
  sequential-combine renderer reproduces exactly (nested cutouts become holes,
  matching Photoshop's own composite of the 2010 Android icon-template file).
  Pinned by `psd_legacy_vmsk_unset_combine_op_fills_by_parity`; real-file
  coverage rides `psd_16_bit_flat_filter_list_loads_if_available`.
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

### GdFl gradient fill geometry (calibrated July 2026, probe5c/5d/5e)

Separating span from easing with smoothness-0 probes pinned the fill-layer
renderer exactly:

- Linear span = the CENTER CHORD of the aligned bounds:
  min(w/|cos a|, h/|sin a|), centered on the bounds center (measured within
  0.5 px at angles 0/20/37/60/75/90 on full-canvas and inset-rect layers; the
  earlier "matches neither" note came from conflating easing with span). This
  intentionally differs from the corner-to-corner projection layer-style
  overlays use - overlays keep their separately pinned calibration via
  GradientSpanBasis::LayerProjection.
- Classic easing applies even to TWO-stop ramps: per-segment catmull-rom with
  duplicated virtual endpoints (f(t) = 0.5t + 1.5t^2 - t^3 for a plain 2-stop
  ramp), scaled by smoothness/4096. The OPACITY ramp eases identically
  (probe5e-alpha). Midpoints are the piecewise-linear law through
  (midpoint, 50%) and apply BEFORE the ease (probe5e-mdpn30s).
- gradient_color/gradient_stop_opacity expose this via the
  endpoint_smoothing flag; the vector fill painter passes it, layer styles
  keep the historical default.

### Known render divergences (July 2026)

- GdFl with UNEVENLY spaced stops: Photoshop parametrizes its smoothness
  spline non-uniformly by stop location; Patchy's uniform per-segment
  catmull differs by a few /255 near uneven stops (gradient fixture: mean
  1.2, max 8). A closed form was not identified from probes
  (probe5e-ms3uneven); revisit only if it ever shows visually.
- Stroke dashes: dash boundaries land where each renderer's arc-length
  integration puts them; sub-pixel flattening differences flip a handful of
  dash-edge pixels (mean delta ~0.3 on the strokes fixture).
- ROTATED pattern fills: the placement mapping is pinned exactly
  (R(angle) @ (p - anchor) / scale, 100% cell-classification agreement on the
  July 2026 full-canvas probes), but Photoshop resamples rotated tiles with
  its own soft per-cell filter (cells render slightly shrunken with light
  gutters), so per-pixel deltas along cell edges are large while the
  structure matches. psd_pattern_params_probe_render_parity_if_available
  therefore checks confident-cell classification agreement (>= 97%), not
  pixel means. Patchy's crisper render is deliberate (the unrotated linear
  tap applied in rotated space).

### Stroke outline winding (fixed July 2026)

The stroker builds the band as a union of per-segment quads plus join/cap
wedges rasterized under the nonzero rule; every loop must carry the SAME
orientation (append_outline_loop normalizes by signed area). Join wedges
were previously emitted with the turn-direction-dependent winding, which
CANCELLED the segment quads a wedge overlapped - invisible while wedges
stayed inside their own corner gap, but a wide stroke on a densely
flattened large-radius arc (short quads, long wedges) showed hatched
notches near the arc-to-straight junction, dependent on width/geometry
(the vectors_overlay_stroke.psd frame corner;
stroke_arc_band_has_no_winding_notches pins the fix, and the stroke golden
digests were deliberately re-pinned for it).

### Interior effects vs the vector stroke (probed July 2026)

The fx-sofi-center/outside/nofill and fx-drsh-outside probes pinned where
layer effects sit relative to a shape layer's vector stroke: interior
overlays (Color/Gradient/Pattern Overlay) apply to the FILL plane only and
the VECTOR STROKE composites above them; on a stroke-only shape (fill
disabled) the overlay covers the stroke itself; drop shadows (and the
silhouette generally) key off the full fill+stroke coverage; the Stroke
EFFECT (frFX) stays above the vector stroke. Implementation: the shape bake
emits split fill/stroke planes (ShapeRasterResult::fill_pixels/stroke_pixels,
cached on VectorShapeContent::fill_cache/stroke_cache in lockstep with
pixels(); empty for strokeless shapes, non-Normal stroke blends, or
un-rebaked imports) and the compositor's overlay passes read the fill plane
with a stroke re-stamp after Color Overlay
(compositor_interior_overlay_stays_under_vector_stroke and the _if_available
probe test pin it). Blend-If layers and transform-preview overrides keep the
legacy combined-plane behavior. An inner shadow whose extent hides under the
stroke matches PS either way (the fx-irsh probe render is fully covered by
the stroke), so inner effects keep their full-silhouette geometry.
- Photoshop's baked derived mask plane (mask flags bit 3) holds UNFEATHERED
  path coverage; the feather parameter applies at render time. Patchy bakes
  its own feathered cache (triple box blur, radius ~ feather/2) - close but
  not gaussian-exact.

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

Untracked local fixture: `local-test-fixtures/psd/vectors_from_patchy.psd` is
the July 2026 damaged user file (pre-fix Patchy wrote a pattern fill with no
Patt block AND a partial vogk; Photoshop refused it). The `_if_available`
test `psd_damaged_pattern_file_resave_is_photoshop_safe_if_available` pins
that resaving it through current Patchy removes both defects.

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
- Selection-to-path conversion (Make Work Path) uses classic published
  methods only: marching-style boundary tracing, Douglas-Peucker (1973)
  simplification, and Schneider's least-squares cubic fitting ("An Algorithm
  for Automatically Fitting Digitized Curves", Graphics Gems, 1990) -
  Photoshop has shipped the equivalent since version 3 (1994); any patents
  are long expired.

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
