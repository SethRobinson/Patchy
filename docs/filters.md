# Visual filters and recipes

This document is the implementation contract for Patchy's built-in pixel filters, the shared filter catalog, and ordered filter recipes. The gallery and Smart Filter work build on these types, so persisted names and default behavior are compatibility surfaces.

## Two execution contracts

`FilterRegistry::apply(id, pixels)` is the legacy compatibility path. It keeps the original built-in function and its historical pixels.

`FilterRegistry::apply(invocation, pixels)` is the named-parameter path used by dialogs, recipes, preview rendering, and future galleries. It uses the catalog defaults when a parameter is absent.

These paths are intentionally separate. Their defaults are not equivalent for every filter. Posterize, Gaussian Blur, Clouds, Glowing Edges, and some rounding paths have historical differences. Do not redirect the legacy wrapper through `default_invocation()` and do not change either output merely to make the two paths agree. Tests pin the two contracts independently.

## Stable identifiers and schemas

The 30 built-in filter IDs are persisted compatibility identifiers. Never rename or reuse one:

```text
patchy.filters.invert
patchy.filters.brightness_contrast
patchy.filters.grayscale
patchy.filters.desaturate
patchy.filters.auto_contrast
patchy.filters.soft_glow
patchy.filters.punchy_color
patchy.filters.noir
patchy.filters.cinematic_matte
patchy.filters.vintage_fade
patchy.filters.sepia
patchy.filters.threshold
patchy.filters.posterize
patchy.filters.box_blur
patchy.filters.sharpen
patchy.filters.unsharp_mask
patchy.filters.gaussian_blur
patchy.filters.motion_blur
patchy.filters.radial_blur
patchy.filters.edge_detect
patchy.filters.emboss
patchy.filters.glowing_edges
patchy.filters.twirl
patchy.filters.wave
patchy.filters.pinch_bloat
patchy.filters.clouds
patchy.filters.pixelate
patchy.filters.color_halftone
patchy.filters.film_grain
patchy.filters.vignette
```

Each `FilterInvocation` stores the filter ID, the filter's schema version, named parameters, and captured foreground/background colors. Schema version 1 is the initial catalog schema. A known ID with an unsupported schema is unsupported, not a request to run the newest schema.

Parameter keys are stable within a filter schema. The version-1 keys and defaults are:

| Filter suffix | Parameters |
| --- | --- |
| `invert` | `amount=100` |
| `brightness_contrast` | `brightness=0`, `contrast=0` |
| `grayscale`, `desaturate`, `auto_contrast` | `amount=100` |
| `soft_glow`, `punchy_color`, `noir`, `cinematic_matte`, `vintage_fade`, `sepia` | `amount=100` |
| `threshold` | `threshold=128` |
| `posterize` | `levels=4` |
| `box_blur` | `radius=1` |
| `sharpen` | `amount=100` |
| `unsharp_mask` | `amount=150`, `radius=2`, `threshold=8` |
| `gaussian_blur` | `radius=2` |
| `motion_blur` | `angle=0`, `distance=12` |
| `radial_blur` | `amount=35`, `samples=16` |
| `edge_detect` | `strength=100` |
| `emboss` | `angle=135`, `height=2`, `amount=100` |
| `glowing_edges` | `edge_width=2`, `brightness=140`, `smoothness=2` |
| `twirl` | `angle=180`, `radius=100` |
| `wave` | `amplitude=12`, `wavelength=48`, `phase=0` |
| `pinch_bloat` | `amount=35`, `radius=100` |
| `clouds` | `scale=96`, `detail=6`, `contrast=40`, `seed=1` |
| `pixelate` | `block_size=4` |
| `color_halftone` | `cell_size=10`, `intensity=75`, `contrast=60` |
| `film_grain` | `amount=50` |
| `vignette` | `strength=55` |

Missing known parameters use these defaults. Unknown parameter keys are ignored so a newer writer can add harmless fields. Unknown filter IDs and unsupported schema versions make the invocation and its containing recipe unsupported. They must never fall back to a different filter.

Values are normalized through the catalog before execution. Integer, double, boolean, and stable string-option values keep their declared types. Numeric values are clamped to the catalog range. A known key with the wrong type or an unknown option token is invalid rather than silently coerced.

## Categories and UI contracts

Seven filters belong to Image > Adjustments: Invert, Brightness/Contrast, Grayscale, Desaturate, Auto Contrast, Threshold, and Posterize. Grayscale currently has no direct action.

The 23 Filter-menu effects are cataloged in this display order:

- Photo Looks: Soft Glow, Punchy Color, Noir, Cinematic Matte, Vintage Fade, Vintage Sepia, Lens Vignette
- Blur: Box Blur, Gaussian Blur, Motion Blur, Radial Blur
- Sharpen: Sharpen, Unsharp Mask
- Distort: Twirl, Wave, Pinch/Bloat
- Noise: Analog Grain
- Pixelate: Pixel Mosaic, Color Halftone
- Stylize: Edge Detect, Emboss, Glowing Edges
- Render: Clouds

The catalog generates dialog controls, but the existing Qt object names such as `filterAmountSpin` and `filterRadiusSlider` remain test and automation contracts.

Only six filter IDs are registered hotkey command IDs today: Invert, Desaturate, Auto Contrast, Brightness/Contrast, Threshold, and Posterize. Direct Filter-menu actions are not HotkeyRegistry commands. A catalog refactor must not silently add or remove commands.

Human-readable catalog names are canonical English translation sources. UI code translates them in the existing `QObject` context, while submenu and action status text keep their existing `MainWindow` context.

## Photo Looks Gallery

`Filter > Visual Filters & Looks...` is the shared entry point for visual filter browsing. Its persisted hotkey command ID is `filter.gallery`; it has no default shortcut. The seven existing Photo Looks remain available as direct Filter-menu actions and keep their existing IDs, dialogs, defaults, and output.

The first gallery checkpoint has this fixed display order:

1. Original
2. Soft Glow (`patchy.filters.soft_glow`)
3. Punchy Color (`patchy.filters.punchy_color`)
4. Noir (`patchy.filters.noir`)
5. Cinematic Matte (`patchy.filters.cinematic_matte`)
6. Vintage Fade (`patchy.filters.vintage_fade`)
7. Vintage Sepia (`patchy.filters.sepia`)
8. Lens Vignette (`patchy.filters.vignette`)

Original is a UI sentinel, not a filter ID and not a persisted invocation. Gallery list items carry the exact built-in ID for the seven real Looks in `Qt::UserRole + 1`; Original carries an empty value. The gallery's automation contracts include `filterGalleryDialog`, `filterGalleryLooksList`, `filterGalleryPreview`, `filterGalleryParameters`, `filterGalleryCanvasPreviewCheck`, `filterGalleryBeforeButton`, `filterGalleryStatusLabel`, `filterGalleryButtonBox`, and the `filterGalleryZoom*` controls. Catalog-generated amount and strength controls keep their existing `filterAmount*` and `filterStrength*` object names.

Thumbnail and center-preview work always starts from an immutable copy of the active layer. Thumbnails are generated lazily from a bounded proxy. Any pixel-distance parameters are scaled through `FilterRegistry::scale`; final canvas preview and Apply always use the unscaled invocation at full layer resolution. Returning to the same Look after viewing another must reproduce the same pixels, never a cumulative re-filtering of an earlier preview.

Full-resolution live-canvas preview requests carry monotonically increasing generations. A finished worker may update the canvas only when it is still the newest generation and the dialog remains open. At most one request runs while the latest pending request replaces older pending work. Closing the dialog invalidates every unfinished result. The bounded center preview is debounced, and thumbnail work advances one Look per event-loop turn. The momentary Before button compares the center preview with the immutable source while held; it does not change the live canvas preview. Live Canvas Preview is enabled by default and can restore or reapply the current full-resolution result without changing the dialog selection.

Cancel restores the active layer's original pixels and document-space bounds exactly, adds no undo entry, and does not mark a previously clean document modified. Apply renders once more from the immutable original, commits one destructive transaction, and creates one undo entry. Undo and Redo restore both pixels and bounds. Original and identity results close without creating a no-op undo entry.

## Spatial scaling and bounds

Thumbnail and proxy rendering scales only parameters marked as pixel distances. Version-1 spatial keys are:

- Box/Gaussian/Unsharp radius
- Motion Blur distance
- Emboss height
- Glowing Edges edge width and smoothness
- Wave amplitude and wavelength
- Clouds scale
- Pixel Mosaic block size
- Color Halftone cell size

Angles, percentages, samples, intensity, detail, seed, and color values do not scale. Scaling returns a normalized copy and never mutates the original invocation.

Output growth and translation support are catalog metadata:

- Box Blur and Gaussian Blur grow by their radius and have translation support equal to that radius.
- Motion Blur grows by its distance and has translation support of distance plus one for bilinear sampling.
- Radial Blur computes growth from the input dimensions and amount. Amount zero has no growth.
- Unsharp Mask does not grow, but its translation support is its radius.
- Sharpen and Edge Detect have translation support of one pixel.
- Other version-1 filters neither grow nor advertise fixed translation support.

The UI selection wrapper decides whether expansion is allowed. A selected operation stays inside the layer bounds. With no selection, an RGBA layer may grow and then trim transparent borders. Preview, Cancel, Apply, Undo, and Redo must restore both pixels and document-space bounds.

## Captured colors

Foreground and background colors are copied into every invocation when it is created. Clouds reads those captured colors. Re-rendering a recipe must not depend on the toolbar swatches at that later time. Filters that do not use colors produce the same result regardless of the captured values.

## Recipes

`FilterRecipe` stores entries in execution order. Each entry contains an invocation, enabled state, opacity, and blend mode. Disabled entries are skipped. An unsupported invocation makes the whole persisted recipe unsupported, including when that entry is disabled, because enabling it later must not produce a substituted result.

Recipe opacity is a finite double from 0 through 1. An out-of-range or non-finite opacity makes the recipe unsupported. The default is enabled, opacity 1, Normal blend mode. Recipe execution is deterministic and starts from its supplied immutable source; callers must not build a preview cumulatively from an earlier preview.

User Look JSON is a later gallery checkpoint. When added, use one UUID-named version-1 JSON record per Look, strict type/shape validation, and `QSaveFile` for atomic replacement. A malformed record is skipped without hiding or modifying other records.

## Regression coverage

Keep separate regression coverage for:

- the legacy wrapper output for every built-in ID;
- named version-1 catalog defaults;
- explicit non-default named parameters;
- the exact ID, category, parameter, scaling, and bounds catalog;
- missing and unknown parameters, unsupported IDs/schemas, and recipe ordering;
- captured Clouds colors;
- progress completion and cancellation;
- menu/action/hotkey contracts;
- selection, expanding bounds, Cancel, and one-step Undo/Redo;
- the all-filter visual contact sheet.

Never re-pin an output canary as part of a structural refactor. First establish that the refactored path reproduces the old focused outputs and the pre-refactor contact-sheet SHA-256 (`FFDC09594B81EE9EE3F31773E79CE8F59E9D46C421FAD976E9135C89E19743A3`).
