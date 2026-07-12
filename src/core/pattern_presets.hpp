#pragma once

#include "core/pattern_resource.hpp"

#include <span>
#include <string_view>

namespace patchy {

// A built-in pattern generated in code (no binary assets, like the default
// brush tips). Every tile is seamless by construction and fully deterministic
// (integer lattice hashing only — the cross-toolchain rule for pixel output).
//
// The id is written into saved PSDs as the pattern's 'Idnt' (GUID-shaped, the
// form Photoshop expects) and the english name as its 'Nm  ': both persist in
// user files, so NEVER rename or re-seed an existing entry — pinned by the
// pattern_presets_generate_stable_tiles hashes.
struct PatternPreset {
  const char* id;            // stable GUID persisted into PSDs; never change
  const char* english_name;  // persisted pattern name; UI wraps in tr() for display
};

[[nodiscard]] std::span<const PatternPreset> builtin_pattern_presets() noexcept;
[[nodiscard]] const PatternPreset* find_builtin_pattern_preset(std::string_view id) noexcept;

// Generates the preset's RGBA8 tile; empty buffer for unknown ids.
[[nodiscard]] PixelBuffer generate_builtin_pattern_tile(std::string_view id);

// Convenience: {id, name, tile, Authored} ready for PatternStore::adopt.
[[nodiscard]] PatternResource builtin_pattern_resource(std::string_view id);

// A built-in pattern backed by a bundled photo texture (real photographs under
// CC0, never AI-generated — see the AGENTS.md sourcing rule and the
// NOTICE-THIRD-PARTY.md provenance table). This core table carries the metadata
// only; the pixels live in the UI layer's Qt resources
// (":/patchy/textures/<resource_alias>", loaded by
// ui/photo_pattern_presets.hpp). Ids and canonical names persist in PSDs and
// library sidecars — append-only, never rename or re-seed. introduced_version
// feeds the pattern library's defaults gate so upgrades add only new entries.
struct PhotoPatternPreset {
  const char* id;
  const char* english_name;
  const char* resource_alias;  // file name inside :/patchy/textures/
  int introduced_version;
};

[[nodiscard]] std::span<const PhotoPatternPreset> photo_pattern_presets() noexcept;
[[nodiscard]] const PhotoPatternPreset* find_photo_pattern_preset(std::string_view id) noexcept;

}  // namespace patchy
