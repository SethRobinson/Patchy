#pragma once

#include "core/document.hpp"

#include <QIcon>
#include <QString>

#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// The color that leaves the image unchanged under a blend mode (#410): the New Layer
// dialog's "Fill with neutral color" fills the new layer with it.
enum class NeutralFillColor { White, Black, Gray };

// White for the darkening modes and Divide, black for the lightening modes, Difference,
// Exclusion and Subtract, 50% gray for the contrast modes whose 50% gray is a no-op
// (not Hard Mix, which thresholds). Nullopt for every other mode.
[[nodiscard]] std::optional<NeutralFillColor> neutral_fill_for_blend_mode(BlendMode mode) noexcept;

// Layer > New > New Layer... (GitHub issue 275): what the new layer starts with.
struct NewLayerSettings {
  QString name;
  bool clip_to_previous{false};
  LayerColorLabel color_label{kLayerColorNone};
  BlendMode blend_mode{BlendMode::Normal};
  float opacity{1.0F};
  // Fill the new layer with this color instead of leaving it transparent (#410).
  std::optional<NeutralFillColor> neutral_fill;
};

// One Color entry, in LayerColorLabel order (None first, no swatch).
struct NewLayerColorChoice {
  QString name;
  QIcon swatch;
};

// The New Layer dialog (objectName newLayerDialog). `clip_available` enables "Use Previous
// Layer to Create Clipping Mask" (a layer below the new one can be its base); `depth` disables
// the blend modes the document cannot use. "Fill with neutral color" follows the chosen mode
// (neutral_fill_for_blend_mode). Nullopt when cancelled. See docs/new-layer.md.
[[nodiscard]] std::optional<NewLayerSettings> request_new_layer_settings(
    QWidget* parent, const QString& default_name, bool clip_available, BitDepth depth,
    const std::vector<NewLayerColorChoice>& colors);

}  // namespace patchy::ui
