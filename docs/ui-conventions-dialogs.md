# Dialog and field conventions

Split from [ui-conventions.md](ui-conventions.md): dialog chrome, closing, async-work guards, placement and prompts, the field controls that dialogs and the options bar share (scrub labels, spin boxes, unit fields, sliders, the `current_*` tool-state mirror), and settings pages.

## Dialog lifecycle

Frameless windows (the main window's custom chrome and every `install_dark_dialog_chrome` dialog) draw their only edge themselves: `frameless_window_edge_width()` is 1 px where the window server shadows the window (Windows DWM, macOS) and 2 px on X11, Wayland and wasm, which give a client-decorated window no shadow; `dialog_chrome_style` and `photoshop_style` read it, and the dialog chrome's root margins match it so the title bar never paints over the outline (`ui_dialog_chrome_outline_reads_at_the_dialog_edge`).

Dialogs that react to closing must funnel every path through `done()`. `reject()` hides a dialog without a QCloseEvent, so closeEvent-only cleanup misses the chrome X and Esc. Do not override `reject()` to call `close()`: `QDialog::closeEvent` calls `reject()` and treats a still-visible dialog as vetoing the close.

`patchy::ui::AppStyle` (`src/ui/app_style.cpp`, installed by main.cpp) wraps the platform style: slider and flyout hints, no icons on dialog buttons (Linux desktops add OK/Cancel glyphs drawn for their own scheme, GitHub issue 99), and Patchy's themed chevrons for toolbar overflow buttons. Never take chrome icons from the desktop icon theme. It also draws the separator rows of every combo box list (`QComboBox::insertSeparator`): one line in the theme's `menu_separator` between 6 px side margins, in the row Qt gives it (one frame width tall). Qt's list delegate would hand the row to the base style as a toolbar separator with a default option, so in that style's shape and the application palette's colors, which differ per platform and desktop setting ([visual-qa.md](visual-qa.md#base-style-and-palette-in-captures); GitHub issue 696 follow-up). A list that wants a roomier separator sets its own delegate (`BlendModeListDelegate`: the menus' 9 px row).

New non-modal dialogs must use `run_non_modal_dialog`. It rejects child dialogs when their parent finishes and applies the macOS above-parent native anchor. `request_patchy_color` permits one picker at a time, with its own remembered position; OK on its parent keeps the picked color (issue 216). Every choose-a-color prompt goes through it (palettes, color names, hex, live preview), never `QColorDialog`; it returns opaque RGB, so a value with alpha keeps it beside the picker (Preferences pairs grid and guide color buttons with opacity spin boxes).

A dialog function that arms async work must disarm it from a `qScopeGuard` declared after the dialog, not a statement after the nested loop: completion callbacks capture the frame by reference and are posted to `QCoreApplication`, so an exception unwinding the frame skips that statement and the completion runs against destroyed widgets. Guards live in `request_visual_filter_gallery`, `request_filter_settings`, and `run_raw_develop_dialog`; every `close_*` helper they call is null-safe and idempotent. `QObject::destroyed` is no substitute: `QWidget` deletes its children before `~QObject` emits it (`ui_filter_gallery_unwinding_call_disarms_in_flight_renders`). To leave a running `run_non_modal_dialog` loop by exception, catch at the slot boundary and pass `std::current_exception()` to `unwind_non_modal_dialog_loop` (`src/ui/dialog_utils.hpp`), which quits the innermost loop so `run_non_modal_dialog` rethrows on its own frame; throwing through the dispatcher is unsupported (macOS terminates inside CFRunLoop).

Use `exec_dialog` or `run_non_modal_dialog`, never `QDialog::exec`. `place_dialog` centers or restores positions. On wasm, shrink to the canvas and scroll content that cannot fit ([wasm.md](wasm.md)). Desktop tall dialogs cap themselves with `QScrollArea`, page `sizeHint`, and `availableGeometry().height() - 40` (Shape Appearance). Unnamed dialogs have no position memory. `QProgressDialog`, `QMessageBox`, and `mark_dialog_always_centered` dialogs (About, New Document) center on the owner and discard saved positions. Saved positions off the owner's screen also recenter.

A window a modal blocks shows the arrow, whatever sets its cursor (issue 504, `src/ui/blocked_window_cursor.cpp`, [wayland.md](wayland.md)). Qt drops the platform's enter for a blocked window and gets no later one while the pointer stays inside, so the same filter gives the window its first mouse move or press after unblock as an enter when the widget under the pointer never got one: after File > Open Recent File the pointer came back from the menu while the Opening dialog blocked the window, the clicked list row was deleted, and the new canvas kept the arrow through every move and drag (issue 634, `ui_blocked_window_cursor_returns_after_open_recent_file`).

Placement clamps the full frame to the screen's available area (no taskbars or docks), again after Show finalizes sizes and margins and on Resize, for unnamed and transient dialogs too. Automatic moves never enter position memory.

A dialog whose word-wrapped label changes text while it is open calls `keep_dialog_tall_enough_for_wrapped_text` (`src/ui/dialog_utils`): Qt takes a window's minimum height from the label's width-independent hint, so more wrapped lines squeeze the rows around it instead of growing the dialog (Remove Object's status, GitHub #612; Import PDF's size note). When such a label sits in a fixed column beside a stretch widget, also give it the horizontal size policy `Ignored` and the stretch widget a real minimum width: a wrapped QLabel hints up to 80 characters of width, and a box layout hands its stretch item the bare minimum once the row's hints no longer fit ([pdf-import.md](pdf-import.md)).

`show_warning_message` answers bare letters: Y/N (Yes/No, Save/Discard), S/D, C (Cancel). A Save + Discard box reads "&Yes", "&No", "&Cancel" (same roles, one width), so its text is a yes/no question; close prompt quotes the name, Yes default (issues 70, 240, 605; `ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys`). The default button has the accent outline.

Fields typing replaces (hex, ratios) call `select_all_on_focus`: select-all on focus-in, queued past the click. The color picker focuses its HTML field on show (Ctrl+V, Return pastes a hex) (GitHub issue 68; `ui_color_picker_opens_with_hex_field_selected_for_paste`).

Dialog spin boxes that retain their minus/plus buttons must append `dialog_spinbox_button_style()` from `src/ui/dialog_utils` to the dialog stylesheet after all children exist. Child dialogs inherit it; the color picker's buttonless fields reset its padding. Sub-control geometry rules (`QSpinBox::up-button { width; subcontrol-position }`) are silently ignored under a descendant-prefixed selector (`QDialog#name QSpinBox::up-button`): write them unprefixed in a stylesheet set on the dialog itself, and set it after the children exist (widgets on initially hidden tab pages miss an earlier one).

A slider row that wants one-unit steppers passes `step_buttons = true` to `add_dialog_slider_spin_row`. The pair comes from `add_spin_step_buttons` (`src/ui/dialog_utils.hpp`): `-` / `+` push buttons that auto-repeat and disable at the range ends, also used by the Blend If fields. `wrap_spin_with_step_buttons` makes a `[spin] - +` form row. The buttons are `<spin objectName>DecreaseButton` / `IncreaseButton`, accessible names `Decrease %1` / `Increase %1` plus the field label.

A font derived from another one goes through `scale_font_size`, `scaled_font`, or `offset_font` in `src/ui/dialog_utils.hpp`. Never write `font.setPointSizeF(font.pointSizeF() * k)`: on a pixel-defined font (macOS) the accessor returns -1, so it is a no-op plus a Qt warning. See the cross-platform rules in [platform.md](platform.md).

## Options bar

All options-bar controls share one fixed 26 px row height. QStyleSheetStyle inflates QToolButton size hints: cap options-bar buttons' min/max height. Free-transform and warp sessions own the row and hide per-tool widgets. Option labels (`optionLabel`) are plain text, no chip (issue 76), and show no trailing colon (issue 127): keep the shared "Name:" source strings (dialog forms want the colon) and set `kTranslationStripsTrailingColonProperty` on the label before binding, which drops an ASCII or fullwidth colon. Separators, grouping, bar height, slider collapse: [options-bar.md](options-bar.md).

A field's "Label:", the gap and the field are one scrub zone (issue 46): drags scrub; a field click edits, number selected; while editing, drags select. `exec_dialog`/`run_non_modal_dialog` run `install_scrub_labels_in` on every dialog, `build_options_bar` on the options bar; `FilterParameterPanel::rebuild`, the Properties panel (shape W/H, artboard X/Y/W/H) and the Brush Dynamics popup call it too. Rules, tests: `src/ui/dialog_utils.hpp`; one `editingFinished` per scrub. Layers panel prefixes use `install_prefix_scrub`. Options-bar numeric controls must use `configure_toolbar_spinbox`. Set range, decimals, prefix, and suffix first: the width is a minimum, grown to fit the widest value and chevron. Never duplicate the popup or use a plain spin box. Dialog and transform-session fields without the range popup use `configure_dialog_spinbox`.

A field with a length, percent, or degree suffix is a `UnitSpinBox` or `UnitIntSpinBox` (`src/ui/unit_spin_box.hpp`). The constructor takes the native `SpinUnit` and sets the suffix; `set_context_provider` supplies the document PPI and the percent basis (what 100% means for that field, 0 rejects percent). A typed unit token converts on entry while the display stays native; plain numbers keep the stock rules. Token table and conversion rules: [resolution-units.md](resolution-units.md).

When the typed range exceeds the useful drag range, set `kToolbarSpinboxSliderMaxProperty` (`dialog_utils.hpp`), not a narrower range: Text Size takes 10000 pt, its slider stops at 200.

Size-like sliders (brush, Quick Select and Liquify size, Feather, Line Weight, effect Size and Distance) use `SliderCurve::FineLowEnd` (`src/ui/curved_slider.hpp`): value grows with the position squared. Opt in with `bind_curved_slider`, `kToolbarSpinboxSliderCurvedProperty` (popups), or `add_dialog_slider_spin_row`'s `curve`. `QSlider::value()` is then a position: use `slider_value` and `set_slider_to_value`. Keep percentages, angles and color channels linear.

Application-wide tool state follows the `current_*` mirror pattern in MainWindow: update the member from the control signal, apply it both during new-session setup and `activate_document_tab`, and update the mirror in `load_tool_settings()` under `QSignalBlocker`. Updating only the current canvas desyncs new or reactivated documents.

## Settings pages

The Preferences dialog: [preferences-dialog.md](preferences-dialog.md).
Settings pages keep fields compact (a combo as wide as its longest entry, capped; its open list
fits every entry, `fit_combo_popup_to_entries`) and let option text wrap.
A dialog's choice list that is not sized by hand calls `fit_combo_to_entries` (`dialog_utils.hpp`,
issue 655): the box's minimum width fits its widest entry as its style and style sheet draw it,
and the open list fits too, which matters under Fusion (Linux), whose open list is a menu with
more padding than the box. Edit > Fill, New Layer's Fill and the generated GEGL and G'MIC
choice lists use it; `tests/ui/combo_fit_checks.hpp` checks a combo closed and open.
`install_wheel_scroll_guard` (`dialog_utils.hpp`) makes combo boxes, spin boxes and sliders
in scrolling pages take the wheel only with keyboard focus (issue 92); use it for any dialog
whose pages scroll. Angle fields get a round `AngleDial` (`angle_dial.hpp`, issue 618).
