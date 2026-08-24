#pragma once

#include <QTimer>
#include <QWidget>

namespace patchy::ui {

// Small rotating-arc activity indicator (16x16) painted in the
// dialog_busy_spinner theme role. set_active(true) shows it and starts its
// own timer; set_active(false) stops and hides it. Shared by the Script
// Manager's run status and the Trace Image dialog (non-Q_OBJECT).
class ActivitySpinner final : public QWidget {
public:
  explicit ActivitySpinner(QWidget* parent = nullptr);

  void set_active(bool active);
  [[nodiscard]] bool active() const noexcept;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void advance();

  QTimer timer_;
  int angle_{0};
};

}  // namespace patchy::ui
