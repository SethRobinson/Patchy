#include "ui/divide_photos_dialog.hpp"

#include "ui/action_icons.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_sequence_dialog.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace patchy::ui {
namespace {

constexpr double kMinRegionEdgeSourcePx = 8.0;
constexpr int kDetectDebounceMs = 250;
constexpr double kReplaceOverlapIou = 0.5;  // re-detected regions overlapping a user one drop

using QuadPoints = std::array<QPointF, 4>;  // TL, TR, BR, BL

[[nodiscard]] QuadPoints quad_points(const std::array<double, 8>& quad) {
  return {QPointF(quad[0], quad[1]), QPointF(quad[2], quad[3]), QPointF(quad[4], quad[5]),
          QPointF(quad[6], quad[7])};
}

[[nodiscard]] std::array<double, 8> quad_array(const QuadPoints& corners) {
  return {corners[0].x(), corners[0].y(), corners[1].x(), corners[1].y(),
          corners[2].x(), corners[2].y(), corners[3].x(), corners[3].y()};
}

[[nodiscard]] QRectF quad_bounds(const QuadPoints& corners) {
  QPolygonF polygon;
  for (const auto& corner : corners) {
    polygon.append(corner);
  }
  return polygon.boundingRect();
}

// The polygon a region shows and edits under a mode: the integer bounding box
// for Cut, the min-area rect for Straighten, the edge-fit quad for Perspective.
[[nodiscard]] QuadPoints region_mode_quad(const PhotoRegion& region, PhotoExtractMode mode) {
  if (mode == PhotoExtractMode::Cut) {
    const QRectF box(region.bounding_box.x, region.bounding_box.y, region.bounding_box.width,
                     region.bounding_box.height);
    return {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
  }
  if (mode == PhotoExtractMode::Perspective && region.perspective_quad) {
    return quad_points(region.perspective_corners);
  }
  return quad_points(region.quad);
}

[[nodiscard]] Rect enclosing_core_rect(const QuadPoints& corners) {
  const QRectF bounds = quad_bounds(corners);
  const auto left = static_cast<std::int32_t>(std::floor(bounds.left()));
  const auto top = static_cast<std::int32_t>(std::floor(bounds.top()));
  const auto right = static_cast<std::int32_t>(std::ceil(bounds.right()));
  const auto bottom = static_cast<std::int32_t>(std::ceil(bounds.bottom()));
  return Rect{left, top, std::max(1, right - left), std::max(1, bottom - top)};
}

// Editing writes the geometry of the CURRENT mode and syncs the others, and
// always marks the region user-owned so re-detection keeps it.

void assign_axis_rect(PhotoRegion& region, QRectF rect) {
  rect = rect.normalized();
  const auto left = static_cast<std::int32_t>(std::floor(rect.left() + 0.5));
  const auto top = static_cast<std::int32_t>(std::floor(rect.top() + 0.5));
  const auto width = std::max(1, static_cast<std::int32_t>(std::floor(rect.width() + 0.5)));
  const auto height = std::max(1, static_cast<std::int32_t>(std::floor(rect.height() + 0.5)));
  region.bounding_box = Rect{left, top, width, height};
  const QuadPoints corners = {QPointF(left, top), QPointF(left + width, top),
                              QPointF(left + width, top + height), QPointF(left, top + height)};
  region.quad = quad_array(corners);
  region.perspective_corners = region.quad;
  region.perspective_quad = false;
  region.angle_degrees = 0.0;
  region.user_added = true;
}

void assign_rect_quad(PhotoRegion& region, const QuadPoints& corners) {
  region.quad = quad_array(corners);
  region.perspective_corners = region.quad;
  region.perspective_quad = false;
  region.bounding_box = enclosing_core_rect(corners);
  const QPointF axis = corners[1] - corners[0];
  region.angle_degrees = std::atan2(axis.y(), axis.x()) * 180.0 / 3.14159265358979323846;
  region.user_added = true;
}

void assign_perspective_quad(PhotoRegion& region, const QuadPoints& corners) {
  region.perspective_corners = quad_array(corners);
  region.perspective_quad = true;
  region.bounding_box = enclosing_core_rect(corners);
  region.user_added = true;
}

// A pure translation moves all three geometries, so nothing is lost when the
// user drags a region in one mode and extracts in another.
void translate_region(PhotoRegion& region, QPointF delta) {
  region.bounding_box.x += static_cast<std::int32_t>(std::floor(delta.x() + 0.5));
  region.bounding_box.y += static_cast<std::int32_t>(std::floor(delta.y() + 0.5));
  for (int i = 0; i < 4; ++i) {
    region.quad[static_cast<std::size_t>(i * 2)] += delta.x();
    region.quad[static_cast<std::size_t>(i * 2 + 1)] += delta.y();
    region.perspective_corners[static_cast<std::size_t>(i * 2)] += delta.x();
    region.perspective_corners[static_cast<std::size_t>(i * 2 + 1)] += delta.y();
  }
  region.user_added = true;
}

[[nodiscard]] double bounding_box_iou(const Rect& a, const Rect& b) {
  const auto left = std::max(a.x, b.x);
  const auto top = std::max(a.y, b.y);
  const auto right = std::min(a.x + a.width, b.x + b.width);
  const auto bottom = std::min(a.y + a.height, b.y + b.height);
  if (right <= left || bottom <= top) {
    return 0.0;
  }
  const double intersection = static_cast<double>(right - left) * (bottom - top);
  const double union_area = static_cast<double>(a.width) * a.height +
                            static_cast<double>(b.width) * b.height - intersection;
  return union_area > 0.0 ? intersection / union_area : 0.0;
}

[[nodiscard]] QImage preview_image_from_pixels(const PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  const auto format = pixels.format();
  if (format.bit_depth == BitDepth::UInt8 && format.channels == 4) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      std::memcpy(image.scanLine(y), pixels.row(y).data(), static_cast<std::size_t>(pixels.width()) * 4);
    }
  } else {
    // Callers hand over rgba8 (pixels_from_image_rgba); anything else renders
    // grayscale-by-first-channel so the preview still shows something usable.
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      auto* line = image.scanLine(y);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const std::uint8_t value = pixels.pixel(x, y)[0];
        line[x * 4 + 0] = value;
        line[x * 4 + 1] = value;
        line[x * 4 + 2] = value;
        line[x * 4 + 3] = 255;
      }
    }
  }
  constexpr int kMaxPreviewEdge = 2048;
  if (std::max(image.width(), image.height()) > kMaxPreviewEdge) {
    image = image.scaled(kMaxPreviewEdge, kMaxPreviewEdge, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
  }
  return image;
}

class DividePhotosPreviewPane final : public QWidget {
public:
  DividePhotosPreviewPane(std::shared_ptr<const PixelBuffer> source,
                          std::vector<PhotoRegion>* regions, QWidget* parent)
      : QWidget(parent),
        source_width_(source->width()),
        source_height_(source->height()),
        regions_(regions) {
    setObjectName(QStringLiteral("dividePhotosPreviewPane"));
    setMinimumSize(360, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // Rendered once up front, like the photocopy preview: the source never
    // changes while the dialog is open.
    preview_ = preview_image_from_pixels(*source);
  }

  void set_mode(PhotoExtractMode mode) {
    mode_ = mode;
    refresh_view_properties();
    update();
  }

  void set_regions_changed_callback(std::function<void()> callback) {
    regions_changed_ = std::move(callback);
  }

  [[nodiscard]] int selected_region() const noexcept { return selected_; }

  void select_region(int index) {
    selected_ = index >= 0 && index < static_cast<int>(regions_->size()) ? index : -1;
    notify_regions_changed();
  }

  void regions_replaced() {
    selected_ = -1;
    drag_ = DragKind::None;
    frozen_view_.reset();
    refresh_view_properties();
    update();
  }

  void add_centered_region() {
    const double width = std::max(kMinRegionEdgeSourcePx, source_width_ / 4.0);
    const double height = std::max(kMinRegionEdgeSourcePx, source_height_ / 4.0);
    PhotoRegion region;
    assign_axis_rect(region, QRectF((source_width_ - width) / 2.0, (source_height_ - height) / 2.0,
                                    width, height));
    regions_->push_back(region);
    selected_ = static_cast<int>(regions_->size()) - 1;
    notify_regions_changed();
  }

  void remove_selected_region() {
    if (selected_ < 0 || selected_ >= static_cast<int>(regions_->size())) {
      return;
    }
    regions_->erase(regions_->begin() + selected_);
    selected_ = -1;
    notify_regions_changed();
  }

protected:
  struct ViewMetrics {
    double scale{0.0};
    QPointF origin;
  };

  // Fits the source image into the widget; frozen for the duration of a drag
  // (the photocopy preview's freeze-the-view rule).
  [[nodiscard]] ViewMetrics view_metrics() const {
    if (frozen_view_.has_value()) {
      return *frozen_view_;
    }
    ViewMetrics metrics;
    const auto available = rect().adjusted(12, 12, -12, -12);
    if (available.isEmpty() || source_width_ <= 0 || source_height_ <= 0) {
      return metrics;
    }
    metrics.scale = std::min(static_cast<double>(available.width()) / source_width_,
                             static_cast<double>(available.height()) / source_height_);
    metrics.origin =
        QPointF(available.x() + (available.width() - source_width_ * metrics.scale) / 2.0,
                available.y() + (available.height() - source_height_ * metrics.scale) / 2.0);
    return metrics;
  }

  [[nodiscard]] QPointF to_device(QPointF source, const ViewMetrics& metrics) const {
    return metrics.origin + source * metrics.scale;
  }

  [[nodiscard]] QPointF to_source(QPointF device, const ViewMetrics& metrics) const {
    if (metrics.scale <= 0.0) {
      return {};
    }
    return (device - metrics.origin) / metrics.scale;
  }

  [[nodiscard]] QuadPoints device_quad(const PhotoRegion& region, const ViewMetrics& metrics) const {
    QuadPoints corners = region_mode_quad(region, mode_);
    for (auto& corner : corners) {
      corner = to_device(corner, metrics);
    }
    return corners;
  }

  // Corner handles 0-3 (TL TR BR BL), edge midpoints 4-7 (top right bottom left).
  [[nodiscard]] std::array<QPointF, 8> handle_centers(const QuadPoints& corners) const {
    return {corners[0],
            corners[1],
            corners[2],
            corners[3],
            (corners[0] + corners[1]) / 2.0,
            (corners[1] + corners[2]) / 2.0,
            (corners[2] + corners[3]) / 2.0,
            (corners[3] + corners[0]) / 2.0};
  }

  [[nodiscard]] int handle_at(QPointF device_pos, const ViewMetrics& metrics) const {
    if (selected_ < 0 || selected_ >= static_cast<int>(regions_->size())) {
      return -1;
    }
    const auto handles = handle_centers(device_quad((*regions_)[static_cast<std::size_t>(selected_)], metrics));
    for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
      const QPointF delta = device_pos - handles[static_cast<std::size_t>(i)];
      if (std::abs(delta.x()) <= 6.0 && std::abs(delta.y()) <= 6.0) {
        return i;
      }
    }
    return -1;
  }

  [[nodiscard]] int region_at(QPointF device_pos, const ViewMetrics& metrics) const {
    for (int i = static_cast<int>(regions_->size()) - 1; i >= 0; --i) {
      QPolygonF polygon;
      for (const auto& corner : device_quad((*regions_)[static_cast<std::size_t>(i)], metrics)) {
        polygon.append(corner);
      }
      if (polygon.containsPoint(device_pos, Qt::OddEvenFill)) {
        return i;
      }
    }
    return -1;
  }

  void notify_regions_changed() {
    refresh_view_properties();
    if (regions_changed_) {
      regions_changed_();
    }
    update();
  }

  // Exposed for the UI tests.
  void refresh_view_properties() {
    const auto metrics = view_metrics();
    setProperty("regionCount", static_cast<int>(regions_->size()));
    setProperty("selectedRegionIndex", selected_);
    QVariantList rects;
    for (const auto& region : *regions_) {
      QPolygonF polygon;
      for (const auto& corner : device_quad(region, metrics)) {
        polygon.append(corner);
      }
      rects.push_back(polygon.boundingRect().toRect());
    }
    setProperty("regionRectsView", rects);
    QVariantList corners_list;
    if (selected_ >= 0 && selected_ < static_cast<int>(regions_->size())) {
      for (const auto& corner : device_quad((*regions_)[static_cast<std::size_t>(selected_)], metrics)) {
        corners_list.push_back(corner);
      }
    }
    setProperty("selectedRegionCornersView", corners_list);
  }

  [[nodiscard]] QPointF clamp_to_source(QPointF point) const {
    return QPointF(std::clamp(point.x(), 0.0, static_cast<double>(source_width_)),
                   std::clamp(point.y(), 0.0, static_cast<double>(source_height_)));
  }

  void apply_handle_drag(QPointF source_pos) {
    auto& region = (*regions_)[static_cast<std::size_t>(selected_)];
    const QPointF clamped = clamp_to_source(source_pos);
    if (mode_ == PhotoExtractMode::Cut) {
      QRectF rect(press_region_.bounding_box.x, press_region_.bounding_box.y,
                  press_region_.bounding_box.width, press_region_.bounding_box.height);
      const bool moves_left = active_handle_ == 0 || active_handle_ == 3 || active_handle_ == 7;
      const bool moves_right = active_handle_ == 1 || active_handle_ == 2 || active_handle_ == 5;
      const bool moves_top = active_handle_ == 0 || active_handle_ == 1 || active_handle_ == 4;
      const bool moves_bottom = active_handle_ == 2 || active_handle_ == 3 || active_handle_ == 6;
      if (moves_left) {
        rect.setLeft(std::min(clamped.x(), rect.right() - kMinRegionEdgeSourcePx));
      }
      if (moves_right) {
        rect.setRight(std::max(clamped.x(), rect.left() + kMinRegionEdgeSourcePx));
      }
      if (moves_top) {
        rect.setTop(std::min(clamped.y(), rect.bottom() - kMinRegionEdgeSourcePx));
      }
      if (moves_bottom) {
        rect.setBottom(std::max(clamped.y(), rect.top() + kMinRegionEdgeSourcePx));
      }
      assign_axis_rect(region, rect);
    } else if (mode_ == PhotoExtractMode::Perspective) {
      QuadPoints corners = region_mode_quad(press_region_, mode_);
      if (active_handle_ < 4) {
        corners[static_cast<std::size_t>(active_handle_)] = clamped;
      } else {
        const int first = active_handle_ - 4;
        const int second = (first + 1) % 4;
        const QPointF press_mid =
            (corners[static_cast<std::size_t>(first)] + corners[static_cast<std::size_t>(second)]) / 2.0;
        const QPointF delta = clamped - press_mid;
        corners[static_cast<std::size_t>(first)] += delta;
        corners[static_cast<std::size_t>(second)] += delta;
      }
      assign_perspective_quad(region, corners);
    } else {
      apply_straighten_handle_drag(region, clamped);
    }
    notify_regions_changed();
  }

  void apply_straighten_handle_drag(PhotoRegion& region, QPointF source_pos) {
    const QuadPoints press = region_mode_quad(press_region_, PhotoExtractMode::Straighten);
    const QPointF center =
        (press[0] + press[1] + press[2] + press[3]) / 4.0;
    if (rotating_) {
      const QPointF press_vector = press_source_ - center;
      const QPointF current_vector = source_pos - center;
      const double press_angle = std::atan2(press_vector.y(), press_vector.x());
      const double current_angle = std::atan2(current_vector.y(), current_vector.x());
      const double delta = current_angle - press_angle;
      const double cos_delta = std::cos(delta);
      const double sin_delta = std::sin(delta);
      QuadPoints corners;
      for (int i = 0; i < 4; ++i) {
        const QPointF offset = press[static_cast<std::size_t>(i)] - center;
        corners[static_cast<std::size_t>(i)] =
            center + QPointF(offset.x() * cos_delta - offset.y() * sin_delta,
                             offset.x() * sin_delta + offset.y() * cos_delta);
      }
      assign_rect_quad(region, corners);
      return;
    }
    QPointF axis_u = press[1] - press[0];
    QPointF axis_v = press[3] - press[0];
    const double width = std::hypot(axis_u.x(), axis_u.y());
    const double height = std::hypot(axis_v.x(), axis_v.y());
    if (width <= 0.0 || height <= 0.0) {
      return;
    }
    axis_u /= width;
    axis_v /= height;
    double new_width = width;
    double new_height = height;
    QPointF new_center = center;
    if (active_handle_ < 4) {
      const int anchor_index = (active_handle_ + 2) % 4;
      const QPointF anchor = press[static_cast<std::size_t>(anchor_index)];
      const QPointF delta = source_pos - anchor;
      const double sign_u = (active_handle_ == 1 || active_handle_ == 2) ? 1.0 : -1.0;
      const double sign_v = (active_handle_ == 2 || active_handle_ == 3) ? 1.0 : -1.0;
      new_width = std::max(kMinRegionEdgeSourcePx,
                           sign_u * (delta.x() * axis_u.x() + delta.y() * axis_u.y()));
      new_height = std::max(kMinRegionEdgeSourcePx,
                            sign_v * (delta.x() * axis_v.x() + delta.y() * axis_v.y()));
      new_center = anchor + axis_u * (sign_u * new_width / 2.0) + axis_v * (sign_v * new_height / 2.0);
    } else {
      // Edge handles slide one edge along its normal; the opposite edge stays.
      const int edge = active_handle_ - 4;  // 0 top, 1 right, 2 bottom, 3 left
      if (edge == 0 || edge == 2) {
        const QPointF fixed_mid = edge == 0 ? (press[2] + press[3]) / 2.0 : (press[0] + press[1]) / 2.0;
        const QPointF delta = source_pos - fixed_mid;
        const double sign = edge == 0 ? -1.0 : 1.0;
        new_height = std::max(kMinRegionEdgeSourcePx,
                              sign * (delta.x() * axis_v.x() + delta.y() * axis_v.y()));
        new_center = fixed_mid + axis_v * (sign * new_height / 2.0);
      } else {
        const QPointF fixed_mid = edge == 3 ? (press[1] + press[2]) / 2.0 : (press[3] + press[0]) / 2.0;
        const QPointF delta = source_pos - fixed_mid;
        const double sign = edge == 3 ? -1.0 : 1.0;
        new_width = std::max(kMinRegionEdgeSourcePx,
                             sign * (delta.x() * axis_u.x() + delta.y() * axis_u.y()));
        new_center = fixed_mid + axis_u * (sign * new_width / 2.0);
      }
    }
    const QuadPoints corners = {
        new_center - axis_u * (new_width / 2.0) - axis_v * (new_height / 2.0),
        new_center + axis_u * (new_width / 2.0) - axis_v * (new_height / 2.0),
        new_center + axis_u * (new_width / 2.0) + axis_v * (new_height / 2.0),
        new_center - axis_u * (new_width / 2.0) + axis_v * (new_height / 2.0),
    };
    assign_rect_quad(region, corners);
  }

  void apply_move_drag(QPointF source_pos) {
    auto& region = (*regions_)[static_cast<std::size_t>(selected_)];
    QPointF delta = source_pos - press_source_;
    // Keep the region's center inside the source so it can never be dragged
    // completely out of reach.
    const QuadPoints press = region_mode_quad(press_region_, mode_);
    const QPointF center = (press[0] + press[1] + press[2] + press[3]) / 4.0 + delta;
    const QPointF clamped_center = clamp_to_source(center);
    delta += clamped_center - center;
    region = press_region_;
    translate_region(region, delta);
    notify_regions_changed();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      return;
    }
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0) {
      return;
    }
    frozen_view_ = metrics;
    press_source_ = to_source(event->position(), metrics);
    active_handle_ = handle_at(event->position(), metrics);
    if (active_handle_ >= 0) {
      drag_ = DragKind::Handle;
      rotating_ = mode_ == PhotoExtractMode::Straighten && active_handle_ < 4 &&
                  event->modifiers().testFlag(Qt::AltModifier);
      press_region_ = (*regions_)[static_cast<std::size_t>(selected_)];
      return;
    }
    const int hit = region_at(event->position(), metrics);
    if (hit >= 0) {
      select_region(hit);
      drag_ = DragKind::Move;
      press_region_ = (*regions_)[static_cast<std::size_t>(hit)];
      return;
    }
    select_region(-1);
    drag_ = DragKind::NewRegion;
    rubber_rect_ = QRectF(clamp_to_source(press_source_), QSizeF(0, 0));
    update();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0) {
      return;
    }
    const QPointF source_pos = to_source(event->position(), metrics);
    switch (drag_) {
      case DragKind::Handle:
        apply_handle_drag(source_pos);
        return;
      case DragKind::Move:
        apply_move_drag(source_pos);
        return;
      case DragKind::NewRegion:
        rubber_rect_ = QRectF(clamp_to_source(press_source_), clamp_to_source(source_pos)).normalized();
        update();
        return;
      case DragKind::None:
        break;
    }
    if (handle_at(event->position(), metrics) >= 0) {
      setCursor(Qt::CrossCursor);
    } else if (region_at(event->position(), metrics) >= 0) {
      setCursor(Qt::SizeAllCursor);
    } else {
      setCursor(Qt::ArrowCursor);
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      return;
    }
    if (drag_ == DragKind::NewRegion && rubber_rect_.width() >= kMinRegionEdgeSourcePx &&
        rubber_rect_.height() >= kMinRegionEdgeSourcePx) {
      PhotoRegion region;
      assign_axis_rect(region, rubber_rect_);
      regions_->push_back(region);
      selected_ = static_cast<int>(regions_->size()) - 1;
      notify_regions_changed();
    }
    drag_ = DragKind::None;
    rotating_ = false;
    rubber_rect_ = QRectF();
    frozen_view_.reset();
    refresh_view_properties();
    update();
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
      remove_selected_region();
      return;
    }
    QWidget::keyPressEvent(event);
  }

  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    refresh_view_properties();
  }

  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(36, 36, 36));
    const auto metrics = view_metrics();
    if (metrics.scale <= 0.0 || preview_.isNull()) {
      return;
    }
    const QRectF image_rect(metrics.origin,
                            QSizeF(source_width_ * metrics.scale, source_height_ * metrics.scale));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(image_rect, preview_, QRectF(preview_.rect()));

    // Dim what falls outside every region (it will not become a photo).
    QPainterPath outside;
    outside.addRect(image_rect);
    for (const auto& region : *regions_) {
      QPolygonF polygon;
      for (const auto& corner : device_quad(region, metrics)) {
        polygon.append(corner);
      }
      QPainterPath region_path;
      region_path.addPolygon(polygon);
      region_path.closeSubpath();
      outside = outside.subtracted(region_path);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillPath(outside, QColor(15, 15, 15, 120));

    // Region outlines: a black and white pair reads over arbitrary photo
    // content, like the other selection chrome (deliberately theme-exempt).
    for (int i = 0; i < static_cast<int>(regions_->size()); ++i) {
      QPolygonF polygon;
      for (const auto& corner : device_quad((*regions_)[static_cast<std::size_t>(i)], metrics)) {
        polygon.append(corner);
      }
      polygon.append(polygon.first());
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(QColor(255, 255, 255), i == selected_ ? 2.0 : 1.0));
      painter.drawPolyline(polygon);
      painter.setPen(QPen(QColor(0, 0, 0), 1.0, Qt::DashLine));
      painter.drawPolyline(polygon);
    }
    if (drag_ == DragKind::NewRegion && !rubber_rect_.isEmpty()) {
      const QRectF device_rect(to_device(rubber_rect_.topLeft(), metrics),
                               to_device(rubber_rect_.bottomRight(), metrics));
      painter.setPen(QPen(QColor(255, 255, 255), 1.0));
      painter.drawRect(device_rect);
      painter.setPen(QPen(QColor(0, 0, 0), 1.0, Qt::DashLine));
      painter.drawRect(device_rect);
    }
    if (selected_ >= 0 && selected_ < static_cast<int>(regions_->size())) {
      const auto handles =
          handle_centers(device_quad((*regions_)[static_cast<std::size_t>(selected_)], metrics));
      painter.setRenderHint(QPainter::Antialiasing, false);
      for (const auto& handle : handles) {
        painter.setPen(QPen(QColor(0, 0, 0), 1.0));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawRect(QRectF(handle.x() - 3.5, handle.y() - 3.5, 7.0, 7.0));
      }
    }
  }

private:
  enum class DragKind { None, Move, Handle, NewRegion };

  QImage preview_;
  std::int32_t source_width_{0};
  std::int32_t source_height_{0};
  std::vector<PhotoRegion>* regions_{nullptr};
  std::function<void()> regions_changed_;
  PhotoExtractMode mode_{PhotoExtractMode::Straighten};
  int selected_{-1};
  DragKind drag_{DragKind::None};
  bool rotating_{false};
  int active_handle_{-1};
  QPointF press_source_;
  PhotoRegion press_region_;
  QRectF rubber_rect_;
  std::optional<ViewMetrics> frozen_view_;
};

}  // namespace

std::optional<DividePhotosDialogResult> request_divide_photos(
    QWidget* parent, std::shared_ptr<const PixelBuffer> source, double source_ppi,
    const DividePhotosSettings& initial, const std::vector<DividePhotosFormatChoice>& formats) {
  if (source == nullptr || source->empty()) {
    return std::nullopt;
  }
  const bool initial_perspective = initial.mode == PhotoExtractMode::Perspective;
  const bool initial_straighten = initial.mode != PhotoExtractMode::Cut;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("dividePhotosDialog"));
  dialog.setWindowTitle(QObject::tr("Divide Scanned Photos"));
  dialog.resize(880, 640);

  auto* root = new QHBoxLayout(&dialog);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(14);

  std::vector<PhotoRegion> regions;
  auto* preview = new DividePhotosPreviewPane(source, &regions, &dialog);
  root->addWidget(preview, 1);

  auto* side = new QWidget(&dialog);
  auto* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(0, 0, 0, 0);
  side_layout->setSpacing(10);
  root->addWidget(side, 0);
  side->setFixedWidth(250);

  auto* detection_group = new QGroupBox(QObject::tr("Detection"), side);
  auto* detection_form = new QFormLayout(detection_group);
  auto* sensitivity_spin = add_dialog_slider_spin_row(
      detection_form, detection_group, QObject::tr("Sensitivity"),
      QStringLiteral("dividePhotosSensitivitySlider"), QStringLiteral("dividePhotosSensitivitySpin"),
      0, 100, std::clamp(initial.sensitivity, 0, 100), QStringLiteral("%"));
  auto* count_label = new QLabel(detection_group);
  count_label->setObjectName(QStringLiteral("dividePhotosCountLabel"));
  detection_form->addRow(QString(), count_label);
  side_layout->addWidget(detection_group);

  auto* adjust_group = new QGroupBox(QObject::tr("Adjustments"), side);
  auto* adjust_layout = new QVBoxLayout(adjust_group);
  auto* straighten_check = new QCheckBox(QObject::tr("Straighten photos"), adjust_group);
  straighten_check->setObjectName(QStringLiteral("dividePhotosStraightenCheck"));
  straighten_check->setChecked(initial_straighten || initial_perspective);
  adjust_layout->addWidget(straighten_check);
  auto* perspective_check = new QCheckBox(QObject::tr("Fix perspective"), adjust_group);
  perspective_check->setObjectName(QStringLiteral("dividePhotosPerspectiveCheck"));
  perspective_check->setChecked(initial_perspective);
  adjust_layout->addWidget(perspective_check);
  auto* direction_label = new QLabel(QObject::tr("Top edge of the photos points:"), adjust_group);
  direction_label->setObjectName(QStringLiteral("dividePhotosUpDirectionLabel"));
  direction_label->setWordWrap(true);
  adjust_layout->addWidget(direction_label);
  auto* direction_group = new QButtonGroup(adjust_group);
  direction_group->setObjectName(QStringLiteral("dividePhotosUpDirectionGroup"));
  direction_group->setExclusive(true);
  auto* direction_buttons = new QHBoxLayout();
  direction_buttons->setSpacing(4);
  const std::array<QString, 4> direction_tooltips = {
      QObject::tr("Top edge points up (no rotation)"), QObject::tr("Top edge points right"),
      QObject::tr("Top edge points down"), QObject::tr("Top edge points left")};
  const std::array<QString, 4> direction_names = {
      QStringLiteral("dividePhotosUpDirectionUpButton"),
      QStringLiteral("dividePhotosUpDirectionRightButton"),
      QStringLiteral("dividePhotosUpDirectionDownButton"),
      QStringLiteral("dividePhotosUpDirectionLeftButton")};
  for (int direction = 0; direction < 4; ++direction) {
    auto* button = new QToolButton(adjust_group);
    button->setObjectName(direction_names[static_cast<std::size_t>(direction)]);
    button->setCheckable(true);
    button->setIcon(up_direction_arrow_icon(direction));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(direction_tooltips[static_cast<std::size_t>(direction)]);
    direction_group->addButton(button, direction);
    direction_buttons->addWidget(button);
  }
  direction_buttons->addStretch(1);
  adjust_layout->addLayout(direction_buttons);
  {
    const int initial_direction = std::clamp(static_cast<int>(initial.up_direction), 0, 3);
    direction_group->button(initial_direction)->setChecked(true);
  }
  side_layout->addWidget(adjust_group);

  auto* regions_group = new QGroupBox(QObject::tr("Regions"), side);
  auto* regions_layout = new QVBoxLayout(regions_group);
  auto* region_buttons = new QHBoxLayout();
  auto* add_button = new QPushButton(QObject::tr("Add Region"), regions_group);
  add_button->setObjectName(QStringLiteral("dividePhotosAddRegionButton"));
  auto* remove_button = new QPushButton(QObject::tr("Remove"), regions_group);
  remove_button->setObjectName(QStringLiteral("dividePhotosRemoveRegionButton"));
  region_buttons->addWidget(add_button);
  region_buttons->addWidget(remove_button);
  regions_layout->addLayout(region_buttons);
  auto* region_hint = new QLabel(
      QObject::tr("Drag on the image to add a photo by hand; drag corners to adjust."), regions_group);
  region_hint->setObjectName(QStringLiteral("dividePhotosRegionHintLabel"));
  region_hint->setWordWrap(true);
  regions_layout->addWidget(region_hint);
  side_layout->addWidget(regions_group);

  auto* output_group = new QGroupBox(QObject::tr("Output"), side);
  auto* output_layout = new QVBoxLayout(output_group);
  auto* open_radio = new QRadioButton(QObject::tr("Open each photo as a new image"), output_group);
  open_radio->setObjectName(QStringLiteral("dividePhotosOpenDocumentsRadio"));
  auto* folder_radio = new QRadioButton(QObject::tr("Save photos to a folder"), output_group);
  folder_radio->setObjectName(QStringLiteral("dividePhotosSaveFolderRadio"));
  auto* both_radio = new QRadioButton(QObject::tr("Save to a folder and open"), output_group);
  both_radio->setObjectName(QStringLiteral("dividePhotosSaveAndOpenRadio"));
  output_layout->addWidget(open_radio);
  output_layout->addWidget(folder_radio);
  output_layout->addWidget(both_radio);
  folder_radio->setChecked(initial.output == DividePhotosOutput::SaveToFolder);
  both_radio->setChecked(initial.output == DividePhotosOutput::SaveAndOpen);
  open_radio->setChecked(initial.output == DividePhotosOutput::OpenDocuments);

  auto* save_form = new QFormLayout();
  save_form->setContentsMargins(0, 4, 0, 0);
  auto* folder_label = new QLabel(QObject::tr("Folder"), output_group);
  auto* folder_row = new QWidget(output_group);
  auto* folder_row_layout = new QHBoxLayout(folder_row);
  folder_row_layout->setContentsMargins(0, 0, 0, 0);
  folder_row_layout->setSpacing(4);
  auto* folder_edit = new QLineEdit(folder_row);
  folder_edit->setObjectName(QStringLiteral("dividePhotosFolderEdit"));
  folder_edit->setText(QDir::toNativeSeparators(initial.folder));
  auto* browse_button = new QPushButton(QStringLiteral("..."), folder_row);
  browse_button->setObjectName(QStringLiteral("dividePhotosFolderBrowseButton"));
  browse_button->setToolTip(QObject::tr("Choose Folder..."));
  configure_compact_symbol_button(browse_button);
  folder_row_layout->addWidget(folder_edit, 1);
  folder_row_layout->addWidget(browse_button, 0);
  save_form->addRow(folder_label, folder_row);
  auto* prefix_label = new QLabel(QObject::tr("Prefix"), output_group);
  auto* prefix_edit = new QLineEdit(output_group);
  prefix_edit->setObjectName(QStringLiteral("dividePhotosPrefixEdit"));
  prefix_edit->setText(initial.prefix);
  save_form->addRow(prefix_label, prefix_edit);
  auto* format_label = new QLabel(QObject::tr("Format"), output_group);
  auto* format_combo = new QComboBox(output_group);
  format_combo->setObjectName(QStringLiteral("dividePhotosFormatCombo"));
  for (const auto& choice : formats) {
    format_combo->addItem(choice.display_name, choice.extension);
  }
  {
    int format_index = format_combo->findData(initial.format);
    if (format_index < 0) {
      format_index = format_combo->findData(QStringLiteral("png"));
    }
    format_combo->setCurrentIndex(std::max(0, format_index));
  }
  save_form->addRow(format_label, format_combo);
  auto* existing_label = new QLabel(QObject::tr("If files exist"), output_group);
  auto* existing_combo = new QComboBox(output_group);
  existing_combo->setObjectName(QStringLiteral("dividePhotosExistingCombo"));
  existing_combo->addItem(QObject::tr("Add"),
                          static_cast<int>(DividePhotosExistingFiles::AddNumbering));
  existing_combo->addItem(QObject::tr("Overwrite"),
                          static_cast<int>(DividePhotosExistingFiles::Overwrite));
  existing_combo->setToolTip(
      QObject::tr("Add continues numbering after the files already in the folder; Overwrite "
                  "starts at 001 and asks before replacing anything."));
  existing_combo->setCurrentIndex(
      initial.existing_files == DividePhotosExistingFiles::Overwrite ? 1 : 0);
  save_form->addRow(existing_label, existing_combo);
  output_layout->addLayout(save_form);

  const auto save_selected = [folder_radio, both_radio] {
    return folder_radio->isChecked() || both_radio->isChecked();
  };
  const auto refresh_output_controls = [=] {
    const bool enabled = save_selected();
    for (QWidget* widget :
         {static_cast<QWidget*>(folder_label), static_cast<QWidget*>(folder_edit),
          static_cast<QWidget*>(browse_button), static_cast<QWidget*>(prefix_label),
          static_cast<QWidget*>(prefix_edit), static_cast<QWidget*>(format_label),
          static_cast<QWidget*>(format_combo), static_cast<QWidget*>(existing_label),
          static_cast<QWidget*>(existing_combo)}) {
      widget->setEnabled(enabled);
    }
  };
  QObject::connect(browse_button, &QPushButton::clicked, &dialog, [&dialog, folder_edit] {
    const auto chosen = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Choose Folder"),
                                                          folder_edit->text());
    if (!chosen.isEmpty()) {
      folder_edit->setText(QDir::toNativeSeparators(chosen));
    }
  });
#ifdef Q_OS_WASM
  // One browser download per photo would be refused as download spam (the
  // image-sequence export precedent); the browser build opens documents only.
  folder_radio->setVisible(false);
  both_radio->setVisible(false);
  for (QWidget* widget :
       {static_cast<QWidget*>(folder_label), static_cast<QWidget*>(folder_row),
        static_cast<QWidget*>(prefix_label), static_cast<QWidget*>(prefix_edit),
        static_cast<QWidget*>(format_label), static_cast<QWidget*>(format_combo),
        static_cast<QWidget*>(existing_label), static_cast<QWidget*>(existing_combo)}) {
    widget->setVisible(false);
  }
  open_radio->setChecked(true);
#endif
  side_layout->addWidget(output_group);

  side_layout->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, side);
  buttons->setObjectName(QStringLiteral("dividePhotosButtonBox"));
  // Not plain "Divide": that source string is already the Divide blend mode in
  // the QObject translation context.
  buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Divide Photos"));
  side_layout->addWidget(buttons);
  // Validating accept: in a save mode the folder must exist (or be creatable)
  // before the dialog closes; on failure it stays open for a correction.
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, folder_edit, save_selected] {
    if (save_selected()) {
      const QString folder = folder_edit->text().trimmed();
      if (folder.isEmpty() || !QDir().mkpath(folder)) {
        (void)show_warning_message(&dialog, QObject::tr("Divide Scanned Photos"),
                                   QObject::tr("The folder \"%1\" could not be created.").arg(folder),
                                   QMessageBox::Ok, QMessageBox::Ok,
                                   QStringLiteral("dividePhotosFolderCreateFailedMessageBox"));
        return;
      }
    }
    dialog.accept();
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  const auto current_mode = [&]() {
    if (perspective_check->isChecked()) {
      return PhotoExtractMode::Perspective;
    }
    return straighten_check->isChecked() ? PhotoExtractMode::Straighten : PhotoExtractMode::Cut;
  };

  const auto refresh_controls = [&] {
    count_label->setText(QObject::tr("Photos found: %1").arg(regions.size()));
    const bool folder_ok = !save_selected() || !folder_edit->text().trimmed().isEmpty();
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!regions.empty() && folder_ok);
    remove_button->setEnabled(preview->selected_region() >= 0);
  };
  for (auto* radio : {open_radio, folder_radio, both_radio}) {
    QObject::connect(radio, &QRadioButton::toggled, &dialog, [&](bool) {
      refresh_output_controls();
      refresh_controls();
    });
  }
  QObject::connect(folder_edit, &QLineEdit::textChanged, &dialog,
                   [&](const QString&) { refresh_controls(); });
  refresh_output_controls();

  const auto detect_now = [&] {
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const auto cursor_guard = qScopeGuard([] { QGuiApplication::restoreOverrideCursor(); });
    PhotoDetectOptions options;
    options.sensitivity = sensitivity_spin->value();
    options.source_ppi = source_ppi;
    auto detected = detect_photo_regions(*source, options).regions;
    // Regions the user added or edited survive; auto regions are replaced,
    // except where a fresh detection lands on a user-owned region.
    std::vector<PhotoRegion> merged;
    for (const auto& region : regions) {
      if (region.user_added) {
        merged.push_back(region);
      }
    }
    for (const auto& candidate : detected) {
      const bool overlaps_user =
          std::any_of(merged.begin(), merged.end(), [&](const PhotoRegion& kept) {
            return bounding_box_iou(kept.bounding_box, candidate.bounding_box) > kReplaceOverlapIou;
          });
      if (!overlaps_user) {
        merged.push_back(candidate);
      }
    }
    order_photo_regions_reading_order(merged);
    regions = std::move(merged);
    preview->regions_replaced();
    refresh_controls();
  };

  auto* detect_timer = new QTimer(&dialog);
  detect_timer->setSingleShot(true);
  detect_timer->setInterval(kDetectDebounceMs);
  QObject::connect(detect_timer, &QTimer::timeout, &dialog, [&] { detect_now(); });
  QObject::connect(sensitivity_spin, &QSpinBox::valueChanged, &dialog,
                   [detect_timer](int) { detect_timer->start(); });

  QObject::connect(straighten_check, &QCheckBox::toggled, &dialog,
                   [&](bool) { preview->set_mode(current_mode()); });
  // Fix Perspective implies Straighten: rectifying the quad always levels it.
  QObject::connect(perspective_check, &QCheckBox::toggled, &dialog, [&](bool checked) {
    if (checked) {
      straighten_check->setChecked(true);
    }
    straighten_check->setEnabled(!checked);
    preview->set_mode(current_mode());
  });
  straighten_check->setEnabled(!perspective_check->isChecked());

  QObject::connect(add_button, &QPushButton::clicked, &dialog, [&] { preview->add_centered_region(); });
  QObject::connect(remove_button, &QPushButton::clicked, &dialog,
                   [&] { preview->remove_selected_region(); });
  preview->set_regions_changed_callback([&] { refresh_controls(); });
  preview->set_mode(current_mode());

  append_themed_style(dialog, dialog_spinbox_button_style());

  detect_now();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  DividePhotosDialogResult result;
  result.regions = std::move(regions);
  order_photo_regions_reading_order(result.regions);
  result.settings.sensitivity = sensitivity_spin->value();
  result.settings.mode = current_mode();
  result.settings.up_direction =
      static_cast<PhotoUpDirection>(std::clamp(direction_group->checkedId(), 0, 3));
  // On wasm the save radios are hidden and force-unchecked above.
  result.settings.output = DividePhotosOutput::OpenDocuments;
  if (folder_radio->isChecked()) {
    result.settings.output = DividePhotosOutput::SaveToFolder;
  } else if (both_radio->isChecked()) {
    result.settings.output = DividePhotosOutput::SaveAndOpen;
  }
  result.settings.folder = folder_edit->text().trimmed();
  const QString prefix = sanitized_file_name(prefix_edit->text());
  result.settings.prefix = prefix.isEmpty() ? QStringLiteral("photo_") : prefix;
  QString format = format_combo->currentData().toString();
  result.settings.format = format.isEmpty() ? QStringLiteral("png") : std::move(format);
  result.settings.existing_files =
      existing_combo->currentData().toInt() == static_cast<int>(DividePhotosExistingFiles::Overwrite)
          ? DividePhotosExistingFiles::Overwrite
          : DividePhotosExistingFiles::AddNumbering;
  return result;
}

}  // namespace patchy::ui
