# Character panel, Paragraph panel and multi-layer edits

Split from [text-tool.md](text-tool.md): options-bar and Character panel edits that reach every selected text layer without a session, where color picks land while text is being edited, the Character and Paragraph panels, and the Type menu.

## No session: every selected text layer (GitHub issue 31)

- **Family, size, face, anti-alias, alignment, color, the bar's toggles and the Character panel
  reach every selected text layer.** Each options-bar slot reads its widget FIRST, then
  `apply_text_character_edit` runs one hidden session per layer of
  `text_character_target_layer_ids()` (the active text layer, then the panel selection; unlocked
  text layers only) and commits them as ONE "Type" step: the first commit that reaches a push
  site takes the snapshot and `text_commit_snapshot_suppressed_` skips the rest. A layer whose
  edit lambda returns false (faux bold on warped text) is canceled alone and its status error is
  re-shown after the loop. The panel selection is re-selected after (commits rebuild
  the rows); alignment selects the whole object first, like the direction combo.
- **Re-entrancy.** The hidden sessions rewrite the bar from each layer they open, so
  `applying_text_options_to_layers_` makes every slot return during a pass, and add_text_at's
  size-spin write is signal-blocked like the family combo. The size spin has keyboard tracking
  OFF (Enter, focus loss or a step applies). The text color panel is
  live, so its layer apply is debounced (`apply_text_color_to_selected_layers_debounced`). A
  Palette swatch click takes the same apply (Type tool or open panel;
  [palette-mode.md](palette-mode.md)).
- **Color picks during a session** (issue 371, `tests/ui/text_color_targets_tests.cpp`). The Text
  Color window (button OK, still live) finds the session open at each pick, so another word or
  layer selected meanwhile takes it; once the session it was opened from is over, a pick is a
  foreground pick. Foreground changes share `apply_foreground_color` (canvas foreground, the
  session's selection, live shape paint): the Foreground Color window and the Color panel;
  Swatches, Swap and Default Colors recolor the selection too. A click in the Color or Swatches
  panel is exempt from the focus-loss commit (`is_text_option_widget`; the Color panel takes
  click focus itself) and hands the keyboard back.
- **The bar mirrors the active text layer** (`sync_text_options_from_active_layer`, from
  `refresh_options_bar` and `refresh_layer_controls`, keyed so an unchanged layer costs nothing;
  the alignment buttons read the first paragraph run). A mixed selection shows the active
  layer's values, the color button keeps the primary color, and a pixel layer leaves the bar
  alone (it seeds the next new layer). Sizes travel through the zoomed editor, so a size typed at
  a very low zoom can round; the bar shows what landed. Sizes are whole document pixels: the bar shows the fewest-decimal points naming them (`text_pixels_to_points`: 67 px at 96 ppi shows 50, not 50.25) and keeps a typed value that names the same pixels (`text_points_for_display`; issue 371, `ui_text_size_*`).
- Tests: `ui_text_options_bar_*_to_selected_layers_without_session`,
  `ui_text_options_bar_follows_active_text_layer`,
  `ui_text_character_panel_edits_all_selected_layers_without_session`.

## Options bar toggles

GitHub issue 673; built in `main_window_actions_options_bar.cpp`, synced by
`sync_text_options_bar_toggles` (from `sync_text_character_dialog_from_editor`, so at every
session, selection and commit boundary; a layer is read once per revision) and
`sync_text_alignment_buttons_from_editor` / `sync_text_orientation_controls_from_editor`.

- **Bold, Italic, Underline** are the Character panel's faux bold, faux italic and underline,
  through the same apply calls (`apply_text_character_faux_bold_to_active_editor(bool)`, the
  italic twin, `apply_text_character_style_to_active_editor(4, on)`), so one Type step each
  and the panel and bar show each other's changes. They do not seed new text, and turn off with
  no session or text layer to edit.
- **Justify** sits on top of L / C / R, which show the last line; it is its own toggle, so the bar
  puts it in the Vertical and RTL section, and its icon is four full lines: Left + Justify is `justify`,
  Center + Justify `justifyCenter`, Right + Justify `justifyRight`; `justifyAll` checks Justify
  with none of L / C / R (the sync lifts the group's exclusivity to clear them). Pressing L, C
  or R while Justify is on keeps it and changes the last line; Justify off returns to the plain
  alignment (Left from Justify all).
- **Vertical** and **RTL** are icon buttons (`text_flow_toggle_icon`), named for screen readers
  and explained by their tooltips.
- **RTL** is checked only for an explicit right to left paragraph (Auto shows unchecked); a
  press applies right to left, the next left to right, through
  `apply_text_direction_to_active_editor`. Off with no text to edit.
- **Anti-alias** is not on the bar. `text_anti_alias_` holds what the Type tool shows (the
  session's or target layer's value, else the next new layer's, saved as `tools/textSmoothing`);
  `show_text_anti_alias` mirrors it into the Character panel's Anti-alias list and
  `choose_text_anti_alias` (the list and Type > Anti-Alias) applies, saves and mirrors.
- Tests (`tests/ui/text_options_bar_toggles_tests.cpp`):
  `ui_text_options_bar_style_toggles_follow_the_character_panel`,
  `ui_text_options_bar_justify_toggle_sets_the_justify_modes`,
  `ui_text_options_bar_rtl_toggle_sets_the_paragraph_direction`,
  `ui_text_anti_alias_default_survives_without_the_bar_list`.

## Character panel

- Options bar > Character... (Text tool); two style rows first ([text-character-styles.md](text-character-styles.md)), leading with Auto leading below, tracking, H/V scales, Rotate Latin, Anti-alias (the options bar's old Smoothing list; it stays enabled with no text, since it also sets the next new layer's value). The leading field is never locked: a typed value turns Auto leading off. Number fields apply on Enter, focus loss or +/-. Inline, it applies to the selection (a bare caret: the whole object); otherwise to every selected unlocked text layer (no-session section above), warped text included, and stays usable after switching tools.
- **Faux bold is refused on warped layers** (Photoshop parity, [warp-text.md](warp-text.md)): enabling Faux Bold on a session whose layer has an active Warp Text shows a status error and reverts; unchecking stays allowed so imported faux+warp files can be fixed. Ctrl+B's faux fallback refuses the same way; a family with a real Bold face keeps toggling. Faux italic is unrestricted.
- Without an inline session, the panel reads the first character's format from the active layer's stored runs (transform vertical scale and document resolution applied to displayed leading). A change opens a hidden session per target layer (`add_text_at(..., show_editor=false)`) and commits through the normal Type undo/render path, without preview or focus; mixed run sizes and colors survive. Opening the panel mutates nothing. The scripting `text` setter rides the same session ([scripting.md](scripting.md)).
- Controls disable with neither a live session nor an editable text layer (pixel locks, transform sessions); `refresh_options_bar` and `refresh_layer_controls` resync the panel on session, selection, lock and history changes. Tests: `ui_text_character_panel_tracks_session_and_layer`, `ui_text_character_panel_edits_selected_layer_without_session`.
- Double-clicking a T thumbnail opens the layer with all text selected (Type tool, zoom and pan kept); the deferred callback re-checks the session since entry may rebuild the rows (`ui_text_thumbnail_double_click_selects_all_without_zoom`).
- Fixed leading opts the layer into the Photoshop layout marker at commit (Qt-natural layout ignores it).

## Paragraph panel

- Options bar > Paragraph... (Text tool); edits alignment (Left, Center, Right and the four justify modes, [text-tool.md](text-tool.md#justify-modes)) plus Photoshop's five metrics (first line, left and right indent, space before and after). The fields show points at the document's text PPI and store document pixels, as `patchy.text.paragraph_runs` (v2 columns 4 to 8) and the TySh EngineData carry them. A negative first line indent with a positive left indent is a hanging indent (`/Hanging true` in the EngineData).
- Same session model as the Character panel: inline, a change merges into the block formats of the paragraphs the selection touches (a bare caret: its own paragraph); with no session, into every paragraph of every selected unlocked text layer through the hidden-session path, one Type undo step. Block margins live in editor pixels (document px times the canvas zoom, divided by a PSD-frame session's display scale, the same conversion leading uses).
- The panel reads the caret's paragraph in a session, else the active layer's first paragraph (1:1 render document); `sync_text_character_dialog_from_editor` and the alignment sync refresh it.
- Scripting: `layer.textParagraph` (read the first paragraph, set merges into every paragraph) and the `paragraph` option of `addTextLayer`, document pixels ([scripting.md](scripting.md)). Tests: `ui_text_paragraph_panel_sets_indents_and_spacing`, `ui_script_text_paragraph_reads_and_sets_metrics`.
- Lists (bullets, numbering) are not modeled: Photoshop stores them only in its Txt2 block, and Patchy's Txt2 writer authors no list style ([txt2.md](txt2.md), "Lists"). Scripted posters type a bullet character into the line instead.

## Type menu

GitHub issue 650; built in `main_window_actions_tool_palette.cpp` (`typeMenu`). Rows: Character..., Paragraph...; Alignment, Direction, Orientation; Anti-Alias, OpenType; Create Work Path, Convert to Shape ([text-outlines.md](text-outlines.md)); Rasterize Type Layer, Warp Text...; Resolve Missing Fonts.... The Type tool is not a row (the palette and T pick it).

- **Every entry calls its control's own path**, so it is the same single "Type" step: the dialogs' openers, `apply_text_alignment_to_active_editor` (an alignment name, the four justify modes included), `apply_text_direction_to_active_editor`, `apply_text_orientation`, `choose_text_anti_alias` (the Character panel's Anti-alias list too: applies and stores `tools/textSmoothing`), `apply_text_character_style_to_active_editor` (the Character panel's second toggle row), `rasterize_layer_ids` on the selected text layers only (`rasterize_text_layers`), `request_warp_text_dialog`. Never add a second apply path here.
- **Checks and enabled state come from `refresh_type_menu_states`** on the menu's `aboutToShow` (and `show_command_search`): the live session, else `text_character_target_layer()`, else the next new layer's seeds. Alignment, Direction and OpenType need text; Orientation and Anti-Alias are also on with the Type tool active (they seed the next layer, like their bar controls). Only the submenu entries turn off: the actions stay enabled so a key bound to one still fires, and an OpenType toggle re-reads the text before flipping, so a stale check never inverts it.
- Hotkey ids `type.character_panel`, `type.paragraph_panel`, `type.align_*`, `type.direction_*`, `type.orientation_*`, `type.anti_alias_*`, `type.opentype_*`, `type.rasterize`, `type.warp_text`; no default keys. Oldstyle figures and Ornaments are not modeled (no `onum`/`ornm` feature in the text code), so the OpenType submenu leaves them out.
- Tests: `ui_type_menu_rows_and_enabled_states`, `ui_type_menu_applies_to_selected_text_layer` (tests/ui/text_outline_tests.cpp).
