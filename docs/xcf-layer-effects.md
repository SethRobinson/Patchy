# GIMP .xcf layer effects

Split from [xcf.md](xcf.md): GIMP 3 layer filters (GEGL and `gimp:` operations) as Drop Shadow styles, pixels filtered by fitted formulas or by Patchy's own GEGL, and the effects left out. The [clean-room rule](xcf.md#clean-room-rule) there applies here.

## Layer effects (GIMP 3 filters)

- `gegl:dropshadow` becomes a Drop Shadow style (#507): GEGL's x/y offset as distance and
  angle, size three times its radius plus the grow radius (spread from the grow share, fitted
  on effects.xcf), its color in any GeglColor model (`gegl_color_to_srgb`: RGB and gray in
  any curve, HSV, HSL, CIE Lab and LCH; an embedded profile is read as sRGB), and its opacity
  times the color's alpha and the filter's opacity (the effect record's FLOAT_OPACITY, the
  Opacity under the filter's blending options). Layer Knocks Out is off: GEGL draws the
  layer over its shadow.
- The filter's Clipping (FILTER_CLIP: 1 Clip, 0 Adjust) is not reproduced: the style always
  draws past the layer's bounds, as Adjust does. Clip differs only where the shadow reaches
  past the layer's edge (not in the reporter's blend-* files). A rectangle mask over the layer
  bounds with Layer Mask Hides Effects would match it, but would also cut later edits off.
- The filter's own mode mixes its result (the layer over the shadow) with the layer, so GIMP
  3.2 shows the shadow only in Normal, Replace, Behind and Merge (Patchy Normal) and Dissolve
  (Dissolve). Every other mode draws no shadow and changes only the layer's own pixels: the
  shadow opens switched off with the nearest Photoshop mode (GIMP-only modes map to the
  Photoshop mode of the same family); the change to the layer is not reproduced.
- GEGL mixes the shadow in linear light, Patchy's 8 and 16-bit documents in display values
  (85% black over white: 108 against 38). A Normal shadow there gets the contour "GIMP
  linear light" (`linear_light_shadow_contour`, 9 smooth points): at each mask level the
  display coverage that best gives GEGL's mix of its color over white, 75% and 50% gray,
  divided by the opacity, so the dialog keeps GIMP's opacity. Exact for black over white;
  a colored shadow keeps some difference, since one coverage cannot serve all three
  channels. 32-bit documents (linear already) and Dissolve (opaque pixels) get none.
  Against GIMP 3.2 (`xcf_drop_shadow_issue_507_matches_gimp_if_available`, the reporter's
  file and one file per layer in local-test-fixtures/xcf-issue-507): black shadows within
  0.1 level on average, a blurred magenta one 3.5, Dissolve 7.6 (its random pattern);
  effects.xcf 0.21. The blend-* files (filter opacity 50% and 75%): Normal 0.01, Dissolve
  9.9 with GIMP's dot count (502 against 520).
- `gegl:gaussian-blur` is applied to the layer's pixels: premultiplied linear light, clamp or
  transparent edges, the layer grown when clip-extent is off. Sigmas over 3 use three box
  passes. Within about six levels of GIMP's own blur.
- Color filters are applied to the pixels with formulas fitted on GIMP 3.2's renders of a
  4096-color sweep (`scripts/dev/xcf/make_effect_fixtures.py`, `fit_effects.py`; worst
  difference 1 level unless noted): `gegl:invert-gamma` (1 - x) and `invert-linear`;
  `gimp:desaturate` (lightness and average on display values, luma with the D50 Y weights on
  display values, luminance in linear light, value); `gimp:threshold` on the value channel;
  `gimp:posterize`; `gimp:brightness-contrast` (in linear light: brightness/2 lifts toward
  white or scales toward black, then contrast pivots at 0.5 with slope tan((c + 1) pi / 4));
  `gimp:hue-saturation` with only the master range set (HSL on display values: hue in half
  turns, saturation scaled by 1 + s, lightness toward white or black); `gimp:colorize`
  (linear luminance as HSL lightness in linear light; the lightness setting is unverified);
  `gimp:levels` (value then color channels, in linear light or display values by its trc,
  values below the input black kept linear rather than raised to the gamma; worst 2);
  `gegl:exposure` (worst 2) and `gegl:mono-mixer` in linear light.
- Every applied effect mixes with the original in linear light by the effect's opacity and,
  when its region is the selection, by its mask (when that mask has the layer's size; an empty one, no selection at the time, means the whole layer). One
  notice covers them. Text layers lose their text when an effect changes their pixels.
- GEGL operations the reader has no code of its own for run through Patchy's own GEGL (#616;
  [gegl-filters.md](gegl-filters.md)) when the operation is in `kRunnerEffects`. The reader is
  Qt-free, so it calls an effect runner (`xcf::set_effect_runner`) that `MainWindow` installs
  at startup (`install_xcf_effect_runner`: one `patchy-gegl-host` run per effect, none in a
  build without the bundle or on the web). GIMP stores a GEGL filter's arguments under GEGL's
  property names, so each passes through by name and PROP_FILTER_ARGUMENT type (1 int, 2
  boolean, 3 number as a float, 5 enum, 7 unsigned for seeds, 8 GeglColor as encoded sRGB;
  6, the string config of GIMP's own `gimp:` operations, is not sent). The layer goes over as
  RGBA at the document's depth and comes back the same size; the result mixes as above.
  With the filter's Clipping on Adjust (FILTER_CLIP 0) an entry's `reach` (Motion Blur's
  length, Waves' amplitude, a pixel for Sobel) adds a transparent margin the result spreads
  into, then trimmed to what it covers, as GIMP grows the layer. Spread, Pick, Slur and
  Ripple get none: GIMP 3.2 draws what they move across the edge a few pixels away from
  GEGL's own result, and a margin moves their seeded noise (GEGL's random numbers follow the
  pixel position). An entry needs an operation Patchy offers as a GEGL filter (its legal
  review) and a tile in `test-fixtures/xcf/gegl-effects.xcf`
  (`scripts/dev/xcf/make_gegl_effect_fixture.py`: one 48-pixel tile layer per effect over
  gray, GIMP 3.2's render beside it). Against that render (November 3, 2026): Cubism 0.84
  levels mean, Lens Distortion and Waves under 0.4 (edges), Color to Alpha 7.4 (the
  composite-space difference of its partly transparent pixels), the five edge cases above
  identical inside the tile, every other tile identical or within a level.
- Everything else is left out with a notice naming the effects (GIMP's name for each):
  GIMP's own `gimp:` operations without a fitted formula (curves, color balance, per-range
  hue-saturation), operations Patchy's GEGL lacks (median blur) or that are not in the table,
  and every runner effect in a build without GEGL or when a run fails. Hidden effects are
  ignored.
