# UI conventions

Read this before changing QActions, dialogs, the options bar, list-row widgets, status messages, application QSS, or other shared UI behavior.

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

- The layer context menu always keeps **Edit Layer Styles...** as its first item; `ui_layer_context_menu_keeps_edit_styles_on_top` enforces this.
- Read modifier state folded from the current event, not `QApplication::keyboardModifiers()`. Live application state can lag event filtering, and offscreen tests retain synthetic modifier bits.
- Application-wide QSS and hotkey changes require both full release test suites under the handoff rules in `AGENTS.md`.
