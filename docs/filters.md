# Visual filters and recipes

The contract for Patchy's built-in pixel filters, the shared catalog, and ordered recipes. The gallery and Smart Filters build on these types, so persisted names and default behavior are compatibility surfaces. Kernels: `src/filters/builtin_filters.cpp`, `filter_engine.cpp`, and the bodies both share with the native Smart Filters in `filter_kernels.{hpp,cpp}`. Catalog metadata: `src/filters/filter_registry.{hpp,cpp}`.

Related documents:

- [filters-gallery.md](filters-gallery.md): the Filter Gallery (items, search, favorites, settings keys, object names, outcome surfacing, overlay controls, preview pipeline) and the direct filter dialogs' bounded preview.
- [filters-spatial-bounds.md](filters-spatial-bounds.md): proxy scaling of pixel-distance parameters, per-filter growth and calibration notes, center and width remaps on padded buffers, soft selections, and the canvas-filling filters.

## Two execution contracts

`FilterRegistry::apply(id, pixels)` is the legacy path (original built-ins, historical pixels); `FilterRegistry::apply(invocation, pixels)` the named-parameter path for dialogs, recipes and previews (absent parameters take catalog defaults). They stay separate: defaults differ for some filters (Posterize, Gaussian Blur, Clouds, Glowing Edges, some rounding). Never route the legacy wrapper through `default_invocation()` or change either output to make them agree; tests pin both. Where both always produced the same bytes (Emboss, Pixel Mosaic, Radial Blur; their Smart Filter copies and Box Blur's), one kernel in `filter_kernels.cpp` serves every caller with its own arguments (legacy: fixed ones, null progress). Never change a kernel body to make callers agree; `filter_legacy_and_named_defaults_share_kernels` and the `smart_filter_*_matches_destructive` tests pin the sharing.

## Stable identifiers and schemas

Built-in filter IDs are `patchy.filters.` plus the 57 suffixes below, in canonical catalog order: persisted, append-only, never renamed or reused. The list doubles as the version-1 parameter keys and defaults (used when a known parameter is missing):

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
blur, blur_more, average, sharpen_more, sharpen_edges: none
diffuse: mode=normal
extrude: type=blocks size=30 depth=30 heights=random solid_front=false seed=1
find_edges, solarize: none
tiles: count=10 max_offset=10 fill=background seed=1
trace_contour: level=128 edge=lower
wind: method=wind direction=angle angle=180 length=40 (direction from_right/from_left: 180/0)
difference_clouds: scale=96 detail=6 contrast=40 seed=1
fibers: variance=16 strength=4
flame: colors=classic mode=base_line rise=up length=40 width=60 turbulence=50 flicker=100 density=100 lean=0 core=100 glow=100 smoke=100 sparks=100 opacity=100 seed=1 center_x=50.0 center_y=100.0
lens_flare: lens=classic brightness=100 center_x=30.0 center_y=30.0
```

A `FilterInvocation` stores filter ID, schema version, named parameters, and captured foreground/background colors. Parameter keys are stable within a schema; unknown keys are ignored. An unknown filter ID or schema version makes the invocation and its containing recipe unsupported; never run the newest schema instead or fall back to another filter. Values normalize through the catalog before execution: declared types kept, numerics clamped to range; a wrong-typed known key or unknown option token is invalid, not coerced.

## Categories and UI contracts

Nine catalog filters carry `FilterCategory::Adjustment` and surface under Image > Adjustments: Invert, Brightness/Contrast, Grayscale (no direct action yet), Desaturate, Auto Tone, Auto Contrast, Auto Color, Threshold, Posterize. Invert (Ctrl+I) applies at once with no dialog, like the three autos below (#411). The dialog adjustments (Levels, Curves, Hue/Saturation, Color Balance, and the [other kinds](adjustment-kinds.md#image--adjustments)) are not catalog filters.

The gallery exposes the other 48 effects in the fixed catalog and category order below (labels translated, never locale-sorted). An effect's ID suffix is its lowercased label with underscores ("&" becomes "and"), except where given in parentheses:

```text
photo_looks: Soft Glow, Punchy Color, Noir, Cinematic Matte, Vintage Fade, Vintage Sepia (sepia), Lens Vignette (vignette)
blur: Box Blur, Gaussian Blur, Motion Blur, Radial Blur, Surface Blur, Lens Blur, Iris Blur, Tilt-Shift Blur, Blur, Blur More, Average
sharpen: Sharpen, Unsharp Mask, High Pass, Sharpen More, Sharpen Edges
distort: Twirl, Wave, Pinch/Bloat (pinch_bloat)
noise: Analog Grain (film_grain), Add Noise, Median, Dust & Scratches
pixelate: Pixel Mosaic (pixelate), Color Halftone
stylize: Edge Detect, Emboss, Glowing Edges, Diffuse, Extrude, Find Edges, Solarize, Tiles, Trace Contour, Wind
render: Clouds, Difference Clouds, Fibers, Flame, Lens Flare
artistic: Plastic Wrap
```

The category selector is `all`, `favorites`, then the nine tokens above; these eleven tokens and their order are settings contracts. Never persist a translated label or a `FilterCategory` ordinal.

The catalog generates dialog controls; existing Qt object names such as `filterAmountSpin` and `filterRadiusSlider` remain test contracts. Editors come from `FilterParameterDefinition` through the one shared `FilterParameterPanel` (`src/ui/filter_parameter_panel.{hpp,cpp}`); `FilterParameterPanelOptions` carries the gallery's presentation deltas.

`practical_minimum`/`practical_maximum` may narrow a linked slider without narrowing the semantic range; every spin box (gallery included), normalization, recipes, and persistence keep `minimum`/`maximum`. `FilterParameterPresentation` roles (an enum; not persisted, never replacing the parameter key or value) select specialized UI/render behavior: never key off parameter key, label, unit, or filter ID instead. The center, tilt width, and iris dimension roles drive the padding remaps under [Spatial scaling and bounds](filters-spatial-bounds.md#spatial-scaling-and-bounds).

Only eight catalog filter IDs are hotkey command IDs: Invert, Desaturate, Auto Tone, Auto Contrast, Auto Color, Brightness/Contrast, Threshold, Posterize. Catalog-generated direct Filter-menu actions are not HotkeyRegistry commands. Liquify uses `filter.liquify` (Ctrl+Shift+X), Camera Raw Filter `filter.camera_raw_filter` (Ctrl+Shift+A). A catalog refactor must not silently add or remove commands.

Catalog names are canonical English translation sources (`QObject` context); submenu and status text keep the `MainWindow` context.

The Filter > Stylize submenu is alphabetical; every other submenu keeps this order. What each issue-496 filter does: [stylize-render-filters.md](stylize-render-filters.md).

Camera Raw Filter and Liquify (Filter menu, in that order below Filter Gallery) are outside this catalog ([camera-raw.md](camera-raw.md#camera-raw-filter), `docs/liquify.md`).

## Bit depth

`deep_filter_support` (`filters/deep_filters.cpp`) decides how a catalog filter runs on a 16/32-bit layer: `Native` (float kernels in `deep_filters.cpp`), `EightBitPrecision` (16 bits only: the 8-bit filter runs on a narrowed copy and `apply_eight_bit_edit_at_depth` folds the change back), or `Unsupported` (32 bits: the menu action is disabled through its `patchy.filterIdentifier` property, the engine and script `applyFilter` throw). The Filter Gallery takes 16-bit layers, its previews drawn from display values, and is disabled at 32 bits like Liquify and Auto All. The per-filter list lives in [high-bit-depth.md](high-bit-depth.md).

## Text and shape layer gate

Text and shape layers regenerate their pixels, so a destructive edit would vanish. Every destructive entry point calls `MainWindow::prompt_rasterize_procedural_layer` first: catalog filter actions (Image > Adjustments and Auto All included), the Filter Gallery, Liquify, Camera Raw Filter, the destructive adjustment dialogs, G'MIC, Perspective Planes, Trace Image to Shapes, and legacy plug-ins. Smart objects are untouched (docs/smart-objects.md).

The prompt (`rasterizeOrConvertMessageBox`) offers Convert To Smart Object (AcceptRole, the default when present), Rasterize (DestructiveRole), and Cancel. Convert appears only when the follow-on can succeed: a direct filter needs a native Smart Filter mapping (`native_smart_filter_kind_for`), the gallery always qualifies, and both need the document under the 64-megapixel editable-mask cap; the other entry points never offer it. Rasterize commits its own "Rasterize layer" undo step, kept if the follow-on dialog is cancelled; Convert converts and re-enters the smart-object routing. The gate commits an active inline text edit first; CLI automation gets a status refusal instead of a blocking prompt. Coverage: `ui_filter*_on_*_layer_*`, `ui_levels_on_text_layer_*`, `ui_legacy_plugin_offers_to_rasterize_text_layer`.

## Last Filter

Filter > Last Filter (`filter.last`; Photoshop's Ctrl+Alt+F, Control+Command+F on macOS; `MainWindow::apply_last_filter`) reruns the last completed filter with its settings, no dialog, on the active layer and selection. `last_filter_` holds a catalog `FilterInvocation` (colors re-read), a Gallery recipe, the G'MIC Library's accepted choice, or an 8BF id. Adjustments, Liquify and Smart Filter dialogs are not recorded; on a Smart Object a natively mapped filter still opens its dialog. Tests: `last_filter`.

## Auto adjustments

The three autos share one Qt-free kernel, `src/filters/auto_levels_math.{hpp,cpp}`, called by both execution contracts:

- Clip scan matches the Levels dialog Auto button: threshold `max(1, samples / 1000)` (0.1% per end), upward black scan, downward white scan bounded by `white > black + 1`. A scan that exhausts without exceeding the threshold leaves the channel unchanged (constant channels and 1x1 images are identity, as in Photoshop).
- Application uses a 256-entry LUT from the kernel's own copy of the levels transfer, matching core `levels_channel` rounding: Auto Tone equals committing the Levels dialog's Auto scan per channel. The transfer formula is deliberately per-consumer (`clamp_levels_record`, `core/adjustment_layer.hpp`).
- **Auto Tone** (`auto_tone`, Ctrl+Shift+L): per-channel clip scan and stretch; neutralizes color casts.
- **Auto Contrast** (`auto_contrast`, Ctrl+Alt+Shift+L): one merged R+G+B histogram, a single black/white pair, the same LUT on all three channels; casts survive.
- **Auto Color** (`auto_color`, Ctrl+Shift+B): the per-channel scan plus a neutral-midtone snap. The channel's whole-image mean, normalized into the stretched range, selects the integer `gamma_percent` in [10, 999] whose curve maps it closest to 128, ties preferring the smaller gamma. Every result is a Levels dialog state.
- Whole-layer analysis even with a selection; the wrapper restores unselected pixels afterward. Alpha untouched; UInt8 buffers with 3+ channels only.
- Menu, hotkey, and scripting invocations of the three autos (and Invert) apply immediately at the catalog defaults with no dialog. `amount` remains a catalog contract for recipes, Saved Looks, and explicit scripting invocations; `apply_filter` special-cases the immediate path (`filter_applies_without_settings_dialog`).
- **Auto All** (`imageAdjustAutoAllAction`, persisted command ID `image.auto_all`, no default shortcut) applies Tone, Contrast, then Color as one three-entry recipe pass and one undo step (`MainWindow::auto_all_adjustments`), a MainWindow command, not a catalog filter. With a selection it follows the recipe contract (unselected pixels restore once at the end) and can differ from three menu applies; without one they are byte-identical.
- Photoshop calibration deltas: [adjustments-calibration.md](adjustments-calibration.md).

## Captured colors

Foreground and background colors are copied into every invocation at creation. Clouds reads the captured colors; re-rendering a recipe must not depend on the toolbar swatches later.

## Recipes

`FilterRecipe` stores entries in execution order: invocation, enabled state, opacity, blend mode. Disabled entries are skipped. An unsupported invocation makes the whole recipe unsupported, even when disabled: enabling it later must not substitute a result. Opacity is a finite double in [0, 1], else unsupported. Defaults: enabled, opacity 1, Normal. Execution is deterministic from the supplied immutable source; never build a preview cumulatively from an earlier preview.

The applied-effects list shows the final effect at the top, opposite execution order, so a reordered list rebuilds the recipe from the bottom row up. Each dialog entry has a transient numeric identity (never persisted) so duplicates stay independent.

Selecting a catalog filter creates the first entry or replaces the active entry's invocation, keeping its enabled state and blending. Duplicate inserts a copy after the active entry. Per-entry blend mode and opacity sit below the list; Saved Looks persist them and the Smart Filter mapping copies them into native entries. Reset returns the active entry to Normal at 100% and default parameters. Remove selects the nearest remaining row; Original clears the recipe. Category, search, and Favorites changes never mutate the recipe. Effect rows are drag sources, not drop targets: a drop inserts between rows and emits the canonical row-move signal.

Recipe scaling returns a normalized copy keeping order, enable state, blending and captured colors. Aggregate translation support is the checked sum over enabled nonzero-opacity entries; an executed entry with unknown support makes the aggregate unknown. Zero-opacity entries are validated for persistence but do not execute, expand bounds, report progress, or affect aggregate support.

With a selection, the whole recipe runs against one immutable source and the wrapper restores outside pixels once after the final entry; per-entry restoration would change spatial results near the edge. A fully transparent expanded result returns to the input rectangle.

## Saved Looks

User Looks live at `<settings dir>/looks/<uuid>.json` (built-in ones: [builtin-looks.md](builtin-looks.md)); the lowercase canonical UUID is the stable preset ID and filename stem. Save creates a fresh UUID, Rename keeps it, Delete removes the record. Library operations apply immediately; gallery Cancel does not roll them back.

Version-1 record shape: `{version, id, name, recipe: {entries: [{enabled, opacity, blendMode, invocation: {filterId, schemaVersion, parameters: {key: {type, value}}, foreground: {red, green, blue}, background}}]}}`. Parameter values carry an explicit `integer`, `double`, `boolean`, or `string` type. Blend modes use the stable full Photoshop descriptor string tokens, never enum ordinals or translated names. Entry array order is execution order; colors are required even when unused.

Writes use `QSaveFile`; memory state changes only after the atomic commit. Loading is strict (1 MiB per file, 64 entries per recipe, 64 parameters per entry) and rejects malformed JSON, unsupported record versions, a filename/record UUID mismatch, invalid UTF-8 or value types, non-finite or out-of-range opacity, invalid colors, and unknown blend tokens. Unknown filter IDs and schema versions stay structurally valid: listed disabled with an unsupported tooltip, preserved, never substituted. A malformed record is skipped without touching neighbors.

## Regression coverage

Keep separate coverage for: wrapper output per built-in ID; defaults and explicit parameters; the exact catalog (IDs, categories, parameters, scaling, bounds); bad parameters, IDs and schemas, recipe order; Clouds colors and canvas filling; progress and cancel; menu, action and hotkey contracts; selection, growth, Cancel, one-step Undo/Redo; the autos; the contact sheet.

Never re-pin an output canary as part of a structural refactor. Capture the contact-sheet SHA-256 from a full pre-refactor suite run of the same tree, then show the refactored path reproduces the old focused outputs and that SHA. Never compare against a SHA from an older commit: the sheet changes whenever filters are added.
