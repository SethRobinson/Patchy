# Preserving imported PSD color spaces

Patchy edits in RGB. Layered PSD/PSB saves can retain an imported 8/16-bit
CMYK, grayscale or Lab document's original mode, ICC profile, color planes
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
vector content must match. Regenerated vector blocks, text and smart
objects are excluded. Styles need their preserved Photoshop blocks.
Changing the embedded profile, depth, or source ICC resource disables reuse.
A layer pasted from another document cannot inherit the destination's inks.

Color/content edits conservatively fall back to the existing RGB writer
for the entire document. Renaming, visibility, opacity, raster masks, reordering,
deletion and raster translations can keep the native samples. Undo restores
eligibility with the original content.
This is preservation of imported content, not native CMYK/Lab editing.
Import notes and the compatibility report explain that content edits may
require RGB saving and change gradients or adjustments.

`WriteOptions::preserve_source_color_mode = false` explicitly selects RGB.
Flat exports, 32-bit documents, other color modes, reads with discarded
unknown blocks, and documents without eligible layer records use the
existing RGB path. The historical `write_layered_rgb8` name also handles
these native saves, as it already handles deep RGB saves.

## Wire output and verification

The normal writer rebuilds layer records, masks, names, hierarchy, resources
and the PSD/PSB container. Native color planes replace only the encoded
color channels. Unchanged adjustments keep their original blocks, including
source channel records; fills keep their native descriptors. The merged
preview is freshly composited by Patchy and converted back through a
sRGB-to-source ICC transform at the document depth. It never uses the source merged image.
Layer data does not pass through that inverse transform.

Regression tests cover exact source samples and alpha, PSD/PSB, profiles,
native gradient/adjustment blocks, edited-pixel fallback, cross-document
provenance and undo. Photoshop acceptance and precision are measured from
its fresh flatten of each saved document. Patchy's RGB preview still has
the non-RGB rendering approximations described in
[high-bit-depth.md](high-bit-depth.md) and
[adjustments-calibration.md](adjustments-calibration.md).

Testy's 20261009-231617 corpus: all 12 scored 16-bit PSD saves match Photoshop's
reference pixels exactly (100% precision). The six CMYK/gray/Lab cases also
match exactly as PSB. All 18 open/render checks use Photoshop's error-enabled
dialog mode. This result covers save fidelity of the imported content, not
native non-RGB editing or the RGB preview's rendering accuracy.
