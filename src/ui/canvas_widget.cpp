#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/gpu_document_capabilities.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/theme_palette.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QDebug>
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
#include <QScrollBar>
#include <QSet>
#include <QTabletEvent>
#include <QTimer>
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

QImage qimage_from_flat_composite_pixels(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return {};
  }

  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  const auto channels = pixels.format().channels;
  const auto source_stride = pixels.stride_bytes();
  const auto* source_bytes = pixels.data().data();
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto* source_row = source_bytes + static_cast<std::size_t>(y) * source_stride;
    auto* target_row = image.scanLine(y);
    if (channels >= 4) {
      std::memcpy(target_row, source_row, static_cast<std::size_t>(pixels.width()) * 4U);
      continue;
    }
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* src = source_row + static_cast<std::size_t>(x) * channels;
      auto* dst = target_row + static_cast<std::size_t>(x) * 4U;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = 255;
    }
  }
  return image;
}

// The hit-test walks must not count as mutations: descending through the
// non-const Layer::children() accessor bumps every visited group's revisions
// on every Move-tool hover and press, silently invalidating the revision-keyed
// thumbnail and style-mask caches (same bug class as Document::find_layer_recursive).
// Walk const here; the topmost_*_at wrappers cast the hit.
const Layer* topmost_pixel_layer_at_recursive(const std::vector<Layer>& layers, QPoint document_point,
                                              bool require_visible_pixel, bool skip_locked,
                                              LayerLockFlags ancestor_lock_flags = kLayerLockNone) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto effective_lock_flags = ancestor_lock_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found =
              topmost_pixel_layer_at_recursive(layer.children(), document_point, require_visible_pixel, skip_locked,
                                               effective_lock_flags);
          found != nullptr) {
        return found;
      }
      continue;
    }
    if (skip_locked && (effective_lock_flags & kLayerLockImagePixels) != kLayerLockNone) {
      continue;
    }
    if (pixel_layer_contains_document_point(layer, document_point, require_visible_pixel)) {
      return &layer;
    }
  }
  return nullptr;
}

// One walk for both Move-tool hit passes: `contains` is the pixel-precise
// test (move_layer_contains_document_point) or the outline-rect test
// (move_layer_rect_contains_document_point); see topmost_move_layer_at.
template <typename Contains>
const Layer* topmost_move_layer_at_recursive(const std::vector<Layer>& layers, QPoint document_point, bool skip_locked,
                                             const Contains& contains,
                                             LayerLockFlags ancestor_lock_flags = kLayerLockNone) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& layer = *it;
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    const auto effective_lock_flags = ancestor_lock_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found = topmost_move_layer_at_recursive(layer.children(), document_point, skip_locked, contains,
                                                              effective_lock_flags);
          found != nullptr) {
        return found;
      }
      continue;
    }
    if (skip_locked && (effective_lock_flags & kLayerLockPosition) != kLayerLockNone) {
      continue;
    }
    if (contains(layer, document_point)) {
      return &layer;
    }
  }
  return nullptr;
}

const Layer* topmost_text_layer_at_recursive(const std::vector<Layer>& layers, QPoint document_point) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& layer = *it;
    if (!layer.visible()) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      if (const auto* found = topmost_text_layer_at_recursive(layer.children(), document_point); found != nullptr) {
        return found;
      }
      continue;
    }
    if (layer_is_text(layer) && layer.bounds().contains(document_point.x(), document_point.y())) {
      return &layer;
    }
  }
  return nullptr;
}

bool expand_mask_to_include_rect(LayerMask& mask, QRect document_rect, QSize canvas_size) {
  document_rect = document_rect.normalized().intersected(QRect(QPoint(), canvas_size));
  if (document_rect.isEmpty()) {
    return false;
  }

  const auto current = QRect(mask.bounds.x, mask.bounds.y, mask.bounds.width, mask.bounds.height);
  if (!mask.pixels.empty() && current.contains(document_rect)) {
    return true;
  }

  const auto expanded = (mask.pixels.empty() ? document_rect : current.united(document_rect))
                            .intersected(QRect(QPoint(), canvas_size));
  if (expanded.isEmpty()) {
    return false;
  }

  PixelBuffer next(expanded.width(), expanded.height(), PixelFormat::gray8());
  next.clear(mask.default_color);
  if (!mask.pixels.empty() && mask.pixels.format() == PixelFormat::gray8()) {
    const auto copy_rect = current.intersected(expanded);
    for (int y = copy_rect.top(); y <= copy_rect.bottom(); ++y) {
      for (int x = copy_rect.left(); x <= copy_rect.right(); ++x) {
        *next.pixel(x - expanded.x(), y - expanded.y()) = *mask.pixels.pixel(x - current.x(), y - current.y());
      }
    }
  }

  mask.bounds = Rect{expanded.x(), expanded.y(), expanded.width(), expanded.height()};
  mask.pixels = std::move(next);
  return true;
}

#ifdef PATCHY_GPU_CANVAS
std::uint64_t webgpu_hash_combine(std::uint64_t hash, std::uint64_t value) {
  hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6U) + (hash >> 2U);
  return hash;
}

std::uint64_t webgpu_document_key(const CanvasGpuDocument& document) {
  std::uint64_t hash = UINT64_C(0xcbf29ce484222325);
  hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(document.document_size.width()));
  hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(document.document_size.height()));
  for (const auto& layer : document.layers) {
    hash = webgpu_hash_combine(hash, layer.id);
    hash = webgpu_hash_combine(hash, layer.revision);
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.blend_mode));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.opacity));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.mask_default));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.mask_density));
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.has_mask));
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.has_blend_if));
    for (const auto& ranges : layer.blend_if) {
      for (const auto& thresholds : {ranges.this_layer, ranges.underlying_layer}) {
        hash = webgpu_hash_combine(hash, thresholds.black_low);
        hash = webgpu_hash_combine(hash, thresholds.black_high);
        hash = webgpu_hash_combine(hash, thresholds.white_low);
        hash = webgpu_hash_combine(hash, thresholds.white_high);
      }
    }
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.image.width()));
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.image.height()));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.document_rect.left()));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.document_rect.top()));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.mask_document_rect.left()));
    hash = webgpu_hash_combine(hash, std::hash<double>{}(layer.mask_document_rect.top()));
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.mask_image.width()));
    hash = webgpu_hash_combine(hash, static_cast<std::uint64_t>(layer.mask_image.height()));
  }
  return hash;
}
#endif

}  // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);
  setMouseTracking(true);
  setTabletTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  const auto make_scroll_bar = [this](Qt::Orientation orientation, const QString& name) {
    auto* bar = new QScrollBar(orientation, this);
    bar->setObjectName(name);
    bar->setFocusPolicy(Qt::NoFocus);  // keep canvas keyboard handling unaffected
    bar->setCursor(Qt::ArrowCursor);   // children otherwise inherit the canvas tool cursor
    bar->hide();
    connect(bar, &QScrollBar::valueChanged, this,
            [this, orientation](int value) { handle_scroll_bar_value_changed(orientation, value); });
    return bar;
  };
  horizontal_scroll_bar_ = make_scroll_bar(Qt::Horizontal, QStringLiteral("canvasHorizontalScrollBar"));
  vertical_scroll_bar_ = make_scroll_bar(Qt::Vertical, QStringLiteral("canvasVerticalScrollBar"));
  // Watch the whole app for modifier-key changes so the selection cursor badge
  // updates the instant Shift/Alt change even when the canvas does not hold
  // keyboard focus (the key events otherwise go to whichever panel has focus).
  qApp->installEventFilter(this);
  selection_timer_.start(120, this);
  pen_proximity_clock_.start();
#ifdef PATCHY_GPU_CANVAS
  initialize_graphics_canvas();
#endif
}

CanvasWidget::~CanvasWidget() {
#ifdef PATCHY_GPU_CANVAS
  canvas_render_backend_ = CanvasRenderBackend::Cpu;
  graphics_surface_.reset();
#endif
}

CanvasWidget::CanvasRenderBackend CanvasWidget::canvas_render_backend() const noexcept {
  return canvas_render_backend_;
}

#ifdef PATCHY_GPU_CANVAS
void CanvasWidget::initialize_webgpu_compositor() {
  if (!WebGpuDocumentCompositor::should_try_automatically()) {
    return;
  }
  auto backend = std::make_unique<WebGpuRenderBackend>();
  if (!backend->initialize()) {
    const auto reason = QString::fromStdString(std::string(backend->last_error()));
    qInfo().noquote() << "Patchy WebGPU document compositor unavailable; using Qt RHI/CPU fallback:" << reason;
    return;
  }
  webgpu_compositor_ = std::move(backend);
}

void CanvasWidget::initialize_graphics_canvas() {
  if (graphics_surface_ != nullptr) {
    return;
  }
  initialize_webgpu_compositor();
  graphics_surface_ = CanvasGraphicsSurface::create(this);
  if (graphics_surface_ == nullptr) {
    disable_gpu_canvas(QStringLiteral("GPU presentation was disabled by configuration or build"));
    return;
  }
  canvas_render_backend_ = CanvasRenderBackend::Initializing;
  connect(graphics_surface_.get(), &CanvasGraphicsSurface::ready, this,
          [this](CanvasGraphicsApi api) { graphics_surface_ready(api); });
  connect(graphics_surface_.get(), &CanvasGraphicsSurface::failed, this,
          [this](const QString& reason) { graphics_surface_failed(reason); });
  graphics_surface_->set_overlay_painter([this](QPainter& painter, QRect exposed_rect) {
    paint_gpu_overlay(painter, exposed_rect);
  });
#ifdef PATCHY_VULKAN_QT_INTEROP_PROBE
  // Capture the Dawn observation on the GUI thread. The render-thread probe
  // must query only Qt's scene-graph resources and never touch the Dawn
  // compositor while its queue may be submitting work.
  const auto dawn_interop_observation = webgpu_compositor_ != nullptr
                                            ? webgpu_compositor_->vulkan_interop_observation()
                                            : DawnVulkanInteropObservation{};
  if (qEnvironmentVariableIsSet("PATCHY_VULKAN_QT_INTEROP_PROBE")) {
    graphics_surface_->set_render_thread_probe([this, dawn_interop_observation](QQuickWindow* window) {
      if (vulkan_qt_interop_probe_reported_.exchange(true, std::memory_order_acq_rel)) {
        return;
      }
      const auto report = probe_vulkan_qt_interop(window, dawn_interop_observation);
      QMetaObject::invokeMethod(this, [this, report] {
        qInfo().noquote() << "Patchy Vulkan/Qt RHI interop probe:" << report.summary;
        qInfo().noquote()
            << "Patchy zero-copy remains disabled:" << QString::fromStdString(report.decision.reason);
      }, Qt::QueuedConnection);
    });
  }
#endif
  graphics_surface_->setGeometry(rect());
  graphics_surface_->lower();
}

void CanvasWidget::show_graphics_canvas() {
  if (graphics_surface_ == nullptr) {
    return;
  }
  resize_graphics_canvas_surface();
  render_graphics_canvas_frame();
}

void CanvasWidget::resize_graphics_canvas_surface() {
  if (graphics_surface_ != nullptr && canvas_render_backend_ != CanvasRenderBackend::Cpu) {
    graphics_surface_->setGeometry(rect());
  }
}

void CanvasWidget::graphics_surface_ready(CanvasGraphicsApi api) {
  switch (api) {
  case CanvasGraphicsApi::OpenGL:
    canvas_render_backend_ = CanvasRenderBackend::OpenGL;
    break;
  case CanvasGraphicsApi::Vulkan:
    canvas_render_backend_ = CanvasRenderBackend::Vulkan;
    break;
  case CanvasGraphicsApi::Metal:
    canvas_render_backend_ = CanvasRenderBackend::Metal;
    break;
  case CanvasGraphicsApi::Direct3D11:
    canvas_render_backend_ = CanvasRenderBackend::Direct3D11;
    break;
  case CanvasGraphicsApi::Direct3D12:
    canvas_render_backend_ = CanvasRenderBackend::Direct3D12;
    break;
  case CanvasGraphicsApi::Unknown:
    disable_gpu_canvas(QStringLiteral("Qt Quick reported an unknown graphics API"));
    return;
  }
  show_graphics_canvas();
}

void CanvasWidget::graphics_surface_failed(const QString& reason) {
  disable_gpu_canvas(reason);
}

void CanvasWidget::render_graphics_canvas_frame() {
  if (graphics_surface_ == nullptr || canvas_render_backend_ == CanvasRenderBackend::Cpu) {
    return;
  }
  CanvasGpuDocument document;
  QString rejection_reason;
  if (!build_gpu_document(document, &rejection_reason)) {
    if (rejection_reason != last_gpu_fallback_reason_) {
      last_gpu_fallback_reason_ = rejection_reason;
      if (!rejection_reason.isEmpty()) {
        qInfo().noquote() << "Patchy GPU document compositor unavailable; using CPU compositor:" << rejection_reason;
      }
    }
    if (gpu_document_active_) {
      gpu_document_active_ = false;
      graphics_surface_->clear_gpu_document();
      graphics_surface_->hide();
      update();
    }
    return;
  }

  if (webgpu_compositor_ != nullptr) {
    if (webgpu_compositor_->state() != patchy::GpuBackendState::Ready &&
        !webgpu_compositor_->recover()) {
      const auto reason = QString::fromStdString(std::string(webgpu_compositor_->last_error()));
      if (!reason.isEmpty() && reason != last_gpu_fallback_reason_) {
        last_gpu_fallback_reason_ = reason;
        qInfo().noquote() << "Patchy WebGPU document compositor could not recover; using Qt RHI/CPU fallback:"
                          << reason;
      }
      webgpu_compositor_.reset();
    }
  }

  if (webgpu_compositor_ != nullptr) {
    const auto cache_key = webgpu_document_key(document);
    QString webgpu_reason;
    // The cached frame is only current when the snapshot key matches and no
    // document change has been recorded since it was published. A widget
    // repaint (expose, scroll, pan) never invalidates it by itself.
    if (cache_key == webgpu_frame_cache_key_ && !webgpu_frame_cache_.isNull() &&
        gpu_frame_invalidation_.empty()) {
      document.composited_frame = webgpu_frame_cache_;
      document.layers.clear();
    } else {
      QImage composited_frame;
      bool composed = false;
      const bool incremental = !webgpu_frame_cache_.isNull() && !gpu_frame_invalidation_.full() &&
                               !gpu_frame_invalidation_.empty() &&
                               webgpu_frame_cache_.size() == document.document_size;
      if (incremental) {
        // Rebuild exactly the tiles the document edits touched, including
        // tiles outside the current viewport, so panning afterwards presents
        // current pixels instead of a stale cached tile.
        composed = webgpu_compositor_->compose_incremental(document, gpu_frame_invalidation_.region(),
                                                           webgpu_frame_cache_, composited_frame, &webgpu_reason);
      } else {
        composed = webgpu_compositor_->compose(document, composited_frame, &webgpu_reason);
      }
      if (composed) {
        webgpu_frame_cache_ = std::move(composited_frame);
        webgpu_frame_cache_key_ = cache_key;
        gpu_frame_invalidation_.clear();
        document.composited_frame = webgpu_frame_cache_;
        document.layers.clear();
      }
    }
    if (!document.composited_frame.isNull()) {
      if (!webgpu_compositor_reported_) {
        webgpu_compositor_reported_ = true;
        qInfo().noquote() << "Patchy WebGPU document compositor active on"
                          << webgpu_compositor_->adapter_name() << "via"
                          << webgpu_compositor_->native_backend_name()
                          << "; render-graph passes:" << webgpu_compositor_->last_submitted_pass_count()
                          << "; tiles:" << webgpu_compositor_->last_rendered_tile_count()
                          << "; readback bytes:" << webgpu_compositor_->last_readback_bytes();
      }
    } else if (!webgpu_reason.isEmpty() && webgpu_reason != last_gpu_fallback_reason_) {
      qInfo().noquote() << "Patchy WebGPU document compositor failed; using Qt RHI/CPU document fallback:"
                        << webgpu_reason;
    }
  }

  gpu_document_active_ = true;
  last_gpu_fallback_reason_.clear();
  if (!graphics_surface_->isVisible()) {
    graphics_surface_->show();
    graphics_surface_->lower();
  }
  if (!graphics_surface_->set_gpu_document(std::move(document))) {
    disable_gpu_canvas(QStringLiteral("Qt Quick could not create the portable GPU compositor passes"));
  }
}

void CanvasWidget::request_graphics_canvas_update(const QRegion& region) {
  if (graphics_surface_ == nullptr || canvas_render_backend_ == CanvasRenderBackend::Cpu || !gpu_document_active_) {
    return;
  }
  render_graphics_canvas_frame();
  graphics_surface_->request_update(region);
}

bool CanvasWidget::build_gpu_document(CanvasGpuDocument& result, QString* rejection_reason) const {
  const auto reject = [rejection_reason](QString reason) {
    if (rejection_reason != nullptr) {
      *rejection_reason = std::move(reason);
    }
    return false;
  };
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    return reject(QStringLiteral("document has no renderable canvas"));
  }

  // The GPU path is deliberately all-or-nothing for a document. Falling back
  // here is safer than mixing a GPU approximation with CPU-rendered siblings.
  // The capability matrix covers the common raster stack in two all-or-nothing
  // tiers: source-over textures and a portable shader pass for separable blends
  // and simple raster masks. Unsupported Photoshop features stay on the CPU
  // compositor rather than being mixed with an approximate GPU sibling.
  if (tiling_preview_enabled_ || uses_deep_zoom_pixel_renderer(zoom_) || transforming_layer_ || warping_layer_ ||
      moving_layer_ || patch_tool_dragging_ || curves_clipping_mode_.has_value() || processing_operation_active() ||
      layer_edit_target_ != LayerEditTarget::Content) {
    return reject(QStringLiteral("an interactive preview or non-content channel is active"));
  }

  const auto capability = patchy::gpu_document_capability(*document_);
  if (!capability.supported()) {
    return reject(QString::fromStdString(capability.reason));
  }
#ifndef PATCHY_GPU_SHADER_COMPOSITOR
  if (capability.mode == patchy::GpuDocumentRenderMode::PixelStackShader) {
    return reject(QStringLiteral("GPU shader compositor is not available in this build"));
  }
#endif

  result.document_size = QSize(document_->width(), document_->height());
  result.canvas_backdrop = theme().canvas_backdrop;
  result.smooth_scaling = uses_smooth_display_scaling(zoom_, false);
  result.shader_composition = capability.mode == patchy::GpuDocumentRenderMode::PixelStackShader;
  const QRectF exact_target_rect(widget_position_f(QPointF(0.0, 0.0)),
                                 widget_position_f(QPointF(document_->width(), document_->height())));
  if (uses_pixel_aligned_view(zoom_)) {
    const auto top_left = widget_position(QPoint(0, 0));
    const auto bottom_right = widget_position(QPoint(document_->width(), document_->height()));
    result.canvas_rect = QRectF(QRect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y())));
  } else {
    result.canvas_rect = exact_target_rect;
  }

  result.layers.reserve(document_->layers().size());
  std::vector<std::uint64_t> snapshot_layer_ids;
  snapshot_layer_ids.reserve(document_->layers().size());
  for (const auto& layer : document_->layers()) {
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    snapshot_layer_ids.push_back(layer.id());
    CanvasGpuLayer gpu_layer;
    gpu_layer.id = layer.id();
    gpu_layer.revision = layer.render_revision();
    gpu_layer.pixel_revision = layer.pixel_revision();
    gpu_layer.content_revision = layer.content_revision();
    gpu_layer.mask_revision = layer.mask_revision();
    gpu_layer.image = gpu_layer_image_cache_.layer_image(layer.id(), layer.pixel_revision(), layer.pixels());
    const QRectF document_rect(layer.bounds().x, layer.bounds().y, layer.bounds().width, layer.bounds().height);
    gpu_layer.document_rect = document_rect;
    gpu_layer.rect = widget_rect_for_document_rect(document_rect);
    gpu_layer.opacity = static_cast<qreal>(std::clamp(layer.opacity() * layer.fill_opacity(), 0.0F, 1.0F));
    gpu_layer.blend_mode = static_cast<int>(layer.blend_mode());
    const auto blend_if_status = layer.blend_if_payload_status();
    if (blend_if_status == BlendIfPayloadStatus::Supported && !blend_if_is_identity(layer.blend_if())) {
      const auto settings = layer.blend_if();
      gpu_layer.has_blend_if = true;
      for (std::size_t index = 0; index < settings.channels.size(); ++index) {
        const auto copy_thresholds = [](const BlendIfThresholds& thresholds) {
          return CanvasGpuBlendIfThresholds{thresholds.black_low, thresholds.black_high, thresholds.white_low,
                                            thresholds.white_high};
        };
        gpu_layer.blend_if[index] = CanvasGpuBlendIfRanges{copy_thresholds(settings.channels[index].this_layer),
                                                           copy_thresholds(settings.channels[index].underlying_layer)};
      }
    }
    if (layer.mask().has_value() && !layer.mask()->disabled) {
      const auto& mask = *layer.mask();
      gpu_layer.has_mask = true;
      if (!mask.pixels.empty()) {
        // Grayscale8 is the format every GPU tier samples from the red
        // channel; see grayscale_qimage_from_pixel_buffer for why Alpha8 is
        // not an option here.
        gpu_layer.mask_image = gpu_layer_image_cache_.mask_image(layer.id(), layer.mask_revision(), mask.pixels);
        gpu_layer.mask_document_rect =
            QRectF(mask.bounds.x, mask.bounds.y, mask.bounds.width, mask.bounds.height);
        gpu_layer.mask_rect = widget_rect_for_document_rect(gpu_layer.mask_document_rect);
      }
      gpu_layer.mask_default = static_cast<qreal>(mask.default_color) / 255.0;
      gpu_layer.mask_density = static_cast<qreal>(mask.density) / 255.0;
    }
    result.layers.push_back(std::move(gpu_layer));
  }
  gpu_layer_image_cache_.retain_only(snapshot_layer_ids);
  return true;
}

void CanvasWidget::paint_gpu_overlay(QPainter& painter, QRect exposed_rect) {
  if (document_ == nullptr || document_->width() <= 0 || document_->height() <= 0) {
    return;
  }
  const QRectF exact_target_rect(widget_position_f(QPointF(0.0, 0.0)),
                                 widget_position_f(QPointF(document_->width(), document_->height())));
  const bool pixel_aligned_view = uses_pixel_aligned_view(zoom_);
  QRect pixel_aligned_target_rect;
  if (pixel_aligned_view) {
    const auto top_left = widget_position(QPoint(0, 0));
    const auto bottom_right = widget_position(QPoint(document_->width(), document_->height()));
    pixel_aligned_target_rect =
        QRect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
  }
  const QRectF target_rect = pixel_aligned_view ? QRectF(pixel_aligned_target_rect) : exact_target_rect;

  if (!curves_clipping_mode_.has_value()) {
    draw_mask_display_overlay(painter, target_rect, pixel_aligned_view, pixel_aligned_target_rect);
  }
  draw_grid_overlay(painter, target_rect, exposed_rect);
  draw_guides_overlay(painter);
  painter.setPen(theme().canvas_document_border);
  const auto border_rect = target_rect.adjusted(0.5, 0.5, -0.5, -0.5);
  if (!border_rect.isEmpty()) {
    painter.drawRect(border_rect);
  }
  draw_selection_overlay(painter);
  draw_patch_tool_drag_outline(painter);
  draw_quick_select_stroke_overlay(painter);
  draw_spot_heal_stroke_overlay(painter);
  draw_pen_overlay(painter);
  draw_path_edit_overlay(painter);
  draw_shape_preview(painter, exposed_rect);
  draw_crop_overlay(painter);
  draw_move_layer_selection(painter);
  draw_drag_size_readout(painter);
  draw_text_rect_preview(painter);
  draw_zoom_preview(painter);
  draw_rulers(painter);
  draw_brush_hover_outline(painter);
  draw_stroke_leash_overlay(painter);
  draw_brush_adjust_overlay(painter);
  draw_processing_overlay(painter);
}

void CanvasWidget::disable_gpu_canvas(const QString& reason) {
  if (canvas_render_backend_ == CanvasRenderBackend::Cpu && graphics_surface_ == nullptr) {
    return;
  }
  canvas_render_backend_ = CanvasRenderBackend::Cpu;
  gpu_document_active_ = false;
  graphics_surface_.reset();
  if (!reason.isEmpty()) {
    qInfo().noquote() << "Patchy GPU presentation unavailable; using CPU canvas:" << reason;
  }
  update();
}
#endif

void CanvasWidget::set_document(Document* document) {
  set_document_internal(document, /*preserve_frame_for_same_size=*/false);
}

bool CanvasWidget::pointer_gesture_active() const noexcept {
  return move_layer_selection_gesture_.has_value() || painting_ || drawing_shape_ || dragging_text_rect_ ||
         move_drag_pending_ ||
         moving_layer_ || dragging_transform_ || dragging_warp_handle_ || selecting_ ||
         lassoing_ || quick_selecting_ || spot_healing_stroke_active_ || patch_tool_dragging_ ||
         moving_selection_ || marquee_resize_handle_ != TransformHandle::None || dragging_guide_ ||
         crop_dragging_out_ || crop_rotating_ ||
         crop_drag_handle_ != TransformHandle::None || pen_handle_dragging_ ||
         pen_session_drag_anchor_ >= 0 || path_drag_mode_ != PathEditDrag::None ||
         path_transform_drag_handle_ != TransformHandle::None;
}

void CanvasWidget::set_tiling_preview_enabled(bool enabled) {
  if (tiling_preview_enabled_ == enabled) {
    return;
  }
  tiling_preview_enabled_ = enabled;
  tiling_tile_pixmap_ = QPixmap();
  tiling_tile_pixmap_size_ = QSize();
  update();
}

bool CanvasWidget::tiling_preview_enabled() const noexcept {
  return tiling_preview_enabled_;
}

void CanvasWidget::set_document_for_history_restore(Document* document, bool normal_composite_unchanged) {
  set_document_internal(document, /*preserve_frame_for_same_size=*/true, normal_composite_unchanged);
}

void CanvasWidget::set_document_internal(Document* document, bool preserve_frame_for_same_size,
                                         bool normal_composite_unchanged) {
  close_canvas_context_menu();
  invalidate_vector_preview();
  cancel_pointer_gestures();
  painting_ = false;
  clear_brush_stroke_tracking();
  reset_brush_smoothing();
  reset_axis_constrained_stroke();
  deferred_wait_release_.reset();
  cancel_pen_path();  // an in-flight path belongs to the outgoing document
  cancel_path_transform();
  clear_preview_scaled_document();
  preview_scale_cache_.reset();
  clear_transform_commit_hold();  // a held commit frame belongs to the outgoing state
  cancel_move_commit_job();
  active_document_path_.reset();
  path_selected_anchors_.clear();
  extra_selected_anchors_.clear();
  clear_transient_read_interaction();
  curves_clipping_mode_.reset();
  curves_clipping_channel_.reset();
  curves_clipping_preview_image_ = QImage();
  curves_clipping_display_mip_cache_.clear();
  curves_clipping_display_mip_source_key_ = 0;
  const auto old_transform_controls_rect = move_transform_controls_rect();
  const bool render_cache_was_dirty = render_cache_dirty_;
  const bool preserve_frame = preserve_frame_for_same_size && document != nullptr && !render_cache_.isNull() &&
                              render_cache_.size() == QSize(document->width(), document->height());
  const bool preserve_quick_mask =
      preserve_frame_for_same_size && quick_mask_active_ && document != nullptr &&
      document_ != nullptr && document_->width() == document->width() &&
      document_->height() == document->height();
  const bool quick_mask_was_cleared = quick_mask_active_ && !preserve_quick_mask;
  if (quick_mask_was_cleared) {
    quick_mask_active_ = false;
    quick_mask_pixels_ = {};
    primary_color_ = quick_mask_saved_primary_;
    secondary_color_ = quick_mask_saved_secondary_;
    quick_mask_edit_before_.reset();
    quick_mask_edit_label_.clear();
    quick_mask_edit_dirty_ = QRegion();
  }
  // A Smart Filter mask edit buffer is tied to a layer instance in the old
  // document. History restoration installs a different Document object, so the
  // host must explicitly resync the target after it has resolved that owner.
  smart_filter_mask_pixels_ = {};
  smart_filter_mask_owner_id_ = 0;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  const auto restore_channel_id =
      preserve_frame_for_same_size && layer_edit_target_ == LayerEditTarget::DocumentChannel
          ? active_document_channel_id_
          : ChannelId{0};
  const auto restore_component_target =
      preserve_frame_for_same_size &&
              (layer_edit_target_ == LayerEditTarget::ComponentRed ||
               layer_edit_target_ == LayerEditTarget::ComponentGreen ||
               layer_edit_target_ == LayerEditTarget::ComponentBlue)
          ? layer_edit_target_
          : LayerEditTarget::Content;
  const auto restore_channel_display_mode = mask_display_mode_;
  cancel_free_transform();
  if (warping_layer_) {
    reset_warp_state();
  }
  cancel_move_layer_selection();
  move_drag_pending_ = false;
  moving_layer_ = false;
  moving_layers_.clear();
  move_readout_base_rect_.reset();
  drag_readout_dirty_rect_ = QRect();
  move_preview_delta_ = QPoint();
  move_preview_patches_.clear();
  move_preview_patches_delta_.reset();
  moving_layers_use_outline_preview_ = false;
  clear_retained_move_caches();
  reset_move_live_latch();
#ifdef PATCHY_GPU_CANVAS
  webgpu_frame_cache_ = QImage();
  webgpu_frame_cache_key_ = 0;
  gpu_frame_invalidation_.mark_full();
  gpu_layer_image_cache_.clear();
#endif
  document_ = document;
  set_move_transform_controls_layer(std::nullopt);
  selected_guide_index_ = -1;
  dragging_guide_ = false;
  creating_guide_ = false;
  guide_drag_remove_ = false;
  layer_edit_target_ = LayerEditTarget::Content;
  active_document_channel_id_ = 0;
  mask_display_mode_ = MaskDisplayMode::None;
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  if (restore_channel_id != 0 && document_ != nullptr &&
      static_cast<const Document*>(document_)->find_channel(restore_channel_id) != nullptr) {
    layer_edit_target_ = LayerEditTarget::DocumentChannel;
    active_document_channel_id_ = restore_channel_id;
    mask_display_mode_ = restore_channel_display_mode == MaskDisplayMode::None
                             ? MaskDisplayMode::Grayscale
                             : restore_channel_display_mode;
  } else if (restore_component_target != LayerEditTarget::Content) {
    layer_edit_target_ = restore_component_target;
    mask_display_mode_ = MaskDisplayMode::Grayscale;
  }
  if (!preserve_frame) {
    render_cache_ = QImage();
    render_cache_diagnostics_ = {};
  }
  const bool keep_normal_composite_cache = preserve_frame && normal_composite_unchanged;
  render_cache_dirty_ = keep_normal_composite_cache ? render_cache_was_dirty : true;
  if (!preserve_frame && document_ != nullptr && document_->metadata().psd_flat_composite.has_value()) {
    const auto& flat_composite = *document_->metadata().psd_flat_composite;
    // RGB-only PSD compatibility composites cannot preserve transparency, so
    // using them as a cache seed hides checkerboards until the next refresh.
    if (flat_composite.format().channels >= 4 && flat_composite.width() == document_->width() &&
        flat_composite.height() == document_->height()) {
      render_cache_ = qimage_from_flat_composite_pixels(flat_composite);
      quantize_image_for_palette_display(render_cache_);
      render_cache_dirty_ = render_cache_.isNull();
    }
    document_->metadata().psd_flat_composite.reset();
  }
  if (!keep_normal_composite_cache) {
    invalidate_display_mip_cache();
  }
  clear_move_hover_outline();
  update_move_transform_controls_dirty(old_transform_controls_rect);
  smudge_state_ = {};
  mixer_brush_state_ = {};
  cancel_quick_select_stroke();
  cancel_spot_heal_stroke();
  cancel_patch_tool_drag();
  cancel_crop_session();
  reset_axis_constrained_stroke();
  last_stroke_end_document_.reset();
  if (brush_adjust_dragging_) {
    end_brush_adjust_drag(false);
  }
  if (isVisible()) {
    constrain_pan();
  }
  sync_scroll_bars();  // this path constrains pan without notify_view_changed()
  update();
  if (quick_mask_was_cleared && quick_mask_changed_callback_) {
    quick_mask_changed_callback_();
  }
}

void CanvasWidget::set_tool(CanvasTool tool) {
  const auto tool_changed = tool_ != tool;
  const auto old_transform_controls_rect = move_transform_controls_rect();
  if (tool_changed) {
    close_canvas_context_menu();
    if (pen_session_active_) {
      // Switching away commits the open path (Photoshop keeps the work): the
      // callback routes it to a shape layer or the work path.
      commit_pen_path(false);
    }
    pen_temp_direct_select_ = false;
    pen_session_drag_anchor_ = -1;
    path_hover_hint_action_ = PenHoverAction::Draw;
    path_hover_hint_target_ = PathHoverTarget::None;
    cancel_magnetic_lasso();
    cancel_spot_heal_stroke();
    cancel_patch_tool_drag();
    // A pending crop cancels on tool switch (never commits): an accidental
    // switch must not resize the document.
    cancel_crop_session();
    commit_path_transform();  // tool switches commit, like the pen session
    finish_free_transform();
    finish_warp_transform();
    cancel_move_layer_selection();
    move_drag_pending_ = false;
    moving_layer_ = false;
    moving_layers_.clear();
    move_readout_base_rect_.reset();
    drag_readout_dirty_rect_ = QRect();
    clear_move_snap_guides();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    clear_retained_move_caches();
    reset_move_live_latch();
    set_move_transform_controls_layer(std::nullopt);
    clear_move_hover_outline();
    mixer_brush_state_ = {};
  }
  tool_ = tool;
  // Each selection tool keeps its own combine mode; surface this tool's stored
  // mode so the cursor badge and Options bar follow when switching tools.
  if (const auto index = selection_tool_index(tool_); index >= 0) {
    selection_mode_ = selection_modes_per_tool_[static_cast<std::size_t>(index)];
  }
  update_tool_cursor();
  if (tool_changed) {
    update_move_transform_controls_dirty(old_transform_controls_rect);
    update();
    notify_transform_controls_changed();
    notify_selection_mode_changed();
  }
}

CanvasTool CanvasWidget::tool() const noexcept {
  return tool_;
}

void CanvasWidget::set_edit_locked(bool locked) noexcept {
  if (edit_locked_ == locked) {
    return;
  }
  edit_locked_ = locked;
  if (edit_locked_) {
    close_canvas_context_menu();
    clear_move_hover_outline();
    cancel_move_layer_selection();
    move_drag_pending_ = false;
    moving_layer_ = false;
    moving_layers_.clear();
    move_readout_base_rect_.reset();
    drag_readout_dirty_rect_ = QRect();
    clear_move_snap_guides();
    move_preview_delta_ = QPoint();
    move_preview_patches_.clear();
    move_preview_patches_delta_.reset();
    moving_layers_use_outline_preview_ = false;
    clear_retained_move_caches();
    reset_move_live_latch();
    dragging_text_rect_ = false;
    selecting_ = false;
    lassoing_ = false;
    cancel_magnetic_lasso();
    cancel_quick_select_stroke();
    cancel_spot_heal_stroke();
    cancel_patch_tool_drag();
    cancel_crop_session();
    moving_selection_ = false;
    drawing_shape_ = false;
    dragging_guide_ = false;
    creating_guide_ = false;
    guide_drag_remove_ = false;
    reset_axis_constrained_stroke();
  }
  update_tool_cursor();
  update();
}

bool CanvasWidget::edit_locked() const noexcept {
  return edit_locked_;
}

void CanvasWidget::set_layer_edit_target(LayerEditTarget target) noexcept {
  if (layer_edit_target_ == target) {
    return;
  }
  cancel_path_transform();
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask &&
      target != LayerEditTarget::SmartFilterMask) {
    // Generic layer/channel switching is an exit path. Pending mask pixels are
    // temporary and must not leak onto a subsequently selected layer.
    smart_filter_mask_pixels_ = {};
    smart_filter_mask_owner_id_ = 0;
    smart_filter_mask_edit_before_.reset();
    smart_filter_mask_edit_label_.clear();
    smart_filter_mask_edit_dirty_ = QRegion();
    ++smart_filter_mask_revision_;
    mask_display_mode_ = MaskDisplayMode::None;
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
  }
  layer_edit_target_ = target;
  if (target != LayerEditTarget::DocumentChannel) {
    active_document_channel_id_ = 0;
  }
  if (target == LayerEditTarget::ComponentRed || target == LayerEditTarget::ComponentGreen ||
      target == LayerEditTarget::ComponentBlue) {
    mask_display_mode_ = MaskDisplayMode::Grayscale;
    mask_display_image_ = QImage();
    mask_display_image_layer_ = 0;
    mask_display_image_channel_ = 0;
    mask_display_image_revision_ = 0;
  }
  clear_brush_stroke_tracking();
  update_tool_cursor();
  update();
}

CanvasWidget::LayerEditTarget CanvasWidget::layer_edit_target() const noexcept {
  return layer_edit_target_;
}

void CanvasWidget::set_document_channel_edit_target(ChannelId id, MaskDisplayMode mode) {
  const auto* channel = document_ != nullptr ? static_cast<const Document*>(document_)->find_channel(id) : nullptr;
  if (channel == nullptr) {
    set_layer_edit_target(LayerEditTarget::Content);
    set_mask_display_mode(MaskDisplayMode::None);
    return;
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    clear_smart_filter_mask_edit_target();
  }
  layer_edit_target_ = LayerEditTarget::DocumentChannel;
  active_document_channel_id_ = id;
  mask_display_mode_ = mode == MaskDisplayMode::None ? MaskDisplayMode::Grayscale : mode;
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  clear_brush_stroke_tracking();
  update_tool_cursor();
  update();
}

void CanvasWidget::set_component_channel_preview(LayerEditTarget component) {
  if (component != LayerEditTarget::ComponentRed && component != LayerEditTarget::ComponentGreen &&
      component != LayerEditTarget::ComponentBlue) {
    set_layer_edit_target(LayerEditTarget::Content);
    set_mask_display_mode(MaskDisplayMode::None);
    return;
  }
  set_layer_edit_target(component);
}

std::optional<ChannelId> CanvasWidget::active_document_channel_id() const noexcept {
  if (layer_edit_target_ != LayerEditTarget::DocumentChannel || active_document_channel_id_ == 0) {
    return std::nullopt;
  }
  return active_document_channel_id_;
}

bool CanvasWidget::editing_document_channel() const noexcept {
  return layer_edit_target_ == LayerEditTarget::DocumentChannel && active_document_channel_const() != nullptr;
}

bool CanvasWidget::document_channel_is_editable() const noexcept {
  const auto* channel = active_document_channel_const();
  return channel != nullptr && channel->kind() == DocumentChannelKind::Alpha;
}

void CanvasWidget::set_mask_display_mode(MaskDisplayMode mode) {
  if (mask_display_mode_ == mode) {
    return;
  }
  mask_display_mode_ = mode;
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  update();
}

CanvasWidget::MaskDisplayMode CanvasWidget::mask_display_mode() const noexcept {
  return mask_display_mode_;
}

void CanvasWidget::invalidate_mask_display() {
  mask_display_image_ = QImage();
  mask_display_image_layer_ = 0;
  mask_display_image_channel_ = 0;
  mask_display_image_revision_ = 0;
  if (mask_display_mode_ != MaskDisplayMode::None) {
    update();
  }
}

void CanvasWidget::set_auto_select_layer(bool enabled) noexcept {
  auto_select_layer_ = enabled;
  clear_move_hover_outline();
}

bool CanvasWidget::auto_select_layer() const noexcept {
  return auto_select_layer_;
}

void CanvasWidget::set_primary_color(QColor color) {
  if (color.alpha() == 0) {
    color.setAlpha(255);
  }
  if (const auto* snap = editing_grayscale_target() ? nullptr : palette_snap_context();
      snap != nullptr) {
    const auto snapped = snap->lut->snap(static_cast<std::uint8_t>(color.red()),
                                         static_cast<std::uint8_t>(color.green()),
                                         static_cast<std::uint8_t>(color.blue()));
    color = QColor(snapped.red, snapped.green, snapped.blue, color.alpha());
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
  if (const auto* snap = editing_grayscale_target() ? nullptr : palette_snap_context();
      snap != nullptr) {
    const auto snapped = snap->lut->snap(static_cast<std::uint8_t>(color.red()),
                                         static_cast<std::uint8_t>(color.green()),
                                         static_cast<std::uint8_t>(color.blue()));
    color = QColor(snapped.red, snapped.green, snapped.blue, color.alpha());
  }
  secondary_color_ = color;
}

QColor CanvasWidget::secondary_color() const noexcept {
  return secondary_color_;
}

void CanvasWidget::set_gradient_method(GradientMethod method) noexcept {
  gradient_method_ = method;
}

GradientMethod CanvasWidget::gradient_method() const noexcept {
  return gradient_method_;
}

void CanvasWidget::set_gradient_reverse(bool reverse) noexcept {
  gradient_reverse_ = reverse;
}

bool CanvasWidget::gradient_reverse() const noexcept {
  return gradient_reverse_;
}

void CanvasWidget::set_gradient_opacity(int opacity) noexcept {
  gradient_opacity_ = std::clamp(opacity, 0, 100);
}

int CanvasWidget::gradient_opacity() const noexcept {
  return gradient_opacity_;
}

void CanvasWidget::set_gradient_stops(std::optional<std::vector<GradientStop>> stops) {
  if (stops.has_value()) {
    auto normalized = normalized_gradient_stops(*stops);
    if (normalized.size() < 2U) {
      normalized = effective_gradient_stops();
    }
    gradient_stops_ = std::move(normalized);
  } else {
    gradient_stops_.reset();
  }
}

const std::optional<std::vector<GradientStop>>& CanvasWidget::gradient_stops() const noexcept {
  return gradient_stops_;
}

std::vector<GradientStop> CanvasWidget::effective_gradient_stops() const {
  if (gradient_stops_.has_value() && gradient_stops_->size() >= 2U) {
    return normalized_gradient_stops(*gradient_stops_);
  }
  auto primary = edit_color(primary_color_);
  primary.a = 255;
  auto secondary = edit_color(secondary_color_);
  secondary.a = 255;
  return normalized_gradient_stops({GradientStop{0.0F, primary}, GradientStop{1.0F, secondary}});
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

void CanvasWidget::set_pattern_stamp_pattern(std::optional<PatternResource> pattern) {
  if (pattern.has_value() &&
      (pattern->tile.empty() || pattern->tile.format() != PixelFormat::rgba8())) {
    pattern.reset();
  }
  const auto old_id = pattern_stamp_pattern_.has_value() ? pattern_stamp_pattern_->id : std::string{};
  const auto new_id = pattern.has_value() ? pattern->id : std::string{};
  pattern_stamp_pattern_ = std::move(pattern);
  if (old_id != new_id) {
    pattern_stamp_origin_.reset();
  }
}

const std::optional<PatternResource>& CanvasWidget::pattern_stamp_pattern() const noexcept {
  return pattern_stamp_pattern_;
}

void CanvasWidget::set_pattern_stamp_aligned(bool aligned) noexcept {
  if (pattern_stamp_aligned_ == aligned) {
    return;
  }
  pattern_stamp_aligned_ = aligned;
  pattern_stamp_origin_.reset();
}

bool CanvasWidget::pattern_stamp_aligned() const noexcept {
  return pattern_stamp_aligned_;
}

void CanvasWidget::set_healing_diffusion(int diffusion) noexcept {
  healing_diffusion_ = std::clamp(diffusion, 1, 7);
}

int CanvasWidget::healing_diffusion() const noexcept {
  return healing_diffusion_;
}

void CanvasWidget::set_retouch_sample_all_layers(bool enabled) noexcept {
  retouch_sample_all_layers_ = enabled;
}

bool CanvasWidget::retouch_sample_all_layers() const noexcept {
  return retouch_sample_all_layers_;
}

QImage CanvasWidget::retouch_source_snapshot() {
  if (document_ == nullptr) {
    return QImage();
  }
  if (retouch_sample_all_layers_) {
    return render_document_image_with_processing();
  }
  const auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return QImage();
  }
  return active_layer_sample_image(*layer, QSize(document_->width(), document_->height()));
}

void CanvasWidget::set_local_adjustment_strength(int strength) noexcept {
  local_adjustment_strength_ = std::clamp(strength, 1, 100);
}

int CanvasWidget::local_adjustment_strength() const noexcept {
  return local_adjustment_strength_;
}

void CanvasWidget::set_local_tone_range(LocalToneRange range) noexcept {
  local_tone_range_ = range;
}

CanvasWidget::LocalToneRange CanvasWidget::local_tone_range() const noexcept {
  return local_tone_range_;
}

void CanvasWidget::set_local_protect_tones(bool enabled) noexcept {
  local_protect_tones_ = enabled;
}

bool CanvasWidget::local_protect_tones() const noexcept {
  return local_protect_tones_;
}

void CanvasWidget::set_sponge_mode(SpongeMode mode) noexcept {
  sponge_mode_ = mode;
}

CanvasWidget::SpongeMode CanvasWidget::sponge_mode() const noexcept {
  return sponge_mode_;
}

void CanvasWidget::set_sponge_vibrance(bool enabled) noexcept {
  sponge_vibrance_ = enabled;
}

bool CanvasWidget::sponge_vibrance() const noexcept {
  return sponge_vibrance_;
}

void CanvasWidget::set_show_transform_controls(bool enabled) noexcept {
  const auto old_transform_controls_rect = move_transform_controls_rect();
  show_transform_controls_ = enabled;
  if (!enabled) {
    cancel_free_transform();
    set_move_transform_controls_layer(std::nullopt);
  }
  update_move_transform_controls_dirty(old_transform_controls_rect);
  clear_move_hover_outline();
  notify_transform_controls_changed();
}

bool CanvasWidget::show_transform_controls() const noexcept {
  return show_transform_controls_;
}

std::optional<QRect> CanvasWidget::active_layer_document_rect() const noexcept {
  const auto* layer = active_pixel_layer();
  if (layer == nullptr) {
    return std::nullopt;
  }
  return to_qrect(layer->bounds());
}

bool CanvasWidget::smart_filter_mask_pixels_are_valid(const PixelBuffer& pixels) const noexcept {
  return document_ != nullptr && pixels.format() == PixelFormat::gray8() &&
         pixels.width() == document_->width() && pixels.height() == document_->height();
}

bool CanvasWidget::set_smart_filter_mask_edit_target(LayerId owner_id, PixelBuffer pixels,
                                                     MaskDisplayMode mode) {
  const auto* owner = document_ != nullptr && owner_id != 0
                          ? static_cast<const Document*>(document_)->find_layer(owner_id)
                          : nullptr;
  if (quick_mask_active_ || owner == nullptr || !smart_filter_mask_pixels_are_valid(pixels)) {
    return false;
  }

  cancel_smart_filter_mask_edit();
  smart_filter_mask_owner_id_ = owner_id;
  smart_filter_mask_pixels_ = std::move(pixels);
  ++smart_filter_mask_revision_;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  layer_edit_target_ = LayerEditTarget::SmartFilterMask;
  active_document_channel_id_ = 0;
  mask_display_mode_ = mode;
  invalidate_mask_display();
  clear_brush_stroke_tracking();
  update_tool_cursor();
  update();
  return true;
}

bool CanvasWidget::resync_smart_filter_mask_edit_target(LayerId owner_id, PixelBuffer pixels) {
  const auto mode = layer_edit_target_ == LayerEditTarget::SmartFilterMask
                        ? mask_display_mode_
                        : MaskDisplayMode::Overlay;
  return set_smart_filter_mask_edit_target(owner_id, std::move(pixels), mode);
}

void CanvasWidget::clear_smart_filter_mask_edit_target() {
  if (layer_edit_target_ != LayerEditTarget::SmartFilterMask &&
      smart_filter_mask_owner_id_ == 0 && smart_filter_mask_pixels_.empty()) {
    return;
  }

  cancel_smart_filter_mask_edit();
  if (painting_) {
    painting_ = false;
    reset_brush_smoothing();
    reset_axis_constrained_stroke();
  }
  if (drawing_shape_) {
    drawing_shape_ = false;
  }
  clear_brush_stroke_tracking();
  smart_filter_mask_pixels_ = {};
  smart_filter_mask_owner_id_ = 0;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  ++smart_filter_mask_revision_;
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    layer_edit_target_ = LayerEditTarget::Content;
  }
  mask_display_mode_ = MaskDisplayMode::None;
  invalidate_mask_display();
  update_tool_cursor();
  update();
}

bool CanvasWidget::editing_smart_filter_mask() const noexcept {
  if (document_ == nullptr || layer_edit_target_ != LayerEditTarget::SmartFilterMask ||
      smart_filter_mask_owner_id_ == 0 || !smart_filter_mask_pixels_are_valid(smart_filter_mask_pixels_)) {
    return false;
  }
  return static_cast<const Document*>(document_)->find_layer(smart_filter_mask_owner_id_) != nullptr;
}

std::optional<LayerId> CanvasWidget::smart_filter_mask_owner_id() const noexcept {
  return editing_smart_filter_mask() ? std::optional<LayerId>{smart_filter_mask_owner_id_}
                                     : std::nullopt;
}

const PixelBuffer& CanvasWidget::smart_filter_mask_pixels() const noexcept {
  return smart_filter_mask_pixels_;
}

std::uint64_t CanvasWidget::smart_filter_mask_revision() const noexcept {
  return smart_filter_mask_revision_;
}

QRect CanvasWidget::fill_smart_filter_mask(QColor color, QString history_label) {
  if (!editing_smart_filter_mask() || !begin_edit(std::move(history_label))) {
    return {};
  }
  const auto dirty = fill_active_layer_mask(color);
  active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::Immediate);
  return dirty;
}

QRect CanvasWidget::invert_smart_filter_mask(QString history_label) {
  if (!editing_smart_filter_mask() || !begin_edit(std::move(history_label))) {
    return {};
  }
  auto bytes = smart_filter_mask_pixels_.data();
  for (auto& value : bytes) {
    value = static_cast<std::uint8_t>(255U - value);
  }
  const auto dirty = QRect(0, 0, document_->width(), document_->height());
  active_edit_target_changed_impl(QRegion(dirty), DocumentChangeReason::Immediate);
  return dirty;
}

void CanvasWidget::finish_smart_filter_mask_edit() {
  if (!smart_filter_mask_edit_before_.has_value()) {
    return;
  }
  auto before = std::move(*smart_filter_mask_edit_before_);
  const auto owner_id = smart_filter_mask_owner_id_;
  auto label = std::move(smart_filter_mask_edit_label_);
  auto pixels = smart_filter_mask_pixels_;
  auto dirty = smart_filter_mask_edit_dirty_;
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();

  const auto* owner = document_ != nullptr && owner_id != 0
                          ? static_cast<const Document*>(document_)->find_layer(owner_id)
                          : nullptr;
  bool committed = dirty.isEmpty();
  if (owner != nullptr && !dirty.isEmpty() && smart_filter_mask_committed_callback_) {
    committed = smart_filter_mask_committed_callback_(owner_id, std::move(label), std::move(pixels),
                                                       std::move(dirty));
  }
  if (!committed && layer_edit_target_ == LayerEditTarget::SmartFilterMask &&
      smart_filter_mask_owner_id_ == owner_id) {
    // Native FEid regeneration is fail-closed. Keep the temporary canvas in
    // lockstep with the last accepted model state when the host rejects a
    // gesture instead of leaving an uncommitted mask visible.
    smart_filter_mask_pixels_ = std::move(before);
    ++smart_filter_mask_revision_;
    invalidate_mask_display();
    update();
  }
}

void CanvasWidget::cancel_smart_filter_mask_edit() {
  if (!smart_filter_mask_edit_before_.has_value()) {
    return;
  }
  smart_filter_mask_pixels_ = std::move(*smart_filter_mask_edit_before_);
  smart_filter_mask_edit_before_.reset();
  smart_filter_mask_edit_label_.clear();
  smart_filter_mask_edit_dirty_ = QRegion();
  ++smart_filter_mask_revision_;
  invalidate_mask_display();
  update();
}

void CanvasWidget::set_before_edit_callback(std::function<void(QString)> callback) {
  before_edit_callback_ = std::move(callback);
}

void CanvasWidget::set_selection_history_callback(std::function<void(QString, SelectionSnapshot, bool)> callback) {
  selection_history_callback_ = std::move(callback);
}

void CanvasWidget::set_quick_mask_changed_callback(
    std::function<void()> callback) {
  quick_mask_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_smart_filter_mask_committed_callback(
    std::function<bool(LayerId, QString, PixelBuffer, QRegion)> callback) {
  smart_filter_mask_committed_callback_ = std::move(callback);
}

void CanvasWidget::set_selection_mode_changed_callback(std::function<void(SelectionMode)> callback) {
  selection_mode_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_color_picked_callback(std::function<void(QColor)> callback) {
  color_picked_callback_ = std::move(callback);
}

void CanvasWidget::set_transient_read_interaction(
    std::function<void(const CanvasReadGesture&)> callback, QCursor cursor) {
  clear_transient_read_interaction();
  transient_read_callback_ = std::move(callback);
  transient_read_cursor_ = std::move(cursor);
  update_tool_cursor();
}

void CanvasWidget::clear_transient_read_interaction() {
  auto callback = transient_read_callback_;
  const auto was_dragging = transient_read_dragging_;
  transient_read_dragging_ = false;
  transient_read_callback_ = {};
  if (QWidget::mouseGrabber() == this) {
    releaseMouse();
  }
  if (was_dragging && callback) {
    callback(CanvasReadGesture{document_position(last_mouse_position_), mapToGlobal(last_mouse_position_),
                               Qt::NoModifier, CanvasReadPhase::Cancel});
  }
  update_tool_cursor();
}

bool CanvasWidget::has_transient_read_interaction() const noexcept {
  return static_cast<bool>(transient_read_callback_);
}

void CanvasWidget::set_curves_clipping_preview(std::optional<CurvesClippingMode> mode,
                                               std::optional<CurvesChannel> channel) {
  curves_clipping_mode_ = mode;
  curves_clipping_channel_ = mode.has_value() ? channel : std::nullopt;
  if (mode.has_value()) {
    wait_for_move_commit_job();  // the clipping preview scans exact composite pixels
    const bool cache_needs_refresh =
        document_ != nullptr &&
        (render_cache_dirty_ || render_cache_.size() != QSize(document_->width(), document_->height()));
    if (cache_needs_refresh) {
      // ensure_render_cache refreshes the clipping image after replacing the
      // source cache, so do not scan it a second time here.
      ensure_render_cache();
    } else {
      refresh_curves_clipping_preview();
    }
  } else {
    refresh_curves_clipping_preview();
  }
  update();
}

std::optional<CurvesClippingMode> CanvasWidget::curves_clipping_preview_mode() const noexcept {
  return curves_clipping_mode_;
}

void CanvasWidget::set_brush_settings_changed_callback(std::function<void()> callback) {
  brush_settings_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_pen_button_action_callback(std::function<void(PenButtonAction)> callback) {
  pen_button_action_callback_ = std::move(callback);
}

void CanvasWidget::set_text_requested_callback(std::function<void(QPoint, QRect)> callback) {
  text_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_active_layer_changed_callback(std::function<void(LayerId)> callback) {
  active_layer_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_layer_selection_requested_callback(
    std::function<void(std::vector<LayerId>, LayerId)> callback) {
  layer_selection_requested_callback_ = std::move(callback);
}

void CanvasWidget::request_layer_selection(std::vector<LayerId> layer_ids, LayerId active_id) {
  if (layer_selection_requested_callback_) {
    // The host round-trips synchronously: the panel selection change pushes
    // back into set_selected_layer_ids and sets the document's active layer.
    layer_selection_requested_callback_(std::move(layer_ids), active_id);
    return;
  }
  // Standalone canvas (no panel wired): apply the selection directly. Do not
  // fire active_layer_changed_callback_ here; in hosted embeddings it reveals
  // a single layer with ClearAndSelect, which would collapse the selection.
  if (document_ != nullptr &&
      (!document_->active_layer_id().has_value() || *document_->active_layer_id() != active_id)) {
    document_->set_active_layer(active_id);
  }
  set_selected_layer_ids(std::move(layer_ids));
}

void CanvasWidget::request_layer_deselection() {
  if (layer_selection_requested_callback_) {
    // Empty ids are the deselect-all contract; the host clears the panel rows
    // and the document's active layer, then pushes {} back.
    layer_selection_requested_callback_({}, LayerId{});
    return;
  }
  if (document_ != nullptr) {
    document_->clear_active_layer();
  }
  if (layer_edit_target_ != LayerEditTarget::Content) {
    set_layer_edit_target(LayerEditTarget::Content);
  }
  set_selected_layer_ids({});
  update();
}

void CanvasWidget::set_status_callback(std::function<void(QString)> callback) {
  status_callback_ = std::move(callback);
}

void CanvasWidget::set_error_status_callback(std::function<void(QString)> callback) {
  error_status_callback_ = std::move(callback);
}

// Blocking refusals from canvas tools. Falls back to the plain status callback
// so hosts that wire only set_status_callback still see the message text.
void CanvasWidget::select_only_layer_if_none_active() {
  if (document_ == nullptr || edit_locked_ || document_->active_layer_id().has_value()) {
    return;
  }
  if (const auto id = only_layer_id(std::as_const(*document_).layers()); id.has_value()) {
    request_layer_selection({*id}, *id);
  }
}

void CanvasWidget::report_status_error(const QString& message) const {
  if (error_status_callback_) {
    error_status_callback_(message);
  } else if (status_callback_) {
    status_callback_(message);
  }
}

void CanvasWidget::set_info_callback(std::function<void(CanvasInfoState)> callback) {
  info_callback_ = std::move(callback);
}

void CanvasWidget::refresh_info_display() const {
  emit_info_for_widget_position(mapFromGlobal(QCursor::pos()));
}

void CanvasWidget::set_document_changed_callback(std::function<void()> callback) {
  document_changed_callback_ = std::move(callback);
  document_changed_reason_callback_ = nullptr;
}

void CanvasWidget::set_document_changed_callback(std::function<void(DocumentChangeReason)> callback) {
  document_changed_reason_callback_ = std::move(callback);
  document_changed_callback_ = nullptr;
}

void CanvasWidget::set_view_changed_callback(std::function<void()> callback) {
  view_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_transform_controls_changed_callback(std::function<void()> callback) {
  transform_controls_changed_callback_ = std::move(callback);
}

void CanvasWidget::set_smart_object_transform_render_callback(std::function<bool(LayerId)> callback) {
  smart_object_transform_render_callback_ = std::move(callback);
}

void CanvasWidget::set_smart_object_source_image_callback(
    std::function<std::optional<QImage>(LayerId, QString* error)> callback) {
  smart_object_source_image_callback_ = std::move(callback);
}

void CanvasWidget::set_smart_object_paint_prompt_callback(std::function<void(LayerId)> callback) {
  smart_object_paint_prompt_callback_ = std::move(callback);
}

void CanvasWidget::set_text_layer_transform_render_callback(std::function<bool(LayerId)> callback) {
  text_layer_transform_render_callback_ = std::move(callback);
}

void CanvasWidget::set_selected_layer_ids(std::vector<LayerId> layer_ids) {
  if (layer_ids != selected_layer_ids_) {
    cancel_path_transform();
  }
  const auto old_transform_controls_rect = move_transform_controls_rect();
  auto keeps_active_transform =
      transforming_layer_ && transform_layer_id_.has_value() && layer_ids.size() == 1U &&
      layer_ids.front() == *transform_layer_id_;
  if (transforming_layer_ && !keeps_active_transform && !transform_session_root_ids_.empty()) {
    // Multi-target session: an identical re-selection of the same roots (panel
    // refreshes re-push the selection) keeps the session alive.
    auto sorted_ids = layer_ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    keeps_active_transform = sorted_ids == transform_session_root_ids_;
  }
  if (transforming_layer_ && !keeps_active_transform) {
    finish_free_transform();
  }
  const auto keeps_active_warp = warping_layer_ && warp_layer_id_.has_value() && layer_ids.size() == 1U &&
                                 layer_ids.front() == *warp_layer_id_;
  if (warping_layer_ && !keeps_active_warp) {
    finish_warp_transform();
  }
  if (move_transform_controls_layer_id_.has_value() &&
      !(layer_ids.size() == 1U && layer_ids.front() == *move_transform_controls_layer_id_)) {
    set_move_transform_controls_layer(std::nullopt);
  }
  selected_layer_ids_ = std::move(layer_ids);
  clear_guide_selection();
  update_move_transform_controls_dirty(old_transform_controls_rect);
  notify_transform_controls_changed();
}

bool CanvasWidget::document_contains(QPoint point) const noexcept {
  return document_ != nullptr && point.x() >= 0 && point.y() >= 0 && point.x() < document_->width() &&
         point.y() < document_->height();
}

bool CanvasWidget::selection_allows(QPoint point) const noexcept {
  return quick_mask_active_ || selection_contains(point);
}

bool CanvasWidget::selection_clips_grayscale_edits() const noexcept {
  return !quick_mask_active_ && has_selection();
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

LayerMask* CanvasWidget::active_layer_mask() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return nullptr;
  }

  auto* layer = document_->find_layer(*document_->active_layer_id());
  if (layer == nullptr || !layer->mask().has_value()) {
    return nullptr;
  }
  return &*layer->mask();
}

bool CanvasWidget::editing_layer_mask() const noexcept {
  if (layer_edit_target_ != LayerEditTarget::Mask || document_ == nullptr ||
      !document_->active_layer_id().has_value()) {
    return false;
  }
  // Const walk on purpose: this predicate runs from read-only paths (color
  // setters, cursor updates, begin_edit preconditions) and the non-const
  // mask() accessor bumps revisions on ACCESS (see docs/performance.md) — merely
  // picking a color while a mask was targeted
  // invalidated the layer's thumbnail and style-mask caches. Writers keep
  // going through active_layer_mask(), whose bump is load-bearing.
  const auto* layer = std::as_const(*document_).find_layer(*document_->active_layer_id());
  return layer != nullptr && layer->mask().has_value();
}

DocumentChannel* CanvasWidget::active_document_channel() const noexcept {
  if (document_ == nullptr || layer_edit_target_ != LayerEditTarget::DocumentChannel ||
      active_document_channel_id_ == 0) {
    return nullptr;
  }
  return document_->find_channel(active_document_channel_id_);
}

const DocumentChannel* CanvasWidget::active_document_channel_const() const noexcept {
  if (document_ == nullptr || layer_edit_target_ != LayerEditTarget::DocumentChannel ||
      active_document_channel_id_ == 0) {
    return nullptr;
  }
  return static_cast<const Document*>(document_)->find_channel(active_document_channel_id_);
}

std::optional<CanvasWidget::GrayscaleEditTarget> CanvasWidget::active_grayscale_edit_target(QRect required_rect) {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  if (quick_mask_active_) {
    if (quick_mask_pixels_.format() != PixelFormat::gray8() ||
        quick_mask_pixels_.width() != document_->width() ||
        quick_mask_pixels_.height() != document_->height()) {
      return std::nullopt;
    }
    return GrayscaleEditTarget{
        &quick_mask_pixels_,
        QRect(0, 0, document_->width(), document_->height())};
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    if (!editing_smart_filter_mask()) {
      return std::nullopt;
    }
    return GrayscaleEditTarget{
        &smart_filter_mask_pixels_, QRect(0, 0, document_->width(), document_->height())};
  }
  if (editing_layer_mask()) {
    auto* mask = active_layer_mask();
    if (mask == nullptr ||
        (!required_rect.isEmpty() &&
         !expand_mask_to_include_rect(*mask, required_rect, QSize(document_->width(), document_->height())))) {
      return std::nullopt;
    }
    return GrayscaleEditTarget{&mask->pixels,
                               QRect(mask->bounds.x, mask->bounds.y, mask->bounds.width, mask->bounds.height)};
  }
  auto* channel = active_document_channel();
  if (channel == nullptr || channel->kind() != DocumentChannelKind::Alpha) {
    return std::nullopt;
  }
  auto& pixels = channel->pixels();
  if (pixels.format() != PixelFormat::gray8() || pixels.width() != document_->width() ||
      pixels.height() != document_->height()) {
    return std::nullopt;
  }
  return GrayscaleEditTarget{&pixels, QRect(0, 0, document_->width(), document_->height())};
}

bool CanvasWidget::editing_grayscale_target() const noexcept {
  return quick_mask_active_ || editing_smart_filter_mask() || editing_layer_mask() ||
         (editing_document_channel() && document_channel_is_editable());
}

bool CanvasWidget::active_layer_locks_transparent_pixels() const noexcept {
  const auto* layer = active_pixel_layer();
  return layer != nullptr && layer_locks_transparent_pixels(*layer);
}

bool CanvasWidget::active_layer_locks_image_pixels() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return false;
  }
  return patchy::layer_effectively_locks_image_pixels(document_->layers(), *document_->active_layer_id());
}

bool CanvasWidget::active_layer_locks_position() const noexcept {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return false;
  }
  return patchy::layer_effectively_locks_position(document_->layers(), *document_->active_layer_id());
}

bool CanvasWidget::layer_effectively_locks_image_pixels(const Layer& layer) const noexcept {
  return document_ != nullptr && patchy::layer_effectively_locks_image_pixels(document_->layers(), layer.id());
}

bool CanvasWidget::layer_effectively_locks_position(const Layer& layer) const noexcept {
  return document_ != nullptr && patchy::layer_effectively_locks_position(document_->layers(), layer.id());
}

void CanvasWidget::show_layer_pixels_locked_message() const {
  report_status_error(tr("Layer pixels are locked."));
}

void CanvasWidget::show_layer_position_locked_message() const {
  report_status_error(tr("Layer position is locked."));
}

void CanvasWidget::show_edit_locked_message() const {
  report_status_error(tr("Finish the open dialog before editing the document"));
}

Layer* CanvasWidget::topmost_pixel_layer_at(QPoint document_point, bool require_visible_pixel,
                                            bool skip_locked) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return const_cast<Layer*>(topmost_pixel_layer_at_recursive(std::as_const(*document_).layers(), document_point,
                                                             require_visible_pixel, skip_locked));
}

Layer* CanvasWidget::topmost_move_layer_at(QPoint document_point, bool skip_locked) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  // Three passes, shared by the press handler, the hover outline, and the
  // lock status message so they always agree on the grabbed layer:
  // 1. a visible pixel under the point wins (precise, the original test);
  // 2. otherwise a selected layer whose Move rect contains the point (the box
  //    the user is looking at beats a larger unselected rect above it);
  // 3. otherwise the topmost layer whose Move rect contains the point, so a
  //    press on a transparent pixel inside a layer's outline still grabs it
  //    instead of drawing the layer-selection rectangle (Photoshop parity).
  const auto& document = std::as_const(*document_);
  if (const auto* hit = topmost_move_layer_at_recursive(document.layers(), document_point, skip_locked,
                                                        move_layer_contains_document_point);
      hit != nullptr) {
    return const_cast<Layer*>(hit);
  }
  for (const auto id : movable_layer_ids()) {
    const auto* layer = document.find_layer(id);
    if (layer != nullptr && move_layer_rect_contains_document_point(*layer, document_point)) {
      return const_cast<Layer*>(layer);
    }
  }
  return const_cast<Layer*>(topmost_move_layer_at_recursive(document.layers(), document_point, skip_locked,
                                                            move_layer_rect_contains_document_point));
}

Layer* CanvasWidget::topmost_text_layer_at(QPoint document_point) const noexcept {
  if (document_ == nullptr) {
    return nullptr;
  }

  return const_cast<Layer*>(topmost_text_layer_at_recursive(std::as_const(*document_).layers(), document_point));
}

void CanvasWidget::activate_layer(Layer& layer) {
  if (document_ == nullptr) {
    return;
  }
  if (document_->active_layer_id() != layer.id() ||
      layer_edit_target_ != LayerEditTarget::Content) {
    cancel_path_transform();
  }
  if (layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    clear_smart_filter_mask_edit_target();
  }
  const auto old_transform_controls_rect = move_transform_controls_rect();
  const auto keeps_multi_transform =
      !transform_session_root_ids_.empty() &&
      std::find(transform_session_root_ids_.begin(), transform_session_root_ids_.end(), layer.id()) !=
          transform_session_root_ids_.end();
  if (transforming_layer_ && !keeps_multi_transform &&
      (!transform_layer_id_.has_value() || *transform_layer_id_ != layer.id())) {
    finish_free_transform();
  }
  if (move_transform_controls_layer_id_.has_value() && *move_transform_controls_layer_id_ != layer.id()) {
    set_move_transform_controls_layer(std::nullopt);
  }
  clear_guide_selection();
  if (!document_->active_layer_id().has_value() || *document_->active_layer_id() != layer.id()) {
    document_->set_active_layer(layer.id());
  }
  layer_edit_target_ = LayerEditTarget::Content;
  if (active_layer_changed_callback_) {
    active_layer_changed_callback_(layer.id());
  }
  update_move_transform_controls_dirty(old_transform_controls_rect);
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
    info.active_rect = marquee_selection_region(selection_start_, selection_current_).boundingRect();
    info.active_rect_label = tr("Selection");
  } else if (document_ != nullptr && drawing_shape_) {
    info.active_rect = shape_drag_rect(shape_start_, snapped_document_point(document_point));
    info.active_rect_label = tr("Shape");
  } else if (document_ != nullptr && dragging_text_rect_) {
    info.active_rect = normalized_rect(text_rect_start_, snapped_document_point(document_point));
    info.active_rect_label = tr("Text");
  } else if (document_ != nullptr && zooming_ && !zoom_scrubbing_) {
    info.active_rect = normalized_rect(zoom_start_, document_point);
    info.active_rect_label = tr("Zoom");
  } else if (document_ != nullptr && !selection_.isEmpty()) {
    info.active_rect = selection_.boundingRect();
    info.active_rect_label = selection_.rectCount() == 1 ? tr("Selection") : tr("Selection bounds");
  }
  info_callback_(std::move(info));
}

}  // namespace patchy::ui
