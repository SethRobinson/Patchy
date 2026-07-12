#pragma once

#include "core/layer.hpp"
#include "core/pattern_resource.hpp"

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

// document_patterns (optional) lists the document's embedded pattern tiles so
// the Pattern Overlay / Bevel Texture pickers can offer them alongside the
// built-in presets. The dialog only reads it; the host materializes any picked
// built-in preset into the document store when applying the settings
// (MainWindow::edit_active_layer_style does this for previews and commits).
[[nodiscard]] std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer,
    std::function<void(const LayerStyleSettings&)> preview_changed = {},
    const PatternStore* document_patterns = nullptr);

}  // namespace patchy::ui
