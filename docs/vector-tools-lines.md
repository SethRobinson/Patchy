# Lines: stroke, weight and arrowheads

Split from [vector-tools.md](vector-tools.md): how a line's stroke is aligned, the Line tool's Weight and arrowheads in the options bar, Shape Appearance and scripts, the arrowhead size limits, and what a head longer than its line draws. The PSD record is in [vector-psd-encodings.md](vector-psd-encodings.md#vogk-vector-origination--live-shapes).

## A line's stroke

A line is a filled outline as wide as its Weight, in Photoshop too (27.11 over COM: a
4 px line from 40 to 360 is the quad 40,58 to 360,62). The stroke runs around that
outline, so an Inside stroke is clipped to it: it only recolours the line, whatever its
width (Photoshop: bounds 40,178 to 360,182 with a 20 px Inside stroke, 30,228 to 370,252
with Center). A line drawn with the Line tool therefore gets Center
(`current_shape_appearance_content(line)`, the drag preview included), so the Stroke width
makes it thicker; a script's `addShape` line and Shape Appearance's Reset on a line give
Center too. A saved stroke preset keeps the
alignment it was saved with, and the other shapes keep Inside. An existing Inside line
renders as before (it matches Photoshop for the same file); Shape Appearance says why its
stroke does not show ([note](vector-tools-appearance.md#appearance-editing-and-fill-layers)),
and so does the status bar after an options-bar width edit
(`note_inside_stroke_on_selected_lines`). Photoshop's `Mk` event gives a shape whose
stroke names no alignment Center, line and rectangle alike; its Line tool's own default
is sticky tool state COM does not show. A Patchy line saved with a Center stroke opens in
Photoshop without a dialog and its redraw there matches pixel for pixel. Test:
`ui_line_tool_new_line_stroke_is_centered`.

## Weight and arrowheads

The Line tool has
start/end arrowhead glyph toggles; a new head is 500% of the weight wide and 1000% long
(PS's proportions, `give_line_arrowheads_default_size`), encoded through the probed
keyOriginLine arrow keys. Weight and the
toggles also edit the selected live lines (GitHub #622), as Radius does rectangles:
the `line.weight` / `line.arrow_start` / `line.arrow_end` appearance properties
(appearance_properties.cpp; other shapes ignore them), a Weight drag queued as one
undo step, a toggle committed at once. A weight change scales the heads with it, so a
line keeps its own head proportions (`tie_line_arrowheads_to_weight`, with the previous
weight; options bar, Shape Appearance and script `updateShape` alike); a head switched
on keeps the line's own size if it has one. Selecting a
line shows its values (`sync_line_options_from_selection`) without changing the
next-line defaults. Shape Appearance has Weight in Geometry (GitHub #622 follow-up 2) and an
Arrowheads section (`shapeArrowheadsGroup`, #622 follow-up) with Start / End
(`shapeGeometryLineArrowStartCheck` / `EndCheck`), and Width, Length (percent of the weight, 10 to 100000% each,
[limits](#arrowhead-size-limits)) and Concavity (-50 to 50%; positive pulls the back of the head toward
the tip): `shapeArrowWidthSpin`, `shapeArrowLengthSpin`, `shapeArrowConcavitySpin`, off
while neither head is on. A head wider than the shaft makes one outline with it
(`line_subpaths`: the shaft edges run to where they meet the back edges), since an
overlap would be punched out by the group's even-odd fill and a concave head would
leave a gap behind its notch; a narrower head stays a separate polygon. The model keeps head sizes in pixels; the percent properties
`line.arrow_width` / `line.arrow_length` and `line.arrow_concavity` let several selected
lines take one field each, every line getting that percent of its own weight. The PSD
reader turns a head size whose `keyOriginLine*ArrowUnitPixels` flag is off (read as a
percent of the weight, from the key name; not confirmed by a Photoshop regeneration) into
pixels; the writer writes pixels with the flags on, which Photoshop reads back unchanged.

## Arrowhead size limits

Width and Length take 10 to 100000% of the weight (`kLineArrowMinPercent`,
`kLineArrowMaxPercent`; they were 1000% and 5000%, a 40 by 200 px head at most on a 4 px
line), and a head is at most 30000 px either way (`kLiveLineArrowMaxSize`). That one rule
serves the dialog, a multi-line edit and the appearance properties
(`line_arrow_size_from_percent`), a weight change (`tie_line_arrowheads_to_weight`) and
the script geometry (`arrowWidth` / `arrowLength`, 0 to 30000 px, and `updateShape`'s
weight scaling). The two fields step by a tenth of the value's size
(`AdaptiveDecimalStepType`: 1% under 100%, 100% from 1000%) and a drag on their labels
scrubs by the same step (`set_scrub_value`). The PSD reader keeps whatever size a file
holds. Tests: `ui_shape_appearance_arrowheads_grow_large`,
`ui_script_vector_line_stroke_centers_and_heads_grow`.

Photoshop 27.11 (COM, November 2026) opens Patchy files with heads of 320 by 480, 5000 by
5000 and 30000 by 30000 px on a 4 px line, and a 480 px head on a 200 px line, without a
dialog; reads the sizes back unchanged (`executeActionGet`); and draws the same shape
when made to redraw the layer (a fill colour change and back: within 2 of 255 on edge
pixels). No Photoshop limit was found below Patchy's own. PSD path points are 8.24
fractions of the canvas, so a point more than 128 canvas widths or heights away is
clamped (`path_coordinate_to_fixed`): a head that far past a small canvas reopens as a
plain path.

## A head longer than its line

A head keeps its size on a line shorter than it: the tip stays on the line's end and
the head's back passes the other end, so Width and Length draw what they say.
`line_subpaths` then leaves the shaft out (no length is left in front of the head's
back); a concave head whose notch still holds the line's other end has its back edges
cut off square there, and a convex head whose point passes the other end while its
sides have not puts that point on the end cap. Two heads share the line: when they
would run into each other, each is drawn at most as long as lets their backs meet in
the middle (their corners, or for convex heads the points where the backs meet the
shaft's edges), at its own width; the stored length stays. Every case is one outline
with no overlap (the group fills even-odd) and no shaft drawn backwards;
`clean_closed_outline` drops the points two heads share. Heads no wider than the shaft
stay separate polygons and lose the shaft when it has no length. Tests:
`live_line_head_longer_than_the_line_keeps_its_size`,
`live_line_two_heads_meet_in_the_middle`,
`live_line_outline_is_the_union_of_shaft_and_heads` (the outline against its parts over
1080 lines), `live_line_large_arrowhead_survives_a_psd_round_trip`.
