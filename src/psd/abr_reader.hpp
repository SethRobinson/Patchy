#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Reader for Adobe Photoshop .abr brush files. Extracts sampled (bitmap) brushes as 8-bit
// grayscale coverage masks plus name and spacing metadata. Supports the legacy v1/v2 layout and
// the v6/v7/v10 8BIM tagged-block layout (subversions 1 and 2). Computed (parametric) brushes
// have no bitmap and are skipped with a warning; brush dynamics are out of scope.
namespace patchy::psd {

struct AbrBrush {
  std::string name;               // empty when the file carries no name; caller assigns a fallback
  double spacing{0.25};           // dab spacing as a fraction of the brush diameter
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::uint8_t> mask; // row-major coverage, width * height bytes, 255 = opaque
};

struct AbrReadResult {
  std::vector<AbrBrush> brushes;
  std::vector<std::string> warnings;  // per-brush skips (computed-only, oversize, decode errors)
};

// Parses an in-memory .abr file. Returns std::nullopt and sets `error` when the file as a whole
// is unusable (bad header, truncation, or no sampled brushes at all); individual undecodable
// brushes are skipped with a warning instead of failing the file.
[[nodiscard]] std::optional<AbrReadResult> read_abr(std::span<const std::uint8_t> bytes, std::string& error);

}  // namespace patchy::psd
