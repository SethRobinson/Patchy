#pragma once

#include <QString>
#include <QStringList>
#include <QMessageBox>

class QDialog;
class QDoubleSpinBox;
class QMenu;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace patchy::ui {

void configure_toolbar_spinbox(QSpinBox* spin, int width);
void configure_toolbar_spinbox(QDoubleSpinBox* spin, int width);
void configure_dialog_spinbox(QSpinBox* spin, int width = 92);
void configure_dialog_spinbox(QDoubleSpinBox* spin, int width = 92);
void configure_compact_symbol_button(QPushButton* button);
QVBoxLayout* install_dark_dialog_chrome(QDialog& dialog, QVBoxLayout* root, const QString& title);
void remember_dialog_position(QDialog& dialog);
int exec_dialog(QDialog& dialog);
int run_non_modal_dialog(QDialog& dialog);
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
