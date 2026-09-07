# Paint with native brushes

Read the current API before painting. `patchy.brushes.listTips()` and
`listPresets()` discover installed brushes. Inspect the returned settings; names
alone do not establish what a brush does. `resolve(settings)` reports effective
settings and applicable capabilities. Never invent settings from another editor.

## Choose the mark before the painting

1. Inspect the reference, crop, light direction, large value shapes and palette.
2. Choose a few candidate tips and render native swatches with
   `patchy.brushes.renderPreview(path, settings)`. Inspect the PNGs, or make a
   separate swatch document and use `get_preview`.
3. Block large forms on separate layers, paint a small batch, inspect the preview,
   and revise brush choices or stroke directions before adding detail.
4. Save layered checkpoints. Evaluate the full composition and close-up details.

An oil painting needs varied edges, convincing values and directional marks.
Short round strokes at full Flow alone tend to produce blobs. Use available
textured tips, moderate Flow, appropriate spacing, angle/direction controls and
intentional stroke lengths. Follow the fur or form, reserving small high-contrast
marks for focal details. Inspect rather than assuming a texture pass improved it.

## Brush, wet edges, and mixing

- Brush dynamics include shape, scattering, transfer, static texture, dual brush,
  color variation, and Wet Edges. Set `dynamics.sizeControl: 'penPressure'` and
  `opacityControl: 'off'` for a taper that stays opaque. A `global` control uses the
  stroke's explicit `pen` settings, never the artist's tablet preferences.
- Wet Edges pools coverage at the boundary of the whole stroke. It does not pick
  up canvas color. It is useful for wash-like edges, not automatic oil blending.
- `tool: 'mixer'` uses Wet/Load/Mix plus Flow. Sample All Layers can blend visible
  colors onto a separate correction layer. Start with low-to-moderate Wet/Mix and
  compare; excessive mixing can muddy a focal feature. Each stroke gets a fresh
  source snapshot and pickup state. Later strokes can mix earlier work.
- Mixer/Eraser use static tips; ordinary Brush dynamics do not apply to them.
  Do not pass unsupported effect settings. These are the existing native models,
  not physical bristle or fluid simulations.

## Time and visible work

Airbrush needs a complete `timeMs` timeline starting at zero. Repeated positions
with increasing times dwell; timed smoothing uses the same timeline. Input time
is simulated, so `patchy.ui.present(60)` controls viewing pace without adding
paint. For watchable work, paint one stroke or a small batch, present, then
continue. Stop remains available. Hidden work omits deliberate viewing delays.

## Reuse and preserve settings

Ordinary strokes use explicit temporary settings and restore the artist's brush.
For "use my current brush", call `getCurrent()` and copy those settings into the
stroke. Only call `activate()` when the user wants the manual brush changed.

`createTip` accepts coverage bytes, image files, or a specified document region.
`importAbr` returns created IDs and warnings. Inspect warnings before claiming
Photoshop brush equivalence. `savePreset` snapshots the brush and tip for later
UI/script use. Colors are excluded unless requested. Resource writes persist
outside document Undo; creating a preset does not mean selecting it.

Examples: `brush-swatches`, `fur-strokes`, `wet-paint`, `brush-library`, and `timed-brush` through
`get_help`. Use IDs returned by discovery, preserve the user's documents, and
save the editable result before ending an isolated session.
