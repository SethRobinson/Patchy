# Open vector strokes in PSD

Photoshop adds closing chords when a stroked shape has multiple subpaths,
even though its path records and resaved PSD retain the open selectors.
One open subpath renders correctly. The same closure occurs when Photoshop
authors the multi-subpath shape itself. Changing combine operations, subpath
group indices, or the native `lnsr`/`lyvr` tags does not change it.

This applies to ordinary vector strokes, including path-drawn lettering and
clock hands. Closing the geometry in Patchy's renderer would change the artwork.
The PSD writer must use a representation Photoshop renders with open ends.

## Native representation

For solid, centered strokes on a shape with multiple subpaths and at least one
open contour, `prepare_compound_vector_psd` expands a temporary document through
`expand_open_path_strokes` in `core/vector_compound.cpp`. Compound vector parts
expand first, so the same rule applies inside merged artwork. Saving does not
change the source document, geometry, selection, layer ids, or history.

The native group carries the original layer's properties. Shapes with enabled
fill, an inverted path, or live-shape data retain a first child with the full
path and stroke disabled. Plain stroke-only shapes reconstruct their full path
from the stroke children, including original operations and group indices;
each child retains the inactive fill paint. This avoids a redundant carrier.
The stroke children are ordinary shape layers, one subpath per stroke.
Nondefault stroke opacity or blend mode adds an inner group to apply those
properties once; its child strokes use full opacity and Normal mode. Opaque
Normal strokes need no extra folder. The outer Fill-opacity boundary follows the
existing compound-vector convention. All content remains editable in Photoshop.

Photoshop 27.8 accepts 8000 native layer records but rejects 8001 with a
composite-only fallback dialog, even under `DialogModes.NO`. Each folder adds
two records. The writer checks this limit after expansion and refuses an
oversized save before writing the destination. Dense line art depends on
omitting redundant stroke folders; the reader accepts both group layouts.

This conversion currently covers solid centered strokes. Gradient/pattern
stroke placement and inside/outside stroke clipping require their own calibrated
representation; do not apply this split to them by dropping their paint or
alignment. Photoshop/Patchy antialiasing can differ at stroke edges.

## Round trip and foreign edits

The group association uses role 3 in the existing image resource 4211 (`PtcV`,
version 1). No new resource id or private per-layer tag is written. The runtime
marker `patchy.psd.openPathStrokes` is only layer metadata; the resource maps the
native group `lyid` to the role. Older readers that do not know the role retain
ordinary editable native groups.

Photoshop removes resource 4211 when it resaves. Such a copy retains the native
editable groups and correct open strokes; Patchy does not infer associations
from names or geometry when the resource is absent.

On read, `collapse_compound_vector_groups` restores an ordinary shape, with its
original full path and common stroke, before enclosing compound groups fold.
Restoration requires matching contour order, anchors, closure, common stroke,
and representable child properties. Photoshop may renumber child group indices;
those indices are taken from the full path instead. Foreign edits that cannot
be reconstructed safely leave the native group intact. Stroke-group opacity is
stored with native layer opacity's 8-bit precision.

## Verification

`test-fixtures/psd/patchy-open-path-strokes.psd` is a small legacy Patchy-authored
regression fixture containing single solid/dashed open paths, a closed control,
and a pair of open paths on one layer. `psd_open_path_strokes_*` checks native
single-contour stroke children, PSD/PSB restoration, source preservation,
deterministic output, opacity, and safe handling of foreign edits.

Photoshop acceptance checks compare native exports at the interior of the
unwanted closing chord, not only whole-image average error. A sparse extra line
can pass a loose whole-image tolerance. File-selection dialogs may need to be
disabled for render diagnosis; that run does not establish warning-free opening.
The universal PSD compatibility contract remains in [ps-compat.md](ps-compat.md).
