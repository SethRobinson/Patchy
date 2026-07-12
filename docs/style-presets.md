# Layer style presets, the Styles page, and .asl interchange

Deep reference for the layer-style preset system: the Styles page in the Layer Style dialog, the persistent StyleLibrary, the Style Manager, the built-in presets, and the Photoshop `.asl` codec. The blendOptions calibration notes live in docs/ps-compat.md ("Style presets and .asl files").

## Surfaces

- **Style Presets page** (Layer Style dialog): a non-checkable "Style Presets" row sits FIRST in the category list, above Blending Options; the dialog still opens on Blending Options (the row carries the selection marker `kStylesCategoryIndex = -2` because it shares `LayerStyleEffectKind::None` with Blending Options, and `LayerStyleCategoryPage::Styles` is appended to the enum so the stacked-widget indices of the older pages never move). Clicking a preset REPLACES the working effects (Photoshop behavior), forces `effects_visible` on, and keeps `layer_mask_hides_effects` and the layer's opacity/blend mode/Blend If unless the preset carries blending options. A "No Style" first entry clears all effects and keeps blending options. "New Style…" saves the current dialog state (checkbox opts into capturing opacity/blend mode/Blend If); "Manage Styles…" opens the Style Manager and applies its "Use Style" pick.
- **Style Manager** (`request_style_manager`, src/ui/style_manager_dialog.cpp): the Pattern Manager's style twin. Tree + large rendered preview + name/folder edits + Import .asl / Export / Duplicate / Delete / Restore Default Styles / Use Style. Library edits apply immediately (no undo, matching patterns).
- **StyleBrowserWidget** (src/ui/style_browser.cpp) is the shared folder tree (48 px icons on the Styles page, 40 px in the manager). Its context menu's "Export to .asl…" exports the selection; a folder row exports its children and supplies the default filename. `export_selection_to(path)` is the prompt-free half tests drive.

## StyleLibrary (src/ui/style_library.*)

- Storage: `<settings dir>/styles/` — per entry a **single-style .asl file** named by storage UUID (self-contained: effects + optional blendOptions + the referenced pattern tiles), a JSON sidecar `{id, sourceId?, name, folder}`, and a cached thumbnail PNG (regenerated when missing or older than the .asl). `reload()` is .asl-driven; the sidecar overrides name/id/folder.
- The storage UUID is separate from the style id ('Idnt') so imported ids stay stable for re-import dedup while filenames stay path-safe — the PatternLibrary convention, including `sourceId` retention when a colliding id is remapped.
- Import groups styles under a folder named after the file; equal payload + same id (or same sourceId) deduplicates. Payload equality is `photoshop_lfx2_layer_style_payload(a) == photoshop_lfx2_layer_style_payload(b)` plus blend-settings equality — the canonical modeled comparison.
- Export unions the referenced tiles of every exported entry (dedup by id, first wins).
- Defaults: `kDefaultStylesVersion` + `styles/defaultStylesVersion` settings gate in `MainWindow::style_library()` (the pattern-library seeding pattern exactly); the manager's Restore repairs deletions and resets drifted recipes/metadata/tiles.
- Thumbnails: `render_style_preview` rasterizes bold "Aa" (application font; rounded-square fallback when no glyph ink, e.g. offscreen) over a mid-gray gradient card, applies the style (and blend settings) through the real Compositor, adopts entry patterns plus built-in pattern fallbacks. Rendered at 96 px, scaled per view; never byte-pinned by tests.

## Applying a preset carries patterns safely

`patterns_for_entry` tiles are materialized into the document store on apply. A tile whose id already belongs to DIFFERENT document pixels goes through the same collision path as the Pattern Manager: fresh document-local UUID, registered in the dialog's `transient_manager_patterns` (so preview-off/cancel restore stays exact), and the applied style's `pattern_id` references are rewritten to follow. MainWindow's snapshot/restore and `ensure_patterns_for_style` cover preview/commit/undo unchanged.

## Built-in presets (src/core/style_presets.*)

26 code-generated presets (no binary assets): the "Text" folder's 20 famous-look crowd pleasers (Adventure, Hack the Gibson, A Galaxy Far Away, Neon Nights, Arcade Cabinet, Chrome Bumper, Liquid Gold, Ice Cold, Molten Core, Toxic Ooze, Midnight Horror, Wanted Poster, Comic Pow, Bubble Pop, Saturday Cartoon, Space Cadet, Royal Decree, Stamped Steel, Honey Drip, Blueprint) and the "Basics" folder's 6 (Soft Shadow, Sticker Outline, Simple Emboss, Warm Glow, Neon Edge, Letterpress).

- Ids are fixed GUID-shaped strings (`57a1e500-…`) persisted in sidecars and exported .asl files — **never rename or re-seed**; append-only, like pattern presets. `style_presets_have_stable_ids_and_recipes` pins ids, folders, pattern resolvability, and that every recipe survives the lfx2 round trip byte-identically.
- Names are original phrases evoking famous looks, no trademarks; recipes stay inside what Patchy renders today (smooth inner bevel, gloss contours, stacked instances, built-in patterns only — Stamped Steel references Brushed Metal + Bumps).
- Canonical English names/folders live in the sidecars; display translates while unrenamed (`style_library_entry_display_name`, `style_preset_folder_display_name` — QObject context, like pattern names).

## .asl codec (src/psd/asl_io.*)

Container (verified against PS 2026 files, see ps-compat.md): u16 2, '8BSL', u16 3, u32 patterns length (0 with NO count field when empty; otherwise standard 'Patt' block records via psd_patterns), u32 style count, then per style a 4-aligned length-prefixed record of two version-16 descriptors — 'null' {Nm, Idnt} and 'Styl' {documentMode, Lefx, blendOptions}. 'Lefx' matches the lfx2 root descriptor, so conversion is shared through psd/psd_layer_effects.hpp (`layer_style_from_lefx_descriptor`, `photoshop_lfx2_layer_style_payload`; the writer re-reads its own payload with `read_descriptor` and re-classes it "Lefx"). ZString names ("$$$/key=Display Name") resolve to the display text; Patchy writes plain names. Trailing 8BIMphry hierarchy data is ignored. Robustness mirrors pat_reader: 32 MiB cap, per-style skip-with-warning, decoded-prefix retention, id repair. Unmodeled blending options (fillOpacity, knockout, channel restrictions) warn and drop; custom Satin contours normalize to Linear at import with a warning. `asl_writer_bytes_are_stable` is the byte canary.

The descriptor engine gained the 'obj ' Action Manager REFERENCE value type (DescriptorReferenceItem, read + byte-identical write) for blendOptions' per-channel 'Chnl' references.

## Fixtures and tests

- `test-fixtures/asl/photoshop-style-blend-options.asl`: PS 2026-authored single-style export (our own probe content) pinning the populated blendOptions layout; its Blend If values cross-check the pinned `photoshop-blend-if-4b-roundtrip.psd` bytes.
- `local-test-fixtures/asl/Abstract Styles.asl`: untracked copy of PS's shipped file for `asl_reader_reads_photoshop_shipped_styles_if_available` (Adobe asset — never commit).
- UI coverage: `ui_style_library_defaults_restore_export_import_round_trip` (writes the `style_preset_thumbnails.png` contact sheet artifact), `ui_layer_style_styles_page_applies_preset_and_previews`, `ui_layer_style_no_style_entry_clears_effects`, `ui_layer_style_new_style_saves_current_settings`, `ui_style_browser_folder_selection_exports_asl`, `ui_style_manager_lists_renames_and_uses_styles`, `ui_style_preset_pattern_apply_cancel_restores_document_store`.

## Gotchas

- **PS re-saves of multi-instance styles use 'lmfx'**: Photoshop stores stacked instances (Comic Pow's two strokes) in the 'lmfx' tagged block (same payload shape as lfx2, the parser reads the `*Multi` lists) and writes a single-instance compatibility lfx2 beside it. Patchy treats lmfx as authoritative in either block order, preserves it raw on untouched layers, and `clear_layer_psd_style_source` + the writer strip drop it on edit so a stale lmfx can never shadow a regenerated lfx2 in Photoshop. Pinned by `psd_photoshop_lmfx_multi_effects_fixture_round_trips` on the PS-authored `photoshop-lmfx-multi-stroke.psd` (the acceptance-probe PSD).

- The Styles page's browser lives inside the dialog whose preview machinery guards on `loading_controls`; preset apply sets the master opacity/blend controls (and `show_effects`) under that guard before `rebuild_category_list` + `emit_preview(true)`. Blending options in a preset do NOT overwrite a preserved-unsupported Blend If payload unless the user already chose "Replace with Editable Defaults".
- Blend If ranges ride blendOptions only when non-identity; parse accepts Gry/Rd/Grn/Bl channel references and ignores others with warnings.
- lupdate is NOT part of the translation flow here: patchy_ja.ts is hand-maintained (a bulk `lupdate -recursive src` run marks the hand-added dynamic-name entries vanished and kills them — see the pattern/contour preset names). Add entries by hand in the runtime context (`patchy::ui::<Class>` for tr(), QObject for QObject::tr/translate).
