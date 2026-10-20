# Photoshop adjustment kinds (#250)

Codec and calibration record for the Photoshop adjustment kinds added under #250, and for the placeholders that stand in for kinds Patchy cannot apply yet. Shared conventions (PS = Photoshop 2026, the COM probe workflow, fixtures) are in [adjustments-calibration.md](adjustments-calibration.md); the probes use a 64x68 image holding a 256-step gray ramp plus a 16-level RGB cube. Unchanged native blocks are written back byte for byte; new kinds append to `AdjustmentKind`.

## Placeholders

Photoshop adjustment kinds Patchy cannot apply yet (`vibA`, `clrL`, `selc`) import as empty pixel layers marked `patchy.adjustment.placeholder_kind` (`photoshop_placeholder_adjustment_kind`): the Layers panel draws the kind's symbol on a muted frame and a tooltip says it is kept but not applied, one import notice covers the file, and the native block is written back byte for byte. A kind leaves this list when it gets a real `AdjustmentKind`; `mixr`, `phfl`, `grdm` and `blwh` blocks their parsers do not read still import as placeholders.

## Channel Mixer

`AdjustmentKind::ChannelMixer`, Photoshop's `mixr` block (44 bytes: u16 version 1, u16 monochrome, then four rows of int16 red, green, blue, unused, constant for the red, green and blue outputs and a fourth row Photoshop leaves zero; in monochrome the gray mix sits in the first row). An unedited block is written back byte for byte; an edited one drops the preset-name `CgEd` block. Fitted on a Photoshop 2026 COM probe (#250): the gray ramp plus RGB cube through 9 settings (layout markers, a channel swap, mixed and negative weights, constants, +-200 extremes, two monochrome mixes, 33/34% thirds). The model matches every probe value.

- Each percentage, weights and constant alike, becomes `percent * 1024 / 100` truncated toward zero (`channel_mixer_weight`: 20% is 204, -71% is -727). An output channel is `(R*Wr + G*Wg + B*Wb + 255*Wc) / 1024`, rounded half up and clamped. Monochrome sends the gray row to all three channels.
- Refuted: exact percentages with any rounding (hundreds to thousands of off-by-one values per setting: a 20% weight takes 153 to 30, not 31), weights floored instead of truncated (negative weights miss), and the constant as `round(c * 2.55)` or on a 1/256 scale.
- Outputs mix channels, so there is no per-channel LUT; 16/32-bit documents use the same quantized weights without the rounding. Grayscale and CMYK documents keep this RGB math, as Hue/Saturation and Color Balance do: Photoshop's CMYK form (four ink rows) is not modeled.

## Photo Filter

`AdjustmentKind::PhotoFilter`, Photoshop's `phfl` block: version 2, 20 bytes (u16 version, u16 color space 7 = Lab, u16 L in hundredths, int16 a and b in hundredths, u16 0, u32 density percent 1..100, u8 Preserve Luminosity, 3 pad bytes). Patchy keeps the color as that Lab (`PhotoFilterAdjustment::lab_*`), so an unedited block is written back byte for byte; other versions and color spaces stay placeholders. A swatch pick converts sRGB to Lab with the matrices below and matches the Lab Photoshop stores for the probe colors to within 2 hundredths. Fitted on a Photoshop 2026 COM probe (#250): the gray ramp plus RGB cube through 9 settings (warming, cooling, pure red, an odd green, gray and magenta filters; densities 1, 25, 50, 63, 100; both Preserve Luminosity states).

- The filter color is Lab (D50) converted to sRGB bytes. Source and filter go to CIE XYZ (D50) through the Bradford-adapted sRGB matrix; each component becomes `source * (1 - d + d * filter / white)`; the result converts back, is clipped to the sRGB gamut, and rounds half up. Within 1 level of Photoshop on every probe value (about 1 % of values differ by 1).
- Preserve Luminosity: PDF SetLum on that rounded result, with luma `(30R + 59G + 11B)/100` rounded to a whole level for both the source and the filtered color, then ClipColor. Exact on grays and most colors, but Photoshop clips bright colors differently: on the probe 89 % of values match, the rest are within 7 levels, almost all where a channel exceeds 255 after the shift. That clip is unresolved (a single clip center does not explain it).
- Refuted: a multiply or normal blend in encoded or linear RGB (dozens to hundreds of levels off on saturated colors: pure red through a red filter at 100 % gives (194, 0, 10)), Preserve Luminosity as an XYZ Y restore or a Lab L restore, unclamped values carried into SetLum, and Rec. 709 luma weights.
- Outputs mix channels, so there is no LUT; 16/32-bit documents run the same math without the byte stages. Grayscale and CMYK documents keep the RGB math.

## Gradient Map

`AdjustmentKind::GradientMap`, Photoshop's `grdm` block: u16 version (1, or 3 with a 4-byte method key `Gcls`/`Lnr `/`Perc` after the reverse and dither bytes), Unicode name with its trailing null, color stops (u32 location 0..4096, u32 midpoint 0..100, u16 color space, four u16 components, u16 pad), opacity stops (u32, u32, u16 0..255), then u16 2, u16 smoothness 0..4096 and 36 bytes of noise fields, padded to four bytes. Only RGB solid stops are read; other color spaces stay placeholders. The settings are a `LayerStyleGradient` (metadata `patchy.adjustment.gradient_map.*` in Photoshop's integer units); an unedited block is written back byte for byte, and an edit regenerates version 3 with the original's noise fields, which reproduces Photoshop's bytes exactly (pinned). Fitted on a Photoshop 2026 COM probe (#250): 12 maps (black to white and red to blue in all three methods, smoothness 0 and 100 %, reverse, three stops with midpoints, dither, the default method).

- Each pixel's luma, `round((30R + 59G + 11B) / 100)`, indexes the gradient (a 256-entry table per gradient, cached per thread for the compositor). Photoshop's own luma differs by one level on 65 of the 4,352 probe colors, such as (0, 255, 255); no fixed-point form explains it.
- Classic maps follow `gradient_color` with the two-stop endpoint ease of gradient fills. Perceptual and Linear apply the same smoothness ease to the segment position first (`ease_every_method`; fills and layer effects do not), then interpolate in OKLab or linear light. Every probe value is within one level. The ease is verified on two-stop gradients only.
- Opacity stops are kept for the file and ignored, as in Photoshop; Dither is kept but not rendered. 16/32-bit documents use `gradient_color_precise` on the continuous luma. Photoshop's localization-key names (`$$$/DefaultGradient/...=Name`) show only the display part in the Layers panel.
- Refuted: Linear and Perceptual without the ease (15 to 17 levels off at mid grays).

## Black & White

`AdjustmentKind::BlackWhite`, Photoshop's `blwh` block: u32 descriptor version 16 and a descriptor of six weight longs (`Rd  `, `Yllw`, `Grn `, `Cyn `, `Bl  `, `Mgnt`, -200..300), `useTint`, `tintColor` (an `RGBC` object of doubles; Photoshop stores 40.0000027 for 40), `bwPresetKind` and an empty `blackAndWhitePresetFileName`. An unedited block is written back byte for byte; an edit rewrites the modeled values in the original descriptor, and a fresh layer starts from Photoshop 2026's own block (pinned byte for byte). Fitted on a Photoshop 2026 COM probe (#250): 10 settings (defaults, zeros, all 100, single colors, mixed negative weights, extremes, three tints).

- Gray: the smallest channel plus (middle - smallest) times the weight of the secondary color the two largest channels form (yellow, cyan or magenta) plus (largest - middle) times the weight of the largest channel's primary, in hundredths, rounded half up (integer arithmetic on the 8-bit path). Exact on every probe value except one mixed negative-weight set (49 of 4,352 pixels off by one level); no rounding or weight quantization explains those.
- Tint: PDF SetLum of the tint color to the gray, luma `(30R + 59G + 11B) / 100` rounded to a whole level, then ClipColor. Within one level everywhere (about a third of the tinted values differ by one).
- Channels mix, so there is no LUT; 16/32-bit documents run the same math without the rounding. Patchy offers no Auto weights (docs/legal-constraints.md).
