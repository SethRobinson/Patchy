#include "ui/mcp_activity.hpp"
#include "ui/main_window.hpp"
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>

namespace patchy::ui {
McpActivity::McpActivity(MainWindow& window, std::function<void()> stop)
    : QWidget(window.statusBar()), window_(window), label_(new QLabel(this)),
      stop_(new QPushButton(this)) {
  setObjectName(QStringLiteral("mcpActivity"));
  label_->setObjectName(QStringLiteral("mcpActivityLabel"));
  label_->setTextFormat(Qt::PlainText);
  stop_->setObjectName(QStringLiteral("mcpStopButton"));
  stop_->setFocusPolicy(Qt::NoFocus);
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(6, 0, 6, 0);
  row->setSpacing(6);
  row->addWidget(label_);
  row->addWidget(stop_);
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
  setToolTip(working_ ? tr("%1 is using this workspace. Editing resumes when the request finishes. Stop keeps changes available for Undo.").arg(client_)
                     : tr("Connected to %1 through MCP. Waiting for a Patchy request; the assistant may still be thinking.").arg(client_));
  stop_->setText(tr("Stop"));
  stop_->setVisible(working_ && editing_);
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
