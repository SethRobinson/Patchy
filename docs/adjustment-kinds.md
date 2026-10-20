# Photoshop adjustment kinds (#250)

Codec and calibration record for the Photoshop adjustment kinds added under #250, and for the placeholders that stand in for kinds Patchy cannot apply yet. Shared conventions (PS = Photoshop 2026, the COM probe workflow, fixtures) are in [adjustments-calibration.md](adjustments-calibration.md); the probes use a 64x68 image holding a 256-step gray ramp plus a 16-level RGB cube. Unchanged native blocks are written back byte for byte; new kinds append to `AdjustmentKind`.

## Placeholders

Photoshop adjustment kinds Patchy cannot apply yet (`vibA`, `blwh`, `clrL`, `grdm`, `selc`) import as empty pixel layers marked `patchy.adjustment.placeholder_kind` (`photoshop_placeholder_adjustment_kind`): the Layers panel draws the kind's symbol on a muted frame and a tooltip says it is kept but not applied, one import notice covers the file, and the native block is written back byte for byte. A kind leaves this list when it gets a real `AdjustmentKind`; `mixr` and `phfl` blocks in a version their parsers do not read still import as placeholders.

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
