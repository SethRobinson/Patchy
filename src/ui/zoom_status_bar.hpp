#pragma once

#include <QElapsedTimer>
#include <QLineEdit>
#include <QStatusBar>
#include <QTimer>

class QPainter;

namespace patchy::ui {

// Photoshop-style zoom percentage box for the status bar: shows the canvas zoom
// as e.g. "300%" and lets the user click it, type a new percentage, and press
// Enter to apply it. Escape (or invalid input on focus loss) reverts to the
// last displayed value. The host applies the committed value and pushes the
// resulting zoom back in through set_display_zoom().
class ZoomPercentEdit final : public QLineEdit {
  Q_OBJECT

public:
  explicit ZoomPercentEdit(QWidget* parent = nullptr);

  // zoom is the canvas scale factor (1.0 = 100%). Skips the text update while
  // the user is typing (isModified), but always refreshes the revert value.
  void set_display_zoom(double zoom);
  void clear_display();
  [[nodiscard]] static QString format_zoom_text(double zoom);

signals:
  void zoom_percent_committed(double percent);

protected:
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  void commit_editor_text();

  QString display_text_;
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
