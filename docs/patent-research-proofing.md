# Research record: soft proofing and gamut warning

Record for View > Proof Colors and View > Gamut Warning (GitHub issue 90, design in
[cmyk.md](cmyk.md)). The binding boundary is the "Soft proof and gamut warning" bullet in
[legal-constraints.md](legal-constraints.md). Written October 14, 2026. This is a note on
published earlier work and a keyword search, not a claim-level review.

## What Patchy does

- **Proof Colors** shows the canvas through one fixed color transform: sRGB to the chosen
  CMYK ICC profile by the chosen rendering intent, then back to sRGB relative
  colorimetric. It is Little CMS's `cmsCreateProofingTransform`, applied to the composite
  that is about to be painted.
- **Gamut Warning** replaces every color the profile cannot hold with one flat gray. The
  test is Little CMS's gamut check (`cmsFLAGS_GAMUTCHECK`): a round trip through the
  profile, or the profile's own gamut tag, against a fixed tolerance.
- Both are whole-canvas views chosen by the user from the menu. Document pixels are never
  changed.

## Prior art

- The ICC profile format defines the pieces used here: device-to-PCS and PCS-to-device
  tables per rendering intent, and a gamut tag (`gamt`) whose stated purpose is
  out-of-gamut indication. ICC.1 dates from 1994 and the tag has been in it throughout.
- Little CMS has shipped the proofing transform and the gamut-check alarm color since its
  1.x releases (first public release 1998); the vendored 2.17 core carries the same API
  under MIT. Patchy calls it and adds nothing to the method.
- Showing a print simulation and a flat-color gamut warning over the image in a raster
  editor is decades old. To the author's recollection Photoshop had a Gamut Warning
  command and a CMYK preview by version 3.0 (1994) and the Proof Setup / Proof Colors pair
  by version 6.0 (2000); that dating was not confirmed against a primary source in this
  pass. Either date is more than twenty years back.

## Search

Keyword searches on October 14, 2026 ("soft proofing", "out of gamut", "gamut warning",
display, overlay, image editing) surfaced soft-proofing patents about other things:

- US 7,209,147 B2, "Correction techniques for soft proofing": corrections applied so a
  display matches a hard-copy proof's white point and viewing conditions. Patchy applies
  no such correction; it uses the standard ICC transform only.
- US 7,999,823, projection-device based soft proofing. Not a monitor view.
- EP 2391116, soft proofing of glossy and diffuse components. Not relevant.

Nothing found claims a flat-color out-of-gamut indication or an ICC proofing transform as
such. The claims of the patents above were not read and their legal status was not checked.

## Boundary (why it is drawn where it is)

The feature stays the generic one the prior art describes. Anything that goes beyond it is
a different invention and needs its own check: correcting the view for a measured monitor,
paper white or viewing condition beyond the ICC intents; choosing or adjusting the proof
per image region or from image content; editing pixels to bring them into gamut
automatically; machine-learned gamut mapping.
