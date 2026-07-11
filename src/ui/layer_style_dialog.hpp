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
  LayerBlendIf blend_if;
  // Malformed, non-RGB, or otherwise unknown native payloads remain raw until
  // the user explicitly chooses to replace them with editable RGB defaults.
  bool replace_unsupported_blend_if{false};
};

[[nodiscard]] std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer,
    std::function<void(const LayerStyleSettings&)> preview_changed = {});

}  // namespace patchy::ui
