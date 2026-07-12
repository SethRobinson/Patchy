#pragma once

#include "core/layer.hpp"
#include "core/pattern_resource.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Codec for Adobe Photoshop .asl style-library files. The envelope is a u16
// version 2, '8BSL', a u16 patterns version 3 plus a length-prefixed patterns
// section (the same per-pattern records as the PSD 'Patt' block; an empty
// section is length 0 with no count field), then a counted list of style
// records. Each record is a length-prefixed pair of version-16 descriptors:
// class 'null' {Nm, Idnt} and class 'Styl' {documentMode, Lefx, blendOptions}.
// The 'Lefx' object matches the lfx2 root descriptor, so its conversion is
// shared with the PSD reader/writer (psd/psd_layer_effects.hpp). Layout and the
// populated blendOptions shape are pinned against Photoshop 2026 captures
// (docs/ps-compat.md "Style presets and .asl").
namespace patchy {
class CmykToRgbTransform;
}

namespace patchy::psd {

// A style's captured layer blending options (Photoshop's "Include Layer
// Blending Options"). Patchy models opacity, blend mode, and Blend If;
// fillOpacity/knockout/channel restrictions are dropped with warnings.
struct AslBlendSettings {
  int opacity{100};  // percent
  BlendMode blend_mode{BlendMode::Normal};
  LayerBlendIf blend_if{};

  friend bool operator==(const AslBlendSettings&, const AslBlendSettings&) = default;
};

struct AslStyle {
  std::string id;    // 'Idnt' GUID-shaped text; reader repairs missing ids
  std::string name;  // display name (ZString "$$$/key=Name" forms resolved)
  LayerStyle style;
  std::optional<AslBlendSettings> blend_settings;
};

struct AslReadResult {
  std::vector<AslStyle> styles;
  // Decoded patterns section. Standalone imports have no document raw block
  // that can preserve them, so resources are returned as Authored (a document
  // that adopts one embeds it when referenced), mirroring pat_reader.
  std::vector<PatternResource> patterns;
  // Per-style skips, repaired ids, and dropped-unsupported notes.
  std::vector<std::string> warnings;
};

// Parses an in-memory .asl file. Returns std::nullopt and sets `error` when the
// file as a whole is unusable (bad header, unsafe counts, or nothing decodable).
// Once at least one style has been decoded, a damaged trailing record is
// reported as a warning and the decoded prefix is returned. Trailing 8BIMphry
// hierarchy data is deliberately ignored. CMYK effect colors use `cmyk_icc`
// when supplied and the naive Photoshop ink mix otherwise.
[[nodiscard]] std::optional<AslReadResult> read_asl(std::span<const std::uint8_t> bytes,
                                                    std::string& error,
                                                    const CmykToRgbTransform* cmyk_icc = nullptr);

// Serializes styles (and the pattern tiles they reference) into a .asl file.
// Callers pass patterns deduped by id; styles with blend_settings gain the
// calibrated blendOptions descriptor. Output is deterministic.
[[nodiscard]] std::vector<std::uint8_t> write_asl(std::span<const AslStyle> styles,
                                                  std::span<const PatternResource> patterns);

}  // namespace patchy::psd
