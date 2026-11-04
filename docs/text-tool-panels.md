# Character panel, Paragraph panel and multi-layer edits

Split from [text-tool.md](text-tool.md): options-bar and Character panel edits that reach every selected text layer without a session, where color picks land while text is being edited, and the Character and Paragraph panels.

## No session: every selected text layer (GitHub issue 31)

- **Family, size, face, smoothing, alignment, color and the Character panel reach every
  selected text layer.** Each options-bar slot reads its widget FIRST, then
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

## Character panel

- Options bar > Character... (Text tool); two style rows first ([text-character-styles.md](text-character-styles.md)), leading with Auto leading below, tracking, H/V scales, Rotate Latin. The leading field is never locked: a typed value turns Auto leading off. Number fields apply on Enter, focus loss or +/-. Inline, it applies to the selection (a bare caret: the whole object); otherwise to every selected unlocked text layer (no-session section above), warped text included, and stays usable after switching tools.
- **Faux bold is refused on warped layers** (Photoshop parity, [warp-text.md](warp-text.md)): enabling Faux Bold on a session whose layer has an active Warp Text shows a status error and reverts; unchecking stays allowed so imported faux+warp files can be fixed. Ctrl+B's faux fallback refuses the same way; a family with a real Bold face keeps toggling. Faux italic is unrestricted.
- Without an inline session, the panel reads the first character's format from the active layer's stored runs (transform vertical scale and document resolution applied to displayed leading). A change opens a hidden session per target layer (`add_text_at(..., show_editor=false)`) and commits through the normal Type undo/render path, without preview or focus; mixed run sizes and colors survive. Opening the panel mutates nothing. The scripting `text` setter rides the same session ([scripting.md](scripting.md)).
- Controls disable with neither a live session nor an editable text layer (pixel locks, transform sessions); `refresh_options_bar` and `refresh_layer_controls` resync the panel on session, selection, lock and history changes. Tests: `ui_text_character_panel_tracks_session_and_layer`, `ui_text_character_panel_edits_selected_layer_without_session`.
- Double-clicking a T thumbnail opens the layer with all text selected (Type tool, zoom and pan kept); the deferred callback re-checks the session since entry may rebuild the rows (`ui_text_thumbnail_double_click_selects_all_without_zoom`).
- Fixed leading opts the layer into the Photoshop layout marker at commit (Qt-natural layout ignores it).

## Paragraph panel

- Options bar > Paragraph... (Text tool); edits alignment (Left, Center, Right, Justify last-left, the only justify variant modeled) plus Photoshop's five metrics (first line, left and right indent, space before and after). The fields show points at the document's text PPI and store document pixels, as `patchy.text.paragraph_runs` (v2 columns 4 to 8) and the TySh EngineData carry them. A negative first line indent with a positive left indent is a hanging indent (`/Hanging true` in the EngineData).
- Same session model as the Character panel: inline, a change merges into the block formats of the paragraphs the selection touches (a bare caret: its own paragraph); with no session, into every paragraph of every selected unlocked text layer through the hidden-session path, one Type undo step. Block margins live in editor pixels (document px times the canvas zoom, divided by a PSD-frame session's display scale, the same conversion leading uses).
- The panel reads the caret's paragraph in a session, else the active layer's first paragraph (1:1 render document); `sync_text_character_dialog_from_editor` and the alignment sync refresh it.
- Scripting: `layer.textParagraph` (read the first paragraph, set merges into every paragraph) and the `paragraph` option of `addTextLayer`, document pixels ([scripting.md](scripting.md)). Tests: `ui_text_paragraph_panel_sets_indents_and_spacing`, `ui_script_text_paragraph_reads_and_sets_metrics`.
- Lists (bullets, numbering) are not modeled: Photoshop stores them only in its Txt2 block, and Patchy's Txt2 writer authors no list style ([txt2.md](txt2.md), "Lists"). Scripted posters type a bullet character into the line instead.
