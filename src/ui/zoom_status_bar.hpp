#pragma once

#include <QLineEdit>
#include <QStatusBar>

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

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  [[nodiscard]] QSize minimumSizeHint() const override;

private:
  void position_left_widget();

  QWidget* left_widget_{nullptr};
};

}  // namespace patchy::ui
