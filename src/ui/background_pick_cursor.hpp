#pragma once

// Shows the Pick tool's background cursor (eyedropper_target_cursor(true)) over a widget
// where an Alt-click sets the background color instead of the foreground: the Color
// panel's field and hue strip and the Palette panel's swatches (GitHub issue 365).
// While the pointer is over the widget, holding Alt (Option on macOS) swaps the widget's
// own cursor for the background cursor and releasing Alt swaps it back, on the key
// itself as well as on mouse moves. A left press fixes the cursor until its release, as
// the press fixes which color the drag sets. Modifiers are read from each event, never
// from QApplication::keyboardModifiers().

#include <QCursor>
#include <QObject>

class QEvent;
class QWidget;

namespace patchy::ui {

class BackgroundPickCursor final : public QObject {
  Q_OBJECT

public:
  // Watches `widget` (and parents itself to it). The widget's cursor at this call is the
  // one shown without Alt.
  explicit BackgroundPickCursor(QWidget* widget);
  ~BackgroundPickCursor() override;

  [[nodiscard]] bool showing_background_cursor() const noexcept { return showing_background_; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void set_hovered(bool hovered);
  void show_background_cursor(bool background);

  QWidget* widget_{nullptr};
  QCursor normal_cursor_;
  bool hovered_{false};
  bool pressed_{false};
  bool showing_background_{false};
};

}  // namespace patchy::ui
