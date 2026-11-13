#pragma once

#include "ui/canvas_types.hpp"

#include <QColor>
#include <QSize>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QSettings;
class QSpinBox;
class QWidget;

namespace patchy::ui {

class CanvasWidget;

// The shape tools' draw options that Shape and Pixels modes share beside MainWindow's fill
// and stroke mirrors (GitHub issues 716 and 697): the Fill only / Stroke only / Both buttons
// and the stroke alignment list; and the Pixels-only ones: Foreground Color as a paint, and
// the Style with its size pairs. It also records which of the bar's controls belong to them, so one row serves
// both modes. MainWindow holds one; build_options_bar creates the controls.
// docs/options-bar-shape-rows.md, "Shape tool rows"; docs/tools.md, "Shapes in Pixels mode".
struct ShapePixelOptions {
  // Foreground Color in the Fill or Stroke paint menu, Pixels mode only: pixel shapes paint
  // that part in the foreground color, as every pixel tool does, whatever the bar's own paint
  // is, until a paint is picked from the bar. Shape mode never reads these: there the bar
  // shows the selected shape's paints and hands them to the next shape, which a paint that
  // follows the foreground cannot also do.
  bool fill_follows_foreground{false};
  bool stroke_follows_foreground{false};
  // The alignment the list set, for new shapes and for Pixels mode. Unset is the default:
  // Inside (a line's stroke is centered whatever this says, docs/vector-tools-lines.md).
  std::optional<patchy::VectorStrokeAlignment> alignment;
  // The fill paint Fill only and Both bring back after Stroke only set it to none.
  patchy::VectorFillKind fill_restore_kind{patchy::VectorFillKind::Solid};
  // The tiles of the bar's pattern paints for Pixels mode, kept until a paint changes.
  std::shared_ptr<const patchy::PatternStore> pattern_store;
  std::string pattern_store_key;
  // The style last handed to a canvas, and that canvas: it is sent again only when it
  // changes, so a style a script or a test set on the canvas itself stays until then.
  ShapePixelStyle pushed_style;
  const CanvasWidget* pushed_canvas{nullptr};

  // Style (Pixels mode, Rectangle and Ellipse): Normal, Fixed Ratio (ratio_w : ratio_h) or
  // Fixed Size (width x height pixels). Session only, like the marquee's.
  MarqueeStyle size_style{MarqueeStyle::Normal};
  int width{1024};
  int height{768};
  double ratio_w{1.0};
  double ratio_h{1.0};

  // Controls, owned by the options bar. Empty until build_options_bar ran.
  // The draw-mode buttons and the Fill and Stroke groups: Shape mode shows them, and so does
  // Pixels mode on the tools that paint pixels with them (shape_tool_has_draw_modes).
  std::vector<QWidget*> appearance_widgets;
  std::vector<QWidget*> fill_widgets;    // greyed while no fill is drawn (Stroke only)
  std::vector<QWidget*> stroke_widgets;  // greyed while no stroke is drawn (Fill only)
  std::vector<QWidget*> ratio_widgets;   // shown for Fixed Ratio
  std::vector<QWidget*> size_widgets;    // shown for Fixed Size
  QButtonGroup* mode_group{nullptr};
  std::array<QAbstractButton*, 3> mode_buttons{};  // indexed by ShapeDrawMode
  QComboBox* align_combo{nullptr};
  // Opacity and Soft (Pixels mode) are the brush's opacity and softness, mirrored.
  QSpinBox* opacity_spin{nullptr};
  QSpinBox* softness_spin{nullptr};

  // False for the size pair the Style does not use (Normal shows neither).
  [[nodiscard]] bool widget_visible(const QWidget* widget) const;
  // On a tool with the draw-mode buttons: false for the Fill group while no fill is drawn and
  // for the Stroke group while no stroke is. The stroke's width field is not in that group:
  // it is the stroke's switch (0 is off), so a typed width turns the stroke back on.
  [[nodiscard]] bool widget_enabled(const QWidget* widget, bool draws_fill, bool draws_stroke) const;
  // What CanvasWidget::set_shape_fixed_size takes: the pixel size, or the ratio as a pair of
  // whole numbers (thousandths, so 1.5 : 1 survives).
  [[nodiscard]] QSize fixed_size_for_canvas() const;
  // The Style and its size on a canvas (the pixel style comes from MainWindow).
  void apply_to_canvas(CanvasWidget& canvas) const;

  // tools/vectorFillFollowsForeground, tools/vectorStrokeFollowsForeground and
  // tools/vectorStrokeAlign (token; absent for the default).
  void load(const QSettings& settings);
  void save(QSettings& settings) const;

  // Checks the button for what is drawn (none when neither is) and puts the alignment list on
  // `alignment`, signals blocked.
  void sync_draw_controls(bool draws_fill, bool draws_stroke, patchy::VectorStrokeAlignment shown) const;
  // Puts the mirrored brush fields on the canvas's values, signals blocked.
  void sync_brush_fields(const CanvasWidget& canvas) const;
};

// The tools with the Fill only / Stroke only / Both buttons, which also paint pixels with the
// bar's fill and stroke in Pixels mode: Rectangle, Ellipse, Polygon and Custom Shape. Line
// keeps its brush-like Pixels bar and the Pen is vector only.
[[nodiscard]] bool shape_tool_has_draw_modes(CanvasTool tool) noexcept;

// The Pixels-mode values of a settings store written before Shape and Pixels modes shared
// one fill and stroke: tools/shapeDrawMode (or tools/shapeFill, the Fill checkbox before it:
// checked was Fill only), tools/shapePixelFillColor and tools/shapePixelStrokeColor ("#rrggbb",
// absent or empty for the foreground color), tools/shapePixelStrokeDashes (a dash preset
// token) and tools/shapePixelStrokeAlign (token, Center when absent). Read once, when
// tools/vectorFillFollowsForeground is absent and the shape tools were left in Pixels mode,
// so their next pixel shape looks as the last one did. Only tools/shapeFill is still written.
struct LegacyShapePixelStyle {
  ShapeDrawMode mode{ShapeDrawMode::Stroke};
  std::optional<QColor> fill_color;
  std::optional<QColor> stroke_color;
  patchy::StrokeDashPreset dashes{patchy::StrokeDashPreset::Solid};
  patchy::VectorStrokeAlignment alignment{patchy::VectorStrokeAlignment::Center};
};
[[nodiscard]] bool shape_settings_predate_shared_style(const QSettings& settings);
[[nodiscard]] LegacyShapePixelStyle legacy_shape_pixel_style(const QSettings& settings);

// Settings tokens (persisted: never rename one). An unknown token reads as `fallback`.
[[nodiscard]] const char* shape_draw_mode_token(ShapeDrawMode mode) noexcept;
[[nodiscard]] ShapeDrawMode shape_draw_mode_from_token(std::string_view token, ShapeDrawMode fallback) noexcept;
[[nodiscard]] const char* stroke_alignment_token(patchy::VectorStrokeAlignment alignment) noexcept;
[[nodiscard]] patchy::VectorStrokeAlignment stroke_alignment_from_token(
    std::string_view token, patchy::VectorStrokeAlignment fallback) noexcept;

}  // namespace patchy::ui
