#pragma once

#include <QString>
#include <QStringList>
#include <QMessageBox>
#include <QSizeGrip>

class QDialog;
class QDoubleSpinBox;
class QMenu;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
class QWidget;

namespace patchy::ui {

void configure_toolbar_spinbox(QSpinBox* spin, int width);
void configure_toolbar_spinbox(QDoubleSpinBox* spin, int width);
void configure_dialog_spinbox(QSpinBox* spin, int width = 92);
void configure_dialog_spinbox(QDoubleSpinBox* spin, int width = 92);
// Large-button spin box styling (24px - / + buttons with readable glyphs; decrement left,
// increment far right). Append to a dialog's stylesheet AFTER all child widgets exist, and keep
// the selectors unprefixed: Qt ignores ::up-button/::down-button geometry under a descendant
// prefix, and applies sub-control rules unreliably to children created after the stylesheet.
[[nodiscard]] QString dialog_spinbox_button_style();
void configure_compact_symbol_button(QPushButton* button);
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
[[nodiscard]] QString get_open_file_name(QWidget* parent, const QString& caption, const QString& dir,
                                          const QString& filter, QString* selected_filter = nullptr,
                                          const QString& object_name = QString());
[[nodiscard]] QStringList get_open_file_names(QWidget* parent, const QString& caption, const QString& dir,
                                              const QString& filter, QString* selected_filter = nullptr,
                                              const QString& object_name = QString());
[[nodiscard]] QString get_save_file_name(QWidget* parent, const QString& caption, const QString& dir,
                                          const QString& filter, QString* selected_filter = nullptr,
                                          const QString& object_name = QString(),
                                          const QStringList& recent_files = QStringList());
void hide_menu_action_icons(QMenu* menu);

}  // namespace patchy::ui
