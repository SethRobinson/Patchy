#pragma once

// How the Layers panel draws its rows (GitHub issue 680; docs/layer-panel-row-options.md),
// set on Customize Tool Palette's Layers Panel tab: compact rows without the blend mode and
// opacity line, the row tooltip, and a color label as the 4 px strip or a tint of the whole row.
// The settings keys and the tokens are persisted identifiers: never rename them. Defaults are
// stored as no key.

#include "ui/app_settings.hpp"

#include <QString>

namespace patchy::ui {

enum class LayerRowColorStyle { Strip, WholeRow };

inline constexpr const char* kLayerCompactRowsSettingsKey = "layersPanel/compactRows";
inline constexpr const char* kLayerRowTooltipsSettingsKey = "layersPanel/rowTooltips";
inline constexpr const char* kLayerRowColorSettingsKey = "layersPanel/rowColor";

struct LayerRowOptions {
  // Hides the blend mode and opacity line under the layer name; the main row gets shorter.
  bool compact{false};
  // The row's tooltip (name, opacity, locks); badge and control tooltips stay.
  bool tooltips{true};
  LayerRowColorStyle color_style{LayerRowColorStyle::Strip};

  bool operator==(const LayerRowOptions&) const = default;
};

[[nodiscard]] inline QString layer_row_color_style_token(LayerRowColorStyle style) {
  return style == LayerRowColorStyle::WholeRow ? QStringLiteral("whole_row") : QStringLiteral("strip");
}

// An unknown or missing token reads as the strip.
[[nodiscard]] inline LayerRowColorStyle layer_row_color_style_from_token(const QString& token) {
  return token == QLatin1String("whole_row") ? LayerRowColorStyle::WholeRow : LayerRowColorStyle::Strip;
}

[[nodiscard]] inline LayerRowOptions stored_layer_row_options() {
  const auto settings = app_settings();
  LayerRowOptions options;
  options.compact = settings.value(QLatin1String(kLayerCompactRowsSettingsKey), false).toBool();
  options.tooltips = settings.value(QLatin1String(kLayerRowTooltipsSettingsKey), true).toBool();
  options.color_style =
      layer_row_color_style_from_token(settings.value(QLatin1String(kLayerRowColorSettingsKey)).toString());
  return options;
}

inline void set_stored_layer_row_options(const LayerRowOptions& options) {
  auto settings = app_settings();
  if (options.compact) {
    settings.setValue(QLatin1String(kLayerCompactRowsSettingsKey), true);
  } else {
    settings.remove(QLatin1String(kLayerCompactRowsSettingsKey));
  }
  if (options.tooltips) {
    settings.remove(QLatin1String(kLayerRowTooltipsSettingsKey));
  } else {
    settings.setValue(QLatin1String(kLayerRowTooltipsSettingsKey), false);
  }
  if (options.color_style == LayerRowColorStyle::Strip) {
    settings.remove(QLatin1String(kLayerRowColorSettingsKey));
  } else {
    settings.setValue(QLatin1String(kLayerRowColorSettingsKey), layer_row_color_style_token(options.color_style));
  }
}

}  // namespace patchy::ui
