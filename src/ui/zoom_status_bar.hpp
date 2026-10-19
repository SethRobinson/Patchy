#pragma once

#include <QElapsedTimer>
#include <QLineEdit>
#include <QStatusBar>
#include <QTimer>

class QPainter;

namespace patchy::ui {

// A QAction property marking view-only navigation (the status-bar zoom presets)
// that the MCP input guard lets through while automation runs.
inline constexpr char kViewNavigationActionProperty[] = "patchy.viewNavigation";

// Photoshop-style zoom percentage box for the status bar: shows the canvas zoom
// as e.g. "300%" and lets the user click it, type a new percentage, and press
// Enter to apply it. Escape (or invalid input on focus loss) reverts to the
// last displayed value. The host applies the committed value and pushes the
// resulting zoom back in through set_display_zoom().
//
// It also works like an options-bar field (GitHub issue 159): the wheel and the
// Up/Down keys step along the zoom ladder, and a horizontal drag that starts on
// the unfocused box scrubs the zoom (right = in); a click without a drag still
// focuses it for typing.
//
// The box is as wide as its widest possible value needs in the current font, and
// that text may run up to the presets chevron (GitHub issue 237).
class ZoomPercentEdit final : public QLineEdit {
  Q_OBJECT

public:
  explicit ZoomPercentEdit(QWidget* parent = nullptr);

  // zoom is the canvas scale factor (1.0 = 100%). Skips the text update while
  // the user is typing (isModified), but always refreshes the revert value.
  void set_display_zoom(double zoom);
  void clear_display();
  [[nodiscard]] static QString format_zoom_text(double zoom);

  // The zoom factor one pixel of horizontal scrub travel applies.
  static constexpr double kScrubFactorPerPixel = 1.01;

signals:
  void zoom_percent_committed(double percent);
  // One ladder rung in (true) or out (false).
  void zoom_step_requested(bool zoom_in);
  // Multiply the current zoom by `factor`, about the view center.
  void zoom_scrub_requested(double factor);

protected:
  bool event(QEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  void commit_editor_text();
  void update_scrub_cursor();
  void refit_to_font();
  [[nodiscard]] int trailing_reserve() const;
  [[nodiscard]] int widest_display_width() const;

  QString display_text_;
  int wheel_remainder_{0};
  bool scrub_armed_{false};
  bool scrubbing_{false};
  int scrub_last_x_{0};
  int scrub_press_x_{0};
  bool refitting_{false};
};

// QStatusBar hides every non-permanent widget while a message is showing, and
// Patchy keeps a persistent message up almost all the time, so a widget added
// with addWidget() would never be seen. This subclass instead hosts one
// manually positioned child at the far left (outside the item layout, so
// QStatusBar never hides it) and repaints the temporary message offset to the
// widget's right instead of at the default x=6 where the widget would cover it.
class ZoomStatusBar final : public QStatusBar {
  Q_OBJECT

public:
  explicit ZoomStatusBar(QWidget* parent = nullptr);

  void set_left_widget(QWidget* widget);

  // Shows `text` exactly like showMessage() (same currentMessage semantics),
  // but marks it as a blocking-error message: the message region flashes red
  // for about a second, then the text stays red with a warning icon until any
  // different message replaces it. Re-showing the same error restarts the
  // flash (QStatusBar::showMessage ignores an identical string).
  void show_error_message(const QString& text);
  [[nodiscard]] bool error_message_active() const;
  [[nodiscard]] bool error_flash_running() const;

signals:
  // After every size change, so left-side widgets can refit their text.
  void resized();

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  [[nodiscard]] QSize minimumSizeHint() const override;

private:
  void position_left_widget();
  void paint_warning_icon(QPainter& painter, const QRect& icon_rect) const;

  QWidget* left_widget_{nullptr};
  QString error_text_;
  bool error_active_{false};
  QTimer error_flash_timer_;
  QElapsedTimer error_flash_clock_;
};

}  // namespace patchy::ui
