# Patent research record: drag points on live shapes (GitHub #690)

Scan of November 10, 2026. Claim text was read on Google Patents; statuses are Google's
estimates, unchecked against USPTO fee or assignment records. The sections dated 2026-11-13
read their claims from the USPTO Patent Public Search text
([method](patent-research.md#reading-patent-text)). The binding rules are the
"Shape drag points" section of [legal-constraints.md](legal-constraints.md#shape-drag-points);
the design is in [vector-tools-shape-handles.md](vector-tools-shape-handles.md). This is an
engineering record of the design and of the published patents considered, in the conventions of
[patent-research.md](patent-research.md).

Question: may a shape tool show, in place of the eight-handle box, (a) a live Polygon's center
and first vertex as two drag points, the vertex setting radius and angle exactly as the
drawing drag does, and (b) a live Line's two end points? [vector-tools.md](vector-tools.md)
held every on-canvas live-shape widget back until it had its own check. This is that check,
for these two forms and no others.

## What is built

- A regular polygon or star: one disc on its center (a drag moves the shape on whole pixels;
  since 2026-11-13 it snaps as a whole-shape drag, [below](#center-point-snapping-github-695-follow-up))
  and one on its first vertex (a drag sets the radius and the angle about the unchanged
  center). Side count and star inset are never changed by a point.
- A straight line with a weight and optional arrowheads: one disc on each end. A drag moves
  that end; the other end, the weight and the arrowheads stay.
- Two discs are all that is drawn. The shape is regenerated from its parameters on every move.

## Published earlier work, older than 20 years

| Source | Date | What it shows |
|---|---|---|
| AutoCAD Release 12, grips | June 1992 (the Autodesk File's feature history lists Grips under Release 12, June 1992) | Grips on a selected entity; a 1993 review of Release 12 describes grabbing an entity by its end point, center, midpoint or quadrant to stretch, move or rotate it without a command. |
| US 5517578 (Aha Software; Google lists Microsoft as the current holder) | Priority 1993-05-20; Expired - Lifetime, 2013-05-20 | Handles "positioned at the endpoints of line type objects"; dragging one "simultaneously re-sizes and rotates" the line, "the rotation ... about the opposite end point". Family: US 5613019, 6535897, 6587587, 6651221, 7203903, same filing era. |
| EP 0635781 A1 (Xerox; Bier and Buxton) | Priority 1993-07-21, published 1995-01-25 | Positions "both endpoints of a straight line segment", "the center point and a circumference point of a circle", and two corners of a triangle "simultaneously translated, rotated, and scaled". |
| Inkscape 0.38 | Released 2004-04-12 | Its notes give the star tool "a polygon mode which generates true polygons" and put each shape's editing handles on the canvas in the tool that drew it ("draw a shape and edit it immediately, without switching to node tool"). The polygon's one handle sits on a vertex and resizes and turns it about its center (the handle's behaviour is from the current manual; the 2004 notes give the mode and the on-canvas handles). |

The line form (b) is the 1992 and 1993 behaviour unchanged: a handle on each end, the dragged
end follows the pointer, the line pivots on the other end. The polygon form (a) is the 2004
handle on a vertex plus a center point that only moves the shape; setting a shape by a center
point and a rim point is the 1993 Xerox publication.

## Patents read

| Patent | Holder, subject | Priority, estimated expiry | What every independent claim read requires |
|---|---|---|---|
| US 10249067 | Adobe, control of shape interaction | 2015-03-19, 2036-02-23 | A shape described by points that each have an associated radius; an input that modifies one point's radius; a test whether that radius meets a neighbouring point's radius, and a change to the neighbour's radius so they do not meet (claims 1 and 17). Claim 8: a point with a position and a radius is moved, its radius follows the move and nearby points are adjusted to move less. |
| US 7864179 | Adobe, fractional parametric polystar | 2007-04-13, listed Expired - Fee Related | A star-points or polygon-sides input that is a non-integer above three, and a shape rendered with the integer part plus one points or sides, the fractional one told apart by its radial length or by curved sides (claims 1 and 15 read; 16 to 30 not shown). |
| US 8971623 | Apple, overlaid tools | 2012-03-06, 2032-12-26 | Two lines flanking a chosen location, each dividing the image, and an effect applied outside them ([audit item 3](patent-research-audit.md)). |
| US 9250786 | Adobe, snapping via dragging | 2013-07-16, 2034-03-17 | As recorded in [patent-research-alignment.md](patent-research-alignment.md): the source feature of a dragged object is determined from where the drag started and is snapped to a target feature found during the drag. The claim text could not be fetched again in this scan; it was read on 2026-11-13 ([below](#center-point-snapping-github-695-follow-up)). |

How the two forms differ from each:

- **US 10249067.** Neither point has a radius and nothing adjusts a neighbouring point. The
  polygon's one radius is the distance from its center to its vertices, set by where the vertex
  is dropped, as the drawing drag sets it. The patent's subject is corner rounding by dragging,
  which this feature does not have. A corner-radius handle would be the first thing to compare
  with it.
- **US 7864179.** Sides and star points stay integers set in the options bar and the Properties
  panel; no point changes them.
- **US 8971623.** Two discs are drawn: no line, nothing that divides the image, no effect area.
- **US 9250786.** The center point is the one drag that moves a whole object from a grabbed
  feature. As built on 2026-11-10 it moved by the pointer's travel on whole pixels and snapped
  to nothing; its snapping was compared separately on 2026-11-13
  ([below](#center-point-snapping-github-695-follow-up)). The vertex and
  a line's end follow the pointer through the same snapping the second point of the drawing
  drag and the box's handles already use. Corrected 2026-11-13: this bullet used to add that
  they resize and do not reposition the object. That is not a difference from the claim
  (its dependent claim 7 has the snap cause "a non-uniform transformation of the source
  object"); what these drags rest on is in
  [Handle drags that snap](#handle-drags-that-snap).

Open-web and Google Patents keyword searches (polygon with handle, vertex, center, rotate;
line with end point handle, arrowhead; "live shape", "parametric shape", on-canvas control;
Adobe as assignee from 2008) returned no patent whose independent claims describe either form.

## Outcome (decided 2026-11-10)

Both forms are built as described above. Kept out, each needing its own check first:

- A handle that edits a corner radius (US 10249067 is where that check starts), a side count
  or a star inset (Illustrator and Keynote ship such handles; no search was made for them).
- Any line, stem, arc, band or numeric readout drawn with the points.
- Snapping of the center point to guides, the grid or other layers (checked and built on
  2026-11-13, next section).
- Points on Rectangle, Ellipse or Custom Shape. They keep the eight-handle box.

## Center point snapping (GitHub #695 follow-up)

Compared and decided 2026-11-13. Question: may a drag of the polygon's center point snap the
way the whole-shape drag of GitHub issue 695 does (Path Select, Ctrl in a shape tool;
[alignment.md](alignment.md#whole-shape-drags))?

**US 9250786, claim 1** (all 19 claims read 2026-11-13 from the USPTO text; Google Patents
shows it Active with adjusted expiration 2034-03-17, which is 20 years from the 2013-07-16
filing plus the 244-day adjustment in the USPTO record; claims 12 and 14, a storage medium
and a system, list the same steps). The steps, summarized, with the deciding phrase quoted: a first input starts a drag at a first location;
the distances from that location to a plurality of source features are evaluated; the source
feature is determined "in response to the source feature having a shortest distance among the
respective distances to the first location"; a second input gives the current location during
the drag; a current target feature is determined from it; the source feature is snapped to the
target, unsnapped when it is dragged away before the drag ends, and new target features keep
being determined. Claim 4 selects among the source features of several source objects "based
on the first location".

**What the center drag does.** It hands the polygon's bounds at the press to the scan the Move
tool and the issue 695 drag use (`snapped_rects_delta_with_matches` through
`snap_whole_shape_drag`). On each axis both edges and the center of those bounds are compared
with every target line (guides, the grid, the canvas, other layers' rects), and the pair with
the smallest gap inside the tolerance gives the correction. No distance from the press
location to any feature is evaluated, and no feature is selected at the press or because of
where the press was: the press position enters only as the pointer's travel.

**How they differ.** The claim fixes the source feature by its distance to the drag's start.
Patchy has no such step: the feature that lands is whichever of the six comes nearest a target
as the shape moves, and it can change from one move to the next. A hexagon held by its center
lands its right edge on a layer's edge
(`ui_shape_drag_snap_polygon_center_point_lands_on_layer_edge_and_clears_guide`). This is the
difference the Move tool's snapping (2026-09) and the issue 695 drag (2026-11-11) are built on.

**Recorded with the decision.** This drag is always pressed on the center disc, and the
center of the bounds is one of the six sources. When the center is the feature that lands,
the feature that snapped is also the one nearest the press. The step the claim requires, a
source feature determined from distances to the press, is still absent in that case. A change
that made the center the only source, gave it priority over the edges, or chose a source from
the pressed point would add that step: it stays out
([legal-constraints.md](legal-constraints.md#shape-drag-points)).

**The press's hit test** (added 2026-11-13). `shape_point_at`
(src/ui/canvas_widget_shape_points.cpp) does measure the press's distance to the two discs and
takes the nearer one within 8 px. That choice is between two drags, move (center) or reshape
(vertex); it selects no snap source. Whichever way the press resolves, the center drag's
sources are the six lines of the bounds, and the one that lands is chosen by its nearness to a
target. The disc is not even one of those features in general: it sits on the polygon's own
center, while the sources are the bounds' edges and the bounds' center, and on an odd-sided
polygon or an odd-pointed star the two centers differ (a triangle with a vertex up has its
bounds' center a quarter of the radius above the disc).

**The continuation, US 10061496** (Adobe, same title, filed 2015-12-18 as a continuation of
the application behind US 9250786, granted 2018-08-28; found by the title search and read
2026-11-13 from the USPTO text; 20 years from 2013-07-16 plus its 394-day adjustment gives
2034-08-14, fees and any terminal disclaimer not checked). Independent claims 1, 12 and 14:
a first input that starts a drag at a first location and indicates a source object; distances
evaluated between that location and "a plurality of source features on the source object"; the
source feature determined "based on the source feature having a shortest distance" among
them; a second input indicating a current location and a current target object; a target
feature within a predetermined distance of the current location; the source feature snapped to
it. The unsnapping and repeated-target steps of the parent are not in these claims. The
shortest-distance step is, so the comparison above holds for it unchanged.

**Class queries repeated** (the watch item of 2026-11-10; Google Patents query endpoint,
2026-11-13, the first result page of each, three of them only partly readable): CPC
G06F 3/04845 and G06T 11/20 with polygon, handle, vertex, center, drag (194 and 521 results);
the same classes with line, end point, handle, drag (4 and 175); Adobe as assignee from 2008
with snap, center handle or center point, drag, polygon, shape, guide (10); "live shape" or
"parametric shape" with on-canvas control, center, move, snap (7). No listed title describes a
polygon with a center point that moves it, or a snap whose source is a handle at an object's
center.

| Patent | Holder, subject | Priority; status as listed | Claim 1 requires (how read) |
|---|---|---|---|
| US 8400477 | Google, object resizing | 2008-05-20; Expired - Fee Related | Height, width and aspect-ratio guide lines produced by dragging a resize handle, each with a gutter that decides the resize (summary). A resize, not a move. |
| US 10460483 | Adobe, arcs | 2016-08-25; Active, 2036-08-25 | A path with an arc segment resized with its segments scaled in proportion (summary); the description puts a handle on a segment's midpoint that turns a line into an arc. No such handle here. |
| US 10650576 | Adobe, snapping under clipping masks | 2018-11-12; Active, 2039-01-10 | Bounding boxes of objects under a clipping mask cut down to the mask's visible area before they generate guides (claim text). Patchy clips no target rect to a mask. |
| US 10832442 | Adobe, smart guides from sub-objects | 2019-03-28; Active, 2039-05-06 | Distances from a subject object to candidates in opposite directions compared, and equal-distance guides shown when they match (claim text). Patchy has no equal-spacing snap or guide (the alignment rule). |
| US 11335050 | Adobe, raster boundary guides | 2020-10-06; Active, 2040-10-06 | Edge paths extracted from a raster image, filtered and shown as snapping guides (summary). Patchy takes no target from a layer's pixels (the alignment rule). |
| US 10762674 | Adobe, "Beautifying freeform drawings using arc and circle center snapping" | 2016-04-25 (a continuation of a division of US 9984481); filed 2020-01-27, 20 years from 2016-04-25 is 2036-04-25 | Claims 1, 10 and 15 (USPTO text): a freeform arc path the user draws, whose center point is identified (claims 7 and 19: a least-squares circle fit); a threshold distance or search radius set from the arc's span; a point of an existing resolved path within it; a path suggestion that moves the arc so its center meets that point. The center drag takes no freeform input, fits no circle, has no span-based threshold and makes no path suggestion. |
| US 9229636, US 10275145 | Adobe, "Drawing support tool" (the second a continuation) | 2010-10-22; filed 2011-03-02 and 2015-11-18 | Every independent claim (USPTO text): a shape shown with a buffer region beside its edge; drawing data received in that region and shown repositioned at a set distance from the edge; the shape moved with a connected child shape. A virtual straight edge. Patchy shows no buffer region and repositions no drawn data. |
| US 9268423 | Stormlit, shapes from nodes on touch screens | 2012-09-08; filed 2013-04-13 | Claims 1 and 23 (USPTO text): several concurrent touches interpreted as node positions and motions that decide which geometric shape is drawn, kept for later reselection. Patchy interprets no multi-touch nodes. |
| US 10795558, US 2020/0371676 | Apple, virtual drawing aid | 2015-06-07 | Claims 1, 11 and 16 of the patent and the application's claims as published (USPTO text): an on-screen ruler on a touch display; touches near its edge draw a line along it, a back-and-forth swipe fills beside it, pinch and rotation gestures dismiss or turn it. Patchy has no drawing-aid ruler. |

Listed by the queries and still not read: Digital Forming JP 6041958 (object design interface;
not a US document, so not in the USPTO service) and Stormlit US 2015/0338974 (the application
in the family of US 9268423).

**Outcome.** Built: the center drag snaps as a whole-shape drag, with the Move tool's
alignment guide. Unchanged: the vertex point, the line's ends, and everything listed as kept
out above. Reviewed again on 2026-11-13 with the full claim texts of US 9250786, US 10061496
and US 10762674: the center drag stays as built.

## Handle drags that snap

Recorded 2026-11-13. The vertex point, a line's two ends and the eight box handles (and the
Move tool's transform handles before them) work alike: a press inside a handle's hit zone
takes that handle, the nearer one where two zones overlap (`shape_point_at`,
`transform_handle_at`); the grabbed point then follows the pointer through
`snapped_document_point`, which on each axis takes the nearest target line inside the
tolerance, lets go when the pointer leaves it, and looks again on every move.

Set beside claim 1 of US 9250786 and of US 10061496, these drags have a counterpart for each
step: a press, a point of the shape taken by its nearness to the press, that point snapped to
a target near the pointer. The earlier sentence that set them apart (they resize, they do not
reposition) is withdrawn above. They are kept on a different basis: the grabbed-point snap is
what drawing programs published long before the 2013-07-16 priority date, including the
source-nearest-the-pointer step itself.

| Source | Date evidence | What it describes |
|---|---|---|
| Adobe US 6993709 B1, "Smart corner move snapping" | Filed 2000-09-11 (provisional 2000-02-12), granted 2006-01-31; 20 years from filing plus the 871-day adjustment in the USPTO record ends 2023-01-30 | Claim 7 (USPTO text): an element with several attraction points is held by a button press and "only a single second attraction point nearest to a cursor is in an attractive state"; the element then moves with the cursor and that point snaps it to an attraction point of the grid. The description: "only a single attraction point nearest cursor 40 is made attractive, and all of the remaining attraction points are made nonattractive". |
| Inkscape 0.47 release notes (wiki.inkscape.org, "Snapping") | Linux Magazine reported the release on 2009-11-27 ("Reworked snapping system with a Snapping toolbar") | "Inkscape can be forced to only try snapping the (source) node that is closest to the mouse pointer, à la Corel DRAW"; "a snap indicator will shortly be shown at that node"; "The Node tool now snaps to any unselected node"; the points considered for snapping include "Each of the handle points (when creating new shapes)". |
| Inkscape 0.46 release notes ("Grids, guides, snapping") | The release before 0.47; its own date was not on the pages read | "Snapping has been implemented or improved for:" with "Handles of objects, incl. gradients" in the list. |
| Using Adobe Illustrator CS4, "Snap objects to anchor points and guides" | "© 2008 Adobe Systems Incorporated" on its copyright page (read on a manual archive site, not Adobe's own) | "position the pointer on the exact point you want to align with anchor points and guides"; "the snapping alignment depends on the position of the pointer, not the edges of the dragged object"; "When the pointer comes within 2 pixels of an anchor point or guide, it snaps to the point." |
| Adobe Photoshop CS3 User Guide, "Use snapping" | 2007 by a summary of its copyright page; the line itself was not read | "Snapping helps with precise placement of selection edges, cropping marquees, slices, shapes, and paths", to guides, the grid, layers, slices and the document bounds. It names no transform handle. |

What each covers: US 6993709 and Inkscape 0.47 describe choosing, at the press, the one point
of the dragged object nearest the pointer as the point that snaps, and snapping it during the
move; the Illustrator manual describes the pressed point as what aligns, to anchor points and
guides, within a set distance. Inkscape 0.46 and 0.47 describe a dragged handle or node
snapping. The vertex point, the line's ends and the box handles do no more than these: one
grabbed point, snapped as it is dragged.

Kept as built on that basis, the kind the composite-preview entry (2026-10-20) and the Crop
entries (2026-11-11) rest on. The limit that follows: a handle drag snaps the point whose hit
zone was pressed and nothing else. Beyond that hit test no drag picks a snap source among the
features of a shape by their distance to the press; the whole-shape drags keep every bounds
line as a source (previous section).

Not verified here: the date of Inkscape 0.46; the Corel DRAW behaviour the 0.47 notes name;
a dated sentence saying Photoshop's transform handles snap to guides; AutoCAD object snap on a
dragged grip (the 1992 grips above); the Illustrator and Photoshop manuals on Adobe's own
site (its archive links returned 404, and the web archive could not be fetched). The
examiner's citation lists of US 9250786 and US 10061496 were not read, so it is not known
whether US 6993709 was before the examiner.

## Not verified

- Google Patents answered the first keyword queries and then refused automated requests
  (HTTP 503). The class-restricted queries (CPC G06T 11/20 and G06F 3/04845: polygon with
  handle, vertex and center; line with end point handle) were not completed on 2026-11-10.
  They were repeated on 2026-11-13 for the first result page of each
  ([above](#center-point-snapping-github-695-follow-up)), and every US result named there was
  then read through the USPTO service. The later result pages are still open, and the USPTO
  service did not answer class or claim-text queries in the forms tried. Read them before the
  points gain any further behaviour.
- Adobe filings from 2019 to 2021 around Photoshop 22's shape controls (October 2020: a line
  defined by two control points with two end handles, on-canvas controls for polygons) were
  searched by keyword only.
- No assignee sweep for Apple, Microsoft, Corel, Serif, Figma or Canva.
- Adobe US 9229636 ("Drawing support tool", priority 2010-10-22, estimated expiry 2033-10-03)
  turned up for "drag points": its description is about a virtual straight edge positioned by
  drag points, not handles on a drawn shape. Its claims were read on 2026-11-13 (table above).
- MacDraw (1984) and Visio 1.0 (1992) are widely described as showing a handle on each end of a
  selected line; no dated source was found, so they are not in the table. The date of each
  Inkscape handle modifier; USPTO fee and assignment status of every patent listed.
