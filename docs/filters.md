# Visual filters and recipes

This document is the implementation contract for Patchy's built-in pixel filters, the shared filter catalog, and ordered filter recipes. The gallery and Smart Filter work build on these types, so persisted names and default behavior are compatibility surfaces. Kernels: `src/filters/builtin_filters.cpp`, `filter_engine.cpp`. Catalog metadata (types, units, steps, ranges): `src/filters/filter_registry.{hpp,cpp}`.

## Two execution contracts

`FilterRegistry::apply(id, pixels)` is the legacy compatibility path: the original built-in function and its historical pixels. `FilterRegistry::apply(invocation, pixels)` is the named-parameter path used by dialogs, recipes, and previews; absent parameters use catalog defaults.

The paths are intentionally separate; defaults are not equivalent for every filter (Posterize, Gaussian Blur, Clouds, Glowing Edges, some rounding paths). Never redirect the legacy wrapper through `default_invocation()` or change either output to make the two agree. Tests pin both independently.

## Stable identifiers and schemas

Built-in filter IDs are `patchy.filters.` plus the 41 suffixes below: persisted, append-only, never renamed or reused. The list is in canonical catalog order and doubles as the version-1 parameter keys and defaults (used when a known parameter is missing):

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
gaussian_blur: radius=2
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

A `FilterInvocation` stores filter ID, schema version, named parameters, and captured foreground/background colors. Schema version 1 is the initial catalog schema. Parameter keys are stable within a schema; unknown keys are ignored. An unknown filter ID or unsupported schema version makes the invocation and its containing recipe unsupported; never run the newest schema instead or fall back to a different filter. Values normalize through the catalog before execution: declared types (integer, double, boolean, stable string option) kept, numerics clamped to range; a wrong-typed known key or unknown option token is invalid, not coerced.

## Categories and UI contracts

Nine catalog filters carry `FilterCategory::Adjustment` and surface under Image > Adjustments: Invert, Brightness/Contrast, Grayscale (no direct action yet), Desaturate, Auto Tone, Auto Contrast, Auto Color, Threshold, Posterize. The menu also holds the four dialog adjustments (Levels, Curves, Hue/Saturation, Color Balance), which are not catalog filters; see [adjustments-calibration.md](adjustments-calibration.md).

The gallery exposes the other 32 effects in the fixed catalog and category order below. Labels are translated; order is never locale-sorted. An effect's ID suffix is its lowercased label with underscores ("&" becomes "and"), except where given in parentheses:

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

The category selector is `all`, `favorites`, then the nine tokens above. These eleven tokens and their order are settings compatibility surfaces. Never persist a translated label or a `FilterCategory` ordinal.

Liquify is a top-level Filter menu action directly under Filter Gallery, deliberately outside this catalog; see `docs/liquify.md`.

## Text and shape layer gate

Text (`layer_is_text`) and shape (`layer_is_vector_shape`) layers re-create their pixels from their source data, so a destructive pixel edit would silently vanish on the next text/shape edit. Every destructive entry point therefore routes through `MainWindow::prompt_rasterize_procedural_layer` before touching pixels: direct catalog filter actions (Image > Adjustments included), the Filter Gallery, Liquify, and the destructive Levels/Curves/Hue/Saturation/Color Balance dialogs. Legacy plug-ins keep their older hard refusal, and smart objects are untouched by this gate (their routing lives in docs/smart-objects.md).

The prompt (`rasterizeOrConvertMessageBox`) offers Convert To Smart Object (AcceptRole, the default when present), Rasterize (DestructiveRole), and Cancel. Convert appears only when the follow-on path can succeed, never as a dead end: a direct filter must have a native Smart Filter mapping (`native_smart_filter_kind_for`), the gallery always qualifies, and both also require the document to fit the 64-megapixel editable-mask cap; Liquify and the adjustment dialogs never offer it because they refuse smart objects. Choosing Rasterize commits its own "Rasterize layer" undo step before the filter flow continues, and deliberately persists even when the follow-on dialog is then cancelled (Photoshop behavior). Choosing Convert runs the standard Convert to Smart Object step on just that layer and the flow re-enters the normal smart-object routing (editable Smart Filter dialog or the gallery's smart-filter target path). Cancel reports "Cancelled %1" and leaves the layer untouched. The gate commits any active inline text edit first, and CLI automation keeps the plug-in path's status refusal instead of a blocking prompt. Coverage: the `ui_filter_on_text_layer_*`, `ui_filter_on_shape_layer_*`, `ui_filter_gallery_on_shape_layer_*`, and `ui_levels_on_text_layer_*` tests.

The catalog generates dialog controls, but existing Qt object names such as `filterAmountSpin` and `filterRadiusSlider` remain test contracts. Editors are generated from `FilterParameterDefinition`, which owns types, units, defaults, ranges, object-name roots, and option tokens. Direct dialogs and the gallery build standard controls through the one shared `FilterParameterPanel` (`src/ui/filter_parameter_panel.{hpp,cpp}`); `FilterParameterPanelOptions` carries the deliberate presentation deltas (gallery PlusMinus spin buttons, 84 px double spins, practical-range integer spins).

`practical_minimum`/`practical_maximum` may narrow a linked slider without narrowing the semantic range; the spin box, normalization, recipes, and persistence keep `minimum`/`maximum`. Per-filter ranges are under Spatial scaling and bounds.

`FilterParameterPresentation` roles (an enum; not persisted, never replacing the parameter key or value) select specialized UI/render behavior: never key off parameter key, label, unit, or filter ID instead. The center, tilt width, and iris dimension roles drive the padding remaps under Spatial scaling and bounds.

Only eight catalog filter IDs are registered hotkey command IDs: Invert, Desaturate, Auto Tone, Auto Contrast, Auto Color, Brightness/Contrast, Threshold, Posterize. Catalog-generated direct Filter-menu actions are not HotkeyRegistry commands. Liquify uses the persisted command ID `filter.liquify` (Ctrl+Shift+X). A catalog refactor must not silently add or remove commands.

Catalog names are canonical English translation sources (`QObject` context); submenu and action status text keep the `MainWindow` context.

## Auto adjustments

The three autos share one Qt-free kernel, `src/filters/auto_levels_math.{hpp,cpp}`, called byte-identically by both execution contracts:

- Clip scan matches the Levels dialog Auto button: threshold `max(1, samples / 1000)` (0.1% per end), upward black scan, downward white scan bounded by `white > black + 1`. A scan that exhausts without exceeding the threshold leaves the channel unchanged, so constant channels and 1x1 images are identity (matches Photoshop).
- Application uses a 256-entry LUT from the kernel's own copy of the levels transfer, matching core `levels_channel` rounding (float round-trip included): Auto Tone equals committing the Levels dialog's Auto scan per channel. The transfer formula is deliberately per-consumer (see `clamp_levels_record` in `core/adjustment_layer.hpp`).
- **Auto Tone** (`auto_tone`, Ctrl+Shift+L): per-channel clip scan and stretch; neutralizes color casts.
- **Auto Contrast** (`auto_contrast`, Ctrl+Alt+Shift+L): one merged R+G+B histogram, a single black/white pair, the same LUT on all three channels; casts survive. The composite stretch deliberately replaced the per-channel original (August 2026); ID unchanged, affected pins re-derived.
- **Auto Color** (`auto_color`, Ctrl+Shift+B): the per-channel scan plus a neutral-midtone snap. The channel's whole-image mean, normalized into the stretched range, selects the integer `gamma_percent` in [10, 999] whose curve maps it closest to 128, ties preferring the smaller gamma. Every result is a state the Levels dialog can represent.
- Whole-layer analysis even with a selection; the wrapper restores unselected pixels afterward. Alpha untouched; buffers must be UInt8 with 3+ channels.
- Menu, hotkey, and scripting command invocations of the three autos apply immediately at the catalog defaults with no settings dialog (August 2026). The `amount` parameter remains a catalog contract for recipes, Saved Looks, and explicit scripting invocations; `apply_filter` special-cases the immediate path (`filter_applies_without_settings_dialog`).
- **Auto All** (`imageAdjustAutoAllAction`, persisted command ID `image.auto_all`, no default shortcut) applies Tone, then Contrast, then Color as one three-entry recipe pass and a single undo step (`MainWindow::auto_all_adjustments`). It is a MainWindow command, not a catalog filter, so the eight-ID hotkey contract above is unchanged. With a selection it follows the recipe contract: whole-layer analysis chains across the entries and unselected pixels restore once at the end, which can differ from three sequential menu applies; with no selection the two are byte-identical.
- Photoshop calibration deltas (bigger effective clip thresholds on small images): [adjustments-calibration.md](adjustments-calibration.md).

## Filter Gallery

`Filter > Filter Gallery...` is the shared browsing entry point; persisted hotkey command ID `filter.gallery`, no default shortcut. Direct Filter-menu actions stay fast paths; IDs, defaults, selection behavior, and output unchanged.

- Original is a UI sentinel (no filter ID, no invocation), always listed first. Real items carry their ID in `Qt::UserRole + 1` (Original: empty) and a session-only SF badge flag in `Qt::UserRole + 6`. Items are pre-created in catalog order and filtered in place. Thumbnail readiness is the ready role (`Qt::UserRole + 2`), never icon nullity.
- Search: case-insensitive, localized; matches translated/English filter names and category names/tokens (underscores as spaces). Hiding the selected filter returns selection to Original without clearing the recipe; explicitly clicking Original clears it, even when already selected by filtering.
- Favorites persist by filter ID in catalog order; loading drops missing/duplicate IDs and rewrites; toggling writes immediately (not undone by Cancel).
- Settings keys (prefix `filters/gallery/`): `favorites` (ordered list of valid IDs), `category` (one of the eleven tokens; unknown falls back to `all`), `lastFilterId` (empty = Original; restored only when still visible), `liveCanvasPreview` (bool), `size` (880x560..3200x2400, default 1120x720). Search, zoom, pan, and parameter edits are session-only.

Widget object names are test contracts, all prefixed `filterGallery`; the full list lives in the dialog source. Catalog-generated controls keep their catalog object-name roots. The center/radius overlay lives inside `filterGalleryPreview`, not a separate child widget.

Outcome surfacing (inline, before Apply): an "SF" thumbnail chip marks filters with a native Smart Filter mapping; the source is `native_smart_filter_kind_for` (`src/filters/smart_filter_recipe_mapping.hpp`, the single decision point). The "SF" glyph is deliberately unlocalized; the localized explanation is the row tooltip, fixed while the dialog is open (the `GalleryTargetContext` target kind cannot change). `filterGalleryOutcomeLabel` states what Apply will do; for a Smart Object it mirrors the caller's whole-recipe predicate (`smart_filter_stack_with_recipe`, which adds the 64-entry native stack cap; default `smart_filter_entries_from_recipe`); an empty recipe keeps the positive text (Original bakes nothing). Rows whose single entry fails the real mapper (parameter gates like Emboss Amount 0 included) carry a warning icon and tooltip; mapping is all-or-nothing over disabled entries too, so disabling a blocking row clears nothing. A recipe rejected only by the entry cap flips the outcome line but marks no row.

The angle dial appears for Motion Blur, Emboss, Twirl, Lens Blur Rotation, Iris Blur, and Tilt-Shift Blur, synced with the numeric controls; its hand wraps visually but Twirl keeps the full -720..720 value. The Wave graph syncs amplitude (vertical drag), phase (horizontal drag), and wavelength (wheel). Both companions also appear in the direct dialogs (including the Motion Blur Smart Filter dialog's angle control) under the same object names, `filterAngleDial` and `filterWaveformControl`.

Overlay controls:

- Radial Blur, Twirl, Pinch/Bloat, and Lens Vignette declare `center_x`/`center_y` with the center roles; the preview draws a draggable crosshair (drag values quantize to the declared step). Twirl and Pinch/Bloat also mark integer `radius` as `EffectRadiusPercent`, adding a draggable radius circle. Drags update overlay and values immediately; a size-changing center proxy is adopted only on release so it cannot jump under the pointer.
- Tilt-Shift Blur adds the tilt width roles: center handle, rotation handle, and short draggable grip bars marking focus band edges (solid) and full-blur onset (dashed). The bars deliberately do not span the image: boundary lines dividing the image around the center are an Apple patent claim (US 8971623, see docs/patent-research.md), and `ui_filter_gallery_tilt_shift_overlay_uses_grip_bars` pins the short-bar rendering. Width handles edit both sides symmetrically; the proxy render defers until the gesture ends.
- Iris Blur uses the center and angle roles (crosshair and dial); Iris Width, Height, and Focus stay numeric. Patchy deliberately draws no editable iris boundary widget and supports no multiple pins; the one explicit ellipse is edited numerically and generates one scalar blend mask (see docs/patent-research.md).

Preview pipeline:

- All preview work starts from an immutable copy of the active layer. Center proxy max dimension 640 px, thumbnail proxy 180 px; premultiplied bilinear, selection scaled alike. Only pixel-distance parameters scale (`FilterRegistry::scale`). Canvas preview and Apply always use the unscaled recipe at full resolution.
- Catalog thumbnails are single-filter previews; center and live-canvas previews render the complete recipe from the immutable original. Rendering traces input bounds for every entry (disabled and zero-opacity included); the active center/radius control maps from its entry's traced input rectangle into the displayed bounds, staying accurate under expansion. A selection fixes every entry to the original local bounds (canvas-filling exception below).
- A plain-layer recipe with an enabled, nonzero-opacity Clouds entry renders center and canvas previews through the full-resolution exact path from the embedded document-wide source (as for Smart Object targets; preview matches Apply); other recipes keep the bounded proxy byte-identical. Thumbnails use the exact renderer only for Smart Object targets.
- Thumbnails are 128x78, generated lazily one visible filter per timer turn; editing a filter refreshes its row icon. The cache is session-local; view changes prioritize newly visible missing thumbnails, never invalidating completed ones.
- Live-canvas preview requests carry increasing generations: only the newest finished worker may update the canvas, only while the dialog is open; one runs at a time, the newest pending wins, closing invalidates the rest. Past the standard overlay delay an in-flight worker shows a "Rendering preview..." spinner badge on the canvas (`CanvasWidget::begin/end_preview_render`), shared with the destructive adjustment dialogs and the smart-filter editor. The center preview follows the same rule on its own worker, debounced.
- The momentary Before button shows the immutable source aligned to the current result bounds (zoom and pan preserved while held) without touching the live canvas preview. Live Canvas Preview defaults on; re-enabling restores or reapplies the current result without changing selection.
- Cancel restores the layer's original pixels and document-space bounds exactly, adds no undo entry, and does not dirty a clean document. Apply renders the recipe once more from the immutable original and commits one destructive transaction with one undo entry. Original, an empty recipe, or one with no enabled nonzero-opacity entries close without an undo entry.

## Direct filter dialog preview

`MainWindow::apply_filter` passes a `FilterDialogPreviewSource` (immutable pixels, bounds, selection, registry); `request_filter_settings` adds a bounded in-dialog proxy preview above the generated controls. A canvas-filling source carries the embedded document-wide buffer and union bounds, so proxy, canvas preview, and Apply agree; the pristine snapshot still drives Cancel. Without a source (the smart-filter editing dialogs) the dialog keeps its lightweight layout, no preview.

The preview reuses the gallery machinery: `src/ui/filter_preview_proxy.{hpp,cpp}` (the 640 px proxy, `render_filter_proxy`, the latest-generation worker) and `src/ui/filter_overlay_sync.{hpp,cpp}` (overlay geometry/sync math). The invocation renders as a one-entry recipe with a 35 ms debounce; the traced input rectangle drives the same center crosshair, radius circle, tilt-shift grip bars, and iris controls as the gallery. Overlay DRAWING stays solely in `zoomable_image_preview.cpp` (the tilt-shift grip bars are a patent design-around; never duplicate or alter that rendering).

Object names: `filterDialogPreview` (a `ZoomableImagePreview`; its `filterDialogRenderedFilterId` property mirrors the gallery's) and the `filterDialogZoomFit/100/Out/In/Label` zoom row. `filterPreviewCheck` gates only the live canvas preview; the in-dialog proxy always renders and is display-only. Apply still renders the unscaled invocation at full resolution.

## Spatial scaling and bounds

Proxies scale only pixel-distance parameters. Version-1 spatial keys: `radius` of box_blur, gaussian_blur, unsharp_mask, high_pass, median, dust_and_scratches, surface_blur, lens_blur; `blur` of iris_blur, tilt_shift_blur; motion_blur `distance`; emboss `height`; glowing_edges `edge_width` and `smoothness`; wave `amplitude` and `wavelength`; clouds `scale`; pixelate `block_size`; color_halftone `cell_size`. Angles, percentages, samples, intensity, detail, seed, and colors never scale. Scaling returns a normalized copy; the original invocation is never mutated.

Ranges, growth, and translation support ("supp"). Ranges are `min..max` with practical slider limits in parentheses; defaults are in the ID list above; filters not listed neither grow nor advertise fixed support:

```text
box_blur, gaussian_blur  grows by radius; supp = radius
sharpen, edge_detect  supp 1 px
motion_blur  angle -360..360 deg (-180..180), distance 1..999 px (1..64); grows by distance; supp distance+1
             (one fixed premultiplied-alpha line kernel; the +1 covers bilinear sampling)
radial_blur  amount 0..100, samples, center; growth notes below; supp none
add_noise  amount 0.1..400 % (to 100), seed 0..999999999; bounds/alpha byte-identical; no growth/supp
unsharp_mask  amount 1..500 %, radius 0.1..1000 px (to 12), threshold 0..255; no growth; supp ceil(3*radius)
high_pass  radius 0.1..1000 px (to 12); bounds/alpha kept; supp 3*radius
median  radius 1..500 px (full range); bounds kept; supp none
dust_and_scratches  radius int 1..500 (full range, PS dialog max), threshold 0..255; bounds/alpha kept; supp none
surface_blur  radius 1..100 px, 0.01 steps (to 25), threshold 2..255; grows <= effective radius; supp none
lens_blur  radius 0..100 px (to 50), blades 3..8, curvature 0..100 %, rotation -180..180 deg; supp none
iris_blur  blur 0..100 px (to 50), center 0..100 %, angle -180..180 deg, width/height 1..200 % of input
           W/H, focus 0..100 % of ellipse radius; Lens Blur's growth; supp none
tilt_shift_blur  blur 0..500 px (to 50), center/focus_half_width/transition_width %; grows <= ceil(blur); supp none
plastic_wrap  highlight_strength 0..20, detail 1..15, smoothness 1..15; bounds/alpha byte-exact; supp none
emboss  angle -360..360 deg (-180..180), height 1..100 px (1..24), amount 1..500 % (0..300)
clouds  fills_entire_canvas; no margin, no supp, never grown by the registry (UI embed below)
```

Calibration notes:

- Radial Blur: historical centered growth kept for exact default compatibility; an edited center grows from the actual sampled corner sweep, uncapped, failing through the registry's checked padding path rather than clipping. Amount 0: no growth. Native Smart Filter mapping requires the default 50/50 center (Photoshop's `RdlB` descriptor stores no center), amount 1..100, and samples exactly on a quality tier (Draft 8, Good 16, Best 32); Photoshop's Zoom method is deliberately unsupported and stays preview-locked.
- Add Noise: deterministic position-hashed noise on RGB only, uniform or a sum-of-four-uniforms gaussian approximation with no transcendental calls (cross-toolchain stable). The seed feeds the hash so re-renders reproduce the same noise; amount deliberately does not scale for thumbnails (like Analog Grain).
- Unsharp Mask: Photoshop scales the signed detail before subtracting Threshold from its magnitude; the radius-2.5 low-pass has its own measured byte kernel, not Gaussian Blur's.
- Median: fractional radii floor for rendering without rewriting the stored value. Transparent pixels borrow straight RGB from the nearest visible source anywhere in the input (the shared nearest-visible extension; also Dust & Scratches, Surface Blur); hence these, the ellipse/band blurs, Plastic Wrap, and Add Noise advertise no finite support, and selected application renders with full-layer context.
- Dust & Scratches: square per-channel RGB median over the extension; replaces the whole RGB triplet only when its maximum channel difference from the source is strictly greater than Threshold.
- Surface Blur: effective integer radius `max(1, floor(radius + 0.5))`. Per channel over a square edge-clamped window, each sample `v` around center `c` weighs `max(0, 5 * threshold - 2 * abs(v - c))`; the weighted quotient rounds to nearest, ties to even. RGB uses the extension; alpha runs the same formula directly. Alpha-trimmed after padding.
- Lens Blur: one deterministic supersampled aperture kernel on premultiplied RGBA; curvature blends the polygon toward a circle; radius 0 is an exact identity; large radii use deterministic fixed-point downsample/convolve/upsample stages. A factor-aligned transparent margin is reserved and alpha-trimmed; the multiscale grid anchors to the whole input rectangle.
- Iris Blur: one fixed round aperture blur, premultiplied-blended with the original through one deterministic smooth elliptical mask; Focus interior sharp, outside the ellipse fully blurred; blur 0 exact identity. It does not infer depth, detect or boost highlights, vary a kernel per pixel, or combine multiple blur patterns.
- Tilt-Shift Blur: angle 0 means horizontal focus lines; focus band sharp, deterministic cubic transition, full requested blur beyond the dashed boundaries; blur 0 exact identity; alpha-trimmed after padding.
- Plastic Wrap: one fixed integer height-field formula (edge-clamped box smoothing of alpha-weighted luminance, local gradients, constant-direction relief plus ridge highlights added to the original RGB); alpha weighting gives isolated artwork contour relief. Dimensionless, no thumbnail scaling; regressions pin visible treatment at the defaults.

When rendering expands an RGBA layer the registry pads every side first, so centers must be remapped to the padded buffer. Per axis:

```text
padded_percent = 100 * (margin + (original_extent - 1) * percent / 100)
                 / (original_extent + 2 * margin - 1)
```

Selected by the center roles; applies to default 50.0 centers too. Never run a centered effect on a padded buffer with the unadjusted percentage.

Tilt-Shift focus and transition widths are percentages of the shorter input extent; padding multiplies both by `original_shorter / padded_shorter`. Iris Width/Height are percentages of input width/height; padding scales each by its own original/padded ratio. The center roles preserve the band and ellipse origins.

The UI wrapper decides expansion: a selected operation stays inside the layer bounds; with no selection an RGBA layer may grow and then trim transparent borders. Preview, Cancel, Apply, Undo, and Redo must restore both pixels and document-space bounds.

Canvas-filling filters are the one exception: the UI wrapper first embeds the layer into a transparent buffer covering the union of layer bounds and canvas rectangle (never a canvas crop) and renders across the whole buffer; with a selection, outside pixels restore as usual, so clouds can appear in selected regions that had no layer content. The result is alpha-trimmed against the original bounds: a selected render grows the layer only to the union of old content and selection, and a fully transparent result returns to the original rectangle. The embed is skipped (historical content-rect render) for no-alpha layers, effective transparent-pixel locks, or layers already covering the canvas (byte-identical to pre-embed output). Cancel restores pristine pixels and bounds, never the embedded buffer. Clouds has no native Smart Filter mapping; on a Smart Object the direct action and a Clouds recipe both reach the rasterize prompt, then run this destructive path.

## Captured colors

Foreground and background colors are copied into every invocation at creation. Clouds reads the captured colors; re-rendering a recipe must not depend on the toolbar swatches at that later time.

## Recipes

`FilterRecipe` stores entries in execution order: invocation, enabled state, opacity, blend mode. Disabled entries are skipped. An unsupported invocation makes the whole persisted recipe unsupported, even when disabled, because enabling it later must not produce a substituted result.

Opacity is a finite double in [0, 1]; out-of-range or non-finite opacity makes the recipe unsupported. Defaults: enabled, opacity 1, Normal blend mode. Execution is deterministic from the supplied immutable source; never build a preview cumulatively from an earlier preview.

The applied-effects list displays the final effect at the top, opposite the stored execution order, so reading a reordered list rebuilds the recipe from the bottom visual row up. Each dialog entry has a transient numeric identity (never persisted) so duplicates stay independent.

Selecting a catalog filter creates the first entry or replaces the active entry's invocation, keeping its enabled state and blending. Duplicate inserts a copy right after the active entry in execution order. Per-entry blend mode and opacity are edited below the list; Saved Looks persist them and the Smart Filter mapping copies them into native entries. Reset returns the active entry to Normal at 100% and default parameters. Remove selects the nearest remaining visual row; Original clears the recipe. Only applied-stack operations mutate the recipe; category, search, and Favorites changes do not. Effect rows are drag sources, not drop targets, so a drop inserts between rows and emits the canonical row-move signal.

Recipe scaling returns a normalized copy retaining order, enable state, blending, and captured colors. Aggregate translation support is the checked sum over enabled nonzero-opacity entries; any executed entry with unknown support makes the aggregate unknown. Zero-opacity entries are still validated for persistence but do not execute, expand bounds, report progress, or affect aggregate support.

With a selection, the complete recipe runs against one immutable source and the wrapper restores outside pixels once after the final entry; per-entry restoration would change spatial results near the selection edge and is not allowed. A fully transparent expanded result returns to the input rectangle instead of growing empty bounds per filter.

## Saved Looks

User Looks live at `<settings dir>/looks/<uuid>.json`; the lowercase canonical UUID is the stable preset ID and filename stem. Save creates a fresh UUID, Rename keeps it, Delete removes the record. These library operations apply immediately and are not rolled back by gallery Cancel.

Version-1 record shape:

```json
{"version": 1, "id": "01234567-89ab-4cde-8123-456789abcdef", "name": "My Look",
 "recipe": {"entries": [{"enabled": true, "opacity": 1.0, "blendMode": "normal",
   "invocation": {"filterId": "patchy.filters.soft_glow", "schemaVersion": 1,
     "parameters": {"amount": {"type": "integer", "value": 75}},
     "foreground": {"red": 0, "green": 0, "blue": 0},
     "background": {"red": 255, "green": 255, "blue": 255}}}]}}
```

Parameter values carry an explicit `integer`, `double`, `boolean`, or `string` type. Blend modes use the stable full Photoshop descriptor string tokens, never enum ordinals or translated names. Entry array order is execution order. Colors are required even when the filter does not use them (see Captured colors).

Writes use `QSaveFile`; memory state changes only after the atomic commit. Loading is strict and bounded: 1 MiB per file, 64 entries per recipe, 64 parameters per entry. Rejected: malformed JSON, unsupported record versions, filename/record UUID mismatch, invalid UTF-8, invalid value types, non-finite or out-of-range opacity, invalid colors, unknown blend tokens. Unknown filter IDs and schema versions remain structurally valid: they list disabled with an unsupported tooltip and are preserved, never substituted. A malformed record is skipped without touching neighbors.

## Regression coverage

Keep separate coverage for: legacy wrapper output per built-in ID; version-1 defaults and explicit non-default parameters; the exact ID/category/parameter/scaling/bounds catalog; missing/unknown parameters, unsupported IDs/schemas, recipe ordering; captured Clouds colors and the whole canvas-filling contract; progress completion and cancellation; menu/action/hotkey contracts; selection, expanding bounds, Cancel, one-step Undo/Redo; the auto adjustments (dual-path byte identity, composite-versus-per-channel casts, degenerate-scan identity, Auto Color midtone snap, no-dialog immediate apply, Auto All's single-undo recipe equivalence); and the all-filter visual contact sheet.

Never re-pin an output canary as part of a structural refactor. First capture the contact-sheet SHA-256 from a full pre-refactor suite run of the same tree, then establish that the refactored path reproduces the old focused outputs and that captured SHA. Do not compare against a SHA recorded in an older commit: the sheet legitimately changes whenever filters are added, so a hardcoded value goes stale.
