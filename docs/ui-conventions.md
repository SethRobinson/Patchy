# UI conventions

Read this before changing QActions, dialogs, the options bar, list-row widgets, status messages, application QSS, or other shared UI behavior.

## Color scheme

Patchy ships Dark and Light, chosen in Preferences (`preferences/colorScheme`, values `system`/`dark`/`light`, default `system`). Switching applies live; nothing requires a restart. `ThemeManager` in `src/ui/theme_manager.hpp` owns the preference, resolves "follow system" through `QStyleHints::colorScheme()`, and emits `color_scheme_changed`. It also mirrors the resolved scheme onto Qt with `QStyleHints::setColorScheme`, which is what makes native chrome Patchy does not style (Windows dock and list scroll bars, tooltips, `QMessageBox`, native title bars) agree with the app.

Standing rules:

- **No hex literals for chrome anywhere in `src/ui`.** Colors are named roles in `src/ui/theme_palette.hpp`. QSS writes them as `@role_name` tokens; painted widgets and delegates read `theme().role_name`. Adding a role means the member, a Dark value, and a row in the role table; `ui_theme_palettes_define_every_role` fails if you miss one.
- **Dark is authored, Light is derived then corrected.** `light_palette()` mirrors the lightness of every Dark role, lifts the mirrored neutrals into Light's surface band, and then overrides the ones where that is wrong (the canvas backdrop, bevels, brand and state colors, icon ink). A new role therefore needs no Light decision unless it deserves one.
- **The lift is what makes Light look lit.** Dark packs its surfaces into the bottom third of the value range, so a plain mirror parks every panel and toolbar in the middle grays and the app reads as medium gray rather than light. `lift_to_surface_band()` raises that end only; ink mirrors below the first stop and passes through untouched. It is monotonic, so roles cannot swap order, but it compresses near white.
- **A mirror inverts an outline.** A line darker than its surface in Dark comes back lighter than it in Light, which on a near-white surface is no line at all. Those roles are corrected by hand in `light_palette()` (field wells, the tool column and dock outlines, the splitter groove); a line that is *lighter* than its surface in Dark is ink and mirrors correctly. When adding a role that draws a line, check which of the two it is.
- **Never hand token text to `setStyleSheet`.** Qt drops the whole declaration containing an unresolved token, silently, so a rule just stops existing. Use `set_themed_style` / `append_themed_style` from `src/ui/theme_qss.hpp`; the shared builders return `ThemedQss` so the mistake is a compile error, and `ui_no_widget_ships_unresolved_theme_tokens` catches inline blobs that slip through. Rich-text labels with colored links use `set_themed_label_text`.
- **`append_themed_style` preserves order.** A widget's sheet is an accumulation (chrome, then the dialog's own block, then `dialog_spinbox_button_style()` last), and the stored template replays in the same order on a scheme change. A dialog that rebuilds its whole sheet reads the accumulated template back with `themed_style_template`, never `styleSheet()`.
- **A scheme change needs no `setIcon` call.** Icons are backed by engines in `src/ui/icon_theme.hpp` that resolve colors when painted, so existing `QIcon`s render correctly on their next repaint. Do not "fix" this by rebuilding icons.
- **Never bake a color into a `QIcon`.** Painting a glyph into a `QPixmap` and wrapping it freezes whichever scheme was active when the icon was built; that is what left the window buttons and dialog closers as near-white marks on Light's pale chrome. One-off procedural marks go through `themed_glyph_icon` in `src/ui/action_icons.hpp`, which hands the glyph its ink at paint time. The same applies to a widget that paints its own chrome: read `theme()` inside `paintEvent`, never a literal.
- **If you cache a pixmap derived from `theme()`, drop it on a scheme change.** Qt sends no event for a palette-struct change. `MainWindow::apply_color_scheme` clears the layer, channel, and path thumbnail caches for this reason.
- **Some colors deliberately do not follow the scheme.** The transparency checkerboard depicts alpha and stays light in both themes, like every other editor, so "grid means transparent" remains a learned signal. Marching ants, the clone-source marker, and tool cursors are black-plus-white pairs engineered to read over arbitrary artwork; tying them to UI colors would make a selection vanish over matching pixels. Saved grid and guide colors are user data. The app icon and splash artwork composite against the OS, not Patchy's chrome. Adjustment-layer glyphs encode meaning through their own internal contrast (the Threshold glyph *is* a black/white split).

Authoring icons: 32x32 viewBox, lowercase hex, drawn from the ten-color vocabulary in `icon_color_roles()` (`icon_ink` plus nine accents). `ui_icon_color_map_covers_every_authored_color` rejects anything outside it. The two paint-swatch icons and the seven referenced from QSS by `url()` are exempt and listed explicitly in `icon_theme.cpp`.

## Hotkeys

Application-level QAction shortcuts must be registered through `MainWindow::register_hotkey(action, "stable.id", default_seq)`, backed by `HotkeyRegistry` in `src/ui/hotkey_registry.hpp`. Never call `setShortcut` or `setShortcuts` directly on an app-level action. Command ids are persisted and must never be renamed. Two commands must not share a default shortcut; `ui_hotkey_defaults_have_no_conflicts` enforces this.

A canvas tool that needs a key owned by an application shortcut must accept the `ShortcutOverride` in `CanvasWidget::event()` while active. Magnetic Lasso Delete/Backspace is the reference pattern; see [selection-tools.md](selection-tools.md).

## Dialog lifecycle

Dialogs that react to closing must funnel every path through `done()`. `reject()` hides a dialog without a QCloseEvent, so closeEvent-only cleanup misses the chrome X and Esc. Do not override `reject()` to call `close()`: `QDialog::closeEvent` calls `reject()` and treats a still-visible dialog as vetoing the close.

New non-modal dialogs must use `run_non_modal_dialog`. It rejects child dialogs when their parent finishes and applies the macOS above-parent native anchor. `request_patchy_color` permits one picker at a time; transient pickers retain their own position-memory group.

Dialog spin boxes that retain their minus/plus buttons must append `dialog_spinbox_button_style()` from `src/ui/dialog_utils` to the dialog stylesheet after all children exist.

## Item-widget rows and selection

Rows installed with `QListWidget::setItemWidget` must paint their own selection because an opaque row widget hides `::item:selected` QSS. Give the list `padding: 0` on `::item`, make the row's child containers transparent, and keep the global `QCheckBox { border: none; }` rule for correct macOS layout.

Every plain QWidget container within the row must have a transparent background in application QSS. With a global QWidget background rule, QStyleSheetStyle applies `WA_StyledBackground`, so `setAutoFillBackground(false)` alone does not prevent the container painting over its row.

Layer rows use dynamic properties and application rules such as `QWidget#layerRowWidget[layerRowSelected="true"]`; repolish after property changes. The `layerTargetActive` pattern is the reference. `ui_layer_row_selected_highlight_paints` pins the rendered colors.

The layer list may omit rows entirely: collapsed folders and the Layers panel name filter (`layerNameFilterEdit`) both rebuild without rows for excluded layers. Never assume every document layer has a row, and never introduce a "row exists but hidden" state; absent rows are the single not-shown state all consumers are hardened for. While the name filter is active, `LayerListWidget::set_drag_blocked` refuses drag reordering because a reorder would silently move filtered-out layers; each refused attempt reports through `show_status_error` and leftover held-button moves are swallowed so the base view cannot start a drag-selection sweep.

The visibility eye (`layerVisibilityCheck`) does not toggle through its QToolButton on real mouse input: `LayerListWidget::eventFilter` eats the press first. A plain press toggles on press and starts a visibility sweep (dragging along the eye column paints the first toggle's state across crossed rows, skipping disabled eyes; `setSelection` is suppressed while sweeping so the drag cannot rubber-band the selection). An Alt press calls `MainWindow::isolate_layer_visibility`, which hides every other layer and restores the per-session snapshot on the second Alt-click; any outside visibility change invalidates the snapshot and the next Alt-click isolates fresh. Both handlers defer through `QTimer::singleShot(0)` because folder toggles rebuild the rows, and the button's `toggled` connection remains for programmatic `click()` (tests rely on it). Visibility stays non-undoable.

`bind_widget_text` on a QLineEdit binds the placeholder text, not `text()`, since line edits carry user data in `text()`.

## Options bar

All options-bar controls share one fixed 26 px row height. QStyleSheetStyle inflates QToolButton size hints, so options-bar buttons need explicit min/max height caps. Free-transform and warp sessions own the row and hide per-tool widgets.

Options-bar numeric controls must use `configure_toolbar_spinbox`. Set range, decimals, prefix, and suffix before calling it because its width is only a minimum and the helper expands for the widest value plus its trailing chevron. Do not duplicate the popup or substitute a plain spin box. Dialog and transform-session fields use `configure_dialog_spinbox` when the range popup is inappropriate.

When the typed range is wider than the useful drag range, set `kToolbarSpinboxSliderMaxProperty` from `dialog_utils.hpp`; do not narrow the spin-box range. Text Size, for example, accepts 10000 pt while its slider normally stops at 200 and expands if the current value is higher.

Application-wide tool state follows the `current_*` mirror pattern in MainWindow: update the member from the control signal, apply it both during new-session setup and `activate_document_tab`, and update the mirror in `load_tool_settings()` under `QSignalBlocker`. Updating only the current canvas desynchronizes newly created or reactivated documents.

## Status bar

QStatusBar hides non-permanent widgets whenever a message is displayed. Patchy's left-side status widgets are manually positioned children of `ZoomStatusBar`; never add them with `addWidget()`.

Blocking refusals and failed operations use `MainWindow::show_status_error` or `CanvasWidget::report_status_error`, which add the red flash and warning icon. Informational confirmations, cancellations, no-op results, and readouts use `showMessage`. Status text reaches `currentMessage()` unchanged, so tests may assert exact strings.

## Standing UI rules

- The right dock stack's minimum width is measured, not hardcoded: `MainWindow::update_right_dock_minimum_width` derives it from the layers panel layout minimum (whose widest row is the localized blend/opacity row) plus the dock chrome measured from a laid-out dock. At that minimum the blend/opacity row fits exactly, so the Fill spin box's right edge lines up with the name-filter edit. Anything that widens a layers-panel row (new controls, longer prefixes) moves the minimum automatically; do not add a competing hardcoded width, and keep staged `setSidePanelWidth` values in tests comfortably above the measured minimum.
- The layer context menu always keeps **Edit Layer Styles...** as its first item; `ui_layer_context_menu_keeps_edit_styles_on_top` enforces this.
- Read modifier state folded from the current event, not `QApplication::keyboardModifiers()`. Live application state can lag event filtering, and offscreen tests retain synthetic modifier bits.
- Application-wide QSS and hotkey changes require both full release test suites under the handoff rules in `AGENTS.md`.
- Never fix a QToolBar's size along its perpendicular axis (the width of a vertical bar). The overflow extension button reveals hidden items by temporarily enlarging that axis into extra rows or columns, and a fixed size clamps the expanded geometry into a clipped sliver. Use a minimum size instead; the tool palette's 43 px minimum width is the reference (`ui_tool_palette_extension_button_expands_palette` pins the expansion).
- Canvas scroll bars are direct `CanvasWidget` children with load-bearing objectNames `canvasHorizontalScrollBar`/`canvasVerticalScrollBar` (theme QSS and UI tests find them by name). Their range mirrors the 10%-minimum-visible pan clamp (`pan_axis_range` in `canvas_widget_view.cpp`), so bar scrolling and hand-tool panning can never disagree, and the bars stay live even when the document fits the view. They are the one scroll bar styled dark on every platform; other Windows/Linux scroll bars stay native.
