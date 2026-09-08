# Merge Layers

`layer.merge_down` (Ctrl+E, Layer > Merge Down and the layer context menu) merges
selected layers, or the active layer with its lower sibling. A selected group
includes its contents. Ancestor selections include descendants only once.

Selections containing vectors use `src/ui/layer_merge.{hpp,cpp}`. Shapes that
produce one vector output merge immediately; groups and mixed selections open
**Merge Layers** with three independent options:

- **Keep vectors and bitmaps separate** preserves editable vector objects and
  merges bitmap runs separately (default on). Turn off to rasterize the merge.
- **Merge within each group separately** keeps folders and merges each folder's
  compatible children (default off). An ordinary all-vector folder in one paint
  category gets one vector child.
  Leave off to release selected ordinary Pass Through folders and merge across
  their boundaries. Groups with opacity, masks, effects or isolation retain
  their boundaries.
- **Separate merges for different vector types** separates solid, gradient,
  pattern and mixed-paint categories (default on). Colors, stroke widths,
  caps, joins and dashes do not split a category. Turn off to merge categories
  together while retaining every appearance.

All three options work together. The readout reports output leaf counts and
removed layers. Merge is disabled only when the choices cannot change anything.
Cancel leaves revisions, saved bytes, dirty state and history unchanged.
Bitmap-only Merge Down keeps its existing flattening behavior without a dialog.

## Vector representation and editing

`core/vector_compound.{hpp,cpp}` owns merged vector objects. They remain real
Pixel-kind shape layers, with immutable `VectorShapeContent` and derived pixel
caches. An ordinary shape has no `parts`; a merged shape has ordered
`VectorShapePart` paints. Each part references group ids in the shared path and
retains its fill, stroke, opacity, fill opacity, whole-canvas/inverted/disabled
flags and pattern reference point. Holes and intersections execute independently
inside each part. No tracing or bitmap-to-vector conversion occurs.

Point editing continues to target the shared path. Deleting a part's last path
removes that paint, except for an intentional whole-canvas fill. Move and document
transforms update part anchors and stroke dimensions. Whole-layer appearance
edits change only edited fields across parts; unrelated colors and strokes stay
intact. Explicit Combine Shapes Boolean commands continue to use the bottom
shape's appearance. New Add shapes append a paint; other shape-area operations
apply the new path to each existing paint.

The deterministic native rasterizer bakes each part then composites its pixels
in paint order. Dynamic Vector Preview expands the parts into temporary native
vector nodes, using the existing bounded tiles, culling and memory budget.
Their tile composite becomes a temporary pixel layer before outer opacity,
Fill, masks, clipping and effects apply.
Unchanged canvas repaints do not rebuild this representation. Object-level
styles apply to the combined object; source layers with styles do not merge.

## Preservation boundaries

Adjacent selected vectors merge without changing paint order. Matching paint
categories can cross other selected vector runs only when conservative geometry
bounds, including stroke extents, do not overlap. Unselected layers, bitmaps and
retained groups remain ordering barriers. Layer-aligned gradients and differently
anchored patterns retain their own paint coordinates.

Hidden and locked layers remain intact. Clip bases and clipped siblings stay
separate, even when only part of a clipping stack is selected. Separate masks,
effects, advanced blending and unsupported imported vectors retain their source
layers. Text and Smart Objects stay editable. A merged vector object with a
subsequently changed opacity remains an isolation boundary when merging again.
Normal bitmap runs can merge; backdrop-dependent bitmap blending stays separate.
The explicit raster option uses the CPU compositor and honors image/group locks;
position-only Background locking permits merging as in ordinary Merge Down.

## PSD, SVG and PDF

A native PSD shape has one fill/stroke, so compound vector layers expand on save
into a normal group containing native shape records, marked by private `pvcl`
bytes `PVCL` followed by big-endian u32 version 1. No private geometry codec is
needed. Other PSD readers see editable vector children. Patchy recognizes the
marker after native vectors and patterns load and folds the compatible group
back into one vector layer. If a foreign editor adds unrepresentable child
properties, Patchy retains that ordinary group instead of dropping data.
A changed Fill opacity uses an inner Normal group marked `pvfi` with the same
version payload, because native folders ignore Fill. On reopen its opacity
restores the merged layer's Fill. Object-level styles use the native group style
semantics in other PSD readers. Ordinary PSD output is unchanged, including the
byte-stability canary.

Native live-shape annotations follow remapped group ids. Unmodeled Custom live
annotations cannot follow reassigned indices and are dropped during merging;
the editable curves remain. The existing writer omits incomplete live annotation
sets. Unparsed vector blocks remain on untouched layers. Pattern resources stay
in the document and follow the existing PSD embedding rules.

SVG and editable PDF exports expand compound parts into native vector groups
before their existing format-specific support checks. Flat exports use the
normal pixel composite. Serialization and exports never mutate the source.

## Transaction, feedback and automation

Planning reads const data and never bakes pixels. The non-modal dialog reads a
copy-on-write snapshot under the edit lock. Output preparation completes before
one Undo snapshot; any failed operation leaves the original document intact.
Native vector and bitmap merging run expensive rendering on background workers.
The existing delayed **Merging layers...** processing overlay animates during
slow work, and disappears when it completes. Session identity is checked before
applying the prepared result. Undo and Redo restore the complete tree.

`doc.mergeLayers(layers, options?)` exposes the same planner without a dialog.
Boolean options `keepVectors`, `withinGroups`, `separateVectorTypes` default to
true, false, true. It returns surviving selected leaf wrappers bottom-to-top.
A single leaf does not imply its lower sibling. Invalid or foreign wrappers and
invalid options throw before mutation. A no-op adds no history. Scripts use the
host's existing progress and cancellation lifecycle. `getShape().parts` exposes
read-only part appearances and group references. API version remains 1.

Tests in `tests/ui/layer_merge_tests.cpp` cover the three-option combination,
colors/strokes, holes/inversion, paint placement, ordering barriers, processing,
transforms, PSD reopening, dialog cancellation, history and scripting. The
optional ignored Little-Everywhere fixture verifies one vector child per folder,
normal and zoomed images, output counts, and large Shift-selection timing.
