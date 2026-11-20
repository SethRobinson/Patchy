# Preserving imported PSD color spaces

Patchy edits in RGB. Layered PSD/PSB saves can retain an imported 8/16-bit
CMYK, grayscale, duotone or Lab document's original mode, ICC profile, color planes
and native adjustment/gradient blocks. This preserves the color space in
which Photoshop evaluates the layers. Converting only raster samples to
RGB cannot preserve a CMYK Posterize operation or a Lab gradient's
interpolation.

## Eligibility and invalidation

The reader stores each layer's decoded big-endian color planes in
`PsdNativeLayerColors`, separately from its RGB editing pixels. After all
import finalizers and depth conversion, it seals the layer content revision
and retains a shallow layer snapshot associated with one shared
`PsdNativeColorSpace`. Copies and undo snapshots share the immutable data.
No private PSD tags are emitted.

A layered save retains the source mode only when every layer still has
that document's source identity and unchanged color content. A changed
revision triggers comparison with the imported snapshot, so mutable reads
cannot disable preservation. Raster pixels, metadata, native blocks and
vector content must match. Unchanged Smart Objects also retain their native
preview planes, provided their placement and document-level source still match
the import snapshot. In CMYK documents, source replacement, placement edits and
regenerated Smart Object previews instead convert the current RGB pixels back
through the document profile, at its depth. Native Smart Filter caches retain
their imported ink-space identity; rebuilt sRGB caches convert their four color
planes independently, leaving alpha, auxiliary slots and mask tails intact.
Matching imported caches remain byte-identical. Unknown cache layouts and caches
from another native profile cannot be converted. Regenerated vector blocks and
text are excluded. Styles need their preserved Photoshop blocks.
Changing the embedded profile, depth, or source ICC resource disables reuse.
A layer pasted from another document cannot inherit the destination's inks.

A flat 8/16-bit CMYK file (no layer records, the merged image only: Photoshop's save of a
one-layer document) keeps its merged ink planes on the Background layer it opens as, marked
`PsdNativeLayerColors::flat_composite`, so it saves CMYK while unchanged (GitHub issue 220).
Its preview stays the reader's conversion: `render_native_cmyk8` never rebuilds a lone flat
layer from those planes. Flat gray, Lab and duotone files still save RGB.
`DocumentIo::layered_save_color_mode` answers ahead of a save which mode the writer will
write (`layered_save_keeps_source_color_mode`: not RGB); both come from the writer's own
`layered_save_color_plan`.

Color/content edits conservatively fall back to the existing RGB writer
for the entire document, except in CMYK Proof: there a CMYK file keeps each unchanged
layer's planes (`native_layer_unchanged`, per record) and separates the edited ones through
its own profile ([cmyk.md](cmyk.md), "CMYK Proof PSD saves"). Renaming, visibility, opacity, raster masks, reordering,
deletion and raster translations can keep the native samples. Undo restores
eligibility with the original content.
This is preservation of imported content, not native CMYK/Lab editing.
Import notes and the compatibility report explain that content edits may
require RGB saving and change gradients or adjustments.

`WriteOptions::preserve_source_color_mode = false` explicitly selects RGB.
Flat exports, 32-bit documents, other color modes, reads with discarded
unknown blocks, and documents without eligible layer records (flat CMYK files
excepted, above) use the existing RGB path. The historical `write_layered_rgb8` name also handles
these native saves, as it already handles deep RGB saves.

## Native Lab adjustments

An imported 8/16-bit Lab file with an unchanged, opaque, full-canvas raster and normal Invert, Posterize, Levels, Curves, Threshold or Brightness/Contrast adjustment layers can render directly from its L*, a* and b* planes. The compositor applies the adjustment in Lab before converting to sRGB. Opacity, fill and adjustment masks mix the native results by coverage. Edited base pixels, unsupported blends, effects, transparency in the base, and mixed adjustment stacks select the RGB renderer. A partial repaint visits only its rectangle.

Photoshop 28 Lab TIFF exports verify all three native channels for Invert and Posterize at 2, 3, 4, 7 and 255 levels. Invert complements a stored sample. At 8 bits Posterize uses the ordinary byte buckets. At 16 bits it computes a bucket on Photoshop's 0..32768 scale and expands its output to the stored 16-bit range. The fresh exports agree exactly at 8 bits and within one stored level at 16 bits. For these stacks a Lab-mode PSD/PSB save writes the freshly calculated native merged planes. Editing the level count of an imported Lab Posterize adjustment updates the original record and preserves the remaining bytes and source planes.

Lab Levels and point Curves use slots 1, 2 and 3 for L*, a* and b*. Photoshop ignores slot 0. Independent Photoshop 28 TIFF exports verify all four slot assignments at both depths, including a nonidentity master as the only edit. The component lookup tables and continuous deep transfer functions act on the original stored samples. On the measured Lightness curves, the maximum channel error was zero at 8 bits and 5/65535 at 16 bits. For Levels it was 1/255 and 11/65535. An unchanged Levels or Curves record survives byte for byte. Editing Levels replaces the four modeled records and retains the tail. Editing Curves writes native control points for the affected channels.

Threshold compares L* to its saved level and supplies neutral a* and b*. Full Photoshop Lightness ramps at levels 1, 64, 128 and 254 place the first white sample at the corresponding byte. At 16 bits the first white samples are 257, 16447, 32895 and 65277. Legacy and modern Brightness/Contrast use the saved `brit` Lab byte or `CgEd` `Lab ` boolean. A true flag adjusts Lightness only. A cleared flag applies the same curve to all three components. Classic files can omit the entire descriptor. Photoshop warns if the modern descriptor omits its Lab field. Their native save keeps the flag and the original descriptor fields when editing the sliders in the imported algorithm. Switching between legacy and modern uses the RGB save path.

## Wire output and verification

The normal writer rebuilds layer records, masks, names, hierarchy, resources
and the PSD/PSB container. Native color planes replace only the encoded
color channels. Unchanged adjustments keep their original blocks, including
source channel records and Gradient Map stop values. An RGB save projects native Gradient Map stops through their source profile. Fills keep their native descriptors. The merged
preview is freshly composited by Patchy and converted back through a
sRGB-to-source ICC transform at the document depth. It never uses the source merged image.
Unchanged layer data does not pass through that inverse transform.
For untagged CMYK, this inverse uses the same installed working-profile fallback
as display conversion, without embedding that assumed profile in the save.

Regression tests cover exact source samples and alpha, PSD/PSB, profiles,
native gradient/adjustment blocks, edited-pixel fallback, cross-document
provenance and undo. Photoshop acceptance and precision are measured from
its fresh flatten of each saved document. Supported 8-bit CMYK stacks now blend
retained or regenerated ink channels before display conversion, including native
gradient stops; limitations and the untagged working-profile fallback are in
[blend-modes-cmyk.md](blend-modes-cmyk.md). Other non-RGB approximations are described in
[high-bit-depth-formats.md](high-bit-depth-formats.md#phase-2-psdpsb-at-depth) and
[adjustments-calibration.md](adjustments-calibration.md).

Testy's 20261009-231617 corpus: all 12 scored 16-bit PSD saves match Photoshop's
reference pixels exactly (100% precision). The six CMYK/gray/Lab cases also
match exactly as PSB. All 18 open/render checks use Photoshop's error-enabled
dialog mode. This result covers save fidelity of the imported content, not
native non-RGB editing or the RGB preview's rendering accuracy.

## Duotone

Eight-bit duotone imports retain the ink definition in color-mode data and the
original gray layer planes. Resource 1066 supplies a bounded 256-entry Lab lookup;
it converts to sRGB once on import. Raster editing previews and gray descriptor
colors use that same lookup. Native fill provenance keeps the gray stops, so the
CPU and UI compose supported untouched stacks in gray and apply the lookup after
composition. Revision-keyed caches keep gray reconstruction out of repaint loops.
PSD/PSB saves preserve the ink definition, original layer channels and native fill
blocks; the merged channel comes from fresh gray composition, not an inverse
conversion of the colored display image.

Edited colors, patterns, adjustments, Smart Objects and styled layers currently
use the RGB preview path. A missing or malformed lookup also keeps the historical
RGB fallback. Photoshop can recompute a stale lookup from its current working ink
profiles on open, so an old file's embedded table can differ from its fresh
Photoshop display even while its native save preserves the inks exactly.
