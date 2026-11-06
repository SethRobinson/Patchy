#pragma once

#include "core/document.hpp"

#include <QColor>
#include <QIcon>
#include <QString>

#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// The color that leaves the image unchanged under a blend mode (#410): the New Layer
// dialog's "Neutral Color for This Mode" fill fills the new layer with it.
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
  // What fills the new layer; transparent when neither is set. The mode's neutral color (#410)
  // is exact at every depth (50% gray is 0.5 linear in 32 bits); any other color (#654) is the
  // 8-bit color at the document's depth, as Edit > Fill puts it.
  std::optional<NeutralFillColor> neutral_fill;
  std::optional<QColor> fill_color;
};

// One Color entry, listed in the order given (the Layer Color menu's: None first, no swatch).
struct NewLayerColorChoice {
  QString name;
  QIcon swatch;
  LayerColorLabel label{kLayerColorNone};
};

// The New Layer dialog (objectName newLayerDialog). `clip_available` enables "Clip to the layer
// below" (a layer below the new one can be its base); `depth` disables the blend modes the
// document cannot use. Fill is Edit > Fill's Contents list (FillContentsField) starting at
// Transparent, `foreground` and `background` its Foreground and Background colors; its
// "Neutral Color for This Mode" entry follows the chosen mode (neutral_fill_for_blend_mode).
// Nullopt when cancelled. See docs/new-layer.md.
[[nodiscard]] std::optional<NewLayerSettings> request_new_layer_settings(
    QWidget* parent, const QString& default_name, bool clip_available, BitDepth depth,
    const std::vector<NewLayerColorChoice>& colors, QColor foreground, QColor background);

}  // namespace patchy::ui
