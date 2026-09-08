# Dynamic Vector Preview

View > Dynamic Vector Preview and Preferences > Application > Dynamic Vector
Preview control the same application view preference. It starts off, persists as
`view/vectorPreview`, and applies across document tabs. The command remains
`view.vector_preview`, with no default shortcut. Renaming the label does not
change existing settings or scripts.
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
a mixed-content warning. Documents with no renderable vector geometry simply use
their normal view. Palette and non-RGB8 documents also retain their normal view.

Above one physical screen pixel per document pixel, vector geometry uses zoom
times device pixel ratio. Lower scales keep the existing display mips. Editing
gestures, transform/warp sessions, channel/mask inspection, Quick Mask, curves
clipping views and seamless tiling temporarily use the existing pixel previews.
The sharp view resumes afterward. Explicit grids, guides and editing overlays
remain on top.

The view does not edit document paths, bounds, cached pixels, revisions, history,
dirty state or output. Document previews and exports retain document resolution.
Window captures include Dynamic Vector Preview and wait up to 60 seconds for it
to settle; an expired wait returns false. Scripts can toggle
`app.runCommand("view.vector_preview")`, change `patchy.ui.zoom`, and call
`patchy.ui.captureWindow(path)`. Connector command restrictions still apply.

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
Raster layers and raster masks sample their original pixels; vector masks
regenerate their coverage at the display scale.

`VectorPaintBounds` separates a vector raster clip from the full paint coordinates,
so gradients do not restart at tile edges. Unpainted native path coverage anchors
gradients with transparent stops; painted
alpha bounds remain the reference for interior layer effects. These temporary
native coverage rasters also pass the preview budget check.
`render/raster_view_context.hpp` provides
scoped, thread-local full-layer paint bounds to the existing compositor, including
the separate fill bounds used by interior effects on stroked shapes. Styled
groups anchor paints to their native child silhouette, including child masks;
this temporary native raster is budgeted before allocation. Normal
renders have no context and retain their existing behavior. Preview compositor
calls run synchronously on their owning worker and bypass persistent style and style-mask
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

## Verification

`ui_vector_preview` covers tile/full-render equality, fractional pan and zoom,
strokes and path complements, mixed raster/shape compositing, clipping, adjustments,
blend restrictions, gradients, patterns, masks, effects, resource limits,
cancellation, cache reuse, canvas lifetime, scripts, Preferences/menu persistence,
status-message suppression and unchanged document output/history.

The optional fixture is
`local-test-fixtures/vector-preview/Little-Everywhere.psd`. Paired pixel/vector
captures cover native, enlarged, fractional and maximum zoom. Repeat with
`QT_SCALE_FACTOR=2` for display density. Test-owned offscreen canvases require no
desktop input or attachment to the user's application.

Merged vector layers expand their independent paints into temporary native vector
nodes when a scene is built. Parts retain their own paint anchors, stroke sizes
and opacity. Their combined tile uses the original pixel layer properties,
including Fill and clipping; tile bounds and raster budgets apply normally. See
[layer-merging.md](layer-merging.md).
