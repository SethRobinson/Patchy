# Native Smart Filters: Image > Adjustments

Calibration record for Image > Adjustments commands stored as native Smart Filters. The shared
descriptor, FEid cache and stack rules are in [smart-filters-native.md](smart-filters-native.md);
the model and UI rules in [smart-objects.md](smart-objects.md); the adjustment math itself in
[adjustments-calibration.md](adjustments-calibration.md) and [adjustment-kinds.md](adjustment-kinds.md).
"PS" is Adobe Photoshop 2026 (27.8), driven over COM ([photoshop-com.md](photoshop-com.md)).

## What Photoshop stores

PS applies Brightness/Contrast, Levels, Curves, Hue/Saturation, Color Balance, Channel Mixer,
Exposure, Posterize, Threshold, Photo Filter, Black & White, Invert and Shadows/Highlights to a
Smart Object as Smart Filters; Desaturate, Equalize and HDR Toning are refused there (October 2026
probe). The entry's `Fltr` object is the applying action descriptor, stored verbatim: the same
form PS's dialogs record in actions (its bundled `Presets/Actions/*.atn` hold examples). A key the
action leaves out renders with the dialog default, and an action with no descriptor gets
`hasoptions` false and no `Fltr`.

| Kind | Fltr class / filterID | Keys (absent = default) |
|---|---|---|
| Levels | `Lvls` / `Lvls` | `presetKind`, `Adjs`: list of `LvlA` {`Chnl` (Enmr Chnl: `Cmps`, `Rd  `, `Grn `, `Bl  `), `Inpt` [lo, hi], `Gmm ` double, `Otpt` [lo, hi]} |
| Curves | `Crvs` / `Crvs` | `presetKind`, `Adjs`: list of `CrvA` {`Chnl`, `Crv `: list of `Pnt ` {`Hrzn`, `Vrtc`} doubles} |
| Hue/Saturation | `HStr` / `HStr` | `presetKind`, `Clrz`, `Adjs`: one `Hst2` {`H   `, `Strt`, `Lght`}: the master sliders, or with `Clrz` the colorize sliders (hue 0..360) |
| Brightness/Contrast | `BrgC` / `BrgC` | `Brgh`, `Cntr`, `useLegacy` (absent: modern) |
| Color Balance | `ClrB` / `ClrB` | `ShdL`, `MdtL`, `HghL` (cyan/red, magenta/green, yellow/blue), `PrsL` |
| Channel Mixer | `ChnM` / `ChnM` | `Mnch`, `Rd  `/`Grn `/`Bl  ` or `Gry `: `ChMx` {`Rd  `, `Grn `, `Bl  `, `Cnst`} |
| Exposure | `Exps` / `Exps` | `Exps`, `Ofst`, `gammaCorrection` doubles |
| Posterize, Threshold | `Pstr`, `Thrs` | `Lvls`; `Lvl ` |
| Photo Filter | stringID `photoFilter` / a run-time number (33 in the capture) | `Clr ` (`LbCl` {`Lmnc`, `A   `, `B   `}), `Dnst`, `PrsL` |
| Black & White | `BanW` / `BanW` | six weights `Rd  `, `Yllw`, `Grn `, `Cyn `, `Bl  `, `Mgnt`, `useTint`, `tintColor` (`RGBC`) |

Entry names follow the menu items (`Levels...`, `Brightness/&Contrast...` with its mnemonic).

## Patchy's model

`SmartFilterKind::Adjustment` holds an `AdjustmentSmartFilter`: the adjustment-layer
`AdjustmentSettings`. `src/psd/psd_smart_filter_adjustments.cpp` reads the table above and fails
closed (the entry stays unmodeled and its stack keeps PS's preview) on any other key: Levels
`Auto` and the other auto flags (PS computes those values), Curves `Mpng` maps, per-hue-range
`Hst2` bands (keys not captured), CMYK channel records, a Photo Filter color outside Lab, values
outside the dialog ranges, and inverted Levels output ranges (Patchy's Levels record keeps output black at or below white). A charID class must carry its own
filterID; a stringID class keeps whatever number PS stored. Invert and Shadows/Highlights belong
to other work; Gradient Map and Color Lookup actions are not modeled.

Rendering runs `apply_adjustment_to_pixels`, the adjustment-layer math, on the straight colors of
the stack's current result: bounds and alpha unchanged, no spatial reads. Like every Smart Filter
it runs at 8 bits; adjustments in CMYK documents still use the RGB math (no ink space).

Writing: an unedited entry re-emits byte for byte; a dirty block whose settings equal the stored
ones keeps the `Fltr` object untouched. An edit rewrites the `Fltr` values in the dialog form above
(channel records only when they differ from identity, the composite record always, `useLegacy`
always), keeping the class, name and `presetKind`. Settings that form cannot carry (an edited
Hue/Saturation band, a CMYK record) refuse the commit ("cannot be edited safely").

## Editing

Double-clicking an adjustment entry (or its Edit pencil) opens that adjustment's own dialog
(`MainWindow::request_adjustment_settings`, shared with adjustment layers) with a live preview of
the re-rendered stack; Apply is one undo step through `commit_smart_filter_stack_edit`. Curves
offers no Auto or canvas sampling there (no layer below to read). The dialogs have no Blending
section, so an entry's imported blend mode and opacity render and save but are not editable yet.
Nothing appends an adjustment entry: Image > Adjustments on a Smart Object keeps its refusal.

## Calibration

Fixture `test-fixtures/psd/photoshop-smart-filter-adjustments.psd` (256 x 32: gray ramp, 16-level
RGB cube, partially transparent color ramp; `scripts/dev/smart-filters/make_adjustment_probe.py`,
`adjustment-jobs.jsx`, `ps-probe-smart-filter-stack.ps1`): one Smart Object duplicate per setting
over an unfiltered `Source`. `smart_filter_adjustments_photoshop_capture_parses_and_renders`
compares Patchy's render of each stack with PS's preview: alpha exactly, straight RGB within the
bounds pinned per layer there.

Results (largest straight-RGB difference; pixels more than one level off of 8,192 visible):

- Exact: Curves, Hue/Saturation colorize, modern Brightness/Contrast, Color Balance without
  Preserve Luminosity, Channel Mixer (both forms), Posterize, Threshold, Black & White.
- Within 1: Levels (master, output, per-channel with preset), legacy Brightness/Contrast (with
  and without the key), Color Balance with Preserve Luminosity, Exposure.
- Hue/Saturation master sliders: within 3 (342 and 379 pixels at 2 or 3), the adjustment layer's
  own residual. Photo Filter: within 5 (35 pixels), its documented Preserve Luminosity clip.
- Stacks: Levels then Curves within 2 (56 pixels; the curve amplifies Levels' one-level
  residual). Gaussian Blur then Hue/Saturation within 3 on opaque pixels and up to 35 at the
  blurred edge, where alpha falls to 8 and straight color has little precision left.

Two rules came out of the capture:

- `useLegacy` absent means legacy (the older recorded form). Read as modern it misses by up to 63.
- Threshold, here and in adjustment layers, compares the 30/59/11 luminance ROUNDED half up, `(30R + 59G + 11B + 50) / 100`,
  in the Smart Filter and the adjustment layer alike (a Threshold adjustment layer probe over
  the same cube agrees: the floored quotient missed 13 colors at level 140, the rounded one
  none). `threshold_luminance` serves both and the destructive Threshold.
