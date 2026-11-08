# Text tool and Character panel

Text sessions, commit/cancel and the Character panel. See [text-render-calibration.md](text-render-calibration.md) for typography, [area-text.md](area-text.md) for shape boundaries, [warp-text.md](warp-text.md) for Warp Text, [text-outlines.md](text-outlines.md) for Convert to Shape and [testing.md](testing.md) for font registration.

`text_font_service` owns font resolution; `text_document` constructs the owning `QTextDocument`; `text_render` builds and draws the shared plan. MainWindow owns editor sessions, commit/cancel and layer metadata adaptation. Extend these interfaces instead of adding member-file splits.

Related documents:

- [text-tool-layout.md](text-tool-layout.md): the line plan that draws text and answers caret, selection and hit-testing (`TextLineGeometry`), the auto leading handed to Photoshop, and how document geometry operations follow the text transform.
- [text-tool-panels.md](text-tool-panels.md): options-bar and Character panel edits that reach every selected text layer without a session, color picks during a session, and the Character and Paragraph panels.
- [type-mask.md](type-mask.md): the Type Mask tool, a Type session whose commit makes a selection from the letters and no layer.

## The text on screen is never missing

An edit session must never produce a frame with no glyphs in it:

- **The edited layer stays visible until its replacement is ready.** `add_text_at` does not hide
  it; `hide_text_editor_source_layer` does, inside `update_text_editor_preview`, once the
  preview pixels are in place, returning the vacated region so hide and reveal land in ONE
  `document_changed_effect_bounds` call (hiding up front blanks the text until the first preview).
- **The debounce never removes the preview.** An expensive style re-renders on the longer delay;
  the last good preview keeps drawing until the new one lands.
- **The debounce never starves.** The delay counts from the oldest edit not drawn yet
  (`patchy.textPreviewPendingSinceMs`), and an auto-repeated key previews at once (the event
  filter sets `patchy.textPreviewNow`), so a held Enter moves the glyphs with the caret (issue
  413, `ui_text_held_enter_relayouts_each_repeat`).

`kTextEditorPreviewPaintProperty` means "the glyphs come from somewhere other than this widget",
true for any previewed session (`ui_expensive_text_style_preview_never_blanks_while_typing`).

Re-editing a layer must not MOVE its text: each such session renders live through
`render_text_pixels`, plain text included (`kTextEditorForceBakedPreviewProperty`); the editor
widget rasterizes differently. `ui_text_edit_entry_leaves_the_pixels_alone`
pins it: enter, do nothing, and the preview and a re-commit are byte-identical at the same origin.
Nor may an edit move what it did not touch: a session pinned to its source ink places every render
from the session-start render's ink offset and first baseline (`kTextEditorSourceInkOffsetProperty`),
not the new ink, so the pen stays put as one word grows, as in Photoshop.

**Every session previews, including the one that creates the text.** A new session renders over
its provisional layer, and `restore_active_layer` is that provisional (not whatever was active
before the click) so the preview insert cannot steal the layer-panel selection. Glyphs are
debounced, so a test that changes an option and measures pixels must let the preview land first.
Ending a session must not flash either: `restore_text_editor_source_layer` puts the edited layer
back BEFORE `remove_text_editor_preview`, in commit and cancel.

## Session lifecycle (provisional layer, commit, cancel)

- A Type-tool click inserts a provisional 1x1 text layer (marker `patchy.internal.provisional_text`); `commit_text_editor` removes it via the marker-checked `MainWindow::take_provisional_text_layer`, then snapshots and recreates the committed layer under the same id; cancel/empty-commit leaves history and modified state untouched.
- Committing empty text to an existing unlocked text layer clears its text and raster (undoable Type edit); canceling a new empty session removes only its provisional layer.
- **A no-op commit adds no step** (issue 387): an existing layer's commit runs on a copy first; a result equal to the layer skips `push_undo_snapshot` (`ui_text_commit_unchanged_adds_no_history_step`).
- **Commit invalidation must cover old ∪ preview ∪ new.** The restore/remove teardown pair invalidates its regions BEFORE the layer mutates, so `commit_text_editor` captures the old layer and preview render bounds up front and unions them into the post-mutation `document_changed_effect_bounds`, or the old render stays baked in the canvas cache outside the new bounds (routine on warped layers). `hide_text_editor_source_layer` likewise returns the vacated rect without invalidating; callers must consume it.
- **Warped text layers get a warp-aware session**: entry resolves a Move-corrected unwarped transform and gates off every raster-derived anchor. See Warp Text in [warp-text.md](warp-text.md).
- **The PSD-frame session holds only a scale.** It edits the transformed frame's bounding box as a document rect, so `layer_should_edit_with_psd_text_frame` refuses a rotated or sheared frame, which edits through its transform on the overlay like any transformed text (GitHub issue 373; `ui_rotated_psd_box_text_edit_keeps_transform`). The transformed resample folds the render buffer's offset in for box text too: a Photoshop-layout box's first line box starts above the frame top.
- **A preview never runs inside a canvas processing wait** (`processing_render_wait_active`): the worker composites the live document while the wait pumps timers, so `update_text_editor_preview` re-arms instead of swapping the preview layer under it (an access violation in `layer_alpha_mask`, `ui_duke_psd_text_runs_survive_reedit`).
- **The box outline** is `draw_text_box_outline`, a light line under accent dashes that reads over any ink (issues 385 and 371). The flat editor paints it in its transparent 1 px style-sheet border from `InlineTextEdit::event` (the frame paint, which the viewport `paintEvent` never sees); the overlay draws it through the transform.
- Commit keys (`MainWindow::eventFilter`): Ctrl/Cmd+Return and the bare keypad Enter (`Key_Enter` + `KeypadModifier`, also Mac fn+Return); plain Return is a line break (GitHub issue 71, `ui_text_keypad_enter_commits_and_return_breaks_line`); Escape commits while Preferences > Tools "Use Esc to commit text" (`input/escapeCommitsText`, default on since issue 415; the UI test main turns it off) is on and cancels when it is off (issue 173, `ui_text_escape_commits_preference`), except on a new session still showing the untouched "Type" placeholder, which Esc always drops (issue 415, `ui_text_escape_drops_the_untouched_placeholder`).
- The wheel over the inline editor (frame or viewport) is re-sent to the canvas, so it scrolls and zooms as anywhere else (issue 413, `ui_text_wheel_over_the_editor_moves_the_canvas`); QTextEdit must never scroll its own document or zoom its font.
- Clicking off commits through the focus-loss handler, which arms `swallow_next_canvas_left_press_` so the press that caused the commit cannot start the next session; a release clears a stale flag. The canvas event filter must leave that flag alone during a blocking processing wait: on wasm the mouseup arrives re-entrantly inside the commit's undo-snapshot wait ([wasm.md](wasm.md); `ui_text_click_off_commit_ignores_reentrant_release_during_wait`).
- Focus moving to nowhere (`focusChanged(editor, nullptr)`, an app switch) parks the session (issue 330): reactivation refocuses the editor; focus landing elsewhere first commits (`ui_text_session_survives_app_switch`).
- Mutating actions that take no focus (layer lock buttons) call `finish_active_text_editor()` first, or they act on a half-committed session.
- Type-tool Ctrl+drag moves text: [move-tool.md](move-tool.md#type-tool-ctrldrag).

## Delete semantics

Delete on a text layer deletes the OBJECT, never its pixels (clearing would leave an invisible layer whose metadata resurrects the text); `clear_active_layer` special-cases it; mixed selections clear pixels and delete text layers in one undo step.

## The overlay must accept click focus

`TransformedTextEditOverlay` covers the text it is editing, so it is what a click on transformed
text hits, and it must take `Qt::ClickFocus`: with `Qt::NoFocus` Qt's focus-before-press walk
(`giveFocusAccordingToFocusPolicy`) skips to the CanvasWidget behind and the focus-loss
auto-commit reads that as clicking off. The overlay path is reached from the SECOND edit of an
imported layer on (committing writes a patchy transform that moves the next session off the
PSD-frame path, where the QTextEdit takes focus itself). Focus landing on the overlay is exempt
from the auto-commit (`is_text_option_widget` objectName match); `mousePressEvent` hands focus
back to the editor. No focus proxy: clearing one while it holds focus makes Qt reassign the focus widget, committing
the session mid-call (`configure()` runs on every cursor move). Real-click tests use `click_widget_like_a_user`
(tests/ui/ui_test_support.cpp), which applies the focus walk first; `send_mouse` skips it.

## Options bar while an editor is open

- The options bar shows session apply/cancel buttons (`textApplyButton`/`textCancelButton`) while an editor is open; they must keep `Qt::NoFocus`, or the focus-loss auto-commit fires on mouse press and Cancel commits instead of canceling.
- The font combo is a `FontPickerCombo` ([font-picker.md](font-picker.md): search, Recent, Style/Language filters, preview); its popup objectName `textFontPickerPopup` must stay matched by `is_text_option_widget` (a Qt::Popup is a window, so isAncestorOf misses it and focusing its search box auto-commits).
- **A popup pick must apply even when its row is already current.** A family the database lacks has no row, so the control parks on another row while `currentFont()` names the missing one; the commit sets the index, then calls `setCurrentFont` while `currentFont().families().value(0)` still disagrees.
- New UI that must coexist with an open text session needs the same `is_text_option_widget` exemption (`textCharacterDialog`, `textParagraphDialog` and their buttons have it).
- **The keyboard returns to the session** (issue 175), so the next key replaces a selected "Type" placeholder: color, alignment and Warp buttons are `Qt::NoFocus`; a combo pick (`activated`, emitted by the font popup too), Enter in the size field, closing its slider popup or the text color panel, and Enter, checkbox or combo edits in the Character/Paragraph panels call `return_keyboard_to_text_editor`. Warp Text commits for its dialog, then reopens the session with its selection (`ui_text_placeholder_survives_*`).
- The inline editor claims Bold, Italic and printable Ctrl+Alt (AltGr) text in `ShortcutOverride` before app hotkeys (Ctrl+B Color Balance, Ctrl+I Invert, Ctrl+Alt+E) take them; Bold and Italic route to `toggle_text_bold_face` / `toggle_text_italic_face`.

## Boxed-text render clipping

Which lines a rectangular box shows and how they clip: [area-text.md](area-text.md#box-text-overflow).

## The style picker

The face control (`textStyleCombo`), recorded styles and Ctrl+B/Ctrl+I: [font-resolution.md](font-resolution.md#the-style-picker-faces-bold--italic-cannot-name).

**Options-bar changes with NO selection apply to the whole type object**, as in Photoshop:
`merge_text_char_format` selects the whole document for a bare caret instead of falling through
to `mergeCurrentCharFormat`, which only formats the NEXT typed character.

## Font resolution

[font-resolution.md](font-resolution.md) (display name to Qt font, flag flattening, GDI names, exact sizes); read it before touching `render_text_font_for_display_family`, `available_text_family_style_match`, or `psd_text_read.cpp`'s DirectWrite resolver.

## Vertical text and paragraph direction

Vertical type (`patchy.text.orientation = vertical`) and the paragraph base direction
(`patchy.text.paragraph_runs` v4 column 9: `auto`/`ltr`/`rtl`). The layout model and its
Photoshop calibration are in [text-render-calibration-vertical-rtl.md](text-render-calibration-vertical-rtl.md); this is
the session contract.

- **One Type tool, an orientation toggle.** `textOrientationButton` (and the layer context
  menu entry, and Type > Orientation) switches a live session in place, converts the selected layer through the
  Character-panel hidden session (one undo step), or arms the NEXT new layer once; never
  persisted, a fresh session starts horizontal. With no session it reads the first layer of
  `text_character_target_layer_ids()`, so a selected vertical row keeps it checked even with no
  active layer; only Deselect Layers makes it read the armed default
  (`ui_new_text_starts_horizontal_after_vertical_layer`). `textDirectionCombo` (per paragraph)
  and `textParagraphVerticalCheck`, the toggle's mirror, live in the Paragraph panel and exist
  only while it is open (issue 91).
- **The plan is the authority, again.** `vertical_text_layout_plan` (ui/text_layout.hpp,
  [text-tool-layout.md](text-tool-layout.md)) re-places every grapheme cluster of the horizontally shaped NoWrap document into a cell;
  `TextLineGeometry::from_vertical_plan` answers caret, selection and hit-testing from the plan
  `draw_vertical_text_plan` draws. `vertical_render_plan` and its `_for_editor` twin add the
  bleed and must mirror each other's scales.
- **The anchor moves the widget, not the text.** Vertical text grows LEFT (and UP when centred
  or bottom-aligned), so `relayout_text_editor` re-derives the widget origin from
  `kTextEditorVerticalAnchorProperty` (`vertical_text_layer_anchor` recovers a re-edit's from
  the raster). Imported PSD vertical layers keep the ink-anchor path.
- **Arrow keys follow the columns** (`InlineTextEdit::keyPressEvent`): Up/Down step along the
  column, Left/Right jump between columns.
- **The IME is placed from the drawn caret.** `InlineTextEdit::inputMethodQuery` answers
  `ImCursorRectangle` with `text_editor_input_method_rect` (the caret's line, or the whole
  remaining column for vertical text, mapped through the overlay when transformed). The preedit
  is mirrored into `kTextEditorPreeditTextProperty` and `document_from_editor_in_document_units`
  inserts it at the cursor, so every render, the caret and an interrupting commit carry it.
- **Right-to-left needs no shaping work** (Qt runs bidi and HarfBuzz in QTextLayout). Alignment
  is logical, so the anchor helpers use `resolved_block_direction`. Spell non-ASCII test
  literals as `\x` escapes.
- Scripting: `doc.addTextLayer(text, {orientation, direction})`, `layer.textOrientation` /
  `textDirection`. Tests: `tests/ui/text_vertical_rtl_tests.cpp`.
- Known gaps: tate-chu-yoko, kinsoku, vmtx metrics, transformed PSD vertical imports (horizontal
  re-anchoring), uncalibrated vertical Warp Text, SVG export rasterizes it.
