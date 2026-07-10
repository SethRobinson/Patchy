#pragma once

#include <QWidget>

namespace patchy::ui {

class CanvasWidget;
class MainWindow;

// Top-level window hosting one document's CanvasWidget when the document is
// floated out of the tab bar (Window > Float in Window). Parented to the
// MainWindow with the Qt::Window flag: an owned native-frame window gets no
// separate taskbar entry, minimizes with its owner, inherits the app style, and
// is torn down with the widget tree so it can never outlive the main window or
// block lastWindowClosed. All document semantics (activation, close prompts,
// titles) live in MainWindow; this class only hosts the canvas and forwards
// window events.
class DocumentFloatWindow final : public QWidget {
  Q_OBJECT

public:
  DocumentFloatWindow(MainWindow& owner, CanvasWidget* canvas);

  [[nodiscard]] CanvasWidget* canvas() const noexcept { return canvas_; }
  // Detaches the canvas for re-docking (or deletion on close); the window is
  // inert afterwards and should be hidden + deleteLater'd by the caller.
  CanvasWidget* take_canvas();

protected:
  void closeEvent(QCloseEvent* event) override;
  bool event(QEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  MainWindow* owner_{nullptr};
  CanvasWidget* canvas_{nullptr};
};

}  // namespace patchy::ui
