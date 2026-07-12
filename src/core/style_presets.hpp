#pragma once

#include "core/layer.hpp"

#include <span>
#include <string_view>

namespace patchy {

// A built-in layer-style preset generated in code (no binary assets). The id is
// persisted in library sidecars and exported .asl files ('Idnt', GUID-shaped),
// and the english name/folder are the canonical strings stored in the user
// library (display translates them only while unrenamed) — so NEVER rename or
// re-seed an existing entry; pinned by style_presets_have_stable_ids_and_recipes.
//
// Recipes stay inside the feature set Patchy renders today (smooth inner
// bevels, gloss contours, stacked effect instances, built-in patterns) and
// reference only builtin_pattern_presets() ids, so applying a preset can always
// materialize its pattern tiles. Built-ins carry effects only, never blending
// options.
struct StylePreset {
  const char* id;
  const char* english_name;
  const char* english_folder;
  // Feeds the style library's defaults gate so upgrades add only new entries
  // (mirrors PhotoPatternPreset::introduced_version).
  int introduced_version;
};

[[nodiscard]] std::span<const StylePreset> builtin_style_presets() noexcept;
[[nodiscard]] const StylePreset* find_builtin_style_preset(std::string_view id) noexcept;

// Builds the preset's modeled effects; an empty style for unknown ids.
[[nodiscard]] LayerStyle builtin_style_preset_style(std::string_view id);

}  // namespace patchy
