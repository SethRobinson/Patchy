# Repository Instructions

Keep this file current: when a change makes anything in here stale (build steps, test conventions, gotchas), update it in the same change. Multiple different coding agents read this file — it is the shared knowledge channel, so write notes for any AI, not a specific one.

When bumping the release version, update all three version fields: `CMakeLists.txt` (`project(... VERSION x.y)`), `vcpkg.json` (`version-semver`), and `latest_version.json` (the windows `version` — this is the update-check manifest, served to the app from raw.githubusercontent.com on main, so it only takes effect once pushed). Also add a new top entry under `README.md`'s "What's New" section for that version, dated with the release date, summarizing the user-visible changes.

When adding or changing user-facing English text, make sure it is wired through Patchy's localization system and update the Japanese translation in `translations/patchy_ja.ts` in the same change.

Keyboard shortcuts for QActions must be registered through `MainWindow::register_hotkey(action, "stable.id", default_seq)` (backed by `HotkeyRegistry` in `src/ui/hotkey_registry.hpp`) — never call `setShortcut`/`setShortcuts` directly on an app-level action. The registry applies user customizations from the `hotkeys/` settings group (only deltas are stored; unmodified commands track new defaults automatically) and feeds the Preferences > Hotkeys editor. Command ids are persisted in user settings, so never rename one. Two commands must not share a default shortcut — Qt silently deactivates ambiguous application shortcuts; the `ui_hotkey_defaults_have_no_conflicts` visual test enforces this.

If tests need files from outside the project directory, copy those files into `local-test-fixtures` first and have the tests read them from there. Do not add hardcoded external drive paths such as `C:\temp` or `D:\projects` to test code.

Keep git commit messages to one or two lines — a concise subject, no multi-paragraph body enumerating every change.

## Single-instance: command-line files reuse the running window

Patchy is single-instance (`src/app/main.cpp`). On launch it resolves the positional file args to
absolute paths, then tries to connect to a per-user `QLocalServer` named
`Patchy-SingleInstance-<USERNAME>`. If a Patchy is already running it serializes the file list over
the socket (a `QStringList` via `QDataStream`, version pinned to `Qt_5_15`) and exits `0`; the running
instance reads the payload on the connection's `disconnected` signal, then `MainWindow::activate_for_second_instance`
un-minimizes/raises/activates the window and opens the files. So double-clicking a file in Explorer
opens it in the existing window and brings it to the foreground instead of spawning a new process.

The first launch becomes the server (`removeServer` first clears a pipe left by a prior crash). This
needs Qt6::Network, already linked via `patchy_ui`. Set `PATCHY_NO_SINGLE_INSTANCE` to allow multiple
instances (tests and other launches are unaffected since they construct `MainWindow` directly, not via
`main()`).

## Document-level alpha imports as an editable layer mask

A flat image's per-pixel alpha (a 32-bit BMP — including `BI_RGB`/compression 0, whose 4th
byte Patchy now keeps — or a PNG/TIFF alpha, or a flat PSD's extra channel) is imported as
an editable grayscale **layer mask** rather than as pixel transparency, matching how
Photoshop shows the alpha of an opaque flattened image as a saved "Alpha 1" channel. The
shared step is `patchy::ui::promote_flat_alpha_to_layer_mask` (called once in
`load_document_from_path`); it only fires for a single flat pixel layer and skips uniform
alpha (all-255 = nothing to mask, all-0 = treated as opaque, never fully masked).

- Such masks are tagged with layer metadata `kLayerMetadataDocumentAlpha`
  (`layer_mask_is_document_alpha`). This marker is what distinguishes an imported
  document-alpha channel from a hand-authored layer mask — only marked masks are written
  back as the file's alpha. Hand-authored layer masks still save as PSD layer masks. The
  marker is re-derived on every load (it is not persisted in the file).
- On save, a marked single-layer doc round-trips the mask as the file's alpha
  **non-destructively** (the colors under the mask are preserved, not erased): BMP 32-bit,
  PNG/TIFF/WebP via Qt, and PSD as a document-level "Alpha 1" channel (header channel count
  4 + image resource 1006 naming it "Alpha 1"; the per-layer mask is suppressed in the
  layered writer so Photoshop shows an opaque layer + the named channel, not both). The
  shared "keep RGB, mask becomes straight alpha" buffer is `document_alpha_rgba8`
  (`core/layer_render_utils`); PSD has a parallel `document_alpha_composite`. Compositing
  (`render_rgba8` / `qimage_from_document`) is the destructive path and is only used for
  unmarked docs.
- Round-trip coverage: `ui_flat_alpha_round_trips_as_editable_mask` in `ui_visual_tests.cpp`.

## Merge Down flattens folders and any multi-selection

`MainWindow::merge_down()` ("Merge Down" / Ctrl+E) flattens a selection into one pixel layer,
mirroring Photoshop. A single **folder** flattens in place ("Merge Group"); a single non-folder
merges with the layer directly below it; two or more selected items (folders and/or layers, any
folder, contiguous or not) merge together. The merged pixels land in the **bottom-most visible**
item, which keeps its id and name; a non-pixel target (folder/adjustment/text) is replaced by a
fresh pixel layer. So selecting everything (including the folders) collapses to a single flat
layer with no folders left.

- **Hidden layers are discarded, not blocking.** A selected layer with visibility off contributes
  nothing and is deleted by the merge (Photoshop's "delete hidden layers" behavior) — it must never
  abort the operation. Likewise a folder flattens only its *visible* children.
- Folders/adjustment layers are added to the scratch merge `Document` as-is and the compositor
  flattens/applies them; group opacity/blend/mask follow whatever the canvas shows (currently
  pass-through). Anything the compositor can't draw is skipped the same way as a hidden layer.
- A cross-folder merge of **leaf** layers (folders themselves not selected) can leave an emptied
  folder behind — that is intended, not a bug.
- Coverage: the `ui_merge_down_*` tests in `ui_visual_tests.cpp`.

## Delete key on a text layer deletes the object, never its pixels

`MainWindow::clear_active_layer()` ("layer.clear", default shortcut Delete, also Layer > Clear
Layer / Selection) special-cases text layers (pixel layers carrying text metadata,
`layer_is_text`), matching Photoshop. Pixel-clearing a text layer is forbidden because it leaves
an invisible layer whose metadata resurrects the "erased" text on the next text-tool click.
Instead: with no marquee selection and no inline text edit in progress, targeted text layers are
deleted outright (a pure-text selection delegates to `delete_layers`, undo label "Delete layer");
with a selection active they are skipped with a status-bar hint; while the `inlineTextEditor` is
alive they are left completely alone (Delete belongs to typing). A mixed selection clears the
pixel layers and deletes the text layers in one undo step. Coverage:
`ui_delete_key_action_removes_text_layer_object` in `ui_visual_tests.cpp`.

## Bitmap brush tips (Photoshop-style) and .abr import

The Brush and Eraser tools can stamp bitmap **brush tips** in addition to the procedural
round/soft brush. Key pieces:

- Core stamping lives in `src/core/brush_tip.hpp/.cpp` (`BrushTip` = 8-bit grayscale coverage
  mask + default spacing; `BrushTipMipChain` box-filtered halvings built once per tip;
  `ScaledBrushTip` resampled per brush size) and `src/core/pixel_tools.cpp` (`paint_tip_dab`,
  `paint_tip_segment`). `EditOptions::brush_tip` (non-owning `const ScaledBrushTip*`, null =
  procedural path) plus `brush_tip_spacing` select the stamp path. Rotation/roundness/subpixel
  are applied per-dab in the inverse map, so the scaled cache depends only on size (pressure
  size is quantized to 2px steps while a tip is active; CanvasWidget keeps an 8-entry LRU of
  scaled stamps). The stateful `paint_brush_segment(..., BrushTipStrokeState&)` overload carries
  dab spacing across the short segments the stroke smoother emits — the state resets in
  `CanvasWidget::clear_brush_stroke_tracking()`.
- `.abr` parsing is `src/psd/abr_reader.*` (`read_abr`): supports v1/v2 and v6/v7/v10
  (subversion 1 = 47-byte, 2 = 301-byte samp entry key skip), pairs `samp` bitmaps with the
  `desc` ActionDescriptor brush list **in order** for names/spacing, decodes per-row PackBits
  RLE, downconverts 16-bit, crops to content, caps dimensions at 4096, and skips bad entries
  with warnings instead of failing the file. v6 8BIM tagged blocks are padded to 4-byte
  boundaries with the padding excluded from the length field — the block walk must skip the
  padding or the next signature read lands mid-pad (latent until July 2026; only surfaces on
  files whose desc length is not already 4-aligned). The descriptor parser + `decode_packbits`
  were extracted from psd_document_io.cpp into the shared `src/psd/psd_descriptor.*` — PSD and
  ABR both use them.
- The user library is `src/ui/brush_tip_library.*`: `<settings dir>/brushes/` (i.e.
  `%APPDATA%/Patchy/brushes/`) with one grayscale PNG (coverage mask) + one JSON sidecar
  (`{"name", "spacing", "folder"}`) per tip; ids are the UUID filename stems. Corrupt files skip
  that tip only. Tests point the library at a temp dir via the constructor arg. Tips carry an
  organizational **folder** (empty = ungrouped, sorted first); `import_abr` files everything
  into a folder named after the .abr; `remove_tips()` bulk-deletes with a single `changed()`.
- **Built-in default tips** (`src/ui/default_brush_tips.*`): 36 parametrically generated tips —
  the 16 natural-media originals (chalk, charcoal, pastel, bristle, sponge, canvas, smoke,
  spray, spatter, stipple, ink splat, grunge, square, calligraphy, star, grass) plus the 20
  v3 stamps (dotted/dashed line, stitches, chain, rope, arrow, brick road, cobblestone, leaf,
  snowflake, rain, bubbles, flower, sparkle, heart, confetti, paw prints, footprints,
  crosshatch, RTsoft logo) — all deterministic seeded noise/SDF math, no binary assets.
  `MainWindow::brush_tip_library()` seeds them into the "Patchy Defaults" folder gated by the
  `brushes/defaultTipsVersion` settings int. Each spec carries `since_version`, and the
  startup gate calls `restore_default_tips(stored_version)` so a version bump seeds ONLY tips
  newer than the install — a user deleting older defaults is respected (the parameterless
  overload, used by the manager's "Restore Default Brushes" button, still restores everything
  missing). Direction-control stamps author their art with bitmap +X = direction of travel
  (a rightward stroke stamps unrotated). Tiling stamps (brick road, cobblestone, rope) are
  square/X-periodic with coverage touching the tile edges, because `add_tip` crops to content
  and dab advance = brush size x spacing (spacing 1.0 = exact butt joint). UI tests suppress
  seeding via `clear_brush_tip_test_state()` setting the version high; the contact-sheet test
  `ui_default_brush_tips_seed_once_and_render_sheet` renders every tip for visual review
  (artifact `ui_default_brush_tips_sheet.png`).
- UI: `BrushTipPicker` (options-bar popup grid + folder filter combo, Brush+Eraser only) and
  `request_brush_tip_manager` (`src/ui/brush_tip_manager_dialog.*`): QTreeWidget grouped by
  expandable folder rows, Extended multi-selection (Shift ranges), bulk delete via button or
  Del key (a selected folder row stands for all its tips), folder line-edit applies to the
  whole selection, live stroke preview painted by the real engine. "Define Brush Tip from
  Selection" (Edit menu, hotkey id `edit.define_brush_tip`) captures inverted luminance ×
  alpha × selection coverage — dark pixels paint, Photoshop semantics. The active tip is
  application-wide (re-applied in `apply_active_brush_settings_to_canvas`) but deliberately
  NOT persisted: every launch resets the brush to the "Round" startup preset (round tip,
  size 25, 100% opacity, 0% soft; `default_startup_brush_preset_id` applied in
  `load_tool_settings`), because restoring a stale tip or a barely-visible opacity confuses
  users. Eraser opacity/softness reset the same way; only `tools/eraserSize` survives a
  restart, and the old `tools/brushTip`/`brushSize`/`brushOpacity`/`brushSoftness`/
  `brushBuildUp`/`brushPreset`/`eraserOpacity`/`eraserSoftness` keys are neither read nor
  written anymore.
- Dialog spin boxes that keep their - / + buttons must append
  `dialog_spinbox_button_style()` (src/ui/dialog_utils) to the dialog stylesheet AFTER all
  children exist — see the sub-control gotcha note in that header (Preferences and the Brush
  Tips manager both use it).
- The Brush/Eraser cursor traces the active tip's actual outline
  (`CanvasWidget::apply_brush_tip_cursor`); when the on-screen footprint exceeds the ~155px OS
  cursor cap (tip or round), the cursor becomes a crosshair and the outline is drawn as a canvas
  overlay following the pointer (`brush_outline_uses_overlay`/`draw_brush_hover_outline`, hover
  tracked in mouseMoveEvent, cached outline image). The Alt+drag size overlay previews the
  red-tinted stamp footprint. Base shape only — per-dab pen rotation/tilt are not reflected.
- The **Soft** setting applies to bitmap tips as an outward edge feather:
  `patchy::soften_scaled_brush_tip` (3× separable box blur, pads the stamp and shifts the
  anchor), driven by `CanvasWidget::scaled_brush_tip_for(size, softness)` whose cache is keyed
  by (size, feather). Feather = size × softness% / 400. Note a soft tip's center coverage can
  drop below 100% for thin tips — soft erase leaving residue is correct behavior.
- Brush size maxes at **512** (canvas clamps, Alt+drag clamps, and the options-bar
  spin/slider/hotkeys all use `kMaxBrushSize`; Quick Select also has its own 512px cap).
- Deleted default tips are recoverable: `BrushTipLibrary::restore_default_tips()` re-adds
  missing ones (matched by name within the defaults folder); the manager's "Restore Default
  Brushes" button pairs it with `reset_default_tips_to_factory()`, which also snaps existing
  default-folder tips whose spacing/tip shape/dynamics were customized back to the shipped
  spec AND rewrites a tip's mask PNG in place (same id) when its pixels differ from the
  current generator output — so a default seeded before a generator artwork fix heals via the
  button (only the button does this — version-gated startup seeding never resets
  customizations or masks). First-run seeding reuses `restore_default_tips` under the
  `brushes/defaultTipsVersion` gate.
- **Brush dynamics** (July 2026, Photoshop-compatible): per-dab Shape Dynamics (size/angle/
  roundness jitter with minimum floors, flip X/Y jitter, angle control Off/Fade/Pen Pressure/
  Pen Tilt/Pen Rotation/Initial Direction/Direction), Scattering (scatter %, both axes, count
  1-16, count jitter), opacity jitter, plus the static Tip Shape (base angle/roundness).
  - Core engine is `src/core/brush_dynamics.hpp/.cpp` (`BrushDynamics` carried by value in
    `EditOptions`; default-constructed = `active()==false` = the historical stamp path
    bit-for-bit, zero RNG draws). Per-dab variation rides the existing inverse map
    (`TipDabTransform` gained inverse_scale + flip signs); size jitter only SHRINKS (Photoshop
    semantics), so the scaled-tip LRU cache and 2px pressure quantization are untouched. The
    RNG is splitmix64 with explicit uniform mapping — NEVER std::uniform_*_distribution
    (implementation-defined output would break cross-toolchain pixel tests). The draw-order
    contract is documented in brush_dynamics.hpp; tests depend on it. Stroke state (RNG, fade
    step index, smoothed + initial direction) lives in `BrushTipStrokeState`, seeded from
    `EditOptions::brush_dynamics.seed` on the stroke's first dab; CanvasWidget seeds per
    stroke in `clear_brush_stroke_tracking()` (`set_brush_dynamics_test_seed` is the UI-test
    hook for reproducible stroke artifacts).
  - Dynamics are Brush-tool only: erase strokes strip `brush_dynamics` in draw_brush_segment /
    draw_brush_at (flip that gate to enable eraser dynamics later). The procedural **Round
    brush supports dynamics too** (July 2026): its capsule renderer has no dab loop, so while
    `brush_dynamics_.active()` a Round Brush stroke stamps through a synthesized 256px disc
    tip (`round_dynamics_tip_mips()` in canvas_widget.cpp, spacing 25%, Soft feathers it like
    any bitmap tip, pressure sizes quantize to 2px like tips); inactive dynamics keep the
    historical capsule path bit-for-bit, and a Round ERASE stroke always stays procedural
    (the erase gate also nulls the synthesized tip). Round dynamics are **session-only**:
    they live in `MainWindow::round_brush_dynamics_` (+ base shape), are never persisted, and
    reset to plain on every launch so a weird leftover setup cannot confuse anyone.
    `draw_brush_at` routes tip presses through a stateful zero-length `paint_brush_segment`
    so the press dab is not re-stamped by the first move segment (invisible for static
    stamps, visibly double-jittered with dynamics).
  - Per-tip persistence: `BrushTipEntry` carries dynamics + base angle/roundness; the JSON
    sidecar gains top-level `"baseAngle"`/`"baseRoundness"` and a `"dynamics"` object
    (camelCase fractions 0..1, scatter 0..10, string enum tokens like `"direction"`), written
    only when non-default. An older build editing such a tip rewrites the sidecar without the
    new keys and silently drops them — accepted. The options-bar **Dynamics** button
    (`BrushDynamicsButton`, src/ui/brush_dynamics_popup.*, Brush tool only) edits the active
    bitmap tip (persisted via `BrushTipLibrary::set_tip_dynamics`, debounced ~200ms) or the
    Round brush's session values (`set_round_session`, routed on the builtin round id in the
    MainWindow `dynamics_edited` handler, never persisted); the canvas gets the values
    through `apply_brush_tip_to_canvas`. Launch behavior is unchanged: the session brush
    still resets to Round with plain dynamics; per-tip dynamics live only in sidecars. The
    manager dialog's stroke preview renders dynamics with a fixed seed.
  - ABR import extracts the supported dynamics (`parse_brush_dynamics`, abr_reader.cpp) from
    the brushPreset descriptor: `useTipDynamics` gates `szVr`/`angleDynamics`/
    `roundnessDynamics` (class `brVr`: `bVTy`/`fStp`/`jitter`/`Mnm `) + sibling
    `minimumDiameter`/`minimumRoundness` + preset-level `flipX`/`flipY` (the flip JITTERS —
    the static tip flips are the ones inside the `Brsh` object); `useScatter` gates
    `scatterDynamics`/`bothAxes`/`Cnt ` (a double)/`countDynamics`; `usePaintDynamics` gates
    `opVr`. Base `Angl`/`Rndn` come from the `Brsh` tip object. `bVTy` mapping: 0 Off, 1 Fade,
    2 Pen Pressure, 3 Pen Tilt, 4 Stylus Wheel (→Off), 5 Rotation, 6 Initial Direction,
    7 Direction. v1 simplification: only the ANGLE control is imported; size/roundness/
    scatter/count controls drop to jitter-only, and the global `input/pen/*` pressure
    preferences stay authoritative for size/opacity pressure response. A preset with texture/
    dual brush/color dynamics enabled imports without them plus one per-brush warning.
  - When `input/pen/tiltShape` is on and the tip's angle control is PenTilt/PenRotation, the
    per-dab path owns the angle and effective_brush_input skips its tilt-angle assignment
    (tilt→roundness still applies) so the stamp is not rotated twice.
  - The built-in default tips ship with **curated dynamics** (`DefaultBrushTipSpec::dynamics`
    in default_brush_tips.cpp): particle/scatter tips scatter (spray/spatter/stipple/star/
    smoke/ink splat/leaf/snowflake/rain/bubbles/flower/sparkle/heart/confetti), media tips get
    subtle angle/opacity jitter, and the path stamps (dashed line, stitches, chain, rope,
    arrow, brick road, cobblestone, paw prints, footprints) plus Bristle + Grass use the
    Direction angle control (the bristle dot-row must stay perpendicular to travel). Canvas,
    Square, Calligraphy, Dotted Line, and RTsoft Logo deliberately stay plain. Seeding is
    gated by `brushes/defaultTipsVersion` = **3** (v3 added the 20 stamp tips); upgrades from
    < 2 additionally run `BrushTipLibrary::apply_default_tip_dynamics()` once — it must NEVER
    re-run for stored version >= 2 because it cannot distinguish "user reset dynamics after
    v2" from "never migrated", so a re-run would stomp deliberate resets. It only touches
    default-folder tips whose dynamics/tip shape are still untouched defaults — user
    customizations always win. The manager's "Restore Default Brushes" button re-adds missing
    tips with their curated dynamics but never re-migrates existing ones.
  - The dynamics FORM is the shared `BrushDynamicsPanel` (brush_dynamics_popup.*): the
    options-bar button popup embeds it, and the Brush Tips manager's "Edit Dynamics…" button
    (`brushTipManagerDynamicsButton`, single selection only) opens it in a child dialog
    (`brushTipManagerDynamicsDialog`) with live debounced apply + stroke-preview refresh.
    Tips carrying dynamics (or a non-default tip shape) show a small blue corner badge on
    their thumbnails everywhere (`brush_tip_entry_has_dynamics` /
    `brush_tip_thumbnail_with_badge` in brush_tip_library) plus a " • dynamics" tooltip
    suffix.
  - The tip picker popup is resizable via an embedded QSizeGrip (a frameless Qt::Popup has no
    native resize border); the size persists as `ui/brushTipPickerPopupSize` (saved in the
    popup subclass's closeEvent — outside-click dismissal goes through close()). The grip is
    custom-painted (`VisibleSizeGrip`, three light diagonal strokes): the native style's grip
    is nearly invisible on the dark QSS theme.
  - Patents: shape dynamics/scattering shipped in Photoshop 7 (2002); the covering patents are
    expired and GIMP/Krita have shipped equivalent jitter/scatter engines for two decades — no
    design constraint here (unlike Quick Select below).
- v1 limitations (deliberate): layer-**mask** painting and Clone/Smudge stay procedural;
  texture, dual brush, color dynamics, wet edges, and flow are neither imported nor modeled.
- Fixtures: `test-fixtures/abr/myer-settlement-brushes.abr` (CC0, see NOTICE.txt; 148 brushes,
  v6.2, dynamics all disabled — pins the defaults path) and the self-authored
  `photoshop-dynamics.abr` / `photoshop-dual-brush.abr` (exported from Photoshop 2026, one
  probe brush each with known dynamics values; the dual one exercises the unsupported-feature
  warning). Bigger CC0 sets for manual testing live in `local-test-fixtures/abr-sets/`.
  Coverage: `brush_tip_*`/`tool_brush_tip_*`/`brush_dynamics_*`/`abr_*` in test_main.cpp,
  `ui_brush_tip_*`/`ui_brush_dynamics_*` in ui_visual_tests.cpp.

## Tool palette icons are hand-authored SVG resources

The 19 tool icons are original SVGs at `src/ui/icons/tool-*.svg` (32x32 viewBox, `#dce2eb`
strokes ~2.4 primary / 2.0 detail, round caps/joins, at most one `#74c0ff` accent element that
is never load-bearing: icons must read from the gray geometry alone on the `#2f75bd` checked
background). Registered in `src/ui/icons.qrc`, mapped by `tool_icon()` (main_window.cpp), which
calls `qInitResources_icons()` itself (resources live in the static `patchy_ui` lib). The flyout
corner triangle is `tool-flyout-corner.svg` via the `::menu-indicator` QSS rules for the three
flyout buttons. Keep to plain SVG elements/presentation attributes (QtSvg renders a Tiny-1.2
subset; `linearGradient` works and is used by tool-gradient.svg, pinned by the visual test).
Review the whole set with `patchy_ui_visual_tests.exe ui_tool_palette_icons`, which writes
`test-artifacts/ui_tool_palette_icons_sheet.png` (normal/hover/checked/disabled at 20px + 40px)
and `ui_tool_palette.png` (real toolbar grab); the test CHECKs per-icon pixel coverage (a
typo'd qrc alias renders EMPTY silently) and pairwise distinctness. All path data is original
(generic metaphors, no Photoshop geometry), so the license stays clean.

## QListWidget rows built with setItemWidget must paint their own selection

The layers panel (`restyle_layer_rows`, main_window.cpp) and the Layer Style dialog's category
list (`restyle_category_rows`, layer_style_dialog.cpp) place opaque row widgets over their
QListWidget items, so the `::item:selected` QSS background is hidden behind them: selection
styling is applied to each row widget's stylesheet whenever the selection changes. Two gotchas
pinned down in July 2026:

- QSS `::item` padding insets the item-widget geometry, and a widget `minimumHeight` taller
  than the padded content rect wins over the requested geometry, so the widget bleeds into the
  next row (misaligned separators and selection slivers). Give such lists `padding: 0` on
  `::item` and let the row widget's layout margins provide the spacing.
- Once a row widget gets a stylesheet, its QLabel/QCheckBox children are drawn through the QSS
  path and fill with the inherited palette background; set `background: transparent` on them
  explicitly in the row's stylesheet.

## Gradient stop editor widget (shared, two-track)

`GradientStopsEditorWidget` (src/ui/gradient_stops_editor.*) is the draggable gradient-stops bar
used by both the gradient tool's "Edit Gradient Stops" dialog (single-track mode: combined RGBA
stops below the bar) and the Layer Style dialog's Gradient Overlay page
(`set_opacity_track_enabled(true)`: Photoshop-style opacity tags above the bar, RGB color tags
below, fixed height 96 instead of 66). Conventions that matter when touching it:

- The widget never mutates its own stop vectors; every interaction fires a callback and the host
  pushes the new state back through the setters. At most one stop is selected across both tracks
  (selecting on one track clears the other).
- The Gradient Overlay page's `gradient_editor_color_stops`/`gradient_editor_alpha_stops` vectors
  ARE the page's widget state: `save_controls_to_style` copies and sorts the *copies*. Never sort
  the working vectors in place — an in-flight tag drag holds an index into them.
- Single-track geometry is pinned by `ui_options_bar_tracks_active_tool` (bar y=16, tags y≥44);
  two-track geometry by the `ui_layer_style_gradient_*` and `ui_gradient_stops_editor_two_track_*`
  tests (opacity area y 0..27, bar y 30..60, color tags ~y 66..89).

## Options toolbar controls share one fixed row height

Every control in the Options bar (`QToolBar#Options`) is pinned by the app stylesheet to 26px
total height (24px QSS min/max-height + 1px borders for labels, combos, spinboxes, checkboxes).
Any taller control grows the whole toolbar only while its tool is active, so the canvas shifts
up/down on tool switches. Gotcha: QStyleSheetStyle inflates QToolButton size hints by +3px (a
"broken QToolButton" workaround inside Qt), so a QToolButton placed in the bar needs an explicit
QSS min/max-height cap; icon-only buttons use the `optionsBarButton` property rule, and the
Brush/Eraser Tip picker has its own `QToolButton#brushTipPicker` rule (20px content + 2px padding
+ 1px border = 26px). `ui_brush_tip_picker_keeps_options_bar_height` asserts the bar keeps one
height across all tools.

## Status bar hosts the zoom percentage box via a QStatusBar subclass

The main window's status bar is a `ZoomStatusBar` (`src/ui/zoom_status_bar.*`, installed with
`setStatusBar` before any `statusBar()` call). Reason: QStatusBar hides every non-permanent
widget whenever a message is showing, and Patchy keeps a persistent message up almost always,
so the Photoshop-style zoom box (`ZoomPercentEdit`, object name `statusZoomEdit`) is a manually
positioned child at the far left, outside the item layout, and the subclass repaints the
temporary message offset to the widget's right. Any future left-side status-bar widget must use
this same pattern, not `addWidget()`. The box commits on Enter/focus-out, reverts on Escape,
zooms about the viewport center, and is refreshed from `refresh_document_info()` (guarded by
`isModified()` so it never stomps in-progress typing). Coverage:
`ui_status_bar_zoom_percent_box_edits_zoom`.

## Argument evaluation order: never read a variable beside std::move(it)

`f(g(x), std::move(x))` is broken: C++ argument evaluation order is unspecified and MSVC goes
right-to-left, so `x` is moved into the by-value parameter before `g(x)` reads it. This silently
committed empty feathered selections (marquee/lasso/wand Add deleted the whole selection, fixed
July 2026). Compute `g(x)` into a local first, or use an overload that derives the value
internally — `CanvasWidget::combine_selection_from_mask(bounds, mask)` exists for exactly this.

## Quick Select tool: solve-on-release is a patent constraint, not a UX choice

The Quick Select tool (`CanvasTool::QuickSelect`, Shift+W, flyout shared with the Magic Wand)
segments each brush stroke ONCE on mouse-release: the drag only accumulates a seed footprint
(`stamp_quick_select_segment`) and draws a translucent capsule overlay, then
`finish_quick_select_stroke` runs the cut and commits one undo entry. **Do not add live
per-mouse-move classification or selection preview before Nov 3, 2029**: Adobe's US 8050498
("Live coherent image selection", term-adjusted) claims classify-and-display while brush input
is being received. Related design rules, all deliberate: Enhance Edge is purely geometric
majority smoothing (US 8013870 claims local-color-model edge opacity until 2028), the solve
uses ONE window per stroke (US 8121407 claims overlapping-tile decomposition until 2030), and
there is no input-driven auto-switching between segmentation algorithms (US 10698588). The
foundations used are expired: Boykov-Jolly seeded min-cut (US 6973212), GrabCut color models
(US 7660463), MSR Paint Selection (US 8452087, fee-lapsed).
`ui_quick_select_stroke_selects_object_and_is_undoable` asserts the no-mid-drag-selection
behavior, so a live-preview change will fail it.

- The algorithm is Qt-free in `src/core/quick_select.hpp/.cpp`: RGB-histogram color models, a
  from-scratch Boykov-Kolmogorov max-flow on the 8-connected window grid (`detail::GridMaxflow`,
  validated against an Edmonds-Karp reference in
  `quick_select_maxflow_matches_reference_on_random_grids`), a seed-connectivity filter, hole
  filling of delta-enclosed regions (catchlights inside an eye; the flood must travel through
  ALL unchanged pixels or kept rings around a subtraction get eaten), and windows over ~600k px
  solved on a downsample.
- The energy was CALIBRATED AGAINST PHOTOSHOP 2026 (July 2026) by replaying identical clicks in
  both engines on `local-test-fixtures/photos/akiko-birthday.psd` (uncommitted) and scoring
  IoU; final agreement ~0.88-0.93 on object clicks (eye/tooth), ~0.6-0.8 on flat-area clicks
  where the residual is a 1-3px rim halo. PS's tool options descriptor (AM
  `currentToolOptions`: `quickSelectBrushSize`, `quickSelectSpread`, `quickSelectStickiness`,
  `autoEnhance`) confirmed the model. The load-bearing design decisions, each pinned by the
  probe data — do not "simplify" them away:
  (1) posterior = SYMMETRIC epsilon over raw frequencies (per-model Laplace floors made every
  unseen color lean foreground and flood); (2) background sampled from the solve WINDOW, not
  the whole image; (3) `kBackgroundPrior` on unbrushed pixels (neutral pixels otherwise join
  the source side for free); (4) a SPREAD BUDGET (`QuickSelectParams::spread`, PS-like, growth
  ~2.5x brush radius at 50) via a chamfer-distance ramp with a steep wall — a click on a
  featureless area returns roughly the brush disc, PS-verified; (5) boundary-dominant
  smoothness (lambda 8) with a THRESHOLDED edge classifier (smoothstep between 2x and 6x the
  window's mean pair distance, 0.15 floor) instead of the GrabCut exponential — soft shading
  gradients and noise attract nothing, real contours are cheap, which is why a click inside an
  eye takes the whole eye opening (including dissimilar catchlights) like PS; (6) the 3x3-blur
  denoise feeds ONLY the contrast term — blurring the data term smears hard boundaries and
  insets the cut ~2px (`quick_select_stroke_grabs_flat_region_and_respects_edges` catches it).
  The scratch comparison harness (qs_probe / mask_compare / a COM+computer-use PS click driver)
  lives in the session scratchpad, not the repo; the methodology is reproducible from this
  note: fixed PS window, 100% zoom, magenta fiducial calibration, masks exported via
  fill-and-saveAs with history restore.
  Tune `lambda` / `kBackgroundPrior` / spread constants only with the `quick_select_*` core
  tests green; they encode the PS-calibrated behaviors (bounded taps, big-seed full coverage,
  budget-bounded subtract shave vs covering-stroke full removal).
- Quick Select has no Intersect mode (Photoshop parity): Shift+Alt clamps to Add at press, and
  the Intersect options-bar button is simply not listed for the tool. A stroke in New mode
  auto-switches the tool to Add (`set_selection_mode(Add)` in `finish_quick_select_stroke`),
  and the MainWindow selection-mode callback writes canvas-driven changes back into
  `selection_modes_` so the mode survives tool/document switches.
- `kSelectionToolCount` is 5; growing it again means updating BOTH brace-initializers
  (canvas_widget.hpp `selection_modes_per_tool_`, main_window.hpp `selection_modes_`) plus the
  tools array in `apply_selection_modes_to_canvas`.

## Marching ants selection outline is traced and cached

The selection outline is no longer rebuilt per paint from QRegion edge subtractions.
`src/ui/selection_outline.*` traces the selection into closed contour loops
(`trace_selection_outlines`, marching-squares wall-follow; diagonal pixels stay
4-connected) and `CanvasWidget` caches both the document-space loops and the stroke-ready
device-space paths (`ensure_selection_outline_screen_path`, keyed by zoom/pan/viewport), so
the 120 ms animation tick only restrokes. Two consequences for future code:

- Any new code that writes `selection_` / `selection_display_region_` directly instead of
  going through `set_selection_from_region` / `set_selection_from_mask` /
  `restore_selection_before_edit` / `apply_selection_snapshot` / `reselect` must call
  `invalidate_selection_outline()` or the ants will render a stale outline.
- Below 100% zoom the outline is retraced at *device resolution* (AA coverage
  rasterisation thresholded at 50%, `trace_device_selection_outlines`), which merges or
  drops sub-pixel holes/islands exactly like the scaled-down artwork — do not "fix" that
  by tracing document space at low zoom; it strobes. Loops shorter than one 4-4 dash
  period go to a separate `pinpoint` path drawn with 2-2 dashes over the solid black
  underlay so single-pixel selections never blink invisible.

Test filters: `selection_outline` (tracer/path units) and `ui_marching_ants` (rendering).

## Reviewing a contributor PR ("let's look at this PR…")

When Seth points at a PR and wants it reviewed, this is the workflow he expects — do these
steps in order and let him drive the merge decision:

1. **Read it first.** `gh pr view <N>` and `gh pr diff <N>`, then review the change in
   context (open the touched files, verify referenced symbols/patterns actually exist and
   match surrounding code). Explicitly check for malicious or sketchy code — network calls,
   filesystem/process/shell access, new dependencies, obfuscation. Report a short verdict.
2. **Build & test it locally — never create remote branches or worktrees.** Fetch the head
   into a local branch (`git fetch origin pull/<N>/head:pr-<N>`), check it out, run the
   release build, then run the relevant test subset plus the full UI + core suites. A running
   `build\release\patchy.exe` will lock the link step (`LNK1104`); ask Seth to close it rather
   than force-killing (he may have unsaved work).
3. **Let Seth test the built `patchy.exe` himself** and wait for his go-ahead. Do **not**
   merge until he says to.
4. **Confirm the merge path before merging.** Do not default to `gh pr merge` as soon as Seth says
   to merge; ask whether he wants a straight GitHub merge or a local merge that can include
   follow-up commits first (for example missing translations found during review).
   - For a straight remote merge, use GitHub with a real merge commit:
     `gh pr merge <N> --merge`, then `git checkout main`, `git pull origin main`, and delete the
     temporary `pr-<N>` local branch.
   - For a local merge with follow-ups, `git checkout main`, `git pull origin main`, then
     `git merge --no-ff pr-<N>` so the contributor's commits keep their authorship. Add the
     follow-up commit(s), build/test the final tree, then push `main`. GitHub should still mark
     the PR merged once the PR head commit is reachable from `main`.
   Never squash contributor PRs, which would strip their commit attribution.

## Build system: runtime asset copies are shared copy-once targets

Fonts, Qt DLLs/plugins, and the Qt base translation are copied into the build directory by shared copy-once custom targets in `CMakeLists.txt` (`patchy_bundled_fonts`, `patchy_qt_runtime`, `patchy_qt_base_translations`); executables depend on them via the `patchy_copy_*` helper functions. Never attach per-target POST_BUILD copies that write into the shared output directory: all executables land in the same folder, and concurrent copies of the same destination file race under parallel Ninja (this caused intermittent release-build failures, fixed July 2026). New executables that need these assets should just call the existing `patchy_copy_*` helpers.

## Testing notes

- `patchy_ui_visual_tests.exe` must run with `QT_QPA_PLATFORM=offscreen`. The offscreen platform does **not** enumerate installed Windows fonts; register what a test needs with `QFontDatabase::addApplicationFont("C:/Windows/Fonts/<file>.ttf")`. Never call `removeApplicationFont` to clean up — invalidating an in-use font cache can hard-crash the suite.
- A registered font may not appear under its familiar GDI name: the offscreen FreeType database uses the OpenType *typographic* family, so ariblk.ttf registers as family "Arial" + style "Black", not "Arial Black". Patchy's text code resolves such names via `available_text_family_style_match` (main_window.cpp); tests should not gate on `QFontDatabase::families().contains("Arial Black")`-style checks.
- The test harness `CHECK()` macro throws. A failing CHECK while a `MainWindow` with an open inline text editor is still alive aborts the process during unwind (exit code 3, no `[FAIL]` line printed). Prefer asserting after the editor session is committed/closed, or structure tests so CHECKs that may fail do not unwind past a live editor.
- Tests save PNGs via `save_widget_artifact(...)` into `test-artifacts/` next to the binary — inspect them to visually confirm rendering behavior.
- The shape tools (rectangle/ellipse, pixel layers and masks) honor the brush **Opacity**/**Soft** settings and go through the single-pass signed-distance renderer in `pixel_tools.cpp` (`render_shape`/`shape_pixel_coverage`); a 1px outline keeps a crisp legacy Bresenham path. The **Fill command** (`layerFillForegroundAction`/`fill_active_layer_with_color`) uses its *own* Fill Opacity/Soft settings — `CanvasWidget::fill_opacity()`/`fill_softness()`, controls on the Fill tool's options bar, persisted as `tools/fillOpacity`/`tools/fillSoftness`, defaulting to **100% / 0** so fills are solid by default. Soft feathers the fill inward from the selection edge via `fill_rect`'s `EditOptions::fill_softness_feather` (an inward distance-transform). Tests that use Fill only to lay down a solid setup color call `use_solid_fill_settings(canvas)` (ui_visual_tests.cpp) to force 100/0 against any persisted value.
- A click on a layer-row mask/content thumbnail can rebuild the layer row (the old row widget is deleted), so never reuse a cached row/thumbnail pointer across the press — use `click_layer_row_thumbnail(...)` in ui_visual_tests.cpp, which refetches the widget for press and release. Reusing the stale pointer is a use-after-free that flakes only when the heap reuses the freed block (this was the June 2026 "unreproducible" suite crash).
- If the visual suite dies with an access violation, the log now ends with a symbolized stack (a dbghelp vectored handler in `main`) — read it instead of re-running and hoping.
- Run a subset of visual tests by passing a name substring as the first argument (or `PATCHY_UI_TEST_FILTER`): `.\patchy_ui_visual_tests.exe ui_audio_splitter`. There is no `--test` flag.

## Photoshop compatibility verification

Adobe Photoshop 2026 is installed on this machine and is the ground truth for PSD compatibility work. It is COM-scriptable from PowerShell: `(New-Object -ComObject Photoshop.Application).DoJavaScript($jsx)` (the first call launches Photoshop, ~30s).

- To learn how Photoshop encodes a setting, save two PSDs differing in exactly one UI toggle and byte-diff them. This is how the layer-mask link flag (mask flags bit 0 = unlinked) and the "use global light" handling (`uglg` + image resources 1037/1049) in `src/psd/psd_document_io.cpp` were pinned down in June 2026.
- Photoshop semantics established the same way, encoded in code + tests: layer record flags bit 3 ("Photoshop 5.0 and later") must be written on every layer — without it Photoshop applies legacy semantics and badly misrenders layers that combine an unlinked mask with effects. The layer mask shapes layer effects (shadow/stroke/glow sources) regardless of the link state — the chain toggle affects move behavior only, never rendering. Effect *output* may still spill onto mask-hidden areas unless the "Layer Mask Hides Effects" blending option is on (tagged block 'lmgm', 4 bytes, first byte = bool; modeled as `LayerStyle::layer_mask_hides_effects` and exposed in the layer style dialog's Blending Options page). Beware confounded controls when byte-bisecting: an early conclusion here was wrong because the "control" file lacked bit 3 and went through Photoshop's legacy path.
- lfx2 effect **blend modes must be written as full stringIDs** ("multiply", "screen", ...) in the 'BlnM' enum — Photoshop 2026 silently reads 4-char codes ('Mltp', 'Scrn') as Normal (pinned July 2026 by byte-patching probe PSDs; a 16-mode sweep through PS verified every mode Patchy writes). The parser accepts both forms via `blend_mode_from_descriptor_enum`; the writer emits stringIDs (`blend_mode_descriptor_value`). Additionally, the **GrFl (gradient overlay) descriptor is shape-sensitive**: PS resets its blend mode to Normal unless the descriptor mirrors PS's own 14-item layout (`enab, present, showInDialog, Md, Opct, Grad, Angl, Type, Rvrs, Dthr, gs99, Algn, Scl, Ofst`) — other effect descriptors are not shape-sensitive (drop shadow/outer glow blend modes survive with Patchy's leaner layouts). Gradient stop midpoints (`Mdpn`) are not modeled: read as default, written as 50 — a PS file using non-default midpoints loses them through a Patchy re-save (known limitation).
- To check how Photoshop interprets a Patchy-written file, query Action Manager getters, e.g. `executeActionGet` of a layer reference and read `userMaskLinked` / `userMaskEnabled`.
- To compare renders, export Photoshop's flattened view and diff it against `Compositor::flatten_rgb8` of the same file. Gotcha: Photoshop's `doc.saveAs`/`doc.duplicate` fail with a misleading "disk error (-1)" on documents whose smart-object layers ('PlLd'/'SoLd' blocks) reference missing document-global 'lnk2' data — pre-June-2026 Patchy builds produced such files by dropping the global tagged-block section (now preserved; dangling references are stripped on save). For such damaged files, `doc.selection.selectAll(); doc.selection.copy(true)` (merged), paste into a fresh document, flatten, and save a 24-bit BMP from there. Single composite pixels can be probed without exporting via `doc.colorSamplers` (max 4 exist at once — add/read/remove in a loop).
- Script hygiene: set `app.displayDialogs = DialogModes.NO`, only close documents the script opened, and close with `SaveOptions.DONOTSAVECHANGES`.
- Small Photoshop-authored regression fixtures are committed under `test-fixtures/psd/` (e.g. `photoshop-unlinked-mask.psd`, `photoshop-global-light-shadow.psd`). Generate new ones with a COM script rather than hand-crafting bytes.
- Quick scratch tools that link Patchy's release libs (e.g. to flatten a PSD through Patchy's reader outside the test suite) compile with:

  ```powershell
  cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /O2 /MD /I <repo>\src <tool>.cpp /link /LIBPATH:<repo>\build\release patchy_psd.lib patchy_render.lib patchy_core.lib patchy_color.lib patchy_filters.lib gdi32.lib user32.lib advapi32.lib dwrite.lib'
  ```

  `/MD` is required to match the libs; `advapi32`/`dwrite` are needed by `psd_document_io`'s font-resolution code.

For code changes, ALWAYS finish work in this repository by refreshing the local release build — `build\release\patchy.exe` must be freshly built from the final working tree at handoff, never stale. If the change is documentation-only or otherwise cannot affect compiled/runtime behavior (for example README text/images, comments, or agent instructions), do not run the full release build/test handoff just for that change; instead report that the release handoff was skipped because the change is non-code.

Required release handoff steps:

1. Build the release preset:

   ```powershell
   cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release'
   ```

   `build\release\CMakeCache.txt` carries hand-edited flags the preset does not set: `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi` and `CMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /INCREMENTAL:NO`. They make release builds emit `patchy.pdb`, which is what symbolizes user crash dumps (WER minidumps land in `%LOCALAPPDATA%\CrashDumps`). If you reconfigure from scratch, pass them again with `-D...`; to symbolize a dump from an older build, rebuild that commit in a temp `git worktree` with the same flags (full links reproduce the binary layout) and read the dump against that PDB.

2. Run the release test binaries from `build\release`:

   ```powershell
   .\patchy_core_tests.exe
   $env:QT_QPA_PLATFORM='offscreen'; .\patchy_ui_visual_tests.exe
   ```

3. In the final response, explicitly report whether the release executable exists at:

   ```text
   build\release\patchy.exe
   ```

Do not say a release was created unless the release preset build completed successfully.
