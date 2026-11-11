# Shape Appearance and fill layers

Split from [vector-tools.md](vector-tools.md): the Shape Appearance dialog (paint, stroke and dash presets, geometry, opacity, Edge), its worker-thread preview, and fill layers (New Fill Layer, Fill Layer Settings, 16/32-bit rendering).

## Appearance editing and fill layers

The Shape Appearance dialog opens from the vector badge, the layer and
canvas context menus ([tools.md](tools.md)), the options-bar Appearance gear, Layer >
Shape > Shape Appearance... (`layer.shape_appearance`), the Properties
panel's Edit Appearance..., and a Path Select / Direct Select double-click on
the shape's geometry. Reset restores the factory appearance (geometry stays). Controls: paint kind, width, alignment, caps, joins, dashes
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
(Photoshop's vector mask density on a shape layer, and what the file stores). A note
under the Density row says so while that is the case (`shapeDensityNote`: "Below 100%
the fill also shows over the whole canvas, as in Photoshop."), built like the Stroke
note below: no width of its own, a fixed height, and a spacer under the Edge section
that holds its room while it is hidden. Feather and Density show the same in Dynamic
Vector Preview ([vector-preview.md](vector-preview.md#shape-feather-and-density)).
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

Edits preview live and restore on cancel or exception; a PSD-read
gradient/pattern stroke stays untouched unless re-picked. The preview
rasterizes on a worker and shows only finished renders (`edit_active_shape_appearance`,
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
TARGETED Paths-panel row becomes the shape path (build_fill_layer),
selections become raster masks. Library patterns join
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
fill and the two fill the pixel); with Density under 100% the Density floor stands in for
"nothing", and an inverted path swaps the two sides. Stacked as two independent
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
- A PSD keeps the pixels its file carried until the shape is edited
  (`finalize_vector_layers`), so a shape saved by an older Patchy shows its seam until
  then; Dynamic Vector Preview draws it from the path, without one.

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

Known difference, not changed: with Density under 100% and a stroke, Photoshop tints the
canvas with the stroke's paint, not the fill's. At 50% a green square with a 10 px
gradient stroke shows the stroke's ramp alone at alpha 127 outside the shape, and inside
it the fill under a 50% wash of the stroke (as if Density blended the layer toward its
unmasked top paint). Patchy spreads the fill and leaves the stroke as drawn.
