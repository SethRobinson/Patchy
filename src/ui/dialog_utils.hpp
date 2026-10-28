#pragma once

#include "ui/curved_slider.hpp"
#include "ui/theme_qss.hpp"
#include "ui/unit_spin_box.hpp"

#include <QCheckBox>
#include <QColor>
#include <QPoint>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QMessageBox>
#include <QRadioButton>
#include <QSizeGrip>
#include <QStyleOptionButton>

#include <exception>
#include <initializer_list>
#include <limits>
#include <optional>

class QAction;
class QBoxLayout;
class QComboBox;
class QDialog;
class QIcon;
class QAbstractSpinBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
class QWidget;

namespace patchy::ui {

// Derived font sizes. A QFont carries its size in EITHER points OR pixels, and
// the unused accessor returns -1; macOS resolves inherited widget fonts by pixel
// size, so pointSizeF() is -1 there. That makes the obvious
// `f.setPointSizeF(f.pointSizeF() * 0.85)` ask for -0.85, which Qt refuses with
// "QFont::setPointSizeF: Point size <= 0" while leaving the font untouched, so
// text meant to be smaller renders at full size. Clamping the result with
// std::max(7.0, ...) only silences the warning; the size is then pinned to the
// floor instead of scaled, which is the same bug without the diagnostic. These
// helpers adjust whichever unit the font actually carries and leave a font that
// declares neither alone.
void scale_font_size(QFont& font, double scale);
[[nodiscard]] QFont scaled_font(QFont font, double scale);
// Additive sibling: shifts the size by `size_delta` points or pixels, and sets bold.
[[nodiscard]] QFont offset_font(QFont font, int size_delta, bool bold);

// Selects the whole text whenever the edit gains focus, so typing replaces the
// old value instead of appending to it (GitHub issues 66 and 68). The select
// runs queued, after the click that gave focus has placed its caret; a drag
// that follows the click still selects its own range.
void select_all_on_focus(QLineEdit& edit);
// Same for a spin box, which takes the focus itself and forwards the event to
// its line edit directly (a filter on the line edit never sees it).
void select_all_on_focus(QAbstractSpinBox& spin);

// `width` is a minimum: the box grows to keep its widest possible value text
// (prefix + min/max + suffix) clear of the trailing popup chevron. Set the
// range, decimals, prefix, and suffix BEFORE calling this.
void configure_toolbar_spinbox(QSpinBox* spin, int width);
void configure_toolbar_spinbox(QDoubleSpinBox* spin, int width);
// Set this double property on a toolbar spin box to cap its popup SLIDER below the
// spin box's maximum (the spin box itself keeps accepting larger typed values; the
// slider extends to the current value when it already sits above the cap).
inline constexpr char kToolbarSpinboxSliderMaxProperty[] = "patchy.popupSliderMax";
// Set this double property on a toolbar QDoubleSpinBox (before configure_toolbar_spinbox)
// to size it for that value's text instead of the min/max, for a field whose display
// drops trailing zeros (UnitSpinBox::set_trim_trailing_zeros) so the max text is not
// its widest realistic value.
inline constexpr char kToolbarSpinboxWidthSampleProperty[] = "patchy.toolbarSpinboxWidthSample";
// Set this bool property on an integer toolbar spin box to give its popup slider the
// SliderCurve::FineLowEnd response (curved_slider.hpp) for size-like ranges.
inline constexpr char kToolbarSpinboxSliderCurvedProperty[] = "patchy.popupSliderCurved";
// Scrub handles (GitHub issue 46; docs/ui-conventions.md). A label,
// the gap between it and its field, and the field itself form one scrub zone:
// - A horizontal drag past the drag distance changes `spin`'s value by one singleStep
//   per pixel, ten with Shift, a tenth with Alt (integer fields then move every ten
//   pixels); changing a modifier mid-drag continues from the current value. The
//   pointer wraps at the screen edge where the platform allows it.
// - A scrub ends with the spin box's editingFinished, so undo paths that coalesce an
//   edit session record one step per scrub.
// - The SizeHor cursor shows wherever a press scrubs.
// - A press on the label or gap without a drag changes nothing. A press on the field
//   released without a drag enters text editing with the number selected. While the
//   field is being edited (after that click, Tab, or typing; until Enter, Esc or focus
//   loss) it is an ordinary text field: drags select text, clicks place the caret, the
//   I-beam shows. A scrub that started on an unfocused field gives focus back.
// `install_prefix_scrub` gives a prefixed spin box with no label (Layers panel
// Opacity/Fill) the field handle and makes its prefix text a label-like handle whose
// click also enters editing. `install_scrub_labels_in` walks a
// container's layout (nested layouts, child containers, QScrollArea contents and
// QTabWidget pages included) and pairs every QLabel with letters in its text with the
// spin box it names: the label's buddy when that is a spin box, else the next item in
// layout order, looking past one QSlider; a spin box pairs directly, and a sub-layout
// or container widget pairs when its first control (sliders, spacers and unlettered
// labels passed over) is a spin box: a form row's "[slider] [spin]" or "[spin] - +"
// field pairs, a "[color button] [spin]" field does not, and a container whose own
// "Width" label comes first keeps its spins for that label. exec_dialog and run_non_modal_dialog call
// it on every dialog, and build_options_bar on the options bar, so a new label+field
// pair opts in by itself; surfaces built after their window is shown (the Filter
// Gallery's parameter panel) call it again. A spin box that received a handle carries
// the bool property kScrubHandleInstalledProperty, so repeated installs are no-ops. A
// label that must not become a handle (the "to" between a range's two fields) sets
// kScrubLabelExemptProperty. Tests: ui_dialog_scrub_labels_pair_every_row_shape,
// ui_options_bar_label_scrub_changes_spin_value, ui_options_bar_field_drag_scrubs_and_click_edits,
// ui_scrub_gap_and_modifiers_follow_the_zone_rules, ui_layer_opacity_prefix_scrub_is_one_undo_entry.
inline constexpr char kScrubHandleInstalledProperty[] = "patchy.scrubHandleInstalled";
inline constexpr char kScrubLabelExemptProperty[] = "patchy.scrubLabelExempt";
// True on a spin box while a scrub drags its value: set once the drag passes the drag
// distance, cleared just before the scrub's editingFinished. A field whose edits are
// expensive previews those values and commits once at editingFinished (the artboard
// X/Y/W/H fields); typed values and arrow steps arrive with it false.
inline constexpr char kScrubActiveProperty[] = "patchy.scrubActive";
[[nodiscard]] bool spin_box_scrub_active(const QAbstractSpinBox* spin);
void install_scrub_label(QLabel* label, QAbstractSpinBox* spin);
void install_prefix_scrub(QSpinBox* spin);
void install_scrub_labels_in(QWidget* container);
void configure_dialog_spinbox(QSpinBox* spin, int width = 92);
void configure_dialog_spinbox(QDoubleSpinBox* spin, int width = 92);
// Large-button spin box styling (24px - / + buttons with readable glyphs; decrement left,
// increment far right). Append to a dialog's stylesheet AFTER all child widgets exist, and keep
// the selectors unprefixed: Qt ignores ::up-button/::down-button geometry under a descendant
// prefix, and applies sub-control rules unreliably to children created after the stylesheet.
[[nodiscard]] ThemedQss dialog_spinbox_button_style();
void configure_compact_symbol_button(QPushButton* button);
// A small "Patchy" pill placed beside a control whose setting Photoshop cannot
// represent (the Patchy-only marker convention in docs/ui-conventions.md).
// `explanation` becomes the badge's tooltip; callers put the same text on the
// control itself so hovering either one explains what Photoshop will do.
[[nodiscard]] QLabel* make_patchy_only_badge(QWidget* parent, const QString& explanation);
// The shared wording for Patchy-only options that Photoshop ignores safely
// (the file opens without warnings; the setting is dropped on a Photoshop
// resave). `photoshop_behavior` completes the sentence "Photoshop ...".
[[nodiscard]] QString patchy_only_explanation(const QString& photoshop_behavior);
struct SpinStepButtons {
  QPushButton* decrease{nullptr};
  QPushButton* increase{nullptr};

  // Disables whichever button cannot move the value any further.
  void sync(const QSpinBox& spin) const;
  void sync(const QDoubleSpinBox& spin) const;
};
// Appends compact - / + push buttons after `spin` in `layout`. They step the spin
// box, auto-repeat while held, and disable at the range ends. Object names are
// <spin objectName>DecreaseButton / <spin objectName>IncreaseButton so UI tests can
// find them. `field_name` is the field's label ("Size"; a trailing colon is dropped)
// and feeds the "Decrease %1" / "Increase %1" accessible names and tooltips.
SpinStepButtons add_spin_step_buttons(QSpinBox* spin, QBoxLayout* layout, const QString& field_name);
SpinStepButtons add_spin_step_buttons(QDoubleSpinBox* spin, QBoxLayout* layout,
                                      const QString& field_name);
// Wraps a QSpinBox/QDoubleSpinBox in a tight "[spin] - +" row widget (for
// QFormLayout::addRow(label, row)); the buttons come from add_spin_step_buttons.
// Hide or disable the ROW, not the spin, so the buttons follow.
QWidget* wrap_spin_with_step_buttons(QAbstractSpinBox* spin, QWidget* parent,
                                     const QString& field_name);
// Adds a "label: [slider ------] [spin]" form row whose slider and spin box mirror
// each other. Object names are passed explicitly (never derived here): UI tests look
// these widgets up by exact objectName, so each call site keeps its own naming
// scheme. row_spacing < 0 keeps the layout's default spacing. step_buttons appends
// the add_spin_step_buttons pair after the spin box for one-unit adjustments.
// A slider_maximum below `maximum` stops the slider short of the spin box: the
// slider covers the practical range while the spin box still accepts `maximum`
// (a typed value past the slider parks the slider at its end). SliderCurve::FineLowEnd
// suits size-like ranges; find the slider's value with slider_value(), never value().
QSpinBox* add_dialog_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                     const QString& slider_object_name, const QString& spin_object_name,
                                     int minimum, int maximum, int value, const QString& suffix = QString(),
                                     int spin_width = 72, int row_spacing = -1, bool step_buttons = false,
                                     int slider_maximum = std::numeric_limits<int>::max(),
                                     SliderCurve curve = SliderCurve::Linear);
// Same row with a unit-entry spin box: the suffix comes from the native unit and typed
// unit tokens convert on entry (px/in/cm/mm/pt/%/deg; see unit_spin_box.hpp). `provider`
// supplies the PPI and percent basis; leave it empty for a plain 300 ppi, no-percent field.
UnitIntSpinBox* add_dialog_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                           const QString& slider_object_name, const QString& spin_object_name,
                                           int minimum, int maximum, int value, SpinUnit unit,
                                           UnitIntSpinBox::ContextProvider provider = {}, int spin_width = 72,
                                           int row_spacing = -1, bool step_buttons = false,
                                           SliderCurve curve = SliderCurve::Linear);
// Gives the label column of every form the width of the widest label among them, so the
// fields of forms stacked in separate group boxes start at one x (the Develop Raw and
// Camera Raw Filter control columns, GitHub issue 392).
void align_form_label_columns(std::initializer_list<QFormLayout*> forms);
// Moves a popup (already resized to its final size) directly below `anchor`:
// clamps it inside the screen's available horizontal range and flips it above
// the anchor when it would run past the bottom. Call before show().
void position_popup_below(const QWidget& anchor, QWidget& popup);
// Gives the window under the pointer the cursor of the widget there, the way Qt sets it when
// the pointer enters that widget. A widget that changes its cursor while the pointer is in
// another window (a popup) has Qt apply that cursor to its whole window, which then shows it
// over every widget until the pointer crosses into another one (GitHub issue 293: the brush
// cursor over the options bar after a quick value in a slider popup). MainWindow calls this
// once a popup closes. Test: ui_slider_popup_close_restores_cursor_under_pointer.
void refresh_cursor_under_pointer();
// Pattern and gradient pickers can list hundreds of entries (a document's embedded patterns
// ahead of the library). Fusion (the Linux desktop style) and the macOS style show a
// non-editable combo box's list as a menu as tall as every row, placed so the current row
// covers the box; on Wayland Qt cannot see where the window is, so that menu ran off the top
// of the screen and its first rows could not be reached (GitHub issue 271). This gives `combo`
// the scrolling list form on every style: at most kLongListPopupRows rows, opened below the
// box (above it near the screen's bottom). Call it on every long list picker. The open list
// always stays inside the screen's available area (fit_combo_popup_to_screen), scrolling when
// the rows do not fit; blend mode lists pass a larger `max_rows` (GitHub issue 414).
// Tests: ui_shape_pattern_list_popup_fits_the_screen, ui_blend_mode_lists_fit_a_small_screen.
inline constexpr int kLongListPopupRows = 14;
void use_scrolling_list_popup(QComboBox& combo, int max_rows = kLongListPopupRows);
// Moves and shortens `combo`'s open list window `popup` so it sits inside the available
// geometry of the combo's screen: below the box, else above it, else on the taller side.
void fit_combo_popup_to_screen(const QComboBox& combo, QWidget& popup);
// Widens `combo`'s open list, each time it opens, to its widest entry (combo_list_entry_width,
// plus the list's frame and scroll bar), never narrower than the box. Qt opens the list as wide
// as the box, and the list's row padding then elided entries the box shows whole (GitHub issue
// 489). Measured from the entries at that moment, so a language change or new entries need no
// call. Preferences applies it to every combo. Test: ui_preferences_combo_lists_fit_their_entries.
void fit_combo_popup_to_entries(QComboBox& combo);
// The width `combo`'s open list needs for entry `row` to show whole, as its delegate and the
// style draw it (the menu-style rows of Fusion and macOS, the inset text of Windows 11's list
// rows); 0 for a separator.
[[nodiscard]] int combo_list_entry_width(const QComboBox& combo, int row);
// For a dialog whose pages scroll (GitHub issue 92): a combo box, spin box or slider inside
// a scroll area under `root` ignores the mouse wheel unless it holds keyboard focus (a click
// or Tab; their focus policy drops from WheelFocus to StrongFocus), so a wheel that passes
// over one scrolls the page instead of changing a setting. Covers controls created later.
// Call once the dialog is built; repeated calls are no-ops. Preferences is the one user so
// far. Test: ui_preferences_wheel_over_unfocused_control_scrolls_the_page.
void install_wheel_scroll_guard(QWidget& root);
// QSizeGrip paints through the platform style, which is close to invisible on the dark QSS
// theme; repaint it as three light diagonal strokes so the resize corner is discoverable.
// The resize handle for frameless windows (chrome dialogs, popups), which have no native border.
class VisibleSizeGrip : public QSizeGrip {
public:
  explicit VisibleSizeGrip(QWidget* parent);

protected:
  void paintEvent(QPaintEvent* event) override;
};
// A check box or radio button whose text wraps onto more lines when its row is narrower
// than the text (Qt's never wrap, so one long option sets the whole dialog's minimum width;
// GitHub issue 451). The indicator sits beside the first line, the height follows the width
// the layout gives it, a click on the indicator or the text toggles it, and the minimum
// width is the indicator plus kWrappingToggleMinimumTextWidth (less for a shorter text).
// Preferences uses them for every option whose text could outgrow a narrow page.
inline constexpr int kWrappingToggleMinimumTextWidth = 220;
class WrappingCheckBox : public QCheckBox {
public:
  explicit WrappingCheckBox(const QString& text, QWidget* parent = nullptr);
  [[nodiscard]] QSize minimumSizeHint() const override;
  [[nodiscard]] bool hasHeightForWidth() const override;
  [[nodiscard]] int heightForWidth(int width) const override;

protected:
  [[nodiscard]] bool hitButton(const QPoint& pos) const override;
  void paintEvent(QPaintEvent* event) override;

private:
  [[nodiscard]] QStyleOptionButton style_option() const;
};
class WrappingRadioButton : public QRadioButton {
public:
  explicit WrappingRadioButton(const QString& text, QWidget* parent = nullptr);
  [[nodiscard]] QSize minimumSizeHint() const override;
  [[nodiscard]] bool hasHeightForWidth() const override;
  [[nodiscard]] int heightForWidth(int width) const override;

protected:
  [[nodiscard]] bool hitButton(const QPoint& pos) const override;
  void paintEvent(QPaintEvent* event) override;

private:
  [[nodiscard]] QStyleOptionButton style_option() const;
};
// Width of the outline a frameless window draws at its edge: 1 px where the window
// server shadows the window (Windows DWM, macOS), 2 px on X11, Wayland and wasm, which
// draw no shadow for a client-decorated window, so the outline is all that separates
// a dialog or the main window from whatever sits behind it (a check of the Linux
// chrome on October 17, 2026: 1 px dialogs blended into the window behind them).
[[nodiscard]] int frameless_window_edge_width();
enum class DialogChromeCloseMode { Reject, Accept };
QVBoxLayout* install_dark_dialog_chrome(QDialog& dialog, QVBoxLayout* root, const QString& title,
                                        DialogChromeCloseMode close_mode = DialogChromeCloseMode::Reject);
// Overrides the settings group used by remember_dialog_position (defaults to the
// dialog's objectName). Lets dialogs that share an objectName (for tests/styling)
// keep separate remembered positions. Set before remember_dialog_position runs.
void set_dialog_position_memory_id(QDialog& dialog, const QString& id);
// Opts a dialog out of position memory: it centers on its owner every time
// and drops any saved position (what About and every message box want).
void mark_dialog_always_centered(QDialog& dialog);
// Places the complete window frame inside the available screen area, then
// rechecks after Show/Resize when Qt has finalized its size and frame margins.
void remember_dialog_position(QDialog& dialog);
int exec_dialog(QDialog& dialog);
int run_non_modal_dialog(QDialog& dialog);
// The X glyph the dark dialog chrome's title bar uses, for a frameless dialog
// that draws its own corner close button (About).
QIcon dialog_chrome_close_icon();
#ifdef Q_OS_WASM
// Installs the app-wide wasm dialog guards: the modal burial raise and the
// initial-focus repair (see dialog_utils.cpp). exec_dialog and
// run_non_modal_dialog ensure them before showing anything; the MainWindow
// constructor ensures them for the wasm-only dialogs that call QDialog::exec
// directly (dialog_utils_wasm.cpp). Idempotent.
void ensure_wasm_dialog_guards();
#endif
// Leaves the innermost run_non_modal_dialog loop on this thread by exception.
// Stores `error` for that loop's frame and quits the loop, so
// run_non_modal_dialog rethrows it on its own frame once exec() returns (the
// dialog's result() is never consulted on that path). Returns false, storing
// nothing, when no run_non_modal_dialog loop is running on this thread.
//
// This is the only supported way to leave a running dialog loop with an
// exception. Throwing straight out of a slot crosses Qt's event dispatcher,
// which Qt does not support: MSVC happens to unwind through the dispatcher
// frames, but macOS reaches std::terminate inside the CFRunLoop frames and
// aborts. Catch at the slot boundary, pass std::current_exception() here, and
// return from the slot.
bool unwind_non_modal_dialog_loop(std::exception_ptr error);
// macOS: anchors the dialog's native window as a child window of its parent
// widget's window whenever it is visible, so it can never drop behind the parent
// (macOS has no Win32-style owned-window z-order; clicking the main window would
// otherwise bury a non-modal dialog, which reads as the app breaking). Implemented
// in dialog_utils_mac.mm; a no-op on other platforms, where the window system
// already keeps owned/transient dialogs above their parent.
void keep_dialog_above_parent_window(QDialog& dialog);
// Moves the mouse pointer. Use this, never QCursor::setPos: on macOS Qt moves the
// pointer by posting a synthetic mouse event, which makes the system ask the user to
// let Patchy control the computer (Accessibility). The macOS half
// (dialog_utils_mac.mm) warps the pointer directly, which needs no permission.
void move_pointer_to_global_position(QPoint global_position);
// The color under a global point, for eyedroppers that reach outside the document.
// own_window_* renders the Patchy window under the point (nullopt when there is none,
// or the point is on its native frame). screen_color_* reads the composited screen;
// on macOS that raises the Screen Recording permission prompt, so there it tries the
// own-window render first and only a pick outside Patchy's windows reads the screen.
[[nodiscard]] std::optional<QColor> own_window_color_at_global_position(QPoint global_position);
[[nodiscard]] std::optional<QColor> screen_color_at_global_position(QPoint global_position);
// Stops a QTabWidget's tab bar from painting the light native tab-bar base across
// its width (the ::tab stylesheet rules still apply). On macOS the base turns the
// whole empty area next to the tabs bright white on the dark theme; on Windows it
// stays covered until the tabs overflow, when a 1px white base line shows through
// the transparent scroll buttons at the bar's right edge.
void suppress_native_tab_bar_base(QTabWidget& tabs);
// Plain letter keys answer the box (native-message-box style; Qt itself only
// wires Alt+mnemonic): Y/N for Yes/No, S/D for Save/Discard, and Y/N also
// stand in for Save/Discard. A Discard button always reads "Don't Save".
[[nodiscard]] QMessageBox::StandardButton show_warning_message(
    QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton default_button = QMessageBox::NoButton, const QString& object_name = QString());
void show_information_message(QWidget* parent, const QString& title, const QString& text,
                              const QString& object_name = QString());
void show_critical_message(QWidget* parent, const QString& title, const QString& text,
                           const QString& object_name = QString());
// Hidden shows only the filter descriptions in the file-type dropdown ("Supported
// Files" instead of "Supported Files (*.psd *.psb ... )"); the parenthesized patterns
// still filter, and nameFilters()/selectNameFilter() keep using the full strings. Use
// it for filters whose pattern list is too long for the dropdown (the all-formats open
// filter); short per-format filters stay Shown so users can see the expected extension.
enum class FilterNameDetails { Shown, Hidden };
// `browsed_directory`, when given, receives the folder the dialog showed last, whether the
// user picked files or cancelled, so a cancelled Open still remembers where it was
// (GitHub issue 183). It stays empty when there is no folder to report (the browser picker).
[[nodiscard]] QString get_open_file_name(QWidget* parent, const QString& caption, const QString& dir,
                                          const QString& filter, QString* selected_filter = nullptr,
                                          const QString& object_name = QString(),
                                          FilterNameDetails filter_details = FilterNameDetails::Shown,
                                          QString* browsed_directory = nullptr);
[[nodiscard]] QStringList get_open_file_names(QWidget* parent, const QString& caption, const QString& dir,
                                              const QString& filter, QString* selected_filter = nullptr,
                                              const QString& object_name = QString(),
                                              FilterNameDetails filter_details = FilterNameDetails::Shown,
                                              QString* browsed_directory = nullptr);
[[nodiscard]] QString get_save_file_name(QWidget* parent, const QString& caption, const QString& dir,
                                          const QString& filter, QString* selected_filter = nullptr,
                                          const QString& object_name = QString(),
                                          const QStringList& recent_files = QStringList());
// A folder picker with an object name (tests find it like the file dialogs). Empty when
// cancelled. The browser build has no folders to pick: it returns a fresh MEMFS folder, and
// each file written there goes out through offer_browser_download_for_saved_file.
[[nodiscard]] QString get_existing_directory(QWidget* parent, const QString& caption, const QString& dir,
                                              const QString& object_name = QString());
// Call after successfully writing `path`. On wasm the write landed in MEMFS,
// which the user cannot see, so this hands the file to the browser as a
// download; desktop builds write real files and this is a no-op.
void offer_browser_download_for_saved_file(const QString& path);
void hide_menu_action_icons(QMenu* menu);
// Qt reserves the check-and-icon column only in a menu where some action has an
// icon or a check mark, so a menu with neither draws its text further left than
// its neighbours (GitHub issue 131). This gives such a menu's first action a
// blank, hidden icon so every menu keeps the column.
void reserve_menu_icon_column(QMenu* menu);
// Qt's QMenu closes on a click released over a separator, as if the click
// landed outside the menu (GitHub issue 317). This installs one application
// event filter that swallows such clicks so every QMenu (menu bar menus,
// submenus, context menus, tool flyouts) stays open, the way a click on a
// disabled item already does. Safe to call more than once.
void install_menu_separator_click_guard();
// A translated sentence on a QAction saying why it is disabled right now
// ("Select an editable pixel layer first"); empty or unset when it is not.
inline constexpr char kActionDisabledReasonProperty[] = "patchy.disabledReason";
// In `menu`, hovering a disabled action that carries kActionDisabledReasonProperty
// shows that reason as a tooltip; every other action keeps showing none (unlike
// QMenu::setToolTipsVisible, which would show each action's own label).
void install_disabled_reason_tooltips(QMenu& menu);

}  // namespace patchy::ui
