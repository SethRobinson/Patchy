#pragma once

class QDialog;
class QMenu;
class QSpinBox;

namespace photoslop::ui {

void configure_toolbar_spinbox(QSpinBox* spin, int width);
void configure_dialog_spinbox(QSpinBox* spin, int width = 92);
int run_non_modal_dialog(QDialog& dialog);
void hide_menu_action_icons(QMenu* menu);

}  // namespace photoslop::ui
