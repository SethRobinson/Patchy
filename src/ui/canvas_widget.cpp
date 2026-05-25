#include "ui/canvas_widget.hpp"

#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_tools.hpp"
#include "ui/image_document_io.hpp"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>
#include <QPolygonF>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace photoslop::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint8_t channel_from_color(QColor color, int channel) {
  switch (channel) {
    case 0:
      return static_cast<std::uint8_t>(color.red());
    case 1:
      return static_cast<std::uint8_t>(color.green());
    case 2:
      return static_cast<std::uint8_t>(color.blue());
    default:
      return static_cast<std::uint8_t>(color.alpha());
  }
}

QRect normalized_rect(QPoint a, QPoint b) {
  return QRect(a, b).normalized();
}

bool same_pixel(const std::uint8_t* pixel, const std::vector<std::uint8_t>& target, std::uint16_t channels) {
  for (std::uint16_t channel = 0; channel < channels; ++channel) {
    if (pixel[channel] != target[channel]) {
      return false;
    }
  }
  return true;
}

float brush_coverage(double distance_squared, int radius, int softness) {
  if (radius <= 0) {
    return distance_squared <= 0.0 ? 1.0F : 0.0F;
  }

  const auto radius_squared = static_cast<double>(radius) * static_cast<double>(radius);
  if (distance_squared > radius_squared) {
    return 0.0F;
  }

  softness = std::clamp(softness, 0, 100);
  if (softness <= 0) {
    return 1.0F;
  }

  const auto edge_width = std::max(1.0, static_cast<double>(radius) * static_cast<double>(softness) / 100.0);
  const auto inner_radius = std::max(0.0, static_cast<double>(radius) - edge_width);
  const auto distance = std::sqrt(distance_squared);
  if (distance <= inner_radius) {
    return 1.0F;
  }
  return static_cast<float>(std::clamp(1.0 - ((distance - inner_radius) / edge_width), 0.0, 1.0));
}

void compose_layer_pixel(const Layer& layer, std::int32_t x, std::int32_t y, std::array<float, 3>& out,
                         float& out_alpha) {
  if (!layer.visible() || layer.opacity() <= 0.0F) {
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      compose_layer_pixel(child, x, y, out, out_alpha);
    }
    return;
  }

  if (layer.kind() != LayerKind::Pixel) {
    return;
  }

  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }

  const auto bounds = layer.bounds();
  const auto local_x = x - bounds.x;
  const auto local_y = y - bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
    return;
  }

  const auto* src = pixels.pixel(local_x, local_y);
  const auto source_alpha = pixels.format().channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
  const auto alpha = source_alpha * layer_mask_alpha_at(layer, x, y) * layer.opacity();
  if (alpha <= 0.0F) {
    return;
  }

  const auto next_alpha = alpha + out_alpha * (1.0F - alpha);
  const std::array<std::uint8_t, 3> src_rgb = {src[0], src[1], src[2]};
  const std::array<std::uint8_t, 3> dst_rgb = {clamp_byte(out[0]), clamp_byte(out[1]), clamp_byte(out[2])};
  const auto blended = blend_rgb(src_rgb, dst_rgb, layer.blend_mode());
  for (int channel = 0; channel < 3; ++channel) {
    out[channel] = next_alpha > 0.0F
                       ? (static_cast<float>(blended[static_cast<std::size_t>(channel)]) * alpha +
                          out[channel] * out_alpha * (1.0F - alpha)) /
                             next_alpha
                       : 0.0F;
  }
  out_alpha = next_alpha;
}

Layer* topmost_pixel_layer_at_recursive(std::vector<Layer>& layers, QPoint document_point, bool require_visible_pixel) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      if (auto* found = topmost_pixel_layer_at_recursive(layer.children(), document_point, require_visible_pixel);
          found != nullptr) {
        return found;
      }
      continue;
    }
    if (layer.kind() != LayerKind::Pixel) {
      continue;
    }
    const auto& pixels = layer.pixels();
    if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
      continue;
    }
    const auto bounds = layer.bounds();
    if (!bounds.contains(document_point.x(), document_point.y())) {
      continue;
    }
    const auto local_x = document_point.x() - bounds.x;
    const auto local_y = document_point.y() - bounds.y;
    if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
      continue;
    }
    if (require_visible_pixel) {
      const auto source_alpha = pixels.format().channels >= 4 ? pixels.pixel(local_x, local_y)[3] : 255;
      const auto mask_alpha = static_cast<int>(std::round(layer_mask_alpha_at(layer, document_point.x(), document_point.y()) *
                                                          255.0F));
      if (std::min(static_cast<int>(source_alpha), mask_alpha) < 8) {
        continue;
      }
    }
    return &layer;
  }
  return nullptr;
}

Layer* topmost_text_layer_at_recursive(std::vector<Layer>& layers, QPoint document_point) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto& layer = *it;
    if (!layer.visible()) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      if (auto* found = topmost_text_layer_at_recursive(layer.children(), document_point); found != nullptr) {
        return found;
      }
      continue;
    }
    if (layer.metadata().contains("photoslop.text") && layer.bounds().contains(document_point.x(), document_point.y())) {
      return &layer;
    }
  }
  return nullptr;
}

EditColor edit_color(QColor color) {
  return EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(std::max(1, color.alpha()))};
}

Rect to_core_rect(QRect rect) {
  rect = rect.normalized();
  return Rect{rect.x(), rect.y(), rect.width(), rect.height()};
}

QRect to_qrect(Rect rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

QPoint clamped_document_point(const Document& document, QPoint point) {
  return QPoint(std::clamp(point.x(), 0, std::max(0, document.width() - 1)),
                std::clamp(point.y(), 0, std::max(0, document.height() - 1)));
}

QRegion expanded_region(const QRegion& region, int pixels, QRect bounds) {
  if (region.isEmpty() || pixels <= 0) {
    return region.intersected(bounds);
  }

  pixels = std::clamp(pixels, 0, 250);
  QRegion expanded;
  for (int dy = -pixels; dy <= pixels; ++dy) {
    for (int dx = -pixels; dx <= pixels; ++dx) {
      expanded = expanded.united(region.translated(dx, dy));
    }
  }
  return expanded.intersected(bounds);
}

QRegion region_from_mask(const std::vector<std::uint8_t>& selected, int width, int height,
                         int min_x, int min_y, int max_x, int max_y) {
  if (selected.empty() || width <= 0 || height <= 0 || min_x > max_x || min_y > max_y) {
    return {};
  }

  min_x = std::clamp(min_x, 0, width - 1);
  max_x = std::clamp(max_x, 0, width - 1);
  min_y = std::clamp(min_y, 0, height - 1);
  max_y = std::clamp(max_y, 0, height - 1);

  std::vector<QRect> runs;
  runs.reserve(static_cast<std::size_t>(std::max(1, max_y - min_y + 1)));
  for (int y = min_y; y <= max_y; ++y) {
    int run_start = -1;
    for (int x = min_x; x <= max_x + 1; ++x) {
      const bool is_selected =
          x <= max_x && selected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)] != 0U;
      if (is_selected && run_start < 0) {
        run_start = x;
      } else if (!is_selected && run_start >= 0) {
        runs.push_back(QRect(run_start, y, x - run_start, 1));
        run_start = -1;
      }
    }
  }

  QRegion region;
  if (!runs.empty()) {
    region.setRects(runs.data(), static_cast<int>(runs.size()));
  }
  return region;
}

QImage layer_source_image(const Layer& layer) {
  const auto& pixels = layer.pixels();
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return image;
  }

  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixelColor(x, y, QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255));
    }
  }
  return image;
}

std::optional<QRect> opaque_pixel_local_rect(const Layer& layer) {
  const auto& pixels = layer.pixels();
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return std::nullopt;
  }
  if (pixels.format().channels < 4) {
    return QRect(0, 0, pixels.width(), pixels.height());
  }

  int min_x = pixels.width();
  int min_y = pixels.height();
  int max_x = -1;
  int max_y = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] == 0) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }

  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return QRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

PixelBuffer pixels_from_image_rgba(const QImage& image) {
  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  PixelBuffer pixels(converted.width(), converted.height(), PixelFormat::rgba8());
  for (int y = 0; y < converted.height(); ++y) {
    for (int x = 0; x < converted.width(); ++x) {
      const auto color = converted.pixelColor(x, y);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      px[3] = static_cast<std::uint8_t>(color.alpha());
    }
  }
  return pixels;
}

int layer_style_padding(const Layer& layer) {
  int padding = 0;
  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      padding = std::max(padding, layer_style_padding(child));
    }
    return padding;
  }

  const auto& style = layer.layer_style();
  if (!style.effects_visible || style.empty()) {
    return 0;
  }

  constexpr double kRadiansPerDegree = kPi / 180.0;
  for (const auto& shadow : style.drop_shadows) {
    if (!shadow.enabled || shadow.opacity <= 0.0F) {
      continue;
    }
    const auto radians = (180.0 - static_cast<double>(shadow.angle_degrees)) * kRadiansPerDegree;
    const auto offset_x = static_cast<int>(std::lround(std::cos(radians) * shadow.distance));
    const auto offset_y = static_cast<int>(std::lround(std::sin(radians) * shadow.distance));
    const auto blur_radius = std::max(0, static_cast<int>(std::lround(shadow.size * 0.5F)));
    const auto spread_radius = std::max(0, static_cast<int>(std::lround(shadow.size * clamp_unit(shadow.spread / 100.0F))));
    padding = std::max(padding, std::abs(offset_x) + std::abs(offset_y) + blur_radius * 3 + spread_radius + 2);
  }
  for (const auto& glow : style.outer_glows) {
    if (!glow.enabled || glow.opacity <= 0.0F || glow.size <= 0.0F) {
      continue;
    }
    const auto blur_radius = std::max(0, static_cast<int>(std::lround(glow.size * 0.5F)));
    padding = std::max(padding, blur_radius * 3 + 2);
  }
  for (const auto& stroke : style.strokes) {
    if (stroke.enabled && stroke.opacity > 0.0F && stroke.size > 0.0F) {
      padding = std::max(padding, std::max(1, static_cast<int>(std::ceil(stroke.size))) + 1);
    }
  }
  return padding;
}

int document_style_padding(const Document& document) {
  int padding = 0;
  for (const auto& layer : document.layers()) {
    padding = std::max(padding, layer_style_padding(layer));
  }
  return padding;
}

Rect expand_rect(Rect rect, int amount) {
  if (rect.empty() || amount <= 0) {
    return rect;
  }
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

Rect layer_render_bounds_for_bounds(const Layer& layer, Rect bounds) {
  return expand_rect(bounds, layer_style_padding(layer));
}

bool layer_locks_transparent_pixels(const Layer& layer) {
  const auto found = layer.metadata().find("photoslop.lock_transparent_pixels");
  return found != layer.metadata().end() && found->second == "true";
}

bool layer_mask_linked(const Layer& layer) {
  const auto found = layer.metadata().find("photoslop.mask_linked");
  return found == layer.metadata().end() || found->second != "false";
}

void translate_layer_mask(Layer& layer, QPoint delta) {
  if (delta.isNull()) {
    return;
  }
  auto& mask = layer.mask();
  if (!mask.has_value() || !layer_mask_linked(layer)) {
    return;
  }
  mask->bounds.x += delta.x();
  mask->bounds.y += delta.y();
}

EditOptions edit_options(QColor primary, QColor secondary, int brush_size, int brush_opacity, int brush_softness,
                         bool fill_shapes, bool lock_transparent_pixels, QRegion selection) {
  EditOptions options;
  primary.setAlpha(std::clamp(static_cast<int>(std::round(255.0 * static_cast<double>(brush_opacity) / 100.0)), 1, 255));
  options.primary = edit_color(primary);
  options.secondary = edit_color(secondary);
  options.brush_size = brush_size;
  options.brush_softness = brush_softness;
  options.fill_shapes = fill_shapes;
  options.lock_transparent_pixels = lock_transparent_pixels;
  if (!selection.isEmpty()) {
    options.selection = to_core_rect(selection.boundingRect());
    options.selection_mask = [selection = std::move(selection)](std::int32_t x, std::int32_t y) {
      return selection.contains(QPoint(x, y));
    };
  }
  return options;
}

std::uint64_t stroke_pixel_key(std::int32_t x, std::int32_t y) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32U) |
         static_cast<std::uint32_t>(x);
}

bool tool_uses_alt_left_for_color_pick(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::Smudge:
    case CanvasTool::Eraser:
    case CanvasTool::Gradient:
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
    case CanvasTool::Ellipse:
    case CanvasTool::Fill:
      return true;
    default:
      return false;
  }
}

}  // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  selection_timer_.start(120, this);
}

void CanvasWidget::set_document(Document* document) {
  cancel_free_transform();
  document_ = document;
  render_cache_ = QImage();
  render_cache_dirty_ = true;
  smudge_state_ = {};
  update();
}

double CanvasWidget::zoom() const noexcept {
  return zoom_;
}

void CanvasWidget::set_zoom(double zoom) {
  zoom_ = std::clamp(zoom, 0.05, 32.0);
  update_tool_cursor();
  update();
}

void CanvasWidget::fit_to_view() {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  const auto available_width = std::max(1.0, static_cast<double>(width() - 80));
  const auto available_height = std::max(1.0, static_cast<double>(height() - 80));
  zoom_ = std::clamp(std::min(available_width / static_cast<double>(document_->width()),
                              available_height / static_cast<double>(document_->height())),
                     0.05, 32.0);
  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_->width()) * zoom_) / 2.0,
                 (static_cast<double>(height()) - static_cast<double>(document_->height()) * zoom_) / 2.0);
  update();
}

void CanvasWidget::zoom_to_document_rect(QRect document_rect) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0 || width() <= 0 || height() <= 0) {
    return;
  }

  document_rect = document_rect.normalized().intersected(QRect(0, 0, document_->width(), document_->height()));
  if (document_rect.width() <= 1 || document_rect.height() <= 1) {
    return;
  }

  const auto available_width = std::max(1.0, static_cast<double>(width() - 80));
  const auto available_height = std::max(1.0, static_cast<double>(height() - 80));
  zoom_ = std::clamp(std::min(available_width / static_cast<double>(document_rect.width()),
                              available_height / static_cast<double>(document_rect.height())),
                     0.05, 32.0);
  pan_ = QPointF((static_cast<double>(width()) - static_cast<double>(document_rect.width()) * zoom_) / 2.0 -
                     static_cast<double>(document_rect.x()) * zoom_,
                 (static_cast<double>(height()) - static_cast<double>(document_rect.height()) * zoom_) / 2.0 -
                     static_cast<double>(document_rect.y()) * zoom_);
  update_tool_cursor();
  update();
}

void CanvasWidget::set_tool(CanvasTool tool) noexcept {
  tool_ = tool;
  update_tool_cursor();
}

CanvasTool CanvasWidget::tool() const noexcept {
  return tool_;
}

void CanvasWidget::set_auto_select_layer(bool enabled) noexcept {
  auto_select_layer_ = enabled;
}

bool CanvasWidget::auto_select_layer() const noexcept {
  return auto_select_layer_;
}

void CanvasWidget::set_primary_color(QColor color) {
  if (color.alpha() == 0) {
    color.setAlpha(255);
  }
  primary_color_ = color;
}

QColor CanvasWidget::primary_color() const noexcept {
  return primary_color_;
}

void CanvasWidget::set_secondary_color(QColor color) {
  if (color.alpha() == 0) {
    color.setAlpha(255);
  }
  secondary_color_ = color;
}

QColor CanvasWidget::secondary_color() const noexcept {
  return secondary_color_;
}

void CanvasWidget::set_brush_size(int size) {
  brush_size_ = std::clamp(size, 1, 256);
  update_tool_cursor();
}

int CanvasWidget::brush_size() const noexcept {
  return brush_size_;
}

void CanvasWidget::set_brush_opacity(int opacity) {
  brush_opacity_ = std::clamp(opacity, 1, 100);
}

int CanvasWidget::brush_opacity() const noexcept {
  return brush_opacity_;
}

void CanvasWidget::set_brush_softness(int softness) {
  brush_softness_ = std::clamp(softness, 0, 100);
}

int CanvasWidget::brush_softness() const noexcept {
  return brush_softness_;
}

void CanvasWidget::set_clone_aligned(bool aligned) noexcept {
  if (clone_aligned_ == aligned) {
    return;
  }
  clone_aligned_ = aligned;
  clone_aligned_offset_set_ = false;
}

bool CanvasWidget::clone_aligned() const noexcept {
  return clone_aligned_;
}

void CanvasWidget::set_wand_tolerance(int tolerance) {
  wand_tolerance_ = std::clamp(tolerance, 0, 255);
}

int CanvasWidget::wand_tolerance() const noexcept {
  return wand_tolerance_;
}

void CanvasWidget::set_fill_shapes(bool fill_shapes) noexcept {
  fill_shapes_ = fill_shapes;
}

void CanvasWidget::set_selection_mode(SelectionMode mode) noexcept {
  selection_mode_ = mode;
}

CanvasWidget::SelectionMode CanvasWidget::selection_mode() const noexcept {
  return selection_mode_;
}

void CanvasWidget::set_marquee_style(MarqueeStyle style) noexcept {
  marquee_style_ = style;
}

CanvasWidget::MarqueeStyle CanvasWidget::marquee_style() const noexcept {
  return marquee_style_;
}

void CanvasWidget::set_marquee_fixed_size(int width, int height) noexcept {
  marquee_fixed_size_ = QSize(std::clamp(width, 1, 30000), std::clamp(height, 1, 30000));
}

QSize CanvasWidget::marquee_fixed_size() const noexcept {
  return marquee_fixed_size_;
}

bool CanvasWidget::begin_free_transform() {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr || layer->pixels().format().bit_depth != BitDepth::UInt8 ||
      layer->pixels().empty()) {
    if (status_callback_) {
      status_callback_(tr("Select an editable pixel layer to transform"));
    }
    return false;
  }
  const auto local_opaque_rect = opaque_pixel_local_rect(*layer);
  if (!local_opaque_rect.has_value()) {
    if (status_callback_) {
      status_callback_(tr("Layer has no opaque pixels to transform"));
    }
    return false;
  }

  transforming_layer_ = true;
  dragging_transform_ = false;
  transform_layer_id_ = layer->id();
  const auto bounds = layer->bounds();
  transform_original_rect_ =
      QRectF(bounds.x + local_opaque_rect->x(), bounds.y + local_opaque_rect->y(), local_opaque_rect->width(),
             local_opaque_rect->height());
  transform_current_rect_ = transform_original_rect_;
  transform_drag_start_rect_ = transform_current_rect_;
  transform_drag_start_point_ = {};
  transform_drag_handle_ = TransformHandle::None;
  transform_angle_ = 0.0;
  transform_start_angle_ = 0.0;
  transform_source_image_ = layer_source_image(*layer).copy(*local_opaque_rect);

  const auto was_visible = layer->visible();
  layer->set_visible(false);
  transform_base_cache_ = render_document_image();
  layer->set_visible(was_visible);
  setCursor(Qt::ArrowCursor);
  update();
  if (status_callback_) {
    status_callback_(tr("Drag handles to transform. Shift keeps aspect ratio."));
  }
  return true;
}

void CanvasWidget::cancel_free_transform() {
  if (!transforming_layer_) {
    return;
  }
  transforming_layer_ = false;
  dragging_transform_ = false;
  transform_layer_id_.reset();
  transform_drag_handle_ = TransformHandle::None;
  transform_base_cache_ = QImage();
  transform_source_image_ = QImage();
  update_tool_cursor();
  update();
}

bool CanvasWidget::free_transform_active() const noexcept {
  return transforming_layer_;
}

std::optional<QRect> CanvasWidget::active_layer_document_rect() const noexcept {
  const auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  return to_qrect(layer->bounds());
}

void CanvasWidget::document_changed() {
  render_cache_dirty_ = true;
  if (document_changed_callback_) {
    document_changed_callback_();
  }
  if (isVisible()) {
    update();
  }
}

void CanvasWidget::document_changed(QRect document_rect) {
  if (!isVisible()) {
    render_cache_dirty_ = true;
    if (document_changed_callback_) {
      document_changed_callback_();
    }
    return;
  }
  if (!document_rect.isValid() || document_rect.isEmpty()) {
    document_changed();
    return;
  }

  if (render_cache_dirty_ || render_cache_.isNull()) {
    document_changed();
    return;
  }

  if (document_ != nullptr) {
    const auto style_padding = document_style_padding(*document_);
    if (style_padding > 0) {
      document_rect = document_rect.adjusted(-style_padding, -style_padding, style_padding, style_padding);
    }
    document_rect = document_rect.intersected(QRect(0, 0, document_->width(), document_->height()));
    if (document_rect.isEmpty()) {
      if (document_changed_callback_) {
        document_changed_callback_();
      }
      return;
    }
    const auto area = static_cast<std::int64_t>(document_rect.width()) * static_cast<std::int64_t>(document_rect.height());
    const auto canvas_area = static_cast<std::int64_t>(document_->width()) * static_cast<std::int64_t>(document_->height());
    if (canvas_area > 0 && area > canvas_area / 6) {
      document_changed();
      return;
    }
  }

  refresh_render_cache_rect(document_rect);
  if (document_changed_callback_) {
    document_changed_callback_();
  }
  update(widget_rect_for_document_rect(document_rect));
}

void CanvasWidget::select_all() {
  if (document_ == nullptr) {
    return;
  }
  selection_ = QRegion(QRect(0, 0, document_->width(), document_->height()));
  selection_edges_visible_ = true;
  update();
}

void CanvasWidget::invert_selection() {
  if (document_ == nullptr) {
    return;
  }
  const QRegion canvas_region(QRect(0, 0, document_->width(), document_->height()));
  selection_ = canvas_region.subtracted(selection_);
  if (status_callback_) {
    status_callback_(tr("Inverted selection"));
  }
  update();
}

void CanvasWidget::clear_selection() {
  if (!selection_.isEmpty()) {
    last_cleared_selection_ = selection_;
  }
  selection_ = QRegion();
  selection_edges_visible_ = true;
  update();
}

void CanvasWidget::reselect() {
  if (document_ == nullptr || last_cleared_selection_.isEmpty()) {
    return;
  }
  selection_ = last_cleared_selection_.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (status_callback_) {
    status_callback_(tr("Reselected previous selection"));
  }
  update();
}

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

void CanvasWidget::expand_selection(int pixels) {
  if (document_ == nullptr || selection_.isEmpty() || pixels <= 0) {
    return;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  selection_ = expanded_region(selection_, pixels, canvas_rect);
  if (status_callback_) {
    status_callback_(tr("Expanded selection by %1 px").arg(pixels));
  }
  update();
}

void CanvasWidget::contract_selection(int pixels) {
  if (document_ == nullptr || selection_.isEmpty() || pixels <= 0) {
    return;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const QRegion canvas_region(canvas_rect);
  const auto outside = canvas_region.subtracted(selection_);
  selection_ = canvas_region.subtracted(expanded_region(outside, pixels, canvas_rect));
  if (status_callback_) {
    status_callback_(tr("Contracted selection by %1 px").arg(pixels));
  }
  update();
}

void CanvasWidget::border_selection(int pixels) {
  if (document_ == nullptr || selection_.isEmpty() || pixels <= 0) {
    return;
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  const QRegion canvas_region(canvas_rect);
  const auto outside = expanded_region(selection_, pixels, canvas_rect);
  const auto outside_of_selection = canvas_region.subtracted(selection_);
  const auto inside = canvas_region.subtracted(expanded_region(outside_of_selection, pixels, canvas_rect));
  selection_ = outside.subtracted(inside);
  if (status_callback_) {
    status_callback_(tr("Selected %1 px border").arg(pixels));
  }
  update();
}

void CanvasWidget::select_layer_opaque_pixels(LayerId layer_id) {
  if (document_ == nullptr) {
    return;
  }
  const auto* layer = document_->find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    if (status_callback_) {
      status_callback_(tr("Select a pixel layer first"));
    }
    return;
  }

  const auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  std::vector<QRect> runs;
  runs.reserve(static_cast<std::size_t>(std::max(1, pixels.height())));
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    int run_start = -1;
    for (std::int32_t x = 0; x <= pixels.width(); ++x) {
      bool selected = false;
      if (x < pixels.width()) {
        const auto* px = pixels.pixel(x, y);
        selected = pixels.format().channels >= 4 ? px[3] != 0 : true;
      }
      if (selected && run_start < 0) {
        run_start = x;
      } else if (!selected && run_start >= 0) {
        runs.emplace_back(bounds.x + run_start, bounds.y + y, x - run_start, 1);
        run_start = -1;
      }
    }
  }

  QRegion layer_region;
  if (!runs.empty()) {
    layer_region.setRects(runs.data(), static_cast<int>(runs.size()));
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  selection_ = layer_region.intersected(canvas_rect);
  selection_edges_visible_ = true;
  if (status_callback_) {
    status_callback_(selection_.isEmpty() ? tr("Layer has no opaque pixels") : tr("Selected layer opacity"));
  }
  update();
}

void CanvasWidget::select_active_layer_opaque_pixels() {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return;
  }
  select_layer_opaque_pixels(*document_->active_layer_id());
}

void CanvasWidget::grow_selection() {
  if (document_ == nullptr || selection_.isEmpty()) {
    if (status_callback_) {
      status_callback_(tr("Make a selection before growing"));
    }
    return;
  }

  ensure_render_cache();
  if (render_cache_.isNull()) {
    return;
  }

  const auto& flattened = render_cache_;
  const auto width = flattened.width();
  const auto height = flattened.height();
  const auto total_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto pixel = [&flattened](int x, int y) {
    return flattened.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  };

  std::array<std::int64_t, 4> sum = {0, 0, 0, 0};
  std::int64_t sample_count = 0;
  for (const auto& rect : selection_) {
    const auto clipped = rect.intersected(QRect(0, 0, width, height));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
      for (int x = clipped.left(); x <= clipped.right(); ++x) {
        const auto* color = pixel(x, y);
        sum[0] += color[0];
        sum[1] += color[1];
        sum[2] += color[2];
        sum[3] += color[3];
        ++sample_count;
      }
    }
  }
  if (sample_count <= 0) {
    return;
  }

  const std::array<int, 4> target = {static_cast<int>(sum[0] / sample_count),
                                     static_cast<int>(sum[1] / sample_count),
                                     static_cast<int>(sum[2] / sample_count),
                                     static_cast<int>(sum[3] / sample_count)};
  const auto tolerance_squared = wand_tolerance_ * wand_tolerance_ * 4;
  const auto matches = [&](int x, int y) {
    const auto* color = pixel(x, y);
    const auto dr = static_cast<int>(color[0]) - target[0];
    const auto dg = static_cast<int>(color[1]) - target[1];
    const auto db = static_cast<int>(color[2]) - target[2];
    const auto da = static_cast<int>(color[3]) - target[3];
    return dr * dr + dg * dg + db * db + da * da <= tolerance_squared;
  };

  std::vector<std::uint8_t> selected(total_pixels);
  std::vector<int> queue;
  queue.reserve(std::min<std::size_t>(total_pixels, 1'000'000U));
  auto min_x = std::numeric_limits<int>::max();
  auto min_y = std::numeric_limits<int>::max();
  auto max_x = std::numeric_limits<int>::min();
  auto max_y = std::numeric_limits<int>::min();
  int count = 0;
  const auto mark = [&](int x, int y) {
    const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    if (selected[index] != 0U) {
      return false;
    }
    selected[index] = 1U;
    queue.push_back(y * width + x);
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    ++count;
    return true;
  };

  for (const auto& rect : selection_) {
    const auto clipped = rect.intersected(QRect(0, 0, width, height));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
      for (int x = clipped.left(); x <= clipped.right(); ++x) {
        mark(x, y);
      }
    }
  }

  for (std::size_t offset = 0; offset < queue.size(); ++offset) {
    const auto index_value = queue[offset];
    const auto x = index_value % width;
    const auto y = index_value / width;
    const auto maybe_mark = [&](int nx, int ny) {
      const auto index = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
      if (selected[index] == 0U && matches(nx, ny)) {
        mark(nx, ny);
      }
    };
    if (x + 1 < width) {
      maybe_mark(x + 1, y);
    }
    if (x > 0) {
      maybe_mark(x - 1, y);
    }
    if (y + 1 < height) {
      maybe_mark(x, y + 1);
    }
    if (y > 0) {
      maybe_mark(x, y - 1);
    }
  }

  selection_ = region_from_mask(selected, width, height, min_x, min_y, max_x, max_y);
  if (status_callback_) {
    status_callback_(tr("Grew selection to %1 px").arg(count));
  }
  update();
}

void CanvasWidget::select_similar_to_selection() {
  if (document_ == nullptr || selection_.isEmpty()) {
    if (status_callback_) {
      status_callback_(tr("Make a selection before selecting similar pixels"));
    }
    return;
  }

  ensure_render_cache();
  if (render_cache_.isNull()) {
    return;
  }

  const auto& flattened = render_cache_;
  const auto width = flattened.width();
  const auto height = flattened.height();
  const auto total_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto pixel = [&flattened](int x, int y) {
    return flattened.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  };

  std::array<std::int64_t, 4> sum = {0, 0, 0, 0};
  std::int64_t sample_count = 0;
  for (const auto& rect : selection_) {
    const auto clipped = rect.intersected(QRect(0, 0, width, height));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
      for (int x = clipped.left(); x <= clipped.right(); ++x) {
        const auto* color = pixel(x, y);
        sum[0] += color[0];
        sum[1] += color[1];
        sum[2] += color[2];
        sum[3] += color[3];
        ++sample_count;
      }
    }
  }
  if (sample_count <= 0) {
    return;
  }

  const std::array<int, 4> target = {static_cast<int>(sum[0] / sample_count),
                                     static_cast<int>(sum[1] / sample_count),
                                     static_cast<int>(sum[2] / sample_count),
                                     static_cast<int>(sum[3] / sample_count)};
  const auto tolerance_squared = wand_tolerance_ * wand_tolerance_ * 4;
  std::vector<std::uint8_t> selected(total_pixels);
  auto min_x = std::numeric_limits<int>::max();
  auto min_y = std::numeric_limits<int>::max();
  auto max_x = std::numeric_limits<int>::min();
  auto max_y = std::numeric_limits<int>::min();
  int count = 0;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto* color = pixel(x, y);
      const auto dr = static_cast<int>(color[0]) - target[0];
      const auto dg = static_cast<int>(color[1]) - target[1];
      const auto db = static_cast<int>(color[2]) - target[2];
      const auto da = static_cast<int>(color[3]) - target[3];
      if (dr * dr + dg * dg + db * db + da * da > tolerance_squared) {
        continue;
      }
      selected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 1U;
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
      ++count;
    }
  }

  selection_ = region_from_mask(selected, width, height, min_x, min_y, max_x, max_y);
  if (status_callback_) {
    status_callback_(tr("Selected %1 similar px").arg(count));
  }
  update();
}

std::optional<QRect> CanvasWidget::selected_document_rect() const noexcept {
  if (selection_.isEmpty()) {
    return std::nullopt;
  }
  return selection_.boundingRect();
}

const QRegion& CanvasWidget::selected_document_region() const noexcept {
  return selection_;
}

bool CanvasWidget::has_selection() const noexcept {
  return !selection_.isEmpty();
}

bool CanvasWidget::selection_contains(QPoint point) const noexcept {
  return selection_.isEmpty() || selection_.contains(point);
}

QPoint CanvasWidget::widget_position_for_document_point(QPoint document_position) const {
  return widget_position(document_position);
}

void CanvasWidget::set_before_edit_callback(std::function<void(QString)> callback) {
  before_edit_callback_ = std::move(callback);
}

void CanvasWidget::set_color_picked_callback(std::function<void(QColor)> callback) {
  color_picked_callback_ = std::move(callback);
}

void CanvasWidget::set_text_requested_callback(std::function<void(QPoint)> callback) {
  text_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_active_layer_changed_callback(std::function<void(LayerId)> callback) {
  active_layer_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_status_callback(std::function<void(QString)> callback) {
  status_callback_ = std::move(callback);
}

void CanvasWidget::set_info_callback(std::function<void(CanvasInfoState)> callback) {
  info_callback_ = std::move(callback);
}

void CanvasWidget::set_document_changed_callback(std::function<void()> callback) {
  document_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_selected_layer_ids(std::vector<LayerId> layer_ids) {
  selected_layer_ids_ = std::move(layer_ids);
}

void CanvasWidget::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(36, 38, 41));

  if (document_ == nullptr || document_->width() == 0 || document_->height() == 0) {
    painter.setPen(QColor(170, 176, 184));
    painter.drawText(rect(), Qt::AlignCenter, tr("No document"));
    return;
  }

  const QRectF target_rect(widget_position_f(QPointF(0.0, 0.0)),
                           widget_position_f(QPointF(document_->width(), document_->height())));
  draw_checkerboard(painter, target_rect);

  ensure_render_cache();

  const auto draw_scaled_image = [&painter, &target_rect](const QImage& image) {
    if (!image.isNull()) {
      painter.drawImage(target_rect, image, QRectF(image.rect()));
    }
  };
  const bool draw_transform_overlay =
      transforming_layer_ && !transform_base_cache_.isNull() && !transform_source_image_.isNull();

  painter.save();
  painter.setClipRect(target_rect);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);
  if (draw_transform_overlay) {
    draw_scaled_image(transform_base_cache_);
  } else if (moving_layer_ && !moving_layers_.empty()) {
    if (!move_preview_cache_.isNull()) {
      draw_scaled_image(move_preview_cache_);
    } else {
      draw_scaled_image(render_cache_);
    }
  } else {
    draw_scaled_image(render_cache_);
  }
  painter.restore();

  if (moving_layer_ && !moving_layers_.empty() && !draw_transform_overlay) {
    painter.setPen(QPen(QColor(95, 170, 255), 1, Qt::DashLine));
    for (const auto& moving_layer : moving_layers_) {
      auto moved_bounds = moving_layer.original_bounds;
      moved_bounds.x += move_preview_delta_.x();
      moved_bounds.y += move_preview_delta_.y();
      const QRect layer_target(widget_position(QPoint(moved_bounds.x, moved_bounds.y)),
                               QSize(std::max(1, static_cast<int>(std::round(moved_bounds.width * zoom_))),
                                     std::max(1, static_cast<int>(std::round(moved_bounds.height * zoom_)))));
      painter.drawRect(layer_target.adjusted(0, 0, -1, -1));
    }
  }
  if (draw_transform_overlay) {
    draw_free_transform(painter);
  }
  painter.setPen(QColor(95, 101, 110));
  const auto border_rect = target_rect.adjusted(0.5, 0.5, -0.5, -0.5);
  if (!border_rect.isEmpty()) {
    painter.drawRect(border_rect);
  }
  draw_selection_overlay(painter);
  draw_shape_preview(painter);
  draw_zoom_preview(painter);
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
  const auto wheel_delta = !event->pixelDelta().isNull() ? event->pixelDelta() : event->angleDelta();
  const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
  if (primary_delta == 0) {
    event->accept();
    return;
  }

  if ((event->modifiers() & Qt::AltModifier) != 0) {
    const auto cursor = event->position();
    const QPointF document_point((cursor.x() - pan_.x()) / zoom_, (cursor.y() - pan_.y()) / zoom_);
    zoom_ = std::clamp(zoom_ * (primary_delta > 0 ? 1.1 : 0.9), 0.05, 32.0);
    pan_ = QPointF(cursor.x() - document_point.x() * zoom_, cursor.y() - document_point.y() * zoom_);
    update_tool_cursor();
    update();
    event->accept();
    return;
  }

  constexpr double kWheelPanScale = 0.5;
  if ((event->modifiers() & Qt::ControlModifier) != 0) {
    pan_.ry() += static_cast<double>(primary_delta) * kWheelPanScale;
  } else {
    pan_.rx() += static_cast<double>(primary_delta) * kWheelPanScale;
  }
  event->accept();
  update();
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
  setFocus(Qt::MouseFocusReason);
  last_mouse_position_ = event->pos();
  emit_info_for_widget_position(event->pos());

  if (spacebar_panning_ || tool_ == CanvasTool::Pan || (event->buttons() & Qt::MiddleButton) != 0 ||
      (event->buttons() & Qt::RightButton) != 0) {
    panning_ = true;
    setCursor(Qt::ClosedHandCursor);
    return;
  }

  if (transforming_layer_ && event->button() == Qt::LeftButton) {
    const auto handle = transform_handle_at(event->pos());
    if (handle != TransformHandle::None) {
      dragging_transform_ = true;
      transform_drag_handle_ = handle;
      transform_drag_start_point_ = document_position_f(event->position());
      transform_drag_start_rect_ = transform_current_rect_;
      transform_start_angle_ = transform_angle_;
      event->accept();
      return;
    }
  }

  const auto document_point = document_position(event->pos());
  if (!document_contains(document_point)) {
    return;
  }

  if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier) != 0 &&
      tool_uses_alt_left_for_color_pick(tool_)) {
    pick_color(document_point);
    return;
  }

  if (tool_ == CanvasTool::Clone) {
    if ((event->modifiers() & Qt::AltModifier) != 0) {
      set_clone_source(document_point);
      return;
    }
    if (!clone_source_set_) {
      if (status_callback_) {
        status_callback_(tr("Alt-click to set a clone source"));
      }
      return;
    }
    if (begin_edit(tr("Clone stamp"))) {
      clone_source_cache_ = render_document_image();
      if (!clone_aligned_ || !clone_aligned_offset_set_) {
        clone_source_offset_ = clone_source_point_ - document_point;
        clone_aligned_offset_set_ = clone_aligned_;
      }
      brush_stroke_pixels_.clear();
      painting_ = true;
      last_document_position_ = document_point;
      const auto dirty = clone_brush_at(document_point);
      document_changed(dirty);
    }
    return;
  }

  if (tool_ == CanvasTool::Eyedropper) {
    pick_color(document_point);
    return;
  }

  if (tool_ == CanvasTool::Text) {
    if (auto* layer = topmost_text_layer_at(document_point); layer != nullptr) {
      activate_layer(*layer);
    }
    if (text_requested_callback_) {
      text_requested_callback_(document_point);
    }
    return;
  }

  if (tool_ == CanvasTool::Move) {
    if (auto_select_layer_ && selected_layer_ids_.size() < 2U) {
      if (auto* hit_layer = topmost_pixel_layer_at(document_point, true); hit_layer != nullptr) {
        activate_layer(*hit_layer);
      }
    }
    const auto layer_ids = movable_layer_ids();
    if (layer_ids.empty()) {
      if (status_callback_) {
        status_callback_(tr("Click an editable pixel layer to move"));
      }
      return;
    }
    moving_layer_ = true;
    move_start_ = document_point;
    move_preview_delta_ = QPoint();
    moving_layers_.clear();
    moving_layers_.reserve(layer_ids.size());
    for (const auto id : layer_ids) {
      auto* layer = document_->find_layer(id);
      if (layer != nullptr) {
        moving_layers_.push_back(MovingLayer{id, layer->bounds()});
      }
    }
    move_preview_cache_ = QImage();
    return;
  }

  if (tool_ == CanvasTool::Marquee || tool_ == CanvasTool::EllipticalMarquee) {
    selecting_ = true;
    spacebar_repositioning_drag_rect_ = false;
    selection_edges_visible_ = true;
    selection_start_ = document_point;
    selection_before_edit_ = selection_;
    selection_operation_ = selection_operation(event->modifiers());
    selection_ = combine_selection(marquee_selection_region(selection_start_, document_point));
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::Lasso) {
    lassoing_ = true;
    selection_edges_visible_ = true;
    lasso_points_.clear();
    lasso_points_ << document_point;
    selection_before_edit_ = selection_;
    selection_operation_ = selection_operation(event->modifiers());
    selection_ = selection_before_edit_;
    update();
    return;
  }

  if (tool_ == CanvasTool::MagicWand) {
    selection_edges_visible_ = true;
    selection_before_edit_ = selection_;
    selection_operation_ = selection_operation(event->modifiers());
    magic_wand_select(document_point);
    selection_before_edit_ = QRegion();
    return;
  }

  if (tool_ == CanvasTool::Zoom) {
    zooming_ = true;
    zoom_start_ = document_point;
    zoom_current_ = document_point;
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (tool_ == CanvasTool::Fill) {
    if (begin_edit(tr("Fill"))) {
      document_changed(flood_fill(document_point));
    }
    return;
  }

  if (tool_ == CanvasTool::Brush || tool_ == CanvasTool::Smudge || tool_ == CanvasTool::Eraser) {
    auto label = tr("Erase");
    if (tool_ == CanvasTool::Brush) {
      label = tr("Brush stroke");
    } else if (tool_ == CanvasTool::Smudge) {
      label = tr("Smudge");
    }
    if (begin_edit(label)) {
      brush_stroke_pixels_.clear();
      smudge_state_ = {};
      painting_ = true;
      last_document_position_ = document_point;
      if (tool_ != CanvasTool::Smudge) {
        const auto dirty = draw_brush_at(document_point, tool_ == CanvasTool::Eraser);
        document_changed(dirty);
      }
    }
    return;
  }

  if (tool_ == CanvasTool::Gradient || tool_ == CanvasTool::Line || tool_ == CanvasTool::Rectangle ||
      tool_ == CanvasTool::Ellipse) {
    if (begin_edit(tool_ == CanvasTool::Gradient ? tr("Gradient") : tr("Shape"))) {
      brush_stroke_pixels_.clear();
      drawing_shape_ = true;
      spacebar_repositioning_drag_rect_ = false;
      shape_start_ = document_point;
      shape_current_ = document_point;
      update();
    }
  }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
  emit_info_for_widget_position(event->pos());
  if (panning_) {
    const auto delta = event->pos() - last_mouse_position_;
    pan_ += QPointF(delta);
    last_mouse_position_ = event->pos();
    update();
    return;
  }

  if (dragging_transform_) {
    update_free_transform_preview(document_position_f(event->position()), event->modifiers());
    last_mouse_position_ = event->pos();
    return;
  }

  if (transforming_layer_) {
    const auto handle = transform_handle_at(event->pos());
    if (handle == TransformHandle::Move) {
      setCursor(Qt::SizeAllCursor);
    } else if (handle == TransformHandle::Rotate) {
      setCursor(Qt::CrossCursor);
    } else if (handle != TransformHandle::None) {
      setCursor(Qt::SizeFDiagCursor);
    } else {
      setCursor(Qt::ArrowCursor);
    }
    last_mouse_position_ = event->pos();
    return;
  }

  const auto document_point = document_position(event->pos());
  if (painting_) {
    QRect dirty;
    if (tool_ == CanvasTool::Clone) {
      dirty = clone_brush_segment(last_document_position_, document_point);
    } else if (tool_ == CanvasTool::Smudge) {
      dirty = smudge_brush_segment(last_document_position_, document_point);
    } else {
      dirty = draw_brush_segment(last_document_position_, document_point, tool_ == CanvasTool::Eraser);
    }
    last_document_position_ = document_point;
    document_changed(dirty);
  } else if (drawing_shape_) {
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      shape_start_ += delta;
      shape_current_ += delta;
      spacebar_reposition_last_document_position_ = document_point;
    } else {
      shape_current_ = document_point;
    }
    update();
  } else if (moving_layer_) {
    const auto old_delta = move_preview_delta_;
    move_preview_delta_ = document_point - move_start_;
    if (move_preview_delta_ == old_delta || document_ == nullptr || moving_layers_.empty()) {
      last_mouse_position_ = event->pos();
      return;
    }
    if (move_preview_cache_.isNull()) {
      ensure_render_cache();
      move_preview_cache_ = render_cache_.copy();
    }
    auto dirty = moving_layers_dirty_rect(old_delta, move_preview_delta_)
                     .intersected(QRect(0, 0, document_->width(), document_->height()));
    if (!dirty.isEmpty()) {
      const auto partial =
          qimage_from_document_rect_with_layer_bounds(*document_, dirty, true, moving_layer_bounds(move_preview_delta_))
              .convertToFormat(QImage::Format_RGBA8888);
      if (!partial.isNull()) {
        QPainter cache_painter(&move_preview_cache_);
        cache_painter.drawImage(dirty.topLeft(), partial);
      }
      update(widget_rect_for_document_rect(dirty));
    }
  } else if (selecting_) {
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      selection_start_ += delta;
      spacebar_reposition_last_document_position_ = document_point;
    }
    selection_ = combine_selection(marquee_selection_region(selection_start_, document_point));
    emit_info_for_widget_position(event->pos());
    update();
  } else if (lassoing_ && document_ != nullptr) {
    const auto point = clamped_document_point(*document_, document_point);
    if (lasso_points_.isEmpty() || (point - lasso_points_.last()).manhattanLength() >= 1) {
      lasso_points_ << point;
      update();
    }
  } else if (zooming_ && document_ != nullptr) {
    zoom_current_ = clamped_document_point(*document_, document_point);
    emit_info_for_widget_position(event->pos());
    update();
  }

  last_mouse_position_ = event->pos();
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (panning_) {
    panning_ = false;
    update_tool_cursor();
    return;
  }

  if (dragging_transform_) {
    update_free_transform_preview(document_position_f(event->position()), event->modifiers());
    dragging_transform_ = false;
    commit_free_transform();
    return;
  }

  if (painting_) {
    painting_ = false;
    clone_source_cache_ = QImage();
    smudge_state_ = {};
    brush_stroke_pixels_.clear();
    return;
  }

  if (moving_layer_) {
    move_preview_delta_ = document_position(event->pos()) - move_start_;
    QRect dirty;
    if (!move_preview_delta_.isNull() && before_edit_callback_) {
      before_edit_callback_(moving_layers_.size() > 1U ? tr("Move layers") : tr("Move layer"));
    }
    if (!move_preview_delta_.isNull()) {
      dirty = moving_layers_dirty_rect(QPoint(), move_preview_delta_);
      for (const auto& moving_layer : moving_layers_) {
        auto* layer = document_->find_layer(moving_layer.id);
        if (layer == nullptr) {
          continue;
        }
        auto new_bounds = moving_layer.original_bounds;
        new_bounds.x += move_preview_delta_.x();
        new_bounds.y += move_preview_delta_.y();
        layer->set_bounds(new_bounds);
        translate_layer_mask(*layer, move_preview_delta_);
      }
    }
    moving_layer_ = false;
    moving_layers_.clear();
    move_preview_cache_ = QImage();
    if (!dirty.isEmpty()) {
      document_changed(dirty);
    } else {
      update();
    }
    return;
  }

  if (selecting_) {
    selecting_ = false;
    const auto document_point = document_position(event->pos());
    if (spacebar_repositioning_drag_rect_) {
      selection_start_ += document_point - spacebar_reposition_last_document_position_;
      spacebar_repositioning_drag_rect_ = false;
    }
    selection_ = combine_selection(marquee_selection_region(selection_start_, document_point));
    selection_before_edit_ = QRegion();
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (lassoing_) {
    lassoing_ = false;
    if (document_ != nullptr) {
      const auto point = clamped_document_point(*document_, document_position(event->pos()));
      if (lasso_points_.isEmpty() || lasso_points_.last() != point) {
        lasso_points_ << point;
      }
      if (lasso_points_.size() >= 3) {
        auto lasso_region = QRegion(lasso_points_, Qt::WindingFill);
        lasso_region = lasso_region.intersected(QRegion(QRect(0, 0, document_->width(), document_->height())));
        selection_ = combine_selection(lasso_region);
      } else {
        selection_ = selection_before_edit_;
      }
    }
    selection_before_edit_ = QRegion();
    lasso_points_.clear();
    update();
    return;
  }

  if (zooming_) {
    zooming_ = false;
    if (document_ != nullptr) {
      zoom_current_ = clamped_document_point(*document_, document_position(event->pos()));
      const auto widget_drag = (event->pos() - widget_position(zoom_start_)).manhattanLength();
      const auto zoom_rect = normalized_rect(zoom_start_, zoom_current_);
      if (widget_drag >= 8 && zoom_rect.width() > 1 && zoom_rect.height() > 1) {
        zoom_to_document_rect(zoom_rect);
      } else {
        const auto anchor = event->position();
        const auto document_anchor = document_position_f(anchor);
        const auto factor = (event->modifiers() & Qt::AltModifier) != 0 ? 0.5 : 2.0;
        zoom_ = std::clamp(zoom_ * factor, 0.05, 32.0);
        pan_ = QPointF(anchor.x() - document_anchor.x() * zoom_, anchor.y() - document_anchor.y() * zoom_);
        update_tool_cursor();
      }
    }
    emit_info_for_widget_position(event->pos());
    update();
    return;
  }

  if (drawing_shape_) {
    const auto document_point = document_position(event->pos());
    if (spacebar_repositioning_drag_rect_) {
      const auto delta = document_point - spacebar_reposition_last_document_position_;
      shape_start_ += delta;
      shape_current_ += delta;
      spacebar_repositioning_drag_rect_ = false;
    } else {
      shape_current_ = document_point;
    }
    const auto erase = false;
    QRect preview_rect = normalized_rect(shape_start_, shape_current_);
    if (document_ != nullptr) {
      const auto margin = std::max(4, brush_size_ + 4);
      preview_rect = preview_rect.adjusted(-margin, -margin, margin, margin)
                         .intersected(QRect(0, 0, document_->width(), document_->height()));
    }
    QRect dirty;
    if (tool_ == CanvasTool::Line) {
      dirty = draw_line(shape_start_, shape_current_, erase);
    } else if (tool_ == CanvasTool::Gradient) {
      dirty = draw_gradient(shape_start_, shape_current_);
    } else if (tool_ == CanvasTool::Rectangle) {
      dirty = draw_rectangle(shape_start_, shape_current_, erase);
    } else if (tool_ == CanvasTool::Ellipse) {
      dirty = draw_ellipse(shape_start_, shape_current_, erase);
    }
    drawing_shape_ = false;
    brush_stroke_pixels_.clear();
    const auto repaint_rect =
        !preview_rect.isEmpty() && !dirty.isEmpty() ? preview_rect.united(dirty)
        : !preview_rect.isEmpty()                  ? preview_rect
                                                   : dirty;
    document_changed(repaint_rect);
    return;
  }
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  const auto document_point = document_position(event->pos());
  if (event->button() == Qt::LeftButton && document_contains(document_point)) {
    if (auto* layer = topmost_text_layer_at(document_point); layer != nullptr) {
      activate_layer(*layer);
      if (text_requested_callback_) {
        text_requested_callback_(document_point);
      }
      event->accept();
      return;
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
  if (transforming_layer_) {
    if (event->key() == Qt::Key_Escape) {
      cancel_free_transform();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      commit_free_transform();
      event->accept();
      return;
    }
  }

  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    if (selecting_ || drawing_shape_) {
      spacebar_repositioning_drag_rect_ = true;
      spacebar_reposition_last_document_position_ = document_position(last_mouse_position_);
      setCursor(Qt::SizeAllCursor);
    } else {
      spacebar_panning_ = true;
      setCursor(Qt::OpenHandCursor);
    }
    event->accept();
    return;
  }

  if (!event->isAutoRepeat() && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)) {
    const auto step = event->modifiers() == Qt::ShiftModifier ? 10 : 1;
    QPoint delta;
    switch (event->key()) {
      case Qt::Key_Left:
        delta = QPoint(-step, 0);
        break;
      case Qt::Key_Right:
        delta = QPoint(step, 0);
        break;
      case Qt::Key_Up:
        delta = QPoint(0, -step);
        break;
      case Qt::Key_Down:
        delta = QPoint(0, step);
        break;
      default:
        break;
    }
    if (!delta.isNull() && !movable_layer_ids().empty()) {
      if (before_edit_callback_) {
        before_edit_callback_(selected_layer_ids_.size() >= 2U ? tr("Nudge layers") : tr("Nudge layer"));
      }
      const auto dirty = move_active_layer_by(delta);
      if (!dirty.isEmpty()) {
        document_changed(dirty);
      }
      event->accept();
      return;
    }
  }
  QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    spacebar_repositioning_drag_rect_ = false;
    spacebar_panning_ = false;
    set_tool(tool_);
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void CanvasWidget::focusOutEvent(QFocusEvent* event) {
  spacebar_repositioning_drag_rect_ = false;
  spacebar_panning_ = false;
  if (!painting_ && !drawing_shape_) {
    brush_stroke_pixels_.clear();
  }
  clone_source_cache_ = QImage();
  smudge_state_ = {};
  zooming_ = false;
  set_tool(tool_);
  QWidget::focusOutEvent(event);
}

void CanvasWidget::timerEvent(QTimerEvent* event) {
  if (event->timerId() == selection_timer_.timerId()) {
    selection_dash_offset_ = (selection_dash_offset_ + 1) % 8;
    if ((!selection_.isEmpty() && selection_edges_visible_) || lassoing_ || zooming_) {
      update();
    }
    event->accept();
    return;
  }
  QWidget::timerEvent(event);
}

QImage CanvasWidget::render_document_image() const {
  return qimage_from_document(*document_, true).convertToFormat(QImage::Format_RGBA8888);
}

void CanvasWidget::ensure_render_cache() {
  if (document_ == nullptr) {
    return;
  }
  if (!render_cache_dirty_ && render_cache_.size() == QSize(document_->width(), document_->height())) {
    return;
  }

  render_cache_ = render_document_image();
  render_cache_dirty_ = false;
}

void CanvasWidget::refresh_render_cache_rect(QRect document_rect) {
  if (document_ == nullptr || render_cache_.isNull()) {
    return;
  }

  document_rect = document_rect.intersected(QRect(0, 0, document_->width(), document_->height()));
  if (document_rect.isEmpty()) {
    return;
  }

  if (document_style_padding(*document_) > 0) {
    const auto partial = qimage_from_document_rect(*document_, document_rect, true).convertToFormat(QImage::Format_RGBA8888);
    if (!partial.isNull()) {
      QPainter painter(&render_cache_);
      painter.drawImage(document_rect.topLeft(), partial);
    }
    return;
  }

  for (int y = document_rect.top(); y <= document_rect.bottom(); ++y) {
    auto* row = render_cache_.scanLine(y);
    for (int x = document_rect.left(); x <= document_rect.right(); ++x) {
      const auto color = compose_document_pixel(x, y);
      auto* px = row + static_cast<std::size_t>(x) * 4U;
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      px[3] = static_cast<std::uint8_t>(color.alpha());
    }
  }
}

QColor CanvasWidget::compose_document_pixel(std::int32_t x, std::int32_t y) const {
  std::array<float, 3> out = {0.0F, 0.0F, 0.0F};
  float out_alpha = 0.0F;
  if (document_ == nullptr) {
    return Qt::transparent;
  }

  for (const auto& layer : document_->layers()) {
    compose_layer_pixel(layer, x, y, out, out_alpha);
  }

  return QColor(clamp_byte(out[0]), clamp_byte(out[1]), clamp_byte(out[2]), clamp_byte(out_alpha * 255.0F));
}

void CanvasWidget::draw_checkerboard(QPainter& painter, const QRectF& rect) const {
  if (rect.isEmpty()) {
    return;
  }

  constexpr int square = 12;
  const auto aligned = rect.toAlignedRect();
  painter.save();
  painter.setClipRect(rect);
  for (int y = aligned.y(); y < aligned.y() + aligned.height(); y += square) {
    for (int x = aligned.x(); x < aligned.x() + aligned.width(); x += square) {
      const bool dark = (((x - aligned.x()) / square) + ((y - aligned.y()) / square)) % 2 == 0;
      painter.fillRect(QRect(x, y, square, square), dark ? QColor(188, 188, 188) : QColor(236, 236, 236));
    }
  }
  painter.restore();
}

void CanvasWidget::draw_shape_preview(QPainter& painter) const {
  if (!drawing_shape_) {
    return;
  }

  const auto a = widget_position(shape_start_);
  const auto b = widget_position(shape_current_);
  if (tool_ == CanvasTool::Gradient) {
    QLinearGradient gradient(a, b);
    gradient.setColorAt(0.0, primary_color_);
    gradient.setColorAt(1.0, secondary_color_);
    painter.setPen(QPen(QBrush(gradient), std::max(2, static_cast<int>(std::round(brush_size_ * zoom_ * 0.35)))));
    painter.drawLine(a, b);
    painter.setPen(QPen(QColor(230, 235, 242), 1));
    painter.setBrush(primary_color_);
    painter.drawEllipse(a, 4, 4);
    painter.setBrush(secondary_color_);
    painter.drawEllipse(b, 4, 4);
  } else if (tool_ == CanvasTool::Line) {
    QPen pen(primary_color_);
    pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
    pen.setCosmetic(false);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(a, b);
  } else if (tool_ == CanvasTool::Rectangle) {
    if (fill_shapes_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(primary_color_.red(), primary_color_.green(), primary_color_.blue(),
                              std::clamp(brush_opacity_ * 2, 60, 180)));
    } else {
      QPen pen(primary_color_);
      pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
      pen.setCosmetic(false);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
    }
    painter.drawRect(normalized_rect(a, b));
  } else if (tool_ == CanvasTool::Ellipse) {
    if (fill_shapes_) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(primary_color_.red(), primary_color_.green(), primary_color_.blue(),
                              std::clamp(brush_opacity_ * 2, 60, 180)));
    } else {
      QPen pen(primary_color_);
      pen.setWidth(std::max(1, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_))));
      pen.setCosmetic(false);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
    }
    painter.drawEllipse(normalized_rect(a, b));
  }
}

void CanvasWidget::draw_zoom_preview(QPainter& painter) const {
  if (!zooming_) {
    return;
  }

  const auto preview_rect = QRect(widget_position(zoom_start_), widget_position(zoom_current_)).normalized();
  if (preview_rect.width() < 2 && preview_rect.height() < 2) {
    return;
  }

  painter.save();
  painter.setBrush(QColor(65, 135, 220, 35));
  painter.setPen(Qt::NoPen);
  painter.drawRect(preview_rect);

  QPen dark(QColor(15, 18, 22), 1.0);
  dark.setDashPattern({4.0, 4.0});
  dark.setDashOffset(selection_dash_offset_ + 4);
  dark.setCosmetic(true);
  QPen light(QColor(248, 250, 253), 1.0);
  light.setDashPattern({4.0, 4.0});
  light.setDashOffset(selection_dash_offset_);
  light.setCosmetic(true);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(dark);
  painter.drawRect(preview_rect);
  painter.setPen(light);
  painter.drawRect(preview_rect);
  painter.restore();
}

QPointF CanvasWidget::transform_handle_position(TransformHandle handle) const {
  const auto rect = widget_rect_for_document_rect(transform_current_rect_);
  const auto center = rect.center();
  QPointF local;
  switch (handle) {
    case TransformHandle::TopLeft:
      local = QPointF(-rect.width() / 2.0, -rect.height() / 2.0);
      break;
    case TransformHandle::Top:
      local = QPointF(0.0, -rect.height() / 2.0);
      break;
    case TransformHandle::TopRight:
      local = QPointF(rect.width() / 2.0, -rect.height() / 2.0);
      break;
    case TransformHandle::Right:
      local = QPointF(rect.width() / 2.0, 0.0);
      break;
    case TransformHandle::BottomRight:
      local = QPointF(rect.width() / 2.0, rect.height() / 2.0);
      break;
    case TransformHandle::Bottom:
      local = QPointF(0.0, rect.height() / 2.0);
      break;
    case TransformHandle::BottomLeft:
      local = QPointF(-rect.width() / 2.0, rect.height() / 2.0);
      break;
    case TransformHandle::Left:
      local = QPointF(-rect.width() / 2.0, 0.0);
      break;
    case TransformHandle::Rotate:
      local = QPointF(0.0, -rect.height() / 2.0 - 32.0);
      break;
    case TransformHandle::Move:
    case TransformHandle::None:
      local = QPointF(0.0, 0.0);
      break;
  }

  QTransform transform;
  transform.translate(center.x(), center.y());
  transform.rotate(transform_angle_);
  return transform.map(local);
}

void CanvasWidget::draw_free_transform(QPainter& painter) const {
  if (!transforming_layer_ || transform_source_image_.isNull()) {
    return;
  }

  const auto rect = widget_rect_for_document_rect(transform_current_rect_);
  if (rect.isEmpty()) {
    return;
  }

  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.translate(rect.center());
  painter.rotate(transform_angle_);
  const QRectF local_rect(-rect.width() / 2.0, -rect.height() / 2.0, rect.width(), rect.height());
  painter.drawImage(local_rect, transform_source_image_, QRectF(transform_source_image_.rect()));
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0, Qt::DashLine));
  painter.drawRect(local_rect);
  painter.setPen(QPen(QColor(95, 170, 255), 1.0));
  painter.drawLine(QPointF(0.0, -rect.height() / 2.0), QPointF(0.0, -rect.height() / 2.0 - 32.0));
  painter.restore();

  constexpr double kHandleSize = 8.0;
  const std::array<TransformHandle, 9> handles = {
      TransformHandle::TopLeft,    TransformHandle::Top,    TransformHandle::TopRight,
      TransformHandle::Right,      TransformHandle::BottomRight, TransformHandle::Bottom,
      TransformHandle::BottomLeft, TransformHandle::Left,   TransformHandle::Rotate};
  painter.save();
  painter.setPen(QPen(QColor(10, 14, 20), 1.0));
  for (const auto handle : handles) {
    const auto point = transform_handle_position(handle);
    const QRectF handle_rect(point.x() - kHandleSize / 2.0, point.y() - kHandleSize / 2.0, kHandleSize, kHandleSize);
    painter.setBrush(handle == TransformHandle::Rotate ? QColor(95, 170, 255) : QColor(245, 248, 252));
    painter.drawRect(handle_rect);
  }
  painter.restore();
}

void CanvasWidget::draw_selection_overlay(QPainter& painter) const {
  if (!selection_.isEmpty() && selection_edges_visible_) {
    QPainterPath outline_path;
    const auto add_vertical_edges = [this, &outline_path](const QRegion& edges, bool right_edge) {
      for (const auto& rect : edges) {
        const auto x = static_cast<double>(right_edge ? rect.right() + 1 : rect.left());
        const auto top = static_cast<double>(rect.top());
        const auto bottom = static_cast<double>(rect.bottom() + 1);
        outline_path.moveTo(widget_position_f(QPointF(x, top)));
        outline_path.lineTo(widget_position_f(QPointF(x, bottom)));
      }
    };
    const auto add_horizontal_edges = [this, &outline_path](const QRegion& edges, bool bottom_edge) {
      for (const auto& rect : edges) {
        const auto y = static_cast<double>(bottom_edge ? rect.bottom() + 1 : rect.top());
        const auto left = static_cast<double>(rect.left());
        const auto right = static_cast<double>(rect.right() + 1);
        outline_path.moveTo(widget_position_f(QPointF(left, y)));
        outline_path.lineTo(widget_position_f(QPointF(right, y)));
      }
    };

    add_vertical_edges(selection_.subtracted(selection_.translated(1, 0)), false);
    add_vertical_edges(selection_.subtracted(selection_.translated(-1, 0)), true);
    add_horizontal_edges(selection_.subtracted(selection_.translated(0, 1)), false);
    add_horizontal_edges(selection_.subtracted(selection_.translated(0, -1)), true);

    if (!outline_path.isEmpty()) {
      QPen white(QColor(248, 250, 253), 1.0);
      white.setDashPattern({4.0, 4.0});
      white.setDashOffset(selection_dash_offset_);
      white.setCosmetic(true);
      QPen black(QColor(18, 20, 24), 1.0);
      black.setDashPattern({4.0, 4.0});
      black.setDashOffset(selection_dash_offset_ + 4);
      black.setCosmetic(true);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(black);
      painter.drawPath(outline_path);
      painter.setPen(white);
      painter.drawPath(outline_path);
    }
  }

  if (lassoing_ && lasso_points_.size() > 1) {
    QPolygon preview;
    preview.reserve(lasso_points_.size());
    for (const auto& point : lasso_points_) {
      preview << widget_position(point);
    }
    QPen black(QColor(18, 20, 24), 1.0);
    black.setDashPattern({4.0, 4.0});
    black.setDashOffset(selection_dash_offset_ + 4);
    black.setCosmetic(true);
    QPen white(QColor(248, 250, 253), 1.0);
    white.setDashPattern({4.0, 4.0});
    white.setDashOffset(selection_dash_offset_);
    white.setCosmetic(true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(black);
    painter.drawPolyline(preview);
    painter.setPen(white);
    painter.drawPolyline(preview);
  }
}

void CanvasWidget::update_tool_cursor() {
  if (spacebar_panning_ || tool_ == CanvasTool::Pan) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  if (tool_ == CanvasTool::Move) {
    setCursor(Qt::SizeAllCursor);
    return;
  }
  if (tool_ == CanvasTool::MagicWand) {
    setCursor(Qt::PointingHandCursor);
    return;
  }
  if (tool_ == CanvasTool::Text) {
    setCursor(Qt::IBeamCursor);
    return;
  }
  if (tool_ == CanvasTool::Zoom) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(20, 23, 28), 3));
    painter.drawEllipse(QRect(4, 4, 11, 11));
    painter.drawLine(13, 13, 21, 21);
    painter.setPen(QPen(QColor(245, 248, 252), 1.6));
    painter.drawEllipse(QRect(4, 4, 11, 11));
    painter.drawLine(13, 13, 21, 21);
    painter.drawLine(7, 10, 13, 10);
    painter.drawLine(10, 7, 10, 13);
    painter.end();
    setCursor(QCursor(pixmap, 10, 10));
    return;
  }
  if (tool_ == CanvasTool::Brush || tool_ == CanvasTool::Clone || tool_ == CanvasTool::Smudge ||
      tool_ == CanvasTool::Eraser) {
    const auto diameter = std::max(3, static_cast<int>(std::round(static_cast<double>(brush_size_) * zoom_)));
    const auto extent = std::clamp(diameter + 5, 17, 160);
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPoint center(extent / 2, extent / 2);
    const auto radius = std::max(2, std::min(diameter, extent - 5) / 2);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
    painter.drawEllipse(center, std::max(1, radius - 1), std::max(1, radius - 1));
    painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
    painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
    painter.end();
    setCursor(QCursor(pixmap, center.x(), center.y()));
    return;
  }
  setCursor(Qt::CrossCursor);
}

QPoint CanvasWidget::document_position(const QPoint& widget_position) const {
  const auto x = static_cast<int>(std::floor((static_cast<double>(widget_position.x()) - pan_.x()) / zoom_));
  const auto y = static_cast<int>(std::floor((static_cast<double>(widget_position.y()) - pan_.y()) / zoom_));
  return QPoint(x, y);
}

QPointF CanvasWidget::document_position_f(QPointF widget_position) const {
  return QPointF((widget_position.x() - pan_.x()) / zoom_, (widget_position.y() - pan_.y()) / zoom_);
}

QPoint CanvasWidget::widget_position(const QPoint& document_position) const {
  return QPoint(static_cast<int>(std::round(pan_.x() + static_cast<double>(document_position.x()) * zoom_)),
                static_cast<int>(std::round(pan_.y() + static_cast<double>(document_position.y()) * zoom_)));
}

QPointF CanvasWidget::widget_position_f(QPointF document_position) const {
  return QPointF(pan_.x() + document_position.x() * zoom_, pan_.y() + document_position.y() * zoom_);
}

bool CanvasWidget::document_contains(QPoint point) const noexcept {
  return document_ != nullptr && point.x() >= 0 && point.y() >= 0 && point.x() < document_->width() &&
         point.y() < document_->height();
}

bool CanvasWidget::selection_allows(QPoint point) const noexcept {
  return selection_contains(point);
}

Layer* CanvasWidget::active_pixel_layer() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return nullptr;
  }

  auto* layer = document_->find_layer(*document_->active_layer_id());
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return nullptr;
  }
  return layer;
}

bool CanvasWidget::active_layer_locks_transparent_pixels() const noexcept {
  const auto* layer = active_pixel_layer();
  return layer != nullptr && layer_locks_transparent_pixels(*layer);
}

Layer* CanvasWidget::topmost_pixel_layer_at(QPoint document_point, bool require_visible_pixel) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return topmost_pixel_layer_at_recursive(document_->layers(), document_point, require_visible_pixel);
}

Layer* CanvasWidget::topmost_text_layer_at(QPoint document_point) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return topmost_text_layer_at_recursive(document_->layers(), document_point);
}

void CanvasWidget::activate_layer(Layer& layer) {
  if (document_ == nullptr) {
    return;
  }
  if (!document_->active_layer_id().has_value() || *document_->active_layer_id() != layer.id()) {
    document_->set_active_layer(layer.id());
  }
  if (active_layer_changed_callback_) {
    active_layer_changed_callback_(layer.id());
  }
}

QPoint CanvasWidget::layer_position(const Layer& layer, QPoint document_point) const noexcept {
  const auto bounds = layer.bounds();
  return QPoint(document_point.x() - bounds.x, document_point.y() - bounds.y);
}

QRect CanvasWidget::widget_rect_for_document_rect(QRect document_rect) const {
  const auto top_left = widget_position(document_rect.topLeft());
  const auto bottom_right = widget_position(document_rect.bottomRight() + QPoint(1, 1));
  return QRect(top_left, bottom_right).normalized().adjusted(-2, -2, 2, 2);
}

QRectF CanvasWidget::widget_rect_for_document_rect(QRectF document_rect) const {
  const auto top_left = widget_position_f(document_rect.topLeft());
  const auto bottom_right = widget_position_f(document_rect.bottomRight());
  return QRectF(top_left, bottom_right).normalized();
}

void CanvasWidget::emit_info_for_widget_position(QPoint widget_position) const {
  if (!info_callback_) {
    return;
  }

  CanvasInfoState info;
  const auto document_point = document_position(widget_position);
  info.document_point = document_point;
  info.inside_document = document_contains(document_point);
  if (info.inside_document) {
    info.color = compose_document_pixel(document_point.x(), document_point.y());
  }
  if (document_ != nullptr && selecting_) {
    info.active_rect = marquee_selection_region(selection_start_, document_point).boundingRect();
    info.active_rect_label = tr("Selection");
  } else if (document_ != nullptr && drawing_shape_) {
    info.active_rect = normalized_rect(shape_start_, document_point);
    info.active_rect_label = tr("Shape");
  } else if (document_ != nullptr && zooming_) {
    info.active_rect = normalized_rect(zoom_start_, document_point);
    info.active_rect_label = tr("Zoom");
  }
  info_callback_(std::move(info));
}

bool CanvasWidget::begin_edit(QString label) {
  auto* layer = active_pixel_layer();
  if (layer == nullptr || layer->pixels().format().bit_depth != BitDepth::UInt8) {
    if (status_callback_) {
      status_callback_(tr("Select an editable 8-bit pixel layer first"));
    }
    return false;
  }
  if (layer->metadata().contains("photoslop.text")) {
    if (status_callback_) {
      status_callback_(tr("Select a normal pixel layer before painting on text"));
    }
    return false;
  }

  if (before_edit_callback_) {
    before_edit_callback_(label);
  }
  return true;
}

QRect CanvasWidget::draw_brush_segment(QPoint from, QPoint to, bool erase) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), selection_);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(photoslop::paint_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(),
                                                 to.y(), options, erase));
}

QRect CanvasWidget::draw_brush_at(QPoint point, bool erase) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), selection_);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(
      photoslop::paint_brush(*document_, *document_->active_layer_id(), point.x(), point.y(), options, erase));
}

QRect CanvasWidget::smudge_brush_segment(QPoint from, QPoint to) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_, active_layer_locks_transparent_pixels(), selection_);
  return to_qrect(photoslop::smudge_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(),
                                                  to.x(), to.y(), options, smudge_state_));
}

void CanvasWidget::set_clone_source(QPoint point) {
  if (!document_contains(point)) {
    return;
  }
  clone_source_point_ = point;
  clone_source_set_ = true;
  clone_aligned_offset_set_ = false;
  if (status_callback_) {
    status_callback_(tr("Clone source set at %1, %2").arg(point.x()).arg(point.y()));
  }
}

QRect CanvasWidget::clone_brush_at(QPoint point) {
  return clone_brush_segment(point, point);
}

QRect CanvasWidget::clone_brush_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr || clone_source_cache_.isNull() ||
      layer->pixels().format().bit_depth != BitDepth::UInt8 || layer->pixels().format().channels < 3) {
    return {};
  }

  const auto radius = std::max(1, brush_size_) / 2;
  const auto left = std::min(from.x(), to.x()) - radius;
  const auto top = std::min(from.y(), to.y()) - radius;
  const auto right = std::max(from.x(), to.x()) + radius + 1;
  const auto bottom = std::max(from.y(), to.y()) + radius + 1;
  auto stroke_rect = QRect(left, top, right - left, bottom - top).intersected(
      QRect(0, 0, document_->width(), document_->height()));
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    photoslop::expand_layer_to_include_rect(*layer, to_core_rect(stroke_rect));
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  stroke_rect = stroke_rect.intersected(to_qrect(bounds));
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  const auto segment_length_squared = static_cast<double>(dx) * static_cast<double>(dx) +
                                      static_cast<double>(dy) * static_cast<double>(dy);
  const auto opacity = static_cast<float>(brush_opacity_) / 100.0F;

  QRect dirty;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    auto row = pixels.row(y - bounds.y);
    for (int x = stroke_rect.left(); x <= stroke_rect.right(); ++x) {
      const auto along =
          segment_length_squared <= 0.0
              ? 0.0
              : std::clamp((static_cast<double>(x - from.x()) * static_cast<double>(dx) +
                            static_cast<double>(y - from.y()) * static_cast<double>(dy)) /
                               segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = static_cast<double>(from.x()) + static_cast<double>(dx) * along;
      const auto closest_y = static_cast<double>(from.y()) + static_cast<double>(dy) * along;
      const auto distance_x = static_cast<double>(x) - closest_x;
      const auto distance_y = static_cast<double>(y) - closest_y;
      const auto coverage = brush_coverage(distance_x * distance_x + distance_y * distance_y, radius,
                                           brush_softness_);
      if (coverage <= 0.0F) {
        continue;
      }
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }

      const auto source_point = document_point + clone_source_offset_;
      if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= clone_source_cache_.width() ||
          source_point.y() >= clone_source_cache_.height()) {
        continue;
      }

      const auto local_x = x - bounds.x;
      auto* dst = row.data() + static_cast<std::size_t>(local_x) * channels;
      if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
        continue;
      }
      if (brush_opacity_ < 100 &&
          !brush_stroke_pixels_.insert(stroke_pixel_key(document_point.x(), document_point.y())).second) {
        continue;
      }

      const auto* src = clone_source_cache_.constScanLine(source_point.y()) +
                        static_cast<std::size_t>(source_point.x()) * 4U;
      const auto covered_opacity = opacity * coverage;
      if (channels >= 4 && !lock_transparent_pixels) {
        dst[0] = clamp_byte(static_cast<float>(src[0]) * covered_opacity +
                            static_cast<float>(dst[0]) * (1.0F - covered_opacity));
        dst[1] = clamp_byte(static_cast<float>(src[1]) * covered_opacity +
                            static_cast<float>(dst[1]) * (1.0F - covered_opacity));
        dst[2] = clamp_byte(static_cast<float>(src[2]) * covered_opacity +
                            static_cast<float>(dst[2]) * (1.0F - covered_opacity));
        dst[3] = clamp_byte(static_cast<float>(src[3]) * covered_opacity +
                            static_cast<float>(dst[3]) * (1.0F - covered_opacity));
      } else {
        const auto effective_opacity = covered_opacity * (static_cast<float>(src[3]) / 255.0F);
        if (effective_opacity <= 0.0F) {
          continue;
        }
        dst[0] = clamp_byte(static_cast<float>(src[0]) * effective_opacity +
                            static_cast<float>(dst[0]) * (1.0F - effective_opacity));
        dst[1] = clamp_byte(static_cast<float>(src[1]) * effective_opacity +
                            static_cast<float>(dst[1]) * (1.0F - effective_opacity));
        dst[2] = clamp_byte(static_cast<float>(src[2]) * effective_opacity +
                            static_cast<float>(dst[2]) * (1.0F - effective_opacity));
      }
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
  }
  return dirty;
}

void CanvasWidget::draw_pixel(Layer& layer, QPoint document_point, QColor color, bool erase) {
  if (!document_contains(document_point) || !selection_allows(document_point)) {
    return;
  }

  auto& pixels = layer.pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return;
  }

  const auto local = layer_position(layer, document_point);
  if (local.x() < 0 || local.y() < 0 || local.x() >= pixels.width() || local.y() >= pixels.height()) {
    return;
  }

  auto* px = pixels.pixel(local.x(), local.y());
  if (erase) {
    if (pixels.format().channels >= 4) {
      px[3] = 0;
    } else {
      px[0] = channel_from_color(secondary_color_, 0);
      px[1] = channel_from_color(secondary_color_, 1);
      px[2] = channel_from_color(secondary_color_, 2);
    }
    return;
  }

  px[0] = channel_from_color(color, 0);
  px[1] = channel_from_color(color, 1);
  px[2] = channel_from_color(color, 2);
  if (pixels.format().channels >= 4) {
    px[3] = channel_from_color(color, 3);
  }
}

QRect CanvasWidget::draw_line(QPoint from, QPoint to, bool erase) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), selection_);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(photoslop::draw_line(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                       options, erase));
}

QRect CanvasWidget::draw_gradient(QPoint from, QPoint to) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  return to_qrect(photoslop::draw_linear_gradient(
      *document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
      edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_, fill_shapes_,
                   active_layer_locks_transparent_pixels(), selection_)));
}

QRect CanvasWidget::draw_rectangle(QPoint from, QPoint to, bool erase) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto rect = normalized_rect(from, to);
  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), selection_);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(photoslop::draw_rectangle(*document_, *document_->active_layer_id(), to_core_rect(rect),
                                            options, erase));
}

QRect CanvasWidget::draw_ellipse(QPoint from, QPoint to, bool erase) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto rect = normalized_rect(from, to);
  auto options = edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_,
                              fill_shapes_,
                              active_layer_locks_transparent_pixels(), selection_);
  if (brush_opacity_ < 100) {
    options.stroke_pixel_gate = [this](std::int32_t x, std::int32_t y) {
      return brush_stroke_pixels_.insert(stroke_pixel_key(x, y)).second;
    };
  }
  return to_qrect(photoslop::draw_ellipse(*document_, *document_->active_layer_id(), to_core_rect(rect),
                                          options, erase));
}

QRect CanvasWidget::flood_fill(QPoint start) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  return to_qrect(photoslop::flood_fill(
      *document_, *document_->active_layer_id(), start.x(), start.y(),
      edit_options(primary_color_, secondary_color_, brush_size_, brush_opacity_, brush_softness_, fill_shapes_,
                   active_layer_locks_transparent_pixels(), selection_)));
}

void CanvasWidget::pick_color(QPoint point) {
  if (document_ == nullptr || !document_contains(point)) {
    return;
  }

  const auto picked = compose_document_pixel(point.x(), point.y());
  primary_color_ = picked;
  if (color_picked_callback_) {
    color_picked_callback_(picked);
  }
}

void CanvasWidget::magic_wand_select(QPoint start) {
  if (document_ == nullptr || !document_contains(start)) {
    return;
  }

  ensure_render_cache();
  if (render_cache_.isNull()) {
    return;
  }

  const auto& flattened = render_cache_;
  const auto width = flattened.width();
  const auto height = flattened.height();
  const auto total_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto pixel = [&flattened](int x, int y) {
    return flattened.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  };
  const auto* target = pixel(start.x(), start.y());
  const int target_red = target[0];
  const int target_green = target[1];
  const int target_blue = target[2];
  const int target_alpha = target[3];
  const auto tolerance_squared = wand_tolerance_ * wand_tolerance_ * 4;
  std::vector<std::uint8_t> visited(total_pixels);
  std::vector<std::uint8_t> selected(total_pixels);
  std::vector<int> queue;
  queue.reserve(std::min<std::size_t>(total_pixels, 1'000'000U));
  queue.push_back(start.y() * width + start.x());
  auto min_x = std::numeric_limits<int>::max();
  auto min_y = std::numeric_limits<int>::max();
  auto max_x = std::numeric_limits<int>::min();
  auto max_y = std::numeric_limits<int>::min();
  int count = 0;

  const auto matches = [&](int x, int y) {
    const auto* color = pixel(x, y);
    const auto dr = static_cast<int>(color[0]) - target_red;
    const auto dg = static_cast<int>(color[1]) - target_green;
    const auto db = static_cast<int>(color[2]) - target_blue;
    const auto da = static_cast<int>(color[3]) - target_alpha;
    return dr * dr + dg * dg + db * db + da * da <= tolerance_squared;
  };

  while (!queue.empty()) {
    const auto index_value = queue.back();
    queue.pop_back();
    const auto index = static_cast<std::size_t>(index_value);
    if (visited[index] != 0U) {
      continue;
    }
    visited[index] = 1U;
    const auto x = index_value % width;
    const auto y = index_value / width;
    if (!matches(x, y)) {
      continue;
    }

    selected[index] = 1U;
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    ++count;
    if (x + 1 < width && visited[index + 1U] == 0U) {
      queue.push_back(index_value + 1);
    }
    if (x > 0 && visited[index - 1U] == 0U) {
      queue.push_back(index_value - 1);
    }
    if (y + 1 < height && visited[index + static_cast<std::size_t>(width)] == 0U) {
      queue.push_back(index_value + width);
    }
    if (y > 0 && visited[index - static_cast<std::size_t>(width)] == 0U) {
      queue.push_back(index_value - width);
    }
  }

  if (count == 0) {
    selection_ = selection_operation_ == SelectionMode::Replace ? QRegion() : selection_before_edit_;
  } else {
    std::vector<QRect> runs;
    runs.reserve(static_cast<std::size_t>(std::max(1, max_y - min_y + 1)));
    for (int y = min_y; y <= max_y; ++y) {
      int run_start = -1;
      for (int x = min_x; x <= max_x + 1; ++x) {
        const bool is_selected =
            x <= max_x && selected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x)] != 0U;
        if (is_selected && run_start < 0) {
          run_start = x;
        } else if (!is_selected && run_start >= 0) {
          runs.push_back(QRect(run_start, y, x - run_start, 1));
          run_start = -1;
        }
      }
    }
    QRegion wand_region;
    if (!runs.empty()) {
      wand_region.setRects(runs.data(), static_cast<int>(runs.size()));
    }
    selection_ = combine_selection(wand_region);
  }
  if (status_callback_) {
    status_callback_(tr("Magic Wand selected %1 px").arg(count));
  }
  update();
}

QRegion CanvasWidget::marquee_selection_region(QPoint anchor, QPoint current) const {
  if (document_ == nullptr) {
    return {};
  }

  QRect rect;
  if (marquee_style_ == MarqueeStyle::FixedSize) {
    rect = QRect(anchor, marquee_fixed_size_);
  } else if (marquee_style_ == MarqueeStyle::FixedRatio) {
    const auto ratio =
        std::max(1.0, static_cast<double>(marquee_fixed_size_.width())) /
        std::max(1.0, static_cast<double>(marquee_fixed_size_.height()));
    const auto delta = current - anchor;
    auto width = std::max(1, std::abs(delta.x()));
    auto height = std::max(1, std::abs(delta.y()));
    if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
      width = std::max(1, static_cast<int>(std::round(height * ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / ratio)));
    }
    const auto signed_width = delta.x() < 0 ? -width : width;
    const auto signed_height = delta.y() < 0 ? -height : height;
    rect = QRect(anchor, anchor + QPoint(signed_width, signed_height)).normalized();
  } else {
    rect = normalized_rect(anchor, current);
  }

  const auto marquee = tool_ == CanvasTool::EllipticalMarquee ? QRegion(rect, QRegion::Ellipse) : QRegion(rect);
  return marquee.intersected(QRect(0, 0, document_->width(), document_->height()));
}

CanvasWidget::SelectionMode CanvasWidget::selection_operation(Qt::KeyboardModifiers modifiers) const noexcept {
  const auto add = modifiers.testFlag(Qt::ShiftModifier);
  const auto subtract = modifiers.testFlag(Qt::AltModifier);
  if (add && subtract) {
    return SelectionMode::Intersect;
  }
  if (add) {
    return SelectionMode::Add;
  }
  if (subtract) {
    return SelectionMode::Subtract;
  }
  return selection_mode_;
}

QRegion CanvasWidget::combine_selection(const QRegion& candidate) const {
  if (candidate.isEmpty()) {
    return selection_before_edit_;
  }

  if (selection_before_edit_.isEmpty()) {
    return selection_operation_ == SelectionMode::Subtract ? QRegion() : candidate;
  }

  switch (selection_operation_) {
    case SelectionMode::Replace:
      return candidate;
    case SelectionMode::Add:
      return selection_before_edit_.united(candidate);
    case SelectionMode::Intersect: {
      return selection_before_edit_.intersected(candidate);
    }
    case SelectionMode::Subtract:
      return selection_before_edit_.subtracted(candidate);
  }
  return candidate;
}

CanvasWidget::TransformHandle CanvasWidget::transform_handle_at(QPoint widget_point) const {
  if (!transforming_layer_) {
    return TransformHandle::None;
  }

  constexpr double kHandleHit = 14.0;
  const std::array<TransformHandle, 9> handles = {
      TransformHandle::Rotate,     TransformHandle::TopLeft, TransformHandle::Top,
      TransformHandle::TopRight,   TransformHandle::Right,   TransformHandle::BottomRight,
      TransformHandle::Bottom,     TransformHandle::BottomLeft, TransformHandle::Left};
  for (const auto handle : handles) {
    const auto point = transform_handle_position(handle);
    const QRectF hit_rect(point.x() - kHandleHit / 2.0, point.y() - kHandleHit / 2.0, kHandleHit, kHandleHit);
    if (hit_rect.contains(widget_point)) {
      return handle;
    }
  }

  QPolygonF polygon;
  polygon << transform_handle_position(TransformHandle::TopLeft) << transform_handle_position(TransformHandle::TopRight)
          << transform_handle_position(TransformHandle::BottomRight)
          << transform_handle_position(TransformHandle::BottomLeft);
  QPainterPath path;
  path.addPolygon(polygon);
  if (path.contains(widget_point)) {
    return TransformHandle::Move;
  }
  return TransformHandle::None;
}

void CanvasWidget::update_free_transform_preview(QPointF document_point, Qt::KeyboardModifiers modifiers) {
  auto rect = transform_drag_start_rect_;
  const auto drag_delta = document_point - transform_drag_start_point_;

  if (transform_drag_handle_ == TransformHandle::Move) {
    rect.translate(drag_delta);
    transform_current_rect_ = rect;
    update();
    return;
  }

  if (transform_drag_handle_ == TransformHandle::Rotate) {
    const auto center = transform_drag_start_rect_.center();
    const auto start = std::atan2(transform_drag_start_point_.y() - center.y(), transform_drag_start_point_.x() - center.x());
    const auto now = std::atan2(document_point.y() - center.y(), document_point.x() - center.x());
    auto degrees = transform_start_angle_ + ((now - start) * 180.0 / kPi);
    if ((modifiers & Qt::ShiftModifier) != 0) {
      degrees = std::round(degrees / 15.0) * 15.0;
    }
    transform_angle_ = degrees;
    update();
    return;
  }

  switch (transform_drag_handle_) {
    case TransformHandle::TopLeft:
      rect.setTopLeft(document_point);
      break;
    case TransformHandle::Top:
      rect.setTop(document_point.y());
      break;
    case TransformHandle::TopRight:
      rect.setTopRight(document_point);
      break;
    case TransformHandle::Right:
      rect.setRight(document_point.x());
      break;
    case TransformHandle::BottomRight:
      rect.setBottomRight(document_point);
      break;
    case TransformHandle::Bottom:
      rect.setBottom(document_point.y());
      break;
    case TransformHandle::BottomLeft:
      rect.setBottomLeft(document_point);
      break;
    case TransformHandle::Left:
      rect.setLeft(document_point.x());
      break;
    case TransformHandle::None:
    case TransformHandle::Move:
    case TransformHandle::Rotate:
      break;
  }

  const auto corner_handle = transform_drag_handle_ == TransformHandle::TopLeft ||
                             transform_drag_handle_ == TransformHandle::TopRight ||
                             transform_drag_handle_ == TransformHandle::BottomRight ||
                             transform_drag_handle_ == TransformHandle::BottomLeft;
  if (corner_handle && (modifiers & Qt::ShiftModifier) != 0 && transform_drag_start_rect_.height() > 0.0) {
    QPointF anchor;
    switch (transform_drag_handle_) {
      case TransformHandle::TopLeft:
        anchor = transform_drag_start_rect_.bottomRight();
        break;
      case TransformHandle::TopRight:
        anchor = transform_drag_start_rect_.bottomLeft();
        break;
      case TransformHandle::BottomLeft:
        anchor = transform_drag_start_rect_.topRight();
        break;
      case TransformHandle::BottomRight:
      default:
        anchor = transform_drag_start_rect_.topLeft();
        break;
    }

    const auto ratio = transform_drag_start_rect_.width() / transform_drag_start_rect_.height();
    const auto dx = document_point.x() - anchor.x();
    const auto dy = document_point.y() - anchor.y();
    const auto sign_x = dx < 0.0 ? -1.0 : 1.0;
    const auto sign_y = dy < 0.0 ? -1.0 : 1.0;
    auto new_width = std::max(1.0, std::abs(dx));
    auto new_height = std::max(1.0, std::abs(dy));
    if (new_width / ratio > new_height) {
      new_height = new_width / ratio;
    } else {
      new_width = new_height * ratio;
    }
    rect = QRectF(anchor, QSizeF(sign_x * new_width, sign_y * new_height)).normalized();
  }

  if (rect.width() < 1.0) {
    rect.setWidth(1.0);
  }
  if (rect.height() < 1.0) {
    rect.setHeight(1.0);
  }
  transform_current_rect_ = rect.normalized();
  update();
}

void CanvasWidget::commit_free_transform() {
  if (!transforming_layer_ || document_ == nullptr || !transform_layer_id_.has_value() ||
      transform_source_image_.isNull()) {
    cancel_free_transform();
    return;
  }

  auto* layer = document_->find_layer(*transform_layer_id_);
  if (layer == nullptr) {
    cancel_free_transform();
    return;
  }

  const auto new_width = std::max(1, static_cast<int>(std::round(transform_current_rect_.width())));
  const auto new_height = std::max(1, static_cast<int>(std::round(transform_current_rect_.height())));
  auto transformed = transform_source_image_.scaled(new_width, new_height, Qt::IgnoreAspectRatio,
                                                    Qt::SmoothTransformation);
  auto new_bounds = Rect{static_cast<std::int32_t>(std::round(transform_current_rect_.left())),
                         static_cast<std::int32_t>(std::round(transform_current_rect_.top())), transformed.width(),
                         transformed.height()};

  if (std::abs(transform_angle_) > 0.01) {
    const QRectF local_rect(-static_cast<double>(new_width) / 2.0, -static_cast<double>(new_height) / 2.0,
                            static_cast<double>(new_width), static_cast<double>(new_height));
    QTransform rotation;
    rotation.rotate(transform_angle_);
    const auto rotated_bounds = rotation.mapRect(local_rect);
    QImage rotated(std::max(1, static_cast<int>(std::ceil(rotated_bounds.width()))),
                   std::max(1, static_cast<int>(std::ceil(rotated_bounds.height()))), QImage::Format_RGBA8888);
    rotated.fill(Qt::transparent);
    QPainter painter(&rotated);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(-rotated_bounds.left(), -rotated_bounds.top());
    painter.rotate(transform_angle_);
    painter.drawImage(local_rect, transformed, QRectF(transformed.rect()));
    painter.end();
    transformed = rotated;
    const auto center = transform_current_rect_.center();
    new_bounds =
        Rect{static_cast<std::int32_t>(std::floor(center.x() + rotated_bounds.left())),
             static_cast<std::int32_t>(std::floor(center.y() + rotated_bounds.top())), transformed.width(),
             transformed.height()};
  }

  const auto old_bounds = layer->bounds();
  const auto original_transform_bounds =
      Rect{static_cast<std::int32_t>(std::round(transform_original_rect_.left())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.top())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.width())),
           static_cast<std::int32_t>(std::round(transform_original_rect_.height()))};
  const auto changed = std::abs(transform_angle_) > 0.01 || new_bounds.x != original_transform_bounds.x ||
                       new_bounds.y != original_transform_bounds.y ||
                       new_bounds.width != original_transform_bounds.width ||
                       new_bounds.height != original_transform_bounds.height;
  if (changed && before_edit_callback_) {
    before_edit_callback_(tr("Free Transform"));
  }
  if (changed) {
    layer->set_pixels(pixels_from_image_rgba(transformed));
    layer->set_bounds(new_bounds);
  }

  transforming_layer_ = false;
  dragging_transform_ = false;
  transform_layer_id_.reset();
  transform_drag_handle_ = TransformHandle::None;
  transform_base_cache_ = QImage();
  transform_source_image_ = QImage();
  update_tool_cursor();
  document_changed(to_qrect(old_bounds).united(to_qrect(new_bounds)));
  if (active_layer_changed_callback_) {
    active_layer_changed_callback_(layer->id());
  }
  if (status_callback_) {
    status_callback_(changed ? tr("Transformed layer") : tr("Free Transform cancelled"));
  }
}

std::vector<LayerId> CanvasWidget::movable_layer_ids() const {
  std::vector<LayerId> ids;
  if (document_ == nullptr) {
    return ids;
  }

  auto add_if_movable = [this, &ids](LayerId id) {
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
      return;
    }
    auto* layer = document_->find_layer(id);
    if (layer == nullptr || layer->kind() != LayerKind::Pixel || layer->pixels().empty() ||
        layer->pixels().format().bit_depth != BitDepth::UInt8) {
      return;
    }
    ids.push_back(id);
  };

  if (selected_layer_ids_.size() >= 2U) {
    for (const auto id : selected_layer_ids_) {
      add_if_movable(id);
    }
  }

  if (ids.empty()) {
    if (auto* layer = active_pixel_layer(); layer != nullptr && !layer->pixels().empty() &&
        layer->pixels().format().bit_depth == BitDepth::UInt8) {
      ids.push_back(layer->id());
    }
  }
  return ids;
}

std::vector<std::pair<LayerId, Rect>> CanvasWidget::moving_layer_bounds(QPoint delta) const {
  std::vector<std::pair<LayerId, Rect>> bounds;
  bounds.reserve(moving_layers_.size());
  for (const auto& moving_layer : moving_layers_) {
    auto moved = moving_layer.original_bounds;
    moved.x += delta.x();
    moved.y += delta.y();
    bounds.emplace_back(moving_layer.id, moved);
  }
  return bounds;
}

QRect CanvasWidget::moving_layers_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  QRect dirty;
  if (document_ == nullptr) {
    return dirty;
  }
  for (const auto& moving_layer : moving_layers_) {
    auto* layer = document_->find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    auto old_bounds = moving_layer.original_bounds;
    old_bounds.x += old_delta.x();
    old_bounds.y += old_delta.y();
    auto new_bounds = moving_layer.original_bounds;
    new_bounds.x += new_delta.x();
    new_bounds.y += new_delta.y();
    dirty = dirty.united(to_qrect(layer_render_bounds_for_bounds(*layer, old_bounds)));
    dirty = dirty.united(to_qrect(layer_render_bounds_for_bounds(*layer, new_bounds)));
  }
  return dirty;
}

QRect CanvasWidget::move_active_layer_by(QPoint delta) {
  if (document_ == nullptr || delta.isNull()) {
    return {};
  }
  QRect dirty;
  for (const auto id : movable_layer_ids()) {
    auto* layer = document_->find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    const auto old_bounds = layer->bounds();
    auto bounds = old_bounds;
    bounds.x += delta.x();
    bounds.y += delta.y();
    layer->set_bounds(bounds);
    translate_layer_mask(*layer, delta);
    dirty = dirty.united(to_qrect(layer_render_bounds_for_bounds(*layer, old_bounds)));
    dirty = dirty.united(to_qrect(layer_render_bounds_for_bounds(*layer, bounds)));
  }
  return dirty;
}

}  // namespace photoslop::ui
