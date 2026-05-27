#pragma once

class QDialog;
class QDoubleSpinBox;
class QMenu;
class QSpinBox;
class QString;
class QVBoxLayout;

namespace photoslop::ui {

void configure_toolbar_spinbox(QSpinBox* spin, int width);
void configure_dialog_spinbox(QSpinBox* spin, int width = 92);
void configure_dialog_spinbox(QDoubleSpinBox* spin, int width = 92);
QVBoxLayout* install_dark_dialog_chrome(QDialog& dialog, QVBoxLayout* root, const QString& title);
void remember_dialog_position(QDialog& dialog);
int exec_dialog(QDialog& dialog);
int run_non_modal_dialog(QDialog& dialog);
void hide_menu_action_icons(QMenu* menu);

}  // namespace photoslop::ui
