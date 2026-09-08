# Merge Layers

`layer.merge_down` (Ctrl+E, Layer > Merge Down and the layer context menu) merges
the selection, or the active layer with its lower sibling. A selected group
means its contents. Ancestor selections include their descendants only once.

Selections containing vectors use `src/ui/layer_merge.{hpp,cpp}`. Compatible
shape layers merge immediately. Groups, mixed content, and differing vector
appearances open **Merge Layers** with three options:

- **Keep vectors and bitmaps separate** keeps editable paths and merges bitmap
  runs separately (default on). Uncheck to explicitly rasterize merged artwork.
- **Merge within each group separately** keeps folder structure and merges
  children inside each folder (default off). Leave off to release selected ordinary Pass Through
  groups and merge across their boundaries. Normal groups and groups with
  opacity, masks, effects, clipping, or advanced blending keep their boundaries.
- **Separate merges for different vector types** keeps different fills, colors,
  gradients, patterns, and strokes apart (default on). Uncheck to use the bottom shape's fill
  and stroke for a vector merge. The dialog states this appearance change.

The readout reports output leaf counts and removed layers before any mutation.
It disables Merge when the choices cannot reduce or rasterize anything. Cancel
leaves document revisions, saved bytes, dirty state and history unchanged.
Bitmap-only Merge Down retains its existing flattening behavior without a dialog.

## Preservation boundaries

A shape layer has one fill and one stroke. The planner merges adjacent selected
runs with compatible appearance. Matching vector runs can cross other selected
vector runs only when their conservative ink bounds do not overlap. Unselected
layers are barriers; overlapping artwork keeps its paint order. Matching
vector paints include their actual settings, not only Solid/Gradient/Pattern.
Canvas-aligned gradients and patterns with matching world anchors can merge.
Layer-aligned gradients and differently anchored linked patterns stay separate,
because merging would change their paint coordinates.

Normal, unmasked shapes without effects, clipping, channel restrictions or
advanced blending can combine. Opaque additive solid fills may overlap. Other
compatible shapes require disjoint conservative geometry bounds including
stroke extents, preserving their opacity, holes and fill/stroke order. Shape
groups are remapped without changing interior subtraction or exclusion. An
independent intersection, inverted path, or whole-canvas fill stays separate.
Native live-shape annotations follow remapped groups. Opaque custom annotations
and unparsed imported vector sources stay on their original layers; their raw
PSD records cannot safely follow reassigned path indices. The explicit Combine
Shapes boolean commands remain separate and retain their existing semantics.

Locked and hidden layers stay intact in the preserving planner. Clip bases and
clipped siblings remain barriers, even when only part of a clipping stack was
selected. Text and Smart Objects stay editable. Normal bitmap runs can merge;
backdrop-dependent bitmap blending, masks and effects remain independent.
The explicit raster option renders selected runs through the CPU compositor.
Position-only Background locking permits raster merging as in ordinary Merge
Down. Image-pixel locks and group locks remain barriers.

## Transaction and automation

Planning reads const data and never renders or changes caches. The non-modal
dialog reads a copy-on-write document snapshot while the edit lock blocks
document changes. Tab identity is checked again after it closes. Raster and
vector outputs are prepared before arming one Undo entry; failures preserve the
source. Changed shapes receive new document-resolution caches and regenerated
PSD vector blocks. Pattern resources stay in the document. Existing save/export
paths need no format changes. Undo and Redo restore the complete layer tree.

`doc.mergeLayers(layers, options?)` exposes the same planner to scripts. Its
boolean options are `keepVectors` (true), `withinGroups` (false), and
`separateVectorTypes` (true). It
returns surviving selected leaf wrappers in bottom-to-top order. It merges
exactly the supplied set; a single leaf does not imply its lower sibling. Missing,
foreign-document or invalid wrappers and invalid options throw before mutation.
A no-op arms no Undo entry. `app.apiVersion` remains 1.

Verification lives in `tests/ui/layer_merge_tests.cpp` and the existing
`ui_merge_down_*` tests: mixed group order and opacity, holes, strokes, gradient
and pattern coordinates, protected layers, group/type choices, dialog cancel and
history, scripts, PSD round trips, and the optional ignored Little-Everywhere
fixture in `local-test-fixtures/vector-preview`.
