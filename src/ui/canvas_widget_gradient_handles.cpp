// CanvasWidget's handles for an existing Gradient Fill (GitHub issue 663, docs/gradients-fill-handles.md,
// "Gradient Fill handles"). With the Gradient tool active on a Gradient Fill layer or a
// single-fill gradient shape, the gradient's line is drawn from its start handle (location 0,
// or 1 when reversed) to its end handle, with the color stops on one side of it, the opacity
// stops on the other and each segment's midpoint beside its stops. Dragging an end changes
// the angle and length (Shift: 15-degree steps), dragging the line moves the gradient, a stop
// slides along the line or leaves it to be deleted, a midpoint slides between its stops, a
// click on the line adds a color stop, and a double-click on a color stop opens its color.
// Every gesture previews through MainWindow and lands as one undo step; Esc cancels it. A
// press anywhere else still draws a new gradient. The overlay is drawn from the stored
// placement through view_transform(); the only per-layer work, the alignment bounds, is
// cached by revision.

#include "ui/canvas_widget.hpp"

#include "core/blend_math.hpp"
#include "core/gradient_fill_drag.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_types.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QHelpEvent>
#include <QAction>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolTip>
#include <Qt>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Screen-pixel sizes and reaches.
constexpr double kEndReach = 8.0;
constexpr double kStopReach = 8.0;
constexpr double kMidpointReach = 5.0;
// The drawn line is a 3 px band: a click a few pixels beside it still means the line
// (GitHub issue 663 follow-up: at 4 px a slightly low click started a new gradient instead).
constexpr double kLineReach = 7.0;
constexpr double kHandleRadius = 6.0;
// A stop marker sits this far off the line: color stops on one side, opacity stops on the other.
constexpr double kStopOffset = 16.0;
constexpr double kStopHalf = 6.0;
constexpr double kMidpointHalf = 4.0;
// A stop dragged this far past its marker's place beside the line is deleted on release.
constexpr double kRemoveDistance = 24.0;
// Midpoints hide when their stops are closer than this on screen.
constexpr double kMinMidpointGap = 16.0;
constexpr double kSnapDegrees = 15.0;
// How far either side of the line a click still lands on it: the stop markers' band and a
// little past it (the opacity side adds opacity stops, the color side color stops).
constexpr double kSideReach = kStopOffset + kStopHalf + 3.0;
// The live preview re-bakes the fill: at most one per this many milliseconds while the
// pointer moves (the overlay itself follows every move).
constexpr int kGradientPreviewIntervalMs = 33;

using PartKind = CanvasWidget::GradientHandlePartKind;
using Part = CanvasWidget::GradientHandlePart;

double distance_to_segment(QPointF point, QPointF a, QPointF b) {
  const QPointF ab = b - a;
  const double length_squared = QPointF::dotProduct(ab, ab);
  if (length_squared <= 0.0) {
    return QLineF(point, a).length();
  }
  const double t = std::clamp(QPointF::dotProduct(point - a, ab) / length_squared, 0.0, 1.0);
  return QLineF(point, a + ab * t).length();
}

double distance_to_line(QPointF point, QPointF a, QPointF b) {
  const QPointF ab = b - a;
  const double length = std::hypot(ab.x(), ab.y());
  if (length <= 0.0) {
    return QLineF(point, a).length();
  }
  return std::abs((point.x() - a.x()) * ab.y() - (point.y() - a.y()) * ab.x()) / length;
}

// The side of the line the color stops sit on (a unit normal in widget space); the opacity
// stops take the other side. A screen-down normal for a left-to-right line.
QPointF stop_side_normal(QPointF start, QPointF end) {
  const QPointF along = end - start;
  const double length = std::hypot(along.x(), along.y());
  if (length <= 1e-9) {
    return {0.0, 1.0};
  }
  return {-along.y() / length, along.x() / length};
}

bool solid_stops(const LayerStyleGradient& gradient) {
  return gradient.form != GradientDefinitionForm::Noise;
}

// Widget-space positions of everything the overlay draws for one gradient.
struct HandleGeometry {
  QPointF start;
  QPointF end;
  QPointF normal;
  std::vector<QPointF> color_stops;
  std::vector<QPointF> alpha_stops;
  // Indexed like the stops (the destination stop owns the midpoint); nullopt when hidden.
  std::vector<std::optional<QPointF>> color_midpoints;
  std::vector<std::optional<QPointF>> alpha_midpoints;
};

template <typename Stop, typename Map>
void place_stops(const std::vector<Stop>& stops, const Map& on_line, QPointF offset, std::vector<QPointF>& points,
                 std::vector<std::optional<QPointF>>& midpoints) {
  points.clear();
  midpoints.clear();
  for (const auto& stop : stops) {
    points.push_back(on_line(static_cast<double>(stop.location)) + offset);
  }
  midpoints.resize(stops.size());
  for (std::size_t i = 1; i < stops.size(); ++i) {
    if (QLineF(points[i - 1], points[i]).length() < kMinMidpointGap) {
      continue;
    }
    const auto from = static_cast<double>(stops[i - 1].location);
    const auto to = static_cast<double>(stops[i].location);
    midpoints[i] = on_line(from + (to - from) * static_cast<double>(stops[i].midpoint)) + offset;
  }
}

QPolygonF diamond_at(QPointF center, double half) {
  return QPolygonF({center + QPointF(0.0, -half), center + QPointF(half, 0.0), center + QPointF(0.0, half),
                    center + QPointF(-half, 0.0)});
}

}  // namespace

void CanvasWidget::set_gradient_fill_edit_callback(GradientFillEditCallback callback) {
  gradient_fill_edit_callback_ = std::move(callback);
}

void CanvasWidget::set_gradient_fill_stop_color_requested_callback(
    std::function<void(LayerId, int, GradientHandleTarget)> callback) {
  gradient_fill_stop_color_requested_callback_ = std::move(callback);
}

bool CanvasWidget::gradient_handles_enabled() const noexcept {
  return tool_ == CanvasTool::Gradient || gradient_handles_under_other_tool();
}

bool CanvasWidget::gradient_handles_under_other_tool() const noexcept {
  if (!gradient_handles_forced_) {
    return false;
  }
  switch (tool_) {
    case CanvasTool::Gradient:  // its own handles, with its own rules
    // These own every press (or only move the view), so handles shown under them could
    // never be pressed.
    case CanvasTool::Pan:
    case CanvasTool::Zoom:
    case CanvasTool::RotateView:
    case CanvasTool::Eyedropper:
    case CanvasTool::Ruler:
    case CanvasTool::Artboard:
    case CanvasTool::Crop:
    case CanvasTool::PerspectiveCrop:
      return false;
    default:
      break;
  }
  // A transform session owns the canvas, and a path or lasso being built by clicks owns
  // each click.
  return !transforming_layer_ && !warping_layer_ && !path_transform_active_ && !pen_session_active_ &&
         !polygon_lassoing_ && !magnetic_lassoing_;
}

void CanvasWidget::set_gradient_handles_forced(bool forced, GradientHandleTarget preferred) {
  if (gradient_handles_forced_ == forced && gradient_handles_preferred_ == preferred) {
    return;
  }
  gradient_handles_forced_ = forced;
  gradient_handles_preferred_ = preferred;
  gradient_handle_bounds_cache_.reset();
  if (tool_ != CanvasTool::Gradient) {
    // As a switch away from the Gradient tool: the drag is put back, the popup commits. The
    // handles may now be gone or on the shape's other gradient.
    cancel_gradient_fill_handle_drag();
    finish_gradient_opacity_popup(true);
    gradient_stop_selection_.reset();
    if (gradient_handle_hover_.kind != PartKind::None || gradient_handle_cursor_ != GradientHandleCursor::None) {
      gradient_handle_hover_ = {};
      gradient_handle_cursor_ = GradientHandleCursor::None;
      update_tool_cursor();
    }
  }
  update();
}

bool CanvasWidget::handle_gradient_handle_key(QKeyEvent* event) {
  if (gradient_handles_enabled() && event->modifiers() == Qt::NoModifier &&
      (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !event->isAutoRepeat() &&
      delete_selected_gradient_stop()) {
    return true;
  }
  if (gradient_handle_drag_.has_value() && event->key() == Qt::Key_Escape) {
    // Esc mid-drag puts the gradient back as it was before the press.
    cancel_gradient_fill_handle_drag();
    return true;
  }
  return false;
}

const Layer* CanvasWidget::gradient_fill_handle_layer() const {
  if (document_ == nullptr || !gradient_handles_enabled() || quick_mask_active_ ||
      layer_edit_target_ != LayerEditTarget::Content) {
    return nullptr;
  }
  const auto& document = std::as_const(*document_);
  const auto active = document.active_layer_id();
  if (!active.has_value()) {
    return nullptr;
  }
  const auto* layer = document.find_layer(*active);
  if (layer == nullptr || !layer_is_vector_shape(*layer)) {
    return nullptr;
  }
  return gradient_handle_target(*layer).has_value() ? layer : nullptr;
}

std::optional<GradientHandleTarget> CanvasWidget::gradient_handle_target(const Layer& layer) const {
  const auto* shape = layer.vector_shape();
  if (shape == nullptr || !shape->parts.empty()) {
    return std::nullopt;
  }
  const bool fill = shape->fill.kind == VectorFillKind::Gradient &&
                    shape->fill.gradient.type != LayerStyleGradientType::ShapeBurst;
  if (tool_ == CanvasTool::Gradient) {
    // The Gradient tool draws and edits fills.
    return fill ? std::optional(GradientHandleTarget::Fill) : std::nullopt;
  }
  const auto& stroke = shape->stroke;
  const bool stroke_gradient = stroke.enabled && stroke.width > 0.0 && !shape->path.empty() &&
                               !shape->path_disabled && stroke.content.kind == VectorFillKind::Gradient &&
                               stroke.content.gradient.type != LayerStyleGradientType::ShapeBurst;
  // The preferred gradient when the shape has both; otherwise the one it has.
  if (gradient_handles_preferred_ == GradientHandleTarget::Stroke && stroke_gradient) {
    return GradientHandleTarget::Stroke;
  }
  if (fill) {
    return GradientHandleTarget::Fill;
  }
  return stroke_gradient ? std::optional(GradientHandleTarget::Stroke) : std::nullopt;
}

const LayerStyleGradient& CanvasWidget::gradient_handle_gradient(const Layer& layer) const {
  const auto* shape = layer.vector_shape();
  return gradient_handle_target(layer) == GradientHandleTarget::Stroke ? shape->stroke.content.gradient
                                                                       : shape->fill.gradient;
}

Rect CanvasWidget::gradient_fill_handle_bounds(const Layer& layer) const {
  const auto canvas = Rect::from_size(document_->width(), document_->height());
  const auto target = gradient_handle_target(layer).value_or(GradientHandleTarget::Fill);
  auto& cache = gradient_handle_bounds_cache_;
  if (cache.has_value() && cache->layer_id == layer.id() && cache->render_revision == layer.render_revision() &&
      cache->content_revision == layer.content_revision() && cache->canvas_width == canvas.width &&
      cache->canvas_height == canvas.height && cache->target == target) {
    return cache->bounds;
  }
  // A stroke's gradient spans the stroke's own band, a fill's the fill (docs/gradients-fill-handles.md).
  const auto bounds = target == GradientHandleTarget::Stroke ? gradient_stroke_alignment_bounds(layer, canvas)
                                                             : gradient_fill_alignment_bounds(layer, canvas);
  cache = GradientHandleBoundsCache{layer.id(),   layer.render_revision(), layer.content_revision(),
                                    canvas.width, canvas.height,           bounds,
                                    target};
  return bounds;
}

std::optional<CanvasWidget::GradientFillHandles> CanvasWidget::gradient_fill_handles() const {
  if (gradient_handle_drag_.has_value()) {
    const auto& drag = *gradient_handle_drag_;
    return GradientFillHandles{drag.layer_id, drag.current, drag.bounds, gradient_fill_line(drag.current, drag.bounds),
                               drag.target};
  }
  const auto* layer = gradient_fill_handle_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  const auto& gradient = gradient_handle_gradient(*layer);
  const auto bounds = gradient_fill_handle_bounds(*layer);
  return GradientFillHandles{layer->id(), gradient, bounds, gradient_fill_line(gradient, bounds),
                             gradient_handle_target(*layer).value_or(GradientHandleTarget::Fill)};
}

namespace {

HandleGeometry handle_geometry(const CanvasWidget::GradientFillHandles& handles,
                               const std::function<QPointF(QPointF)>& to_widget) {
  HandleGeometry geometry;
  const auto& line = handles.line;
  geometry.start = to_widget(QPointF(line.start_x, line.start_y));
  geometry.end = to_widget(QPointF(line.end_x, line.end_y));
  geometry.normal = stop_side_normal(geometry.start, geometry.end);
  if (!solid_stops(handles.gradient)) {
    return geometry;
  }
  const auto reverse = handles.gradient.reverse;
  const auto on_line = [&](double location) {
    const auto [x, y] = gradient_fill_line_point(line, reverse, location);
    return to_widget(QPointF(x, y));
  };
  place_stops(handles.gradient.color_stops, on_line, geometry.normal * kStopOffset, geometry.color_stops,
              geometry.color_midpoints);
  place_stops(handles.gradient.alpha_stops, on_line, geometry.normal * -kStopOffset, geometry.alpha_stops,
              geometry.alpha_midpoints);
  return geometry;
}

}  // namespace

std::optional<QPointF> CanvasWidget::gradient_fill_handle_widget_position(GradientHandlePart part) const {
  const auto handles = gradient_fill_handles();
  if (!handles.has_value()) {
    return std::nullopt;
  }
  const auto geometry = handle_geometry(*handles, [this](QPointF point) { return widget_position_f(point); });
  const auto index = static_cast<std::size_t>(std::max(part.index, 0));
  switch (part.kind) {
    case PartKind::Start:
      return geometry.start;
    case PartKind::End:
      return geometry.end;
    case PartKind::Line:
      return (geometry.start + geometry.end) / 2.0;
    case PartKind::AlphaLine:
      return (geometry.start + geometry.end) / 2.0 - geometry.normal * (kStopOffset * 0.75);
    case PartKind::ColorStop:
      return part.index >= 0 && index < geometry.color_stops.size() ? std::optional(geometry.color_stops[index])
                                                                     : std::nullopt;
    case PartKind::AlphaStop:
      return part.index >= 0 && index < geometry.alpha_stops.size() ? std::optional(geometry.alpha_stops[index])
                                                                     : std::nullopt;
    case PartKind::ColorMidpoint:
      return part.index >= 0 && index < geometry.color_midpoints.size() ? geometry.color_midpoints[index]
                                                                         : std::nullopt;
    case PartKind::AlphaMidpoint:
      return part.index >= 0 && index < geometry.alpha_midpoints.size() ? geometry.alpha_midpoints[index]
                                                                         : std::nullopt;
    case PartKind::None:
      break;
  }
  return std::nullopt;
}

CanvasWidget::GradientHandlePart CanvasWidget::gradient_fill_handle_part_at(QPointF widget_point) const {
  const auto handles = gradient_fill_handles();
  if (!handles.has_value()) {
    return {};
  }
  const auto geometry = handle_geometry(*handles, [this](QPointF point) { return widget_position_f(point); });
  const auto distance = [&widget_point](QPointF at) { return QLineF(widget_point, at).length(); };
  // The end wins over the start where they overlap, so a gradient of no length can grow.
  if (distance(geometry.end) <= kEndReach) {
    return {PartKind::End, -1};
  }
  if (distance(geometry.start) <= kEndReach) {
    return {PartKind::Start, -1};
  }
  // The nearest stop within reach; ties go to the later stop (drawn on top).
  const auto nearest = [&](const std::vector<QPointF>& points, double reach) {
    int best = -1;
    double best_distance = reach;
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (const auto d = distance(points[i]); d <= best_distance) {
        best = static_cast<int>(i);
        best_distance = d;
      }
    }
    return std::pair{best, best_distance};
  };
  const auto [color_stop, color_distance] = nearest(geometry.color_stops, kStopReach);
  const auto [alpha_stop, alpha_distance] = nearest(geometry.alpha_stops, kStopReach);
  if (color_stop >= 0 && (alpha_stop < 0 || color_distance <= alpha_distance)) {
    return {PartKind::ColorStop, color_stop};
  }
  if (alpha_stop >= 0) {
    return {PartKind::AlphaStop, alpha_stop};
  }
  const auto nearest_midpoint = [&](const std::vector<std::optional<QPointF>>& points) {
    int best = -1;
    double best_distance = kMidpointReach;
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (points[i].has_value()) {
        if (const auto d = distance(*points[i]); d <= best_distance) {
          best = static_cast<int>(i);
          best_distance = d;
        }
      }
    }
    return best;
  };
  if (const auto midpoint = nearest_midpoint(geometry.color_midpoints); midpoint >= 0) {
    return {PartKind::ColorMidpoint, midpoint};
  }
  if (const auto midpoint = nearest_midpoint(geometry.alpha_midpoints); midpoint >= 0) {
    return {PartKind::AlphaMidpoint, midpoint};
  }
  // The line and the bands beside it, out to just past the stop markers: the line and the color
  // side add color stops, the opacity side opacity stops (or drag the whole gradient).
  const QPointF along = geometry.end - geometry.start;
  const double length_squared = QPointF::dotProduct(along, along);
  if (length_squared > 0.0) {
    const double t = QPointF::dotProduct(widget_point - geometry.start, along) / length_squared;
    const double side = QPointF::dotProduct(widget_point - geometry.start, geometry.normal);
    if (t >= 0.0 && t <= 1.0 && side <= kSideReach && side >= -kSideReach) {
      if (side < -kLineReach && solid_stops(handles->gradient)) {
        return {PartKind::AlphaLine, -1};
      }
      if (side <= kLineReach || solid_stops(handles->gradient)) {
        return {PartKind::Line, -1};
      }
    }
  }
  if (distance_to_segment(widget_point, geometry.start, geometry.end) <= kLineReach) {
    return {PartKind::Line, -1};
  }
  return {};
}

QString CanvasWidget::gradient_fill_handle_hint(GradientHandlePart part) const {
  switch (part.kind) {
    case PartKind::Start:
      return tr("Drag to move the gradient's start point. Shift keeps the angle in 15-degree steps.");
    case PartKind::End:
      return tr("Drag to move the gradient's end point. Shift keeps the angle in 15-degree steps.");
    case PartKind::Line:
      return tr("Click to add a color stop here (on the opacity stops' side, an opacity stop), or drag to move "
                "the gradient.");
    case PartKind::AlphaLine:
      return tr("Click to add an opacity stop here, or drag to move the gradient.");
    case PartKind::ColorStop:
      return tr("Drag to move this color stop. Drag it off the line or select it and press Delete to remove it, "
                "right-click for more, double-click to change its color.");
    case PartKind::AlphaStop:
      return tr("Drag to move this opacity stop. Drag it off the line or select it and press Delete to remove "
                "it, right-click for more, double-click to set its opacity.");
    case PartKind::ColorMidpoint:
      return tr("Drag to move the color midpoint between these color stops.");
    case PartKind::AlphaMidpoint:
      if (gradient_fill_midpoint_dimmed(part)) {
        return tr("Drag to move the opacity midpoint between these opacity stops.") + QLatin1Char(' ') +
               tr("Both stops have the same opacity, so it has no effect yet.");
      }
      return tr("Drag to move the opacity midpoint between these opacity stops.");
    case PartKind::None:
      break;
  }
  return {};
}

QString CanvasWidget::gradient_fill_handle_tooltip(GradientHandlePart part) const {
  const auto handles = gradient_fill_handles();
  if (!handles.has_value()) {
    return {};
  }
  const auto& gradient = handles->gradient;
  const auto index = static_cast<std::size_t>(std::max(part.index, 0));
  const auto percent = [](float value) { return QString::number(static_cast<int>(std::lround(value * 100.0F))); };
  switch (part.kind) {
    case PartKind::Start:
      return tr("Gradient start point");
    case PartKind::End:
      return tr("Gradient end point");
    case PartKind::Line:
      return tr("Gradient line");
    case PartKind::AlphaLine:
      return tr("Opacity side of the gradient line");
    case PartKind::ColorStop:
      if (index < gradient.color_stops.size()) {
        return tr("Color stop at %1%").arg(percent(gradient.color_stops[index].location)) + QLatin1Char('\n') +
               gradient_fill_handle_hint(part);
      }
      break;
    case PartKind::AlphaStop:
      if (index < gradient.alpha_stops.size()) {
        return tr("Opacity stop at %1%: %2% opacity")
                   .arg(percent(gradient.alpha_stops[index].location), percent(gradient.alpha_stops[index].opacity)) +
               QLatin1Char('\n') + gradient_fill_handle_hint(part);
      }
      break;
    case PartKind::ColorMidpoint:
      if (index < gradient.color_stops.size()) {
        return tr("Color midpoint: %1%").arg(percent(gradient.color_stops[index].midpoint));
      }
      break;
    case PartKind::AlphaMidpoint:
      if (index < gradient.alpha_stops.size()) {
        const auto text = tr("Opacity midpoint: %1%").arg(percent(gradient.alpha_stops[index].midpoint));
        return gradient_fill_midpoint_dimmed(part)
                   ? text + QLatin1Char('\n') + tr("Both stops have the same opacity, so it has no effect yet.")
                   : text;
      }
      break;
    case PartKind::None:
      break;
  }
  return {};
}

void CanvasWidget::set_gradient_fill_handle_hover(GradientHandlePart part) {
  if (part == gradient_handle_hover_) {
    return;
  }
  gradient_handle_hover_ = part;
  update();
  if (part.kind != PartKind::None && status_callback_) {
    status_callback_(gradient_fill_handle_hint(part));
  }
}

bool CanvasWidget::press_gradient_fill_handle(QMouseEvent* event) {
  if ((event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0) {
    return false;
  }
  const auto* layer = gradient_fill_handle_layer();
  if (layer == nullptr) {
    return false;
  }
  const auto part = gradient_fill_handle_part_at(event->position());
  if (part.kind == PartKind::None) {
    // A press away from the handles (a new gradient) drops the stop selection.
    if (gradient_stop_selection_.has_value()) {
      gradient_stop_selection_.reset();
      update();
    }
    return false;
  }
  if (!vector_lock_reason(*layer).empty() || layer_effectively_locks_image_pixels(*layer)) {
    report_status_error(tr("Unlock the Gradient Fill layer to change its gradient"));
    return true;
  }
  GradientFillHandleDrag drag;
  drag.layer_id = layer->id();
  drag.bounds = gradient_fill_handle_bounds(*layer);
  drag.target = gradient_handle_target(*layer).value_or(GradientHandleTarget::Fill);
  drag.original = gradient_handle_gradient(*layer);
  drag.current = drag.original;
  drag.part = part;
  drag.current_index = part.index;
  drag.press_widget = event->position();
  drag.pointer_widget = event->position();
  drag.press_document = document_position_f(event->position());
  drag.press_line = gradient_fill_line(drag.original, drag.bounds);
  drag.serial = ++gradient_handle_drag_serial_;
  gradient_handle_drag_ = std::move(drag);
  // A press on a stop selects it (Delete removes it); any other part drops the selection.
  if (part.kind == PartKind::ColorStop || part.kind == PartKind::AlphaStop) {
    gradient_stop_selection_ =
        GradientStopSelection{layer->id(), part.kind == PartKind::AlphaStop, part.index, gradient_handle_drag_->target};
  } else {
    gradient_stop_selection_.reset();
  }
  set_gradient_fill_handle_hover(part);
  apply_gradient_handle_cursor(gradient_handle_cursor_for(part));
  update();
  return true;
}

void CanvasWidget::update_gradient_fill_handle_drag(QPointF widget_point, Qt::KeyboardModifiers modifiers) {
  if (!gradient_handle_drag_.has_value()) {
    return;
  }
  auto& drag = *gradient_handle_drag_;
  drag.pointer_widget = widget_point;
  const bool grabs_end = drag.part.kind == PartKind::Start || drag.part.kind == PartKind::End;
  if (!drag.moved) {
    // The ends move at once; everything else waits for a real drag, so a click on the line
    // adds a stop and a click on a stop changes nothing.
    if (!grabs_end && (widget_point - drag.press_widget).manhattanLength() < QApplication::startDragDistance()) {
      return;
    }
    drag.moved = true;
  }
  const auto point = document_position_f(widget_point);
  auto edited = drag.original;
  drag.removing = false;
  drag.current_index = drag.part.index;
  const auto index = static_cast<std::size_t>(std::max(drag.part.index, 0));
  const auto location = static_cast<float>(
      gradient_fill_line_location(drag.press_line, drag.original.reverse, point.x(), point.y()));
  switch (drag.part.kind) {
    case PartKind::Start:
    case PartKind::End: {
      auto line = drag.press_line;
      const bool start = drag.part.kind == PartKind::Start;
      QPointF moved = point;
      if ((modifiers & Qt::ShiftModifier) != 0) {
        const QPointF anchor = start ? QPointF(line.end_x, line.end_y) : QPointF(line.start_x, line.start_y);
        const QPointF along = moved - anchor;
        const double length = std::hypot(along.x(), along.y());
        const double step = kSnapDegrees * std::numbers::pi / 180.0;
        const double angle = std::round(std::atan2(along.y(), along.x()) / step) * step;
        moved = anchor + QPointF(std::cos(angle), std::sin(angle)) * length;
      }
      if (start) {
        line.start_x = moved.x();
        line.start_y = moved.y();
      } else {
        line.end_x = moved.x();
        line.end_y = moved.y();
      }
      place_gradient_fill_line(edited, drag.bounds, line);
      break;
    }
    case PartKind::Line:
    case PartKind::AlphaLine: {
      const auto delta = point - drag.press_document;
      auto line = drag.press_line;
      line.start_x += delta.x();
      line.start_y += delta.y();
      line.end_x += delta.x();
      line.end_y += delta.y();
      place_gradient_fill_line(edited, drag.bounds, line);
      break;
    }
    case PartKind::ColorStop:
    case PartKind::AlphaStop: {
      const bool color = drag.part.kind == PartKind::ColorStop;
      const auto count = color ? edited.color_stops.size() : edited.alpha_stops.size();
      const auto start = widget_position_f(QPointF(drag.press_line.start_x, drag.press_line.start_y));
      const auto end = widget_position_f(QPointF(drag.press_line.end_x, drag.press_line.end_y));
      if (count > 2U && distance_to_line(widget_point, start, end) > kRemoveDistance + kStopOffset) {
        drag.removing = true;
        drag.current_index = -1;
        if (color) {
          remove_gradient_color_stop(edited, index);
        } else {
          remove_gradient_alpha_stop(edited, index);
        }
      } else {
        drag.current_index = static_cast<int>(color ? move_gradient_color_stop(edited, index, location)
                                                    : move_gradient_alpha_stop(edited, index, location));
      }
      break;
    }
    case PartKind::ColorMidpoint:
    case PartKind::AlphaMidpoint: {
      const bool color = drag.part.kind == PartKind::ColorMidpoint;
      const auto count = color ? edited.color_stops.size() : edited.alpha_stops.size();
      if (index == 0U || index >= count) {
        break;
      }
      const auto from = color ? edited.color_stops[index - 1U].location : edited.alpha_stops[index - 1U].location;
      const auto to = color ? edited.color_stops[index].location : edited.alpha_stops[index].location;
      if (std::abs(to - from) <= 1e-6F) {
        break;
      }
      const auto midpoint = (location - from) / (to - from);
      if (color) {
        set_gradient_color_midpoint(edited, index, midpoint);
      } else {
        set_gradient_alpha_midpoint(edited, index, midpoint);
      }
      break;
    }
    case PartKind::None:
      break;
  }
  // The line moves the gradient once the press has really moved; a stop past the delete
  // distance shows the delete cursor (and turns back when it returns).
  apply_gradient_handle_cursor(drag.removing                                     ? GradientHandleCursor::DeleteStop
                               : grabs_end || drag.part.kind == PartKind::Line || drag.part.kind == PartKind::AlphaLine
                                   ? GradientHandleCursor::Move
                                                                               : GradientHandleCursor::AlongLine);
  if (drag.removing && status_callback_) {
    status_callback_(tr("Release to delete this stop."));
  }
  if (edited == drag.current) {
    update();
    return;
  }
  drag.current = std::move(edited);
  // The first change previews at once; later ones at most every kGradientPreviewIntervalMs,
  // the newest one when the interval ends.
  if (!drag.previewed || !gradient_handle_preview_clock_.isValid() ||
      gradient_handle_preview_clock_.elapsed() >= kGradientPreviewIntervalMs) {
    send_gradient_fill_preview();
  } else if (!drag.preview_scheduled) {
    drag.preview_scheduled = true;
    const auto serial = drag.serial;
    const auto wait = std::max<qint64>(1, kGradientPreviewIntervalMs - gradient_handle_preview_clock_.elapsed());
    QTimer::singleShot(static_cast<int>(wait), this, [this, serial] {
      if (gradient_handle_drag_.has_value() && gradient_handle_drag_->serial == serial &&
          gradient_handle_drag_->preview_scheduled) {
        send_gradient_fill_preview();
      }
    });
  }
  update();
}

void CanvasWidget::send_gradient_fill_preview() {
  if (!gradient_handle_drag_.has_value()) {
    return;
  }
  auto& drag = *gradient_handle_drag_;
  drag.preview_scheduled = false;
  if (!gradient_fill_edit_callback_ || (drag.previewed && drag.current == drag.previewed_gradient)) {
    return;
  }
  drag.previewed = true;
  drag.previewed_gradient = drag.current;
  gradient_handle_preview_clock_.start();
  // The callback renders the layer and may repaint; copy what it needs first.
  const auto layer_id = drag.layer_id;
  const auto gradient = drag.current;
  const auto serial = drag.serial;
  const auto target = drag.target;
  gradient_fill_edit_callback_(layer_id, gradient, GradientFillEditPhase::Preview, serial, QString(), target);
}

bool CanvasWidget::move_gradient_fill_handle(QMouseEvent* event) {
  if (gradient_handle_drag_.has_value()) {
    if ((event->buttons() & Qt::LeftButton) == 0) {
      // The release never arrived: put the gradient back rather than commit a guess.
      cancel_gradient_fill_handle_drag();
      return true;
    }
    update_gradient_fill_handle_drag(event->position(), event->modifiers());
    return true;
  }
  if (event->buttons() != Qt::NoButton || drawing_shape_) {
    return false;
  }
  // Under another tool Ctrl, Alt and Meta presses are that tool's (press_gradient_fill_handle
  // lets them through), so the hover shows that tool's cursor too.
  const bool tool_modifier_held =
      tool_ != CanvasTool::Gradient &&
      (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
  const auto part = gradient_fill_handle_layer() != nullptr && !tool_modifier_held
                        ? gradient_fill_handle_part_at(event->position())
                        : GradientHandlePart{};
  const bool was_hovering = gradient_handle_hover_.kind != PartKind::None;
  set_gradient_fill_handle_hover(part);
  if (part.kind == PartKind::None) {
    if (was_hovering || gradient_handle_cursor_ != GradientHandleCursor::None) {
      gradient_handle_cursor_ = GradientHandleCursor::None;
      update_tool_cursor();
    }
    return false;
  }
  apply_gradient_handle_cursor(gradient_handle_cursor_for(part));
  return true;
}

bool CanvasWidget::release_gradient_fill_handle(QMouseEvent* event) {
  if (!gradient_handle_drag_.has_value() || event->button() != Qt::LeftButton) {
    return false;
  }
  update_gradient_fill_handle_drag(event->position(), event->modifiers());
  auto drag = std::move(*gradient_handle_drag_);
  gradient_handle_drag_.reset();
  const auto send = [this, &drag](const LayerStyleGradient& gradient, GradientFillEditPhase phase,
                                  const QString& label) {
    if (gradient_fill_edit_callback_) {
      gradient_fill_edit_callback_(drag.layer_id, gradient, phase, drag.serial, label, drag.target);
    }
  };
  if (!drag.moved) {
    if (drag.part.kind == PartKind::Line && solid_stops(drag.original)) {
      // A click on the line adds a color stop there, in the color the gradient has there.
      const auto location = std::clamp(
          gradient_fill_line_location(drag.press_line, drag.original.reverse, drag.press_document.x(),
                                      drag.press_document.y()),
          0.0, 1.0);
      auto added = drag.original;
      const auto added_index = add_gradient_color_stop(added, static_cast<float>(location));
      send(added, GradientFillEditPhase::Commit, tr("Add Color Stop"));
      gradient_stop_selection_ =
          GradientStopSelection{drag.layer_id, false, static_cast<int>(added_index), drag.target};
    } else if (drag.part.kind == PartKind::AlphaLine && solid_stops(drag.original)) {
      // On the opacity side it adds an opacity stop, with the opacity the gradient has there.
      const auto location = std::clamp(
          gradient_fill_line_location(drag.press_line, drag.original.reverse, drag.press_document.x(),
                                      drag.press_document.y()),
          0.0, 1.0);
      auto added = drag.original;
      const auto added_index = add_gradient_alpha_stop(added, static_cast<float>(location));
      send(added, GradientFillEditPhase::Commit, tr("Add Opacity Stop"));
      gradient_stop_selection_ =
          GradientStopSelection{drag.layer_id, true, static_cast<int>(added_index), drag.target};
    }
  } else if (drag.current == drag.original) {
    if (drag.previewed) {
      send(drag.original, GradientFillEditPhase::Cancel, QString());
    }
  } else {
    QString label;
    switch (drag.part.kind) {
      case PartKind::Start:
        label = tr("Move Gradient Start Point");
        break;
      case PartKind::End:
        label = tr("Move Gradient End Point");
        break;
      case PartKind::Line:
      case PartKind::AlphaLine:
        label = tr("Move Gradient");
        break;
      case PartKind::ColorStop:
        label = drag.removing ? tr("Delete Color Stop") : tr("Move Color Stop");
        break;
      case PartKind::AlphaStop:
        label = drag.removing ? tr("Delete Opacity Stop") : tr("Move Opacity Stop");
        break;
      case PartKind::ColorMidpoint:
      case PartKind::AlphaMidpoint:
        label = tr("Move Gradient Midpoint");
        break;
      case PartKind::None:
        break;
    }
    send(drag.current, GradientFillEditPhase::Commit, label);
    if (drag.part.kind == PartKind::ColorStop || drag.part.kind == PartKind::AlphaStop) {
      if (drag.removing || drag.current_index < 0) {
        gradient_stop_selection_.reset();
      } else {
        gradient_stop_selection_ = GradientStopSelection{drag.layer_id, drag.part.kind == PartKind::AlphaStop,
                                                         drag.current_index, drag.target};
      }
    }
  }
  gradient_handle_hover_ = {};
  set_gradient_fill_handle_hover(gradient_fill_handle_layer() != nullptr
                                     ? gradient_fill_handle_part_at(event->position())
                                     : GradientHandlePart{});
  if (gradient_handle_hover_.kind == PartKind::None) {
    gradient_handle_cursor_ = GradientHandleCursor::None;
    update_tool_cursor();
  } else {
    apply_gradient_handle_cursor(gradient_handle_cursor_for(gradient_handle_hover_));
  }
  update();
  return true;
}

void CanvasWidget::cancel_gradient_fill_handle_drag() {
  if (!gradient_handle_drag_.has_value()) {
    return;
  }
  const auto drag = std::move(*gradient_handle_drag_);
  gradient_handle_drag_.reset();
  if (drag.previewed && gradient_fill_edit_callback_) {
    gradient_fill_edit_callback_(drag.layer_id, drag.original, GradientFillEditPhase::Cancel, drag.serial, QString(),
                                 drag.target);
  }
  gradient_handle_hover_ = {};
  gradient_handle_cursor_ = GradientHandleCursor::None;
  update_tool_cursor();
  update();
}

void CanvasWidget::abandon_gradient_fill_handle_drag() {
  // The document under the drag was swapped (undo, redo, a history jump): its preview went
  // with it, so nothing is restored and a later call for this drag is stale.
  gradient_handle_drag_.reset();
  gradient_handle_hover_ = {};
  gradient_handle_cursor_ = GradientHandleCursor::None;
  gradient_handle_bounds_cache_.reset();
  gradient_stop_selection_.reset();
  if (gradient_opacity_edit_.has_value()) {
    const QPointer<QWidget> popup = gradient_opacity_edit_->popup;
    gradient_opacity_edit_->finished = true;
    gradient_opacity_edit_.reset();
    if (popup != nullptr) {
      popup->close();
    }
  }
}

bool CanvasWidget::gradient_fill_midpoint_dimmed(GradientHandlePart part) const {
  if (part.kind != PartKind::AlphaMidpoint || part.index < 1) {
    return false;
  }
  const auto handles = gradient_fill_handles();
  if (!handles.has_value()) {
    return false;
  }
  const auto& stops = handles->gradient.alpha_stops;
  const auto index = static_cast<std::size_t>(part.index);
  return index < stops.size() && stops[index - 1U].opacity == stops[index].opacity;
}

CanvasWidget::GradientHandleCursor CanvasWidget::gradient_handle_cursor() const noexcept {
  return gradient_handle_cursor_;
}

CanvasWidget::GradientHandleCursor CanvasWidget::gradient_handle_cursor_for(GradientHandlePart part) {
  switch (part.kind) {
    case PartKind::Start:
    case PartKind::End:
      return GradientHandleCursor::Move;
    case PartKind::Line:
    case PartKind::AlphaLine:
      // A click here adds a stop; a press that moves on drags the gradient (Move from then on).
      return GradientHandleCursor::AddStop;
    case PartKind::ColorStop:
    case PartKind::AlphaStop:
    case PartKind::ColorMidpoint:
    case PartKind::AlphaMidpoint:
      return GradientHandleCursor::AlongLine;
    case PartKind::None:
      break;
  }
  return GradientHandleCursor::None;
}

void CanvasWidget::apply_gradient_handle_cursor(GradientHandleCursor kind) {
  if (kind == GradientHandleCursor::None) {
    gradient_handle_cursor_ = kind;
    update_tool_cursor();
    return;
  }
  gradient_handle_cursor_ = kind;
  setCursor(gradient_handle_qcursor(kind));
}

std::optional<CanvasWidget::GradientHandlePart> CanvasWidget::gradient_fill_selected_stop() const {
  if (!gradient_stop_selection_.has_value()) {
    return std::nullopt;
  }
  const auto* layer = gradient_fill_handle_layer();
  if (layer == nullptr || layer->id() != gradient_stop_selection_->layer_id ||
      gradient_handle_target(*layer) != gradient_stop_selection_->target) {
    return std::nullopt;
  }
  const auto& gradient = gradient_handle_gradient(*layer);
  const auto count = gradient_stop_selection_->alpha ? gradient.alpha_stops.size() : gradient.color_stops.size();
  if (gradient_stop_selection_->index < 0 || static_cast<std::size_t>(gradient_stop_selection_->index) >= count) {
    return std::nullopt;
  }
  return GradientHandlePart{gradient_stop_selection_->alpha ? PartKind::AlphaStop : PartKind::ColorStop,
                            gradient_stop_selection_->index};
}

bool CanvasWidget::delete_selected_gradient_stop() {
  const auto selected = gradient_fill_selected_stop();
  if (!selected.has_value() || gradient_handle_drag_.has_value()) {
    gradient_stop_selection_.reset();
    return false;
  }
  const auto* layer = gradient_fill_handle_layer();
  if (!vector_lock_reason(*layer).empty() || layer_effectively_locks_image_pixels(*layer)) {
    report_status_error(tr("Unlock the Gradient Fill layer to change its gradient"));
    return true;
  }
  const bool alpha = selected->kind == PartKind::AlphaStop;
  const auto target = gradient_stop_selection_->target;
  auto edited = gradient_handle_gradient(*layer);
  const auto index = static_cast<std::size_t>(selected->index);
  const bool removed = alpha ? remove_gradient_alpha_stop(edited, index) : remove_gradient_color_stop(edited, index);
  if (!removed) {
    // The stop stays selected; the key does nothing else (it never reaches Edit > Clear).
    report_status_error(alpha ? tr("A gradient keeps at least two opacity stops.")
                              : tr("A gradient keeps at least two color stops."));
    return true;
  }
  gradient_stop_selection_.reset();
  if (gradient_fill_edit_callback_) {
    const auto layer_id = layer->id();
    gradient_fill_edit_callback_(layer_id, edited, GradientFillEditPhase::Commit, ++gradient_handle_drag_serial_,
                                 alpha ? tr("Delete Opacity Stop") : tr("Delete Color Stop"), target);
  }
  update();
  return true;
}

bool CanvasWidget::show_gradient_stop_context_menu(QPoint widget_point, QPoint global_position) {
  const auto* layer = gradient_fill_handle_layer();
  if (layer == nullptr || gradient_handle_drag_.has_value()) {
    return false;
  }
  const auto part = gradient_fill_handle_part_at(QPointF(widget_point));
  if (part.kind != PartKind::ColorStop && part.kind != PartKind::AlphaStop) {
    return false;
  }
  const bool alpha = part.kind == PartKind::AlphaStop;
  const auto layer_id = layer->id();
  const auto target = gradient_handle_target(*layer).value_or(GradientHandleTarget::Fill);
  gradient_stop_selection_ = GradientStopSelection{layer_id, alpha, part.index, target};
  update();
  const auto& gradient = gradient_handle_gradient(*layer);
  const bool deletable = (alpha ? gradient.alpha_stops.size() : gradient.color_stops.size()) > 2U;
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("gradientStopContextMenu"));
  menu->setToolTipsVisible(true);
  canvas_context_menu_ = menu;
  connect(menu, &QMenu::aboutToHide, this, [this, menu] { retire_canvas_context_menu(menu); });
  connect(menu, &QMenu::triggered, this, [this, menu](QAction*) {
    std::erase_if(retired_context_menus_,
                  [menu](const RetiredContextMenu& retired) { return retired.menu == menu; });
    menu->deleteLater();
  });
  // Each entry re-checks that its stop is still the selected one on the same layer.
  const auto still_selected = [this, layer_id, alpha, index = part.index] {
    const auto selected = gradient_fill_selected_stop();
    return selected.has_value() && gradient_stop_selection_->layer_id == layer_id &&
           (selected->kind == PartKind::AlphaStop) == alpha && selected->index == index;
  };
  if (alpha) {
    auto* opacity = menu->addAction(tr("Opacity..."));
    opacity->setObjectName(QStringLiteral("gradientStopOpacityAction"));
    connect(opacity, &QAction::triggered, this, [this, still_selected, index = part.index] {
      if (still_selected()) {
        open_gradient_opacity_popup(GradientHandlePart{PartKind::AlphaStop, index});
      }
    });
  }
  auto* remove = menu->addAction(tr("Delete Stop"));
  remove->setObjectName(QStringLiteral("gradientStopDeleteAction"));
  remove->setEnabled(deletable);
  if (!deletable) {
    remove->setToolTip(alpha ? tr("A gradient keeps at least two opacity stops.")
                             : tr("A gradient keeps at least two color stops."));
  }
  connect(remove, &QAction::triggered, this, [this, still_selected] {
    if (still_selected()) {
      delete_selected_gradient_stop();
    }
  });
  if (alpha) {
    auto* settings = menu->addAction(tr("Fill Layer Settings..."));
    settings->setObjectName(QStringLiteral("gradientStopFillSettingsAction"));
    connect(settings, &QAction::triggered, this, [this, still_selected] {
      if (still_selected() && shape_appearance_requested_callback_) {
        shape_appearance_requested_callback_();
      }
    });
  } else {
    auto* color = menu->addAction(tr("Change Color..."));
    color->setObjectName(QStringLiteral("gradientStopColorAction"));
    connect(color, &QAction::triggered, this, [this, still_selected, layer_id, target, index = part.index] {
      if (still_selected() && gradient_fill_stop_color_requested_callback_) {
        gradient_fill_stop_color_requested_callback_(layer_id, index, target);
      }
    });
    auto* opacity_here = menu->addAction(tr("Opacity Here..."));
    opacity_here->setObjectName(QStringLiteral("gradientStopOpacityHereAction"));
    opacity_here->setToolTip(tr("Set the gradient's opacity at this color stop (adds an opacity stop here when "
                                "there is none)"));
    connect(opacity_here, &QAction::triggered, this, [this, still_selected, index = part.index] {
      if (still_selected()) {
        open_gradient_opacity_popup(GradientHandlePart{PartKind::ColorStop, index});
      }
    });
  }
  menu->popup(global_position);
  return true;
}

bool CanvasWidget::double_click_gradient_fill_handle(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || gradient_handle_drag_.has_value()) {
    return false;
  }
  const auto* layer = gradient_fill_handle_layer();
  if (layer == nullptr) {
    return false;
  }
  const auto part = gradient_fill_handle_part_at(event->position());
  switch (part.kind) {
    case PartKind::ColorStop:
      if (!vector_lock_reason(*layer).empty() || layer_effectively_locks_image_pixels(*layer)) {
        report_status_error(tr("Unlock the Gradient Fill layer to change its gradient"));
      } else if (gradient_fill_stop_color_requested_callback_) {
        gradient_fill_stop_color_requested_callback_(
            layer->id(), part.index, gradient_handle_target(*layer).value_or(GradientHandleTarget::Fill));
      }
      return true;
    case PartKind::AlphaStop:
      // The opacity popup beside the stop.
      open_gradient_opacity_popup(part);
      return true;
    case PartKind::Start:
    case PartKind::End:
    case PartKind::Line:
    case PartKind::AlphaLine:
    case PartKind::ColorMidpoint:
    case PartKind::AlphaMidpoint:
      // The first click already did its work (a click on the line added a stop); the second
      // must not add another one.
      return true;
    case PartKind::None:
      break;
  }
  return false;
}

bool CanvasWidget::show_gradient_fill_handle_tooltip(QEvent* event) {
  if (event->type() != QEvent::ToolTip || !gradient_handles_enabled()) {
    return false;
  }
  const auto* help = static_cast<QHelpEvent*>(event);
  const auto part = gradient_fill_handle_layer() != nullptr ? gradient_fill_handle_part_at(QPointF(help->pos()))
                                                            : GradientHandlePart{};
  if (part.kind == PartKind::None) {
    return false;
  }
  QToolTip::showText(help->globalPos(), gradient_fill_handle_tooltip(part), this);
  return true;
}

namespace {

// Enter commits, Esc cancels, and the popup closing any other way (a click away from it, a
// tool switch) commits: the opacity popup's keys and close, watched on the popup and its
// fields before they handle them.
class GradientOpacityPopupKeys final : public QObject {
public:
  GradientOpacityPopupKeys(QObject* parent, std::function<void(bool)> finish)
      : QObject(parent), finish_(std::move(finish)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<QKeyEvent*>(event);
      if (key->key() == Qt::Key_Escape) {
        finish_(false);
        return true;
      }
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        if (auto* spin = qobject_cast<QSpinBox*>(watched); spin != nullptr) {
          spin->interpretText();
        }
        finish_(true);
        return true;
      }
    } else if (event->type() == QEvent::Hide && watched == parent()) {
      finish_(true);
    }
    return false;
  }

private:
  std::function<void(bool)> finish_;
};

}  // namespace

QWidget* CanvasWidget::gradient_opacity_popup() const {
  return gradient_opacity_edit_.has_value() ? gradient_opacity_edit_->popup.data() : nullptr;
}

bool CanvasWidget::open_gradient_opacity_popup(GradientHandlePart part) {
  if (gradient_handle_drag_.has_value() || gradient_opacity_edit_.has_value()) {
    return false;
  }
  const auto* layer = gradient_fill_handle_layer();
  if (layer == nullptr || (part.kind != PartKind::AlphaStop && part.kind != PartKind::ColorStop) || part.index < 0) {
    return false;
  }
  if (!vector_lock_reason(*layer).empty() || layer_effectively_locks_image_pixels(*layer)) {
    report_status_error(tr("Unlock the Gradient Fill layer to change its gradient"));
    return true;
  }
  const auto& gradient = gradient_handle_gradient(*layer);
  const auto index = static_cast<std::size_t>(part.index);
  GradientOpacityEdit edit;
  edit.target = gradient_handle_target(*layer).value_or(GradientHandleTarget::Fill);
  edit.layer_id = layer->id();
  edit.original = gradient;
  edit.serial = ++gradient_handle_drag_serial_;
  if (part.kind == PartKind::AlphaStop) {
    if (index >= gradient.alpha_stops.size()) {
      return false;
    }
    edit.alpha_index = part.index;
    edit.location = gradient.alpha_stops[index].location;
  } else {
    if (index >= gradient.color_stops.size()) {
      return false;
    }
    // Opacity Here: the opacity stop already at this color stop, else a new one on commit.
    edit.location = gradient.color_stops[index].location;
    constexpr float kSameLocation = 0.001F;
    for (std::size_t alpha = 0; alpha < gradient.alpha_stops.size(); ++alpha) {
      if (std::abs(gradient.alpha_stops[alpha].location - edit.location) <= kSameLocation) {
        edit.alpha_index = static_cast<int>(alpha);
        break;
      }
    }
  }
  const auto opacity = edit.alpha_index >= 0
                           ? gradient.alpha_stops[static_cast<std::size_t>(edit.alpha_index)].opacity
                           : gradient_stop_opacity(gradient, edit.location, true);
  edit.initial_percent = std::clamp(static_cast<int>(std::lround(opacity * 100.0F)), 0, 100);
  edit.percent = edit.initial_percent;

  auto* popup = new QFrame(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  // A click away closes and commits; it must not also start a gradient drag on the canvas.
  popup->setAttribute(Qt::WA_NoMouseReplay);
  popup->setObjectName(QStringLiteral("gradientOpacityStopPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QHBoxLayout(popup);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(6);
  auto* label = new QLabel(tr("Opacity:"), popup);
  auto* slider = new QSlider(Qt::Horizontal, popup);
  slider->setObjectName(QStringLiteral("gradientOpacityStopSlider"));
  slider->setRange(0, 100);
  slider->setValue(edit.percent);
  slider->setMinimumWidth(120);
  auto* spin = new QSpinBox(popup);
  spin->setObjectName(QStringLiteral("gradientOpacityStopSpin"));
  spin->setRange(0, 100);
  spin->setSuffix(QStringLiteral("%"));
  spin->setValue(edit.percent);
  spin->setAccessibleName(tr("Stop opacity"));
  layout->addWidget(label);
  layout->addWidget(slider, 1);
  layout->addWidget(spin);
  edit.popup = popup;
  gradient_opacity_edit_ = std::move(edit);

  connect(slider, &QSlider::valueChanged, popup, [this, spin](int value) {
    const QSignalBlocker blocker(spin);
    spin->setValue(value);
    set_gradient_opacity_popup_value(value);
  });
  connect(spin, qOverload<int>(&QSpinBox::valueChanged), popup, [this, slider](int value) {
    const QSignalBlocker blocker(slider);
    slider->setValue(value);
    set_gradient_opacity_popup_value(value);
  });
  auto* keys = new GradientOpacityPopupKeys(popup, [this](bool commit) { finish_gradient_opacity_popup(commit); });
  for (QObject* watched : std::initializer_list<QObject*>{popup, slider, spin}) {
    watched->installEventFilter(keys);
  }

  // Beside the stop, on the far side of the line from it, kept on the screen.
  popup->adjustSize();
  const auto anchor = gradient_fill_handle_widget_position(part).value_or(QPointF(rect().center()));
  const auto handles = gradient_fill_handles();
  QPointF away(0.0, -1.0);
  if (handles.has_value()) {
    const auto start = widget_position_f(QPointF(handles->line.start_x, handles->line.start_y));
    const auto end = widget_position_f(QPointF(handles->line.end_x, handles->line.end_y));
    away = stop_side_normal(start, end) * (part.kind == PartKind::AlphaStop ? -1.0 : 1.0);
  }
  const QSize size = popup->sizeHint();
  QPointF corner = anchor + away * (kStopHalf + 10.0);
  // Grow away from the line: the popup's near edge sits at `corner`.
  corner.rx() -= size.width() / 2.0;
  if (away.y() < 0.0) {
    corner.ry() -= size.height();
  }
  QPoint global = mapToGlobal(corner.toPoint());
  if (const auto* screen = this->screen(); screen != nullptr) {
    const auto area = screen->availableGeometry();
    global.setX(std::clamp(global.x(), area.left(), std::max(area.left(), area.right() - size.width())));
    global.setY(std::clamp(global.y(), area.top(), std::max(area.top(), area.bottom() - size.height())));
  }
  popup->move(global);
  popup->show();
  spin->setFocus(Qt::PopupFocusReason);
  spin->selectAll();
  if (status_callback_) {
    status_callback_(tr("Set the opacity. Enter or a click outside keeps it, Esc puts it back."));
  }
  return true;
}

void CanvasWidget::set_gradient_opacity_popup_value(int percent) {
  if (!gradient_opacity_edit_.has_value() || gradient_opacity_edit_->finished) {
    return;
  }
  auto& edit = *gradient_opacity_edit_;
  edit.percent = std::clamp(percent, 0, 100);
  auto edited = edit.original;
  const auto opacity = static_cast<float>(edit.percent) / 100.0F;
  if (edit.alpha_index >= 0) {
    edited.alpha_stops[static_cast<std::size_t>(edit.alpha_index)].opacity = opacity;
  } else {
    const auto added = add_gradient_alpha_stop(edited, edit.location);
    edited.alpha_stops[added].opacity = opacity;
  }
  if (gradient_fill_edit_callback_) {
    edit.previewed = true;
    const auto layer_id = edit.layer_id;
    const auto serial = edit.serial;
    const auto target = edit.target;
    gradient_fill_edit_callback_(layer_id, edited, GradientFillEditPhase::Preview, serial, QString(), target);
  }
  update();
}

void CanvasWidget::finish_gradient_opacity_popup(bool commit) {
  if (!gradient_opacity_edit_.has_value() || gradient_opacity_edit_->finished) {
    return;
  }
  auto edit = *gradient_opacity_edit_;
  gradient_opacity_edit_->finished = true;
  const QPointer<QWidget> popup = edit.popup;
  gradient_opacity_edit_.reset();
  const bool inserting = edit.alpha_index < 0;
  const bool changed = inserting || edit.percent != edit.initial_percent;
  if (gradient_fill_edit_callback_) {
    if (commit && changed) {
      auto edited = edit.original;
      const auto opacity = static_cast<float>(edit.percent) / 100.0F;
      int selected = edit.alpha_index;
      if (inserting) {
        const auto added = add_gradient_alpha_stop(edited, edit.location);
        edited.alpha_stops[added].opacity = opacity;
        selected = static_cast<int>(added);
      } else {
        edited.alpha_stops[static_cast<std::size_t>(edit.alpha_index)].opacity = opacity;
      }
      gradient_fill_edit_callback_(edit.layer_id, edited, GradientFillEditPhase::Commit, edit.serial,
                                   inserting ? tr("Add Opacity Stop") : tr("Change Opacity Stop"), edit.target);
      gradient_stop_selection_ = GradientStopSelection{edit.layer_id, true, selected, edit.target};
    } else if (edit.previewed) {
      gradient_fill_edit_callback_(edit.layer_id, edit.original, GradientFillEditPhase::Cancel, edit.serial,
                                   QString(), edit.target);
    }
  }
  if (popup != nullptr && popup->isVisible()) {
    popup->close();
  }
  update();
}

void CanvasWidget::draw_gradient_fill_handles(QPainter& painter) const {
  const auto handles = gradient_fill_handles();
  if (!handles.has_value()) {
    return;
  }
  const auto geometry = handle_geometry(*handles, [this](QPointF point) { return widget_position_f(point); });
  const auto& palette = theme();
  const QColor ink = palette.canvas_gradient_handle_ink;
  const QColor light = palette.canvas_gradient_handle_fill;
  const QColor active = palette.canvas_gradient_handle_active;
  const auto highlighted = [&](PartKind kind, int index) {
    if (gradient_handle_drag_.has_value()) {
      const auto& drag = *gradient_handle_drag_;
      if (drag.part.kind != kind) {
        return false;
      }
      return index < 0 || (drag.part.kind == PartKind::ColorStop || drag.part.kind == PartKind::AlphaStop
                               ? index == drag.current_index
                               : index == drag.part.index);
    }
    if (const auto selected = gradient_fill_selected_stop(); selected.has_value() && selected->kind == kind &&
                                                               selected->index == index) {
      return true;
    }
    return gradient_handle_hover_.kind == kind && (index < 0 || gradient_handle_hover_.index == index);
  };

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  // The line: light over a wider dark band, readable on any artwork.
  painter.setBrush(Qt::NoBrush);
  const bool line_active = highlighted(PartKind::Line, -1) || highlighted(PartKind::AlphaLine, -1);
  painter.setPen(QPen(ink, 3.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(QLineF(geometry.start, geometry.end));
  painter.setPen(QPen(line_active ? active : light, 1.0, Qt::SolidLine, Qt::FlatCap));
  painter.drawLine(QLineF(geometry.start, geometry.end));

  const auto& gradient = handles->gradient;
  const auto draw_connector = [&](QPointF marker, double side) {
    const QPointF foot = marker - geometry.normal * (kStopOffset * side);
    painter.setPen(QPen(ink, 1.0));
    painter.drawLine(QLineF(foot, marker - geometry.normal * (kStopHalf * side)));
  };
  const auto draw_midpoints = [&](const std::vector<std::optional<QPointF>>& midpoints, PartKind kind) {
    for (std::size_t i = 0; i < midpoints.size(); ++i) {
      if (!midpoints[i].has_value()) {
        continue;
      }
      const bool on = highlighted(kind, static_cast<int>(i));
      // An opacity midpoint between equal opacities does nothing yet: drawn faded (the
      // same roles at reduced opacity), back to normal once the opacities differ.
      const bool dimmed = kind == PartKind::AlphaMidpoint && i < gradient.alpha_stops.size() && i > 0U &&
                          gradient.alpha_stops[i - 1U].opacity == gradient.alpha_stops[i].opacity;
      painter.setOpacity(dimmed && !on ? 0.4 : 1.0);
      painter.setPen(QPen(on ? active : ink, on ? 2.0 : 1.0));
      painter.setBrush(light);
      painter.drawPolygon(diamond_at(*midpoints[i], kMidpointHalf));
      painter.setOpacity(1.0);
    }
  };
  draw_midpoints(geometry.color_midpoints, PartKind::ColorMidpoint);
  draw_midpoints(geometry.alpha_midpoints, PartKind::AlphaMidpoint);
  const auto removing_index = [&](PartKind kind) {
    return gradient_handle_drag_.has_value() && gradient_handle_drag_->removing && gradient_handle_drag_->part.kind == kind;
  };
  // Color stops: a square in the stop's color with a light rim inside a dark outline.
  for (std::size_t i = 0; i < geometry.color_stops.size() && i < gradient.color_stops.size(); ++i) {
    const auto at = geometry.color_stops[i];
    draw_connector(at, 1.0);
    const auto& color = gradient.color_stops[i].color;
    const bool on = highlighted(PartKind::ColorStop, static_cast<int>(i));
    const QRectF box(at.x() - kStopHalf, at.y() - kStopHalf, kStopHalf * 2.0, kStopHalf * 2.0);
    painter.setPen(QPen(on ? active : ink, on ? 2.0 : 1.0));
    painter.setBrush(light);
    painter.drawRect(box.adjusted(-1.0, -1.0, 1.0, 1.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(color.red, color.green, color.blue));
    painter.drawRect(box.adjusted(1.0, 1.0, -1.0, -1.0));
  }
  // Opacity stops: a circle whose gray shows the opacity (black is opaque, like a mask).
  for (std::size_t i = 0; i < geometry.alpha_stops.size() && i < gradient.alpha_stops.size(); ++i) {
    const auto at = geometry.alpha_stops[i];
    draw_connector(at, -1.0);
    const auto value = 255 - static_cast<int>(std::lround(std::clamp(gradient.alpha_stops[i].opacity, 0.0F, 1.0F) * 255.0F));
    const bool on = highlighted(PartKind::AlphaStop, static_cast<int>(i));
    painter.setPen(QPen(on ? active : ink, on ? 2.0 : 1.0));
    painter.setBrush(light);
    painter.drawEllipse(at, kStopHalf + 1.0, kStopHalf + 1.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(value, value, value));
    painter.drawEllipse(at, kStopHalf - 1.0, kStopHalf - 1.0);
  }
  // A stop pulled off the line to be deleted follows the pointer, struck through.
  if (removing_index(PartKind::ColorStop) || removing_index(PartKind::AlphaStop)) {
    const auto at = gradient_handle_drag_->pointer_widget;
    painter.setPen(QPen(ink, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QLineF(at + QPointF(-kStopHalf, -kStopHalf), at + QPointF(kStopHalf, kStopHalf)));
    painter.drawLine(QLineF(at + QPointF(-kStopHalf, kStopHalf), at + QPointF(kStopHalf, -kStopHalf)));
    painter.setPen(QPen(active, 1.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QLineF(at + QPointF(-kStopHalf, -kStopHalf), at + QPointF(kStopHalf, kStopHalf)));
    painter.drawLine(QLineF(at + QPointF(-kStopHalf, kStopHalf), at + QPointF(kStopHalf, -kStopHalf)));
  }
  // The ends: light discs in a dark ring; the end handle carries a dark center dot.
  const auto draw_end = [&](QPointF at, bool end, bool on) {
    painter.setPen(QPen(on ? active : ink, on ? 2.5 : 1.5));
    painter.setBrush(light);
    painter.drawEllipse(at, kHandleRadius, kHandleRadius);
    if (end) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(on ? active : ink);
      painter.drawEllipse(at, 1.75, 1.75);
    }
  };
  draw_end(geometry.start, false, highlighted(PartKind::Start, -1));
  draw_end(geometry.end, true, highlighted(PartKind::End, -1));
  painter.restore();
}

}  // namespace patchy::ui
