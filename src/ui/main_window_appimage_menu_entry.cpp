// Help > Add Patchy to Applications Menu (GitHub issue 226) and its startup offer. The
// file work is ui/appimage_menu_entry.{hpp,cpp}; docs/appimage.md, "Applications menu entry".

#include "ui/main_window.hpp"

#include "ui/app_settings.hpp"
#include "ui/appimage_menu_entry.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_registry.hpp"

#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

namespace patchy::ui {

namespace {

// Startup waits this long before offering, so the window is up and the update check's
// answer (a modal prompt of its own) usually came first.
constexpr int kOfferDelayMs = 2000;
// Retried this often while another dialog is up.
constexpr int kOfferRetryMs = 3000;

AppImageMenuEntryPaths menu_entry_paths(const QString& bundled_share_override) {
  auto paths = resolve_appimage_menu_entry_paths();
  if (!bundled_share_override.isEmpty() && !paths.appimage_file.isEmpty()) {
    paths.bundled_share = bundled_share_override;
  }
  return paths;
}

}  // namespace

void MainWindow::refresh_appimage_menu_entry_action() {
  if (appimage_menu_entry_action_ == nullptr) {
    return;
  }
  const auto state = appimage_menu_entry_state(menu_entry_paths(appimage_bundled_share_for_tests_));
  appimage_menu_entry_action_->setVisible(state != AppImageMenuEntryState::Unavailable);
  appimage_menu_entry_action_->setEnabled(state == AppImageMenuEntryState::NotAdded ||
                                          state == AppImageMenuEntryState::Added);
  appimage_menu_entry_action_->setText(state == AppImageMenuEntryState::Added
                                           ? tr("Remove Patchy from Applications Menu")
                                           : tr("Add Patchy to Applications Menu..."));
  appimage_menu_entry_action_->setProperty(
      kActionTooltipDetailProperty,
      state == AppImageMenuEntryState::AddedElsewhere
          ? QString::fromLatin1(
                QT_TR_NOOP("Patchy is already in the applications menu, added by another tool or package."))
          : QString());
  refresh_action_tooltip(appimage_menu_entry_action_);
}

void MainWindow::toggle_appimage_menu_entry() {
  const auto paths = menu_entry_paths(appimage_bundled_share_for_tests_);
  set_appimage_menu_entry(appimage_menu_entry_state(paths) != AppImageMenuEntryState::Added);
}

void MainWindow::set_appimage_menu_entry(bool adding) {
  const auto paths = menu_entry_paths(appimage_bundled_share_for_tests_);
  const auto result = adding ? add_appimage_menu_entry(paths) : remove_appimage_menu_entry(paths);
  if (result.ok && result.changed && appimage_bundled_share_for_tests_.isEmpty()) {
    refresh_desktop_menu_caches(paths);
  }
  if (result.ok && !adding) {
    // Removing it is an answer too: the startup offer stays away.
    set_stored_appimage_menu_entry_prompt_declined(true);
  }
  refresh_appimage_menu_entry_action();
  if (!result.ok) {
    show_status_error(adding ? tr("Could not add Patchy to the applications menu: %1").arg(result.error)
                             : tr("Could not remove Patchy from the applications menu: %1").arg(result.error));
    return;
  }
  statusBar()->showMessage(adding ? tr("Patchy was added to the applications menu")
                                  : tr("Patchy was removed from the applications menu"));
}

bool MainWindow::offer_appimage_menu_entry() {
  if (unattended_automation() || stored_appimage_menu_entry_prompt_declined() ||
      appimage_menu_entry_state(menu_entry_paths(appimage_bundled_share_for_tests_)) !=
          AppImageMenuEntryState::NotAdded) {
    return false;
  }
  QMessageBox dialog(QMessageBox::Question, tr("Applications Menu"),
                     tr("Add Patchy to your applications menu?\n\n"
                        "Patchy then starts from the menu and shows its own icon in the taskbar. "
                        "This writes a menu entry and icons to your home folder.\n\n"
                        "You can do this later from Help > Add Patchy to Applications Menu."),
                     QMessageBox::NoButton, this);
  dialog.setObjectName(QStringLiteral("appImageMenuEntryOfferMessageBox"));
  dialog.setTextFormat(Qt::PlainText);
  auto* add_button = dialog.addButton(tr("Add"), QMessageBox::AcceptRole);
  add_button->setObjectName(QStringLiteral("appImageMenuEntryAddButton"));
  auto* not_now_button = dialog.addButton(tr("Not Now"), QMessageBox::RejectRole);
  not_now_button->setObjectName(QStringLiteral("appImageMenuEntryNotNowButton"));
  auto* never_button = dialog.addButton(tr("Don't Ask Again"), QMessageBox::DestructiveRole);
  never_button->setObjectName(QStringLiteral("appImageMenuEntryDontAskButton"));
  dialog.setDefaultButton(add_button);
  dialog.setEscapeButton(not_now_button);
  run_non_modal_dialog(dialog);
  // The offer stays open while the user works: Help may have added the entry meanwhile.
  if (dialog.clickedButton() == add_button &&
      appimage_menu_entry_state(menu_entry_paths(appimage_bundled_share_for_tests_)) ==
          AppImageMenuEntryState::NotAdded) {
    set_appimage_menu_entry(true);
  } else if (dialog.clickedButton() == never_button) {
    set_stored_appimage_menu_entry_prompt_declined(true);
  }
  return true;
}

void MainWindow::schedule_appimage_menu_entry_offer(int delay_ms) {
  QTimer::singleShot(delay_ms, this, [this] {
    // Another dialog (the update prompt, a recovery notice) finishes first.
    if (QApplication::activeModalWidget() != nullptr || QApplication::activePopupWidget() != nullptr) {
      schedule_appimage_menu_entry_offer(kOfferRetryMs);
      return;
    }
    (void)offer_appimage_menu_entry();
  });
}

void MainWindow::begin_appimage_menu_entry_startup() {
  const auto paths = menu_entry_paths(appimage_bundled_share_for_tests_);
  const auto state = appimage_menu_entry_state(paths);
  if (state == AppImageMenuEntryState::Added) {
    if (const auto result = refresh_appimage_menu_entry(paths); !result.ok) {
      qWarning("Patchy: could not refresh the applications menu entry: %s", qPrintable(result.error));
    } else if (result.changed && appimage_bundled_share_for_tests_.isEmpty()) {
      refresh_desktop_menu_caches(paths);
    }
  } else if (state == AppImageMenuEntryState::NotAdded && !stored_appimage_menu_entry_prompt_declined()) {
    schedule_appimage_menu_entry_offer(kOfferDelayMs);
  }
  refresh_appimage_menu_entry_action();
}

}  // namespace patchy::ui
