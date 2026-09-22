#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QRegion>
#include <QString>
#include <QSize>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QPainter;
class QQuickWidget;
class QResizeEvent;

namespace patchy::ui {

enum class CanvasGraphicsApi : std::uint8_t {
  Unknown,
  OpenGL,
  Vulkan,
  Metal,
  Direct3D11,
  Direct3D12
};

struct CanvasGpuBlendIfThresholds {
  std::uint8_t black_low{0};
  std::uint8_t black_high{0};
  std::uint8_t white_low{255};
  std::uint8_t white_high{255};
};

struct CanvasGpuBlendIfRanges {
  CanvasGpuBlendIfThresholds this_layer;
  CanvasGpuBlendIfThresholds underlying_layer;
};

// A document snapshot that the Qt Quick scene graph or an optional external
// compositor can composite directly.
// `rect` is in CanvasWidget coordinates; the image remains in document pixel
// coordinates and is therefore filtered/scaled by the graphics backend.
struct CanvasGpuLayer {
  std::uint64_t id{0};
  std::uint64_t revision{0};
  QImage image;
  QRectF rect;
  QRectF document_rect;
  qreal opacity{1.0};
  int blend_mode{1};
  QImage mask_image;
  QRectF mask_rect;
  QRectF mask_document_rect;
  qreal mask_default{1.0};
  qreal mask_density{1.0};
  bool has_mask{false};
  bool has_blend_if{false};
  // The order is Gray, Red, Green, Blue, matching LayerBlendIf and PSD.
  std::array<CanvasGpuBlendIfRanges, 4> blend_if;
};

struct CanvasGpuDocument {
  QSize document_size;
  QRectF canvas_rect;
  QColor canvas_backdrop;
  std::vector<CanvasGpuLayer> layers;
  // When a separate document compositor such as Dawn/WebGPU has already
  // produced the complete image, this is presented as one authoritative layer.
  // It must never be combined with the individual layer list.
  QImage composited_frame;
  bool smooth_scaling{true};
  bool shader_composition{false};
};

// Owns the optional Qt Quick scene-graph surface used to present and compose
// supported documents. Unsupported documents never get partially composed:
// CanvasWidget keeps them on its authoritative CPU compositor instead.
class CanvasGraphicsSurface final : public QWidget {
  Q_OBJECT

public:
  static std::unique_ptr<CanvasGraphicsSurface> create(QWidget* parent);
  ~CanvasGraphicsSurface() override;

  [[nodiscard]] CanvasGraphicsApi api() const noexcept;
  [[nodiscard]] bool set_gpu_document(CanvasGpuDocument document);
  void clear_gpu_document();
  void request_update(const QRegion& region);
  void set_overlay_painter(std::function<void(QPainter&, QRect)> painter);

signals:
  void ready(CanvasGraphicsApi api);
  void failed(QString reason);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  class QuickCanvasItem;
  class OverlayWidget;

  explicit CanvasGraphicsSurface(QWidget* parent);
  void scene_graph_error(int error, const QString& message);
  void set_api_from_scene_graph();

  QQuickWidget* quick_widget_{nullptr};
  QuickCanvasItem* quick_item_{nullptr};
  OverlayWidget* overlay_widget_{nullptr};
  CanvasGraphicsApi api_{CanvasGraphicsApi::Unknown};
  bool scene_graph_probe_started_{false};
};

}  // namespace patchy::ui
