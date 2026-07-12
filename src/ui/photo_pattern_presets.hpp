#pragma once

#include "core/pattern_resource.hpp"

#include <optional>
#include <string_view>

// Pixel access for the bundled photo-texture pattern presets
// (core/pattern_presets.hpp's PhotoPatternPreset table; tiles live in the
// ":/patchy/textures" Qt resources). Also the one-stop resolver every
// style-preset site uses for "does this pattern id ship with Patchy": the
// code-generated presets and the photo presets together.
namespace patchy::ui {

// Decoded RGBA8 tile for a photo preset id; std::nullopt for unknown ids or a
// broken resource.
[[nodiscard]] std::optional<PatternResource> photo_pattern_resource(std::string_view id);

// True when the id is a bundled preset of either kind.
[[nodiscard]] bool is_bundled_pattern_id(std::string_view id);

// {id, name, tile, Authored} for a bundled preset of either kind (generated or
// photo); std::nullopt for unknown ids.
[[nodiscard]] std::optional<PatternResource> bundled_pattern_resource(std::string_view id);

}  // namespace patchy::ui
