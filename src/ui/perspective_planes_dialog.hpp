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
  // The visible layers under the edited one composited (straight RGBA8, document-sized),
  // drawn behind it so there is something to line planes up with; empty for none.
  PixelBuffer backdrop;
  // The visible layers over it, the same way: Clone's All Layers reads them too.
  PixelBuffer above;
  // Clone's Sample choice (the retouch tools' shared setting).
  SampleLayers sample_layers{SampleLayers::AllLayers};
  // The edited layer's name, shown with the target plane.
  QString layer_name;
  // The document selection's coverage (gray8, document-sized), empty for none: what Place
  // Layer takes out of the layer.
  PixelBuffer selection;
  // The layer can give up its own content to Place Layer (an 8-bit layer with transparency).
  bool can_place_layer{false};
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
  // After Place Layer: the layer's own pixels on the canvas with the placed content taken
  // out (straight RGBA8, document-sized). The layer becomes this plus, for Replace this
  // layer, the edits.
  std::optional<PixelBuffer> lifted_layer;
  bool write_onto_layer{false};
  // Clone's Sample choice as the workspace left it.
  SampleLayers sample_layers{SampleLayers::AllLayers};
};

// Runs the Perspective Planes workspace (docs/perspective-planes.md).
[[nodiscard]] PerspectiveWorkspaceResult run_perspective_planes_workspace(QWidget* parent,
                                                                          PerspectiveWorkspaceInput input);

}  // namespace patchy::ui
