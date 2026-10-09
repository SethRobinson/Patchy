# UI conventions

Read this before changing QActions, dialogs, the options bar, list-row widgets, status messages, application QSS, or other shared UI behavior.

Visible automation permits Preferences, About and AI Setup through the dialog
helpers. The activity guard permits inspection but explains conflicting actions.
An editable Pause restores ordinary manual dialog and save-prompt behavior. See
[automation-feedback.md](automation-feedback.md) for API lifetime and resume rules.

## Patchy-only options

An option Photoshop cannot represent (the file still opens cleanly there; Photoshop renders without it and drops it on resave) is marked two ways, both from `src/ui/dialog_utils.hpp`:

- **A badge on the control.** `make_patchy_only_badge(parent, explanation)` returns the small "Patchy" pill for the control's row; put the same explanation on the control's tooltip. `patchy_only_explanation("renders ...")` supplies the shared wording; only the Photoshop clause varies.
- **An info banner while the option is on.** Dialogs with a banner slot (the Layer Style dialog) show a `@info_banner_*` label with the same sentence, visible only while the option is active, so a colleague's file explains itself. Calmer than `@warning_banner_*`: nothing is broken.

No save-time prompt or Layers-panel marker. First user: the Drop Shadow's Continuous (long shadow) option (`ui_layer_style_continuous_shadow_controls_map_to_settings_and_mark_patchy_only`).

## Color scheme

Patchy ships Dark and Light, chosen in Preferences (`preferences/colorScheme`, values `system`/`dark`/`light`, default `system`). Switching applies live. `ThemeManager` in `src/ui/theme_manager.hpp` owns the preference, resolves "follow system" through `QStyleHints::colorScheme()`, and emits `color_scheme_changed`. It also mirrors the resolved scheme onto Qt with `QStyleHints::setColorScheme`; the native-chrome and offscreen consequences live in [platform.md](platform.md).

Standing rules:

- **No hex literals for chrome anywhere in `src/ui`.** Colors are named roles in `src/ui/theme_palette.hpp`. QSS writes them as `@role_name` tokens; painted widgets and delegates read `theme().role_name`. Adding a role means the member, a Dark value, and a row in the role table; `ui_theme_palettes_define_every_role` fails if you miss one.
- **Dark is authored, Light is derived then corrected.** `light_palette()` mirrors the lightness of every Dark role, lifts the mirrored neutrals into Light's surface band, and then overrides the ones where that is wrong (the canvas backdrop, bevels, brand and state colors, icon ink). A new role therefore needs no Light decision unless it deserves one.
- **The lift is what makes Light look lit.** Dark packs its surfaces into the bottom third of the value range, so a plain mirror leaves the app reading medium gray. `lift_to_surface_band()` raises that end only; ink mirrors below the first stop and passes through untouched. It is monotonic, so roles cannot swap order, but it compresses near white.
- **A mirror inverts an outline.** A line darker than its surface in Dark comes back lighter than it in Light, which on a near-white surface is no line at all. Those roles are corrected by hand in `light_palette()` (field wells, the tool column and dock outlines, the splitter groove); a line that is *lighter* than its surface in Dark is ink and mirrors correctly. When adding a role that draws a line, check which of the two it is.
- **A mirror also inverts lightness that carries meaning.** Where a role signals selection or control by sitting lighter than its neighbours, the flip keeps the value and drops the claim. Those roles are restated by hand in `light_palette()`: the selected tab rises above the strip and the unselected tabs drop below it, and the scroll-bar handle becomes the dark element in a light groove. A role whose lightness is only a surface level mirrors correctly; one whose lightness is a statement about state or elevation has to be re-decided for Light.
- **A tint on a surface is not a brand color.** `flip_for_light()` chooses between lifting a role into Light's surface band and pulling it to a mid tone by saturation alone, so a near-black wearing a cool cast trips the chromatic branch and comes back as a mid tone of that hue. The About dialog's body, its secondary button, and that button's hover are restated in `light_palette()` as the light surface band with the cast kept. A new tinted surface needs the same check; a role whose chroma carries brand or state meaning does not.
- **A widget that wants its own values gets its own roles.** Borrowed roles read as a coincidence that holds in one scheme; a named family states the relationship the corrections have to preserve. The color picker's tab bar has its own `picker_tab_*` family. `ui_light_scheme_color_picker_selected_tab_reads_as_selected` renders the bar in both schemes and pins it.
- **Never hand token text to `setStyleSheet`.** Qt drops the whole declaration containing an unresolved token, silently, so a rule just stops existing. Use `set_themed_style` / `append_themed_style` from `src/ui/theme_qss.hpp`; the shared builders return `ThemedQss` so the mistake is a compile error, and `ui_no_widget_ships_unresolved_theme_tokens` catches inline blobs that slip through. Rich-text labels with colored links use `set_themed_label_text`.
- **`append_themed_style` preserves order.** A widget's sheet is an accumulation (chrome, then the dialog's own block, then `dialog_spinbox_button_style()` last), and the stored template replays in the same order on a scheme change. A dialog that rebuilds its whole sheet reads the accumulated template back with `themed_style_template`, never `styleSheet()`.
- **A scheme change needs no `setIcon` call.** Icons are backed by engines in `src/ui/icon_theme.hpp` that resolve colors when painted, so existing `QIcon`s render correctly on their next repaint. Do not "fix" this by rebuilding icons.
- **Never bake a color into a `QIcon`.** Painting a glyph into a `QPixmap` and wrapping it freezes whichever scheme was active when the icon was built. One-off procedural marks go through `themed_glyph_icon` in `src/ui/action_icons.hpp`, which hands the glyph its ink at paint time. The same applies to a widget that paints its own chrome: read `theme()` inside `paintEvent`, never a literal.
- **If you cache a pixmap derived from `theme()`, drop it on a scheme change.** Qt sends no event for a palette-struct change. `MainWindow::apply_color_scheme` clears the layer, channel, and path thumbnail caches for this reason.
- **Checkable and disabled QPushButtons and disabled QCheckBoxes are styled globally.** The application sheet defines `QPushButton:checked`, `QPushButton:disabled`, and `QCheckBox:disabled` plus `::indicator:disabled` (Options bar too, checked glyph kept); without the checkbox rules a disabled box looks enabled and seems to refuse toggling. Never copy these states per dialog.
- **Some colors deliberately do not follow the scheme.** The transparency checkerboard depicts alpha and stays light in both themes. Marching ants, the clone-source marker, and tool cursors are black-plus-white pairs that read over arbitrary artwork; UI colors would vanish over matching pixels. Saved grid and guide colors are user data, as is the pasteboard color picked from the backdrop's right-click menu (presets in `CanvasWidget::add_backdrop_color_menu_entries`; unset, the `canvas_backdrop` role). The app icon and splash artwork composite against the OS, not Patchy's chrome. Adjustment-layer glyphs encode meaning through their own internal contrast (the Threshold glyph *is* a black/white split).
- **User-importable theme files** overlay a `base` scheme with role overrides from a `.patchytheme` JSON file, imported/exported in Preferences. Format and the `ThemeManager::set_custom_theme` apply path live in [theme-files.md](theme-files.md); read it before touching either.

Authoring icons: 32x32 viewBox, lowercase hex, drawn from the ten-color vocabulary in `icon_color_roles()` (`icon_ink` plus nine accents). `ui_icon_color_map_covers_every_authored_color` rejects anything outside it. The two paint-swatch icons and the seven referenced from QSS by `url()` are exempt and listed explicitly in `icon_theme.cpp`.

## Hotkeys

Application-level QAction shortcuts must be registered through `MainWindow::register_hotkey(action, "stable.id", default_seq)`, backed by `HotkeyRegistry` in `src/ui/hotkey_registry.hpp`. Never call `setShortcut` or `setShortcuts` directly on an app-level action. Command ids are persisted and must never be renamed. Two commands must not share a default shortcut; `ui_hotkey_defaults_have_no_conflicts` enforces this. Late registration goes only through `MainWindow::refresh_script_commands` ([scripting.md](scripting.md)). Tools that share a flyout therefore never carry a Shift+letter default of their own: the flyout's "Cycle <Group> Tools" command owns that key (see the tool palette section of [tools.md](tools.md)).

A canvas tool that needs a key owned by an application shortcut must accept the `ShortcutOverride` in `CanvasWidget::event()` while active. Magnetic Lasso Delete/Backspace is the reference pattern; see [selection-tools.md](selection-tools.md).

Modifier keys are named per platform two ways. The shortcut chip beside an action name comes from `QKeySequence::NativeText` in `refresh_action_tooltip`. A sentence naming a key to hold (status hint, tooltip detail, accessibility description) writes `%CTRL%` and `%ALT%` and resolves through `resolve_modifier_names` (`src/ui/modifier_names.hpp`): Ctrl/Alt on Windows and Linux, Command/Option on macOS. Patchy does not set `AA_MacDontSwapCtrlAndMeta`, so `Qt::ControlModifier` is Command on a Mac. Shift, Delete, and the mouse verbs take no token. Never write a bare "Ctrl" or "Alt" into a user-facing sentence. Not `%1`-style: the tool-hint sources in `main_window_actions_tool_palette.cpp` are translated later and `QString::arg` consumes only `%` plus a digit. Translations keep the tokens; `ui_path_tools_show_activation_hints_and_tooltips` builds the expected sentence through the same helper.

## Dialog lifecycle

Dialogs that react to closing must funnel every path through `done()`. `reject()` hides a dialog without a QCloseEvent, so closeEvent-only cleanup misses the chrome X and Esc. Do not override `reject()` to call `close()`: `QDialog::closeEvent` calls `reject()` and treats a still-visible dialog as vetoing the close.

New non-modal dialogs must use `run_non_modal_dialog`. It rejects child dialogs when their parent finishes and applies the macOS above-parent native anchor. `request_patchy_color` permits one picker at a time; transient pickers retain their own position-memory group. Every choose-a-color prompt in the app goes through it (palettes, color names, hex, live preview), never `QColorDialog`; it returns opaque RGB, so a value that carries alpha keeps it beside the picker (Preferences pairs grid and guide color buttons with opacity spin boxes).

A dialog function that arms async work must disarm it from a `qScopeGuard` declared after the dialog, not a statement after the nested loop: completion callbacks capture the frame by reference and are posted to `QCoreApplication`, so an exception unwinding the frame skips that statement and the completion runs against destroyed widgets. Guards live in `request_visual_filter_gallery`, `request_filter_settings`, and `run_raw_develop_dialog`; every `close_*` helper they call is null-safe and idempotent. `QObject::destroyed` is no substitute: `QWidget` deletes its children before `~QObject` emits it (`ui_filter_gallery_unwinding_call_disarms_in_flight_renders`). To leave a running `run_non_modal_dialog` loop by exception, catch at the slot boundary and pass `std::current_exception()` to `unwind_non_modal_dialog_loop` (`src/ui/dialog_utils.hpp`), which quits the innermost loop so `run_non_modal_dialog` rethrows on its own frame; throwing through the dispatcher is unsupported (macOS terminates inside CFRunLoop).

Use `exec_dialog` or `run_non_modal_dialog`, never `QDialog::exec`. `place_dialog` centers or restores positions. On wasm, shrink to the canvas; scroll content if its layout minimum cannot fit ([wasm.md](wasm.md)). Desktop tall dialogs cap themselves with `QScrollArea`, page `sizeHint`, and `availableGeometry().height() - 40` (Shape Appearance). Unnamed dialogs have no position memory. `QProgressDialog`, `QMessageBox`, and `mark_dialog_always_centered` dialogs (About) center on the owner and discard saved positions. Saved positions off the owner's screen also recenter.

Placement clamps the full frame to the screen's available area, excluding taskbars and docks. The position filter repeats it after Show finalizes sizes and native margins, and on Resize (expanded details). Unnamed and transient dialogs are covered. Automatic moves never enter position memory.

`show_warning_message` answers bare letter keys: Y/N for Yes/No, S/D for Save/Discard (Y/N also work), and labels Discard "Don't Save"; the default button carries the accent outline (`QPushButton:default`, follows focus). The close-document prompt offers Save / Don't Save / Cancel, Save default (GitHub issue 70; `ui_save_prompt_uses_save_dont_save_cancel_with_letter_hotkeys`).

Fields typing replaces (hex, ratios) call `select_all_on_focus`: select-all on focus-in, queued past the click. The color picker focuses its HTML field on show (Ctrl+V, Return pastes a hex) (GitHub issue 68; `ui_color_picker_opens_with_hex_field_selected_for_paste`).

Dialog spin boxes that retain their minus/plus buttons must append `dialog_spinbox_button_style()` from `src/ui/dialog_utils` to the dialog stylesheet after all children exist.

A slider row that wants one-unit steppers passes `step_buttons = true` to `add_dialog_slider_spin_row` (every Layer Style effect slider does). The pair comes from `add_spin_step_buttons` (`src/ui/dialog_utils.hpp`): compact 22 px `-` / `+` push buttons that auto-repeat and disable at the range ends, also used by the Blend If fields. `wrap_spin_with_step_buttons` makes a `[spin] - +` form row. The buttons are named `<spin objectName>DecreaseButton` and `<spin objectName>IncreaseButton`, with the shared `Decrease %1` / `Increase %1` accessible names plus the field label.

A font derived from another one (a smaller detail line, a larger headline) goes through `scale_font_size`, `scaled_font`, or `offset_font` in `src/ui/dialog_utils.hpp`. Never write `font.setPointSizeF(font.pointSizeF() * k)` directly: the size lives in points or pixels and the unused accessor returns -1, so that line is a no-op plus a Qt warning wherever the inherited font is pixel-defined (macOS). See the cross-platform rules in [platform.md](platform.md).

## Item-widget rows and selection

Rows installed with `QListWidget::setItemWidget` must paint their own selection because an opaque row widget hides `::item:selected` QSS. Give the list `padding: 0` on `::item`, make the row's child containers transparent, and keep the global `QCheckBox { border: none; }` rule for correct macOS layout.

Every plain QWidget container within the row must have a transparent background in application QSS. With a global QWidget background rule, QStyleSheetStyle applies `WA_StyledBackground`, so `setAutoFillBackground(false)` alone does not prevent the container painting over its row.

Everything specific to the Layers panel (row styling, thumbnails, rebuild order and absent rows, click selection including the Move tool's canvas-side Ctrl+click toggle, the folder disclosure arrow, double-click editors, and the visibility eye) lives in [layer-panel.md](layer-panel.md); read it before changing layer rows.

`bind_widget_text` on a QLineEdit binds the placeholder text, not `text()`, since line edits carry user data in `text()`.

## Options bar

All options-bar controls share one fixed 26 px row height. QStyleSheetStyle inflates QToolButton size hints, so options-bar buttons need explicit min/max height caps. Free-transform and warp sessions own the row and hide per-tool widgets. Option labels (`optionLabel`) are plain text, no chip (issue 76).

Every "Label:" before a numeric field is its scrub handle (issue 46): `exec_dialog`/`run_non_modal_dialog` run `install_scrub_labels_in` on every dialog, `build_options_bar` on the options bar; `FilterParameterPanel::rebuild`, the Properties W/H row and the Brush Dynamics popup call it too. Pairing rules and tests: `src/ui/dialog_utils.hpp` (`ui_dialog_scrub_labels_pair_every_row_shape`). One singleStep per pixel, ten with Shift, `editingFinished` on release; the Layers panel prefixes use `install_prefix_scrub`. Options-bar numeric controls must use `configure_toolbar_spinbox`. Set range, decimals, prefix, and suffix first: its width is a minimum that grows for the widest value plus the chevron. Never duplicate the popup or substitute a plain spin box. Dialog and transform-session fields use `configure_dialog_spinbox` when the range popup is inappropriate.

A field with a length, percent, or degree suffix is a `UnitSpinBox` or `UnitIntSpinBox` (`src/ui/unit_spin_box.hpp`). The constructor takes the native `SpinUnit` and sets the suffix; `set_context_provider` supplies the document PPI and the percent basis (what 100% means for that field, 0 rejects percent). A typed unit token converts on entry while the display stays native; plain numbers keep the stock rules. Token table and conversion rules: [resolution-units.md](resolution-units.md).

When the typed range is wider than the useful drag range, set `kToolbarSpinboxSliderMaxProperty` from `dialog_utils.hpp`; do not narrow the spin-box range. Text Size accepts 10000 pt while its slider stops at 200 unless the value is higher.

Size-like sliders (brush, Quick Select and Liquify size, Feather, Line Weight, effect Size and Distance) use `SliderCurve::FineLowEnd` (`src/ui/curved_slider.hpp`): value grows with the position squared. Opt in with `bind_curved_slider`, `kToolbarSpinboxSliderCurvedProperty` (popups), or `add_dialog_slider_spin_row`'s `curve`. `QSlider::value()` is then a position: use `slider_value` and `set_slider_to_value`. Keep percentages, angles and color channels linear.

Application-wide tool state follows the `current_*` mirror pattern in MainWindow: update the member from the control signal, apply it both during new-session setup and `activate_document_tab`, and update the mirror in `load_tool_settings()` under `QSignalBlocker`. Updating only the current canvas desynchronizes newly created or reactivated documents.

## Status bar

QStatusBar hides non-permanent widgets whenever a message is displayed. Patchy's left-side status widgets are manually positioned children of `ZoomStatusBar`; never add them with `addWidget()`.

Blocking refusals and failed operations use `MainWindow::show_status_error` or `CanvasWidget::report_status_error`, which add the red flash and warning icon. Informational confirmations, cancellations, no-op results, and readouts use `showMessage`. Status text reaches `currentMessage()` unchanged, so tests may assert exact strings.

## Standing UI rules

- The right dock stack's minimum width is measured, not hardcoded: `MainWindow::update_right_dock_minimum_width` derives it from the layers panel layout minimum (whose widest row is the localized blend/opacity row) plus the dock chrome measured from a laid-out dock. At that minimum the blend/opacity row fits exactly. Anything that widens a layers-panel row (new controls, longer prefixes) moves the minimum automatically; do not add a competing hardcoded width, and keep staged `setSidePanelWidth` values in tests comfortably above the measured minimum.
- Right-dock panel heights must never pin the window taller. A collapsed dock pins min == max to its title bar's styled sizeHint (`refresh_collapsed_right_dock_heights` re-pins after the stylesheet lands). Expanding sets a temporary minimum that `MainWindow::handle_right_dock_panel_toggled` releases one event-loop hop later, re-clamping the window to the screen only if the toggle grew it. Panels that can outgrow their share scroll internally; never give a right-dock panel an unbounded minimum height (`ui_right_dock_panels_expand_within_window_height`, `ui_collapsed_right_docks_have_uniform_title_height`). `create_docks` sets `QMainWindow::GroupedDragging` (`ui_tabbed_right_dock_drags_out_by_tab`), but `handle_right_dock_title_drag_event` detaches only the dock under the cursor from a tabbed right dock (`ui_tabbed_dock_title_drag_floats_single_dock`): Qt's floating group window breaks with custom title bars. A floating group or single floating right dock (`floatingChrome` property) gets chrome from `handle_dock_group_window_event`: a divider-chrome strip with edge-resize cursors and blank areas that drag the window (`ui_dock_group_window_drags_by_blank_chrome`). Every docked dock hosts the 7px `rightDockResizeHandle` over its left edge (`install_right_dock_width_handle`) with a matching inset on title bars and contents (`ui_right_dock_contents_clear_width_handle`). Floating docks skip the width pin and the handle, get a size nudge on toggle (`resync_native_frame_geometry`), and are always expanded: pulling a panel out auto-expands it and the collapse toggle shows only while docked (`ui_floating_right_dock_auto_expands`).
- The layer context menu always keeps **Edit Layer Styles...** as its first item; `ui_layer_context_menu_keeps_edit_styles_on_top` enforces this.
- The canvas right mouse button opens context menus and never pans; the builder and the rule live in [tools.md](tools.md), "Canvas right-click menu".
- The menu-bar Layer menu groups its new-layer, layer-mask, and move/flip sets into the `layerNewMenu`, `layerMaskMenu`, and `layerArrangeMenu` submenus so the whole menu fits a short browser viewport in the wasm build; `ui_main_window_renders_color_controls` bounds its direct row count at 23, so add new Layer commands inside an existing submenu or group rather than as top-level rows. The Layer Mask submenu mirrors the layer context menu's item order and separators; keep the two in step.
- New File import or export commands go inside the `fileImportMenu` / `fileExportMenu` submenus, whose title carries the verb so no row repeats it; `ui_file_export_menu_actions_registered` pins this.
- The Open Recent File menu leads with a filter row (`fileOpenRecentFilterEdit`, the codebase's only QWidgetAction). `rebuild_recent_files_menu` recreates the row on every rebuild (filter state resets); `aboutToShow` focuses the edit, and the MainWindow event filter forwards Up/Down and defers Enter through `QTimer::singleShot(0)` (a synchronous trigger rebuilds the menu and deletes the edit mid-delivery). Absent under a native menu bar ([platform.md](platform.md)). Tests find file entries by non-empty `data()`, never by position. The start panel's `startPanelRecentList` shows the same 200 entries in a box fixed at 8 rows with the same `MainWindow::show_recent_path_context_menu`; its `startPanelRecentFilterEdit` uses the menu's matching rule (all whitespace-separated tokens anywhere in the native path), Enter opens the top row, and `StartPanel::showEvent` clears it and takes focus. A filter matching nothing leaves one placeholder row whose `kRecentPathRole` path is empty, which the delegate, click and context-menu handlers test for; never give a real row an empty path.
- A QWidget subclass that paints its own content inside a themed container needs `background: transparent` in that container's sheet. The global `QWidget` rule sets `WA_StyledBackground` on it, so the whole slot fills with `window_bg` and a window-colored rectangle appears wherever the widget's own painting leaves a margin. The About dialog's artwork (`splashArtwork`) is the reference case; `ui_light_scheme_about_dialog_reads_as_a_light_surface` pins it.
- Read modifier state folded from the current event, not `QApplication::keyboardModifiers()`. Live application state can lag event filtering, and offscreen tests retain synthetic modifier bits.
- Application-wide QSS and hotkey changes require both full release test suites under the handoff rules in `AGENTS.md`.
- Interface scale is one global `QT_SCALE_FACTOR` on desktop, set by `apply_gui_scale_factor` in `src/app/main.cpp` before `QApplication` exists. On wasm the shell page applies it (a CSS-transformed iframe, see [wasm.md](wasm.md): the wasm plugin's pointer path ignores the Qt scale factor). The steps (67% through 200%), the platform default, and the helpers live in `src/ui/app_settings.hpp`; the shell page duplicates the step list and the 75% web default, so a change must touch both. A change applies on the next start (a page reload in the browser); the key `preferences/guiScalePercent` is a compatibility contract.
- Never fix a QToolBar's size along its perpendicular axis (the width of a vertical bar). The overflow extension button reveals hidden items by temporarily enlarging that axis into extra rows or columns, and a fixed size clamps the expanded geometry into a clipped sliver. Use a minimum size instead; the tool palette's 43 px minimum width is the reference (`ui_tool_palette_extension_button_expands_palette` pins the expansion). `ToolPaletteExpansionReservation` raises the minimum to the measured expanded width while the extension button is checked (so the layout reserves the columns instead of overlaying the canvas) and restores 43 on collapse.
- Chrome buttons the style positions itself are exempt from the global QToolButton minimums. QTabBar's overflow scroll arrows take their geometry from the fixed scroll-button metric, so the `QTabBar QToolButton` rule zeroes the global 26 px minimums back out (they clipped the right arrow) and `QTabBar::scroller` pins the two-button area to 32 px on every platform. The base styles draw the glyph differently: Windows/Linux paint a fixed-size arrow (padding must stay 0 or it collapses toward a dot), macOS scales it edge-to-edge (a mac-only 2 px padding gives it margins). `suppress_native_tab_bar_base` runs on every platform: the native base still showed as a 1px white line in the scroller area once tabs overflowed. `ui_document_tab_scroll_arrows_render_inside_the_bar` pins all of this.
- Canvas scroll bars are direct `CanvasWidget` children with load-bearing objectNames `canvasHorizontalScrollBar`/`canvasVerticalScrollBar` (theme QSS and UI tests find them by name). Their range mirrors the 10%-minimum-visible pan clamp (`pan_axis_range` in `canvas_widget_view.cpp`), so bar scrolling and hand-tool panning can never disagree, and the bars stay live even when the document fits the view.
- Every scroll bar is styled by `photoshop_style()` on every platform; none can stay native (once QSS touches a scroll bar `QStyleSheetStyle` owns the whole control, and one without subcontrol rules fills its groove with the window background). Canvas bars follow `canvas_backdrop`, not the window surface, and carry the opaque `scroll-dither` texture authored around `canvas_backdrop`/`canvas_scrollbar_track` so gutter, corner square and pasteboard read as one surface (moving that role means re-authoring `icons/light/scroll-dither.svg`). One handle fill (`scrollbar_handle_bg`) serves both families and must clear the darker track (`ui_light_scheme_panel_scroll_bar_handle_reads_against_track`). The `margin: 0` on the `QScrollBar` widget rules is load-bearing: without box properties on the widget rule the native style reserves room for the removed arrow buttons, a short bar's groove ends up smaller than the handle and every drag snaps to the minimum; a `::groove` rule never affects geometry (`ui_short_panel_scroll_bar_drags_by_handle`).

Preferences tabs, in order: Application, Tools, Pen, Units & Grids, Snapping, Hotkeys, and
Plug-ins (Windows only). Tool and canvas-input behavior (the mouse wheel, Free Transform
options) goes on Tools; Pen holds only pen and tablet settings; Application holds app-level
settings (language, theme, updates, recovery, open/save policies). Tests find Hotkeys by
title and pin the rest of the order in `ui_canvas_aid_preferences_and_guide_dialogs_work`.

Application-wide fill, quick-selection, interpolation, and vector options transfer
when the active canvas changes. Language changes in Preferences apply only on OK.
The color panel accepts named colors and CSS `#RRGGBBAA` (plus `#RGBA`); its stored
foreground/background colors remain opaque RGB. History navigation is refused
during a live pointer gesture, and focus loss clears the drag latches.

Recent files and folders include successful interactive, scripted, headless and
MCP document opens and saves, including saved flat copies. Saving records the
containing folder too. `recent_history_settings()` shares the persistent history
with owned MCP workspaces while their other preferences stay temporary. Native
updates lock the complete read/modify/write transaction and merge against the
latest stored list, so another process's additions or Clear command survive.
The File menu and Save As refresh before opening; the empty start panel refreshes
every two seconds while no popup is active and when it reappears. A refresh only
rereads settings and never stats an entry (a cold, spun-down or unreachable volume
would stall startup and every File menu open). Existence checks run on a tracked
worker (`schedule_recent_history_check`), right after startup, whenever a stored
list changes, and otherwise at most every 30 seconds. Entries show until a check
finds them missing, and a result that arrives under an open popup waits for the
next refresh. Network paths (UNC, and `DRIVE_REMOTE` drive letters on Windows) are
never checked, and missing entries stay stored so an unplugged drive's entries
return. A click on a vanished entry reports "Recent file is missing" and removes it.
Rebuilds occur only when the displayed list changes, preserving a live start-panel
filter.
