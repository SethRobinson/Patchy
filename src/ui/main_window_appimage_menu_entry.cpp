// Help > Add Patchy to Applications Menu (GitHub issue 226) and its startup offer. The
// file work is ui/appimage_menu_entry.{hpp,cpp}; docs/appimage.md, "Applications menu entry".

#include "ui/main_window.hpp"

#include "ui/app_settings.hpp"
#include "ui/appimage_menu_entry.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace patchy::ui {

namespace {

// Startup waits this long before offering, so the window is up and settled first.
constexpr int kOfferDelayMs = 2000;

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
  if (state != AppImageMenuEntryState::NotAdded) {
    // Help (or another tool) answered the question the open bar asks.
    close_appimage_menu_entry_offer();
  }
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
      appimage_menu_entry_offer_bar_ != nullptr || workspace_splitter_ == nullptr ||
      centralWidget() != workspace_splitter_ ||
      appimage_menu_entry_state(menu_entry_paths(appimage_bundled_share_for_tests_)) !=
          AppImageMenuEntryState::NotAdded) {
    return false;
  }
  // A bar across the top of the workspace, not a dialog: nothing covers the window, and
  // the first window a screenshot or the AppImage catalog's test captures is the main
  // window (GitHub issue 208).
  auto* bar = new QFrame;
  bar->setObjectName(QStringLiteral("appImageMenuEntryOfferBar"));
  set_themed_style(*bar, QStringLiteral("QFrame#appImageMenuEntryOfferBar { background: @info_banner_bg; border: none; "
                                        "border-bottom: 1px solid @info_banner_border; }"
                                        "QFrame#appImageMenuEntryOfferBar QLabel { background: transparent; "
                                        "color: @info_banner_text; }"));
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(10, 6, 8, 6);
  layout->setSpacing(8);
  auto* message = new QLabel(bar);
  message->setObjectName(QStringLiteral("appImageMenuEntryOfferText"));
  message->setTextFormat(Qt::PlainText);
  message->setWordWrap(true);
  bind_widget_text(message, QT_TR_NOOP("Add Patchy to your applications menu? It then starts from the menu and shows "
                                       "its own icon in the taskbar. This writes a menu entry and icons to your home "
                                       "folder. You can also do it later from Help > Add Patchy to Applications Menu."));
  layout->addWidget(message, 1);
  const auto add_answer = [bar, layout](const char* name, const char* text) {
    auto* button = new QPushButton(bar);
    button->setObjectName(QString::fromLatin1(name));
    bind_widget_text(button, text);
    layout->addWidget(button, 0, Qt::AlignVCenter);
    return button;
  };
  auto* add_button = add_answer("appImageMenuEntryAddButton", QT_TR_NOOP("Add"));
  auto* not_now_button = add_answer("appImageMenuEntryNotNowButton", QT_TR_NOOP("Not Now"));
  auto* never_button = add_answer("appImageMenuEntryDontAskButton", QT_TR_NOOP("Don't Ask Again"));
  connect(add_button, &QPushButton::clicked, this, [this] {
    close_appimage_menu_entry_offer();
    // The bar stays up while the user works: Help may have added the entry meanwhile.
    if (appimage_menu_entry_state(menu_entry_paths(appimage_bundled_share_for_tests_)) ==
        AppImageMenuEntryState::NotAdded) {
      set_appimage_menu_entry(true);
    }
  });
  connect(not_now_button, &QPushButton::clicked, this, [this] { close_appimage_menu_entry_offer(); });
  connect(never_button, &QPushButton::clicked, this, [this] {
    close_appimage_menu_entry_offer();
    set_stored_appimage_menu_entry_prompt_declined(true);
  });

  auto* holder = new QWidget(this);
  holder->setObjectName(QStringLiteral("appImageMenuEntryOfferHolder"));
  auto* holder_layout = new QVBoxLayout(holder);
  holder_layout->setContentsMargins(0, 0, 0, 0);
  holder_layout->setSpacing(0);
  // Moving the workspace into the holder drops keyboard focus; give it back.
  const QPointer<QWidget> focus = QApplication::focusWidget();
  (void)takeCentralWidget();
  holder_layout->addWidget(bar);
  holder_layout->addWidget(workspace_splitter_, 1);
  setCentralWidget(holder);
  workspace_splitter_->show();
  if (focus != nullptr) {
    focus->setFocus();
  }
  appimage_menu_entry_offer_bar_ = bar;
  return true;
}

void MainWindow::close_appimage_menu_entry_offer() {
  if (appimage_menu_entry_offer_bar_ == nullptr) {
    return;
  }
  appimage_menu_entry_offer_bar_ = nullptr;
  auto* holder = centralWidget();
  if (holder == nullptr || holder == workspace_splitter_) {
    return;
  }
  // Called from the bar's own buttons: the holder (and the bar in it) goes later.
  const QPointer<QWidget> focus = QApplication::focusWidget();
  (void)takeCentralWidget();
  setCentralWidget(workspace_splitter_);
  workspace_splitter_->show();
  if (focus != nullptr && workspace_splitter_->isAncestorOf(focus)) {
    focus->setFocus();
  }
  holder->hide();
  holder->deleteLater();
}

void MainWindow::schedule_appimage_menu_entry_offer(int delay_ms) {
  // The bar covers nothing, so it needs no wait for another dialog (the update prompt, a
  // recovery notice) to finish.
  QTimer::singleShot(delay_ms, this, [this] { (void)offer_appimage_menu_entry(); });
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
  } else if (state == AppImageMenuEntryState::NotAdded && !stored_appimage_menu_entry_prompt_declined() &&
             appimage_menu_entry_offer_session_allowed()) {
    schedule_appimage_menu_entry_offer(kOfferDelayMs);
  }
  refresh_appimage_menu_entry_action();
}

}  // namespace patchy::ui
