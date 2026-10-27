#pragma once

#include "core/perspective_planes.hpp"

#include <QColor>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

class QWidget;

namespace patchy::ui {

// An image the workspace can paste onto a plane (straight RGBA8).
struct PerspectivePasteSource {
  QString name;
  PixelBuffer pixels;
};

struct PerspectiveWorkspaceInput {
  PixelBuffer layer;  // the active layer as straight RGBA8
  Rect layer_bounds{};
  std::int32_t document_width{0};
  std::int32_t document_height{0};
  std::vector<PerspectivePlane> planes;
  std::vector<PerspectivePasteSource> paste_sources;
  QColor foreground{Qt::black};
  bool write_onto_layer{false};
  // False when the layer cannot take the edits itself (it is then always a new layer).
  bool can_write_onto_layer{true};
};

struct PerspectiveWorkspaceResult {
  bool accepted{false};
  // The planes as the workspace left them, kept for the document even after Cancel.
  std::vector<PerspectivePlane> planes;
  // The edits alone, cropped to what they cover, in document pixels.
  std::optional<std::pair<PixelBuffer, Rect>> edits;
  bool write_onto_layer{false};
};

// Runs the Perspective Planes workspace (docs/perspective-planes.md).
[[nodiscard]] PerspectiveWorkspaceResult run_perspective_planes_workspace(QWidget* parent,
                                                                          PerspectiveWorkspaceInput input);

}  // namespace patchy::ui
