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
rows are a nested form in their picker row's field column (#552).
`pattern_linked` anchors at the effects
reference point when on and document origin when off; offsets add either way
(PatternTileSampler).

Geometry requires one modeled origination covering every subpath: rect
bounds and radii, ellipse bounds, or line endpoints/weight. A radius makes a
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
