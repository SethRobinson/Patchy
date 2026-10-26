// Theme events, effects and scripts, MainWindow side (docs/theme-files.md, "Theme
// scripts"): the effects overlay above the document tabs, the theme menu right of Help
// and its hotkeys, the trust prompt, the splash, status messages, tool cursors and the
// panel backgrounds the theme script host asks for, and the start-screen idle event.

#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_package.hpp"
#include "ui/start_panel.hpp"
#include "ui/theme_effects.hpp"
#include "ui/theme_script_host.hpp"

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>

namespace patchy::ui {

namespace {

// Seconds between "idle" events while the empty start screen shows.
constexpr int kIdleEventIntervalMs = 12000;

// A splash that closes on a click or when its time is up; never modal, never focused.
class ThemeSplash final : public QLabel {
 public:
  explicit ThemeSplash(QWidget* parent)
      : QLabel(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus) {
    setObjectName(QStringLiteral("themeSplash"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
  }

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    event->accept();
    close();
  }
};

// "theme." + the percent-encoded theme id + "." + the command id: persisted (Preferences
// hotkey overrides key on it), and a "/" in a QSettings key would open a group.
QString theme_command_hotkey_id(const QString& theme_id, const QString& command_id) {
  return QStringLiteral("theme.") + QString::fromLatin1(QUrl::toPercentEncoding(theme_id)) + QLatin1Char('.') +
         command_id;
}

bool g_trust_prompt_open = false;

}  // namespace

bool MainWindow::confirm_theme_scripts(QWidget* parent, const QString& theme_name) {
  QMessageBox box(QMessageBox::Question, tr("Theme Scripts"),
                  tr("The theme \"%1\" contains scripts. They can play sounds and effects and add menu commands, "
                     "but cannot read files or change documents. Allow?")
                      .arg(theme_name),
                  QMessageBox::NoButton, parent);
  box.setObjectName(QStringLiteral("themeScriptTrustPrompt"));
  auto* allow = box.addButton(tr("Allow"), QMessageBox::AcceptRole);
  allow->setObjectName(QStringLiteral("themeScriptTrustAllowButton"));
  auto* deny = box.addButton(tr("Don't Allow"), QMessageBox::RejectRole);
  deny->setObjectName(QStringLiteral("themeScriptTrustDenyButton"));
  box.setDefaultButton(deny);
  exec_dialog(box);
  return box.clickedButton() == allow;
}

void MainWindow::install_theme_extras() {
  if (document_tabs_ != nullptr) {
    auto* overlay = new ThemeEffectsOverlay(document_tabs_);
    overlay->set_start_screen_probe([this] { return start_screen_showing(); });
    theme_effects_overlay_ = overlay;
    register_theme_effects_overlay(overlay);
  }
  // Right of Help (the last menu), shown only while the theme adds commands.
  theme_menu_ = menuBar()->addMenu(tr("Theme"));
  theme_menu_->setObjectName(QStringLiteral("themeMenu"));
  theme_menu_->menuAction()->setMenuRole(QAction::NoRole);
  theme_menu_->menuAction()->setObjectName(QStringLiteral("themeMenuAction"));
  theme_menu_->menuAction()->setVisible(false);

  auto& host = ThemeScriptHost::instance();
  connect(&host, &ThemeScriptHost::commands_changed, this, [this] { rebuild_theme_menu(); });
  connect(&host, &ThemeScriptHost::cursors_changed, this, [this] { refresh_theme_cursors(); });
  connect(&host, &ThemeScriptHost::panel_backgrounds_changed, this,
          [this] { setStyleSheet(photoshop_style(tool_palette_factor_)); });
  connect(&host, &ThemeScriptHost::status_requested, this,
          [this](const QString& text, int milliseconds) { statusBar()->showMessage(text, milliseconds); });
  connect(&host, &ThemeScriptHost::splash_requested, this,
          [this](const QImage& image, int milliseconds) { show_theme_splash(image, milliseconds); });
  connect(&host, &ThemeScriptHost::trust_needed, this, [this] {
    // Outside the theme apply that raised it (often inside the Preferences preview).
    QTimer::singleShot(0, this, [this] { ask_theme_script_trust(); });
  });
  if (host.trust_pending()) {
    QTimer::singleShot(0, this, [this] { ask_theme_script_trust(); });
  }

  theme_idle_timer_ = new QTimer(this);
  theme_idle_timer_->setInterval(kIdleEventIntervalMs);
  connect(theme_idle_timer_, &QTimer::timeout, this, [this] {
    if (active_theme_assets() != nullptr && isVisible() && start_screen_showing()) {
      fire_theme_event(ThemeSoundEvent::Idle);
    }
  });
  theme_idle_timer_->start();
  rebuild_theme_menu();
}

ThemeEffectsOverlay* MainWindow::theme_effects_overlay() const {
  return static_cast<ThemeEffectsOverlay*>(theme_effects_overlay_.data());
}

bool MainWindow::start_screen_showing() const { return start_panel_ != nullptr && start_panel_->isVisible(); }

void MainWindow::ask_theme_script_trust() {
  auto& host = ThemeScriptHost::instance();
  if (!host.trust_pending() || g_trust_prompt_open || unattended_automation() || !isVisible()) {
    return;
  }
  const auto theme_id = host.theme_id();
  g_trust_prompt_open = true;
  const bool allowed = confirm_theme_scripts(this, host.theme_name());
  g_trust_prompt_open = false;
  host.set_trust(theme_id, allowed);
}

void MainWindow::rebuild_theme_menu() {
  if (theme_menu_ == nullptr) {
    return;
  }
  for (auto it = theme_command_actions_.begin(); it != theme_command_actions_.end(); ++it) {
    hotkey_registry_.unregister_command(it.key());
    it.value()->deleteLater();
  }
  theme_command_actions_.clear();
  theme_menu_->clear();
  const auto& host = ThemeScriptHost::instance();
  for (const auto& command : host.commands()) {
    const auto id = theme_command_hotkey_id(host.theme_id(), command.id);
    auto* action = new QAction(command.title, this);
    action->setMenuRole(QAction::NoRole);  // never merged on macOS (docs/platform.md)
    action->setObjectName(QStringLiteral("themeCommand.") + command.id);
    const auto command_id = command.id;
    connect(action, &QAction::triggered, this, [command_id] { ThemeScriptHost::instance().run_command(command_id); });
    QList<QKeySequence> defaults;
    if (!command.hotkey.isEmpty()) {
      defaults << QKeySequence::fromString(command.hotkey, QKeySequence::PortableText);
    }
    register_hotkey(action, id, defaults, QStringLiteral("theme"));
    theme_menu_->addAction(action);
    theme_command_actions_.insert(id, action);
  }
  hotkey_registry_.apply_to_actions();
  theme_menu_->setTitle(host.menu_title().isEmpty() ? tr("Theme") : host.menu_title());
  theme_menu_->menuAction()->setVisible(!host.commands().empty());
}

void MainWindow::refresh_theme_cursors() {
  for (const auto& session : sessions_) {
    if (session != nullptr && session->canvas != nullptr) {
      session->canvas->refresh_tool_cursor();
    }
  }
}

void MainWindow::show_theme_splash(const QImage& image, int milliseconds) {
  if (image.isNull() || !isVisible()) {
    return;
  }
  if (theme_splash_ != nullptr) {
    theme_splash_->close();
  }
  auto* splash = new ThemeSplash(this);
  const auto bounds = size() * 0.7;
  auto pixmap = QPixmap::fromImage(image);
  if (pixmap.width() > bounds.width() || pixmap.height() > bounds.height()) {
    pixmap = pixmap.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  splash->setPixmap(pixmap);
  splash->resize(pixmap.size());
  splash->move(mapToGlobal(QPoint((width() - pixmap.width()) / 2, (height() - pixmap.height()) / 2)));
  splash->show();
  theme_splash_ = splash;
  QTimer::singleShot(milliseconds, splash, &QWidget::close);
}

}  // namespace patchy::ui
