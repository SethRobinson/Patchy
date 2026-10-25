# Vector tools: pen paths, shape layers, vector masks, Paths panel

References: [scripting](vector-automation.md), [preview](vector-preview.md), [merging](layer-merging.md), [open strokes](open-path-strokes.md), [point editing and commands](vector-commands.md), [fixtures and render differences](vector-fixtures.md).

PS 27.8 COM probes: `local-test-fixtures/vector-probe/`.
Rules: docs/legal-constraints.md.

## Shape tools (Line / Rectangle / Ellipse)

Shape | Path | Pixels persists as `tools/vectorToolMode` (default Shape).
Shape drags preview options-bar fill/stroke at draw time for the Content
target, with the committed stroke alignment, join and cap (the inside/outside
band clipped by the fill path, as in `rasterize_vector_stroke`; issue 125);
mask/channel/quick-mask targets always rasterize. A drag that commits a work
path or vector-mask subpaths (Path mode, Polygon, Custom Shape) previews the
committed geometry as a path-accent outline (`shape_drag_preview_path`), never
Pixels mode's brush-size paint (`ui_path_mode_drag_previews_path_outline_only`). Release creates a
shape layer from live parameters (rect, rounded rect via Radius, ellipse,
line with Weight) and the options-bar paints. Stroke alignment defaults to
Inside, like PS. Combine (New Layer / Add / Subtract / Intersect / Exclude)
appends a group with that op to the active shape layer (Shift at the press: Add
once; Ctrl: [Path Select](vector-commands.md#tool-roles)). Path mode appends
to the work path; Pixels keeps the raster commit byte-identical.

Fill/Stroke popups (No Fill / Solid / Gradient / Pattern) use app-wide
`VectorFill` mirrors. Gradient picks resolve FG/BG stops immediately; patterns
enter the document store at commit (`ensure_vector_fill_patterns`, honoring
the Patt-block refusal below). Kind and preset ids persist under
vectorFill*/vectorStrokePaint* keys; gradient/pattern PLACEMENT resets each
launch. The controls also serve Path Select / Direct Select and next-shape
defaults; [width 0 = no stroke](options-bar.md#shape-tool-rows) (bar,
dialog); multi-shape edits: [batch appearance](batch-appearance.md).

A bare click with Rectangle, Ellipse, Polygon, or Custom Shape opens the
Create <Shape> dialog (shape_create_dialog.cpp, no shape menu): W and H share
a row, From Center (click = center, else top-left), the title fits.
Rectangle: one linked Radius (unlinked or unequal radii: four corners).
Polygon: Sides; Custom Shape: the bar's picker (`populate_custom_shape_combo`);
both go back to the bar. It commits like a drag (mode routing, Combine,
vector-mask target, naming); values persist per tool for the session.
A click releases on the press's document pixel. Line has no dialog, a
Fixed Size click places its W x H, and Pixels mode keeps its legacy click.

The options-bar W / H spins (`vectorShapeWidthSpin`/`vectorShapeHeightSpin`,
link button for proportional edits) mirror the active shape layer's bounds:
shape tools show them in Shape mode (disabled with no shape layer),
Path Select / Direct Select only with an editable shape layer, not Move. A debounced edit
scales the shape about its top-left through `transform_layer_vector_data`
(live shapes stay live; one "Shape size" undo per edit). The Properties panel
repeats the row (`propertiesShapeSizePanel`) for an active editable shape
layer; both share `handle_vector_shape_size_value_changed`
(`ui_shape_size_controls_follow_move_and_properties_selection`).

## Pen tool

Pen (P): click adds a corner, drag pulls symmetric handles (Alt breaks the
pair; Shift, ends: [more](vector-commands.md#pen-extending-and-ending-paths)), clicking the first anchor closes and commits, Enter commits the open
path (it fills its implied chord, PS's open-subpath rule), Backspace pops
the last anchor, Escape cancels; tool switches commit, document switches
cancel. Shape mode turns the committed path into a shape layer (or extends
the active one per Combine); other modes route to the work path. Overlay:
canvas_widget_vector_tools.cpp.

Badge cursor, hover classifier, Auto Add/Delete, anchor tools:
[vector-commands.md](vector-commands.md). Holding Ctrl acts as Direct
Select: with no session it latches the gesture onto the path-edit handlers
(one "Edit path" undo entry); mid-session it drags an in-progress anchor
(Ctrl+click away ends the path); it never inserts, deletes, closes or
extends. Delete removes a Ctrl-selected anchor.

## Polygon, Custom Shape, and line arrowheads

Polygon drags center-out with Sides and a Star inset percent (0 = plain);
Custom Shape stamps a library shape (unit box, own proportions) into the drag
rect; Shift or the W/H link (live for this tool) keeps it square, so the shape
keeps its proportions; Alt: from center. Both are vector-only: the combo greys out Pixels (and the Pen) and
shows the effective mode (Path) without changing it. They write plain paths
(PS's polygon/custom origination descriptors: unprobed). The Line tool has
start/end arrowhead glyph toggles (head width 5x, length 10x the weight, PS's
proportions) encoded through the probed keyOriginLine arrow keys. The
CustomShapeLibrary (JSON sidecars in a `shapes` folder beside the settings
file, unit-box paths, v1 text codec) ships 17 builtins: ids shape.builtin.*
are append-only, geometry is code-authoritative (restore_default_shapes
rewrites drifted builtin sidecars, keeping user renames). Edit > Define
Custom Shape from Path adds a user entry.

## Path editing (Path Select / Direct Select)

Path Select (A, black arrow) moves whole shape groups only
([roles](vector-commands.md#tool-roles)). Direct Select (white arrow, Shift+A)
works per anchor: click or marquee selects, drag moves
anchors or knobs (smooth pairs stay colinear; a collapsed handle on its
corner anchor is not grabbable), Shift adds, arrows
nudge (1 px, Shift 10 px, coalesced per burst), Delete removes selected
anchors (subpaths under two anchors disappear;
[emptied shape layers go](vector-commands.md#deleting-the-last-shape)), Escape deselects. With a
selection, the options-bar Combine box rewrites the selected shapes' op in
place. The Pen doubles as the point editor (insert by exact de Casteljau
split, delete, Alt+click converts). Any direct edit drops the touched groups' live-shape
annotations (PS's keyShapeInvalidated rule) and re-rasterizes; the target is
the active shape layer (a press picks another), else the work path.

## Vector mask UI

Layers with a vector mask get a third row thumbnail (grayscale coverage,
density and disabled-cross conventions). Click targets the mask path for
pen/path tools (raster painting refuses), Ctrl-click loads the coverage as a
selection, Alt-click toggles the grayscale view, Shift-click disables.
Layer > Vector Mask: Reveal All (empty path = full coverage), Hide All
(inverted empty path), Current Path (copies the work path), Delete, Disable,
Rasterize (bakes coverage, density and any raster mask multiplied in, into
the layer mask). While the vector-mask target is active, shape drags and pen
commits append subpaths to the mask path.

## Paths panel

Tabs with Channels. Rows: saved paths (filled coverage thumbnails so boolean
holes read, 1 px outline), the work path (italic, last), and a transient row
for the active layer's shape or vector-mask path. Selecting a row targets it
for pen/path tools (outranking the layer/work-path fallback); empty-space
click deselects. Double-click saves the work path: inline rename, row moves
to the END (DocumentPath::set_kind drops the stale 1025 source so the writer
allocates a saved-range id; psd_work_path_saved_as_named_round_trips).
Ctrl-click (Cmd on macOS) loads a row's path as a selection without changing
targeting; Ctrl+Enter on the CANVAS does the same for the targeted row.

Saved rows drag-reorder among themselves (frame-breaking drops revert); the
writer assigns the sorted saved-range id set by document order so reorders
round-trip with verbatim payloads (psd_saved_paths_reorder_round_trips).
Writer invariants: new paths allocate ABOVE the highest stored id, ids
outside 2000..2997 never enter the saved set, path-range entries normalize
to ascending id order after upserts.

A selected row's outline draws with every tool, except a shape layer's own
path: path and shape tools only ([vector-commands.md](vector-commands.md)).
Anchors/handles stay path-tool-only. A drawn layer-owned outline follows its
layer's Move drag or Free Transform preview
([interactive-previews.md](interactive-previews.md)). View > Show Target Path
(Ctrl+Shift+H, view.target_path, not persisted) hides the overlay without
touching targeting; a path-transform session always draws its box. View >
Extras (Ctrl+H, view.selection_edges) hides it while Show Extras Options ticks
Target path ([view-navigation.md](view-navigation.md#extras-view--extras-ctrlh);
ui_ctrl_h_hides_path_points_with_selection_edges). The context menu's
Clipping Path entry makes ONE saved path the document clipping path
(resource 2999; name underlines). Work-path draws and layer activation
auto-select/target their rows; activating a layer with its own path drops a
stale work/saved-path target (vector-commands.md). Dismissal (empty click, or
Escape with no anchor selected) sticks per layer until the layer changes, the
row is re-clicked, or a new drag commits; a path tool still draws its
edit-target fallback.

Footer commands (row commands need a selected row; the panel refreshes on
selectionChanged AND currentItemChanged;
ui_paths_panel_actions_follow_row_selection pins it):

- New Path: empty, immediately targeted.
- Fill Path: persisted dialog; FG/BG color or a PATTERN with
  Scale/Angle/Offset/Align-with-layer rows (shared PatternTileSampler; Align
  anchors at the layer's effects reference point) plus opacity; raster-only;
  palette mode snaps via snap_pixel_to_palette.
- Stroke Path: replays the flattened path through the BRUSH ENGINE as
  synthetic input (one "Stroke path" undo); Simulate Pressure sends tablet
  events with a sine taper; open subpaths get no implied chord.
- Make Selection: feather (triple box blur), anti-alias, combine ops.
- Make Work Path from Selection: tolerance (persisted); traces the
  hard selection, fits via core/path_fit (Douglas-Peucker corners +
  Schneider cubics); outer loops Add, holes Subtract.
- Delete Path; Duplicate Path in the row context menu ("<name> copy").

## Geometry operations

Document-geometry ops transform the vector data with the pixels and
re-rasterize at the new canvas: Image Size scales anchors and stroke width,
Canvas Size/crop translate (canvas-relative PSD records need this),
90-degree rotates map edge coordinates, per-layer flips mirror about the
pixel-bounds center. Free Transform applies its affine delta to the path
model and re-rasterizes (no resampling); Move translates the model. Stroke
width never scales and the box hugs the ink, so the path maps onto the box
inset by the stroke overhang (`shape_free_transform_delta`); the drag
preview still stretches the stroke. Live-shape annotations survive positive
axis-aligned scale + translate and drop otherwise (keyShapeInvalidated
rule). Saved and work paths ride document ops. Warp refuses on shape layers
and vector masks (convert to a smart object or rasterize first).

## Appearance editing and fill layers

The Shape Appearance dialog opens from the vector badge, the layer and
canvas context menus ([tools.md](tools.md)), the options-bar Appearance... button, Layer >
Shape > Shape Appearance... (`layer.shape_appearance`), the Properties
panel's Edit Appearance..., and a Path Select / Direct Select double-click on
the shape's geometry. Reset restores the factory appearance (geometry stays). Controls: paint kind, width, alignment, caps, joins, dash presets
(Custom keeps PSD dash arrays). `pattern_linked` anchors at the effects
reference point when on and document origin when off; offsets add either way
(PatternTileSampler).

Geometry requires one modeled origination covering every subpath: rect
bounds and radii, ellipse bounds, or line endpoints/weight. A radius makes a
rect rounded. Chain buttons sit between labels and fields: `shapeGeometryLinkButton` keeps W/H in the ratio captured when
switched on; `shapeGeometryRadiusLinkButton` edits all four radii together,
on by default only when the corners agree. Dialogs are the reviewed route; no on-canvas gizmos. The dialog also edits layer Opacity and Fill opacity,
the stroke's own opacity (vstk strokeStyleOpacity), and Feather / Density
(Edge group); all PSD-native, one undo entry.

Edits preview live and restore on cancel or exception; a PSD-read
gradient/pattern stroke stays untouched unless re-picked. The preview
rasterizes on a worker: the vector MODEL applies synchronously, baked pixels
lag, requests coalesce, the pattern anchor rides a scratch layer; accept
commits the in-flight result (60 s timeout).

Layer > New Fill Layer creates Solid Color, Gradient (FG-to-BG linear), and
Pattern fill layers as shape layers with an empty path (whole canvas); a
TARGETED Paths-panel row becomes the new layer's shape path (PS's "current
path" rule, build_fill_layer), and selections become raster masks. Library
patterns adopt into the document store on use. New Gradient/Pattern Fill
stages the layer; one history entry, only on OK. Cancel restores the
original document, active layer and pattern store included. Changing a path
transform's layer, selection, path, or edit target cancels it. Sub-lattice
dash lengths clamp to the raster lattice; excessive boundary counts fall
back to a solid stroke.

On 16/32-bit documents shape and fill layers keep regenerating their 8-bit
bake, which the deep compositor reads, except gradient-only fills
(`deep_gradient_fill_raster`: no stroke, feather, density or parts) that
render at the document depth; see [high-bit-depth.md](high-bit-depth.md).

## Photoshop file encodings

The observed PSD encodings (shape and fill layer structure, `vmsk`/`vsms`, `SoCo`/`GdFl`/`PtFl`, `vstk`, `vogk`, stroke rasterization, vector masks, document path resources) are in [vector-psd-encodings.md](vector-psd-encodings.md).

## Patents and trademarks (assessed July 2026)

Claim-level record: docs/patent-research.md. docs/legal-constraints.md binds
this section.

Published earlier work, now expired: classic pen-tool
bezier editing (Illustrator 88 era); shape layers with editable fill, vector
clipping masks, combine ops, and vector masks (PS 6/7, patents expired
~2021-2024); boolean path combines, even-odd/nonzero fills, stroke dashing,
caps/joins (decades-old techniques); selection-to-path conversion via
boundary tracing, Douglas-Peucker (1973), and Schneider cubic fitting (1990;
shipped in Photoshop 3, 1994).

Excluded pending their own review (do NOT build without a new patent check):

- Curvature Pen tool (2018+) and any auto-fitting curve-through-points UX.
- Edge-magnetic vector snapping. (Image tracing itself is reviewed with
  boundaries: docs/image-trace.md and the "Vector tracing" bullet in
  docs/legal-constraints.md.)
- Snap-to-pixel "align edges" automatic pixel-grid fitting of vector renders
  (plain user-invoked grid/guide snapping of anchors is fine).
- On-canvas live-shape gizmo widgets (in-canvas radius handles etc.);
  options-bar/dialog parameter editing is the reviewed route (Apple
  US 8971623; docs/patent-research.md).
- Variable-width strokes / art brushes on paths (out of scope).

Method rules (as for all PSD work): ground truth is licensed Photoshop's
observed output via COM byte-diffing; no Adobe specification text in the
repo; self-authored fixtures only; referential "compatible with Adobe
Photoshop" phrasing; original tool icons with non-Photoshop geometry.
