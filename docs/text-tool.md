# Text tool and Character panel

The inline text editor's session machinery, commit/cancel semantics, and the Character panel. The Photoshop layout/measurement model (engine units, leading, faux bold/italic, whole-pixel folding, run-format columns) lives in [text-render-calibration.md](text-render-calibration.md); Warp Text lives in [warp.md](warp.md) and offscreen font registration in [testing.md](testing.md).

Do NOT attempt to split the remaining text code out of main_window.cpp as a pure file move: the render pipeline is shared between too many members; it is a "design a module with its own header" job, not a file split. The line-layout half already lives that way in `src/ui/text_layout.{hpp,cpp}` (next section).

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
PS-model layer (`ui_psd_text_caret_follows_photoshop_leading` pins it on the fixed-leading probe:
40 px leading against Qt's ~29). `BoxTextLineRenderItem::block_position` exists only for this:
`QTextLine::lineNumber()` is an index within its own block's layout, so the owning block cannot be
recovered from the line alone. Caret lookup resolves the owning block FIRST, the way
`QTextDocument::findBlock` does, because a block's last line ends before the paragraph separator
and a document-wide scan answers the previous block for a position at the start of the next one.

**The caret layout must be built with the same SCALES as the render pass**, not just the same
document. `build_text_editor_document_space_layout` has to mirror `update_text_editor_preview`
argument for argument: `metric_scale` AND the PSD-frame `layout_scale`
(`text_editor_size_display_scale` when `photoshop_layout && usesPsdTextFrame`). A frame session
keeps its runs in raw engine units and folds the frame transform's vertical scale into the glyph
sizes only at render time, so `layout_scale` left at its 1.0 default lays the caret and selection
out at the raw size while the glyphs are drawn scaled: on a 1.5x frame, selecting one character
highlights one and a half. `ui_psd_frame_text_highlight_matches_scaled_glyphs` pins it by
comparing a select-all highlight against the rendered ink (space-free text), and by clicking the
middle of the INK and requiring the cursor to land mid-text. The click probe must be derived from
the render: clicking the caret proves nothing, because click and caret share a layout and agree
even when that layout is wrong against the glyphs.

Mouse hit-testing goes through the same plan. `QTextEdit::cursorForPosition` must never resolve a
click inside a text session: the widget hit-tests against its own internal layout, built at an
integer pixel size of `round(size * zoom)` with Qt's natural line spacing, so a click and the
caret it produced can answer different lines. `MainWindow::handle_text_editor_viewport_mouse_event`
intercepts left press/drag/double-click on the editor viewport for the flat case and
`TransformedTextEditOverlay::cursor_position_for_overlay_point` covers the transformed one; both
end at `TextLineGeometry::position_at`. Everything else (right-click menu, middle click, release)
falls through to QTextEdit. `ui_psd_text_click_returns_to_the_caret_it_drew` pins the round trip:
click exactly where the caret is drawn and the caret must come back to that position.

The editor widget is SIZED from that layout too, not from `QTextDocument::size()`. The widget's
rect is its hit area, so a widget shorter than the glyphs makes the lines past its bottom edge
unclickable: the click falls through to the canvas and the focus-loss auto-commit ends the
session, routine under the Photoshop leading model.
`ui_transformed_text_click_returns_to_the_caret_it_drew` covers it, rotating the fixed-leading
fixture so the click has to survive both the transform inverse and the leading divergence.

Every type in that header holds handles into the document's `QTextLayout`. They are valid only
while that document is alive and has not been laid out again, and `build_text_render_document`
does its final `setTextWidth` before any plan is built. Keep that order.

## The text on screen is never missing

An edit session must never produce a frame with no glyphs in it. Two rules enforce that:

- **The edited layer stays visible until its replacement is ready.** `add_text_at` does not hide
  it; `hide_text_editor_source_layer` does, from inside `update_text_editor_preview`, once the
  preview pixels are in place, and it returns the vacated region so hide and reveal land in ONE
  `document_changed_effect_bounds` call. Hiding up front blanks the text for the whole
  first-preview delay.
- **The debounce never removes the preview.** An expensive style re-renders on the longer delay,
  but the last good preview keeps drawing until the new one replaces it; tearing it out first
  flashes every keystroke between two rasterizations.

`kTextEditorPreviewPaintProperty` therefore means "the glyphs come from somewhere other than this
widget", true for the whole of any previewed session.
`ui_expensive_text_style_preview_never_blanks_while_typing` pins it by sampling the canvas for
the text's own dark pixels at every point where it could vanish.

Re-editing an existing layer also must not MOVE its text. Every such session renders live through
`render_text_pixels`, including plain unstyled text that needs no preview otherwise
(`kTextEditorForceBakedPreviewProperty`), because the editor widget's own glyph rasterization
differs from the committed layer's. `ui_text_edit_entry_leaves_the_pixels_alone` pins both flows:
enter, do nothing, and the preview must be byte-identical to the committed pixels at the same
origin, as must a re-commit.

**Every session previews, including the one that creates the text.** A new session renders over
its provisional layer, and `restore_active_layer` is that provisional (not whatever was active
before the click) so the preview insert does not steal the layer-panel selection. Without this,
selection highlights drew at widget metrics before the first commit and at render metrics after
it. One consequence for tests: on-screen glyphs are debounced, so a test that changes an option
(alignment, size) and measures pixels has to let the preview land first.

Ending a session must not flash either. `restore_text_editor_source_layer` puts the edited layer
back BEFORE `remove_text_editor_preview` takes the preview away, in commit and in cancel: the
layer still holds its committed pixels there, so the text carries through the handover. Removing
the preview first leaves a frame with neither on screen.

## Session lifecycle (provisional layer, commit, cancel)

- A Type-tool click inserts a provisional 1x1 text layer (marker `patchy.internal.provisional_text`); `commit_text_editor` removes it via the marker-checked `MainWindow::take_provisional_text_layer` (a stale id can never delete an unrelated layer), then snapshots and recreates the committed layer under the same id; cancel/empty-commit leaves history and modified state untouched.
- Mutating actions that take no focus (e.g. layer lock buttons) must call `finish_active_text_editor()` first, or they operate on a half-committed session.

## Delete semantics

Delete on a text layer deletes the OBJECT, never its pixels: pixel-clearing leaves an invisible layer whose metadata resurrects the text (`clear_active_layer` special-cases it; mixed selections clear pixels and delete text layers in one undo step).

## The overlay must accept click focus

`TransformedTextEditOverlay` covers the text it is editing, so it is what a click on transformed
text hits, and it must take `Qt::ClickFocus`. With `Qt::NoFocus` Qt's focus-before-press walk
(`giveFocusAccordingToFocusPolicy`) skips past it to the CanvasWidget behind, and the editor's
focus-loss auto-commit reads that as clicking off: every click on transformed text ends the
session instead of moving the caret. The overlay path is only reached from the SECOND edit of an
imported layer on: committing writes a patchy transform that moves the next session off the
PSD-frame path, where the QTextEdit takes focus itself. Focus landing on the overlay is exempt
from the auto-commit via the `is_text_option_widget` objectName match, and `mousePressEvent`
hands focus straight back to the editor.

Do NOT reach for a focus proxy here. It reads as the tidier answer and is a trap: clearing a proxy
while the proxy holds focus makes Qt reassign the application focus widget, so the editor silently
loses focus and the session commits out from under whoever was mid-call. `configure()` runs on
every cursor move, selection change and preview refresh, so that fires constantly.

Tests that ask "would a real click reach the right widget" must use
`click_widget_like_a_user` (tests/ui/ui_test_support.cpp), which routes the press to the deepest
child under the point and applies the focus policy walk first. `send_mouse` straight to the canvas
answers a different question and hid this bug from several tests that looked like they covered it.

## Options bar while an editor is open

- The options bar shows session apply/cancel buttons (`textApplyButton`/`textCancelButton`) while an editor is open; they must keep `Qt::NoFocus`, or the focus-loss auto-commit fires on mouse press and Cancel commits instead of canceling.
- The font combo is a `FontPickerCombo` (src/ui/font_picker.*, a QFontComboBox whose overridden showPopup opens a searchable list + writing-system preview); its popup objectName `textFontPickerPopup` must stay matched by `is_text_option_widget` (a Qt::Popup is a window, so isAncestorOf-based ownership misses it and focusing the search box would auto-commit the session).
- **A popup pick must apply even when its row is already current.** A family the database lacks cannot be a row, so a control set to one parks on another family while `currentFont()` still names the missing one, and an index-only commit is silent there. The commit sets the index, then pushes the family through `setCurrentFont` when `currentFont().families().value(0)` still disagrees.
- Any new UI that must coexist with an open text session needs the same `is_text_option_widget` exemption from the focus-loss auto-commit.
- The inline editor claims the standard Bold and Italic shortcuts in `ShortcutOverride` before the app-level Ctrl+B Color Balance and Ctrl+I Invert actions can consume them. The key press routes to `toggle_text_bold_face` / `toggle_text_italic_face` (see the style picker section below).

## Boxed-text render clipping

Every boxed render path gates whole LINES against the frame, never raster rows: a line straddling
the frame bottom draws completely (its clip band extends below the frame by the font's descent
bleed) and lines wholly past the frame stay hidden. The editor, Photoshop-layout, and metadata
re-render paths (reopened documents, the Affinity post-open pass) share this rule in
`render_text_pixels_with_local_rect`: skipping the line plan there cuts the straddling line
mid-glyph at the buffer edge, and Affinity and Photoshop both draw it whole. The metadata path
keeps the buffer origin and width at the frame's and grows only the bottom, because pixels-only
callers place the buffer at the frame corner.

## The style picker, and what bold + italic cannot say

A family's faces are an arbitrary list; bold and italic can only name four of them. The options
bar's style picker (`textStyleCombo`) is the ONLY face control, like Photoshop: there are no B/I
buttons. `PsdTextStyleRun::style` / runs v5 column 12 persist a chosen face the flags cannot
express, and `render_text_font_for_display_family` takes it as its last argument, applying
`setStyleName` when the family really offers it and falling back to the flags otherwise (a stray
style must not render nothing).

- **A style is recorded only when the flags cannot express it.** The DirectWrite resolver and the
  picker both drop Regular/Bold/Italic/Bold Italic/Oblique variants
  (`text_style_is_flag_expressible` mirrors the reader's list), so ordinary imports and picks
  stay on runs v1-v4 and the byte-stability canaries do not move. The picker derives those four
  rows from `bold`/`italic` and previews each row in its own face (per-item `Qt::FontRole`).
- **A face name's flags union the database's answer; the database never vetoes the name**
  (`text_style_flags_for_style`): `QFontDatabase::bold` calls only weight >= 700 bold, and
  Bookman Old Style declares Bold at 600, which collapsed a "Bold Italic" pick to plain italic.
  The database still adds axes for localized face names.
- **Ctrl+B / Ctrl+I toggle the face axis during a session** (`toggle_text_bold_face` /
  `toggle_text_italic_face`): the real face when `family_offers_face_axis` says the family ships
  one, FAUX bold/italic otherwise (Ctrl+I on Century Gothic, Regular + Bold and no italic,
  applies the synthetic slant). The axis state folds the faux flag in, so a second press always
  turns the axis off, and the toggle clears any recorded exotic style (Ctrl+B on a Black run
  selects the Bold face, as Photoshop does). `family_offers_face_axis` is asked ONE AXIS AT A TIME
  on purpose, and `real_face_style_name` masks the flags the same way, so bold + italic on Century
  Gothic shows and renders the real Bold face plus a synthetic slant.
- **Never ask `QFontDatabase::styles()` with an unresolved display family.** A face-baked name
  ("ITC Lubalin Graph Demi", the "Bookman Old Style Italic" older imports recorded) lists its
  faces only under the SPLIT base family; the unsplit name answers nothing, which emptied the
  picker and diverted Ctrl+B/Ctrl+I to faux (the Game_Screen.psd regression).
  `available_text_family_styles` resolves through `text_style_query_family`, probes the four
  flag combinations with `QRawFont` (forcing Qt's lazy per-family population;
  `QFontInfo::styleName` can echo the request back), re-queries for faces only the database knows
  (Light, Semibold, Black), orders the four standard faces first like Photoshop, and caches per
  family with `fontDatabaseChanged` invalidation. A family that is not installed at all still
  offers the four standard faces so the toggles do not synthesize on top of a substituted face.
- New text seeds from the picker's selection (`add_text_at`); no pre-armed bold outside a session.
- On export, `photoshop_font_name_for_run` resolves the recorded style (or the face split off a
  compound display family) to that face's PostScript name: "Arial" + "Black" writes `Arial-Black`
  instead of flattening onto `Arial-BoldMT`. Style-empty runs keep the weight-based lookup
  byte-identical, and a split whose remainder names no real face exports verbatim like any unknown
  family.
- `textStyleCombo` needs the `is_text_option_widget` exemption or focusing it auto-commits.

**Options-bar changes with NO selection apply to the whole type object**, as Photoshop does:
`merge_text_char_format` selects the whole document for a bare caret instead of falling through to
`mergeCurrentCharFormat`, which only formats the NEXT typed character.

## Font resolution

- **A family that resolves but covers none of the layer's characters counts as MISSING.** Patchy
  bundles Noto Naskh Arabic (third_party/fonts), so the family is in the database, but its cmap
  holds no Latin letters at all: space, `!`, `,`, `.`, `:` and the digits are the whole ASCII
  coverage, so an installed-only check called it available and the layer rendered entirely in the
  Latin fallback with no warning. `text_family_draws_any_of` probes per writing system with
  `QRawFont::fromFont(font, system)` and requires the face that comes back to BE the requested
  family: asking without the writing system resolves through the default script, so a family that
  cannot draw Latin quietly returns the Latin fallback and reports full coverage. "Any", not
  "all" -- one exotic glyph missing is ordinary per-glyph fallback, not a missing font.
  `missing_text_families_for_layer` (main_window_shared.hpp) is the shared entry point; the layer
  panel draws a warning triangle on the "T" tile and names the font in the thumbnail tooltip.
- `render_text_font_for_display_family` resolves a display name first as a family, then as
  family + style ("Arial Black" -> "Arial"/"Black"). If BOTH fail on Windows,
  `try_register_missing_system_font_family` loads every CurrentVersion\Fonts registry entry whose
  name starts with the requested family as an application font and retries: Qt's Windows database
  can miss registered fonts entirely (Arial Narrow, registered and on disk yet absent from the
  database, fell to Tahoma). Attempted families are cached per run; application fonts are never
  removed (removeApplicationFont can crash live font users).
- On wasm, `available_text_family_match` also resolves common system families through the bundled
  metric-compatible alias table, and every text render appends a Noto Sans JP fallback family. See
  [fonts.md](fonts.md).
- **Only Regular (400) and Bold (700) survive being flattened into a family plus a bold flag.**
  The PSD reader keeps the real face for every other weight (`psd_text_read.cpp`): DirectWrite
  on Windows, the font database elsewhere (`src/ui/psd_font_resolver.hpp`), the suffix
  heuristic last (wasm stays heuristic-only so alias families never reach imported
  metadata). Flattening Demi/Semi (600) onto the family's BOLD face renders heavier
  and taller than Photoshop (ITC Lubalin Graph Demi measured 995x868 against Photoshop's 982x826
  on the entry_poster.psd body copy; the real face matches the width exactly). Gated on
  the face NAME, not the raw weight: a face whose name the flags can already express is never
  baked into the family, whatever weight it declares (Bookman Old Style ships its whole family at
  weight 500, so "BookmanOldStyle-Italic" must resolve to the plain family plus the italic flag).
  Two further rules make the kept faces work:
  - The kept name is `family + " " + faceName`, what Qt calls such a face when it splits it into
    its own family ("ITC Lubalin Graph Demi"). The DirectWrite FULL_NAME can be a PostScript-style
    name ("LubalinGraphITCbyBT-Demi") that matches nothing in the database and falls through to a
    substitute.
  - The bold flag is NOT set alongside it: the name already carries the weight, and Qt would
    synthesise bold on top of the face, the same "heavier and wider" bug by another route.
    Black/Heavy (>= 800) is the deliberate exception, keeping the flag as an uninstalled-face
    fallback; its calibration is pinned by the SNES box-blurb probe.

## Character panel

- Opened via options bar > Character... while the Text tool is active; edits the LIVE session (leading auto/fixed, tracking, H/V glyph scales, faux bold, faux italic) per selection.
- With no live session its controls gray out and a hint label (`textCharacterHint`) says to click in text; the state is kept live by `refresh_options_bar()` calling `sync_text_character_dialog_from_editor()` (every session boundary funnels through that refresh). Without it the non-modal dialog keeps stale enabled controls after a commit and edits silently no-op (`ui_text_character_panel_disables_without_session` pins it).
- `textCharacterDialog` is exempted from the focus-loss auto-commit via `is_text_option_widget`.
- Setting fixed leading opts the layer into the Photoshop layout marker at commit (explicit leading does not render under Qt-natural layout; see the Photoshop text model below).

## Document geometry operations follow the text transform

Every operation that remaps document space -- Image Size, Canvas Size, Crop to Selection,
Rotate Canvas, layer Flip Horizontal/Vertical, and Shift Seams -- composes its matrix onto each
text layer's `patchy.text.transform` (`compose_text_layer_transform`, document_geometry.cpp)
BEFORE mutating the layer, so the implicit case can materialize translate(bounds) from
pre-operation bounds. A layer with no stored transform gets one only under a matrix with a
linear part (scale/rotate/flip); a pure translation already rides in the bounds the operation
moves. Skipping this left the transform stale, and the next metadata re-render (an edit commit,
`--append-text`, a PSD save) put the text back where and how big it was BEFORE the operation:
the August 2026 pinball-poster corruption, where an Image Size to A3 scaled the raster
~3.3x/3.7x per axis while the saved TySh still mapped the text to the old placement, ~900 px
away. The `ui_*_keeps_text_transform_in_sync` and `ui_layer_flip_keeps_text_mirrored_across_reedit`
probes pin each operation with a no-change re-edit. `patchy.psd.text.*` stays untouched on
purpose: it is the import snapshot, and diverging from it is exactly what routes the PSD writer
off the templated TySh (and turns off the PSD-frame edit session via
`layer_patchy_text_transform_overrides_psd_source`).

Negative-determinant (flipped) transforms are ordinary transforms everywhere in the pipeline:
the free-transform commit composes the signed delta, the crisp re-render draws THROUGH the
mirrored matrix, and the drag preview's plain source blit applies the scale signs like the
proxy path (it used to show unmirrored pixels for the whole drag).
`ui_point_text_flip_transform_mirrors_and_survives_reedit` pins flip -> re-edit -> flip back.
