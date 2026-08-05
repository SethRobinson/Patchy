#pragma once

#include "ui/theme_qss.hpp"

#include <QFont>
#include <QString>
#include <QStringList>
#include <QMessageBox>
#include <QSizeGrip>

#include <exception>

class QAction;
class QDialog;
class QDoubleSpinBox;
class QFormLayout;
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

// `width` is a minimum: the box grows to keep its widest possible value text
// (prefix + min/max + suffix) clear of the trailing popup chevron. Set the
// range, decimals, prefix, and suffix BEFORE calling this.
void configure_toolbar_spinbox(QSpinBox* spin, int width);
void configure_toolbar_spinbox(QDoubleSpinBox* spin, int width);
// Set this double property on a toolbar spin box to cap its popup SLIDER below the
// spin box's maximum (the spin box itself keeps accepting larger typed values; the
// slider extends to the current value when it already sits above the cap).
inline constexpr char kToolbarSpinboxSliderMaxProperty[] = "patchy.popupSliderMax";
void configure_dialog_spinbox(QSpinBox* spin, int width = 92);
void configure_dialog_spinbox(QDoubleSpinBox* spin, int width = 92);
// Large-button spin box styling (24px - / + buttons with readable glyphs; decrement left,
// increment far right). Append to a dialog's stylesheet AFTER all child widgets exist, and keep
// the selectors unprefixed: Qt ignores ::up-button/::down-button geometry under a descendant
// prefix, and applies sub-control rules unreliably to children created after the stylesheet.
[[nodiscard]] ThemedQss dialog_spinbox_button_style();
void configure_compact_symbol_button(QPushButton* button);
// Adds a "label: [slider ------] [spin]" form row whose slider and spin box mirror
// each other. Object names are passed explicitly (never derived here): UI tests look
// these widgets up by exact objectName, so each call site keeps its own naming
// scheme. row_spacing < 0 keeps the layout's default spacing.
QSpinBox* add_dialog_slider_spin_row(QFormLayout* form, QWidget* parent, const QString& label,
                                     const QString& slider_object_name, const QString& spin_object_name,
                                     int minimum, int maximum, int value, const QString& suffix = QString(),
                                     int spin_width = 72, int row_spacing = -1);
// Moves a popup (already resized to its final size) directly below `anchor`:
// clamps it inside the screen's available horizontal range and flips it above
// the anchor when it would run past the bottom. Call before show().
void position_popup_below(const QWidget& anchor, QWidget& popup);
// QSizeGrip paints through the platform style, which is close to invisible on the dark QSS
// theme; repaint it as three light diagonal strokes so the resize corner is discoverable.
// The resize handle for frameless windows (chrome dialogs, popups), which have no native border.
class VisibleSizeGrip : public QSizeGrip {
public:
  explicit VisibleSizeGrip(QWidget* parent);

protected:
  void paintEvent(QPaintEvent* event) override;
};
enum class DialogChromeCloseMode { Reject, Accept };
QVBoxLayout* install_dark_dialog_chrome(QDialog& dialog, QVBoxLayout* root, const QString& title,
                                        DialogChromeCloseMode close_mode = DialogChromeCloseMode::Reject);
// Overrides the settings group used by remember_dialog_position (defaults to the
// dialog's objectName). Lets dialogs that share an objectName (for tests/styling)
// keep separate remembered positions. Set before remember_dialog_position runs.
void set_dialog_position_memory_id(QDialog& dialog, const QString& id);
void remember_dialog_position(QDialog& dialog);
int exec_dialog(QDialog& dialog);
int run_non_modal_dialog(QDialog& dialog);
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
// macOS: stops a document-mode QTabWidget's tab bar from painting the light native
// window-tab-bar base across its width (the ::tab stylesheet rules still apply, but
// the empty area next to the tabs turns bright white on the dark theme). No-op on
// other platforms, whose base drawing is already invisible under the stylesheet.
void suppress_native_tab_bar_base(QTabWidget& tabs);
// When the box has Yes/No buttons, plain Y/N key presses activate them
// (native-message-box style; Qt itself only wires Alt+mnemonic).
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
[[nodiscard]] QString get_open_file_name(QWidget* parent, const QString& caption, const QString& dir,
                                          const QString& filter, QString* selected_filter = nullptr,
                                          const QString& object_name = QString(),
                                          FilterNameDetails filter_details = FilterNameDetails::Shown);
[[nodiscard]] QStringList get_open_file_names(QWidget* parent, const QString& caption, const QString& dir,
                                              const QString& filter, QString* selected_filter = nullptr,
                                              const QString& object_name = QString(),
                                              FilterNameDetails filter_details = FilterNameDetails::Shown);
[[nodiscard]] QString get_save_file_name(QWidget* parent, const QString& caption, const QString& dir,
                                          const QString& filter, QString* selected_filter = nullptr,
                                          const QString& object_name = QString(),
                                          const QStringList& recent_files = QStringList());
// Call after successfully writing `path`. On wasm the write landed in MEMFS,
// which the user cannot see, so this hands the file to the browser as a
// download; desktop builds write real files and this is a no-op.
void offer_browser_download_for_saved_file(const QString& path);
void hide_menu_action_icons(QMenu* menu);

}  // namespace patchy::ui
