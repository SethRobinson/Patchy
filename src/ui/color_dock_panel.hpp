#pragma once

// The right column's Color panel (docs/right-docks.md, "Color panel"; GitHub issue 365):
// the foreground/background pair with its swap and default glyphs, a saturation/value
// field and a hue strip. The field shows the foreground color. A click or drag sets the
// foreground; with Alt (Option on macOS) held at the press it sets the background, for
// the whole drag, as the Pick tool's Alt-click does. MainWindow applies each pick and
// pushes every color change back through set_colors.

#include "ui/color_field_widgets.hpp"

#include <QColor>
#include <QWidget>

class QAction;
class QEvent;

namespace patchy::ui {

class BackgroundPickCursor;
class ColorWellSwatch;
class ColorWellGlyphButton;

class ColorDockPanel final : public QWidget, public HsvFieldSource {
  Q_OBJECT

public:
  ColorDockPanel(QAction* swap_colors, QAction* default_colors, QWidget* parent = nullptr);

  // Shows the current pair. Cheap: it stores the colors and repaints only what changed.
  void set_colors(QColor foreground, QColor background);

  [[nodiscard]] ColorWellSwatch* foreground_chip() const noexcept { return foreground_chip_; }
  [[nodiscard]] ColorWellSwatch* background_chip() const noexcept { return background_chip_; }
  [[nodiscard]] ColorPlaneWidget* field() const noexcept { return field_; }
  [[nodiscard]] HueSliderWidget* hue_strip() const noexcept { return hue_strip_; }
  [[nodiscard]] BackgroundPickCursor* field_cursor() const noexcept { return field_cursor_; }
  [[nodiscard]] BackgroundPickCursor* hue_strip_cursor() const noexcept { return hue_strip_cursor_; }

  // HsvFieldSource: the foreground's hue, saturation and value.
  [[nodiscard]] int hue() const override { return hue_; }
  [[nodiscard]] int saturation() const override { return saturation_; }
  [[nodiscard]] int value() const override { return value_; }
  void begin_field_pick(Qt::KeyboardModifiers modifiers) override;
  void end_field_pick() override;
  void set_saturation_value_from_point(QPoint point, QSize size) override;
  void set_hue_from_point(QPoint point, QSize size) override;

signals:
  // A pick in the field or the hue strip, on the press and on every move of its drag.
  void color_picked(QColor color, bool background);

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslate();
  void adopt_foreground(QColor color);
  void emit_pick(QColor color);

  ColorWellSwatch* foreground_chip_{nullptr};
  ColorWellSwatch* background_chip_{nullptr};
  ColorWellGlyphButton* swap_{nullptr};
  ColorWellGlyphButton* default_{nullptr};
  ColorPlaneWidget* field_{nullptr};
  HueSliderWidget* hue_strip_{nullptr};
  BackgroundPickCursor* field_cursor_{nullptr};
  BackgroundPickCursor* hue_strip_cursor_{nullptr};
  int hue_{0};
  int saturation_{0};
  int value_{0};
  // The foreground this panel last produced: when the canvas hands the same color
  // back, the field keeps its own hue and saturation rather than re-deriving them
  // through RGB, which would move the crosshair under the pointer.
  QColor produced_foreground_;
  bool picking_{false};
  bool pick_background_{false};
};

}  // namespace patchy::ui
