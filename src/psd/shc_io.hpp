#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Reader and writer for Adobe Photoshop .shc contour preset files (GitHub
// issue 726), worked out from files Photoshop 2027 wrote and checked by
// loading Patchy's output back into it (layout and method in
// docs/style-presets-contours.md, "Photoshop .shc files"). Big-endian:
// '8BFS', u16 version 1, u32 contour count; per contour a u32 2, a u32 UTF-16
// unit count and the name (terminator included, no padding), a u16 2, a u16
// point count, the points as u16 pairs (output, then input, 0..255), one byte
// per point (1 = smooth, 0 = corner) and eight zero bytes.
namespace patchy::psd {

struct ShcReadResult {
  // Names are the text a user should see (Photoshop's own presets carry a
  // "$$$/key=Name" form; the part after '=' is kept).
  std::vector<StyleContour> contours;
  // Contours skipped for unusable points, and a damaged tail.
  std::vector<std::string> warnings;
};

// Parses an in-memory .shc file. Returns std::nullopt and sets `error` (an
// English text marked for translation) when the file is not a contour file or
// holds no usable contour. A damaged tail keeps the contours before it.
[[nodiscard]] std::optional<ShcReadResult> read_shc(std::span<const std::uint8_t> bytes, std::string& error);

// Writes the contours in the layout above. Points are written as the editor
// reads them (editable_style_contour), rounded to whole levels; an unnamed
// contour is written as "Custom".
[[nodiscard]] std::vector<std::uint8_t> write_shc(std::span<const StyleContour> contours);

}  // namespace patchy::psd
