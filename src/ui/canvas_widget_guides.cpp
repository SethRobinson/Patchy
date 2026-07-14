// CanvasWidget's rulers/grid/guides/snapping implementation, split out of
// canvas_widget.cpp: the selection-edges / rulers / grid / guides / snap
// settings accessors and toggles, the ruler unit menu, guide add/clear/
// selection, the snapping math (snapped_document_point/_f,
// append_snap_target_candidates, snapped_rect_delta,
// snapped_marquee_current_point, snapped_move_delta), and the guide drag
// lifecycle. Pure function moves from canvas_widget.cpp; behavior must stay
// identical.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kSnapToleranceScreenPixels = 8.0;

double guide_position_pixels(const DocumentGuide& guide) noexcept {
  return static_cast<double>(guide.position_32) / 32.0;
}

std::int32_t guide_position_32(double pixels) noexcept {
  return static_cast<std::int32_t>(std::lround(pixels * 32.0));
}

void append_rect_snap_candidates(QRect rect, std::vector<double>& x_candidates, std::vector<double>& y_candidates) {
  if (rect.isEmpty()) {
    return;
  }
  const auto left = static_cast<double>(rect.left());
  const auto top = static_cast<double>(rect.top());
  const auto right = static_cast<double>(rect.right() + 1);
  const auto bottom = static_cast<double>(rect.bottom() + 1);
  x_candidates.push_back(left);
  x_candidates.push_back((left + right) * 0.5);
  x_candidates.push_back(right);
  y_candidates.push_back(top);
  y_candidates.push_back((top + bottom) * 0.5);
  y_candidates.push_back(bottom);
}

void append_layer_snap_candidates(const std::vector<Layer>& layers, std::vector<double>& x_candidates,
                                  std::vector<double>& y_candidates) {
  for (const auto& layer : layers) {
    if (!layer.visible()) {
      continue;
    }
    append_rect_snap_candidates(to_qrect(layer_render_bounds(layer)), x_candidates, y_candidates);
    if (layer.kind() == LayerKind::Group) {
      append_layer_snap_candidates(layer.children(), x_candidates, y_candidates);
    }
  }
}

}  // namespace

void CanvasWidget::set_selection_edges_visible(bool visible) noexcept {
  if (selection_edges_visible_ == visible) {
    return;
  }
  selection_edges_visible_ = visible;
  update();
}

bool CanvasWidget::selection_edges_visible() const noexcept {
  return selection_edges_visible_;
}

void CanvasWidget::toggle_selection_edges_visible() {
  set_selection_edges_visible(!selection_edges_visible_);
  if (status_callback_) {
    status_callback_(selection_edges_visible_ ? tr("Showing selection edges") : tr("Hiding selection edges"));
  }
}

void CanvasWidget::set_rulers_visible(bool visible) noexcept {
  if (rulers_visible_ == visible) {
    return;
  }
  rulers_visible_ = visible;
  update();
}

bool CanvasWidget::rulers_visible() const noexcept {
  return rulers_visible_;
}

void CanvasWidget::set_ruler_unit(MeasurementUnit unit) noexcept {
  if (ruler_unit_ == unit) {
    return;
  }
  ruler_unit_ = unit;
  if (rulers_visible_) {
    update();
  }
}

MeasurementUnit CanvasWidget::ruler_unit() const noexcept {
  return ruler_unit_;
}

void CanvasWidget::set_ruler_unit_change_requested_callback(std::function<void(MeasurementUnit)> callback) {
  ruler_unit_change_requested_callback_ = std::move(callback);
}

double CanvasWidget::ruler_pixels_per_unit(bool horizontal_axis) const noexcept {
  if (document_ == nullptr) {
    return 1.0;
  }
  switch (ruler_unit_) {
    case MeasurementUnit::Pixels:
      return 1.0;
    case MeasurementUnit::Percent:
      return std::max(horizontal_axis ? document_->width() : document_->height(), 1) / 100.0;
    default: {
      const auto& print_settings = document_->print_settings();
      const auto ppi = sanitized_document_ppi(horizontal_axis ? print_settings.horizontal_ppi
                                                              : print_settings.vertical_ppi);
      return ppi / measurement_units_per_inch(ruler_unit_);
    }
  }
}

void CanvasWidget::show_ruler_unit_menu(QPoint global_position) {
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("canvasRulerUnitMenu"));
  for (const auto unit : {MeasurementUnit::Pixels, MeasurementUnit::Inches, MeasurementUnit::Centimeters,
                          MeasurementUnit::Millimeters, MeasurementUnit::Points, MeasurementUnit::Percent}) {
    auto* action = menu.addAction(measurement_unit_name(unit));
    action->setCheckable(true);
    action->setChecked(unit == ruler_unit_);
    connect(action, &QAction::triggered, this, [this, unit] {
      if (ruler_unit_change_requested_callback_) {
        ruler_unit_change_requested_callback_(unit);
      } else {
        set_ruler_unit(unit);
      }
    });
  }
  menu.exec(global_position);
}

void CanvasWidget::set_grid_visible(bool visible) noexcept {
  if (grid_visible_ == visible) {
    return;
  }
  grid_visible_ = visible;
  update();
}

bool CanvasWidget::grid_visible() const noexcept {
  return grid_visible_;
}

void CanvasWidget::set_guides_visible(bool visible) noexcept {
  if (guides_visible_ == visible) {
    return;
  }
  guides_visible_ = visible;
  update();
}

bool CanvasWidget::guides_visible() const noexcept {
  return guides_visible_;
}

void CanvasWidget::set_guides_locked(bool locked) noexcept {
  guides_locked_ = locked;
  update_tool_cursor();
}

bool CanvasWidget::guides_locked() const noexcept {
  return guides_locked_;
}

void CanvasWidget::set_snap_enabled(bool enabled) noexcept {
  snap_enabled_ = enabled;
}

bool CanvasWidget::snap_enabled() const noexcept {
  return snap_enabled_;
}

void CanvasWidget::set_snap_to_guides(bool enabled) noexcept {
  snap_to_guides_ = enabled;
}

bool CanvasWidget::snap_to_guides() const noexcept {
  return snap_to_guides_;
}

void CanvasWidget::set_snap_to_grid(bool enabled) noexcept {
  snap_to_grid_ = enabled;
}

bool CanvasWidget::snap_to_grid() const noexcept {
  return snap_to_grid_;
}

void CanvasWidget::set_snap_to_document(bool enabled) noexcept {
  snap_to_document_ = enabled;
}

bool CanvasWidget::snap_to_document() const noexcept {
  return snap_to_document_;
}

void CanvasWidget::set_snap_to_layers(bool enabled) noexcept {
  snap_to_layers_ = enabled;
}

bool CanvasWidget::snap_to_layers() const noexcept {
  return snap_to_layers_;
}

void CanvasWidget::set_snap_to_selection(bool enabled) noexcept {
  snap_to_selection_ = enabled;
}

bool CanvasWidget::snap_to_selection() const noexcept {
  return snap_to_selection_;
}

void CanvasWidget::set_grid_subdivisions(int subdivisions) noexcept {
  grid_subdivisions_ = std::clamp(subdivisions, 1, 64);
  update();
}

int CanvasWidget::grid_subdivisions() const noexcept {
  return grid_subdivisions_;
}

void CanvasWidget::set_grid_style(int style) noexcept {
  grid_style_ = std::clamp(style, 0, 1);
  update();
}

int CanvasWidget::grid_style() const noexcept {
  return grid_style_;
}

void CanvasWidget::set_grid_color(QColor color) noexcept {
  if (!color.isValid()) {
    return;
  }
  grid_color_ = color;
  update();
}

QColor CanvasWidget::grid_color() const noexcept {
  return grid_color_;
}

void CanvasWidget::set_guide_color(QColor color) noexcept {
  if (!color.isValid()) {
    return;
  }
  guide_color_ = color;
  update();
}

QColor CanvasWidget::guide_color() const noexcept {
  return guide_color_;
}

void CanvasWidget::add_guide(GuideOrientation orientation, std::int32_t position_32) {
  if (document_ == nullptr) {
    return;
  }
  const auto limit_32 = (orientation == GuideOrientation::Vertical ? document_->width() : document_->height()) * 32;
  DocumentGuide guide{orientation, std::clamp(position_32, 0, limit_32)};
  if (before_edit_callback_) {
    before_edit_callback_(tr("New Guide"));
  }
  document_->guides().push_back(guide);
  selected_guide_index_ = static_cast<int>(document_->guides().size()) - 1;
  document_changed();
}

void CanvasWidget::clear_guides() {
  if (document_ == nullptr || document_->guides().empty() || guides_locked_) {
    return;
  }
  if (before_edit_callback_) {
    before_edit_callback_(tr("Clear Guides"));
  }
  document_->guides().clear();
  selected_guide_index_ = -1;
  document_changed();
}

void CanvasWidget::clear_selected_guides() {
  if (document_ == nullptr || guides_locked_ || selected_guide_index_ < 0 ||
      selected_guide_index_ >= static_cast<int>(document_->guides().size())) {
    return;
  }
  if (before_edit_callback_) {
    before_edit_callback_(tr("Clear Selected Guide"));
  }
  document_->guides().erase(document_->guides().begin() + selected_guide_index_);
  selected_guide_index_ = -1;
  document_changed();
}

bool CanvasWidget::has_selected_guides() const noexcept {
  return document_ != nullptr && selected_guide_index_ >= 0 &&
         selected_guide_index_ < static_cast<int>(document_->guides().size());
}

QPoint CanvasWidget::snapped_document_point(QPoint point) const {
  if (document_ == nullptr || !snap_enabled_) {
    return point;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto snapped_x = static_cast<double>(point.x());
  auto snapped_y = static_cast<double>(point.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  const auto consider_x = [&](double candidate) {
    const auto distance = std::abs(candidate - static_cast<double>(point.x()));
    if (distance <= best_x) {
      best_x = distance;
      snapped_x = candidate;
    }
  };
  const auto consider_y = [&](double candidate) {
    const auto distance = std::abs(candidate - static_cast<double>(point.y()));
    if (distance <= best_y) {
      best_y = distance;
      snapped_y = candidate;
    }
  };

  if (snap_to_guides_) {
    for (const auto& guide : document_->guides()) {
      if (guide.orientation == GuideOrientation::Vertical) {
        consider_x(guide_position_pixels(guide));
      } else {
        consider_y(guide_position_pixels(guide));
      }
    }
  }

  if (snap_to_grid_ && grid_visible_) {
    const auto step_x = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    const auto step_y = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    if (step_x > 0.0) {
      consider_x(std::round(static_cast<double>(point.x()) / step_x) * step_x);
    }
    if (step_y > 0.0) {
      consider_y(std::round(static_cast<double>(point.y()) / step_y) * step_y);
    }
  }

  if (snap_to_document_) {
    consider_x(0.0);
    consider_x(static_cast<double>(document_->width()) * 0.5);
    consider_x(static_cast<double>(document_->width()));
    consider_y(0.0);
    consider_y(static_cast<double>(document_->height()) * 0.5);
    consider_y(static_cast<double>(document_->height()));
  }

  std::vector<double> x_candidates;
  std::vector<double> y_candidates;
  if (snap_to_selection_ && !selection_.isEmpty()) {
    append_rect_snap_candidates(selection_.boundingRect(), x_candidates, y_candidates);
  }
  if (snap_to_layers_) {
    append_layer_snap_candidates(document_->layers(), x_candidates, y_candidates);
  }
  for (const auto candidate : x_candidates) {
    consider_x(candidate);
  }
  for (const auto candidate : y_candidates) {
    consider_y(candidate);
  }

  return QPoint(static_cast<int>(std::lround(snapped_x)), static_cast<int>(std::lround(snapped_y)));
}

QPointF CanvasWidget::snapped_document_point_f(QPointF point) const {
  const auto snapped = snapped_document_point(QPoint(static_cast<int>(std::lround(point.x())),
                                                    static_cast<int>(std::lround(point.y()))));
  return QPointF(snapped);
}

void CanvasWidget::append_snap_target_candidates(std::vector<double>& x_candidates,
                                                 std::vector<double>& y_candidates) const {
  if (document_ == nullptr) {
    return;
  }

  if (snap_to_guides_) {
    for (const auto& guide : document_->guides()) {
      if (guide.orientation == GuideOrientation::Vertical) {
        x_candidates.push_back(guide_position_pixels(guide));
      } else {
        y_candidates.push_back(guide_position_pixels(guide));
      }
    }
  }
  if (snap_to_document_) {
    x_candidates.push_back(0.0);
    x_candidates.push_back(static_cast<double>(document_->width()) * 0.5);
    x_candidates.push_back(static_cast<double>(document_->width()));
    y_candidates.push_back(0.0);
    y_candidates.push_back(static_cast<double>(document_->height()) * 0.5);
    y_candidates.push_back(static_cast<double>(document_->height()));
  }
  if (snap_to_selection_ && !selection_.isEmpty()) {
    append_rect_snap_candidates(selection_.boundingRect(), x_candidates, y_candidates);
  }
  if (snap_to_layers_) {
    append_layer_snap_candidates(document_->layers(), x_candidates, y_candidates);
  }
}

QPoint CanvasWidget::snapped_rect_delta(QRect source_rect, QPoint raw_delta) const {
  if (document_ == nullptr || !snap_enabled_ || source_rect.isEmpty()) {
    return raw_delta;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto adjusted_x = static_cast<double>(raw_delta.x());
  auto adjusted_y = static_cast<double>(raw_delta.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  std::vector<double> target_x;
  std::vector<double> target_y;
  append_snap_target_candidates(target_x, target_y);

  const auto consider_x = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.x()));
    const auto distance = std::abs(correction);
    if (distance <= best_x) {
      best_x = distance;
      adjusted_x = static_cast<double>(raw_delta.x()) + correction;
    }
  };
  const auto consider_y = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.y()));
    const auto distance = std::abs(correction);
    if (distance <= best_y) {
      best_y = distance;
      adjusted_y = static_cast<double>(raw_delta.y()) + correction;
    }
  };
  const auto consider_grid_x = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step > 0.0) {
      consider_x(source, std::round((source + static_cast<double>(raw_delta.x())) / step) * step);
    }
  };
  const auto consider_grid_y = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step > 0.0) {
      consider_y(source, std::round((source + static_cast<double>(raw_delta.y())) / step) * step);
    }
  };

  const auto left = static_cast<double>(source_rect.left());
  const auto right = static_cast<double>(source_rect.right() + 1);
  const auto top = static_cast<double>(source_rect.top());
  const auto bottom = static_cast<double>(source_rect.bottom() + 1);
  const std::array<double, 3> source_x{left, (left + right) * 0.5, right};
  const std::array<double, 3> source_y{top, (top + bottom) * 0.5, bottom};
  for (const auto source : source_x) {
    for (const auto target : target_x) {
      consider_x(source, target);
    }
    consider_grid_x(source);
  }
  for (const auto source : source_y) {
    for (const auto target : target_y) {
      consider_y(source, target);
    }
    consider_grid_y(source);
  }

  return QPoint(static_cast<int>(std::lround(adjusted_x)), static_cast<int>(std::lround(adjusted_y)));
}

QPoint CanvasWidget::snapped_marquee_current_point(QPoint anchor, QPoint current) const {
  if (document_ == nullptr || !snap_enabled_) {
    return current;
  }

  const auto rect = marquee_selection_rect(anchor, current);
  if (rect.isEmpty()) {
    return current;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto adjusted_x = static_cast<double>(current.x());
  auto adjusted_y = static_cast<double>(current.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  std::vector<double> target_x;
  std::vector<double> target_y;
  append_snap_target_candidates(target_x, target_y);

  const auto active_x_edge = current.x() < anchor.x() ? static_cast<double>(rect.left())
                                                      : static_cast<double>(rect.right() + 1);
  const auto active_y_edge = current.y() < anchor.y() ? static_cast<double>(rect.top())
                                                      : static_cast<double>(rect.bottom() + 1);

  const auto consider_x = [&](double target) {
    const auto correction = target - active_x_edge;
    const auto distance = std::abs(correction);
    if (distance <= best_x) {
      best_x = distance;
      adjusted_x = static_cast<double>(current.x()) + correction;
    }
  };
  const auto consider_y = [&](double target) {
    const auto correction = target - active_y_edge;
    const auto distance = std::abs(correction);
    if (distance <= best_y) {
      best_y = distance;
      adjusted_y = static_cast<double>(current.y()) + correction;
    }
  };

  for (const auto target : target_x) {
    consider_x(target);
  }
  for (const auto target : target_y) {
    consider_y(target);
  }
  if (snap_to_grid_ && grid_visible_) {
    const auto step_x = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    const auto step_y = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                        static_cast<double>(std::max(1, grid_subdivisions_));
    if (step_x > 0.0) {
      consider_x(std::round(active_x_edge / step_x) * step_x);
    }
    if (step_y > 0.0) {
      consider_y(std::round(active_y_edge / step_y) * step_y);
    }
  }

  return QPoint(static_cast<int>(std::lround(adjusted_x)), static_cast<int>(std::lround(adjusted_y)));
}

QPoint CanvasWidget::snapped_move_delta(QPoint raw_delta) const {
  if (document_ == nullptr || !snap_enabled_ || moving_layers_.empty()) {
    return raw_delta;
  }

  const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
  auto adjusted_x = static_cast<double>(raw_delta.x());
  auto adjusted_y = static_cast<double>(raw_delta.y());
  double best_x = tolerance + 0.0001;
  double best_y = tolerance + 0.0001;

  std::vector<double> target_x;
  std::vector<double> target_y;
  if (snap_to_guides_) {
    for (const auto& guide : document_->guides()) {
      if (guide.orientation == GuideOrientation::Vertical) {
        target_x.push_back(guide_position_pixels(guide));
      } else {
        target_y.push_back(guide_position_pixels(guide));
      }
    }
  }
  if (snap_to_document_) {
    target_x.push_back(0.0);
    target_x.push_back(static_cast<double>(document_->width()) * 0.5);
    target_x.push_back(static_cast<double>(document_->width()));
    target_y.push_back(0.0);
    target_y.push_back(static_cast<double>(document_->height()) * 0.5);
    target_y.push_back(static_cast<double>(document_->height()));
  }
  if (snap_to_selection_ && !selection_.isEmpty()) {
    append_rect_snap_candidates(selection_.boundingRect(), target_x, target_y);
  }
  if (snap_to_layers_) {
    append_layer_snap_candidates(document_->layers(), target_x, target_y);
  }

  auto consider_x = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.x()));
    const auto distance = std::abs(correction);
    if (distance <= best_x) {
      best_x = distance;
      adjusted_x = static_cast<double>(raw_delta.x()) + correction;
    }
  };
  auto consider_y = [&](double source, double target) {
    const auto correction = target - (source + static_cast<double>(raw_delta.y()));
    const auto distance = std::abs(correction);
    if (distance <= best_y) {
      best_y = distance;
      adjusted_y = static_cast<double>(raw_delta.y()) + correction;
    }
  };
  const auto consider_grid_x = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step <= 0.0) {
      return;
    }
    consider_x(source, std::round((source + static_cast<double>(raw_delta.x())) / step) * step);
  };
  const auto consider_grid_y = [&](double source) {
    if (!snap_to_grid_ || !grid_visible_) {
      return;
    }
    const auto step = grid_cycle_pixels(document_->grid_settings().vertical_cycle_32) /
                      static_cast<double>(std::max(1, grid_subdivisions_));
    if (step <= 0.0) {
      return;
    }
    consider_y(source, std::round((source + static_cast<double>(raw_delta.y())) / step) * step);
  };

  for (const auto& moving_layer : moving_layers_) {
    const auto rect = moving_layer_outline_rect(moving_layer, QPoint());
    if (rect.isEmpty()) {
      continue;
    }
    const std::array<double, 3> source_x{static_cast<double>(rect.left()),
                                         static_cast<double>(rect.left() + rect.width() / 2.0),
                                         static_cast<double>(rect.right() + 1)};
    const std::array<double, 3> source_y{static_cast<double>(rect.top()),
                                         static_cast<double>(rect.top() + rect.height() / 2.0),
                                         static_cast<double>(rect.bottom() + 1)};
    for (const auto source : source_x) {
      for (const auto target : target_x) {
        consider_x(source, target);
      }
      consider_grid_x(source);
    }
    for (const auto source : source_y) {
      for (const auto target : target_y) {
        consider_y(source, target);
      }
      consider_grid_y(source);
    }
  }

  return QPoint(static_cast<int>(std::lround(adjusted_x)), static_cast<int>(std::lround(adjusted_y)));
}

int CanvasWidget::guide_at_widget_position(QPoint widget_position) const {
  if (document_ == nullptr || !guides_visible_) {
    return -1;
  }
  const auto document_point = document_position_f(QPointF(widget_position));
  if (document_point.x() < 0.0 || document_point.y() < 0.0 ||
      document_point.x() > static_cast<double>(document_->width()) ||
      document_point.y() > static_cast<double>(document_->height())) {
    return -1;
  }

  constexpr double kGuideHitPixels = 4.0;
  const auto tolerance = kGuideHitPixels / std::max(zoom_, 0.0001);
  for (int index = static_cast<int>(document_->guides().size()) - 1; index >= 0; --index) {
    const auto& guide = document_->guides()[static_cast<std::size_t>(index)];
    const auto pixels = guide_position_pixels(guide);
    const auto distance = guide.orientation == GuideOrientation::Vertical
                              ? std::abs(document_point.x() - pixels)
                              : std::abs(document_point.y() - pixels);
    if (distance <= tolerance) {
      return index;
    }
  }
  return -1;
}

GuideOrientation CanvasWidget::guide_orientation_from_ruler(QPoint widget_position) const noexcept {
  if (widget_position.y() < kTopRulerHeight && widget_position.x() >= kLeftRulerWidth) {
    return GuideOrientation::Horizontal;
  }
  return GuideOrientation::Vertical;
}

bool CanvasWidget::widget_position_in_ruler(QPoint widget_position) const noexcept {
  return rulers_visible_ &&
         ((widget_position.y() >= 0 && widget_position.y() < kTopRulerHeight &&
           widget_position.x() >= kLeftRulerWidth) ||
          (widget_position.x() >= 0 && widget_position.x() < kLeftRulerWidth &&
           widget_position.y() >= kTopRulerHeight));
}

void CanvasWidget::clear_guide_selection() noexcept {
  if (selected_guide_index_ < 0) {
    return;
  }
  selected_guide_index_ = -1;
  update();
}

void CanvasWidget::begin_guide_drag(int guide_index, QPoint widget_position) {
  if (document_ == nullptr || guide_index < 0 || guide_index >= static_cast<int>(document_->guides().size())) {
    return;
  }
  selected_guide_index_ = guide_index;
  if (guides_locked_) {
    update();
    return;
  }
  const auto& guide = document_->guides()[static_cast<std::size_t>(guide_index)];
  dragging_guide_ = true;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  guide_drag_orientation_ = guide.orientation;
  guide_drag_position_32_ = guide.position_32;
  guide_drag_original_orientation_ = guide.orientation;
  guide_drag_original_position_32_ = guide.position_32;
  update_guide_drag(widget_position, Qt::NoModifier);
}

void CanvasWidget::begin_new_guide_drag(QPoint widget_position) {
  if (document_ == nullptr) {
    return;
  }
  dragging_guide_ = true;
  creating_guide_ = true;
  guide_drag_remove_ = false;
  selected_guide_index_ = -1;
  guide_drag_orientation_ = guide_orientation_from_ruler(widget_position);
  guide_drag_original_orientation_ = guide_drag_orientation_;
  guide_drag_original_position_32_ = 0;
  guide_drag_position_32_ = 0;
  update_guide_drag(widget_position, Qt::NoModifier);
}

void CanvasWidget::update_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  if (!dragging_guide_ || document_ == nullptr) {
    return;
  }

  auto orientation = guide_drag_original_orientation_;
  if ((modifiers & Qt::AltModifier) != 0) {
    orientation = orientation == GuideOrientation::Vertical ? GuideOrientation::Horizontal : GuideOrientation::Vertical;
  }
  guide_drag_orientation_ = orientation;

  const auto document_point = document_position_f(QPointF(widget_position));
  const auto axis_position = orientation == GuideOrientation::Vertical ? document_point.x() : document_point.y();
  const auto limit = static_cast<double>(orientation == GuideOrientation::Vertical ? document_->width() : document_->height());
  auto snapped_position = axis_position;

  if ((modifiers & Qt::ShiftModifier) != 0) {
    // Shift snaps to the ruler's minor ticks in the active ruler unit (a vertical
    // guide moves along the horizontal axis).
    const bool horizontal_axis = orientation == GuideOrientation::Vertical;
    const auto pixels_per_unit = ruler_pixels_per_unit(horizontal_axis);
    const auto steps = ruler_tick_steps(ruler_unit_, pixels_per_unit * zoom_);
    const auto tick = std::max(1e-6, steps.major / steps.subdivisions * pixels_per_unit);
    snapped_position = std::round(snapped_position / tick) * tick;
  } else if (snap_enabled_) {
    const auto tolerance = kSnapToleranceScreenPixels / std::max(zoom_, 0.0001);
    double best = tolerance + 0.0001;
    const auto consider = [&](double candidate) {
      const auto distance = std::abs(candidate - axis_position);
      if (distance <= best) {
        best = distance;
        snapped_position = candidate;
      }
    };
    if (snap_to_grid_ && grid_visible_) {
      const auto step =
          (orientation == GuideOrientation::Vertical
               ? grid_cycle_pixels(document_->grid_settings().horizontal_cycle_32)
               : grid_cycle_pixels(document_->grid_settings().vertical_cycle_32)) /
          static_cast<double>(std::max(1, grid_subdivisions_));
      if (step > 0.0) {
        consider(std::round(axis_position / step) * step);
      }
    }
    if (snap_to_document_) {
      consider(0.0);
      consider(limit * 0.5);
      consider(limit);
    }
    std::vector<double> x_candidates;
    std::vector<double> y_candidates;
    if (snap_to_selection_ && !selection_.isEmpty()) {
      append_rect_snap_candidates(selection_.boundingRect(), x_candidates, y_candidates);
    }
    if (snap_to_layers_) {
      append_layer_snap_candidates(document_->layers(), x_candidates, y_candidates);
    }
    const auto& candidates = orientation == GuideOrientation::Vertical ? x_candidates : y_candidates;
    for (const auto candidate : candidates) {
      consider(candidate);
    }
  }

  guide_drag_remove_ = snapped_position < 0.0 || snapped_position > limit;
  snapped_position = std::clamp(snapped_position, 0.0, limit);
  guide_drag_position_32_ = guide_position_32(snapped_position);
  update();
}

void CanvasWidget::finish_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  if (!dragging_guide_ || document_ == nullptr) {
    return;
  }
  update_guide_drag(widget_position, modifiers);

  if (creating_guide_) {
    if (!guide_drag_remove_) {
      if (before_edit_callback_) {
        before_edit_callback_(tr("New Guide"));
      }
      document_->guides().push_back(DocumentGuide{guide_drag_orientation_, guide_drag_position_32_});
      selected_guide_index_ = static_cast<int>(document_->guides().size()) - 1;
      dragging_guide_ = false;
      creating_guide_ = false;
      document_changed();
      return;
    }
  } else if (selected_guide_index_ >= 0 && selected_guide_index_ < static_cast<int>(document_->guides().size())) {
    const auto changed = guide_drag_remove_ || guide_drag_orientation_ != guide_drag_original_orientation_ ||
                         guide_drag_position_32_ != guide_drag_original_position_32_;
    if (changed && before_edit_callback_) {
      before_edit_callback_(guide_drag_remove_ ? tr("Clear Selected Guide") : tr("Move Guide"));
    }
    if (changed) {
      if (guide_drag_remove_) {
        document_->guides().erase(document_->guides().begin() + selected_guide_index_);
        selected_guide_index_ = -1;
      } else {
        auto& guide = document_->guides()[static_cast<std::size_t>(selected_guide_index_)];
        guide.orientation = guide_drag_orientation_;
        guide.position_32 = guide_drag_position_32_;
      }
      dragging_guide_ = false;
      creating_guide_ = false;
      document_changed();
      return;
    }
  }

  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  update();
}

void CanvasWidget::cancel_guide_drag() {
  if (!dragging_guide_) {
    return;
  }
  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  update();
}

}  // namespace patchy::ui
