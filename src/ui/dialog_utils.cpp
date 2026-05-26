#include "ui/dialog_utils.hpp"

#include <QAbstractSpinBox>
#include <QAction>
#include <QDialog>
#include <QEventLoop>
#include <QMenu>
#include <QSpinBox>

namespace photoslop::ui {

void configure_toolbar_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setFixedWidth(width);
}

void configure_dialog_spinbox(QSpinBox* spin, int width) {
  spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  spin->setMinimumWidth(width);
  spin->setMinimumHeight(24);
}

int run_non_modal_dialog(QDialog& dialog) {
  dialog.setModal(false);
  dialog.setWindowModality(Qt::NonModal);
  QEventLoop loop;
  QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
  dialog.show();
  dialog.raise();
  dialog.activateWindow();
  loop.exec();
  return dialog.result();
}

void hide_menu_action_icons(QMenu* menu) {
  if (menu == nullptr) {
    return;
  }
  for (auto* action : menu->actions()) {
    action->setIconVisibleInMenu(false);
    if (auto* child_menu = action->menu(); child_menu != nullptr) {
      hide_menu_action_icons(child_menu);
    }
  }
}

}  // namespace photoslop::ui
