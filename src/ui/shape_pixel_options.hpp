#pragma once

#include "ui/canvas_types.hpp"

#include <QSize>
#include <QString>

#include <array>
#include <functional>
#include <string_view>
#include <vector>

class QAbstractButton;
class QComboBox;
class QIcon;
class QSettings;
class QSpinBox;
class QToolButton;
class QWidget;

namespace patchy::ui {

class CanvasWidget;

// The Rectangle and Ellipse tools' Pixels-mode options (GitHub issues 716 and 697): what the
// options bar's Pixels row sets, which of its controls show and are enabled, and the saved
// settings. MainWindow holds one; build_options_bar creates the controls and records them
// here. docs/options-bar-shape-rows.md, "Shape tool rows"; docs/tools.md, "Shapes in Pixels mode".
struct ShapePixelOptions {
  // The draw mode, the fill's and the stroke's own colors, the dashes and the alignment.
  ShapePixelStyle style;
  // Style: Normal, Fixed Ratio (ratio_w : ratio_h) or Fixed Size (width x height pixels).
  // Session only, like the marquee's.
  MarqueeStyle size_style{MarqueeStyle::Normal};
  int width{1024};
  int height{768};
  double ratio_w{1.0};
  double ratio_h{1.0};

  // Controls, owned by the options bar. Empty until build_options_bar ran.
  std::vector<QWidget*> fill_widgets;    // greyed for Stroke only
  std::vector<QWidget*> stroke_widgets;  // greyed for Fill only
  std::vector<QWidget*> ratio_widgets;   // shown for Fixed Ratio
  std::vector<QWidget*> size_widgets;    // shown for Fixed Size
  std::array<QAbstractButton*, 3> mode_buttons{};  // indexed by ShapeDrawMode
  QToolButton* fill_swatch{nullptr};
  QToolButton* stroke_swatch{nullptr};
  QComboBox* dash_combo{nullptr};
  QComboBox* align_combo{nullptr};
  // Stroke width, Opacity and Soft are the brush's size, opacity and softness (the values these
  // tools always used); these fields mirror the brush fields.
  QSpinBox* stroke_width_spin{nullptr};
  QSpinBox* opacity_spin{nullptr};
  QSpinBox* softness_spin{nullptr};

  [[nodiscard]] bool fills() const noexcept { return style.mode != ShapeDrawMode::Stroke; }
  [[nodiscard]] bool strokes() const noexcept { return style.mode != ShapeDrawMode::Fill; }
  // False for the size pair the Style does not use (Normal shows neither).
  [[nodiscard]] bool widget_visible(const QWidget* widget) const;
  // False for the Fill controls under Stroke only and the Stroke controls under Fill only.
  [[nodiscard]] bool widget_enabled(const QWidget* widget) const;
  // What CanvasWidget::set_shape_fixed_size takes: the pixel size, or the ratio as a pair of
  // whole numbers (thousandths, so 1.5 : 1 survives).
  [[nodiscard]] QSize fixed_size_for_canvas() const;
  void apply_to_canvas(CanvasWidget& canvas) const;

  // tools/shapeDrawMode (token), tools/shapePixelFillColor and tools/shapePixelStrokeColor
  // ("#rrggbb", empty for the foreground color), tools/shapePixelStrokeDashes (a
  // stroke_dash_preset_token) and tools/shapePixelStrokeAlign (token). tools/shapeFill is the
  // Fill checkbox's old key: it is read when tools/shapeDrawMode is absent (checked was Fill
  // only, unchecked Stroke only) and still written for older versions.
  void load(const QSettings& settings);
  void save(QSettings& settings) const;

  // Puts the mode buttons and the dash and alignment lists on `style`, signals blocked.
  void sync_style_controls() const;
  // Shows each swatch's color: the paint's own, or `foreground` while it follows the
  // foreground color. `chip` draws the options bar's swatch chip for a paint
  // (MainWindow::options_bar_swatch_icon); call again when the foreground color, a paint's
  // color or the color scheme changes.
  void show_swatches(QColor foreground,
                     const std::function<QIcon(const patchy::VectorFill&, const QToolButton&)>& chip) const;
  // Puts the mirrored brush fields on the canvas's values, signals blocked.
  void sync_brush_fields(const CanvasWidget& canvas) const;
};

// Settings tokens (persisted: never rename one). An unknown token reads as `fallback`.
[[nodiscard]] const char* shape_draw_mode_token(ShapeDrawMode mode) noexcept;
[[nodiscard]] ShapeDrawMode shape_draw_mode_from_token(std::string_view token, ShapeDrawMode fallback) noexcept;
[[nodiscard]] const char* stroke_alignment_token(patchy::VectorStrokeAlignment alignment) noexcept;
[[nodiscard]] patchy::VectorStrokeAlignment stroke_alignment_from_token(
    std::string_view token, patchy::VectorStrokeAlignment fallback) noexcept;

}  // namespace patchy::ui
