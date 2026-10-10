Self-authored Photoshop 2026 COM calibration fixtures.

calibration.psd: 8 x 26, one raster backdrop/source pair per row. Rows are
Normal, Multiply, Screen, Overlay, Darken, Lighten, Color Dodge, Color Burn,
Hard Light, Soft Light, Difference, Exclusion, Hue, Saturation, Color,
Luminosity, Subtract, Linear Dodge, Linear Burn, Vivid Light, Linear Light,
Pin Light, Hard Mix, Divide, Darker Color and Lighter Color.
Inverted CMYK source bytes, left to right:
255,0,20,255; 50,100,150,200; 128,128,128,128; 0,0,0,0;
255,255,255,255; 20,220,100,60; 70,40,90,240; 254,1,128,190.
Backdrop bytes:
10,250,200,150; 180,20,230,30; 70,200,30,220; 255,255,255,255;
0,0,0,0; 190,50,90,160; 240,240,240,5; 1,254,128,80.
calibration.bmp is Photoshop's fresh flatten converted to sRGB with relative
colorimetric intent, black-point compensation and no dither, then encoded as BMP.

smart-gradient.psd: 64 x 48, a code-generated transparent PNG placed at 72 PPI
over a horizontal Classic gradient. Native CMYK stops (percent inks) are
15,85,30,10 and 80,5,60,35; smoothness 4096, midpoint 50, dither off.
The placed layer uses Difference. Its eight 8-pixel stripes are RGB primaries,
yellow, cyan, magenta, (80,120,200), and (128,128,128). Three 16-pixel rows
have alpha 255, 128 and 0. smart-gradient.png is the same fresh Photoshop
flatten/profile conversion as above.

Both PSDs embed the working CMYK profile used for their creation, so the tests
do not depend on the host's installed profiles. These documents contain only
self-authored pixels and geometry. They are regression data, not runtime assets.
