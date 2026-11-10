# Patent research record: drag points on live shapes (GitHub #690)

Scan of November 10, 2026. Claim text was read on Google Patents; statuses are Google's
estimates, unchecked against USPTO fee or assignment records. The binding rules are the
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

- A regular polygon or star: one disc on its center (a drag moves the shape on whole pixels)
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
| US 9250786 | Adobe, snapping via dragging | 2013-07-16, 2034-03-17 | As recorded in [patent-research-alignment.md](patent-research-alignment.md): the source feature of a dragged object is determined from where the drag started and is snapped to a target feature found during the drag. The claim text could not be fetched again in this scan. |

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
  feature. It moves by the pointer's travel on whole pixels and snaps to nothing. The vertex and
  a line's end follow the pointer through the same snapping the second point of the drawing
  drag and the box's handles already use; they resize, they do not reposition the object.

Open-web and Google Patents keyword searches (polygon with handle, vertex, center, rotate;
line with end point handle, arrowhead; "live shape", "parametric shape", on-canvas control;
Adobe as assignee from 2008) returned no patent whose independent claims describe either form.

## Outcome (decided 2026-11-10)

Both forms are built as described above. Kept out, each needing its own check first:

- A handle that edits a corner radius (US 10249067 is where that check starts), a side count
  or a star inset (Illustrator and Keynote ship such handles; no search was made for them).
- Any line, stem, arc, band or numeric readout drawn with the points.
- Snapping of the center point to guides, the grid or other layers.
- Points on Rectangle, Ellipse or Custom Shape. They keep the eight-handle box.

## Not verified

- Google Patents answered the first keyword queries and then refused automated requests
  (HTTP 503). The class-restricted queries (CPC G06T 11/20 and G06F 3/04845: polygon with
  handle, vertex and center; line with end point handle) were not completed. Repeat them before
  the points gain any behaviour.
- Adobe filings from 2019 to 2021 around Photoshop 22's shape controls (October 2020: a line
  defined by two control points with two end handles, on-canvas controls for polygons) were
  searched by keyword only.
- No assignee sweep for Apple, Microsoft, Corel, Serif, Figma or Canva.
- Adobe US 9229636 ("Drawing support tool", priority 2010-10-22, estimated expiry 2033-10-03)
  turned up for "drag points": its description is about a virtual straight edge positioned by
  drag points, not handles on a drawn shape. Its claims were not read.
- MacDraw (1984) and Visio 1.0 (1992) are widely described as showing a handle on each end of a
  selected line; no dated source was found, so they are not in the table. The date of each
  Inkscape handle modifier; USPTO fee and assignment status of every patent listed.
