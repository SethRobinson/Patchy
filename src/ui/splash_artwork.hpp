#pragma once

#include <QWidget>

namespace patchy::ui {

// The painted Patchy logo card shared by the About dialog and the start panel.
// Callers size it (setFixedSize/layout); the painting scales to the widget rect.
class SplashArtwork final : public QWidget {
 public:
  explicit SplashArtwork(QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
};

}  // namespace patchy::ui
