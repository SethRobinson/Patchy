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

**The caret layout must be built with the same SCALES as the render pass**, not just the same
document. `build_text_editor_document_space_layout` has to mirror `update_text_editor_preview`
argument for argument: `metric_scale` AND the PSD-frame `layout_scale`
(`text_editor_size_display_scale` when `photoshop_layout && usesPsdTextFrame`). A frame session
keeps its runs in raw engine units and folds the frame transform's vertical scale into the glyph
sizes only at render time, so leaving `layout_scale` at its 1.0 default laid the caret and
selection out at the raw size while the glyphs were drawn scaled. The highlight was then wrong by
exactly that factor: on a 1.5x frame, selecting one character highlighted one and a half.
Committing rewrites the layer without the frame, which is why re-entering looked correct and made
the bug read as "wrong until you exit and come back".
`ui_psd_frame_text_highlight_matches_scaled_glyphs` pins it by comparing a select-all highlight
against the rendered ink (with space-free text, since a trailing space is legitimately
highlighted and has no ink of its own), and by clicking the middle of the INK and requiring the
cursor to land mid-text. The click probe has to be derived from the render: clicking the caret
proves nothing, because the click and the caret share a layout and agree with each other even
when that layout is wrong against the glyphs. A too-small layout maps a click near the end of
the text past the end of the layout, where it clamps, which is what "the mouse does not select
at all" looks like.

Mouse hit-testing goes through the same plan. `QTextEdit::cursorForPosition` must never resolve a
click inside a text session: the widget hit-tests against its own internal layout, built at an
integer pixel size of `round(size * zoom)` with Qt's natural line spacing, so the click and the
caret it produced could answer different lines. `MainWindow::handle_text_editor_viewport_mouse_event`
intercepts left press/drag/double-click on the editor viewport for the flat case and
`TransformedTextEditOverlay::cursor_position_for_overlay_point` covers the transformed one; both
end at `TextLineGeometry::position_at`. Everything else (right-click menu, middle click, release)
still falls through to QTextEdit. `ui_psd_text_click_returns_to_the_caret_it_drew` pins the round
trip: click exactly where the caret is drawn and the caret must come back to that position.

The editor widget is SIZED from that layout too, not from `QTextDocument::size()`. The widget's
rect is its hit area, so a widget shorter than the glyphs makes the lines past its bottom edge
unclickable: the click falls through to the canvas and the focus-loss auto-commit ends the
session. Under the Photoshop leading model that is routine (40 px leading against Qt's natural
29 left the widget a third too short on the fixed-leading probe).
`ui_transformed_text_click_returns_to_the_caret_it_drew` covers it, rotating the fixed-leading
fixture so the click has to survive both the transform inverse and the leading divergence.

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

**Every session previews, including the one that creates the text.** A new session renders over
its provisional layer, and `restore_active_layer` is that provisional (not whatever was active
before the click) so the preview insert does not steal the layer-panel selection. Without this
the creating session was the odd one out: its glyphs, caret and highlight all came from the
editor widget at the widget's own zoom-scaled metrics, so selecting text drew a different-sized
highlight before the first commit than after it. One consequence for tests: on-screen glyphs are
now debounced, so a test that changes an option (alignment, size) and measures pixels has to let
the preview land first.

Ending a session must not flash either. `restore_text_editor_source_layer` puts the edited layer
back BEFORE `remove_text_editor_preview` takes the preview away, in commit and in cancel: the
layer still holds its committed pixels there, so the text carries straight through the handover.
Removing the preview first left a window with neither on screen, and the widget teardown and
options-bar relayout that follow are enough to get a paint delivered inside it.

## Session lifecycle (provisional layer, commit, cancel)

- A Type-tool click inserts a provisional 1x1 text layer immediately (marker `patchy.internal.provisional_text`); `commit_text_editor` removes it first via the marker-checked `MainWindow::take_provisional_text_layer` (a stale id can never delete an unrelated layer), then snapshots and recreates the committed layer under the same id; cancel/empty-commit leaves history and modified state untouched.
- Mutating actions that take no focus (e.g. the layer lock buttons) must call `finish_active_text_editor()` first, or they operate on a half-committed session.

## Delete semantics

Delete on a text layer deletes the OBJECT, never its pixels: pixel-clearing leaves an invisible layer whose metadata resurrects the text (`clear_active_layer` special-cases it; mixed selections clear pixels + delete text layers in one undo step).

## The overlay must accept click focus

`TransformedTextEditOverlay` covers the text it is editing, so it is what a click on transformed
text actually hits, and it must take `Qt::ClickFocus`. With `Qt::NoFocus` Qt's focus-before-press
walk (`giveFocusAccordingToFocusPolicy`) skipped straight past it to the CanvasWidget behind, and
the editor's focus-loss auto-commit read that as clicking off: every click on transformed text
ended the session instead of moving the caret, so the mouse could not select at all. It only
showed up on the SECOND edit of an imported layer, because committing writes a patchy transform
that moves the next session off the PSD-frame path (where clicks land on the QTextEdit, which
takes focus itself) and onto the overlay. Focus landing on the overlay is already exempt from the
auto-commit, via the `is_text_option_widget` objectName match, and `mousePressEvent` hands focus
straight back to the editor.

Do NOT reach for a focus proxy here. It reads as the tidier answer and it is a trap: clearing a
proxy while the proxy holds focus makes Qt reassign the application focus widget, so the editor
silently loses focus and the session commits out from under whoever was mid-call. `configure()`
runs on every cursor move, selection change and preview refresh, so that fires constantly.

Tests that ask "would a real click reach the right widget" must use
`click_widget_like_a_user` (tests/ui/ui_test_support.cpp), which routes the press to the deepest
child under the point and applies the focus policy walk first. `send_mouse` straight to the canvas
answers a different question and hid this bug from several tests that looked like they covered it.

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

## The style picker, and what bold + italic cannot say

A family's faces are an arbitrary list; bold and italic can only name four of them. The options
bar carries a style picker (`textStyleCombo`) populated from `QFontDatabase::styles(family)`, and
`PsdTextStyleRun::style` / runs v5 column 12 persist the chosen face. `render_text_font_for_display_family`
takes it as its last argument and applies `setStyleName` when the family really offers it,
falling back to the flags otherwise (a style carried over from another family, or from a machine
where that font was installed, must not render nothing).

- **A style is recorded only when the flags cannot express it.** The DirectWrite resolver drops
  Regular/Bold/Italic/Bold Italic/Oblique, so ordinary imports stay on v3/v4 and the byte-stability
  canaries do not move. The picker derives those four from `bold`/`italic` instead.
- The Bold/Italic buttons stay as the shortcut for the four they can name and track the picker in
  both directions (`sync_text_style_combo_from_flags`, `apply_text_style_to_active_editor`).
- `textStyleCombo` needs the `is_text_option_widget` exemption like every other option widget, or
  focusing it auto-commits the session.

**Options-bar changes with NO selection apply to the whole type object**, the way Photoshop does.
`merge_text_char_format` used to fall through to `QTextEdit::mergeCurrentCharFormat`, which only
sets the format the NEXT typed character gets, so clicking Bold, Italic, a family, a size or a
colour with a bare caret left every existing run untouched and they won at render time (the
reported "clicking italics does nothing" on the Dungeon Scroll buttons).

## Font resolution

- **A family that resolves but covers none of the layer's characters counts as MISSING.** Patchy
  bundles Noto Naskh Arabic (third_party/fonts), so the family is in the database, but its cmap
  holds no Latin letters at all: space, `!`, `,`, `.`, `:` and the digits are the whole ASCII
  coverage. A Latin type layer set in it therefore reported "font available", skipped the
  substitution warning, and rendered entirely in the Latin fallback. `text_family_draws_any_of`
  probes per writing system with `QRawFont::fromFont(font, system)` and requires the face that
  comes back to BE the requested family: asking without the writing system resolves through the
  default script, so a family that cannot draw Latin quietly returns the Latin fallback and
  reports full coverage. "Any", not "all" -- one exotic glyph missing is ordinary per-glyph
  fallback, not a missing font. `missing_text_families_for_layer` (declared in
  main_window_shared.hpp) is the shared entry point; the layer panel draws a warning triangle on
  the "T" tile and names the font in the thumbnail tooltip.
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
- **Only Regular (400) and Bold (700) survive being flattened into a family plus
  a bold flag.** The PSD reader's DirectWrite resolver keeps the real face for
  every other weight (`psd_text_read.cpp`), because flattening loses it: Demi /
  Semi (600) used to resolve to the family's BOLD face, which renders heavier
  and taller than Photoshop did (ITC Lubalin Graph Demi measured 995x868
  against Photoshop's own 982x826 on the entry_poster.psd body copy; with the
  real face the width matches exactly). Two rules make that work:
  - The kept name is `family + " " + faceName`, which is what Qt calls such a
    face when it splits it into its own family ("ITC Lubalin Graph Demi"). The
    DirectWrite FULL_NAME can be a PostScript-style name
    ("LubalinGraphITCbyBT-Demi") that matches nothing in the font database and
    falls through to a substitute.
  - The bold flag is NOT set alongside it. The name already carries the weight,
    and Qt would synthesise bold on top of the face, which is the same "heavier
    and wider" bug by another route. Black/Heavy (>= 800) is the deliberate
    exception: it keeps the flag as an uninstalled-face fallback, and its
    calibration is pinned by the SNES box-blurb probe.

## Character panel

- Opened via options bar > Character... while the Text tool is active. It edits the LIVE editor session (leading auto/fixed, tracking, H/V glyph scales, faux bold) per selection.
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

Run format "patchy.text.runs" v3 adds double sizes, a leading column (number or `auto`), tracking, and H/V glyph scales; v4 appends the faux-bold flag; paragraph v3 appends the auto-leading fraction. Every column is read by INDEX, so the version token only rises when a run actually needs the new column and existing files stay byte-identical. Patchy-authored text keeps v1/v2 and Qt-natural layout (the PS model is opt-in per layer, so Patchy PSDs reopen unchanged). Export writes `/AutoLeading false` for fixed leading (PS ignores it otherwise), non-zero `/Tracking`, non-1 `/HorizontalScale`/`/VerticalScale`.

## Faux bold is not the bold face

`/FauxBold` asks Photoshop to synthesize weight on the face the run already names. It is NOT
"use the family's bold face", and folding it into the run's bold flag (which is what the reader
used to do) swaps in a different typeface: on the Dungeon Scroll `Game_Screen.psd` headings,
Georgia-Italic + faux bold measures 58px wide in Photoshop's own raster while Georgia **Bold**
Italic renders 63. Photoshop's Character panel shows the same split, which is why clicking into
such a layer used to come up Bold + Italic when Photoshop shows only Italic.

- `PsdTextStyleRun::faux_bold` carries it, `bold`/`italic` keep meaning the real face, and runs
  v4 serializes it in column 11. The Character panel's `textCharacterFauxBold` checkbox edits it
  live per selection.
- Rendering: `apply_faux_bold_to_document` strokes the glyph outlines with a pen
  `kFauxBoldEmFraction` (0.03) of the em wide and adds the same amount to every advance, which is
  what Photoshop does (its faux bold pushes the glyph out on both sides and pays for it in the
  advance). Calibrated on Georgia-Italic at 12px against Photoshop's rasters: "Dungeon:" 58px and
  "Fights Left:" 69px both land exactly anywhere in 0.025-0.030.
- It is applied in `build_text_render_document`, not when the runs are parsed, so it follows
  later colour and size edits and so the raster pass and the caret layout (which share that
  function) see identical advances. Apply it BEFORE the final `setTextWidth`: the widened
  advances have to be in place while the lines are laid out.
- Export writes `/FauxBold` from `faux_bold` alone. The run's real weight already rides in the
  font name `font_index_for_run` resolves (`Arial-BoldMT`, not Arial + FauxBold); writing both
  made Photoshop embolden an already-bold face.
- `/FauxItalic` is still conflated with `run.italic`, so a real italic face round-trips with
  `/FauxItalic true`. Known gap, same class of bug. It needs a real shear to render: measured on
  Arial, `QFont::setStyle(QFont::StyleOblique)` resolves to the family's REAL Italic face
  (`QFontInfo::styleName()` comes back "Italic", identical ink), so Qt cannot express "slant the
  regular face" through QFont at all.

## Glyph sizes fold only to whole pixels

Qt rasterizes glyphs at whole pixel sizes only: `QFont::setPixelSize` takes an int, and a
fractional `setPointSizeF` quantizes to the same whole pixel (measured -- 16.2px and 16px report
an identical advance). So `render_text_pixels_with_local_rect` folds a transform's vertical scale
into the glyph sizes only as far as the nearest whole pixel and leaves the remainder in
`document_transform`, which the rasterizer applies exactly because these lines are drawn THROUGH
the matrix rather than resampled after the fact. `dominant_text_run_size` picks the size that
gets to land exactly (the largest run, vertical glyph scale included).

Folding the whole scale silently rounded the text off Photoshop's size, and only for some layers:
in the Dungeon Scroll repro, 18 x 0.9 = 16.2 became 16 (~1.2% narrow -- "Jumble"/"Submit word"
lost 1-2px and shifted on edit) while 14.44444 x 0.9 = 13.0 was already whole and never moved
("Quit"/"Pause"). It also brought the render into agreement with the caret, which lays out at the
raw size and applies the full transform through the overlay.

`ui_dungeon_scroll_psd_text_commit_keeps_placement_if_available` pins both fixes against
Photoshop's rasters. What is left is a pixel of grid phase: Photoshop's anchors sit at fractional
document positions (tx 267.35, ty 305.4) and both rasters land on the whole-pixel grid, so an
edited layer can still settle 1px off in x or y.

- **Text renders UNHINTED**: PS never runs TrueType hinting; every antialiased `/AntiAlias` mode maps to `QFont::PreferNoHinting` (`configure_text_font_smoothing`); mode 0/None keeps `NoAntialias` + full hinting. Full hinting fattens stems on small-print-era fonts and shifts advances into collisions.
- **Imported type layers keep Photoshop's raster until edited** (`should_regenerate_imported_text_preview`, psd_text_write.cpp): a missing font never changes appearance on open. Rasters are kept even under big effects; regenerate only when the stored preview is visibly NOT any run's declared fill color (baked-in effect pixels would corrupt the live outer-effect contour) or when the type block is Patchy-authored. Editing a kept raster warns before substituting fonts; `--append-text` substitutes silently.
- **Black/Heavy faces (DirectWrite weight >= 800)** resolve to their FULL face name so the family+style matcher finds the real face (family+bold renders Bold, ~15% narrower); the bold flag stays set for fallback. Never feed such a name raw to the font combo: `QFont("Arial Black")` resolves to Tahoma; use `text_font_combo_font_for_family`.
- **Rotated point-text anchoring**: committed placement pins the TEXT-SPACE anchor (justification fraction along the reading axis, first-line side on the stack axis), never a fixed document corner; the CS-era document-bounds fallback pins the fractionally corresponding point of the source ink box.
- **Scaled BOX text**: runs and box dims (`patchy.text.box_width/height`, from `/BoxBounds`) are engine units, but a PSD-frame edit session works in DOCUMENT space; the render call's `layout_scale` folds the transform's vertical scale into glyph sizes WITHOUT scaling box dims, and commit stores frame dims divided back to raw units so runs + box + transform stay one coordinate system.
- Committing a transformed point-text layer re-renders CRISP through the aligned transform even when the font is substituted (resampling would deliver the same glyphs blurry). The first re-edit after conversion settles placement by a few pixels; later cycles are identical.
- Known gaps: LeadingType 1 (Japanese top-to-top), per-run BaselineShift, VerticalScale x auto leading under a folded transform; box-text RE-edits resample through the transform (crisp path is point-text only).
