#pragma once

#include "core/vector_shape.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Reader for Adobe Photoshop .csh custom-shape libraries (GitHub issue 486),
// written from files only (layout in docs/vector-tools.md, "Photoshop .csh
// files"): 'cush', a u32 version 2 and a u32 shape count, then per shape a
// u32 UTF-16 length with the big-endian name (terminator included), zero
// padding to a 4-byte file offset, a u32 1, and a u32 block length. The block
// holds a Pascal id string, the shape's bounds (four i32: top, left, bottom,
// right) and the PSD path-resource record stream (26-byte records, points as
// 8.24 fractions of the bounds box), zero padded to a 4-byte file offset.
namespace patchy::psd {

struct CshShape {
  std::string name;  // UTF-8; empty in the file becomes "" (the caller names it)
  std::string id;    // the file's id text (GUID-shaped or zeros)
  // Normalized to the unit box by the outline's exact curve bounds, keeping
  // the shape's proportions: Photoshop fits those bounds to the drawn
  // rectangle (measured against Photoshop 2026, October 2026).
  VectorPath path;
};

struct CshReadResult {
  std::vector<CshShape> shapes;
  // Shapes skipped for damaged or empty geometry, and a damaged tail.
  std::vector<std::string> warnings;
};

// Parses an in-memory .csh file. Returns std::nullopt and sets `error` (an
// English text marked for translation) when the file is not a custom-shape
// library or holds no usable shape. A damaged shape after decoded ones is
// skipped with a warning.
[[nodiscard]] std::optional<CshReadResult> read_csh(std::span<const std::uint8_t> bytes, std::string& error);

}  // namespace patchy::psd
