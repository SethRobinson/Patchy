# Type Mask tool

GitHub issue 670. The Type Mask tool (`toolTypeMaskAction`, hotkey id `tools.type_mask`, no
default key; `CanvasTool::TypeMask`) shares the Type tool's slot: the Type flyout
(`typeToolButton`, `typeToolMenu`) holds Type and Type Mask, and Shift+T (`tools.cycle.type`,
`toolCycleTypeAction`) walks them ([tool-flyouts.md](tool-flyouts.md)). Typed text becomes a
selection in the shape of the letters; no layer is made. Vertical mask text follows the Type
tool's Vertical toggle, so there is no separate vertical tool. Icon: `tool-type-mask.svg`, a
dashed T outline with an accent fill.

## Session

- It is a Type tool session (`MainWindow::add_text_at`) marked `patchy.typeMask` on the editor:
  the same editor, options bar (font, style, size, smoothing, alignment, Vertical, Character and
  Paragraph; not the color swatch or Warp), commit keys and click-off commit as the Type tool
  ([text-tool.md](text-tool.md#session-lifecycle-provisional-layer-commit-cancel)).
- A press always starts new mask text (`CanvasWidget::mousePressEvent`): it never opens a text
  layer, never fills a shape's area, and a double-click never edits a text layer. A click places
  point text, a drag of at least 16 px draws a box. The press's own modifiers are kept
  (`type_mask_press_modifiers`) and choose the combine on commit: none replaces, Shift adds, Alt
  subtracts, Shift+Alt intersects (`patchy.typeMaskOperation`, combine_selection_coverage's 0 to 3).
- Esc always cancels, whatever Preferences' "Use Esc to commit text" says: the mask is a
  selection edit, so there is nothing typed to keep.
- The provisional layer (named Type Mask) has opacity 0, so the preview, which copies its
  opacity, draws no glyphs. While the session is open the canvas draws a half-opacity tint in
  the `canvas_type_mask_tint` theme role (red, like Quick Mask) over the document, with the
  preview layer's letters cut out of it (`CanvasWidget::draw_type_mask_overlay`). The cut-out is
  built from the preview's pixels only when they change (`TypeMaskHoleCache`), never per repaint.
- With no session the Vertical toggle always arms the next mask session; it never converts a
  selected text layer while Type Mask is the tool.

## Commit

- `commit_text_editor` builds the text layer as for a new Type commit but never adds it; it
  takes no "Type" snapshot. `MainWindow::apply_type_mask_selection` (main_window_text_outlines.cpp)
  draws that layer's glyph outlines (`text_layer_outlines`, Create Work Path's source,
  [text-outlines.md](text-outlines.md)), never a raster trace, fills them anti-aliased
  (`make_path_selection_coverage`) and combines them with the selection in one
  `run_selection_command("Type Mask")` history step. Undo restores the previous selection.
- Empty text or Esc leaves the selection, the layers and history as they were.
- It works with any layer kind active and with no layer selected. Quick Mask disables it, like
  the Type tool.

## Tests

`tests/ui/type_mask_tests.cpp`: `ui_type_mask_commit_selects_the_letters_without_a_layer`
(bounds within 2 px of the same text's layer transparency, IoU above 0.85, no layer, one undo),
`ui_type_mask_modifiers_combine_with_the_selection`, `ui_type_mask_escape_cancels`,
`ui_type_mask_vertical_toggle_and_cycle`, `ui_type_mask_review_captures` (artifacts
`ui_type_mask_flyout`, `ui_type_mask_typing`, `ui_type_mask_selection`).
