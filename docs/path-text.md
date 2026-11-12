# Text on a path

A type layer can run along a line instead of standing in a box: the Type tool clicked on an
open path, or on the edge of a shape, sets the text along that contour (GitHub issue 717). The
layer owns a copy of one open or closed Bézier contour and a start and end point on it; the
story's first line is set between the two. Editing the source path or shape afterwards does not
change the copy. Text that fills a closed shape is [area text](area-text.md); the session,
commit and caret rules are in [text-tool.md](text-tool.md) and
[text-tool-layout.md](text-tool-layout.md).

## What Photoshop does (COM captures)

`scripts/dev/capture-path-text.ps1` makes the reference files in Photoshop 2026 through the
Action Manager: a point text layer whose `textShape[0]` gets `char = onACurve`, a `path` and a
`tRange` (`Strt`, `End `). Documents are 660 by 520 px at 72 ppi, the text origin is (80, 60),
type is black Arial, and each case saves the layered PSD and a flattened render made after it.
The committed ones are `test-fixtures/psd/photoshop-path-*.psd` with a PNG of the flatten beside
each; the script writes the whole matrix (36 cases) into `local-test-fixtures/psd/path-text/`.

Contours of the matrix, in text-local coordinates (anchor, incoming handle, outgoing handle):

- arc: `(0,300)` out `(120,0)`; `(500,300)` in `(380,0)`. Arc length 713.548 px.
- wave: `(0,250)` out `(40,120)`; `(150,150)` in `(110,150)` out `(260,150)`; `(500,250)` in
  `(400,380)`. Length 605.915 px.
- ellipse (closed): the 500 by 400 ellipse of the area-text matrix. Length 1418.283 px.

Findings, each from a capture:

- **The range is in segment units.** The whole part of a range value names a cubic segment of
  the contour and the fraction is the Bézier parameter inside it, not a share of the arc length:
  on the wave, `0.5` is 128.98 px along and `1.5` is 411.50 px. An open contour of N segments
  takes 0 to N. On a closed one the end may pass the seam (start 3.5, end 4.999 on the ellipse
  ran past the first anchor); the Action Manager rewrites an end below the start to just under
  N + 1 and swaps a reversed range on an open contour.
- **Glyphs advance by arc length.** Rows of capital I at 48 px (advance 13.336 px): the bars'
  centroids sit 13.3 px of arc apart on the arc, the wave and the ellipse alike, the first one
  half an advance past the start point. Curvature does not change the spacing.
- **Each glyph lies along the tangent at the middle of its advance**, with its baseline on the
  contour (the bars' long axes are within 0.3 degrees of the normal there, and their centroids
  half a cap height from the line). A baseline shift moves a glyph along the normal; mixed sizes
  share the baseline.
- **Alignment works inside the range.** Centered text is centered between the start and end
  points; right-aligned text ends at the end point. Trailing spaces do not count.
- **Text that does not fit is hidden by whole words**, as if the range were a one-line box as
  wide as its arc length: five groups of `IIIII` on a range that holds four and a half show
  four. A single word longer than the range is cut by glyph. A second paragraph is hidden. The
  hidden text stays in the story.
- **A layer transform applies to the contour.** The rotated capture keeps the same frame and
  carries the rotation in the TySh transform.
- **The `flip` flag of the Action Manager descriptor does nothing**: a file saved with it set
  renders and stores exactly what the unflipped one does (so do `pathTypeSpacing` and the
  other `pathType*` keys). Photoshop's own flip, made by dragging across the path in its window,
  was not captured: that needs clicks in Photoshop.
- **Vertical type on a path** stacks upright glyphs along the line and adds a frame key
  (below). Patchy does not set vertical type on paths.

## Records in a PSD

The contour lives only in the document's `Txt2` block ([txt2.md](txt2.md)), in the frame the
text object names. A path frame, key for key as Photoshop 2026 writes it:

```
<< /0 << /1 << /0 [ 0.0 300.0 120.0 0.0 380.0 0.0 500.0 300.0 ] >> /2 << /0 2 /6 [ .3 .9 ] >> >> >>
```

- `/0/1/0`: the contour, eight numbers per cubic segment (start anchor, outgoing handle, next
  incoming handle, next anchor), in text-local coordinates. Consecutive segments share their end
  points. There is no closed flag: a contour is closed when its last segment ends where its
  first starts.
- `/0/2/0 2`: the frame type. A point frame has no `/0` here and a box frame has `1`.
- `/0/2/6`: the range, start then end (the key that holds `[-1 -1]`, `[-2 -2]` and `[-3 -3]` in
  point, box and area frames; the named-key engine calls it `TextOnPathTRange`).
- `/0/2/2`: an optional six-number affine from the stored coordinates to text-local space, as
  in area frames. Photoshop writes it when the descriptor's shape transform is not identity.
- `/0/2/1 2`: vertical type. A path frame has no `/0/2/11` and no baseline rule.

The layer's `TySh` records a box (`/ShapeType 1`) whose `/BoxBounds` are the contour's bounds
in text-local coordinates, and its transform maps text-local space into the document.

**Reading.** `TextEngineBlock::object_geometry` returns `Kind::Path` with the contour and the
normalized range for a frame whose `/0/2` holds only the keys `0`, `2` and `6`, whose range is
finite with the end past the start, and whose segments join. Any other path frame (vertical
type, a key not listed here, a reversed range) stays `Unsupported`: the layer keeps its saved
pixels and native data and is protected from editing, as before. An untouched imported layer
keeps Photoshop's raster until it is edited. The named-key engine of Photoshop CS-era files is
not read for path frames: those layers stay protected.

**Writing.** `author_frame` writes exactly the frame above from `TextEngineInputs::path` and
`path_range`; the TySh box gets the contour's bounds. A document whose preserved text engine
cannot be authored into (a named-key engine, an untouched Photoshop 5.x type record) refuses
PSD/PSB save with an explicit error when it holds path text, as for area text.

Acceptance (Photoshop 2026 over COM, `scripts/dev/photoshop-open-check.ps1` and the readback in
the capture script): Patchy-authored path text on an open curve, a ranged two-segment wave, a
closed ellipse and a flipped arc opened with no prompt, read back `char onACurve`, the same
contour and range, `textWarningLevel 0` and no `oldText`, and Photoshop's re-layout put the ink
where Patchy drew it.

## Model

- `patchy.text.path` (`kLayerMetadataTextPath`): `serialize_vector_path` of one subpath, open
  or closed, in text-local coordinates under the text transform.
- `patchy.text.path_range` (`kLayerMetadataTextPathRange`): `start end` in Photoshop's segment
  units, normalized by `normalized_text_path_range`: open contours hold 0 <= start < end <= N,
  closed ones 0 <= start < N and start < end <= start + N.
- `patchy.text.flow` stays `box`, and the box size is the contour's bounds, as for area text.
  A layer has an area contour or a path, never both.

`core/text_path.hpp` owns the rules: `valid_text_path`, the range helpers,
`text_path_frame_for_layer`, and `TextPathCurve`, an arc-length table of 128 chords per cubic
segment (plain doubles in a fixed order; the measured lengths above agree within 0.02 px).
`sample(distance)` gives the point and unit tangent, wrapping on a closed contour and running
straight on past the ends of an open one; `nearest(x, y)` gives the parameter, arc length, gap
and side for a point.

**Flip.** Patchy flips path text by reversing its copy of the contour
(`reversed_text_path`, `reversed_text_path_range`): the text then runs back along the same
stretch on the other side of the line. The file holds an ordinary path frame, so Photoshop
shows the flipped text without needing a flag.

## Layout and drawing

`build_text_render_document` lays a path story out at the arc length of its range
(`text_path_layout_width`) with word wrap that may break inside a word, so Qt's first line is
what Photoshop shows. `text_path_line_plan` (ui/text_path_layout.hpp) takes that line into
*straight space*: x is the arc length from the range's start point, y is 0 on the baseline and
grows downward. The plan maps straight space onto the contour (`map`, `glyph_transform`,
`strip`) and back (`unmap`).

`build_text_render_plan` puts the plan in `TextRenderPlan::path_plan`; `draw_path_text_plan`
draws each glyph through `glyph_transform` of the middle of its advance (a combining mark rides
with the glyph before it), with the same faux bold, faux italic, stretched-face and Strong
anti-alias handling as other lines. Underline and strikethrough bars are bent into strips. The
raster's `local_rect` is the union of the placed glyph boxes, so the layer's bounds follow the
curve. Nothing snaps to the pixel grid, and the transform's scale is never folded into the font
size: the text always renders through the layer's full transform, like area text.

Tolerance against the captures (`ui_text_path_glyphs_match_photoshop_placement_if_available`,
`ui_text_path_photoshop_files_redraw_where_photoshop_drew_them_if_available`): every capital I
centroid within 1 px of Photoshop's, and for the text fixtures at least 90 percent of each
render's ink within a pixel of the other's, ink bounds within 2 px.

Not modelled: vertical type on a path (switching path text to vertical makes it ordinary
vertical text in the contour's bounds and drops the path), Warp Text on path text, Photoshop's justification of a
path line beyond what Qt's own justification does, and a paragraph's indents (they shift the
line inside the range as in a box).

## The Type tool

- **Targeting** (`CanvasWidget::text_path_at`): existing text first, then the targeted path,
  then the topmost visible shape whose outline passes within six screen pixels of the pointer.
  A hit on a line wins over the inside of a closed shape, so the edge of a shape starts path
  text and its inside starts area text. The cursor over a line is an I-beam on a wavy stroke.
  The range runs from the clicked point to the end of an open contour, or once around a closed
  one.
- **Session.** Path text edits on the transformed overlay, like area text
  (`kTextEditorPathProperty`, `kTextEditorPathRangeProperty` on the editor). The caret is a bar
  across the line and selections are strips, both bent from `TextLineGeometry` answers in
  straight space; a click resolves from the nearest point of the contour. A press further from
  the line than the type can reach clicks off, even inside the contour's bounds.
- **Marks** (Patchy's own design): a round mark at the start of the range, a square one at its
  end and a diamond at its middle, with a short tick at each end on the side the text stands
  on. Dragging the start or end mark slides that end along the contour; dragging the diamond
  slides the whole range, and dragging it across the line (past half the type size) flips the
  text. During a drag only the overlay moves (the marks and the outline); the text is laid out
  once, on release, like a box resize, so there is nothing to coalesce. The end mark carries
  the overflow plus when the range hides text (issue 631). Published for tests as
  `patchy.transformedTextPathHandleCenters` (start, end, middle, canvas coordinates).
- **Re-edit.** A click on the text, or within a few pixels of the bare part of its contour,
  opens the layer. The whole session is one undo step.
- **The contour as a path.** With the path-text layer active, the Direct Selection and Path
  Selection tools edit its contour like an area boundary (`text_area_edit_target_path`): the
  text follows, the range stays where it was in segment units, and points cannot be added or
  removed there.
- Free Transform, Image Size and the other document operations compose onto the text
  transform; the contour is text-local, so it follows.

## Scripting

- `doc.addTextLayer(text, {path, pathRange})`: `path` is a `PatchyVectorPath` of one open or
  closed contour in document pixels (`closed: false` for an open one); `pathRange` is
  `{start, end}` in segment units and defaults to the whole contour. `path` excludes `area` and
  `box`, and the contour decides the position, so `x` and `y` do not move it.
- `layer.textPath`: a detached snapshot of the contour in document pixels, or `null`. Assigning
  a contour sets the text along it (the range stays when the new contour has as many segments);
  assigning `null` leaves a rectangular text box.
- `layer.textPathRange`: `{start, end}` or `null`; assignable on path text.
- `layer.flipTextPath()`: reverses the contour.

Each is one undoable step through the ordinary text session and throws before any change on a
contour that is not exactly one subpath of at least two anchors, a range outside the contour,
or a layer that is not editable text.

## Tests

`tests/core/text_path_tests.cpp` (arc lengths, range rules, reversal, Photoshop frames read and
authored, the legacy-engine refusal) and `tests/ui/text_path_tests.cpp` (the line plan,
placement against the captures, scripting, the Type tool's targeting, the marks, the caret and
clicks).
