#pragma once

#include "core/layer.hpp"

#include <functional>
#include <optional>

class QWidget;

namespace patchy::ui {

struct LayerStyleSettings {
  int opacity{100};
  BlendMode blend_mode{BlendMode::Normal};
  LayerStyle style;
};

[[nodiscard]] std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer,
    std::function<void(const LayerStyleSettings&)> preview_changed = {});

}  // namespace patchy::ui
