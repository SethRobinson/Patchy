#include "ui/mcp_activity.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/zoom_status_bar.hpp"
#include "ui/hotkey_registry.hpp"
#include <QAction>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QAbstractSlider>
#include <QComboBox>
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolTip>
#include <QCursor>

namespace patchy::ui {
McpActivity::McpActivity(MainWindow& window, std::function<void()> stop, bool script)
    : QWidget(window.statusBar()), window_(window), label_(new QLabel(this)),
      stop_(new QPushButton(this)), pause_(new QPushButton(this)), slow_(new QPushButton(this)), script_(script) {
  setObjectName(script ? QStringLiteral("scriptActivity") : QStringLiteral("mcpActivity"));
  label_->setObjectName(script ? QStringLiteral("scriptActivityLabel") : QStringLiteral("mcpActivityLabel"));
  label_->setTextFormat(Qt::PlainText);
  stop_->setObjectName(script ? QStringLiteral("scriptStopButton") : QStringLiteral("mcpStopButton"));
  label_->setMaximumWidth(210);
  stop_->setFocusPolicy(Qt::NoFocus);
  pause_->setObjectName(script ? QStringLiteral("scriptPauseButton") : QStringLiteral("mcpPauseButton"));
  pause_->setCheckable(true);
  pause_->setFocusPolicy(Qt::NoFocus);
  slow_->setObjectName(script ? QStringLiteral("scriptSlowButton") : QStringLiteral("mcpSlowButton"));
  slow_->setCheckable(true);
  slow_->setFocusPolicy(Qt::NoFocus);
  auto& host = window.script_engine_host();
  slow_->setChecked(host.slow_mode());
  connect(slow_, &QPushButton::toggled, &host, &ScriptEngineHost::set_slow_mode);
  connect(&host, &ScriptEngineHost::slow_mode_changed, slow_, &QPushButton::setChecked);
  connect(pause_, &QPushButton::clicked, &host, &ScriptEngineHost::set_paused);
  connect(&host, &ScriptEngineHost::paused_changed, this, [this] { refresh(); });
  connect(&host, &ScriptEngineHost::run_state_changed, this, [this] { refresh(); });
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(6, 0, 6, 0);
  row->setSpacing(6);
  row->addWidget(label_);
  row->addWidget(stop_);
  row->addWidget(pause_);
  row->addWidget(slow_);
  connect(stop_, &QPushButton::clicked, this, [stop = std::move(stop)] { stop(); });
  window.statusBar()->addPermanentWidget(this);
  qApp->installEventFilter(this);
  auto* heartbeat = new QTimer(this);
  heartbeat->setInterval(100);
  connect(heartbeat, &QTimer::timeout, this, [this] {
    // A user browsing a menu or dialog may keep its nested event loop open.
    // This is activity; a tight JS loop still cannot deliver timer events.
    if (working_) window_.script_engine_host().keep_alive_for_ui();
  });
  heartbeat->start();
  refresh();
}

void McpActivity::set_connected(const QString& client) {
  client_ = client.left(80);
  connected_ = true;
  refresh();
}
void McpActivity::set_operation(const QString& operation, bool editing) {
  operation_ = operation.left(48);
  // The custom title bar and its window buttons live inside the menu bar.
  // Keep browsing and window chrome enabled; filter conflicting activations.
  working_ = true;
  editing_ = editing;
  refresh();
  // A synchronous operation may not return to the event loop for a while.
  // Paint the truthful working state before entering it, without input delivery.
  QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  repaint();
}
void McpActivity::finish_operation() {
  if (panning_canvas_) { (void)panning_canvas_->end_pan(); panning_canvas_.clear(); }
  pan_button_ = Qt::NoButton;
  space_down_ = false;
  working_ = false;
  refresh();
}
void McpActivity::set_disconnected() {
  connected_ = false;
  finish_operation();
}
void McpActivity::refresh() {
  const auto& host = window_.script_engine_host();
  const bool paused = working_ && editing_ && host.paused();
  const bool ready = paused && host.manual_edit_pause();
  if (ready && panning_canvas_) {
    (void)panning_canvas_->end_pan(); panning_canvas_.clear();
    pan_button_ = Qt::NoButton; space_down_ = false;
  }
  label_->setText(working_ ? (editing_ ? tr("AI editing: %1") : tr("AI reading: %1")).arg(operation_)
                          : tr("AI connected"));
  if (script_) { label_->setText(tr("Running script: %1").arg(operation_)); }
  if (paused) { label_->setText((script_ ? tr("Script paused: %1") : tr("AI paused: %1")).arg(operation_)); }
  if (paused && !ready) { label_->setText((script_ ? tr("Script pausing: %1") : tr("AI pausing: %1")).arg(operation_)); }
  setToolTip(working_ ? tr("%1 is using this workspace. You can browse while it works. Pause to edit; Stop keeps completed changes available for Undo.").arg(client_)
                     : tr("Connected to %1 through MCP. Waiting for a Patchy request; the assistant may still be thinking.").arg(client_));
  stop_->setText(tr("Stop"));
  pause_->setText(ready ? tr("Resume") : paused ? tr("Pausing...") : tr("Pause"));
  pause_->setChecked(paused);
  pause_->setVisible(!qEnvironmentVariableIsSet("PATCHY_HEADLESS"));
  pause_->setEnabled(working_ && editing_ && host.run_active() && window_.isVisible());
  pause_->setToolTip(ready ? tr("Continue automation using the edited workspace. Missing or incompatible targets stop with an error.")
                          : tr("Pause after the current edit so you can draw, move layers, or change the document."));
  slow_->setText(tr("Slow"));
  slow_->setVisible(!qEnvironmentVariableIsSet("PATCHY_HEADLESS"));
  slow_->setEnabled(window_.isVisible());
  slow_->setToolTip(tr("Show each stroke or edit with a short pause and a separate Undo step. You can change this while work is running. History limits still apply."));
  stop_->setVisible(true);
  stop_->setEnabled(working_ && editing_);
  stop_->setToolTip(working_ && editing_ ? tr("Stop this operation and keep its changes available for Undo.")
                                       : tr("No Patchy edit is running. Use Stop in your assistant to stop it between requests."));
  setVisible(connected_);
}
void McpActivity::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) { refresh(); }
}
bool McpActivity::action_allowed(const QAction* action) const {
  if (!action || action->isSeparator() || action->menu()) return true;
  if (action->menuRole() == QAction::QuitRole) return false;
  QString id;
  for (const auto& command : window_.hotkey_registry().commands()) {
    if (command.action == action) { id = command.id; break; }
  }
  if (id == QStringLiteral("file.quit")) return false;
  if (window_.script_engine_host().manual_edit_pause()) return true;
  if (action->menuRole() == QAction::AboutRole || action->menuRole() == QAction::AboutQtRole ||
      action->menuRole() == QAction::PreferencesRole || id == QStringLiteral("file.preferences") ||
      id.startsWith(QStringLiteral("help."))) return true;
  if (id.startsWith(QStringLiteral("view.")) && !id.startsWith(QStringLiteral("view.new_guide")) &&
      !id.startsWith(QStringLiteral("view.clear_"))) return true;
  return id == QStringLiteral("window.force_refresh") ||
         action->objectName().startsWith(QStringLiteral("windowSetScreenSize"));
}
const QAction* McpActivity::shortcut_action(const QKeyEvent& event) const {
  const QKeySequence sequence(event.keyCombination());
  for (const auto& command : window_.hotkey_registry().commands()) {
    if (!command.action || !command.action->isEnabled()) continue;
    for (const auto& shortcut : command.action->shortcuts()) {
      if (shortcut.matches(sequence) == QKeySequence::ExactMatch) return command.action;
    }
  }
  return nullptr;
}
bool McpActivity::explain_conflict(QEvent* event, bool closing) {
  if (closing || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
      event->type() == QEvent::KeyPress || event->type() == QEvent::Shortcut || event->type() == QEvent::ContextMenu) {
    const auto message = closing ? tr("Stop automation before closing Patchy.")
        : window_.script_engine_host().paused()
            ? tr("Finishing the current edit. Manual editing is available when Resume appears.")
            : tr("Pause automation to change the document or its editing controls. Browsing and scrolling are available while it works.");
    window_.statusBar()->showMessage(message, 6000);
    QToolTip::showText(QCursor::pos(), message, &window_);
  }
  event->accept();
  return true;
}
bool McpActivity::eventFilter(QObject* watched, QEvent* event) {
  if (!working_) { return false; }
  if (auto* action = qobject_cast<QAction*>(watched); action && event->type() == QEvent::Shortcut) {
    for (auto* owner = action->parent(); owner; owner = owner->parent()) {
      if (owner == &window_) return action_allowed(action) ? false : explain_conflict(event);
    }
    return false;
  }
  auto* widget = qobject_cast<QWidget*>(watched);
  if (!widget || widget == this || isAncestorOf(widget)) return false;
  // QWidget::isAncestorOf stops at window boundaries. Menus and dialogs are
  // separate windows, but their QObject ownership still belongs to this workspace.
  bool owned = false;
  for (auto* owner = static_cast<QObject*>(widget); owner; owner = owner->parent()) {
    if (owner == &window_) { owned = true; break; }
  }
  if (!owned) return false;
  if (event->type() == QEvent::Close && widget == &window_) {
    explain_conflict(event, true); event->ignore(); return true;
  }
  if (auto* menu = qobject_cast<QMenu*>(widget)) {
    const bool mouse_activation = event->type() == QEvent::MouseButtonRelease;
    const bool key_activation = event->type() == QEvent::KeyPress &&
        (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Return ||
         static_cast<QKeyEvent*>(event)->key() == Qt::Key_Enter ||
         static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space);
    const auto* action = mouse_activation ? menu->actionAt(static_cast<QMouseEvent*>(event)->position().toPoint()) : menu->activeAction();
    if ((mouse_activation || key_activation) && !action_allowed(action)) return explain_conflict(event);
    if (event->type() == QEvent::KeyPress && !key_activation) {
      const auto text = static_cast<QKeyEvent*>(event)->text();
      if (text.size() == 1) {
        for (const auto* candidate : menu->actions()) {
          const auto label = candidate->text();
          for (qsizetype i = 0; i + 1 < label.size(); ++i) {
            if (label[i] != QLatin1Char('&')) continue;
            if (label[i + 1] == QLatin1Char('&')) { ++i; continue; }
            if (label[i + 1].toCaseFolded() == text[0].toCaseFolded() && !action_allowed(candidate))
              return explain_conflict(event);
          }
        }
      }
    }
    return false;
  }
  if (qobject_cast<QMenuBar*>(widget)) { return false; }
  // Once native API locals have unwound, ordinary editing owns the canvas.
  if (window_.script_engine_host().manual_edit_pause()) { return false; }
  // Preferences can be inspected without applying their painting/tool changes.
  // About and the other permitted informational dialogs remain interactive.
  for (auto* parent = widget; parent && parent != &window_; parent = parent->parentWidget()) {
    if (auto* dialog = qobject_cast<QDialog*>(parent)) {
      if (dialog->objectName() == QStringLiteral("patchyPreferencesDialog")) {
        bool applies = widget->objectName() == QStringLiteral("preferencesRunStressTestButton") ||
                       widget->objectName() == QStringLiteral("preferencesRemoveUserFontsButton");
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
          if (auto* box = qobject_cast<QDialogButtonBox*>(button->parentWidget())) {
            const auto role = box->buttonRole(button);
            applies = applies || role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::ApplyRole;
          }
        }
        const bool enter = event->type() == QEvent::KeyPress &&
            (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Return || static_cast<QKeyEvent*>(event)->key() == Qt::Key_Enter);
        const bool button_key = event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease || event->type() == QEvent::Shortcut;
        if ((applies && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease || button_key)) || enter) {
          const auto message = tr("Close Preferences and pause automation before applying settings.");
          window_.statusBar()->showMessage(message, 6000); QToolTip::showText(QCursor::pos(), message, widget);
          event->accept(); return true;
        }
      }
      return false;
    }
  }
  // Navigation must never enter the active tool's mouse handlers: a native
  // automation stroke may already own their painting/selection gesture state.
  if (qobject_cast<ZoomPercentEdit*>(widget)) { return false; }
  if (widget->objectName() == QStringLiteral("layerNameFilterEdit") ||
      widget->objectName() == QStringLiteral("layerFolderDisclosureButton") ||
      widget->property("dockCollapseButton").toBool() ||
      widget->objectName() == QStringLiteral("rightDockResizeHandle") ||
      widget->objectName().endsWith(QStringLiteral("DockTitle")) ||
      widget->objectName().endsWith(QStringLiteral("DockTitleLabel"))) { return false; }
  if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
    auto* owner = qobject_cast<QTabWidget*>(tabs->parentWidget());
    if (owner && owner->objectName() != QStringLiteral("documentTabs")) return false;
  }
  auto* canvas = qobject_cast<CanvasWidget*>(widget);
  if (canvas && (event->type() == QEvent::Wheel || event->type() == QEvent::NativeGesture)) { return false; }
  if (qobject_cast<QScrollBar*>(widget)) { return false; }
  if (event->type() == QEvent::Wheel) {
    // Wheel events normally start on a row label, not on the viewport. Allow
    // propagation to the scroll area without admitting spin/slider edits.
    for (auto* parent = widget; parent; parent = parent->parentWidget()) {
      if (qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent) ||
          qobject_cast<QAbstractSlider*>(parent)) return explain_conflict(event);
      if (qobject_cast<QAbstractScrollArea*>(parent)) return false;
    }
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
      event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (const auto* action = shortcut_action(*key)) {
      if (action_allowed(action)) return false;
      if (event->type() != QEvent::KeyRelease) return explain_conflict(event);
    }
    if (key->key() == Qt::Key_Alt || (key->modifiers() & Qt::AltModifier)) return false;
    if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
      if (event->type() != QEvent::ShortcutOverride && !key->isAutoRepeat()) {
        space_down_ = event->type() == QEvent::KeyPress;
      }
      event->accept(); return true;
    }
  }
  if (event->type() == QEvent::WindowDeactivate) {
    space_down_ = false;
    if (panning_canvas_) { (void)panning_canvas_->end_pan(); panning_canvas_.clear(); }
    pan_button_ = Qt::NoButton;
  }
  const bool mouse = event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
                     event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove;
  if (mouse) {
    auto* pointer = static_cast<QMouseEvent*>(event);
    if (panning_canvas_) {
      if (event->type() == QEvent::MouseMove) {
        (void)panning_canvas_->pan_to_global_position(pointer->globalPosition().toPoint());
        event->accept(); return true;
      }
      if (event->type() == QEvent::MouseButtonRelease && pointer->button() == pan_button_) {
        (void)panning_canvas_->end_pan(); panning_canvas_.clear(); pan_button_ = Qt::NoButton;
        event->accept(); return true;
      }
    }
    if (canvas && event->type() == QEvent::MouseButtonPress &&
        (pointer->button() == Qt::MiddleButton ||
         (pointer->button() == Qt::RightButton && pointer->modifiers() == Qt::NoModifier) ||
         (pointer->button() == Qt::LeftButton && space_down_))) {
      if (canvas->begin_pan_at_global_position(pointer->globalPosition().toPoint())) {
        panning_canvas_ = canvas; pan_button_ = pointer->button();
      }
      event->accept(); return true;
    }
    if (widget == &window_ || widget->objectName() == QStringLiteral("windowMinimizeButton") ||
        widget->objectName() == QStringLiteral("windowMaximizeButton")) { return false; }
    if (widget == window_.menuBar() &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease ||
         window_.menuBar()->actionAt(pointer->position().toPoint()) == nullptr)) { return false; }
  }
  switch (event->type()) {
    case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick: case QEvent::MouseMove: case QEvent::Wheel:
    case QEvent::TabletPress: case QEvent::TabletMove: case QEvent::TabletRelease:
    case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd:
    case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::Shortcut:
    case QEvent::ShortcutOverride: case QEvent::InputMethod:
    case QEvent::ContextMenu:
    case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop:
      return explain_conflict(event);
    case QEvent::Close:
      // Internal text/editor teardown must still close its own widgets.
      if (widget == &window_ || (widget->isWindow() && event->spontaneous())) {
        event->ignore(); return true;
      }
      return false;
    default: return false;
  }
}
}  // namespace patchy::ui
