#pragma once

#include "core/palette.hpp"

#include <span>
#include <string_view>

namespace patchy {

// A built-in palette embedded in code (no binary assets, like the default brush
// tips). Hardware color tables are uncopyrightable facts; community palettes are
// included only where clearly free (provenance notes in palette_presets.cpp and
// NOTICE.txt).
struct PalettePreset {
  const char* id;            // stable identifier, persisted in settings; never rename
  const char* english_name;  // UI wraps in tr() for display
  std::span<const RgbColor> colors;
};

[[nodiscard]] std::span<const PalettePreset> builtin_palette_presets() noexcept;
[[nodiscard]] const PalettePreset* find_builtin_palette_preset(std::string_view id) noexcept;

}  // namespace patchy
