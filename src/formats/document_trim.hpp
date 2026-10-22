#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace patchy {

// Image > Trim (GitHub issue 297): what the trim compares against. The ids are permanent
// (settings value `trim/basedOn` and the script's `basedOn` option).
enum class TrimBasis : std::uint8_t {
  TransparentPixels,
  TopLeftColor,
  BottomRightColor
};

[[nodiscard]] std::string_view trim_basis_id(TrimBasis basis) noexcept;
[[nodiscard]] std::optional<TrimBasis> parse_trim_basis(std::string_view id) noexcept;

struct TrimSides {
  bool top{true};
  bool bottom{true};
  bool left{true};
  bool right{true};
};

// The composite the trim analyses: every visible layer at the document's depth (RGBA8 for
// 8-bit documents, straight RGBA at 16 or 32 bits otherwise).
[[nodiscard]] PixelBuffer flatten_document_for_trim(const Document& document);

// True when some composite pixel is not fully opaque (Trim's Transparent Pixels choice is
// offered only then).
[[nodiscard]] bool trim_composite_has_transparency(const PixelBuffer& composite);

// The rect Trim keeps, in canvas coordinates: on each enabled side the run of rows or
// columns whose every pixel matches the basis is cut away. Transparent means alpha 0; a
// corner color matches exactly (every channel at the composite's depth; a transparent
// corner matches every transparent pixel). nullopt when every pixel matches (nothing would
// remain); the full canvas when nothing on the enabled sides matches.
[[nodiscard]] std::optional<Rect> trim_bounds(const PixelBuffer& composite, TrimBasis basis, TrimSides sides);

}  // namespace patchy
