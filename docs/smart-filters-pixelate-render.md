# Native Smart Filters: Pixelate, Render, Noise and Other

Calibration record for the table-described native Smart Filters. The shared descriptor, FEid cache
and stack rules are in [smart-filters-native.md](smart-filters-native.md); the model and UI rules in
[smart-objects.md](smart-objects.md). "PS" is Adobe Photoshop 2026 (27.8), driven over COM
([photoshop-com.md](photoshop-com.md)). Every rule below is pinned by PS captures (October 2026).

## Table-described filters

`src/core/smart_filter_table.{hpp,cpp}` holds one `SmartFilterTableSpec` per filter: the entry
name, the `Fltr` class id and form, `filterID`, and the descriptor fields in PS's key order (type,
unit, range, dialog control). Settings are a `TableSmartFilter` (one value per field). The table
drives everything except the pixels:

- PSD read (`parse_table_smart_filter`, src/psd/psd_smart_filter_table.cpp): every field must be
  present (or `optional`) with its type and in range, or the entry stays unmodeled and
  preview-locks its stack. Extra keys are ignored on read and preserved on write.
- PSD write: a new entry is authored in table order (byte-identical to PS's `Fltr` for the same
  values); an imported entry is patched leaf by leaf, so unedited descriptors re-emit byte for byte
  and edited ones keep unmodeled keys, id forms and order. An `optional` key PS omitted is written
  only once its value leaves the default.
- A filter without settings has `class_id == nullptr`: PS writes NO `Fltr` object for it
  (`hasoptions` false) and identifies it by `filterID` alone.
- Settings dialog and layer-row tooltip (src/ui/smart_filter_table_ui.cpp): one control per field
  with a `parameter_key`; a field without one keeps its value. Undo and status text use the
  generic "%1" forms.
- Render: `render_table_smart_filter` (src/filters/smart_filter_table_render.cpp) gets the stack's
  current result embedded in the document-space FEid canvas (the domain for wrap, repeat and
  edge clamping, also when the Smart Object is smaller than the document) and returns those
  bounds; the stack alpha-trims.

These filters exist only as imported (or duplicated) entries: they have no Filter-menu action, so
`native_smart_filter_kind_supported` makes them editable but nothing appends them.

Adding a filter: a `SmartFilterKind` value (plus its `native_smart_filter_kind_supported` case), a
table row, a kernel branch, a capture layer in the fixture, and catalog strings.

## Transparent colors

Filters that read neighbors (Minimum, Custom) see alpha-0 pixels with the straight RGB of the
nearest visible pixel by MANHATTAN distance; ties prefer the most vertical source (smallest |dx|),
then the source below, then the one to the right. This is not Median's rule
([smart-filters-native.md](smart-filters-native.md)): Euclidean and vertical-then-horizontal
variants each left 69 to 324 mismatched pixels on the holed probe, this one none.

## Calibrated filters (byte-exact on the captures)

- **Offset** `Ofst`/0x4f667374, entry `Offset...`: integers `Hrzn`, `Vrtc`, enum `Fl  `
  (`FlMd`: `Bckg` transparent, `Rpt ` repeat edge pixels, `Wrp ` wrap around). Output pixel (x, y)
  takes source (x - Hrzn, y - Vrtc) over the document canvas; wrap is modulo the canvas (300/-250
  on a 128x96 canvas wraps), repeat clamps to the canvas edge. Range not read off PS's dialog;
  imports take +/-300000.
- **Maximum** `Mxm `/0x4d786d20 and **Minimum** `Mnm `/0x4d6e6d20: unit-double `Rds ` (#Pxl) and an
  OPTIONAL `preserveShape` enum (stringID key and type). PS writes Squareness as the stringID
  `squareness`, Roundness as the charID `Rndn`, and omits the key when a script passes none
  (renders as Squareness). Squareness is a (2n+1)^2 window, n = round-half-up(Rds) with a
  minimum of 1 (0.1, 0.2 and 1.49 render as 1; 2.3 as 2; 2.6 as 3; 600 renders), clipped at the
  canvas. RGB takes the extreme of the extended straight colors; alpha takes the OPPOSITE extreme
  (Maximum shrinks the visible area, Minimum grows it). Roundness anti-aliases its window (new
  intermediate values), matches no disc, and stays unmodeled: its stack keeps PS's preview. PS
  stores out-of-range radii verbatim (and renders them); imports and the dialog take 0.1..1000
  (PS's own dialog range was not read).
- **Custom** `Cstm`/0x4373746d, entry `Custom...`: `Mtrx` list of 25 integers (row-major 5x5,
  -999..999), integers `Scl ` (1..9999) and `Ofst` (-9999..9999). Over the extended straight RGB,
  edge-clamped: `v = ((sum + floor(Scl / 2)) * floor(32768 / Scl)) >> 15` (arithmetic shift) plus
  `Ofst`, clamped; alpha unchanged. The 15-bit reciprocal was fit on six kernels (scales 1, 3, 4,
  7, 35, 193); plain rounding misses up to 15263 pixels, a 16-bit reciprocal 7138. The dialog edits Scale and Offset; the
  matrix has no control yet and keeps its values.
- **Fragment** 0x4672676d, entry `Fragment`, no `Fltr`: four copies offset by (+/-4, +/-4),
  edge-clamped, averaged in byte-rounded premultiplied space: `p = round(c * a / 255)`, average
  the four `p` and `a` with round-half-up, unpremultiply `round(p * 255 / a)`.

## Randomized filters (Patchy's own pixels)

Crystallize `Crst`/0x43727374 and Pointillize `Pntl`/0x506e746c (entries `Crystallize...`,
`Pointillize...`, one integer `ClSz`, 3..300) and Mezzotint `Mztn`/0x4d7a746e (`Mezzotint...`, enum
`MztT`: `FnDt`, `MdmD`, `GrnD`, `CrsD`, `ShrL`, `MdmL`, `LngL`, `ShSt`, `MdmS`, `LngS`, confirmed
through PS's stringID table: fineDots ... longStrokes) store no seed, and PS draws new random cells
or screens on every application: two identical captures differ by about 41 (Crystallize 10) and
102 (Mezzotint fine dots) per visible channel sample. No render can reproduce PS's preview, so Patchy renders its own
deterministic version in the same style (integer math, splitmix64, fixed salts, cells anchored at
the canvas origin), like Add Noise:

- Crystallize: Voronoi cells of one jittered seed per `ClSz` grid cell, each filled with the
  (Manhattan-extended) color under its seed. PS's cells likewise take one source pixel's color,
  never an average. Alpha unchanged.
- Pointillize: the entry's background color (`BckC`, as PS) under anti-aliased dots, one per
  jittered cell, radius 0.55 to 0.8 cells, each the color under its center. Alpha unchanged.
- Mezzotint: each RGB channel becomes 0 or 255 against an independent random threshold screen:
  dots at grain 1, 2 or 3 px (grainy: fine noise pulled toward 3 px clumps), lines as horizontal
  runs of 4, 8 or 16 px, strokes as 2 px tall runs of 8, 16 or 32 px. Alpha unchanged.

`smart_filter_table_random_filters_round_trip_and_render_in_style` pins the descriptors
(`photoshop-smart-filter-random.psd`), determinism, alpha, and Patchy's mean difference from PS's
render: within 1.25 times PS's difference from itself (Crystallize 42 against PS's 41).

## Captured but not modeled yet

Descriptor facts from the captures; these entries stay preview-locked:

- Facet `Fct `, Despeckle `Dspc`, Clouds `Clds`, Difference Clouds `DfrC`: no `Fltr`, `hasoptions`
  false. Clouds re-rendered identically in two documents (deterministic in PS; uses `FrgC`/`BckC`).
- Color Halftone `ClrH`: integers `Rds ` then `Ang1`..`Ang4`; anti-aliased dots on a grid not
  yet derived. Fibers `Fbrs`: integers `Vrnc`, `Strg`; no seed stored and two captures differ.
  Lens Flare `LnsF`: integer `Brgh`, `FlrC` point object (`Hrzn`, `Vrtc` #Pxl), enum `Lns `
  (`Zm  ` zoom, `Nkn ` 35 mm, `Nkn1` 105 mm, `PnVs` movie prime).
- HSB/HSL: event and class `HsbP`/0x48736250, entry `HSB/HSL`, enums `Inpt` and `Otpt` (`ClrS`:
  `RGBC`, `HSBl`, `HSLC`). RGB to HSB: alpha unchanged, B = max, H = hue * 256 / 360 (blue is
  171) but a fixed-point step not yet found rounds 6 of 10281 hue samples one lower near .5; S
  is not plain rounding either.

## Fixture and probes

`test-fixtures/psd/photoshop-smart-filter-other.psd`: one Smart Object ("Source", unfiltered) and
thirteen duplicates sharing its embedded source, each with one capture's stack (named after its
settings, including "Maximum 4 roundness" for the lock and "Fragment then Minimum 2" for a
two-entry stack). `smart_filter_table_*` tests render each stack from "Source" and compare with
PS's stored layer pixels (alpha everywhere, RGB where visible), round-trip and patch the SoLd,
and compare authored `Fltr` bytes with PS's.

`scripts/dev/smart-filters/`: `make_probe_source.py` (128x96 probe; `v2` adds canvas-edge contact,
a transparent disc and stripe, and an alpha ramp), `ps-probe-filters.ps1 -Source -Jobs -OutDir`
(one PSD and composite PNG per job; `-CanvasWidth/-CanvasHeight` places the source in a larger
canvas; `-Combined NAME` builds the duplicate-layer fixture), `dump_filterfx.py` (prints
`filterFX`), `read_feid_cache.py` (the unfiltered FEid cache as PNG, hidden RGB included). Wrap
each COM run in the review desk's Photoshop lock.
