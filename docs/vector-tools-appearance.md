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
rasterizes on a worker: the vector MODEL applies synchronously, baked pixels
lag, requests coalesce, the pattern anchor rides a scratch layer; accept
commits the in-flight result (60 s timeout).

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
