#include "ui/shape_pixel_options.hpp"

#include "ui/canvas_widget.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
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

// A stored flag; only the forms QSettings writes (true/false, 1/0) count.
std::optional<bool> flag_from_setting(const QVariant& value) {
  const auto text = value.toString().trimmed().toLower();
  if (text == QLatin1String("true") || text == QLatin1String("1")) {
    return true;
  }
  if (text == QLatin1String("false") || text == QLatin1String("0")) {
    return false;
  }
  return std::nullopt;
}

constexpr auto kFillFollowsForegroundKey = "tools/vectorFillFollowsForeground";
constexpr auto kStrokeFollowsForegroundKey = "tools/vectorStrokeFollowsForeground";
constexpr auto kStrokeAlignKey = "tools/vectorStrokeAlign";

}  // namespace

bool shape_tool_has_draw_modes(CanvasTool tool) noexcept {
  return tool == CanvasTool::Rectangle || tool == CanvasTool::Ellipse || tool == CanvasTool::Polygon ||
         tool == CanvasTool::CustomShape;
}

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

bool shape_settings_predate_shared_style(const QSettings& settings) {
  return !settings.contains(QLatin1String(kFillFollowsForegroundKey));
}

LegacyShapePixelStyle legacy_shape_pixel_style(const QSettings& settings) {
  LegacyShapePixelStyle legacy;
  // The Fill checkbox's key first: checked filled the shape, unchecked outlined it.
  if (const auto filled = flag_from_setting(settings.value(QStringLiteral("tools/shapeFill"))); filled.has_value()) {
    legacy.mode = *filled ? ShapeDrawMode::Fill : ShapeDrawMode::Stroke;
  }
  legacy.mode = shape_draw_mode_from_token(
      settings.value(QStringLiteral("tools/shapeDrawMode")).toString().toStdString(), legacy.mode);
  legacy.fill_color = color_from_setting(settings.value(QStringLiteral("tools/shapePixelFillColor")));
  legacy.stroke_color = color_from_setting(settings.value(QStringLiteral("tools/shapePixelStrokeColor")));
  legacy.dashes = patchy::stroke_dash_preset_from_token(
      settings.value(QStringLiteral("tools/shapePixelStrokeDashes")).toString().toStdString());
  legacy.alignment = stroke_alignment_from_token(
      settings.value(QStringLiteral("tools/shapePixelStrokeAlign")).toString().toStdString(),
      patchy::VectorStrokeAlignment::Center);
  return legacy;
}

bool ShapePixelOptions::widget_visible(const QWidget* widget, CanvasTool tool) const {
  if (contains(ratio_widgets, widget)) {
    return size_style == MarqueeStyle::FixedRatio;
  }
  if (contains(size_widgets, widget)) {
    // The Polygon's drag is not a box, so it has no Style: its pair shows a live size only.
    if (tool == CanvasTool::Polygon) {
      return size_live;
    }
    return size_style == MarqueeStyle::FixedSize || (size_style == MarqueeStyle::Normal && size_live);
  }
  if (shape_tool_has_draw_modes(tool)) {
    if (contains(fill_widgets, widget)) {
      return shown_fill;
    }
    if (contains(stroke_widgets, widget)) {
      return shown_stroke;
    }
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
  canvas.set_shape_style(size_style);
  const auto fixed = fixed_size_for_canvas();
  canvas.set_shape_fixed_size(fixed.width(), fixed.height());
}

void ShapePixelOptions::load(const QSettings& settings) {
  legacy_fill_foreground = flag_from_setting(settings.value(QLatin1String(kFillFollowsForegroundKey))).value_or(false);
  legacy_stroke_foreground =
      flag_from_setting(settings.value(QLatin1String(kStrokeFollowsForegroundKey))).value_or(false);
  alignment.reset();
  const auto token = settings.value(QLatin1String(kStrokeAlignKey)).toString().toStdString();
  for (const auto candidate : {patchy::VectorStrokeAlignment::Inside, patchy::VectorStrokeAlignment::Center,
                               patchy::VectorStrokeAlignment::Outside}) {
    if (token == stroke_alignment_token(candidate)) {
      alignment = candidate;
    }
  }
}

void ShapePixelOptions::save(QSettings& settings) const {
  // Written false from now on: they also mark the settings as newer than the shared style.
  settings.setValue(QLatin1String(kFillFollowsForegroundKey), false);
  settings.setValue(QLatin1String(kStrokeFollowsForegroundKey), false);
  if (alignment.has_value()) {
    settings.setValue(QLatin1String(kStrokeAlignKey), QString::fromLatin1(stroke_alignment_token(*alignment)));
  } else {
    settings.remove(QLatin1String(kStrokeAlignKey));
  }
}

void ShapePixelOptions::sync_draw_controls(bool draws_fill, bool draws_stroke,
                                           patchy::VectorStrokeAlignment shown) const {
  int checked = -1;
  if (draws_fill && draws_stroke) {
    checked = static_cast<int>(ShapeDrawMode::Both);
  } else if (draws_fill) {
    checked = static_cast<int>(ShapeDrawMode::Fill);
  } else if (draws_stroke) {
    checked = static_cast<int>(ShapeDrawMode::Stroke);
  }
  if (mode_group != nullptr) {
    // An exclusive group refuses to uncheck its checked button, and nothing is checked while
    // the shape draws neither a fill nor a stroke.
    const QSignalBlocker group_blocker(mode_group);
    mode_group->setExclusive(false);
    for (std::size_t index = 0; index < mode_buttons.size(); ++index) {
      if (auto* button = mode_buttons[index]; button != nullptr) {
        const QSignalBlocker blocker(button);
        button->setChecked(static_cast<int>(index) == checked);
      }
    }
    mode_group->setExclusive(true);
  }
  if (align_combo != nullptr) {
    const QSignalBlocker blocker(align_combo);
    align_combo->setCurrentIndex(std::max(0, align_combo->findData(static_cast<int>(shown))));
  }
}

void ShapePixelOptions::sync_brush_fields(const CanvasWidget& canvas) const {
  const auto put = [](QSpinBox* spin, int value) {
    if (spin != nullptr && spin->value() != value) {
      const QSignalBlocker blocker(spin);
      spin->setValue(value);
    }
  };
  put(opacity_spin, canvas.brush_opacity());
  put(softness_spin, canvas.brush_softness());
}

}  // namespace patchy::ui
