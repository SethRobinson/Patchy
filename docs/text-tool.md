# Text tool and Character panel

The inline text editor's session machinery, commit/cancel semantics, and the Character panel. The Photoshop layout/measurement model (engine units, leading, faux faces, run-format columns) lives in [text-render-calibration.md](text-render-calibration.md); Warp Text is in [warp.md](warp.md), offscreen font registration in [testing.md](testing.md).

Do NOT split the remaining text code out of main_window.cpp as a pure file move: the render pipeline is shared between too many members; design a module with its own header instead.

## One layout authority (src/ui/text_layout.hpp)

The renderer draws a text layer by walking a LINE PLAN (one `QTextLine` per visual line with the
origin it is drawn at) rather than letting QTextDocument place the lines, because Photoshop's
leading model moves baselines off Qt's natural spacing. `text_layout.hpp` owns that plan and is
the single geometric authority: `photoshop_text_layout_plan` / `boxed_text_render_plan` produce
it, and `TextLineGeometry` reads the same plan back for caret rects, selection rects and
hit-testing. Build it with the same `boxed` / `photoshop_layout` flags the render pass got and its
answers are guaranteed to agree with the drawn glyphs.

Caret and selection geometry MUST go through `TextLineGeometry`, never Qt's natural
`blockBoundingRect` origins: those drift roughly (leading - Qt line spacing) px per line on a
PS-model layer (pinned by `ui_psd_text_caret_follows_photoshop_leading`). `BoxTextLineRenderItem::block_position` exists only for this:
`QTextLine::lineNumber()` is an index within its own block's layout, so the owning block cannot be
recovered from the line alone. Caret lookup resolves the owning block FIRST (as
`QTextDocument::findBlock` does): a document-wide line scan answers the previous block for a
position at the start of the next one.

**The caret layout must be built with the same SCALES as the render pass**:
`build_text_editor_document_space_layout` mirrors `update_text_editor_preview` argument for
argument (`metric_scale` AND the PSD-frame `layout_scale`, `text_editor_size_display_scale` when
`photoshop_layout && usesPsdTextFrame`). A frame session keeps its runs in raw engine units and
folds the frame's vertical scale in only at render time, so a default `layout_scale` of 1.0 lays
the caret out at the raw size while the glyphs draw scaled
(`ui_psd_frame_text_highlight_matches_scaled_glyphs`, which probes by clicking mid-INK from the
render: click and caret share a layout and agree even when it is wrong).

Mouse hit-testing goes through the same plan; `QTextEdit::cursorForPosition` must never resolve a
click inside a session (the widget hit-tests its own integer-pixel, Qt-spaced layout).
`MainWindow::handle_text_editor_viewport_mouse_event` intercepts left press/drag/double-click for
the flat case and `TransformedTextEditOverlay::cursor_position_for_overlay_point` the transformed
one; both end at `TextLineGeometry::position_at`; right-click, middle click and release fall
through. `ui_psd_text_click_returns_to_the_caret_it_drew` pins the round trip. The editor widget
is SIZED from that layout too, never `QTextDocument::size()`: its rect is its hit area, and a
line past a too-short bottom edge clicks through to the canvas, whose focus-loss auto-commit ends
the session (`ui_transformed_text_click_returns_to_the_caret_it_drew`).

`render_text_pixels_with_local_rect` = `build_text_render_plan` (layout, line plan, local rect,
post-fold residual transform) + `draw_text_render_plan(plan, QPainter&)`. The raster path draws
the plan into a QImage; `draw_text_layer_to_painter` (`ui/text_layer_painter.hpp`, editable PDF
export) draws the same plan through the layer's canonical text transform, so PDF text lands on
the raster. Keep the two consumers on one plan. `local_rect` is text-local (origin = line start and
first line top = the transform origin) and starts left of or above (0, 0) when glyph ink
overhangs, so placements add `local_rect.topLeft()` to the transform translation; the buffer
corner is not the pen ([text-render-calibration.md](text-render-calibration.md), "Glyph ink").
The render also returns the plan's layout
metrics (first baseline, box baseline inset, auto-leading fraction); every caller storing the
pixels on a layer hands them to `store_text_layout_metrics` for the PSD writer
([text-render-calibration.md](text-render-calibration.md), "re-renders where Patchy drew it").

Every type in that header holds handles into the document's `QTextLayout`, valid only while
that document is alive and not laid out again; `build_text_render_document` does its final
`setTextWidth` before any plan is built. Keep that order.

## The text on screen is never missing

An edit session must never produce a frame with no glyphs in it:

- **The edited layer stays visible until its replacement is ready.** `add_text_at` does not hide
  it; `hide_text_editor_source_layer` does, inside `update_text_editor_preview`, once the
  preview pixels are in place, returning the vacated region so hide and reveal land in ONE
  `document_changed_effect_bounds` call (hiding up front blanks the text until the first preview).
- **The debounce never removes the preview.** An expensive style re-renders on the longer delay;
  the last good preview keeps drawing until the new one lands.

`kTextEditorPreviewPaintProperty` means "the glyphs come from somewhere other than this widget",
true for any previewed session (`ui_expensive_text_style_preview_never_blanks_while_typing`).

Re-editing an existing layer must not MOVE its text: each such session renders live through
`render_text_pixels`, plain text included (`kTextEditorForceBakedPreviewProperty`), because the
editor widget's own rasterization differs from the layer's. `ui_text_edit_entry_leaves_the_pixels_alone`
pins it: enter, do nothing, and the preview and a re-commit are byte-identical at the same origin.

**Every session previews, including the one that creates the text.** A new session renders over
its provisional layer, and `restore_active_layer` is that provisional (not whatever was active
before the click) so the preview insert cannot steal the layer-panel selection. Glyphs are
debounced, so a test that changes an option and measures pixels must let the preview land first.
Ending a session must not flash either: `restore_text_editor_source_layer` puts the edited layer
back BEFORE `remove_text_editor_preview`, in commit and cancel.

## Session lifecycle (provisional layer, commit, cancel)

- A Type-tool click inserts a provisional 1x1 text layer (marker `patchy.internal.provisional_text`); `commit_text_editor` removes it via the marker-checked `MainWindow::take_provisional_text_layer`, then snapshots and recreates the committed layer under the same id; cancel/empty-commit leaves history and modified state untouched.
- **Commit invalidation must cover old ∪ preview ∪ new.** The restore/remove teardown pair invalidates its regions BEFORE the layer mutates, so `commit_text_editor` captures the old layer and preview render bounds up front and unions them into the post-mutation `document_changed_effect_bounds`, or the old render stays baked in the canvas cache wherever the new bounds do not cover it (routine on warped layers). Same rule for `hide_text_editor_source_layer`: it returns the vacated rect and never invalidates itself, so every caller must consume the return.
- **Warped text layers get a warp-aware session**: entry resolves a Move-corrected unwarped transform and gates off every raster-derived anchor (the raster is the warped ink). See Warp Text in [warp.md](warp.md).
- Clicking off commits through the focus-loss handler, which arms `swallow_next_canvas_left_press_` so the press that caused the commit cannot start the next session; a release clears a stale flag. The canvas event filter must leave that flag alone during a blocking processing wait: on wasm the mouseup arrives re-entrantly inside the commit's undo-snapshot wait ([wasm.md](wasm.md); `ui_text_click_off_commit_ignores_reentrant_release_during_wait`).
- Mutating actions that take no focus (layer lock buttons) call `finish_active_text_editor()` first, or they act on a half-committed session.

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
back to the editor. Do NOT use a focus proxy: clearing one while it holds focus makes Qt reassign
the application focus widget, so the session commits out from under whoever was mid-call, and
`configure()` runs on every cursor move. Tests asking whether a real click reaches the right
widget use `click_widget_like_a_user` (tests/ui/ui_test_support.cpp), which applies the focus
policy walk before routing the press; `send_mouse` straight to the canvas asks something else.

## Options bar while an editor is open

- The options bar shows session apply/cancel buttons (`textApplyButton`/`textCancelButton`) while an editor is open; they must keep `Qt::NoFocus`, or the focus-loss auto-commit fires on mouse press and Cancel commits instead of canceling.
- The font combo is a `FontPickerCombo` (src/ui/font_picker.*, a QFontComboBox whose overridden showPopup opens a searchable list + writing-system preview); its popup objectName `textFontPickerPopup` must stay matched by `is_text_option_widget` (a Qt::Popup is a window, so isAncestorOf-based ownership misses it and focusing the search box would auto-commit the session).
- **A popup pick must apply even when its row is already current.** A family the database lacks has no row, so a control set to one parks on another family while `currentFont()` names the missing one; the commit sets the index, then pushes the family through `setCurrentFont` when `currentFont().families().value(0)` still disagrees.
- New UI that must coexist with an open text session needs the same `is_text_option_widget` exemption.
- The inline editor claims the standard Bold and Italic shortcuts in `ShortcutOverride` before the app-level Ctrl+B Color Balance and Ctrl+I Invert actions can consume them. The key press routes to `toggle_text_bold_face` / `toggle_text_italic_face` (see the style picker section below).

## Boxed-text render clipping

Every boxed render path gates whole LINES against the frame, never raster rows: a line straddling
the frame bottom draws completely (its clip band extends below the frame by the descent bleed)
and lines wholly past the frame stay hidden. The editor, Photoshop-layout and metadata re-render
paths share this rule in `render_text_pixels_with_local_rect` (skipping the line plan cuts the
straddling line mid-glyph; Affinity and Photoshop draw it whole). The metadata path keeps the
buffer origin and width at the frame's and grows only the bottom: pixels-only callers place
the buffer at the frame corner.

## The style picker, and what bold + italic cannot say

A family's faces are an arbitrary list; bold and italic name only four of them. The options
bar's style picker (`textStyleCombo`) is the ONLY face control, like Photoshop (no B/I buttons).
`PsdTextStyleRun::style` / runs v5 column 12 persist a face the flags cannot express;
`render_text_font_for_display_family` takes it as its last argument, applying `setStyleName`
when the family offers it and falling back to the flags otherwise (a stray style must
not render nothing).

- **A style is recorded only when the flags cannot express it.** The DirectWrite resolver and the
  picker both drop Regular/Bold/Italic/Bold Italic/Oblique variants
  (`text_style_is_flag_expressible` mirrors the reader's list), so ordinary imports and picks
  stay on runs v1-v4 and the byte-stability canaries do not move. The picker derives those four
  rows from `bold`/`italic` and previews each row in its own face (per-item `Qt::FontRole`).
- **A face name's flags union the database's answer; the database never vetoes the name.**
  (`text_style_flags_for_style`): `QFontDatabase::bold` calls only weight >= 700 bold;
  Bookman Old Style declares Bold at 600, which collapsed a "Bold Italic" pick to plain italic;
  the database still adds axes for localized face names.
- **Ctrl+B / Ctrl+I toggle the face axis during a session** (`toggle_text_bold_face` /
  `toggle_text_italic_face`): the real face when `family_offers_face_axis` says the family ships
  one, FAUX bold/italic otherwise (Ctrl+I on Century Gothic, which has no italic, slants). The axis state folds the faux flag in, so a second press always turns the
  axis off, and the toggle clears any recorded exotic style (Ctrl+B on a Black run selects the
  Bold face, as Photoshop does). `family_offers_face_axis` is asked ONE AXIS AT A TIME and
  `real_face_style_name` masks the flags the same way, so bold + italic on Century Gothic renders
  the real Bold face plus a synthetic slant.
- **Never ask `QFontDatabase::styles()` with an unresolved display family.** A face-baked name
  ("ITC Lubalin Graph Demi") lists its faces only under the SPLIT base family; the unsplit name
  answers nothing, which emptied the picker and diverted Ctrl+B/Ctrl+I to faux.
  `available_text_family_styles` resolves through `text_style_query_family`, probes the four flag
  combinations with `QRawFont` (forcing Qt's lazy per-family population), re-queries for faces
  only the database knows (Light, Semibold, Black), orders the four standard faces first, and
  caches per family with `fontDatabaseChanged` invalidation. A family that is not installed still
  offers the four standard faces so the toggles never synthesize on top of a substituted face.
- New text seeds from the picker's selection (`add_text_at`).
- On export, `photoshop_font_name_for_run` resolves the recorded style (or the face split off a
  compound display family) to that face's PostScript name: "Arial" + "Black" writes `Arial-Black`
  instead of flattening onto `Arial-BoldMT`. Style-empty runs keep the weight-based lookup
  byte-identical; a split whose remainder names no real face exports verbatim like any unknown
  family.
- `textStyleCombo` needs the `is_text_option_widget` exemption or focusing it auto-commits.

**Options-bar changes with NO selection apply to the whole type object**, as in Photoshop:
`merge_text_char_format` selects the whole document for a bare caret instead of falling through
to `mergeCurrentCharFormat`, which only formats the NEXT typed character.

## No session: every selected text layer (GitHub issue 31)

- **Family, size, face, smoothing, alignment, color and the Character panel reach every
  selected text layer.** Each options-bar slot reads its widget FIRST, then
  `apply_text_character_edit` runs one hidden session per layer of
  `text_character_target_layer_ids()` (the active text layer, then the panel selection; unlocked
  text layers only) and commits them as ONE "Type" step: the first commit that reaches a push
  site takes the snapshot and `text_commit_snapshot_suppressed_` skips the rest. A layer whose
  edit lambda returns false (faux bold on warped text) is canceled alone and its status error is
  re-shown after the loop. The panel selection is re-selected afterward (every commit rebuilds
  the rows); alignment selects the whole object first, like the direction combo.
- **Re-entrancy.** The hidden sessions rewrite the bar from each layer they open, so
  `applying_text_options_to_layers_` makes every slot return during a pass, and add_text_at's
  size-spin write is signal-blocked like the family combo. The size spin has keyboard tracking
  OFF (Enter, focus loss or a step applies, never each keystroke). The text color panel is
  live, so its layer apply is debounced (`apply_text_color_to_selected_layers_debounced`).
- **The bar mirrors the active text layer** (`sync_text_options_from_active_layer`, from
  `refresh_options_bar` and `refresh_layer_controls`, keyed so an unchanged layer costs nothing;
  the alignment buttons read the first paragraph run). A mixed selection shows the active
  layer's values, the color button keeps the primary color, and a pixel layer leaves the bar
  alone (it seeds the next new layer). Sizes travel through the session's zoomed editor, so a
  size typed at a very low zoom can round; the bar shows what landed.
- Tests: `ui_text_options_bar_size_applies_to_selected_layers_without_session`,
  `ui_text_options_bar_family_and_style_apply_to_selected_layers_without_session`,
  `ui_text_options_bar_follows_active_text_layer`,
  `ui_text_character_panel_edits_all_selected_layers_without_session`.

## Font resolution

Lives in [font-resolution.md](font-resolution.md): how a display family name becomes a Qt font (family, family + face, the Windows registry rescue, the DirectWrite name lookup for full and PostScript names), why only Regular and Bold flatten onto flags, the GDI-name rule the PSD reader follows, and the exact-size rule that keeps re-edits from drifting. Read it before touching `render_text_font_for_display_family`, `available_text_family_style_match`, or `psd_text_read.cpp`'s DirectWrite resolver.

## Character panel

- Opened via options bar > Character... while the Text tool is active; edits leading auto/fixed, tracking, H/V glyph scales, faux bold and faux italic. The leading field is never locked: a typed value turns Auto leading off. Number fields apply on Enter, focus loss or +/-. During inline editing it applies to the text selection, or the whole object with a bare caret. Otherwise it applies to every selected unlocked text layer (see the no-session section above), warped text included, and remains usable after switching tools.
- **Faux bold is refused on warped layers** (Photoshop parity, [warp.md](warp.md)): the checkbox shows a status error and reverts when ENABLING on a session whose layer carries an active Warp Text; unchecking stays allowed so imported faux+warp files can be fixed. Ctrl+B's faux fallback refuses the same way; a family with a real Bold face keeps toggling. Faux italic is unrestricted (PS warps it fine).
- Without an inline session, the panel reads the first character's format from the active layer's stored runs (transform vertical scale and document resolution applied to displayed leading). A change opens a hidden session per target layer through `add_text_at(..., show_editor=false)` and commits through the normal Type undo/render path, without an unwarped preview or keyboard focus; mixed run sizes and colors survive. Opening the panel alone mutates nothing. The scripting `text` setter rides the same session ([scripting.md](scripting.md)).
- Controls disable when neither a live session nor an editable text layer is available (pixel locks, active transform sessions). `refresh_options_bar` and `refresh_layer_controls` synchronize the panel on session, selection, lock and history changes. Tests: `ui_text_character_panel_tracks_session_and_layer`, `ui_text_character_panel_edits_selected_layer_without_session`.
- Double-clicking a text layer's T thumbnail activates the Type tool and opens the layer with all text selected, preserving zoom and pan; the deferred callback re-checks the session because entry may rebuild the rows (`ui_text_thumbnail_double_click_selects_all_without_zoom`).
- `textCharacterDialog` is exempted from the focus-loss auto-commit via `is_text_option_widget`.
- Setting fixed leading opts the layer into the Photoshop layout marker at commit (explicit leading does not render under Qt-natural layout).

## Vertical text and paragraph direction

Vertical type (`patchy.text.orientation = vertical`) and the paragraph base direction
(`patchy.text.paragraph_runs` v4 column 9: `auto`/`ltr`/`rtl`). The layout model and its
Photoshop calibration are in [text-render-calibration.md](text-render-calibration.md); this is
the session contract.

- **One Type tool, an orientation toggle.** `textOrientationButton` (and the layer context
  menu entry) switches a live session in place, converts the selected layer through the
  Character-panel hidden session (one undo step), or arms the NEXT new layer once; never
  persisted, a fresh session starts horizontal. `textDirectionCombo` is per paragraph.
- **The plan is the authority, again.** `vertical_text_layout_plan` (ui/text_layout.hpp)
  re-places every grapheme cluster of the horizontally shaped NoWrap document into a cell;
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
  re-anchoring), box indents, uncalibrated vertical Warp Text, SVG export rasterizes it.

## Document geometry operations follow the text transform

Every operation that remaps document space (Image Size, Canvas Size, Crop to Selection, Rotate
Left/Right, Rotate Arbitrary, layer Flip Horizontal/Vertical, Shift Seams) composes its matrix
onto each text layer's `patchy.text.transform` (`compose_text_layer_transform`,
document_geometry.cpp) BEFORE mutating the layer, so the implicit case can materialize
translate(bounds) from pre-operation bounds. A layer with no stored transform gets one only under
a matrix with a linear part; a pure translation already rides in the bounds. Without it the next
metadata re-render (an edit commit, `--append-text`, a PSD save) used the stale transform.
`ui_*_keeps_text_transform_in_sync` and `ui_layer_flip_keeps_text_mirrored_across_reedit` pin
each operation with a no-change re-edit. `patchy.psd.text.*` stays untouched on purpose: it is
the import snapshot, and diverging from it routes the PSD writer off the templated TySh (and
turns off the PSD-frame edit session via `layer_patchy_text_transform_overrides_psd_source`).

Image Size RESAMPLES, so composing the matrix is not enough: after the resized document is
swapped in, `resize_document_image` (dialog, `doc.resizeImage` and MCP) runs
`rerender_text_layers_through_transforms`, which re-renders every text layer whose transform
carries scale with the free-transform commit's rules (`rerender_text_layer_through_stored_transform`):
Patchy-authored point AND box text fold the vertical scale into the size, per-run sizes,
paragraph metrics and frame dims (`fold_text_transform_scale_into_font_size`) and re-rasterize
through the residual; installed-font PSD point text re-renders crisp through the glyph-aligned
transform; everything else keeps the resampled raster and its raster status. The options bar
derives its displayed size from the transform's vertical scale for ANY layer, so documents saved
in that split state edit at the effective size (`ui_image_size_dialog_*`,
`ui_split_state_text_size_spin_shows_effective_size`).

Negative-determinant (flipped) transforms are ordinary transforms everywhere: the free-transform
commit composes the signed delta, the crisp re-render draws THROUGH the mirrored matrix, and the
drag preview's source blit applies the scale signs like the proxy path
(`ui_point_text_flip_transform_mirrors_and_survives_reedit`: flip, re-edit, flip back).

Committing empty text to an existing unlocked text layer clears its stored text
and raster with an undoable Type edit. Canceling a new empty text session still
removes only its provisional layer.
