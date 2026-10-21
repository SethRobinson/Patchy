#pragma once

#include "core/document.hpp"

#include <QIcon>
#include <QString>

#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Layer > New > New Layer... (GitHub issue 275): what the new layer starts with.
struct NewLayerSettings {
  QString name;
  bool clip_to_previous{false};
  LayerColorLabel color_label{kLayerColorNone};
  BlendMode blend_mode{BlendMode::Normal};
  float opacity{1.0F};
};

// One Color entry, in LayerColorLabel order (None first, no swatch).
struct NewLayerColorChoice {
  QString name;
  QIcon swatch;
};

// The New Layer dialog (objectName newLayerDialog). `clip_available` enables "Use Previous
// Layer to Create Clipping Mask" (a layer below the new one can be its base); `depth` disables
// the blend modes the document cannot use. Nullopt when cancelled. See docs/layer-panel.md.
[[nodiscard]] std::optional<NewLayerSettings> request_new_layer_settings(
    QWidget* parent, const QString& default_name, bool clip_available, BitDepth depth,
    const std::vector<NewLayerColorChoice>& colors);

}  // namespace patchy::ui
