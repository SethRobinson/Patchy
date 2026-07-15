# Visual filters and recipes

This document is the implementation contract for Patchy's built-in pixel filters, the shared filter catalog, and ordered filter recipes. The gallery and Smart Filter work build on these types, so persisted names and default behavior are compatibility surfaces.

## Two execution contracts

`FilterRegistry::apply(id, pixels)` is the legacy compatibility path. It keeps the original built-in function and its historical pixels.

`FilterRegistry::apply(invocation, pixels)` is the named-parameter path used by dialogs, recipes, preview rendering, and future galleries. It uses the catalog defaults when a parameter is absent.

These paths are intentionally separate. Their defaults are not equivalent for every filter. Posterize, Gaussian Blur, Clouds, Glowing Edges, and some rounding paths have historical differences. Do not redirect the legacy wrapper through `default_invocation()` and do not change either output merely to make the two paths agree. Tests pin the two contracts independently.

## Stable identifiers and schemas

The 38 built-in filter IDs are persisted compatibility identifiers. Never rename or reuse one:

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
patchy.filters.high_pass
patchy.filters.median
patchy.filters.dust_and_scratches
patchy.filters.surface_blur
patchy.filters.lens_blur
patchy.filters.iris_blur
patchy.filters.tilt_shift_blur
patchy.filters.plastic_wrap
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
| `radial_blur` | `amount=35`, `samples=16`, `center_x=50.0`, `center_y=50.0` |
| `edge_detect` | `strength=100` |
| `emboss` | `angle=135`, `height=2`, `amount=100` |
| `glowing_edges` | `edge_width=2`, `brightness=140`, `smoothness=2` |
| `twirl` | `angle=180`, `radius=100`, `center_x=50.0`, `center_y=50.0` |
| `wave` | `amplitude=12`, `wavelength=48`, `phase=0` |
| `pinch_bloat` | `amount=35`, `radius=100`, `center_x=50.0`, `center_y=50.0` |
| `clouds` | `scale=96`, `detail=6`, `contrast=40`, `seed=1` |
| `pixelate` | `block_size=4` |
| `color_halftone` | `cell_size=10`, `intensity=75`, `contrast=60` |
| `film_grain` | `amount=50` |
| `vignette` | `strength=55`, `center_x=50.0`, `center_y=50.0` |
| `high_pass` | `radius=10.0` |
| `median` | `radius=1.0` |
| `dust_and_scratches` | `radius=1`, `threshold=0` |
| `surface_blur` | `radius=5.0`, `threshold=15` |
| `lens_blur` | `radius=15.0`, `blades=6`, `blade_curvature=50`, `rotation=0` |
| `iris_blur` | `blur=15.0`, `center_x=50.0`, `center_y=50.0`, `angle=0`, `iris_width=50.0`, `iris_height=40.0`, `focus=50.0` |
| `tilt_shift_blur` | `blur=15.0`, `center_x=50.0`, `center_y=50.0`, `angle=0`, `focus_half_width=10.0`, `transition_width=20.0` |
| `plastic_wrap` | `highlight_strength=9`, `detail=7`, `smoothness=5` |

Missing known parameters use these defaults. Unknown parameter keys are ignored so a newer writer can add harmless fields. Unknown filter IDs and unsupported schema versions make the invocation and its containing recipe unsupported. They must never fall back to a different filter.

Values are normalized through the catalog before execution. Integer, double, boolean, and stable string-option values keep their declared types. Numeric values are clamped to the catalog range. A known key with the wrong type or an unknown option token is invalid rather than silently coerced.

## Categories and UI contracts

Seven filters belong to Image > Adjustments: Invert, Brightness/Contrast, Grayscale, Desaturate, Auto Contrast, Threshold, and Posterize. Grayscale currently has no direct action.

The gallery exposes the other 31 effects in the fixed catalog and category order below. Display labels are translated, but order is never locale-sorted.

| Category token | Gallery effects in order |
| --- | --- |
| `photo_looks` | Soft Glow (`patchy.filters.soft_glow`), Punchy Color (`patchy.filters.punchy_color`), Noir (`patchy.filters.noir`), Cinematic Matte (`patchy.filters.cinematic_matte`), Vintage Fade (`patchy.filters.vintage_fade`), Vintage Sepia (`patchy.filters.sepia`), Lens Vignette (`patchy.filters.vignette`) |
| `blur` | Box Blur (`patchy.filters.box_blur`), Gaussian Blur (`patchy.filters.gaussian_blur`), Motion Blur (`patchy.filters.motion_blur`), Radial Blur (`patchy.filters.radial_blur`), Surface Blur (`patchy.filters.surface_blur`), Lens Blur (`patchy.filters.lens_blur`), Iris Blur (`patchy.filters.iris_blur`), Tilt-Shift Blur (`patchy.filters.tilt_shift_blur`) |
| `sharpen` | Sharpen (`patchy.filters.sharpen`), Unsharp Mask (`patchy.filters.unsharp_mask`), High Pass (`patchy.filters.high_pass`) |
| `distort` | Twirl (`patchy.filters.twirl`), Wave (`patchy.filters.wave`), Pinch/Bloat (`patchy.filters.pinch_bloat`) |
| `noise` | Analog Grain (`patchy.filters.film_grain`), Median (`patchy.filters.median`), Dust & Scratches (`patchy.filters.dust_and_scratches`) |
| `pixelate` | Pixel Mosaic (`patchy.filters.pixelate`), Color Halftone (`patchy.filters.color_halftone`) |
| `stylize` | Edge Detect (`patchy.filters.edge_detect`), Emboss (`patchy.filters.emboss`), Glowing Edges (`patchy.filters.glowing_edges`) |
| `render` | Clouds (`patchy.filters.clouds`) |
| `artistic` | Plastic Wrap (`patchy.filters.plastic_wrap`) |

The category selector starts with the synthetic `all` and `favorites` views, then uses the nine tokens in the table. These eleven tokens and their order are settings compatibility surfaces. Never persist a translated label or a `FilterCategory` ordinal.

Liquify appears first in the Distort submenu but is deliberately outside this catalog and gallery. It records an ordered sequence of manual brush gestures rather than one stable filter invocation. Its separate implementation and PSD contract live in `docs/liquify.md`.

The catalog generates dialog controls, but the existing Qt object names such as `filterAmountSpin` and `filterRadiusSlider` remain test and automation contracts.

The catalog is also the type contract for generated editors. Integer and double parameters receive linked sliders and spin boxes, booleans receive check boxes, and stable string options receive combo boxes whose item data holds the option token. Double slider ticks are derived from the declared minimum, maximum, and step, while the spin box keeps the declared precision. Units, defaults, ranges, object-name roots, and option tokens all come from `FilterParameterDefinition`. Direct filter dialogs and the gallery consume the same `FilterDialogSpec`, so their standard controls must stay in sync. The gallery adds visual companions without creating a second parameter model.

`FilterParameterDefinition::practical_minimum` and `practical_maximum` may narrow a linked slider without narrowing the parameter's semantic range. The spin box, normalization, recipes, and persistence continue to use `minimum` and `maximum`. High Pass and Unsharp Mask keep their radius sliders useful through 12 px while accepting Photoshop-compatible typed radii through 1000 px. Unsharp Mask also accepts Amount 1 through 500 percent and Threshold 0 through 255. Motion Blur keeps practical Angle and Distance controls at -180 through 180 degrees and 1 through 64 px while accepting native typed values through +/-360 degrees and 999 px. Median uses a practical 1 through 25 px slider while retaining native typed values through 500 px. Dust & Scratches uses a practical 1 through 25 px radius slider while retaining its native integer range through 100 px; Threshold spans 0 through 255. Surface Blur uses a practical 1 through 25 px radius slider while retaining its native fractional range through 100 px; Threshold spans 2 through 255. Lens Blur and Iris Blur use practical 0 through 50 px blur sliders while accepting typed values through 100 px. Tilt-Shift Blur uses a practical 0 through 50 px blur slider while retaining typed values through 500 px.

`FilterParameterPresentation` is not persisted and does not replace the parameter key or value. Current roles are `Angle`, `CenterXPercent`, `CenterYPercent`, `EffectRadiusPercent`, `WaveAmplitude`, `WaveWavelength`, `WavePhase`, `TiltFocusHalfWidthPercent`, `TiltTransitionWidthPercent`, `IrisWidthPercent`, and `IrisHeightPercent`. UI and rendering code must select specialized behavior by these roles, not by a parameter key, display label, unit, or filter ID. The render wrapper uses the center, Tilt-Shift width, and Iris dimension roles to preserve their image-space geometry when transparent padding is added.

Only six catalog filter IDs are registered hotkey command IDs today: Invert, Desaturate, Auto Contrast, Brightness/Contrast, Threshold, and Posterize. Catalog-generated direct Filter-menu actions are not HotkeyRegistry commands. The separate Liquify workspace uses the persisted command ID `filter.liquify` with Ctrl+Shift+X. A catalog refactor must not silently add or remove commands.

Human-readable catalog names are canonical English translation sources. UI code translates them in the existing `QObject` context, while submenu and action status text keep their existing `MainWindow` context.

## Visual Filters and Looks Gallery

`Filter > Visual Filters & Looks...` is the shared entry point for visual filter browsing. Its persisted hotkey command ID is `filter.gallery`; it has no default shortcut. Existing direct Filter-menu actions remain fast paths and keep their IDs, dialogs, defaults, selection behavior, and output.

Original is a UI sentinel, not a filter ID or persisted invocation. It remains visible at the top of every category and search view. Real items carry their exact built-in ID in `Qt::UserRole + 1`; Original carries an empty value. The gallery pre-creates all items in catalog order and filters them in place instead of rebuilding or sorting the list.

Search is case-insensitive and localized. It matches the translated filter name, canonical English filter name, translated category name, and stable category token with underscores treated as spaces. Original stays visible when no real effect matches. Filtering the catalog rows does not render an effect or change the applied stack. If a search, category, or favorite change hides the selected catalog filter, the catalog selection returns to Original without clearing the active recipe. Explicitly clicking that Original row still clears the recipe, even when it was already selected by filtering.

Favorites are stored by stable filter ID and follow catalog order. Loading discards missing IDs and duplicate entries, then rewrites the cleaned list. Toggling a favorite writes immediately and is a harmless library preference, so Cancel does not undo it. The Favorites view may contain no real effects; Original still provides a safe no-op selection.

The gallery settings keys are fixed:

| Key | Value |
| --- | --- |
| `filters/gallery/favorites` | Ordered `QStringList` of valid built-in filter IDs |
| `filters/gallery/category` | One of the eleven stable view/category tokens |
| `filters/gallery/lastFilterId` | Last selected built-in filter ID, or empty for Original |
| `filters/gallery/liveCanvasPreview` | Boolean live-preview preference |
| `filters/gallery/size` | Last dialog size |

Unknown category tokens fall back to `all`. A saved filter is restored only when it still exists and is visible in the restored view. Saved sizes are accepted only from 880 by 560 through 3200 by 2400 pixels; the default is 1120 by 720. Search text, zoom, pan, and parameter edits are session-only.

The gallery's automation contracts include `filterGalleryDialog`, `filterGallerySearchEdit`, `filterGalleryCategoryCombo`, `filterGalleryLooksList`, `filterGalleryEmptyLabel`, `filterGalleryPreview`, `filterGalleryParameters`, `filterGalleryParameterEditor`, `filterGalleryFavoriteButton`, `filterGalleryCanvasPreviewCheck`, `filterGalleryBeforeButton`, `filterGalleryStatusLabel`, `filterGalleryButtonBox`, and the `filterGalleryZoom*` controls. The stack controls are `filterGalleryAppliedEffects`, `filterGalleryAppliedEffectsList`, `filterGalleryDuplicateEffectButton`, and `filterGalleryRemoveEffectButton`. Saved Look controls are `filterGallerySavedLooks`, `filterGallerySavedLooksCombo`, `filterGallerySaveLookButton`, `filterGalleryRenameLookButton`, and `filterGalleryDeleteLookButton`. Catalog-generated controls keep their catalog object-name roots. The gallery assigns `filterAngleDial` and `filterWaveformControl` to its two specialized widgets. The center/radius overlay is part of `filterGalleryPreview`, not a separate child widget.

The angle dial appears for Motion Blur, Emboss, Twirl, Lens Blur Rotation, Iris Blur, and Tilt-Shift Blur. It is synchronized with the standard numeric controls. Its hand wraps visually, but Twirl retains the full `-720` through `720` degree value. The Wave graph synchronizes amplitude, wavelength, and phase while retaining all three numeric controls. Horizontal dragging changes phase, vertical dragging changes amplitude, and the wheel changes wavelength.

Radial Blur, Twirl, Pinch/Bloat, and Lens Vignette declare `center_x` and `center_y` as doubles from 0.0 through 100.0, with defaults of 50.0 and steps of 0.1. Their roles are `CenterXPercent` and `CenterYPercent`. The preview draws a draggable crosshair for these filters. Dragged values are quantized to the declared step before both the editor and invocation are updated. Twirl and Pinch/Bloat also mark their integer `radius` as `EffectRadiusPercent`, so the overlay adds a draggable radius circle and handle. Normal pan and zoom remain active when a drag does not begin near one of those handles. Spatial drags move the overlay and numeric values immediately, but a size-changing center proxy is adopted only after release so its coordinate system cannot jump under the pointer.

Tilt-Shift Blur uses the same center and angle roles plus `TiltFocusHalfWidthPercent` and `TiltTransitionWidthPercent`. The preview draws a center handle, a rotation handle, and short draggable grip bars marking the focus band edges (solid) and the full-blur onset (dashed). The bars deliberately do not span the image: boundary lines that divide the image around the center are an Apple patent claim (US 8971623, see docs/smart-objects.md "Patents and trademarks"), and `ui_filter_gallery_tilt_shift_overlay_uses_grip_bars` pins the short-bar rendering. Dragging either width handle edits both sides symmetrically. Normal pan and zoom remain available away from the handles, and the proxy render is deferred until the gesture finishes so expanding bounds cannot move the control under the pointer.

Iris Blur uses the center and angle roles, so its preview provides the normal center crosshair and angle dial. Iris Width, Iris Height, and Focus remain linked numeric controls. Patchy deliberately does not draw Photoshop's editable iris boundary widget or support multiple pins. The one explicit ellipse is edited numerically and generates one scalar blend mask; see docs/smart-objects.md "Patents and trademarks".

Thumbnail and center-preview work always starts from an immutable copy of the active layer. The center proxy has a maximum dimension of 640 pixels and the thumbnail proxy has a maximum dimension of 180 pixels. Both use bounded premultiplied bilinear resampling and a correspondingly scaled selection. Pixel-distance parameters are scaled through `FilterRegistry::scale`; percentages, angles, sample counts, centers, and captured colors do not scale. Final canvas preview and Apply always use the unscaled recipe at full layer resolution. Returning to the same effect after viewing another must reproduce the same pixels, never a cumulative re-filtering of an earlier preview.

Catalog thumbnails remain single-filter previews. The center and live-canvas previews render the complete applied recipe from the immutable original. Recipe rendering traces the input bounds for every entry, including disabled and zero-opacity entries. The active center/radius control maps from that entry's traced input rectangle into the final displayed bounds, so it remains accurate when preceding or following filters expand the layer. A selection fixes every entry to the original local bounds because selected recipes never expand the layer.

Thumbnail icons are 128 by 78 pixels. Original is available immediately. Missing thumbnails are generated lazily, one visible filter per timer turn, and the per-dialog ready flag prevents repeat work. Hidden rows are skipped. Selecting and editing a filter refreshes that row's icon from the current invocation. The cache is session-local and is not persisted across gallery openings. Category, search, and favorite changes prioritize newly visible missing thumbnails without invalidating completed ones.

Full-resolution live-canvas preview requests carry monotonically increasing generations. A finished worker may update the canvas only when it is still the newest generation and the dialog remains open. At most one request runs while the latest pending request replaces older pending work. Closing the dialog invalidates every unfinished result. The bounded center preview uses the same latest-generation rule on its own worker, cooperatively cancels obsolete work, and is debounced before dispatch. Thumbnail work stays bounded to the 180-pixel proxy and advances one effect per event-loop turn. The momentary Before button aligns the immutable source to the current expanded result bounds, preserving zoom and pan while it is held; it does not change the live canvas preview. Live Canvas Preview is enabled by default and can restore or reapply the current full-resolution result without changing the dialog selection.

Cancel restores the active layer's original pixels and document-space bounds exactly, adds no undo entry, and does not mark a previously clean document modified. Apply renders the complete recipe once more from the immutable original, commits one destructive transaction, and creates one undo entry. Undo and Redo restore both pixels and bounds. Original, an empty recipe, and a recipe with no enabled nonzero-opacity entries close without creating a no-op undo entry.

## Spatial scaling and bounds

Thumbnail and proxy rendering scales only parameters marked as pixel distances. Version-1 spatial keys are:

- Box Blur, Gaussian Blur, Unsharp Mask, High Pass, Median, Dust & Scratches, Surface Blur, and Lens Blur radius, plus Iris Blur and Tilt-Shift Blur
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
- Motion Blur accepts a user-entered Angle from -360 through 360 degrees and Distance from 1 through 999 px. Its practical controls use -180 through 180 degrees and 1 through 64 px. Rendering uses one fixed, premultiplied-alpha line kernel, grows conservatively by the distance, and has translation support of distance plus one for bilinear sampling.
- Radial Blur keeps the historical centered growth calculation for exact default compatibility. An edited center computes growth from the actual sampled corner sweep without a fixed pixel cap. Impossible dimensions fail through the registry's checked padding path instead of clipping valid output. Amount zero has no growth.
- Unsharp Mask accepts Amount 1 through 500 percent, fractional Radius 0.1 through 1000 px with a practical slider through 12 px, and Threshold 0 through 255. It does not grow, preserves alpha, and advertises translation support of `ceil(3 * radius)`. Photoshop scales the signed detail before subtracting Threshold from its magnitude; the radius-2.5 low-pass has its own measured byte kernel rather than Gaussian Blur's radius-2.5 kernel.
- High Pass keeps the input bounds and alpha. Its linked slider spans the practical 0.1 through 12 px range, with a default of 10 px, while typed values, recipes, and native Smart Filter imports retain Photoshop's 0.1 through 1000 px range. Translation support is three times the radius.
- Median keeps the input bounds. Its linked slider spans the practical 1 through 25 px range, with a default of 1 px, while typed values, recipes, and native Smart Filter imports retain Photoshop's 1 through 500 px range. Photoshop floors fractional radii for rendering without rewriting the stored value. Median advertises no finite translation support because transparent RGB extension chooses the nearest visible source across the full input; selected filtering must process the complete layer before restoring unselected pixels.
- Dust & Scratches keeps the input bounds and alpha. Radius is an integer from 1 through 100 with a practical slider through 25, and Threshold is an integer from 0 through 255. It computes a square per-channel RGB median using Median's nearest-visible straight-RGB extension, then replaces the complete RGB triplet only when its maximum channel difference from the source is strictly greater than Threshold. It advertises no finite translation support because the transparent-RGB extension can choose a visible source anywhere in the input.
- Surface Blur accepts a fractional radius from 1 through 100 px in 0.01 px steps, with a default of 5 px and a practical slider through 25 px. Its effective integer radius is `max(1, floor(radius + 0.5))`. Threshold is an integer from 2 through 255 with a default of 15. For each independently filtered channel, a square edge-clamped window assigns every sample `v` around center `c` the weight `max(0, 5 * threshold - 2 * abs(v - c))`; the weighted quotient is rounded to the nearest integer with ties to even. RGB uses Median's nearest-visible straight-RGB extension under transparent pixels, while alpha runs the same weighted formula directly. The result is alpha-trimmed after padding and can grow by at most the effective radius. It advertises no finite translation support because the straight-RGB extension can choose a visible source anywhere in the input.
- Lens Blur accepts a fractional Radius from 0 through 100 px, with a default of 15 px and a practical slider through 50 px. Blades is 3 through 8, Blade Curvature is 0 through 100 percent, and Rotation is -180 through 180 degrees. One deterministic supersampled aperture kernel is applied to premultiplied RGBA. Curvature blends the selected polygon toward a circle. Radius 0 is an exact identity. Large radii use deterministic fixed-point downsample, aperture convolution, and upsample stages. The registry reserves a factor-aligned transparent margin large enough for the aperture and alpha-trims afterward; no translation support is advertised because the multiscale grid is anchored to the complete input rectangle.
- Iris Blur accepts Blur from 0 through 100 px, with a default of 15 px and a practical slider through 50 px. Center is 0 through 100 percent, Angle is -180 through 180 degrees, Iris Width and Height are 1 through 200 percent of their respective input dimensions, and Focus is 0 through 100 percent of the ellipse radius. It computes one fixed round aperture blur once, then premultiplied-alpha blends between the original and blurred image through one deterministic smooth elliptical mask. The Focus interior stays sharp and pixels outside the ellipse receive the full fixed blur. Blur 0 is an exact identity. It uses Lens Blur's factor-aligned growth and advertises no translation support. It does not infer depth, detect or boost highlights, vary a kernel per pixel, or combine multiple blur patterns.
- Plastic Wrap keeps the input bounds and alpha byte-exact. Highlight Strength is an integer from 0 through 20, while Detail and Smoothness are integers from 1 through 15. One fixed integer height-field formula smooths alpha-weighted luminance with an edge-clamped box, takes local gradients, and adds a pronounced constant-direction relief plus ridge highlights to the original RGB. Alpha weighting gives isolated artwork contour relief without changing its transparency. The settings are dimensionless and do not scale for thumbnails. Representative low-contrast and flat-color-on-transparency regressions pin visible treatment at the defaults. It conservatively advertises no finite translation support, so selected application renders with full-layer context.
- Tilt-Shift Blur accepts a fractional maximum blur from 0 through 500 px, with a default of 15 px and a practical slider through 50 px. Center, focus half-width, and transition width are percentages; angle 0 means horizontal focus lines. Pixels inside the focus band stay sharp, a deterministic cubic transition increases the local radius, and pixels beyond the dashed boundaries use the requested maximum blur. Radius 0 is an exact identity. The result is alpha-trimmed after transparent padding and can grow by at most `ceil(blur)`. It advertises no finite translation support because its band geometry depends on the complete input rectangle.
- Sharpen and Edge Detect have translation support of one pixel.
- Other version-1 filters neither grow nor advertise fixed translation support.

When rendering expands an RGBA layer, the registry pads every side before executing the filter. A center expressed against the original image must therefore be remapped to the padded buffer. For each axis, the registry applies:

```text
padded_percent = 100 * (margin + (original_extent - 1) * percent / 100)
                 / (original_extent + 2 * margin - 1)
```

This remap is selected by the `CenterXPercent` and `CenterYPercent` roles and preserves the same image-space point after padding. It applies to default 50.0 centers as well as edited centers. Never run a centered effect on a padded buffer with the unadjusted percentage, because an off-center value would shift with the new bounds.

Tilt-Shift focus and transition widths are percentages of the shorter input extent. Padding multiplies both percentages by `original_shorter / padded_shorter`, preserving the same document-space band widths while the center roles preserve the band origin.

Iris Width is a percentage of input width and Iris Height is a percentage of input height. Padding scales them independently by `original_width / padded_width` and `original_height / padded_height`, preserving the same document-space ellipse while the center roles preserve its origin.

The UI selection wrapper decides whether expansion is allowed. A selected operation stays inside the layer bounds. With no selection, an RGBA layer may grow and then trim transparent borders. Preview, Cancel, Apply, Undo, and Redo must restore both pixels and document-space bounds.

## Captured colors

Foreground and background colors are copied into every invocation when it is created. Clouds reads those captured colors. Re-rendering a recipe must not depend on the toolbar swatches at that later time. Filters that do not use colors produce the same result regardless of the captured values.

## Recipes

`FilterRecipe` stores entries in execution order. Each entry contains an invocation, enabled state, opacity, and blend mode. Disabled entries are skipped. An unsupported invocation makes the whole persisted recipe unsupported, including when that entry is disabled, because enabling it later must not produce a substituted result.

Recipe opacity is a finite double from 0 through 1. An out-of-range or non-finite opacity makes the recipe unsupported. The default is enabled, opacity 1, Normal blend mode. Recipe execution is deterministic and starts from its supplied immutable source; callers must not build a preview cumulatively from an earlier preview.

The applied-effects list displays the final effect at the top, opposite the stored execution order. Reading a reordered list therefore rebuilds the recipe from the bottom visual row to the top. Each dialog entry has a transient numeric identity so duplicate entries remain independent even when they share the same filter ID and parameters. This identity is never persisted.

Selecting the first catalog filter creates the first recipe entry. Selecting another catalog filter replaces the active recipe entry. Duplicate inserts a separate copy immediately after the active entry in execution order, which places it immediately above the source entry in the visual list. Remove selects the nearest remaining visual row. Original clears the recipe. Enable, duplicate, remove, pointer drag reorder, Reset, and parameter edits affect only the applied stack; category, search, and Favorites changes do not. Effect rows are drag sources but not drop targets, forcing Qt to treat a pointer drop as an insertion between rows and emit the canonical row-move signal.

Scaling a recipe returns a normalized copy and retains entry order, enable state, opacity, blend mode, and captured colors. Aggregate translation support is the checked sum of enabled, nonzero-opacity entry support. Unknown support from any executed entry makes the aggregate unknown. Zero-opacity entries are still validated as part of the persistence contract, but they do not execute, expand bounds, report progress, or affect aggregate support.

When a selection exists, the complete recipe runs against one immutable source and the wrapper restores pixels outside the selection once after the final entry. Restoring outside pixels after each entry changes spatial-filter results near the selection edge and is not allowed. A fully transparent expanded result returns to the input rectangle instead of growing empty bounds once per filter.

## Saved Looks

User Looks live as independent files under `<settings directory>/looks/<uuid>.json`. The lowercase canonical UUID is the stable preset ID and filename stem. Save creates a fresh UUID, Rename keeps it, and Delete removes its one record. These library operations take effect immediately and are not rolled back when the gallery is cancelled.

Each record uses this version-1 shape:

```json
{
  "version": 1,
  "id": "01234567-89ab-4cde-8123-456789abcdef",
  "name": "My Look",
  "recipe": {
    "entries": [
      {
        "enabled": true,
        "opacity": 1.0,
        "blendMode": "normal",
        "invocation": {
          "filterId": "patchy.filters.soft_glow",
          "schemaVersion": 1,
          "parameters": {
            "amount": {"type": "integer", "value": 75}
          },
          "foreground": {"red": 0, "green": 0, "blue": 0},
          "background": {"red": 255, "green": 255, "blue": 255}
        }
      }
    ]
  }
}
```

Parameter values carry an explicit `integer`, `double`, `boolean`, or `string` type. Blend modes use the stable full Photoshop descriptor string tokens, never enum ordinals or translated names. Entry array order is execution order. Colors are required even when the current filter does not use them, so later rendering never reads the toolbar swatches.

Writes use `QSaveFile`, and in-memory state changes only after the atomic commit succeeds. Loading is strict and bounded to 1 MiB per file, 64 entries per recipe, and 64 parameters per entry. It rejects malformed JSON, unsupported record versions, filename/record UUID mismatches, invalid UTF-8, invalid value types, non-finite or out-of-range opacity, invalid colors, and unknown blend tokens. Unknown filter IDs and schema versions remain structurally valid records. They appear in the Saved Looks list disabled with an unsupported tooltip, and Patchy preserves them rather than substituting another filter. One malformed record is skipped without hiding, modifying, or deleting neighboring records.

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
