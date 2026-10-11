# Vector PSD fixtures

The committed fixtures the vector codec and renderer tests read
(`tests/core/psd_vector_fixtures_tests.cpp` unless noted). The encodings they
pin are in [vector-tools.md](vector-tools.md).

## Inventory (test-fixtures/psd)

Photoshop-authored files were made through COM. Each .psd with a .bmp
sibling has Photoshop's own flatten beside it (24-bit, white background
layer) for render-parity tests (`check_flatten_matches_reference`, whose
per-fixture gates live in the test); the embedded composites are
headless-stale ([ps-compat.md](ps-compat.md)).

- photoshop-shape-solid.psd/bmp: curved shape, SoCo red; pins knot in/out
  order via render.
- photoshop-shape-gradient.psd/bmp: GdFl linear 37 deg, 3 color + 3
  transparency stops with midpoints.
- photoshop-shape-pattern.psd/bmp: PtFl, 8x8 checker in the Patt block.
- photoshop-shape-strokes.psd/bmp: six stroked layers (alignments, caps,
  joins, dashed open curve, stroke-only / fillEnabled false).
- photoshop-shape-boolean.psd/bmp: four subpaths add/subtract/intersect/xor
  (sequential-combine ground truth).
- photoshop-shape-first-ops.psd/bmp: single-subpath layers with op
  subtract/intersect/xor (initial-accumulator semantics).
- photoshop-shape-live-rect.psd/bmp: live rounded rect (radii 4/8/12/16),
  live ellipse, live line w4 (vogk per kind; vowv presence).
- photoshop-vector-mask-on-pixel.psd/bmp: pixel layer + vector mask; no mask
  channel or section.
- photoshop-both-masks.psd/bmp: raster + vector masks on one layer; second
  layer at density 60% + feather 1.5 px (parameters + baked -2, flags 0x18).
- photoshop-both-masks-params.psd/bmp: raster mask + vector mask with its own
  density/feather on one layer: the 48-byte section, -2 as Photoshop's
  combined render, the painted mask in -3 (layout in vector-tools.md,
  "Vector masks on layers").
- photoshop-vector-mask-feather.psd/bmp: vector feather 4 (path on the
  canvas corner) and 8.
- photoshop-user-mask-params.psd/bmp: raster-mask feather 3 + density 50%,
  feather 6.5, density 25%.
- photoshop-saved-paths.psd/bmp: "Alpha Path" (rect, clipping path), "Beta
  Path" (donut), work path; resources 2000/2001/1025/2999.
- photoshop-shape.psb/photoshop-shape-psb.bmp: PSB variant of the solid
  shape.
- photoshop-shape-feather.psd/bmp: three SHAPE layers with vector-mask
  parameters on their own path: feather 4 on a rect touching the canvas
  top-left, feather 8 on a rect with a 6 px inside stroke, density 60% on a
  plain rect. Authored by local-test-fixtures/vector-probe/author-shape-feather.jsx.
  Acceptance (PS 27.9): Photoshop opened Patchy's rewrite
  (test-artifacts/psd_shape_feather_rewritten.psd, dumped by
  `psd_shape_layer_feather_and_density_match_photoshop`) without prompts,
  read feather 4/8 and density 153 back through executeActionGet, and its
  flatten matched the capture within 6/255 at the probe points.
- patchy-compound-group.psd/bmp: PATCHY-written (the document
  `make_compound_group_document` builds;
  `psd_compound_group_writes_continuation_records_and_round_trips` dumps it
  to test-artifacts/psd_compound_group_authored.psd) with five one-group
  shape layers: same-winding donut, opposite-winding donut, inner-first
  donut, outer + hole + island, and a donut plus a united second group. The
  .bmp is Photoshop 2026's flatten of that file: every same-group inner
  contour is a hole. Photoshop also opened it with error dialogs enabled
  without a prompt and resaved the length records byte-identical.
- photoshop-compound-text.psd/bmp: Photoshop's own compound encoding. A
  "B8" Arial 64 px text work path (`textItem.createPath`) made into a
  solid-color content layer: one group per glyph, lead op 1 with +6 field
  2, the counters as 0xFFFF / 0 continuation records. Headless-stale
  composite. Both compound pairs were captured by
  local-test-fixtures/vector-probe/author-compound-group.ps1 (`-Dir` = the
  folder holding the Patchy-written PSD); open-error-mode.ps1 beside it is
  the DialogModes.ERROR open check.
- patchy-open-path-strokes.psd: see [open-path-strokes.md](open-path-strokes.md).
- patchy-gradient-empty-transparency.psd: the empty-`Trns` heal; see
  [ps-compat.md](ps-compat.md).

Optional local probes in `local-test-fixtures/vector-probe/` drive the
`_if_available` tests and skip when absent: `probe-pat-full-combo.psd/bmp`
(pattern angle 30, scale 150%, phase 10,20;
`psd_pattern_params_probe_render_parity_if_available`) and
`probe-fx-sofi-center.psd/bmp` (a color overlay over a centered pattern
stroke).

## Known render divergences

- Native `sn2P` (a four-byte big-endian flag; Align Edges / Action Manager
  `useAlignedRendering`) snaps
  expanded rectilinear, undashed miter-stroke edges to whole pixels, with
  half-pixel ties toward positive infinity. Before expansion it rounds the
  band's width (double the style width for Inside/Outside). The stored width
  and editable path stay unchanged. The renderer reads the owning layer's
  preserved flag, which remains intact across edits and PSD saves. Photoshop
  COM probes at widths 0.25 through 5 pin this rule; rebuilding
  `double-stroke-effects.psd` then preserves both colored effect bands.
  Curved/oblique contours, dashed strokes and non-miter joins retain their
  continuous rasterization; aligned rendering for those cases is uncalibrated.

- A lone closed Intersect group fills its contour but has no native stroke.
  Photoshop's translated-and-restored `intersect-first.psd` capture pins this
  distinction; Add and Xor on the same path retain the outline.

- GdFl: with Photoshop's pixel-corner sampling and whole-pixel ends
  ([gradients.md](gradients.md), "Gradient fill layer geometry") the
  gradient fixture's mean error is 0.29. Residual: Photoshop parametrizes its
  smoothness spline non-uniformly by stop location, so Patchy's uniform
  per-segment catmull can differ by a few /255 with UNEVENLY spaced stops.
- Stroke dashes: arc-length integration differs, so a few dash-edge pixels
  flip (mean ~0.3 on the strokes fixture).
- Inside/outside dash cap sizes are calibrated against Photoshop 27.10: the
  Dotted `{0,2}` preset at width 12 keeps 12 px dots and approximately
  12 px straight-edge gaps with square caps; round dots remain full circles
  on the selected side. `stroke_aligned_*` (tests/core/vector_raster_tests.cpp)
  pins cap size, gaps, curves, compound holes, reversed contours and
  first-subpath subtraction. Photoshop's aligned dash phase/corner spacing
  still differs from Patchy's path-length traversal. Force a native
  rerasterization when comparing imported PSDs: Photoshop's saved pixels can
  hide a rendering fault until a shape edit.

  Inside/outside bands clip a double-width stroke against the fill or its
  complement. Each half-band gets an original-sized cap centered half a stroke
  width from the path; fill clipping selects the side, including holes and
  reversed contours. Doubling cap size too closes square-dot/dash gaps and
  creates oversized clipped round dots. Zero-length on-entries carry their
  path tangent explicitly and emit caps at the first dot too; never synthesize
  epsilon-length segments to orient dots.
- ROTATED pattern fills: the placement mapping is pinned exactly
  (R(angle) @ (p - anchor) / scale), but Photoshop resamples rotated tiles
  with a soft per-cell filter: cell-edge deltas are large, the structure
  matches. `psd_pattern_params_probe_render_parity_if_available` checks
  confident-cell agreement (>= 97%), not pixel means. Patchy's crisper render
  is deliberate.
