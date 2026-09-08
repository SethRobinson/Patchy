# Vector Preview

View > Vector Preview (`view.vector_preview`, no default shortcut) displays supported
vector artwork at screen resolution. It starts off, persists as `view/vectorPreview`,
and applies to existing and newly activated document canvases. The checked action
means the option is requested; its tooltip and the status bar explain pixel-view
fallbacks. English strings use localization and ship with Japanese translations.

## Supported content and display

The visible document tree must contain native solid shape/fill layers, Normal leaf
blending, and Normal or Pass Through groups. Layer/fill/group opacity, all modeled
path combine operations, inverted/disabled shape paths, and solid strokes including
alignment, caps, joins and dashes use the existing rasterizer/compositor semantics.
Hidden or zero-opacity subtrees do not affect eligibility. Offscreen visible layers
still participate in eligibility, so panning cannot reveal unsupported content.

Raster layers, text, Smart Objects, gradients, patterns, separate raster/vector masks,
clipping, layer styles, filters, restricted channels, Blend If, unsupported vector
payloads, and palette/non-RGB8 documents fall back to the normal view. The option
stays enabled and resumes automatically when the document becomes eligible.

Above one physical screen pixel per document pixel, vectors rasterize at zoom times
device pixel ratio. Lower scales use the existing display mips. Editing gestures,
transform/warp sessions, channel/mask inspection, Quick Mask, curves clipping views,
and seamless tiling temporarily use the existing pixel previews. On completion the
sharp view resumes. Explicit grids, guides and editing overlays remain on top.

The view never edits document paths, bounds, cached layer pixels, revisions, history,
dirty state or output. Document previews and exports retain document resolution.
Window captures include Vector Preview and wait up to 60 seconds for rendering to
settle; an expired wait returns false. Scripts can toggle the view with
`app.runCommand("view.vector_preview")`, change `patchy.ui.zoom`, and call
`patchy.ui.captureWindow(path)`. Connector command restrictions still apply.

## Rendering and lifetime

`ui/vector_preview_renderer` snapshots only the visible vector models and group
properties through const access. A worker transforms that immutable scene into
physical viewport coordinates. Conservative control-point hulls plus stroke
extents cull ordinary shapes; empty/complement paths retain full-viewport coverage.
Coordinates and stroke extents are checked before entering the fixed-point
rasterizer. No full enlarged-document bitmap is allocated.

Each physical-pixel tile builds a temporary clipped raster layer tree and composites
through `qimage_from_document_rect`. Shared transformed geometry keeps the tile
antialias phase consistent. Only the tile's combined layer pixels survive its
rasterizer call; fill/stroke auxiliary planes are discarded. The raster reservation
includes the retained viewport, new output, retained tile layers, and conservative
coverage/group workspace. Budget/range/allocation failures discard the incomplete
frame and fall back. Path geometry is separate from this raster budget.

`canvas_widget_vector_preview.cpp` owns the scene, frame, generation, and worker.
Document invalidation drops its scene/frame and cancels obsolete work. Zoom, pan,
widget size and device pixel ratio identify a view. One worker runs per canvas;
changes cancel its request at tile/layer boundaries, and the next paint submits only
the newest view. A previous frame may be mapped to its document location during
view changes. Completed requests must match the current generation and view, with
QPointer protecting closed canvases. `render_settled()` includes this work. Unchanged
paints reuse the frame and never scan layer pixels or rasterize paths.

Tracked workers participate in application shutdown. Builds without background
threads retain pixel view rather than executing this work synchronously in paint.
Diagnostics count requests and record elapsed render time and peak raster reservation.

## Verification

`ui_vector_preview` covers full-render versus tile equality, fractional viewport
phase, stroke/complement/group semantics, eligibility, numeric/memory limits,
cancellation, cache reuse, canvas lifetime, the action, persistence, scripts and
unchanged PSD output/history. The optional fixture is
`local-test-fixtures/vector-preview/Little-Everywhere.psd`; it writes paired pixel
and vector canvas artifacts at native, enlarged, fractional and maximum zoom.
Repeat with `QT_SCALE_FACTOR=2` to exercise display density. Test-owned offscreen
canvases require no desktop input or attachment to the user's application.
