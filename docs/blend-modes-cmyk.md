# Native CMYK compositing

Split from [blend-modes.md](blend-modes.md): how supported 8-bit CMYK stacks composite on their four ink channels, what still selects the RGB renderer, and the untagged working-profile fallback.

## Imported CMYK documents

The CPU and canvas renderers composite supported 8-bit CMYK stacks on four
inverted ink channels before converting the result to sRGB. Raster layers use
their retained native planes while their pixels are unchanged. Edited pixels
and regenerated Smart Objects convert through the document's CMYK profile;
native solid/gradient descriptors supply their original ink colors for fresh
vector rendering. Pixel edits invalidate the source-plane reuse and undo
restores it. The conversion/raster preparation cache is bounded and keyed by
layer render revision and source-space identity. A layer at an uncached revision
(the one being painted, or a Move override's pixels) separates only its part
inside the render clip, so a brush dab costs its own rect; the profile's two
Little CMS transforms are built once per source space (`ink_transforms`), never
per render. Building them per render cost 44 ms per dab (issue 220's sample),
in CMYK Proof and RGB mode alike.
`native_cmyk_render_stats` counts transform builds and separated pixels;
`compositor_cmyk_dab_renders_reuse_transforms_and_separate_only_their_rect` holds
both per dab, and `patchy_perf_tests cmykbrush` times strokes on that sample
(`local-test-fixtures/cmyk-brush-perf/ag-psd-cmyk-src.psd`).

Component modes use the existing byte kernels on C/M/Y and K. Hue, Saturation
and Color retain backdrop K; Luminosity takes source K. Darker/Lighter Color
choose one complete four-channel color by comparing `(30*C + 59*M + 11*Y)*K`
on inverted bytes, keeping the backdrop on a tie. The comparison precedes ICC
conversion. Photoshop COM captures of 512 color pairs pin that selection rule;
the committed `cmyk-render/calibration` fixture covers all 26 color modes.
This coupled selection applies to layer and group blends. Non-Normal vector
strokes require a coupled ink bake and currently select the RGB renderer.

Masks, layer Opacity/Fill, isolated groups, faded Pass Through groups and artboards (over
their paper's inks) keep their native-channel composition. Normal-mode Threshold uses native inks too
([adjustment calibration](adjustments-calibration.md)). Other adjustments, layer effects, clipping
runs, knockout and rendered Blend If/channel restrictions currently select the
existing RGB renderer for the whole stack. Noise/pattern fills and compound
vector paints also use that fallback. Deep CMYK and Dynamic Vector Preview
remain on that renderer. Untagged CMYK editing pixels use the same installed
working-profile fallback as the native composite.
Flat imports without native layer records keep their already-converted RGB
preview; sending it back through the CMYK gamut would introduce avoidable error.
Native save eligibility is separate: [preservation rules](psd-native-color.md).

For untagged CMYK, Patchy uses U.S. Web Coated (SWOP) v2 if that profile is already
installed in a conventional Adobe/system profile directory. No Adobe profile
is bundled or downloaded. Without it the existing mathematical conversion is
the fallback. Embedded profiles take precedence; an assumed profile is not
silently embedded into the original PSD. Untagged color therefore depends on
installed profiles, and may differ from another application's chosen working
space. Stored Smart Object previews in an untagged file may reflect an unknown
earlier working profile; even Photoshop can change them when rebuilding the
embedded contents. Do not infer that profile from the stored preview colors.
`cmyk-render/smart-gradient` pins fresh Smart Object and ink-gradient
rendering; the core tests cover partial bounds, cache invalidation and Unicode
profile paths.
