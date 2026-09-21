#pragma once

#include <QImage>
#include <QRegion>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>

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

// Owns the optional Qt Quick scene-graph surface used to present the already
// composed canvas. The document compositor remains outside this class and is
// always CPU-authoritative.
class CanvasGraphicsSurface final : public QWidget {
  Q_OBJECT

public:
  static std::unique_ptr<CanvasGraphicsSurface> create(QWidget* parent);
  ~CanvasGraphicsSurface() override;

  [[nodiscard]] CanvasGraphicsApi api() const noexcept;
  void set_frame(QImage frame);
  void request_update(const QRegion& region);

signals:
  void ready(CanvasGraphicsApi api);
  void failed(QString reason);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  class QuickCanvasItem;

  explicit CanvasGraphicsSurface(QWidget* parent);
  void scene_graph_error(int error, const QString& message);
  void set_api_from_scene_graph();

  QQuickWidget* quick_widget_{nullptr};
  QuickCanvasItem* quick_item_{nullptr};
  CanvasGraphicsApi api_{CanvasGraphicsApi::Unknown};
  bool scene_graph_probe_started_{false};
};

}  // namespace patchy::ui
