Self-authored Adobe Photoshop 2026 COM captures, October 10, 2026.

gamma22.psd uses Gray Gamma 2.2; dot20.psd uses Dot Gain 20%.
Both are 64 x 48 grayscale documents with an embedded 64 x 48 RGBA PNG
placed at 72 PPI without scaling. The source has eight 8-pixel-wide stripes:
red, green, blue, yellow, cyan, magenta, RGB(80,120,200), RGB(128,128,128).
The three 16-pixel-high rows have alpha 255, 128 and 0 respectively.
The embedded source stays colored while the parent's raster is gray.

Each reference PNG is Photoshop's duplicate, flattened onto white, converted
to sRGB with relative colorimetric intent, black point compensation enabled
and dithering disabled. The tests compare regenerated Smart Object previews
with the imported layer pixels within 3/255 for ICC-engine rounding. Opaque
patches also match Photoshop's reference PNG within 3/255. Fractional-alpha
blending in native gray space remains separate from this source conversion.
The unchanged embedded PNG bytes and alpha behavior are tested separately.
These files contain only code-generated color patches, no third-party artwork.
