#pragma once
#include <QWidget>
#include <QPointer>
#include <functional>

class QLabel;
class QPushButton;
namespace patchy::ui {
class MainWindow;
class CanvasWidget;

// A permanent status-bar readout, also guarding manual input during a request.
// This never locks programmatic document operations or changes the active tab.
class McpActivity final : public QWidget {
  Q_OBJECT
 public:
  McpActivity(MainWindow& window, std::function<void()> stop, bool script = false);
  void set_connected(const QString& client);
  void set_operation(const QString& operation, bool editing);
  void finish_operation();
  void set_disconnected();
  [[nodiscard]] bool working() const { return working_; }
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
 private:
  void refresh();
  MainWindow& window_;
  QLabel* label_;
  QPushButton* stop_;
  QPushButton* pause_;
  QPushButton* slow_;
  QPointer<CanvasWidget> panning_canvas_;
  Qt::MouseButton pan_button_{Qt::NoButton};
  bool space_down_{false};
  QString client_;
  QString operation_;
  bool connected_{false};
  bool working_{false};
  bool editing_{false};
  bool script_{false};
};
}  // namespace patchy::ui
