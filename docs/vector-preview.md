# Dynamic Vector Preview

View > Dynamic Vector Preview and Preferences > Application > Dynamic Vector
Preview control the same application view preference. It starts off, persists as
`view/vectorPreview`, and applies across document tabs. The command is
`view.vector_preview`, with no default shortcut. The setting key and command id
are permanent; the visible label is not part of either.
Loading the preference synchronizes the View action before it can be used;
Preferences and the first menu toggle after startup reflect the live canvas state.

## Mixed content and display

Native shape/fill geometry and vector masks render at screen resolution alongside
ordinary pixel layers. Shape paints include solid colors, gradients, patterns and
strokes, with the existing path operations, caps, joins, dashes and stroke blending.
The CPU compositor preserves layer order, clipping runs, layer/fill opacity,
Normal and Pass Through groups, blend modes, Blend If, channel restrictions,
adjustments, raster masks and layer effects.

Raster content retains its document-pixel detail. Text, Smart Objects, applied
Smart Filter previews and unparsed vector records use their existing pixel
representation; the mode does not substitute fonts or re-open embedded documents.
Those layers can coexist with sharp vectors without disabling the view or showing
a mixed-content warning. Documents with no renderable vector geometry use
their normal view. Palette documents and anything other than 8-bit RGB (16/32-bit
and non-RGB documents) also keep the normal view (`vector_preview_renderer.cpp`
checks the document format).

Above one physical screen pixel per document pixel, vector geometry uses zoom
times device pixel ratio. Lower scales keep the existing display mips. Editing
gestures, warp sessions, channel/mask inspection, Quick Mask, curves clipping
views and seamless tiling temporarily use the existing pixel previews. The sharp
view resumes afterward.

Artboard documents render too: the scene keeps each artboard on its group copy
with the rect mapped to the view (edges rounded), so the compositor clips and
paints the paper as the document render does, and the frame's backdrop is
`draw_canvas_backdrop` (pasteboard between artboards, checkerboard inside).

A Free Transform session on a single shape layer (no pending warp, no raster
mask, not text or a smart object: `free_transform_vector_preview_possible`)
stays sharp between drags. The scene renders `free_transform_vector_preview_document`,
the document with the shape's vector data through the delta the commit applies
(`shape_layer_commit_delta`), whatever the interpolation setting; the key (layer
and delta) rebuilds it whenever the session changes, starts or ends. A drag shows
the pixel preview, and other sessions keep it throughout. Pinned by
`ui_vector_preview_stays_sharp_through_free_transform` (plain and artboard
documents) and `ui_vector_preview_renders_artboards`. Explicit grids, guides and editing overlays
remain on top (next section).

## Overlay order

The paint order is: composite (through `draw_deep_zoom_pixels` from 600%, or from
a lower "Show pixel grid from" threshold while the grid is drawn,
[view-navigation.md](view-navigation.md#deep-zoom-and-the-pixel-grid); it bakes
View > Show > Pixel Grid into its raster), then the preview frame
(`draw_vector_preview`, a checkerboard plus the frame over the composite), then
every overlay: the document grid, guides, artboards, the document border,
selection edges, tool overlays and readouts, and the rulers last. Anything an
overlay draws after the frame is above it by construction; only the pixel grid
lives inside the composite's raster, which the frame covers. The rule: whenever
the pixel grid is drawn (`pixel_grid_drawn`), `draw_vector_preview` redraws the
grid alone after the frame, a grid-only `deep_zoom_raster` (transparent except
the lines) of the document pixels at the current view's cell edges, so each line
keeps its cell's own tone and lands where the composite would draw it, including
while an old frame is mapped to a new view. Any future frame that paints over the
composite must do the same, or draw before `draw_deep_zoom_pixels`. The cost is
one more write per exposed device pixel, the composite's own bound. Test:
`ui_vector_preview_keeps_pixel_grid_and_overlays_above` (grid above the frame at
600 to 1200% and mid-zoom, none below 600% or with the grid off, preview toggles,
and guides, the document grid and selection edges above the frame).

The view does not edit document paths, bounds, cached pixels, revisions, history,
dirty state or output. Document previews and exports retain document resolution.
Window captures (`save_debug_screenshot`) include Dynamic Vector Preview and wait
up to 60 seconds for `render_settled()`; an expired wait returns false. Scripts
can toggle `app.runCommand("view.vector_preview")`, change `patchy.ui.zoom`, and
call `patchy.ui.captureWindow(path)`. Connector command restrictions still apply.

## Status messages

Routine progress, native-resolution views and temporary editing states appear
only in the action tooltip. They do not take over the status bar. A resource or
coordinate failure may show one short notice per distinct reason per window.
A notice never replaces an existing status message, and repeated edits, zooms,
pans or toggles do not repeat it. The preference remains enabled.

## Rendering and lifetime

`ui/vector_preview_renderer` builds an immutable tree of render properties through
const document access. Source pixel buffers, masks and pattern tiles share their
existing copy-on-write storage. PSD tagged blocks and embedded object payloads
are not cloned, apart from the pattern reference point needed for painting.
Source geometry and temporary paths remain separate from document objects.

The worker maps paths, stroke dimensions, pattern phases, masks and effect sizes
into physical viewport coordinates. Conservative geometry bounds cull shapes.
Empty/complement paths retain full coverage. Temporary tiles include effect and
feather halos so neighboring pixels contribute to the visible tile's result.
Raster layers and raster masks sample their original pixels
([Raster content sampling](#raster-content-sampling)); vector masks regenerate
their coverage at the display scale.

Merged vector layers expand their independent paints into temporary native vector
nodes when a scene is built. Parts retain their own paint anchors, stroke sizes
and opacity. Their combined tile uses the original pixel layer properties,
including Fill and clipping; tile bounds and raster budgets apply normally. See
[layer-merging.md](layer-merging.md).
Compound tiles use the group silhouette compositor over the complete padded
rectangle. The buffer and its bounds must cover the same area, including negative
viewport coordinates. Clipping that intermediate to the viewport loses effect
halos and makes its bounds disagree with its pixels
(`ui_vector_preview_compound_effect_halos_match_cropped_view`).

`VectorPaintBounds` separates a vector raster clip from the full paint coordinates,
so gradients do not restart at tile edges. Unpainted native path coverage anchors
gradients with transparent stops; painted alpha bounds remain the reference for
interior layer effects. These temporary native coverage rasters also pass the
preview budget check. `render/raster_view_context.hpp` provides scoped,
thread-local full-layer paint bounds to the existing compositor, including the
separate fill bounds used by interior effects on stroked shapes. Styled groups
anchor paints to their native child silhouette, including child masks; this
temporary native raster is budgeted before allocation. Normal renders have no
context and retain their existing behavior. Preview compositor calls run
synchronously on their owning worker and bypass persistent style and style-mask
caches, so temporary tile masks cannot pollute document caches.

Raster reservations cover retained/new viewport frames, tile layers, split planes,
masks and conservative effect/group workspace, including halos. Paths and shared
source storage are separate from this raster allocation budget. Numeric, budget
or allocation failures discard incomplete frames and retain the normal pixel view.
No enlarged full-document bitmap is allocated.

`canvas_widget_vector_preview.cpp` owns the scene, frame, generation and worker.
Document changes drop the scene/frame and cancel obsolete work. Physical scale,
pan and viewport size identify a view. One request runs per canvas; changes
coalesce to the newest view and obsolete results are discarded. A previous frame
may be mapped to its document location during view changes. QPointer protects
closed canvases, tracked workers participate in shutdown, and `render_settled()`
includes pending preview work. Unchanged repaints only reuse cached results.
Builds without background threads keep the normal view.

## Shape Feather and Density

A shape layer's Edge values belong to its node (`copy_nodes` takes `feather` and
`density` with the path and paints; local-051 follow-up 2: they were left out when
Shape Appearance gained them, so the sharp view drew the shape unfeathered and without
its Density tint for seven weeks). The sharp view must show what the document render
shows:

- Density below 100% shows the fill over the whole canvas (`apply_shape_density`), or
  with a stroke the stroke's paint (`spread_stroke_paint`), so such a node
  (`vector_shape_density_spreads`) has no bounds: it is never culled, and every tile's
  clip (its area inside the mapped canvas) takes the tint.
- Feather is scaled with the view and blurred by `rasterize_vector_shape`, which reaches
  the three box radii past the clip itself, so a halo is not cut at the path's box or at a
  tile edge. The node's cull bounds grow by that reach, and the blur's planes over the
  enlarged domain are reserved in the raster budget first.
- Past `kVectorPreviewNativeFeatherReach` (192 view pixels of reach, a feather of about
  64 view pixels) every tile would blur far past its own edge. A feather that wide has no
  detail a document pixel cannot hold, so the node is `sampled`: the layer's document
  bake, and its fill, stroke and matte planes for effects, are read bilinearly
  (`sample_pixels_smooth`) whatever the view's smoothing rule. A 9 px feather is blurred
  natively up to about 700% and sampled above.
- A layer-aligned gradient spans the box of its paint's coverage in the document render,
  and Feather changes that box: a feathered shape is drawn over the canvas grown by the
  feather's reach (so a shape past the canvas edge is cut there, not at the canvas).
  Density does not: under 100% the fill covers the whole clip, but the gradient stays on
  the shape, as in Photoshop
  ([vector-tools-appearance.md](vector-tools-appearance.md#gradient-fill-and-density)).
  `native_gradient_bounds` mirrors the bake in both; where it did not, a gradient on such
  a shape was a different gradient in the sharp view.

## Pattern sampling

A pattern is raster content: inside a shape the sharp view shows it as the pixel view
does, so turning the preview on changes only the edges. Above 100% a shape's pattern
fill, its pattern stroke and a Pattern Overlay (on a shape, a pixel layer or a group)
are drawn in two steps per tile:

- `defer_pattern` (in `transform_nodes`) keeps the paint as the document holds it
  (`VectorPreviewNode::view_patterns`, with the layer's effects reference point in
  document space) and points the paint at a per-tile pattern read at 100%, unrotated,
  unlinked, with no phase.
- `add_view_pattern` (in `raster_nodes`) builds that pattern for the tile's area, grown
  by a shape's feather reach: `PatternTileSampler` with the document's own scale, angle,
  link and phase gives the document pixels the area reads, exactly as the document
  render computes them (nearest at 100%, linear when the pattern is enlarged or rotated
  in the document, box when reduced), and `sample_pixels` or `sample_pixels_smooth`
  enlarges them by the view's rule ([Raster content sampling](#raster-content-sampling)),
  document edge clamp included. The texels are stored rolled so view pixel (x, y) is
  texel (x mod width, y mod height), whichever tile is drawn: one phase across tiles, no
  seam.

The per-tile patterns live in the tile's scratch pattern store, are reserved in the
raster budget and end with the tile (a merged vector layer's parts: with its flattening).
Nothing in `src/core` or the document render changes. Scaling the paint with the view
instead samples the tile's texels at view resolution, which interpolates between them:
a small pattern looked soft where the pixel view shows blocks.

Still scaled with the view (`transform_pattern`):

- Views at 100% and below. At 100% the scaled paint is the document's own sampling, and
  below it the canvas shows its pixel view, so the frame is never drawn there.
- A Bevel texture. It is a height field: its shading comes from the slope between
  neighboring pixels, so blocks would be flat inside each texel with a ridge around it.
- A pattern on a layer that is neither pixels nor a group, or one whose tile is missing.

There `PatternTileSampler` interpolates a magnified or rotated tile at whole pixel
indices, `src = (x - anchor) / scale`, and a view pixel's index is not its document
position times the zoom (its center sits at `(index + 0.5) / zoom - 0.5` document
pixels), so `transform_pattern` adds `(zoom - 1) / 2` to the scaled phase when the scaled
tile is magnified or rotated. The nearest and box-filter paths work on pixel edges and
take no shift.

Tests: `ui_vector_preview_patterns_follow_display_smoothing_rule` (fill, stroke and
overlay on a shape, a pixel layer and a group; linked, phased, scaled, rotated and
reduced patterns and transparent texels; exact blocks at 800%, 400%, 550% and 338.9%,
bilinear at 150%, 250% and 338.9%; tile/crop equality; a sharp shape edge) and
`ui_vector_preview_pattern_matches_pixel_view_on_canvas` (preview on against off at 800%
and 250%).

## Raster content sampling

The frame covers the composite, so its raster content (pixel layers, text, Smart
Objects, anything else kept as pixels, and raster masks) follows the canvas's one
display smoothing rule, `uses_smooth_display_scaling`
([view-navigation.md](view-navigation.md#display-interpolation)). Otherwise a photo
shown smooth at a fractional zoom turns into blocks the moment the document holds
a shape. `vector_preview_view()` reads the rule into `VectorPreviewView::smooth_pixels`;
the flag is part of the view's identity, so a change of it (a zoom, the pixel grid)
is a new frame. Vector geometry is sharp at screen resolution either way.

- Flag off (whole zooms, 400% and up, the pixel grid, deep zoom): `sample_pixels`,
  the exact blocks, device pixel center to the document pixel under it.
- Flag on (fractional zooms between 100% and 400%): `sample_pixels_smooth`,
  bilinear at the position Qt's smooth blit of the composite reads,
  (device + 0.5) / scale - 0.5. Color is weighted by alpha (no fringe at a layer
  edge); document pixels outside the layer are transparent, outside a mask its
  default color; the sampled rect and the cull bounds grow by half a document
  pixel (`smooth_sample_reach`) for that falloff.
- At the document edge the read position is clamped to the document, as the
  composite's blit clamps instead of fading into the backdrop. The clamp also
  covers device pixels up to one past the exact edge, which the composite's
  pixel-aligned rect can show. Farther out (clipped by the canvas) the exact read
  stays, so a layer that ends at the document edge keeps that silhouette for its
  effects.
- Each output pixel depends only on its own device position and the source, never
  on the tile, so tiles and a full render agree.
- Smoothing each layer before compositing equals smoothing the composite for
  Normal layers over an even backdrop. Where two layers both vary inside one
  document pixel (a layer edge over a busy photo, non-Normal blend modes,
  adjustments) the two differ slightly; that is accepted.

Tests: `ui_vector_preview_raster_content_follows_display_smoothing_rule` (canvas
with the preview on against off at 150%, 250% and 338.9%, exact blocks at 200%,
300%, 400% and 550%, a sharp shape edge; device pixel ratio 1, and 2 in a
`QT_SCALE_FACTOR=2` child run) and
`ui_vector_preview_smooth_pixels_match_bilinear_composite` (alpha, layer bounds,
mask default, document edge, tile/crop equality).

## Clipped flattening

Render cost follows the viewport, not the zoomed size of the shapes. Every
256 px tile rasterizes each shape that reaches it with its tile as the clip, so
nothing in the core rasterizer (`core/vector_raster.cpp`) may cost more than
linear in the zoomed path per tile, and its output must stay byte-identical:

- A curve (or a de Casteljau half of one) whose control box lies wholly outside
  the clip is emitted as its chord. The curve and its chord share endpoints and
  stay inside that box; `rasterize_edges` drops edges above, below and right of
  its buffer and gives one left of it only the per-row cover, which depends on
  the endpoints alone. Strokes grow the clip by the outline reach (half width
  times the miter limit, at least 1.5, plus 4 px) and keep every curve for
  dashes (arc length), Align Edges (its rectilinear test) and multi-group paths.
- Stroke outline pieces (segment quads, joins, caps: small convex loops) whose
  box misses the clip plus 2 px are not emitted; such a loop adds nothing inside
  the band buffer.
- A path that is one pixel-aligned rectangle (a closed four-corner subpath with
  straight sides on whole pixels and a first op other than Subtract: an
  artboard's paper and clip, a mask made from a marquee) is not scan-converted:
  its fill coverage is `full_coverage` of the rectangle inside the clip, the
  bytes the scan conversion gives (`raster_pixel_aligned_rect_shortcut_matches_reference`).
  A page-sized one took 13 s of a 115 Mpx artboard page's export derivation.
- A multi-group path's stroke follows its Boolean outline
  (`fill_boundary_contours`). Its crossings come from a sweep over segment boxes
  and its inside tests read only the edges in the query's horizontal band: the
  same pairs and edges all-pairs loops would. The outline does not depend on the
  clip, so the last outlines (up to 2 million points) stay cached per thread for
  the following tiles. SVG import gives every subpath its own group, so most
  imported strokes take this path.

`VectorRasterOptions::reference_geometry` turns all of this off (full
flattening, every loop, the all-pairs walk, no cache) as the tests' reference
(`raster_clipped_flattening_matches_full_flattening`,
`ui_vector_preview_cost_follows_viewport_not_zoom`, which also times
`test-fixtures/svg/hot_air_balloons_cc0.svg` from 100% to 1600%). New per-tile
work follows the same rule: clip to the tile plus its halo before doing anything
proportional to the zoomed geometry.

## Document shape bake past the canvas

The document bake (`update_vector_shape_raster`) is not clipped to the canvas
either: `shape_bake_domain` (core/vector_raster.cpp) extends it to the shape's
padded path hull, like a pixel layer keeps pixels past the edge, so a shape
moved onto the pasteboard keeps its pixels and stays grabbable. Coverage
buffers are sized to the shape, so a far-off shape costs its own size.
Disabled, inverted, and subtract-first paths (coverage fills the whole clip)
and hulls over max(16 Mpx, 4x the canvas) keep the canvas. The extended bake
passes `VectorPaintBounds{canvas}` so unaligned gradients and pattern phase
keep their canvas geometry and on-canvas pixels are unchanged; aligned
gradients follow the whole shape, as in Photoshop
(`update_vector_shape_raster_keeps_off_canvas_shape`,
`ui_shape_moved_off_canvas_by_free_transform_moves_back`).

## Verification

`tests/ui/vector_preview_parity_tests.cpp` holds the sharp view to the document render:

- `ui_vector_preview_matches_document_render_for_shape_layers`: 78 documents (every
  shape kind, fill, stroke alignment on straight, slanted and curved edges, dash, cap and
  join, Feather and Density alone, together and with a stroke, inverted and disabled
  paths, masks,
  effects, shapes past the canvas edge, combine ops, merged vector layers), each rendered
  at 100%, 400% and 250% (pattern paints at the whole zooms: blocks average back to
  document pixels only there). The frame,
  averaged down to document pixels, is compared with the composite; a reduced view counts
  how far a pixel lies outside the range of the other image's 3 x 3 neighborhood, so an
  antialiased edge costs nothing and a flat area, a missing feature or an extra one costs
  in full. A panned 400% view must equal the same pixels of the full one. The report is
  `test-artifacts/vector-preview-parity.txt`; a failing view saves its images beside it.
  One class is looser on purpose: blurs (Feather, effects), whose box sizes round
  differently at each zoom. A pattern is held as strictly as a flat fill, and so are
  Inside and Outside strokes: fill and stroke share the edge pixels without a seam at
  every zoom
  ([vector-tools-appearance.md](vector-tools-appearance.md#fill-and-stroke-on-shared-edge-pixels)).
- `ui_vector_preview_shows_shape_feather_and_density_at_every_zoom`: on the canvas at
  50%, 100%, 300% and 1600%, preview on and off, live in Shape Appearance, after OK, Undo
  and Cancel.
- `ui_shape_appearance_fields_show_on_the_canvas_for_every_shape_kind` and
  `ui_shape_options_bar_fields_show_on_the_canvas`: every spin box, list and checkbox of
  Shape Appearance, and the options bar's shape fields, for each shape kind with the
  preview on and off. After each change lands: the layer's pixels are what its outline
  draws, the sharp view matches the render, and forcing a fresh frame changes no canvas
  pixel. The changes pile up (Feather and Density stay set while the paints change), which
  is how the gradient box above was found. Failures are listed in
  `test-artifacts/vector-preview-states.txt` with the paints in play, and the first ones
  keep their pictures.

`ui_vector_preview` covers tile/full-render equality, fractional pan and zoom,
strokes and path complements, mixed raster/shape compositing, clipping, adjustments,
blend restrictions, gradients, patterns, masks, effects, resource limits,
cancellation, cache reuse, canvas lifetime, scripts, Preferences/menu persistence,
status-message suppression and unchanged document output/history.

The optional fixture is
`local-test-fixtures/vector-preview/Little-Everywhere.psd` (also read by
`tests/ui/layer_merge_tests.cpp`). Paired pixel/vector captures cover native,
enlarged, fractional and maximum zoom. Repeat with `QT_SCALE_FACTOR=2` for
display density. Test-owned offscreen canvases require no desktop input or
attachment to the user's application.
