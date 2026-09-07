# Native vector scripting

JavaScript exposes the existing native shape, path, paint, and vector-mask models.
MCP uses the same bindings through `execute_script`; API version remains 1.
The public schema is `scripts/bundled/patchy.d.ts`. The packaged scripting guide
and served examples teach inspection, small edits, preview, revision, and save.
The installed control skill stays a stable entry point to the connected package.

## Binding boundaries

`script_vector.cpp` validates values and converts detached path snapshots.
`script_vector_paints.cpp` handles paint definitions and resource adoption.
`script_vector_shapes.cpp` owns shapes and layer organization;
`script_vector_paths.cpp` owns document-path wrappers and selection bindings;
`script_vector_masks.cpp` owns masks and raster path painting.
`script_vector_host.cpp` is the MainWindow-facing module. Wrappers use host
services for libraries, selection, targets, and deferred refresh. `ScriptPathObject`
resolves an owning session and decimal-string ID on each access.

Reuse native live generators, affine geometry, boolean groups, resources, and
rasterizers. `vector_operations.cpp` contains shared UI/script selection coverage,
tracing/fitting, brush polyline sampling, and mask baking. These functions own no
history. Polygon/Star UI and scripts share `generate_polygon_subpath`; drag angles
stay in radians. Raster path fills use the native writer through `paint_pixel_block`.

## Data and edits

`PatchyVectorPath` is `{subpaths:[{anchors,closed,operation,group}]}`. Coordinates
and incoming/outgoing controls are absolute document pixels, finite within
-100000..100000. Missing handles coincide with the anchor; `smooth` defaults false.
Each subpath has at least two anchors. Limits are 4096 subpaths and 100000 anchors.
Defaults are closed, unite, and the subpath index as group. Group numbers are
nonnegative integers. Members share one operation; native even-odd fill combines
their subpaths before sequential group booleans. Refresh snapshot anchor indices
and group references after geometry replacement or Undo. Reads never bump revisions.

`addShape(name,geometry,appearance)` authors rectangle, roundedRectangle, ellipse,
line with arrowheads, polygon/star, custom resource, or explicit path geometry.
Defaults are black fill and no outline, independent of the toolbar. Supplying a
stroke enables it unless `enabled:false` is explicit; alignment defaults inside.
`addFillLayer` explicitly authors the empty-path full-canvas fill model. Empty
shape geometry is rejected; empty saved/work paths are allowed.

`getShape()` returns path, live groups, appearance, and editability. Unparsed
imported markers still identify a shape but may lack parsed geometry.
`updateShape` changes only supplied fields. `geometry` and `path` are exclusive;
`group` with geometry replaces that group, retaining its operation and position.
Whole geometry replacement replaces live annotations. Direct path edits drop
annotations only for changed groups. Appearance edits preserve geometry and
unrelated imported descriptor fields. Native import locks are enforced.

Affine matrices are `[a,b,c,d,tx,ty]`: `x'=a*x+c*y+tx`, `y'=b*x+d*y+ty`.
Singular or out-of-range transforms fail. `transformShape` accepts a shape or
group containing only shapes/nested groups, prevalidating all descendants.
It transforms native shape/vector-mask geometry, including group vector masks;
raster masks remain in their own pixel coordinates. Native affine rules preserve
supported live parameters. Stroke width stays fixed unless a positive explicit
`strokeScale` is supplied. Native paint placement rules remain in effect.
`transformVectorMask` changes only that mask's geometry.

`addGroup` creates a pass-through group. `groupLayers` accepts unlocked siblings,
orders them by storage, and inserts the group at the bottom selected position.
`moveLayers` accepts multiple parents; `{parentId:null,index}` targets the root.
Omitted index appends at the top. Indices count bottom to top after removal of
moving layers. Reject stale/cross-document wrappers, duplicate/ancestor pairs,
locked parents, and cycles.

## Paints and resources

Fill and stroke paint types are none, solid, gradient, and pattern. RGB strings
abbreviate solid; `"none"` means none. Alpha belongs to layer/stroke opacity or
gradient alpha stops. Strokes expose enabled/fillEnabled, width, alignment, cap,
join, miter limit, dashes/offset in width multiples, opacity, blend mode,
scaleLock, and adjust. Partial updates preserve omitted fields.

Gradient snapshots expose solid/noise definitions and placement: stop positions,
midpoints and opacity (0..1), smoothness (0..4096), five native fill geometries,
interpolation, reverse/dither, angle, scale (1 = 100%), alignment, percentage
offsets, and noise seed/roughness/color-space/ranges. Preset application replaces
the definition while retaining omitted placement. A preset ID cannot accompany
explicit definition fields in the same update. Dynamic foreground/background
inputs default black/white and can be supplied explicitly.

`listVectorResources()` lists custom shape and gradient preset IDs, plus patterns.
Pattern references distinguish `source:"library"` with a storage ID from
`source:"document"` with an actual resource ID. Applying a library pattern adopts
its actual tile. An unequal same-ID tile receives a fresh ID; repeated adoption
reuses the matching remapped resource. Inspection returns the adopted document
reference. Adoption is staged until validation succeeds. Preset management is
outside this API.

## Paths, masks, selections, and pixels

`doc.paths` returns saved/work wrappers in storage order. `workPath` and
`clippingPath` are wrappers or null; clipping assignment accepts only a saved path
in that document. `setWorkPath` preserves an existing work path's ID. Wrappers
get/set geometry, transform, duplicate, remove, reorder among saved paths,
activate, and save. Saving a work path converts it to named storage at the end,
retaining its ID; saving a named path renames it. Reordering leaves the work
path's position intact. There is at most one clipping path.

Masks expose path, enabled/inverted/linked, density 0..100 and feather 0..1000 pixels.
`setVectorMask` creates or partially updates; removal is explicit. Shape layers
already occupy their native PSD vector-path slot; mask a containing group instead.
Adding a separate mask to a shape is rejected before mutation. Group masks persist
through the standard folder record, including the derived density/feather plane.
Raster and vector mask parameters can coexist in the mask-data section.
Adjustment-layer masks use the same native blocks and parameter-plane encoding.
The native Vector Mask menu uses the same target restriction. Existing imported
shape masks can still be inspected, removed, or rasterized before PSD export.
An empty mask reveals all unless inverted. `rasterizeVectorMask` shares native
baking. A disabled vector mask is removed without changing an existing raster
mask. A disabled raster mask contributes no coverage when baking enabled masks.

`selection.fromPath` uses native coverage, antialias/feather, and replace/add/
subtract/intersect operations. Quick Mask is rejected. `selection.toPath` uses
the current outline tracer and tolerance fitter, returning detached geometry;
storing as work/saved data is explicit. Raster `fillPath` and `strokePath` require
an explicitly targeted editable 8-bit pixel layer. Fill accepts the same paints
plus opacity 0..1, selection clipping, and native palette snapping. Stroke uses
native brush/eraser batch options plus optional pressure. Fills use the implied
closing chord for open paths; strokes traverse only actual segments.

## Transactions, discovery, and verification

Validate types, ranges, references, locks, resources, and destinations before
`prepare_mutation`. Stage multi-layer changes and shape/mask caches before commit.
The host supplies one Undo entry per document per script. Later script failures
leave earlier edits undoable. Deferred refresh includes layer/Paths panels,
thumbnails, active overlays, shape controls, and old/new dirty bounds. Geometry
edits clear stale anchor selections; removing active paths/masks repairs targets.
Cancellation follows the existing host and MCP lifecycle.

MCP capabilities include vectorShapes/vectorPaths/vectorMasks/vectorPaints. State
includes shape/editability flags, compact mask summaries, saved/work path IDs and
revisions, clipping state, and the current vector target. Fetch full geometry by
targeted script. Revisions and targets participate in attached fingerprints;
expectedState, busy, Stop, activity status, preview, and disconnect stay shared.

`ui_script_vector` covers native live geometry, detached snapshots, partial edits,
group annotations/holes, transforms/order, invalid input/locks/read revisions,
masks/conversions, brush closing behavior, paints/resource collision adoption,
PSD/SVG Unicode output, and Undo/Redo. `ui_mcp_vector` covers targets, stale tokens,
and fresh previews. Also run existing scripting/vector/MCP/theme/hotkey filters
and the full core suite because shared core tools are touched.
