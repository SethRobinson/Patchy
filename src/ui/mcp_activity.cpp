#include "ui/mcp_activity.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>

namespace patchy::ui {
McpActivity::McpActivity(MainWindow& window, std::function<void()> stop, bool script)
    : QWidget(window.statusBar()), window_(window), label_(new QLabel(this)),
      stop_(new QPushButton(this)), slow_(new QPushButton(this)), script_(script) {
  setObjectName(script ? QStringLiteral("scriptActivity") : QStringLiteral("mcpActivity"));
  label_->setObjectName(script ? QStringLiteral("scriptActivityLabel") : QStringLiteral("mcpActivityLabel"));
  label_->setTextFormat(Qt::PlainText);
  stop_->setObjectName(script ? QStringLiteral("scriptStopButton") : QStringLiteral("mcpStopButton"));
  label_->setMaximumWidth(280);
  stop_->setFocusPolicy(Qt::NoFocus);
  slow_->setObjectName(script ? QStringLiteral("scriptSlowButton") : QStringLiteral("mcpSlowButton"));
  slow_->setCheckable(true);
  slow_->setFocusPolicy(Qt::NoFocus);
  auto& host = window.script_engine_host();
  slow_->setChecked(host.slow_mode());
  connect(slow_, &QPushButton::toggled, &host, &ScriptEngineHost::set_slow_mode);
  connect(&host, &ScriptEngineHost::slow_mode_changed, slow_, &QPushButton::setChecked);
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(6, 0, 6, 0);
  row->setSpacing(6);
  row->addWidget(label_);
  row->addWidget(stop_);
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
  if (!working_) {
    menu_was_enabled_ = window_.menuBar()->isEnabled();
    window_.menuBar()->setEnabled(false);
  }
  working_ = true;
  editing_ = editing;
  refresh();
  // A synchronous operation may not return to the event loop for a while.
  // Paint the truthful working state before entering it, without input delivery.
  QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  repaint();
}
void McpActivity::finish_operation() {
  if (working_) { window_.menuBar()->setEnabled(menu_was_enabled_); }
  working_ = false;
  refresh();
}
void McpActivity::set_disconnected() {
  connected_ = false;
  finish_operation();
}
void McpActivity::refresh() {
  label_->setText(working_ ? (editing_ ? tr("AI editing: %1") : tr("AI reading: %1")).arg(operation_)
                          : tr("AI connected"));
  if (script_) { label_->setText(tr("Running script: %1").arg(operation_)); }
  setToolTip(working_ ? tr("%1 is using this workspace. Editing resumes when the request finishes. Stop keeps changes available for Undo.").arg(client_)
                     : tr("Connected to %1 through MCP. Waiting for a Patchy request; the assistant may still be thinking.").arg(client_));
  stop_->setText(tr("Stop"));
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
