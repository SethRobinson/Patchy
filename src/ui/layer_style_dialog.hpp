#pragma once

#include "core/layer.hpp"
#include "core/pattern_resource.hpp"

#include <functional>
#include <optional>

class QWidget;

namespace patchy::ui {

class PatternLibrary;
class StyleLibrary;

struct LayerStyleSettings {
  int opacity{100};
  BlendMode blend_mode{BlendMode::Normal};
  LayerStyle style;
  LayerBlendIf blend_if;
  // Malformed, non-RGB, or otherwise unknown native payloads remain raw until
  // the user explicitly chooses to replace them with editable RGB defaults.
  bool replace_unsupported_blend_if{false};
};

// document_patterns (optional) lists the document's embedded pattern tiles.
// pattern_library adds the persistent folder-aware user presets and enables the
// Pattern Manager buttons. The dialog can add a transient document resource when
// a chosen library pattern has the same Photoshop id as different embedded
// pixels. MainWindow snapshots the store around the dialog so cancel stays clean.
// style_library enables the Styles page's preset browser (apply on click, New
// Style..., Manage Styles...); null leaves the page present but inert.
[[nodiscard]] std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer,
    std::function<void(const LayerStyleSettings&)> preview_changed = {},
    PatternStore* document_patterns = nullptr,
    PatternLibrary* pattern_library = nullptr,
    StyleLibrary* style_library = nullptr);

}  // namespace patchy::ui
