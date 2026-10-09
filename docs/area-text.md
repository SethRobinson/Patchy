# Text inside shapes

Area text owns a copy of one closed Bézier contour. The Type tool gives existing
text priority, then the targeted path, then an eligible visible shape under the
pointer. Editing the source shape does not change the text boundary. Edge-following
type is a separate feature.

## Native geometry

Photoshop COM captures use self-authored documents and Arial, with independent
flattened renders saved after the layered PSD. The text descriptor's
`textShape[0].path` accepts rectangles, triangles, ellipses, concave outlines and
self-intersections. Supplying multiple path components or multiple subpaths in a
component retains only the first contour: a second inner rectangle does not create
a hole, and a second disconnected rectangle does not receive overflow. Combine
operations on discarded components do not change this behavior. The retained
contour is independent of the original path or shape.

The committed `photoshop-area-{triangle,concave,ellipse}.psd` files were generated
through Photoshop's Action Manager. They contain black Arial 24 px text inside a
boundary with document origin (80,60) and extent 500 by 400 px. Text is eight
repetitions of `One two three four five six seven eight nine ten.` separated by
spaces. The concave contour is
`(0,0),(500,0),(500,400),(320,400),(320,150),(180,150),(180,400),(0,400)`.
Its first five lines occupy the whole width; subsequent lines fill the left span
then the right span on each baseline. The triangle has vertices
`(250,0),(500,400),(0,400)` and line lengths
`4,10,14,17,19,26,24,31,33,36,40,46`, including trailing spaces. Hidden text remains
in the story. Native triangle captures at 8, 16 and 32 bits have identical geometry.

The first Arial 24 px triangle baseline is 74.78372 px below the frame origin;
following baselines advance 28.8 px. Rectangle and ellipse first baselines are
17.18372 px. Probes at 12, 24, 48 and 96 px establish the cap-height first baseline
and the band covering approximately 90% of cap height above the baseline.
Descenders may overhang. Typography otherwise uses existing text capabilities.

The mixed-size triangle changes characters 12 through 39 to 48 px. Its native
line lengths are `4,10,10,10,24,37,39,44`, with baselines
`74.78372,132.38373,189.98373,247.58374,305.18375,333.98373,362.78372,391.58371`.
A larger run in the next word can move the current line down to a wider span.
The layout checks that opportunity before fixing the line break.

`scripts/dev/capture-area-text.ps1` reproduces the 25-case native matrix, including
compound inputs, winding, mixed sizes, paragraph alignment, boundary edits,
transforms, vertical type and depth. `scripts/dev/area-text-reference.py <capture-dir>
<output.json>` extracts the native cubic coordinates and cached line spans. The
measured directory is committed as `test-fixtures/psd/area-text-reference.json`;
the six principal PSD fixtures have independent BMP references beside them.
Photoshop's last line count can include its terminal paragraph character, which
`QTextLine::textLength()` excludes.

These captures exercise Photoshop's Action Manager text-path descriptor. They
establish which contours that descriptor retains; they do not establish an
additional boolean conversion performed by every possible Photoshop UI gesture.
Area flow uses the existing Qt shaping and paragraph capabilities. Photoshop's
emergency word splitting and hyphenation in very narrow terminal spans can differ,
including the final vertical triangle columns and self-intersection fragments.
Font substitution also retains the substitute face's own cap height and advances.

## Model and layout

`patchy.text.area` stores `serialize_vector_path` in text-local coordinates, under
the existing text affine. `patchy.text.flow` remains `box` for frame machinery.
This metadata participates in document snapshots, duplication and recovery. Geometry
is independent of pixel depth; the normal text rendering boundary supplies its cache.

`text_area_layout` flattens cubic segments and calculates spans containing a whole
line band. Removing each boundary segment's swept horizontal interval from the
initial inside spans covers concavities and crossings between sample rows.
`QTextLayout` shapes each line with existing character formats. Rendering and
`TextLineGeometry` consume these same lines. Vertical flow reuses the vertical
grapheme model with rotated flow geometry. All iteration is bounded.
Sparse stories in frames larger than 16 million square pixels allocate their
visible ink bounds rather than the full contour rectangle.

## PSD and PSB

The boundary is in `Txt2`, frame set `/0/8/0[]`, addressed through the object's
`/1/0/0/0` frame index. Area frames use `/0/2/6 [-3 -3]` and matching `-3` values
in `/0/2/11`. Box and point frames use `-2` and `-1`. Area coordinates are
`/0/1/0`: eight numbers per cubic segment, ordered start anchor, outgoing control,
next incoming control, next anchor. Segments form one closed contour. An optional
six-number `/0/2/2` affine maps these into text-local space; the layer's `TySh`
transform maps them into document space. `TySh` carries box type, orientation,
transform, text and formatting; the document frame supplies its shape.
On platforms without DirectWrite, newly authored area frames resolve each face's
PostScript name from its OpenType name table for both native font lists. A display
family such as `Arial` is not interchangeable with Photoshop's `ArialMT` face name.

Untouched imports keep their frame, text object and saved raster. Edited objects
author their native frame from the renderer's local boundary. Unrecognized native
geometry receives `patchy.text.geometry_protected` and cannot enter a destructive
text edit. Its original native data remains preserved.
The older named-key engine uses `DocumentResources/TextFrameSet/Resources`,
`Resource/Data/TextOnPathTRange`, and `DocumentObjects/TextObjects/View/Frames`.
Its known point (`[-1 -1]`) and box (`[-2 -2]`) frames keep their established edit
behavior. Other named-key geometry is preserved and protected.
Adding an area frame to a document whose preserved engine cannot be authored
(including named-key engines and untouched Photoshop 5.x type records) refuses
PSD/PSB save with an explicit error. It never silently writes a rectangle or drops
the older text data. Copy the area layer into a new document to save it as native
area text. Supporting mixed legacy engines is separate codec work.

## Scripting

`addTextLayer(text, {area: path})` copies a `PatchyVectorPath` in document coordinates.
It accepts one closed contour; `area` and `box` are mutually exclusive. The boundary
determines position, so `x` and `y` do not move it. `layer.textArea` reads a detached
document-coordinate snapshot and writes through the ordinary text session. Assigning
`null` converts an area frame to a rectangular paragraph frame. Validate before
mutation; edits preserve overflow text, formatting and undo.
