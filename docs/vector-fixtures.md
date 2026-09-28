# Vector PSD fixtures

The self-authored Photoshop fixtures the vector codec and renderer tests read. Split out of [vector-tools.md](vector-tools.md) (September 2026) to keep that file under the size limit.

## Inventory (test-fixtures/psd, self-authored via COM, July 2026)

Each .psd has a sibling .bmp, Photoshop's own flatten (24-bit, white
background layer), for render-parity tests; the embedded composites are
headless-stale (ps-compat.md).

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
- photoshop-vector-mask-feather.psd/bmp: vector feather 4 (path on the
  canvas corner) and 8.
- photoshop-user-mask-params.psd/bmp: raster-mask feather 3 + density 50%,
  feather 6.5, density 25%.
- photoshop-saved-paths.psd/bmp: "Alpha Path" (rect, clipping path), "Beta
  Path" (donut), work path; resources 2000/2001/1025/2999.
- photoshop-shape.psb/photoshop-shape-psb.bmp: PSB variant of the solid
  shape.

## Known render divergences (July 2026)

- GdFl with UNEVENLY spaced stops: PS parametrizes its smoothness spline
  non-uniformly by stop location; Patchy's uniform per-segment catmull
  differs by a few /255 there (gradient fixture: mean 1.2, max 8).
- Stroke dashes: arc-length integration differs, so a few dash-edge pixels
  flip (mean ~0.3 on the strokes fixture).
- ROTATED pattern fills: the placement mapping is pinned exactly
  (R(angle) @ (p - anchor) / scale), but PS resamples rotated tiles with a
  soft per-cell filter: cell-edge deltas are large, the structure matches; psd_pattern_params_probe_render_parity_if_available
  checks confident-cell agreement (>= 97%), not pixel means. Patchy's
  crisper render is deliberate.

- photoshop-shape-feather.psd/bmp (September 2026): three SHAPE layers with
  vector-mask parameters on their own path: feather 4 on a rect touching the
  canvas top-left, feather 8 on a rect with a 6 px inside stroke, density 60%
  on a plain rect. Authored by local-test-fixtures/vector-probe/author-shape-feather.jsx.
  Acceptance (2026-09-21): PS 27.9 opened Patchy's rewrite
  (test-artifacts/psd_shape_feather_rewritten.psd, dumped by
  psd_shape_layer_feather_and_density_match_photoshop) without prompts, read
  feather 4/8 and density 153 back through executeActionGet, and its flatten
  matched the capture within 6/255 at the probe points.
- patchy-compound-group.psd/bmp (September 2026): PATCHY-written (the
  document `make_compound_group_document` builds in
  psd_vector_fixtures_tests.cpp; `psd_compound_group_writes_continuation_records_and_round_trips`
  dumps it to test-artifacts/psd_compound_group_authored.psd) with five
  one-group shape layers: same-winding donut, opposite-winding donut,
  inner-first donut, outer + hole + island, and a donut plus a united second
  group. The .bmp is PS 2026's flatten of that file (COM, 2026-09-26): every
  same-group inner contour is a hole. PS also opened it with error dialogs
  enabled without a prompt and resaved the length records byte-identical.
- photoshop-compound-text.psd/bmp (September 2026): PS's own compound
  encoding. A "B8" Arial 64 px text work path (`textItem.createPath`) made
  into a solid-color content layer: one group per glyph, lead op 1 with +6
  field 2, the counters as 0xFFFF / 0 continuation records. Headless-stale
  composite (ps-compat.md). Both pairs were captured by
  local-test-fixtures/vector-probe/author-compound-group.ps1 (`-Dir` = the
  folder holding the Patchy-written PSD); open-error-mode.ps1 beside it is
  the DialogModes.ERROR open check.
