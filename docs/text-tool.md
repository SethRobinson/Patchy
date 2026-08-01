# Text tool and Character panel

The inline text editor's session machinery, commit/cancel semantics, and the Character panel. The Photoshop text layout model is the calibration section at the end of this document, Warp Text lives in [warp.md](warp.md), and offscreen font registration rules live in [testing.md](testing.md).

Do NOT attempt to split the remaining text code out of main_window.cpp as a pure file move: the text render pipeline is shared between too many members; it is really a "design a module with its own header" job, not a file split (tried and backed out). The line-layout half has been done that way and lives in `src/ui/text_layout.{hpp,cpp}`; see the next section.

## One layout authority (src/ui/text_layout.hpp)

The renderer draws a text layer by walking a LINE PLAN (one `QTextLine` per visual line with the
origin it is drawn at) rather than letting QTextDocument place the lines, because Photoshop's
leading model moves baselines off Qt's natural spacing. `text_layout.hpp` owns that plan and is
the single geometric authority: `photoshop_text_layout_plan` / `boxed_text_render_plan` produce
it, and `TextLineGeometry` reads the same plan back for caret rects, selection rects and
hit-testing. Build it with the same `boxed` / `photoshop_layout` flags the render pass got and
its answers are guaranteed to agree with the drawn glyphs.

Caret and selection geometry MUST go through `TextLineGeometry`, never through Qt's natural
`blockBoundingRect` origins. Reading those was the old bug: the caret layout also never passed
`photoshop_layout`, so on a PS-model layer the caret drifted roughly (leading - Qt line spacing)
px per line off the text (`ui_psd_text_caret_follows_photoshop_leading` pins it on the fixed-
leading probe: 40 px leading against Qt's ~29). `BoxTextLineRenderItem::block_position` exists
only for this: `QTextLine::lineNumber()` is an index within its own block's layout, so the
owning block cannot be recovered from the line alone. Caret lookup resolves the owning block
FIRST, the way `QTextDocument::findBlock` does, because a block's last line ends before the
paragraph separator and a document-wide scan answers the previous block for a position at the
start of the next one.

Mouse hit-testing goes through the same plan. `QTextEdit::cursorForPosition` must never resolve a
click inside a text session: the widget hit-tests against its own internal layout, built at an
integer pixel size of `round(size * zoom)` with Qt's natural line spacing, so the click and the
caret it produced could answer different lines. `MainWindow::handle_text_editor_viewport_mouse_event`
intercepts left press/drag/double-click on the editor viewport for the flat case and
`TransformedTextEditOverlay::cursor_position_for_overlay_point` covers the transformed one; both
end at `TextLineGeometry::position_at`. Everything else (right-click menu, middle click, release)
still falls through to QTextEdit. `ui_psd_text_click_returns_to_the_caret_it_drew` pins the round
trip: click exactly where the caret is drawn and the caret must come back to that position.

Every type in that header holds handles into the document's `QTextLayout`. They are valid only
while that document is alive and has not been laid out again, and `build_text_render_document`
does its final `setTextWidth` before any plan is built. Keep that order.

## The text on screen is never missing

An edit session must never produce a frame with no glyphs in it. Two rules enforce that:

- **The edited layer stays visible until its replacement is ready.** `add_text_at` does not hide
  it; `hide_text_editor_source_layer` does, from inside `update_text_editor_preview`, at the
  moment the preview pixels are in place, and it returns the vacated region so the hide and the
  reveal land in ONE `document_changed_effect_bounds` call. Hiding up front blanked the text for
  as long as the first preview took, which on a styled layer is the whole
  `kExpensiveTextEditorPreviewDelayMs`.
- **The debounce never removes the preview.** An expensive style re-renders on the longer delay,
  but the last good preview keeps drawing until the new one replaces it. `schedule_text_editor_preview`
  used to tear the preview layer out first and let the editor widget paint raw glyphs meanwhile,
  so every keystroke flashed between two different rasterizations.

`kTextEditorPreviewPaintProperty` therefore means "the glyphs come from somewhere other than this
widget", and it is true for the whole of any previewed session. `ui_expensive_text_style_preview_never_blanks_while_typing`
pins it by sampling the canvas for the text's own dark pixels at every point where it used to
vanish.

Re-editing an existing layer also must not MOVE its text. Every such session renders live through
`render_text_pixels`, including plain unstyled text that needs no preview otherwise
(`kTextEditorForceBakedPreviewProperty`), because the editor widget's own glyph rasterization
differs from the committed layer's. Box text was excluded from that rule and so still shifted on
entry until August 2026. `ui_text_edit_entry_leaves_the_pixels_alone` pins both flows: enter, do
nothing, and the preview must be byte-identical to the committed pixels at the same origin, as
must a re-commit.

## Session lifecycle (provisional layer, commit, cancel)

- A Type-tool click inserts a provisional 1x1 text layer immediately (marker `patchy.internal.provisional_text`); `commit_text_editor` removes it first via the marker-checked `MainWindow::take_provisional_text_layer` (a stale id can never delete an unrelated layer), then snapshots and recreates the committed layer under the same id; cancel/empty-commit leaves history and modified state untouched.
- Mutating actions that take no focus (e.g. the layer lock buttons) must call `finish_active_text_editor()` first, or they operate on a half-committed session.

## Delete semantics

Delete on a text layer deletes the OBJECT, never its pixels: pixel-clearing leaves an invisible layer whose metadata resurrects the text (`clear_active_layer` special-cases it; mixed selections clear pixels + delete text layers in one undo step).

## Options bar while an editor is open

- The options bar shows session apply/cancel buttons (`textApplyButton`/`textCancelButton`) while an editor is open; they must keep `Qt::NoFocus`, otherwise the editor's focus-loss auto-commit fires on mouse press and Cancel commits instead of canceling.
- The font combo is a `FontPickerCombo` (src/ui/font_picker.*, a QFontComboBox whose overridden showPopup opens a searchable list + writing-system preview); its popup objectName `textFontPickerPopup` must stay matched by `is_text_option_widget` (a Qt::Popup is a window, so isAncestorOf-based ownership misses it and focusing the search box would auto-commit the session).
- Any new UI that must coexist with an open text session needs the same `is_text_option_widget` exemption from the focus-loss auto-commit.
- The inline editor claims the standard Bold and Italic shortcuts in `ShortcutOverride` before the app-level Ctrl+B Color Balance and Ctrl+I Invert actions can consume them. The key press toggles the same options-bar buttons so selection and typing-format behavior stay on one path.

## Boxed-text render clipping

Every boxed render path gates whole LINES against the frame, never raster rows: a line
straddling the frame bottom draws completely (its clip band extends below the frame by the
font's descent bleed) and lines wholly past the frame stay hidden. The editor,
Photoshop-layout, and metadata re-render paths (reopened documents, the Affinity post-open
pass) all share this rule in `render_text_pixels_with_local_rect`; the metadata path
historically skipped the line plan and cut the straddling line mid-glyph at the buffer edge
(the tips.af regression, July 2026; Affinity and Photoshop both draw the straddler whole).
The metadata path keeps the buffer origin and width at the frame's and grows only the
bottom, because pixels-only callers place the buffer at the frame corner.

## Font resolution

- `render_text_font_for_display_family` resolves a display name first as a
  family, then as family + style ("Arial Black" -> "Arial"/"Black"). If BOTH
  fail on Windows, `try_register_missing_system_font_family` loads every
  CurrentVersion\Fonts registry entry whose name starts with the requested
  family as an application font and retries: Qt's Windows database can miss
  registered fonts entirely (this machine's Arial Narrow was in the registry
  and on disk yet absent from the family list AND Arial's style list, so
  imported text silently fell to Tahoma). Attempted families are cached per
  run; application fonts are never removed (removeApplicationFont can crash
  live font users).
- On wasm, `available_text_family_match` additionally resolves common system
  families through the bundled metric-compatible alias table, and every text
  render appends a Noto Sans JP fallback family. See [fonts.md](fonts.md).

## Character panel

- Opened via options bar > Character... while the Text tool is active. It edits the LIVE editor session (leading auto/fixed, tracking, H/V glyph scales) per selection.
- With no live session its controls gray out and a hint label (`textCharacterHint`) says to click in text; the state is kept live by `refresh_options_bar()` calling `sync_text_character_dialog_from_editor()` (every session boundary funnels through that refresh). Without that call the non-modal dialog kept stale enabled controls after a commit and edits silently no-oped (`ui_text_character_panel_disables_without_session` pins it).
- Its dialog (`textCharacterDialog`) is exempted from the editor's focus-loss auto-commit via `is_text_option_widget`.
- Setting fixed leading opts the layer into the Photoshop layout marker at commit (explicit leading does not render under Qt-natural layout; see the Photoshop text model section below).

## Photoshop text model (type layers)

Probe PSDs `photoshop-text-*.psd`. The rules apply when `kLayerMetadataTextLayoutMode == "photoshop"` (set on import of non-Patchy TySh):

- **Engine units are document pixels.** 24 pt UI at 300 dpi stores `/FontSize 100` with an identity transform; the transform does NOT carry DPI. UI pt = engine size x transform y-scale x 72/dpi.
- **The TySh transform maps text space to document pixels.** Vertical scale (`hypot(yx, yy)`) multiplies sizes and leading; the x/y ratio is a pure horizontal glyph stretch (free transform folds into the matrix, so xx != yy is common). Never average the axes.
- **Style runs omit properties equal to the ResourceDict normal style sheet**; a run without `/FontSize` uses the sheet's default (usually 12.0), never a sibling run's value.
- **Leading is per-character; a line's baseline advance = the max effective leading among the ENTERED line's characters.** Fixed leading applies only with `/AutoLeading false`; otherwise the recorded `/Leading` is stale and the effective value is the paragraph auto-leading fraction (default 1.2) x FontSize, sub-pixel exact. Leading may be smaller than the em.
- **Point text anchors the FIRST baseline at the transform translation (tx, ty)**; justification decides whether tx is line start, middle, or end. No leading on the first line.
- **Box text puts the first baseline at box top + OS/2 sTypoAscender x size** (largest run on line 1; capHeight and hhea/winAscent are wrong). Read via QRawFont (`typographic_ascent_fraction`). Leading does not move the first baseline.
- **Tracking = FontSize x tracking/1000 px per inter-glyph gap** (not after the last glyph), as absolute letter spacing.
- **VerticalScale/HorizontalScale scale glyphs only**; auto leading stays 1.2 x FontSize, unscaled.

Run format "patchy.text.runs" v3 adds double sizes, a leading column (number or `auto`), tracking, and H/V glyph scales; paragraph v3 appends the auto-leading fraction. Patchy-authored text keeps v1/v2 and Qt-natural layout (the PS model is opt-in per layer, so Patchy PSDs reopen unchanged). Export writes `/AutoLeading false` for fixed leading (PS ignores it otherwise), non-zero `/Tracking`, non-1 `/HorizontalScale`/`/VerticalScale`.

- **Text renders UNHINTED**: PS never runs TrueType hinting; every antialiased `/AntiAlias` mode maps to `QFont::PreferNoHinting` (`configure_text_font_smoothing`); mode 0/None keeps `NoAntialias` + full hinting. Full hinting fattens stems on small-print-era fonts and shifts advances into collisions.
- **Imported type layers keep Photoshop's raster until edited** (`should_regenerate_imported_text_preview`, psd_text_write.cpp): a missing font never changes appearance on open. Rasters are kept even under big effects; regenerate only when the stored preview is visibly NOT any run's declared fill color (baked-in effect pixels would corrupt the live outer-effect contour) or when the type block is Patchy-authored. Editing a kept raster warns before substituting fonts; `--append-text` substitutes silently.
- **Black/Heavy faces (DirectWrite weight >= 800)** resolve to their FULL face name so the family+style matcher finds the real face (family+bold renders Bold, ~15% narrower); the bold flag stays set for fallback. Never feed such a name raw to the font combo: `QFont("Arial Black")` resolves to Tahoma; use `text_font_combo_font_for_family`.
- **Rotated point-text anchoring**: committed placement pins the TEXT-SPACE anchor (justification fraction along the reading axis, first-line side on the stack axis), never a fixed document corner; the CS-era document-bounds fallback pins the fractionally corresponding point of the source ink box.
- **Scaled BOX text**: runs and box dims (`patchy.text.box_width/height`, from `/BoxBounds`) are engine units, but a PSD-frame edit session works in DOCUMENT space; the render call's `layout_scale` folds the transform's vertical scale into glyph sizes WITHOUT scaling box dims, and commit stores frame dims divided back to raw units so runs + box + transform stay one coordinate system.
- Committing a transformed point-text layer re-renders CRISP through the aligned transform even when the font is substituted (resampling would deliver the same glyphs blurry). The first re-edit after conversion settles placement by a few pixels; later cycles are identical.
- Known gaps: LeadingType 1 (Japanese top-to-top), per-run BaselineShift, VerticalScale x auto leading under a folded transform; box-text RE-edits resample through the transform (crisp path is point-text only).
