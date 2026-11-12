#include "ui/shape_pixel_options.hpp"

#include "ui/canvas_widget.hpp"

#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QIcon>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace patchy::ui {

namespace {

bool contains(const std::vector<QWidget*>& widgets, const QWidget* widget) {
  return std::find(widgets.begin(), widgets.end(), widget) != widgets.end();
}

std::optional<QColor> color_from_setting(const QVariant& value) {
  const auto text = value.toString().trimmed();
  if (text.isEmpty()) {
    return std::nullopt;
  }
  QColor color(text);
  if (!color.isValid()) {
    return std::nullopt;
  }
  color.setAlpha(255);
  return color;
}

QString color_to_setting(const std::optional<QColor>& color) {
  return color.has_value() ? color->name(QColor::HexRgb) : QString();
}

}  // namespace

const char* shape_draw_mode_token(ShapeDrawMode mode) noexcept {
  switch (mode) {
    case ShapeDrawMode::Fill:
      return "fill";
    case ShapeDrawMode::Both:
      return "both";
    case ShapeDrawMode::Stroke:
      break;
  }
  return "stroke";
}

ShapeDrawMode shape_draw_mode_from_token(std::string_view token, ShapeDrawMode fallback) noexcept {
  for (const auto mode : {ShapeDrawMode::Stroke, ShapeDrawMode::Fill, ShapeDrawMode::Both}) {
    if (token == shape_draw_mode_token(mode)) {
      return mode;
    }
  }
  return fallback;
}

const char* stroke_alignment_token(patchy::VectorStrokeAlignment alignment) noexcept {
  switch (alignment) {
    case patchy::VectorStrokeAlignment::Inside:
      return "inside";
    case patchy::VectorStrokeAlignment::Outside:
      return "outside";
    case patchy::VectorStrokeAlignment::Center:
      break;
  }
  return "center";
}

patchy::VectorStrokeAlignment stroke_alignment_from_token(std::string_view token,
                                                          patchy::VectorStrokeAlignment fallback) noexcept {
  for (const auto alignment : {patchy::VectorStrokeAlignment::Inside, patchy::VectorStrokeAlignment::Center,
                               patchy::VectorStrokeAlignment::Outside}) {
    if (token == stroke_alignment_token(alignment)) {
      return alignment;
    }
  }
  return fallback;
}

bool ShapePixelOptions::widget_visible(const QWidget* widget) const {
  if (contains(ratio_widgets, widget)) {
    return size_style == MarqueeStyle::FixedRatio;
  }
  if (contains(size_widgets, widget)) {
    return size_style == MarqueeStyle::FixedSize;
  }
  return true;
}

bool ShapePixelOptions::widget_enabled(const QWidget* widget) const {
  if (contains(fill_widgets, widget)) {
    return fills();
  }
  if (contains(stroke_widgets, widget)) {
    return strokes();
  }
  return true;
}

QSize ShapePixelOptions::fixed_size_for_canvas() const {
  if (size_style == MarqueeStyle::FixedRatio) {
    // Thousandths, scaled down for an extreme ratio so the larger side stays inside the
    // canvas's 30000 ceiling (set_shape_fixed_size clamps, which would change the ratio).
    const auto scale = std::min(1000.0, 30000.0 / std::max({ratio_w, ratio_h, 0.01}));
    return QSize(std::max(1, static_cast<int>(std::lround(ratio_w * scale))),
                 std::max(1, static_cast<int>(std::lround(ratio_h * scale))));
  }
  return QSize(width, height);
}

void ShapePixelOptions::apply_to_canvas(CanvasWidget& canvas) const {
  canvas.set_shape_pixel_style(style);
  canvas.set_shape_style(size_style);
  const auto fixed = fixed_size_for_canvas();
  canvas.set_shape_fixed_size(fixed.width(), fixed.height());
}

void ShapePixelOptions::load(const QSettings& settings) {
  // The Fill checkbox's key first: checked filled the shape, unchecked outlined it.
  auto mode = style.mode;
  if (settings.contains(QStringLiteral("tools/shapeFill"))) {
    const auto stored = settings.value(QStringLiteral("tools/shapeFill"));
    const auto text = stored.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("1")) {
      mode = ShapeDrawMode::Fill;
    } else if (text == QLatin1String("false") || text == QLatin1String("0")) {
      mode = ShapeDrawMode::Stroke;
    }
  }
  style.mode = shape_draw_mode_from_token(
      settings.value(QStringLiteral("tools/shapeDrawMode")).toString().toStdString(), mode);
  style.fill_color = color_from_setting(settings.value(QStringLiteral("tools/shapePixelFillColor")));
  style.stroke_color = color_from_setting(settings.value(QStringLiteral("tools/shapePixelStrokeColor")));
  style.dashes = patchy::stroke_dash_preset_from_token(
      settings.value(QStringLiteral("tools/shapePixelStrokeDashes")).toString().toStdString());
  style.alignment = stroke_alignment_from_token(
      settings.value(QStringLiteral("tools/shapePixelStrokeAlign")).toString().toStdString(),
      patchy::VectorStrokeAlignment::Center);
}

void ShapePixelOptions::save(QSettings& settings) const {
  settings.setValue(QStringLiteral("tools/shapeDrawMode"), QString::fromLatin1(shape_draw_mode_token(style.mode)));
  // An older version reads only this: Both shows there as a checked Fill.
  settings.setValue(QStringLiteral("tools/shapeFill"), style.mode != ShapeDrawMode::Stroke);
  settings.setValue(QStringLiteral("tools/shapePixelFillColor"), color_to_setting(style.fill_color));
  settings.setValue(QStringLiteral("tools/shapePixelStrokeColor"), color_to_setting(style.stroke_color));
  settings.setValue(QStringLiteral("tools/shapePixelStrokeDashes"),
                    QString::fromUtf8(patchy::stroke_dash_preset_token(style.dashes)));
  settings.setValue(QStringLiteral("tools/shapePixelStrokeAlign"),
                    QString::fromLatin1(stroke_alignment_token(style.alignment)));
}

void ShapePixelOptions::sync_style_controls() const {
  for (std::size_t index = 0; index < mode_buttons.size(); ++index) {
    if (auto* button = mode_buttons[index]; button != nullptr) {
      const QSignalBlocker blocker(button);
      button->setChecked(index == static_cast<std::size_t>(style.mode));
    }
  }
  if (dash_combo != nullptr) {
    const QSignalBlocker blocker(dash_combo);
    dash_combo->setCurrentIndex(std::max(0, dash_combo->findData(static_cast<int>(style.dashes))));
  }
  if (align_combo != nullptr) {
    const QSignalBlocker blocker(align_combo);
    align_combo->setCurrentIndex(std::max(0, align_combo->findData(static_cast<int>(style.alignment))));
  }
}

void ShapePixelOptions::show_swatches(
    QColor foreground, const std::function<QIcon(const patchy::VectorFill&, const QToolButton&)>& chip) const {
  const auto show = [&](QToolButton* button, const std::optional<QColor>& own) {
    if (button == nullptr) {
      return;
    }
    const QColor color = own.value_or(foreground);
    patchy::VectorFill paint;
    paint.kind = patchy::VectorFillKind::Solid;
    paint.color = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
    button->setIcon(chip(paint, *button));
  };
  show(fill_swatch, style.fill_color);
  show(stroke_swatch, style.stroke_color);
}

void ShapePixelOptions::sync_brush_fields(const CanvasWidget& canvas) const {
  const auto put = [](QSpinBox* spin, int value) {
    if (spin != nullptr && spin->value() != value) {
      const QSignalBlocker blocker(spin);
      spin->setValue(value);
    }
  };
  put(stroke_width_spin, canvas.brush_size());
  put(opacity_spin, canvas.brush_opacity());
  put(softness_spin, canvas.brush_softness());
}

}  // namespace patchy::ui
