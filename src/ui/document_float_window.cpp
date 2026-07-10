#include "ui/document_float_window.hpp"

#include "ui/action_icons.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"

#include <QCloseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QMoveEvent>
#include <QVBoxLayout>

namespace patchy::ui {

DocumentFloatWindow::DocumentFloatWindow(MainWindow& owner, CanvasWidget* canvas)
    : QWidget(&owner, Qt::Window), owner_(&owner), canvas_(canvas) {
  setObjectName(QStringLiteral("documentFloatWindow"));
  setWindowIcon(patchy_app_icon());
  setAcceptDrops(true);
  setMinimumSize(200, 150);
  auto* window_layout = new QVBoxLayout(this);
  window_layout->setContentsMargins(0, 0, 0, 0);
  window_layout->setSpacing(0);
  window_layout->addWidget(canvas_);
  // The canvas arrives hidden from QTabWidget::removeTab.
  canvas_->show();
}

CanvasWidget* DocumentFloatWindow::take_canvas() {
  auto* canvas = canvas_;
  canvas_ = nullptr;
  if (canvas != nullptr) {
    if (layout() != nullptr) {
      layout()->removeWidget(canvas);
    }
    canvas->setParent(nullptr);
  }
  return canvas;
}

void DocumentFloatWindow::closeEvent(QCloseEvent* event) {
  if (owner_ == nullptr || canvas_ == nullptr) {
    // Already released (re-dock or session close in progress); nothing to ask.
    event->accept();
    return;
  }
  if (qGuiApp != nullptr && qGuiApp->isSavingSession()) {
    // OS session end delivers closeEvent to every top-level window. Quit must
    // stay one atomic decision in MainWindow::closeEvent (Cancel there keeps
    // every document), so a float only hides; the main window re-shows it if
    // the user cancels the shutdown.
    event->accept();
    return;
  }
  // The full session-close flow runs (smart-object children, save prompt); on
  // success the owner detaches the canvas and schedules this window's deletion.
  if (owner_->handle_float_window_close_request(this)) {
    event->accept();
  } else {
    event->ignore();
  }
}

bool DocumentFloatWindow::event(QEvent* event) {
  if (event->type() == QEvent::WindowActivate && owner_ != nullptr && canvas_ != nullptr) {
    owner_->handle_float_window_activated(this);
  }
  return QWidget::event(event);
}

void DocumentFloatWindow::moveEvent(QMoveEvent* event) {
  QWidget::moveEvent(event);
  if (owner_ != nullptr && canvas_ != nullptr) {
    // Dragging the window over the tab bar docks it on release (the owner arms a
    // settle check; programmatic moves never do).
    owner_->handle_float_window_drag_moved(this);
  }
}

void DocumentFloatWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (owner_ == nullptr || !owner_->accept_open_file_drag(event)) {
    QWidget::dragEnterEvent(event);
  }
}

void DocumentFloatWindow::dragMoveEvent(QDragMoveEvent* event) {
  if (owner_ == nullptr || !owner_->accept_open_file_drag(event)) {
    QWidget::dragMoveEvent(event);
  }
}

void DocumentFloatWindow::dropEvent(QDropEvent* event) {
  if (owner_ == nullptr || !owner_->open_dropped_files(event)) {
    QWidget::dropEvent(event);
  }
}

}  // namespace patchy::ui
