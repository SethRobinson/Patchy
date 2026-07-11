#pragma once

#include "core/adjustment_layer.hpp"

#include <QImage>
#include <QSize>
#include <QString>

#include <span>

namespace patchy::ui {

// Built-in Curves presets have stable ids because the same ids can later be
// referenced by favorites, recent choices, or a contextual Properties panel.
struct CurvesPreset {
  QString id;
  QString english_name;
  CurvesAdjustment adjustment;
};

[[nodiscard]] std::span<const CurvesPreset> builtin_curves_presets();
[[nodiscard]] const CurvesPreset* find_curves_preset(const QString& id);
[[nodiscard]] const CurvesPreset* find_curves_preset(const CurvesAdjustment& adjustment);
[[nodiscard]] QString curves_preset_display_name(const CurvesPreset& preset);

// A deterministic, code-generated tonal target keeps thumbnails useful even
// when no source histogram is available (for example, a clipped adjustment).
// Every interior source pixel is transformed through the same core LUT as the
// canvas; there is no separate thumbnail-only curve math.
[[nodiscard]] QImage curves_adjustment_thumbnail(const CurvesAdjustment& adjustment,
                                                 QSize size = QSize(72, 48));

}  // namespace patchy::ui
