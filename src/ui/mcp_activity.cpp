#include "ui/mcp_activity.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/zoom_status_bar.hpp"
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>

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
  // Keep it enabled; the event filter blocks menu commands, not window chrome.
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
  label_->setText(working_ ? (editing_ ? tr("AI editing: %1") : tr("AI reading: %1")).arg(operation_)
                          : tr("AI connected"));
  if (script_) { label_->setText(tr("Running script: %1").arg(operation_)); }
  if (paused) { label_->setText((script_ ? tr("Script paused: %1") : tr("AI paused: %1")).arg(operation_)); }
  setToolTip(working_ ? tr("%1 is using this workspace. Editing resumes when the request finishes. Stop keeps changes available for Undo.").arg(client_)
                     : tr("Connected to %1 through MCP. Waiting for a Patchy request; the assistant may still be thinking.").arg(client_));
  stop_->setText(tr("Stop"));
  pause_->setText(paused ? tr("Resume") : tr("Pause"));
  pause_->setChecked(paused);
  pause_->setVisible(!qEnvironmentVariableIsSet("PATCHY_HEADLESS"));
  pause_->setEnabled(working_ && editing_ && host.run_active() && window_.isVisible());
  pause_->setToolTip(paused ? tr("Continue this operation from where it paused.")
                            : tr("Pause automation at its next checkpoint. You can still move the window, zoom, and pan."));
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
bool McpActivity::eventFilter(QObject* watched, QEvent* event) {
  if (!working_) { return false; }
  auto* widget = qobject_cast<QWidget*>(watched);
  if (!widget || widget == this || isAncestorOf(widget) ||
      (widget != &window_ && !window_.isAncestorOf(widget))) { return false; }
  // Navigation must never enter the active tool's mouse handlers: a native
  // automation stroke may already own their painting/selection gesture state.
  if (qobject_cast<ZoomPercentEdit*>(widget)) { return false; }
  auto* canvas = qobject_cast<CanvasWidget*>(widget);
  if (canvas && (event->type() == QEvent::Wheel || event->type() == QEvent::NativeGesture)) { return false; }
  if (auto* bar = qobject_cast<QScrollBar*>(widget)) {
    for (auto* parent = bar->parentWidget(); parent; parent = parent->parentWidget()) {
      if (qobject_cast<CanvasWidget*>(parent)) { return false; }
    }
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
      event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
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
    case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop:
      event->accept(); return true;
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
