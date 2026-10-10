# Visual filters and recipes

The implementation contract for Patchy's built-in pixel filters, the shared filter catalog, and ordered filter recipes. The gallery and Smart Filter work build on these types, so persisted names and default behavior are compatibility surfaces. Kernels: `src/filters/builtin_filters.cpp`, `filter_engine.cpp`, and the bodies both share with the native Smart Filters in `filter_kernels.{hpp,cpp}` (box blur, radial blur, emboss, pixel mosaic, the premultiplied bilinear accumulator, the position noise hash). Catalog metadata: `src/filters/filter_registry.{hpp,cpp}`.

## Two execution contracts

`FilterRegistry::apply(id, pixels)` is the legacy compatibility path: the original built-in function and its historical pixels. `FilterRegistry::apply(invocation, pixels)` is the named-parameter path used by dialogs, recipes, and previews; absent parameters use catalog defaults. The paths are deliberately separate; defaults differ for some filters (Posterize, Gaussian Blur, Clouds, Glowing Edges, some rounding paths). Never redirect the legacy wrapper through `default_invocation()` or change either output to make the two agree. Tests pin both. Where the two paths always produced the same bytes (Emboss, Pixel Mosaic, Radial Blur; the Smart Filter copies of those and of Box Blur), one kernel in `filter_kernels.cpp` serves every caller: the legacy wrapper calls it with its fixed arguments and a null progress, the engine and the smart renderer with theirs. A kernel body is never changed to make callers agree; `filter_legacy_and_named_defaults_share_kernels` and the `smart_filter_*_matches_destructive` tests pin the sharing.

## Stable identifiers and schemas

Built-in filter IDs are `patchy.filters.` plus the 41 suffixes below: persisted, append-only, never renamed or reused. Canonical catalog order; doubles as the version-1 parameter keys and defaults (used when a known parameter is missing):

```text
invert: amount=100
brightness_contrast: brightness=0 contrast=0
grayscale, desaturate, auto_tone, auto_contrast, auto_color: amount=100
soft_glow, punchy_color, noir, cinematic_matte, vintage_fade, sepia: amount=100
threshold: threshold=128
posterize: levels=4
box_blur: radius=1
sharpen: amount=100
unsharp_mask: amount=150 radius=2 threshold=8
gaussian_blur: radius=2.0
motion_blur: angle=0 distance=12
radial_blur: amount=35 samples=16 center_x=50.0 center_y=50.0
edge_detect: strength=100
emboss: angle=135 height=2 amount=100
glowing_edges: edge_width=2 brightness=140 smoothness=2
twirl: angle=180 radius=100 center_x=50.0 center_y=50.0
wave: amplitude=12 wavelength=48 phase=0
pinch_bloat: amount=35 radius=100 center_x=50.0 center_y=50.0
clouds: scale=96 detail=6 contrast=40 seed=1
pixelate: block_size=4
color_halftone: cell_size=10 intensity=75 contrast=60
film_grain: amount=50
add_noise: amount=12.5 distribution=uniform monochromatic=false seed=1
vignette: strength=55 center_x=50.0 center_y=50.0
high_pass: radius=10.0
median: radius=1.0
dust_and_scratches: radius=1 threshold=0
surface_blur: radius=5.0 threshold=15
lens_blur: radius=15.0 blades=6 blade_curvature=50 rotation=0
iris_blur: blur=15.0 center_x=50.0 center_y=50.0 angle=0 iris_width=50.0 iris_height=40.0 focus=50.0
tilt_shift_blur: blur=15.0 center_x=50.0 center_y=50.0 angle=0 focus_half_width=10.0 transition_width=20.0
plastic_wrap: highlight_strength=9 detail=7 smoothness=5
```

A `FilterInvocation` stores filter ID, schema version, named parameters, and captured foreground/background colors. Parameter keys are stable within a schema; unknown keys are ignored. An unknown filter ID or schema version makes the invocation and its containing recipe unsupported; never run the newest schema instead or fall back to another filter. Values normalize through the catalog before execution: declared types kept, numerics clamped to range; a wrong-typed known key or unknown option token is invalid, not coerced.

## Categories and UI contracts

Nine catalog filters carry `FilterCategory::Adjustment` and surface under Image > Adjustments: Invert, Brightness/Contrast, Grayscale (no direct action yet), Desaturate, Auto Tone, Auto Contrast, Auto Color, Threshold, Posterize. The four dialog adjustments (Levels, Curves, Hue/Saturation, Color Balance) are not catalog filters; see [adjustments-calibration.md](adjustments-calibration.md).

The gallery exposes the other 32 effects in the fixed catalog and category order below (labels translated, never locale-sorted). An effect's ID suffix is its lowercased label with underscores ("&" becomes "and"), except where given in parentheses:

```text
photo_looks: Soft Glow, Punchy Color, Noir, Cinematic Matte, Vintage Fade, Vintage Sepia (sepia), Lens Vignette (vignette)
blur: Box Blur, Gaussian Blur, Motion Blur, Radial Blur, Surface Blur, Lens Blur, Iris Blur, Tilt-Shift Blur
sharpen: Sharpen, Unsharp Mask, High Pass
distort: Twirl, Wave, Pinch/Bloat (pinch_bloat)
noise: Analog Grain (film_grain), Add Noise, Median, Dust & Scratches
pixelate: Pixel Mosaic (pixelate), Color Halftone
stylize: Edge Detect, Emboss, Glowing Edges
render: Clouds
artistic: Plastic Wrap
```

The category selector is `all`, `favorites`, then the nine tokens above; these eleven tokens and their order are settings contracts. Never persist a translated label or a `FilterCategory` ordinal.

The catalog generates dialog controls; existing Qt object names such as `filterAmountSpin` and `filterRadiusSlider` remain test contracts. Editors come from `FilterParameterDefinition` (types, units, defaults, ranges, object-name roots, option tokens) through the one shared `FilterParameterPanel` (`src/ui/filter_parameter_panel.{hpp,cpp}`); `FilterParameterPanelOptions` carries the gallery's presentation deltas.

`practical_minimum`/`practical_maximum` may narrow a linked slider without narrowing the semantic range; every spin box (gallery included), normalization, recipes, and persistence keep `minimum`/`maximum`. `FilterParameterPresentation` roles (an enum; not persisted, never replacing the parameter key or value) select specialized UI/render behavior: never key off parameter key, label, unit, or filter ID instead. The center, tilt width, and iris dimension roles drive the padding remaps under Spatial scaling and bounds.

Only eight catalog filter IDs are hotkey command IDs: Invert, Desaturate, Auto Tone, Auto Contrast, Auto Color, Brightness/Contrast, Threshold, Posterize. Catalog-generated direct Filter-menu actions are not HotkeyRegistry commands. Liquify uses `filter.liquify` (Ctrl+Shift+X). A catalog refactor must not silently add or remove commands.

Catalog names are canonical English translation sources (`QObject` context); submenu and status text keep the `MainWindow` context.

Liquify is a top-level Filter menu action under Filter Gallery, outside this catalog; see `docs/liquify.md`.

## Bit depth

`deep_filter_support` (declared in `filters/filter_engine.hpp`, defined in `filters/deep_filters.cpp`) decides how a catalog filter runs on a 16/32-bit layer: `Native` (float kernels in `deep_filters.cpp`), `EightBitPrecision` (16 bits only: the 8-bit filter runs on a narrowed copy and `apply_eight_bit_edit_at_depth` folds the change back), or `Unsupported` (32 bits: the menu action is disabled through its `patchy.filterIdentifier` property, the engine and script `applyFilter` throw). The Filter Gallery takes 16-bit layers, its previews drawn from display values, and is disabled at 32 bits like Liquify and Auto All. The per-filter list lives in [high-bit-depth.md](high-bit-depth.md).

## Text and shape layer gate

Text (`layer_is_text`) and shape (`layer_is_vector_shape`) layers re-create their pixels from source data, so a destructive pixel edit would vanish on the next edit. Every destructive entry point routes through `MainWindow::prompt_rasterize_procedural_layer` first: direct catalog filter actions (Image > Adjustments included), the Filter Gallery, Liquify, and the destructive Levels/Curves/Hue/Saturation/Color Balance dialogs. Legacy plug-ins keep their older hard refusal; smart objects are untouched (docs/smart-objects.md).

The prompt (`rasterizeOrConvertMessageBox`) offers Convert To Smart Object (AcceptRole, the default when present), Rasterize (DestructiveRole), and Cancel. Convert appears only when the follow-on can succeed: a direct filter needs a native Smart Filter mapping (`native_smart_filter_kind_for`), the gallery always qualifies, and both need the document under the 64-megapixel editable-mask cap; Liquify and the adjustment dialogs never offer it. Rasterize commits its own "Rasterize layer" undo step, which persists even when the follow-on dialog is cancelled (Photoshop behavior). Convert runs the standard Convert to Smart Object step and re-enters the smart-object routing. Cancel leaves the layer untouched. The gate commits any active inline text edit first; CLI automation keeps the plug-in path's status refusal instead of a blocking prompt. Coverage: `ui_filter*_on_*_layer_*`, `ui_levels_on_text_layer_*`.

## Auto adjustments

The three autos share one Qt-free kernel, `src/filters/auto_levels_math.{hpp,cpp}`, called by both execution contracts:

- Clip scan matches the Levels dialog Auto button: threshold `max(1, samples / 1000)` (0.1% per end), upward black scan, downward white scan bounded by `white > black + 1`. A scan that exhausts without exceeding the threshold leaves the channel unchanged (constant channels and 1x1 images are identity, as in Photoshop).
- Application uses a 256-entry LUT from the kernel's own copy of the levels transfer, matching core `levels_channel` rounding: Auto Tone equals committing the Levels dialog's Auto scan per channel. The transfer formula is deliberately per-consumer (`clamp_levels_record`, `core/adjustment_layer.hpp`).
- **Auto Tone** (`auto_tone`, Ctrl+Shift+L): per-channel clip scan and stretch; neutralizes color casts.
- **Auto Contrast** (`auto_contrast`, Ctrl+Alt+Shift+L): one merged R+G+B histogram, a single black/white pair, the same LUT on all three channels; casts survive.
- **Auto Color** (`auto_color`, Ctrl+Shift+B): the per-channel scan plus a neutral-midtone snap. The channel's whole-image mean, normalized into the stretched range, selects the integer `gamma_percent` in [10, 999] whose curve maps it closest to 128, ties preferring the smaller gamma. Every result is a Levels dialog state.
- Whole-layer analysis even with a selection; the wrapper restores unselected pixels afterward. Alpha untouched; UInt8 buffers with 3+ channels only.
- Menu, hotkey, and scripting invocations of the three autos apply immediately at the catalog defaults with no dialog. `amount` remains a catalog contract for recipes, Saved Looks, and explicit scripting invocations; `apply_filter` special-cases the immediate path (`filter_applies_without_settings_dialog`).
- **Auto All** (`imageAdjustAutoAllAction`, persisted command ID `image.auto_all`, no default shortcut) applies Tone, Contrast, then Color as one three-entry recipe pass and one undo step (`MainWindow::auto_all_adjustments`). A MainWindow command, not a catalog filter, so the eight-ID hotkey contract is unchanged. With a selection it follows the recipe contract (unselected pixels restore once at the end), which can differ from three sequential menu applies; with no selection the two are byte-identical.
- Photoshop calibration deltas: [adjustments-calibration.md](adjustments-calibration.md).

## Filter Gallery

`Filter > Filter Gallery...` is the shared browsing entry point; persisted hotkey command ID `filter.gallery`, no default shortcut. Direct Filter-menu actions stay fast paths with unchanged IDs, defaults, and output.

- Original is a UI sentinel (no filter ID, no invocation), always listed first. Real items carry their ID in `Qt::UserRole + 1` (Original: empty) and a session-only SF badge flag in `Qt::UserRole + 6`. Items are pre-created in catalog order and filtered in place. Thumbnail readiness is the ready role (`Qt::UserRole + 2`), never icon nullity.
- Search: case-insensitive, matching translated and English filter and category names. Hiding the selected filter returns selection to Original without clearing the recipe; clicking Original clears it, even when already selected by filtering.
- Favorites persist by filter ID in catalog order; loading drops missing/duplicate IDs and rewrites; toggling writes immediately (not undone by Cancel).
- Settings keys (prefix `filters/gallery/`): `favorites` (ordered list of valid IDs), `category` (one of the eleven tokens; unknown falls back to `all`), `lastFilterId` (empty = Original; restored only when still visible), `liveCanvasPreview` (bool), `size` (880x560..3200x2400, default 1120x720). Search, zoom, pan, and parameter edits are session-only.

Widget object names are test contracts, prefixed `filterGallery` (list in the dialog source); catalog-generated controls keep their catalog roots. The center/radius overlay lives inside `filterGalleryPreview`, not a separate child widget.

Outcome surfacing (inline, before Apply): an unlocalized "SF" thumbnail chip marks filters with a native Smart Filter mapping (`native_smart_filter_kind_for`, the single decision point); the localized explanation is the row tooltip. `filterGalleryOutcomeLabel` states what Apply will do; for a Smart Object it mirrors the caller's whole-recipe predicate (`smart_filter_stack_with_recipe`, which adds the 64-entry native stack cap); an empty recipe keeps the positive text. Rows whose single entry fails the real mapper (parameter gates like Emboss Amount 0 included) carry a warning icon and tooltip; mapping is all-or-nothing over disabled entries too, so disabling a blocking row clears nothing. A recipe rejected only by the entry cap flips the outcome line but marks no row.

The angle dial (Motion Blur, Emboss, Twirl, Lens Blur Rotation, Iris Blur, Tilt-Shift Blur) syncs with the numeric controls; its hand wraps visually but Twirl keeps the full -720..720 value. The Wave graph syncs amplitude (vertical drag), phase (horizontal drag), and wavelength (wheel). Both also appear in the direct dialogs (the Motion Blur Smart Filter dialog included) as `filterAngleDial` and `filterWaveformControl`.

Overlay controls:

- Radial Blur, Twirl, Pinch/Bloat, and Lens Vignette declare `center_x`/`center_y` with the center roles: a draggable crosshair (values quantize to the declared step). Twirl and Pinch/Bloat also mark integer `radius` as `EffectRadiusPercent`, adding a draggable radius circle. Drags update overlay and values immediately; a size-changing center proxy is adopted only on release so it cannot jump under the pointer.
- Tilt-Shift Blur adds the tilt width roles: center handle, rotation handle, and short draggable grip bars marking focus band edges (solid) and full-blur onset (dashed). The bars deliberately do not span the image: boundary lines dividing the image around the center are an Apple patent claim (US 8971623, docs/patent-research.md); `ui_filter_gallery_tilt_shift_overlay_uses_grip_bars` pins the short bars. Width handles edit both sides symmetrically; the proxy render defers until the gesture ends.
- Iris Blur uses the center and angle roles (crosshair and dial); Width, Height, and Focus stay numeric. Patchy draws no editable iris boundary widget and supports no multiple pins; the one ellipse is edited numerically and generates one scalar blend mask (docs/patent-research.md).

Preview pipeline:

- All preview work starts from an immutable copy of the active layer. Center and thumbnail proxies are bounded (`kFilterProxyMaximumDimension`, `kMaximumThumbnailDimension`), premultiplied bilinear, selection scaled alike. Only pixel-distance parameters scale (`FilterRegistry::scale`); canvas preview and Apply use the unscaled recipe.
- Thumbnails are single-filter previews; center and live-canvas previews render the whole recipe from the immutable original. Rendering traces input bounds for every entry (disabled and zero-opacity included); the active center/radius control maps from its entry's traced input rectangle into the displayed bounds. A selection fixes every entry to the original local bounds (canvas-filling exception below).
- A plain-layer recipe with an enabled, nonzero-opacity Clouds entry renders center and canvas previews through the full-resolution exact path from the embedded document-wide source (as for Smart Object targets, so preview matches Apply); other recipes keep the bounded proxy. Thumbnails use the exact renderer only for Smart Object targets.
- Thumbnails generate lazily, one visible filter per timer turn; editing a filter refreshes its row icon. The cache is session-local; view changes prioritize newly visible missing thumbnails, never invalidating completed ones.
- Live-canvas preview requests carry increasing generations: only the newest finished worker may update the canvas, only while the dialog is open; one runs at a time, the newest pending wins. Past the standard overlay delay an in-flight worker shows the "Rendering preview..." canvas badge (`CanvasWidget::begin/end_preview_render`, shared with the adjustment dialogs). The center preview follows the same rule on its own debounced worker.
- The momentary Before button shows the immutable source aligned to the current result bounds (zoom and pan kept) without touching the live canvas preview. Live Canvas Preview defaults on; re-enabling restores or reapplies the current result without changing selection.
- Cancel restores the original pixels and document-space bounds exactly, adds no undo entry, and does not dirty a clean document. Apply renders the recipe once more from the immutable original and commits one destructive transaction with one undo entry. Original, an empty recipe, or one with no enabled nonzero-opacity entries close without an undo entry.

## Direct filter dialog preview

`MainWindow::apply_filter` passes a `FilterDialogPreviewSource` (immutable pixels, bounds, selection, registry); `request_filter_settings` adds a bounded in-dialog proxy preview above the generated controls. A canvas-filling source carries the embedded document-wide buffer and union bounds, so proxy, canvas preview, and Apply agree; the pristine snapshot still drives Cancel. Without a source (the smart-filter editing dialogs) there is no preview.

The preview reuses the gallery machinery: `src/ui/filter_preview_proxy.{hpp,cpp}` (the bounded proxy, `render_filter_proxy`, the latest-generation worker) and `src/ui/filter_overlay_sync.{hpp,cpp}` (overlay geometry). The invocation renders as a debounced one-entry recipe; the traced input rectangle drives the same crosshair, radius circle, tilt-shift grip bars, and iris controls as the gallery. Overlay DRAWING stays solely in `zoomable_image_preview.cpp` (the grip bars are a patent design-around; never duplicate or alter that rendering).

Object names: `filterDialogPreview` (a `ZoomableImagePreview`; `filterDialogRenderedFilterId` mirrors the gallery's) and the `filterDialogZoomFit/100/Out/In/Label` zoom row. `filterPreviewCheck` gates only the live canvas preview; the in-dialog proxy always renders, display-only. Apply renders the unscaled invocation at full resolution.

## Spatial scaling and bounds

Proxies scale only pixel-distance parameters. Version-1 spatial keys are the catalog parameters declared `FilterSpatialScale::Pixels` (pinned by the catalog test). Angles, percentages, samples, intensity, detail, seed, and colors never scale. Scaling returns a normalized copy.

Ranges (`min..max`, practical slider limits in parentheses), growth, and translation support ("supp"); defaults are in the ID list above; unlisted filters neither grow nor advertise fixed support:

```text
box_blur  radius int 1..2000 px (to 100); grows by radius; supp = radius
gaussian_blur  radius 0.1..1000 px (to 100); grows/supp ceil(3*radius)
sharpen, edge_detect  supp 1 px
motion_blur  angle -360..360 deg (-180..180), distance 1..2000 px (1..64, PS max); grows by distance;
             supp distance+1 (+1 covers bilinear taps); kernels in smart-filters-native.md
radial_blur  amount 0..100, samples, center; growth notes below; supp none
add_noise  amount 0.1..400 % (to 100), seed 0..999999999; bounds/alpha byte-identical; no growth/supp
unsharp_mask  amount 1..500 %, radius 0.1..1000 px (to 100), threshold 0..255; no growth; supp ceil(3*radius)
high_pass  radius 0.1..1000 px (to 100); bounds/alpha kept; supp 3*radius
median  radius 1..500 px (full range); bounds kept; supp none
dust_and_scratches  radius int 1..500 (full range, PS dialog max), threshold 0..255; bounds/alpha kept; supp none
surface_blur  radius 1..100 px, 0.01 steps (to 25), threshold 2..255; grows <= effective radius; supp none
lens_blur  radius 0..100 px (to 50), blades 3..8, curvature 0..100 %, rotation -180..180 deg; supp none
iris_blur  blur 0..500 px (to 50), center 0..100 %, angle -180..180 deg, width/height 1..200 % of input
           W/H, focus 0..100 % of ellipse radius; Lens Blur's growth; supp none
tilt_shift_blur  blur 0..500 px (to 50), center/focus_half_width/transition_width %; grows <= ceil(blur); supp none
plastic_wrap  highlight_strength 0..20, detail 1..15, smoothness 1..15; bounds/alpha byte-exact; supp none
emboss  angle -360..360 deg (-180..180), height 1..100 px (1..24), amount 0..500 % (0..300; native minimum 1)
clouds  fills_entire_canvas; no margin, no supp, never grown by the registry (UI embed below)
```

Calibration notes:

- Radial Blur: historical centered growth kept for default compatibility; an edited center grows from the sampled corner sweep, uncapped, failing through the registry's checked padding path rather than clipping. Amount 0: no growth. The native Smart Filter mapping (default center, amount 1..100, quality-tier samples, Spin only) is in [smart-filters-native.md](smart-filters-native.md).
- Add Noise: deterministic position-hashed noise on RGB only, uniform or a sum-of-four-uniforms gaussian approximation with no transcendental calls. The seed feeds the hash so re-renders reproduce the same noise; amount does not scale for thumbnails (like Analog Grain).
- Box Blur: radii to 12 keep the direct double path (pins, Smart Filter parity); larger ones use an exact int64 running sum.
- Gaussian Blur, High Pass, Unsharp Mask, Median, Dust & Scratches, and Surface Blur share their kernels with the native Smart Filters (`render_photoshop_*` in `smart_filter_renderer.cpp`); the exact math is in [smart-filters-native.md](smart-filters-native.md) "Render semantics". Gaussian's `accepts_legacy_integer` widens pre-decimal integer radii; Median floors and Surface Blur rounds fractional radii for rendering without rewriting the stored value.
- Median, Dust & Scratches, and Surface Blur give transparent pixels the straight RGB of the nearest visible source anywhere in the input, so these, the ellipse/band blurs, Plastic Wrap, and Add Noise advertise no finite support and selected application renders with full-layer context. Surface Blur alpha-trims after padding.
- Lens Blur: one deterministic supersampled aperture kernel on premultiplied RGBA; curvature blends the polygon toward a circle; radius 0 is an exact identity; large radii use fixed-point downsample/convolve/upsample stages. A factor-aligned transparent margin is reserved and alpha-trimmed; the multiscale grid anchors to the whole input rectangle.
- Iris Blur: one fixed round aperture blur, premultiplied-blended with the original through one smooth elliptical mask; Focus interior sharp, outside the ellipse fully blurred; blur 0 exact identity. No depth inference, highlight detection, per-pixel kernels, or combined blur patterns.
- Tilt-Shift Blur: angle 0 means horizontal focus lines; focus band sharp, cubic transition, full blur beyond the dashed boundaries; blur 0 exact identity; alpha-trimmed after padding.
- Plastic Wrap: one fixed integer height-field formula (edge-clamped box smoothing of alpha-weighted luminance, local gradients, constant-direction relief plus ridge highlights added to the original RGB); alpha weighting gives isolated artwork contour relief. Dimensionless, no thumbnail scaling.

When rendering expands an RGBA layer the registry pads every side first, so centers are remapped to the padded buffer, per axis:

```text
padded_percent = 100 * (margin + (original_extent - 1) * percent / 100)
                 / (original_extent + 2 * margin - 1)
```

Selected by the center roles, default 50.0 centers included; never run a centered effect on a padded buffer with the unadjusted percentage. Tilt-Shift focus and transition widths are percentages of the shorter input extent; padding multiplies both by `original_shorter / padded_shorter`. Iris Width/Height are percentages of input width/height; padding scales each by its own original/padded ratio.

The UI wrapper decides expansion: a selected operation stays inside the layer bounds; with no selection an RGBA layer may grow, then trims transparent borders. Preview, Cancel, Apply, Undo, and Redo restore pixels and document-space bounds.

Canvas-filling filters are the one exception: the UI wrapper first embeds the layer into a transparent buffer covering the union of layer bounds and canvas rectangle (never a canvas crop) and renders across it; with a selection, outside pixels restore as usual, so clouds can appear in selected regions that had no layer content. The result is alpha-trimmed against the original bounds: a selected render grows the layer only to the union of old content and selection, and a fully transparent result returns to the original rectangle. No-alpha layers, effective transparent-pixel locks, and layers already covering the canvas skip the embed (content-rect render). Cancel restores pristine pixels and bounds, never the embedded buffer. Clouds has no native Smart Filter mapping; on a Smart Object the direct action and a Clouds recipe both reach the rasterize prompt, then run this destructive path.

## Captured colors

Foreground and background colors are copied into every invocation at creation. Clouds reads the captured colors; re-rendering a recipe must not depend on the toolbar swatches later.

## Recipes

`FilterRecipe` stores entries in execution order: invocation, enabled state, opacity, blend mode. Disabled entries are skipped. An unsupported invocation makes the whole recipe unsupported, even when disabled: enabling it later must not substitute a result. Opacity is a finite double in [0, 1], else unsupported. Defaults: enabled, opacity 1, Normal. Execution is deterministic from the supplied immutable source; never build a preview cumulatively from an earlier preview.

The applied-effects list shows the final effect at the top, opposite the stored execution order, so a reordered list rebuilds the recipe from the bottom visual row up. Each dialog entry has a transient numeric identity (never persisted) so duplicates stay independent.

Selecting a catalog filter creates the first entry or replaces the active entry's invocation, keeping its enabled state and blending. Duplicate inserts a copy after the active entry. Per-entry blend mode and opacity are edited below the list; Saved Looks persist them and the Smart Filter mapping copies them into native entries. Reset returns the active entry to Normal at 100% and default parameters. Remove selects the nearest remaining row; Original clears the recipe. Only applied-stack operations mutate the recipe; category, search, and Favorites changes do not. Effect rows are drag sources, not drop targets: a drop inserts between rows and emits the canonical row-move signal.

Recipe scaling returns a normalized copy that keeps order, enable state, blending, and captured colors. Aggregate translation support is the checked sum over enabled nonzero-opacity entries; an executed entry with unknown support makes the aggregate unknown. Zero-opacity entries are validated for persistence but do not execute, expand bounds, report progress, or affect aggregate support.

With a selection, the whole recipe runs against one immutable source and the wrapper restores outside pixels once after the final entry; per-entry restoration would change spatial results near the selection edge. A fully transparent expanded result returns to the input rectangle.

## Saved Looks

User Looks live at `<settings dir>/looks/<uuid>.json`; the lowercase canonical UUID is the stable preset ID and filename stem. Save creates a fresh UUID, Rename keeps it, Delete removes the record. Library operations apply immediately; gallery Cancel does not roll them back.

Version-1 record shape: `{version, id, name, recipe: {entries: [{enabled, opacity, blendMode, invocation: {filterId, schemaVersion, parameters: {key: {type, value}}, foreground: {red, green, blue}, background}}]}}`. Parameter values carry an explicit `integer`, `double`, `boolean`, or `string` type. Blend modes use the stable full Photoshop descriptor string tokens, never enum ordinals or translated names. Entry array order is execution order; colors are required even when unused.

Writes use `QSaveFile`; memory state changes only after the atomic commit. Loading is strict: 1 MiB per file, 64 entries per recipe, 64 parameters per entry. Rejected: malformed JSON, unsupported record versions, filename/record UUID mismatch, invalid UTF-8, invalid value types, non-finite or out-of-range opacity, invalid colors, unknown blend tokens. Unknown filter IDs and schema versions stay structurally valid: listed disabled with an unsupported tooltip, preserved, never substituted. A malformed record is skipped without touching neighbors.

## Regression coverage

Keep separate coverage for: legacy wrapper output per built-in ID; version-1 defaults and explicit parameters; the exact catalog (IDs, categories, parameters, scaling, bounds); missing or unknown parameters, unsupported IDs and schemas, recipe ordering; captured Clouds colors and the canvas-filling contract; progress and cancellation; menu, action and hotkey contracts; selection, expanding bounds, Cancel, one-step Undo/Redo; the auto adjustments; the all-filter contact sheet.

Never re-pin an output canary as part of a structural refactor. Capture the contact-sheet SHA-256 from a full pre-refactor suite run of the same tree, then show the refactored path reproduces the old focused outputs and that SHA. Never compare against a SHA from an older commit: the sheet changes whenever filters are added.
