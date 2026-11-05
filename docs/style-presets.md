# Layer style presets, the Styles page, and .asl interchange

Deep reference for the layer-style preset system: the Styles page in the Layer Style dialog, the persistent StyleLibrary, the Style Manager, the built-in presets, and the Photoshop `.asl` codec. The blendOptions calibration notes live in docs/psd-layer-styles.md ("Style presets and .asl").

Related documents:

- [style-presets-patterns.md](style-presets-patterns.md): the bundled photo-texture patterns, the pattern library and Pattern Manager, long picker popups, Photoshop preset pattern names, and the versioned defaults the pattern and style libraries share.
- [style-presets-strokes.md](style-presets-strokes.md): Shape Appearance's saved stroke presets and the Stroke Presets manager.

## Surfaces

Layer Style supports selected-layer property edits, ordered effect operations,
and explicit full replacement. See [batch appearance](batch-appearance.md) for
matching, mixed values, preview transactions, native-data safeguards, and history.

- **Style Presets page** (Layer Style dialog): a non-checkable "Style Presets" row sits FIRST in the category list, above Blending Options; the dialog still opens on Blending Options (the row carries the selection marker `kStylesCategoryIndex = -2` because it shares `LayerStyleEffectKind::None` with Blending Options, and `LayerStyleCategoryPage::Styles` is appended to the enum so the stacked-widget indices of the older pages never move). Clicking a preset REPLACES the working effects (Photoshop behavior), forces `effects_visible` on, and keeps `layer_mask_hides_effects`, `vector_mask_hides_effects` and the layer's opacity/blend mode/Blend If unless the preset carries blending options. A "No Style" first entry clears all effects and keeps blending options. The presets show as a thumbnail grid (`layerStyleStylesGrid`, a `PresetGridWidget` whose first cell is No Style) or as the folder tree (`StyleBrowserWidget`); the List/Grid buttons switch them and `layerStyle/stylesGridView` remembers the choice (Grid by default, GitHub issue 606). Both share the tree's .asl export menu (`StyleBrowserWidget::exec_export_menu`); the info icon beside Manage Styles says so. The page lives in `layerStyleStylesPane`, beside the scrolling `layerStyleOptionsScroll` in the `layerStyleOptionsPane` stack (its `layerStyleStylesPage` in the options stack is an empty placeholder that keeps the current-page name), so only the preset view scrolls and New Style, Manage Styles and the view buttons always show (606 follow-up). The views open at least half the screen's available height, clamped to 240..500 px; once shown, `OptionsPaneScreenCap::set_shrink_after_show` lowers that to 160 so the dialog still drags shorter. "New Style…" saves the current dialog state (checkbox opts into capturing opacity/blend mode/Blend If); "Manage Styles…" opens the Style Manager and applies its "Use Style" pick.
- **Style Manager** (`request_style_manager`, src/ui/style_manager_dialog.cpp): the Pattern Manager's style twin. Tree + large rendered preview + name/folder edits + Import .asl / Export / Duplicate / Delete / Restore Default Styles / Use Style. Library edits apply immediately (no undo, matching patterns). Opened from Edit > Presets it runs with `offer_use` false (no Use Style: there is no layer to style).
- **Edit > Presets** (`editPresetsMenu`, GitHub issues 485 and 486): Styles Manager, Brush Tips, Saved Brushes, Gradient Manager, Pattern Manager, Shapes Manager (`edit.presets_custom_shapes`), Stroke Presets (`edit.presets_stroke_presets`, grouped with Shapes), in three separated groups; hotkey ids `edit.presets_*`, no default keys, `QAction::NoRole`, no document needed. Menu and page both read `MainWindow::preset_manager_entries()` (labels, order, groups, hotkey ids, open function). Preferences > Presets, the last page of the list, shows one button per entry, in one grid so they share a width, with a line of description; each calls the same open function with Preferences as the parent, so the manager opens over it. Edit > Define New Preset (`editDefineMenu`, right above Presets) holds Define Brush Tip from Selection and Define Custom Shape from Path / from SVG File under their full names, because the shape context menu and the Hotkeys page show the same QActions. Use from the menu goes to the matching tool (Gradient tool, Pattern Stamp, the Custom Shape picker). The Shapes Manager imports Photoshop `.csh` and SVG files ([vector-tools-custom-shapes.md](vector-tools-custom-shapes.md#photoshop-csh-files)). The options bar and Layer Style buttons still open the same dialogs. Pinned by `ui_edit_presets_menu_lists_the_managers_in_order`.
- **StyleBrowserWidget** (src/ui/style_browser.cpp) is the shared folder tree (48 px icons on the Styles page, 40 px in the manager). Its context menu's "Export to .asl…" exports the selection; a folder row exports its children and supplies the default filename. `export_selection_to(path)` is the prompt-free half tests drive.

The dialog controller remains in `ui/layer_style_dialog.cpp`: it owns category order,
batch edits, preset application, preview cancellation, and transient pattern restoration.
`layer_style_effect_pages.{hpp,cpp}` builds each effect page and returns its control
handles; the dialog retains those aggregates through `exec()`. The construction context
must not be retained by callbacks. `layer_style_gradient_controls.{hpp,cpp}` owns the
shared gradient editor, whose separate context lives through the whole dialog; callbacks
capture that stable state or widget handles. `layer_style_dialog_controls.{hpp,cpp}` owns
shared color and numeric rows. Page object names, stacked indices, and translation
contexts remain compatibility contracts for the UI tests.

The settings pages sit in the `layerStyleOptionsScroll` scroll area (GitHub issue 190).
`OptionsPaneScreenCap` keeps its minimum equal to the tallest page up to what the
screen leaves (available height minus 40, measured before the dialog shows), so a
tall screen opens and grows the dialog exactly as before and a short one scrolls
the pane with OK/Cancel in view. The first Show takes any overflow left (height,
or width on a screen narrower than the dialog's minimum) out of the pane. `ui_layer_style_dialog_fits_a_short_screen` pins
it on the 800 x 800 offscreen screen. With no remembered size the dialog opens at most
three quarters of the available height (at least 520 px), scrolling the rest; the user
can drag it up to the cap (GitHub issue 606). The dark chrome is frameless, so a
`VisibleSizeGrip` (`layerStyleSizeGrip`) at the right of the OK/Cancel row is the
resize handle. Changing pages scrolls the shared pane back to its top (606 follow-up 3).

Page layout (GitHub issue 606): every form label ends in a colon and
`align_page_label_columns` gives all page forms one label width, so every field
starts at one indent; a page's sub-sections (Bevel's Shading, Highlight, Shadow) are
`add_form_section` lines and bold titles inside the page's one form, not nested group
boxes. Page titles go through `page_title`, which escapes "&" (a QGroupBox reads it as
a mnemonic). Color rows are a swatch button plus R/G/B fields; two-choice settings are
radio pairs (`make_two_choice_radios`). Blend If sits inside the Blending Options
section: "Blend If:" in the label column and the channel row in the field column;
`layerStyleBlendIfGroup` (the This Layer / Underlying Layer lines and ramps) is a
spanning form row from the section's left edge, each title on the line with its
values (606 follow-ups 1 and 2; no step buttons:
the fields step with the arrow keys and the wheel; Alt-drag still splits a handle),
and `CheckBoxColumns` moves the mask and Show Effects boxes into a second column
when the section is wide enough. Angle rows (`add_angle_spin_row`) put an `AngleDial`
where the slider was and keep the field at the right; Bevel's Shading uses the
angle-and-altitude dial (`add_angle_altitude_rows`) with Altitude's field under
Angle's (GitHub issue 618).

## Make Default and Reset to Default

Every effect page except Style Presets, Blending Options, Contour and Texture has
Make Default and Reset to Default. All pairs (object names
`layerStyle<Effect>MakeDefaultButton` / `ResetToDefaultButton`) live in one
left-aligned row, `layerStyleDefaultsRow`, under the scroll area (606 follow-up 3);
only the current page's pair shows, and the row hides (keeping its height) on the
pages without one. Make Default stores that page's values for
the effect under `layerStyle/effectDefaults/<token>` (bevelEmboss, stroke,
innerShadow, innerGlow, satin, colorOverlay, gradientOverlay, patternOverlay,
outerGlow, dropShadow; persisted identifiers) as a one-style .asl blob; the drop
shadow's Patchy-only long-shadow fields, which .asl cannot carry, sit beside it in
`dropShadowContinuous` and `dropShadowFade`. Bevel & Emboss stores its own page
only: Contour and Texture stay built-in. The dialog loads the stored values when it
opens; `default_<effect>()` returns them (enabled) or the built-in values, so a newly
ticked or added effect starts from them. Reset to Default puts them (or the built-in
values) back on the current instance and keeps whether it is on. Nothing is stored
until the user presses Make Default, so PSD output and settings are unchanged
otherwise. `clear_stored_layer_style_effect_defaults` forgets them all. Pinned by
`ui_layer_style_make_default_and_reset_to_default`.

## StyleLibrary (src/ui/style_library.*)

- Storage: `<settings dir>/styles/`: per entry a **single-style .asl file** named by storage UUID (self-contained: effects + optional blendOptions + the referenced pattern tiles), a JSON sidecar `{id, sourceId?, name, folder}`, and a cached thumbnail PNG (regenerated when missing or older than the .asl). `reload()` is .asl-driven; the sidecar overrides name/id/folder.
- The storage UUID is separate from the style id ('Idnt') so imported ids stay stable for re-import dedup while filenames stay path-safe (the PatternLibrary convention, including `sourceId` retention when a colliding id is remapped).
- Import groups styles under a folder named after the file; equal payload + same id (or same sourceId) deduplicates. Payload equality is `photoshop_lfx2_layer_style_payload(a) == photoshop_lfx2_layer_style_payload(b)` plus blend-settings equality, the canonical modeled comparison.
- Export unions the referenced tiles of every exported entry (dedup by id, first wins).
- Defaults: `kDefaultStylesVersion` + `styles/defaultStylesVersion` settings gate in `MainWindow::style_library()` (the pattern-library seeding pattern exactly); the manager's Restore repairs deletions and resets drifted recipes/metadata/tiles.
- Thumbnails: `render_style_preview` rasterizes bold "Aa" (application font; rounded-square fallback when no glyph ink, e.g. offscreen) over a mid-gray gradient card, applies the style (and blend settings) through the real Compositor, adopts entry patterns plus built-in pattern fallbacks. Rendered at 96 px, scaled per view; never byte-pinned by tests.

## Applying a preset carries patterns safely

`patterns_for_entry` tiles are materialized into the document store on apply. A tile whose id already belongs to DIFFERENT document pixels goes through the same collision path as the Pattern Manager: fresh document-local UUID, registered in the dialog's `transient_manager_patterns` (so preview-off/cancel restore stays exact), and the applied style's `pattern_id` references are rewritten to follow. MainWindow's snapshot/restore and `ensure_patterns_for_style` cover preview/commit/undo unchanged.

Pattern presets (bundled textures, the pattern library and Pattern Manager) and the [versioned defaults](style-presets-patterns.md#versioned-defaults-patterns-and-styles) the pattern and style libraries share are in [style-presets-patterns.md](style-presets-patterns.md); Shape Appearance stroke presets are in [style-presets-strokes.md](style-presets-strokes.md).

## Built-in presets (src/core/style_presets.*)

39 presets: the "Text" folder's 20 famous-look crowd pleasers (Adventure, Hack the Gibson, A Galaxy Far Away, Neon Nights, Arcade Cabinet, Chrome Bumper, Liquid Gold, Ice Cold, Molten Core, Toxic Ooze, Midnight Horror, Wanted Poster, Comic Pow, Bubble Pop, Saturday Cartoon, Space Cadet, Royal Decree, Stamped Steel, Honey Drip, Blueprint), the "Basics" folder's 6 (Soft Shadow, Sticker Outline, Simple Emboss, Warm Glow, Neon Edge, Letterpress), and the "Materials" folder's 13 built on the bundled photo textures (Carved Oak, Walnut Gloss, Weathered Sign, Driftwood, Timber Grain, Marble Monument, Slate Etched, Granite Bold, Rust Bucket, Riveted Steel, Leather Stamp, Frost Drift, Cracked Desert).

- Ids are fixed GUID-shaped strings (`57a1e500-…`) persisted in sidecars and exported .asl files: **never rename or re-seed**; append-only, like pattern presets (the 12 generated built-in pattern presets in src/core/pattern_presets.cpp carry the same rule with `c4a11e00-…` ids; `pattern_presets_generate_stable_tiles` pins their tiles). `style_presets_have_stable_ids_and_recipes` pins ids, folders, pattern resolvability, and that every recipe survives the lfx2 round trip byte-identically.
- Names are original phrases evoking famous looks, no trademarks; recipes stay inside what Patchy renders today (smooth inner bevel, gloss contours, stacked instances, built-in patterns only; Stamped Steel references Brushed Metal + Bumps).
- Canonical English names/folders live in the sidecars; display translates while unrenamed (`style_library_entry_display_name`, `style_preset_folder_display_name`, QObject context, like pattern names).

## .asl codec (src/psd/asl_io.*)

Container (verified against PS 2026 files, see docs/psd-layer-styles.md, "Style presets and .asl"): u16 2, '8BSL', u16 3, u32 patterns length (0 with NO count field when empty; otherwise standard 'Patt' block records via psd_patterns), u32 style count, then per style a 4-aligned length-prefixed record of two version-16 descriptors: 'null' {Nm, Idnt} and 'Styl' {documentMode, Lefx, blendOptions}. 'Lefx' matches the lfx2 root descriptor, so conversion is shared through psd/psd_layer_effects.hpp (`layer_style_from_lefx_descriptor`, `photoshop_lfx2_layer_style_payload`; the writer re-reads its own payload with `read_descriptor` and re-classes it "Lefx"). ZString names ("$$$/key=Display Name") resolve to the display text; Patchy writes plain names. Trailing 8BIMphry hierarchy data is ignored. Error handling mirrors pat_reader: 32 MiB cap, per-style skip-with-warning, decoded-prefix retention, id repair. `fillOpacity` is modeled with opacity, blend mode, and Blend If; it is emitted only when nondefault so existing default bytes stay stable. The Patchy-only continuous (long) drop shadow fields (`LayerDropShadow::continuous/fade`) have no Lefx representation, so an .asl export or a saved style preset keeps only the plain DrSh; the fields survive in PSDs through image resource 4212 (docs/ps-compat.md). Knockout and channel restrictions still warn and drop; custom Satin contours normalize to Linear at import with a warning. `asl_writer_bytes_are_stable` is the byte canary.

The descriptor engine supports the 'obj ' Action Manager REFERENCE value type (DescriptorReferenceItem, read + byte-identical write) for blendOptions' per-channel 'Chnl' references.

## Fixtures and tests

- `test-fixtures/asl/photoshop-style-blend-options.asl`: PS 2026-authored single-style export (our own probe content) pinning the populated blendOptions layout; its Blend If values cross-check the pinned `photoshop-blend-if-4b-roundtrip.psd` bytes.
- `local-test-fixtures/asl/Abstract Styles.asl`: untracked copy of PS's shipped file for `asl_reader_reads_photoshop_shipped_styles_if_available` (Adobe asset; never commit).
- UI coverage: `ui_style_library_defaults_restore_export_import_round_trip` (writes the `style_preset_thumbnails.png` contact sheet artifact), `ui_layer_style_styles_page_applies_preset_and_previews`, `ui_layer_style_no_style_entry_clears_effects`, `ui_layer_style_new_style_saves_current_settings`, `ui_style_browser_folder_selection_exports_asl`, `ui_style_manager_lists_renames_and_uses_styles`, `ui_style_preset_pattern_apply_cancel_restores_document_store`.

## Gotchas

- **PS re-saves of multi-instance styles use 'lmfx'**: Photoshop stores stacked instances (Comic Pow's two strokes) in the 'lmfx' tagged block (same payload shape as lfx2, the parser reads the `*Multi` lists) and writes a single-instance compatibility lfx2 beside it. Patchy treats lmfx as authoritative in either block order, preserves it raw on untouched layers, and `clear_layer_psd_style_source` + the writer strip drop it on edit so a stale lmfx can never shadow a regenerated lfx2 in Photoshop. Pinned by `psd_photoshop_lmfx_multi_effects_fixture_round_trips` on the PS-authored `photoshop-lmfx-multi-stroke.psd` (the acceptance-probe PSD).

- The Styles page's browser lives inside the dialog whose preview machinery guards on `loading_controls`; preset apply sets the master opacity/blend controls (and `show_effects`) under that guard before `rebuild_category_list` + `emit_preview(true)`. Blending options in a preset do NOT overwrite a preserved-unsupported Blend If payload unless the user already chose "Replace with Editable Defaults".
- Blend If ranges ride blendOptions only when non-identity; parse accepts Gry/Rd/Grn/Bl channel references and ignores others with warnings.
- Preset and folder names in the core tables are wrapped in `PATCHY_TRANSLATE_NOOP("QObject", ...)` and translated at display time with `translate_data_text`; lupdate extracts them, so `scripts\update-translations.ps1` keeps every catalog current. See [localization.md](localization.md).
