# Shape Appearance and fill layers

Split from [vector-tools.md](vector-tools.md): Shape Appearance (the stay-open window and the modal dialog: paint, stroke and dash presets, geometry, opacity, Edge), the dialog's worker-thread preview, and fill layers (New Fill Layer, Fill Layer Settings, 16/32-bit rendering).

Related documents:

- [vector-tools-appearance-window.md](vector-tools-appearance-window.md): the stay-open Shape Appearance window (following the selection, one undo step per gesture, lifetime rules).

## Appearance editing and fill layers

Shape Appearance opens from the vector badge, the layer and
canvas context menus ([tools.md](tools.md)), the options-bar Appearance gear, Layer >
Shape > Shape Appearance... (`layer.shape_appearance`), the Properties
panel's Edit Appearance..., and a Path Select / Direct Select double-click on
the shape's geometry. All of them open [the stay-open window](vector-tools-appearance-window.md);
the modal dialog with the same rows is what New Fill Layer shows. Reset restores the factory appearance (geometry stays). Controls: paint kind, width, alignment, caps, joins, dashes
(`StrokeDashPreset` ids as item data, Custom last). Custom's Dash pattern field
(`shapeStrokeDashPatternEdit`, `parse_dash_pattern_text`) takes lengths in stroke widths,
space separated; an odd count repeats once (as SVG) and one length must be above 0.
It holds any file's array and commits on Enter or focus-out. Zero-length dashes draw only
through Round or Square caps (nothing draws them with Butt caps), so Dotted is `dotted_dash_pattern(cap)`: {0, 2}, or {1, 1} with Butt caps, the
same square dots; changing Caps while Dotted is chosen swaps the array (#548).
The other presets follow the cap the same way (`stroke_dash_preset_pattern`: written for
Butt caps, each dash one width shorter and each gap one width longer for Round or Square), so
they look alike on every cap; Dashed alone stays a fixed {2, 2}. A preset also sets
its caps and corners (`apply_stroke_dash_preset`, shared with the options bar; #548): Dashed, Long dashes and Short dashes set Butt caps; Dotted, Sparse
dots, Dash dot and Dash dot dot set Round caps and Round corners, so every dot is round; Solid
sets neither. Caps and Corners stay editable. `stroke_dash_preset_for` maps a file's array to
its row by its shortest repeating period (GIMP repeats each pattern to fill its dash table), so
GIMP's Dense dots on Butt caps is Dotted; arrays matching no preset show as Custom.
Dash offset (`shapeStrokeDashOffsetSpin`, stroke widths; vstk strokeStyleLineDashOffset, SVG
`stroke-dashoffset`) works only while dashed. Sub-lattice
dash lengths clamp to the raster lattice; excessive boundary counts fall
back to a solid stroke. Saved stroke presets join the built-ins in one Dashes list (#557):
[style-presets-strokes.md](style-presets-strokes.md#shape-appearance-stroke-presets). A gradient's and a pattern's own
rows are a nested form in their picker row's field column (#552). The right column lines up
the same way (#679, `align_form_columns`): Fill's and Stroke's forms share one label width, and
the four nested gradient and pattern forms share another; the fill-only dialog measures Fill alone.
`pattern_linked` anchors at the effects
reference point when on and document origin when off; offsets add either way
(PatternTileSampler).

Edge's Density below 100% shows the fill over the whole canvas, not only at the shape
(Photoshop's vector mask density on a shape layer, and what the file stores), or, when
the shape has a stroke, the stroke's paint ([Density with a stroke](#density-with-a-stroke)).
A note under the Density row says so while that is the case (`shapeDensityNote`: "Below
100% the fill also shows over the whole canvas, as in Photoshop.", or the stroke wording),
built like the Stroke note below: no width of its own, a fixed height, and a spacer under
the Edge section that holds its room while it is hidden. Feather and Density show the
same in Dynamic Vector Preview ([vector-preview.md](vector-preview.md#shape-feather-and-density)).
How the bake draws the fill beside a stroke, and a gradient under Density:
[Fill and stroke on shared edge pixels](#fill-and-stroke-on-shared-edge-pixels),
[Gradient fill and Density](#gradient-fill-and-density).

With a line among the targets the Stroke form has a note under Align
(`shapeStrokeInsideLineHint`, local-051), shown while the stroke is on and Align is
Inside: "Inside keeps the stroke within the line's Weight. Choose Center or Outside to
make the line thicker." ([why](vector-tools-lines.md#a-lines-stroke)). It takes no width of its
own (Ignored horizontal policy) and has a fixed height, what the text needs at the form's
narrowest. QFormLayout drops a hidden row whatever its retain-size policy, so while the
note is hidden a spacer under the Stroke section (`stroke_hint_room`) holds its height:
the rows below Align close up, and the page and the dialog keep their size when the note
comes and goes (`ui_shape_appearance_inside_stroke_hint_on_lines`). Reset gives a line
Center, the stroke a new line gets.

The left column is Layer, Geometry (`shapeGeometryGroup`), Radius (`shapeRadiusGroup`),
Arrowheads, Polygon (`shapePolygonGroup`) and Edge. Every section lines its fields up
(#641): Geometry and Radius are grids (label, link column, field), the others forms whose
label-to-field gap is the link column, and `align_section_columns` gives all of them the
widest label's width. Geometry (X, Y, Width, Height, or a line's Start, End and Weight)
needs one modeled origination covering every subpath (rect, ellipse or polygon bounds,
line endpoints) or, for a plain path with no modeled origination (custom shape, Pen
outline; #642), the path's bounds (`ShapeAppearanceSettings::path_box`): a changed box
moves and scales the path (`fit_vector_path_to_box`) and drops any unmodeled origination
read from the file. A plain path's Geometry ends with the Custom Shape tool's Shape picker
(`shapeGeometryCustomShapeCombo`, #644): a pick replaces the outline (`path_shape`, one shape
group) with that preset stretched to the box shown. It opens on the shape the layer was
stamped from or last picked (`VectorShapeContent::library_shape_id`, session only, never
written), by one id lookup plus one check that the outline still is that shape fitted to its
bounds (`vector_path_is_fitted_shape`): moves and axis scales keep it, point edits, combines,
rotations and flips end it, and a reopened file or a deleted shape opens it empty. Never
match an outline against the whole library (slow with the bundled pack). Radius holds the four corners, Top left: to Bottom left:
(#641); Polygon holds Sides and Star inset (#643). With several layers selected the
single-layer rows (X, Y, Width, Height, Start, End) are off or absent; Weight
(#622 follow-up 2) sits in a Geometry box of its own when no single shape is shown, and
Weight, Shape, Radius, Arrowheads and Polygon apply per field to the layers they fit. A
line's arrowheads (a Heads: row with Start and End, `shapeArrowHeadsLabel`; Width, Length,
Concavity) are its Arrowheads section
([vector-tools-lines.md](vector-tools-lines.md#weight-and-arrowheads)). A radius makes a
rect rounded. Chain buttons sit between labels and fields: `shapeGeometryLinkButton` keeps W/H in the ratio captured when
switched on; `shapeGeometryRadiusLinkButton` edits all four radii together,
on by default only when the corners agree. Dialogs are the reviewed route; no on-canvas gizmos. The dialog also edits layer Opacity and Fill opacity,
the stroke's own opacity (vstk strokeStyleOpacity), and Feather / Density
(Edge group); all PSD-native, one undo entry.

In the modal dialog, edits preview live and restore on cancel or exception; a PSD-read
gradient/pattern stroke stays untouched unless re-picked. The preview
rasterizes on a worker and shows only finished renders (`edit_active_shape_appearance_modal`,
local-051 follow-up): a request's outline and the pixels baked from it reach the document
together when its render lands, so the composite, the path outline and Dynamic Vector
Preview never pair a new outline with older pixels, and between two renders the canvas
shows the last finished one, never the layer from when the dialog opened. One render runs
at a time and a finished one lands even while a newer value waits (the adjustment
previews' rule, GitHub #601), so a scrub shows each size in order; requests coalesce to
the newest; Preview off restores the originals at once and bumps the generation, so a
render it overtook never lands. The request's pattern store lands with its layers, the
pattern anchor rides a scratch layer, and accept commits the in-flight result (60 s
timeout). Test: `ui_shape_appearance_preview_shows_only_finished_renders`.

Layer > New Fill Layer makes Solid Color, Gradient (FG-to-BG linear) and
Pattern fill layers: shape layers with an empty path (whole canvas); a
TARGETED work-path or saved-path row of the Paths panel becomes the shape
path (build_fill_layer). The active layer's own path row (a shape layer's
path, a vector mask, a text box) is targeted automatically and never
counts, so a fill layer made with a shape layer selected spans the canvas
(GitHub #710; `ui_new_fill_layer_spans_canvas_with_shape_layer_active`,
`ui_new_gradient_fill_layer_clips_to_targeted_work_path`). The Gradient
tool's Fill Layer mode builds its layer the same way.
Selections become raster masks. Library patterns join
the document store on use. New Gradient/Pattern Fill stages the layer;
one history entry, only on OK; Cancel restores the document, active layer
and pattern store. On a pathless fill layer (`fill_layer_only`) the dialog,
titled Fill Layer Settings, shows only Fill (no heading), typed Solid Color,
Gradient or Pattern (no No Fill) and saved so (#582). Gradient lists open on
the layer's preset, else Custom ([gradients.md](gradients.md#preset-lists)).
Such a layer (`layer_is_fill_layer`, #580) has a fill badge, not the vector one;
the badge, row and thumbnail double-clicks and Edit Fill Settings... open
the dialog (#607).

On 16/32-bit documents shape and fill layers keep regenerating their 8-bit
bake for the deep compositor, except gradient-only fills
(`deep_gradient_fill_raster`: no stroke, feather, density or parts) that
render at depth.

## Fill and stroke on shared edge pixels

An Inside or Outside stroke is the double-width band cut by the path's own coverage
(`rasterize_stroke_band`), so on a pixel the path's edge crosses, the stroke and the fill
are two parts of one pixel: the stroke lies in the part inside the path (Inside) or in the
part outside it (Outside). `rasterize_vector_shape` composites them that way. With `a` the
fill's alpha on the pixel, `s` the stroke's coverage, `o` its opacity and `u` the fill's
alpha under the stroke:

    alpha = a + s * o * (1 - u)        fill's share = a - s * o * u

`u` is the fill paint's own opacity where the fill is whole (an Inside stroke replaces
fill, it never adds to it) and nothing where it is not (an Outside stroke sits beside the
fill and the two fill the pixel); an inverted path swaps the two sides, and Density does
not enter (under a stroke the fill is never lifted). Stacked as two independent
coverages with "over" (`u = a`, the rule until November 2026) an Outside edge pixel was
only `1 - f + f * f` covered (75% at `f` = 0.5), a pale seam of backdrop between fill and
stroke, and fill color showed outside an Inside stroke. It was in the baked pixels, so
in saved files too.

Pixels wholly inside or outside the path, Center strokes (the stroke covers the edge),
fill-less shapes and paths with an open contour (its band is moved sideways, not cut)
keep the plain "over" byte for byte: on the probes below a Center stroke changed no
pixel and an Inside or Outside stroke only the pixels the path crosses (398 of 32,000 on
the ellipse). Stroke and fill coverage themselves are unchanged
(`stroke_golden_digests_are_stable`, `raster_golden_digests_are_stable`).

Photoshop 27.11 over COM (November 2026): a red fill and a blue 8 px stroke on a
transparent canvas, a 120 x 100 ellipse and a 110 x 64 rectangle turned 20 degrees, each
layer saved as a PNG. Red times alpha is the fill's share of a pixel, blue times alpha
the stroke's. Values of 255; ellipse / rectangle.

| | Photoshop | Before | Now |
|---|---|---|---|
| Outside: uncovered part of the pixels fill and stroke share | 0 on all 354 / 365 | to 64, mean 44 / 45 | 0 on all 351 / 368 |
| Inside: fill's share of the partly covered edge pixels | 0 on all 386 / 418 | to 64, mean 40 / 38 | 0 on all 398 / 424 |
| Inside: mean alpha of those pixels | 138.5 / 124.4 | 161.4 / 149.2 | 138.6 / 124.2 |
| Outside on a 50% fill (gradient, both alpha stops 50%): uncovered, mean | 69.0 / 68.6 | 104.3 / 102.2 | 69.1 / 69.2 |
| Outside, dashes {2, 2}: uncovered, mean (dash ends only in Photoshop) | 7.8 / 8.9 | 47.9 / 58.2 | 6.0 / 16.2 |

- On a half-transparent fill Photoshop's Outside edge pixel is `0.5 f + (1 - f)` (202 at
  `f` = 106) and its Inside edge pixel the stroke alone at alpha `f`: side by side and
  nested, never doubled. Center strokes agreed before and are unchanged.
- The path's own coverage agrees with Photoshop's within 17 of 255 on the ellipse and 3
  on the rectangle. Flattened over a pale backdrop (yellow fill, navy stroke), pixels
  more than 24 from Photoshop's render of the same PSD: 215 before and 0 now (Outside
  ellipse), 232 and 0 (Outside rectangle), and on the path's edge of the Inside ellipse
  225 and 0.
- Not changed, measured on the same probes: the inner edge of an Inside stroke differs
  a little from Photoshop's, on the ellipse by a fraction of a pixel (63 pixels more than
  24 apart, largest 39) and on the rectangle at two of its inner corners (41).
- The stroke's own opacity has no Photoshop reference: 27.11 reads `strokeStyleOpacity`
  back (50.2%) but draws the stroke at full strength, from a file and after setting it
  through Action Manager, flattened too. Patchy applies it with the same geometry (the
  formula above).
- The split planes for interior overlays (`fill_cache`, `stroke_cache`) are unchanged:
  the compositor still stamps the stroke over the overlays with "over", on a base pass
  that no longer has the seam.
- Existing files, measured: a PSD Patchy saved before the change opens redrawn, with no
  seam (11 probe files equal the new render unedited, the gradient and Density ones
  too). A PSD Photoshop saved opens with Photoshop's stored pixels (2 probe files
  re-saved by Photoshop equal its own render on every pixel; `finalize_vector_layers`
  keeps them unless the shape has
  [Feather or Density](#feather-and-density-in-files-photoshop-saved)), and an edit of
  the shape redraws it.

Tests: `stroke_shape_fill_and_stroke_share_edge_pixels` (Outside, Inside, half opacity,
Density, on an ellipse and a slanted quadrilateral), the Inside and Outside cases of
`ui_vector_preview_matches_document_render_for_shape_layers`, now held to its strict
limits ([vector-preview.md](vector-preview.md#verification)). The probe scripts and
Photoshop's renders are in `local-test-fixtures/vector-probe/stroke-seam/`.

## Gradient fill and Density

Density under 100% spreads the fill over the canvas, but a gradient aligned with the
layer stays on the shape. Photoshop 27.11 (an 80 px square at 60..140 on a 200 x 160
canvas, a red to blue ramp at 0 degrees, Density set through `vectorMaskDensity`): at
100%, 98% and 50% the square's pixels are identical, and the rest of the canvas shows the
ramp's end colors (red left of the square, blue right of it) at alpha 5 and 127. So
`rasterize_vector_shape` aligns the gradient to the fill's coverage bounds from before
Density (`fill_alignment_bounds`); `gradient_fill_alignment_bounds` (the Gradient tool's
handles) and Dynamic Vector Preview's `native_gradient_bounds` give the same box. Until
November 2026 the ramp ran over the whole canvas once Density was under 100%. Patchy's
render of that probe now equals Photoshop's on every pixel at all three values. A
gradient not aligned with the layer spans the canvas in both, at any Density. Test:
`gradient_fill_alignment_bounds_follow_the_bake`.

With a stroke the fill no longer spreads at all: [Density with a stroke](#density-with-a-stroke).

## Density with a stroke

On a shape whose stroke draws, Density under 100% does not spread the fill. Photoshop
27.11 shows

    density * the shape as drawn at 100% + (1 - density) * the stroke's paint alone

mixed premultiplied, with the stroke's paint spread over the whole canvas: a solid wash
of the stroke's color, not its band or its dashes. So the canvas is tinted with the
stroke's color, the fill sits under a wash of it, and the stroke itself is unchanged; at
0% the canvas is the stroke's paint. Without a stroke (off, zero width, no paint, or a
disabled path) the fill's paint is what spreads, as before (`apply_shape_density`).
`rasterize_vector_shape` draws the shape at 100% with the fill's coverage left alone,
feathers it, then mixes in the wash (`spread_stroke_paint`); Density 100% takes none of
this path and is byte-identical. `vector_shape_density_spreads` tells Dynamic Vector
Preview such a shape has no bounds to cull by.

Measured over COM (November 2026) on 25 probes, a green square with a red or red to blue
stroke on a transparent canvas, at Density 98%, 50% and 0% (`vectorMaskDensity`; bytes
250, 128 and 0). Against the formula, built from Photoshop's own 100% and 0% renders, the
largest difference is under 1 of 255 on 23 probes and 1.4 on the feathered one; the
inverted path is the one exception (below):

- Inside, Center and Outside strokes; 2, 10 and 30 px; dashes {2, 2} (the wash is solid).
  Far from the shape the stroke's red is at alpha 5, 127 and 255; inside the fill the
  pixel is 5,250,0 at 98% and 127,128,0 at 50%.
- A gradient stroke aligned with the layer keeps its ramp on the stroke's bounds and shows
  its end colors past them; one not aligned spans the canvas. A pattern stroke tiles the
  canvas.
- No fill: the wash also covers the hollow inside. A half-transparent fill or stroke
  paint mixes by the same formula (a 50% stroke paint washes at alpha 64 at Density 50%).
- Feather blurs the shape, not the wash. Subtract, intersect and exclude: the wash is the
  same, under the combined shape.
- A Color Overlay stays on the fill and the wash keeps the stroke's color: 127,0,128
  inside a blue-overlaid fill under a red stroke at 50%.
- The stroke's own opacity, which Photoshop ignores, thins the wash with the stroke in
  Patchy.

The split planes follow the same picture for the compositor's overlay pass: `fill_cache`
stays the fill at 100%, and `stroke_cache` carries the wash where the fill is (an overlay
has just covered it there) and nothing outside it, where the layer's own pixels hold the
wash. An overlay at 100% is exact; a thinner overlay keeps a second-order trace of the
fill under the wash.

Not matched: an inverted path with a stroke. Photoshop draws it unlike anything else even
at 100% (the stroke's color over everything outside the stroke's outer edge, a hole
inside), and after `vectorMaskDensity` is set it shows the fill inside and the stroke's
color outside at alpha `density` (nothing at 0%). Photoshop's own panels cannot make such
a shape; Patchy applies the formula to its own drawing of it.

Also seen at 100%, not changed: a stroke paint with transparency (a gradient whose alpha
stops are 50%) replaces the fill beneath it in Photoshop (255,0,0 at alpha 128 on the
band), where Patchy lets the fill show through it (128,127,0 opaque).

The note under Density names what spreads: "Below 100% the stroke's color also shows over
the whole canvas and over the fill, as in Photoshop." while the dialog's stroke draws
(on, width and opacity above 0), the fill wording otherwise. It follows the stroke fields
as they change and its fixed height fits either wording.

Existing documents: Patchy redraws its own shapes when a file opens, so a document with
Density under 100% and a stroke looks different from this change on, the way Photoshop
shows it.

Patchy against Photoshop on the same probes: 10 are identical at all four Density values
(Inside, Center and Outside, 2 and 30 px, the aligned gradient, dashes, no fill, stroke
off and the pattern stroke) and the canvas gradient and the half-transparent fill are
within 1, where before the mean difference at 50% was 68 to 124 of 255 (only stroke off
already matched). The rest keep only what
they differ by at 100% (edge antialiasing on curves and combined paths up to 55, the
feather's blur 5.4), scaled down by Density. PSDs written by the new build open in
Photoshop 27.11 without a warning (13 of 13), and Photoshop's render of a Density 50%
picture equals Patchy's within 1.

Tests: `raster_shape_density_spreads_the_strokes_paint` (fails before the change),
`compositor_interior_overlay_stays_under_vector_stroke` (the Density block),
`psd_photoshop_saved_shapes_keep_feather_and_density` (Photoshop's flatten of a gradient
stroke at Density 50%), eight Density-with-stroke documents in
`ui_vector_preview_matches_document_render_for_shape_layers` at the strict limits, and
`ui_shape_appearance_density_note_names_what_spreads`. Probes and scripts:
`local-test-fixtures/vector-probe/stroke-seam/density/`.

## Feather and Density in files Photoshop saved

Once Photoshop has drawn a shape it saves the shape's crisp pixels in the layer record
(record bounds are the shape and its stroke, alpha up to 255) and keeps Feather and
Density only as mask parameters, applied on screen (section flags 0x18, `vector feather`
and `vector density`, the derived plane in channel -2). A shape that was never drawn has
an empty record: Photoshop's script-made fixtures, and every shape Patchy writes. The
reader kept stored pixels as Photoshop's render whenever they had alpha, so a shape
Photoshop had saved with Feather or Density opened hard-edged and without its tint until
it was edited. Seven probes re-saved by Photoshop 27.11, against its own render: mean 102
to 122 of 255 at Density 50% (114 with a stroke), 16 for Feather 4 alone (largest 179),
and 0 with neither. `finalize_vector_layers` now draws a shape that has Feather or
Density from its path, as it does a record with no pixels; any other shape keeps
Photoshop's pixels. The same probes then equal Photoshop's render on every pixel
(Density, with and without a stroke) or within 5.4 of 255 (Feather).

Fixtures `photoshop-shape-density-stroke.psd` and `photoshop-shape-edges-resaved.psd`
with Photoshop's flatten beside each ([vector-fixtures.md](vector-fixtures.md)); test
`psd_photoshop_saved_shapes_keep_feather_and_density` (Patchy's flatten equals the first
and is within 4 of 255 of the second; before, means of 54 and 49).
