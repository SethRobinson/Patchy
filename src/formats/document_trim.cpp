#include "formats/document_trim.hpp"

#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "render/compositor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace patchy {

std::string_view trim_basis_id(TrimBasis basis) noexcept {
  switch (basis) {
    case TrimBasis::TransparentPixels:
      return "transparent";
    case TrimBasis::TopLeftColor:
      return "topLeft";
    case TrimBasis::BottomRightColor:
      return "bottomRight";
  }
  return "transparent";
}

std::optional<TrimBasis> parse_trim_basis(std::string_view id) noexcept {
  for (const auto basis : {TrimBasis::TransparentPixels, TrimBasis::TopLeftColor, TrimBasis::BottomRightColor}) {
    if (trim_basis_id(basis) == id) {
      return basis;
    }
  }
  return std::nullopt;
}

PixelBuffer flatten_document_for_trim(const Document& document) {
  return document.color_state().bit_depth == BitDepth::UInt8 ? flatten_document_rgba8(document)
                                                             : Compositor{}.flatten_rgba_deep(document);
}

namespace {

[[nodiscard]] DeepDomain composite_domain(const PixelBuffer& composite) noexcept {
  return composite.format().bit_depth == BitDepth::Float32 ? DeepDomain::Linear : DeepDomain::Encoded;
}

}  // namespace

bool trim_composite_has_transparency(const PixelBuffer& composite) {
  if (composite.empty() || composite.format().channels < 4) {
    return false;
  }
  const auto width = composite.width();
  std::vector<float> row(static_cast<std::size_t>(width) * 4U);
  const auto domain = composite_domain(composite);
  for (std::int32_t y = 0; y < composite.height(); ++y) {
    load_rgba_row(composite, y, 0, width, domain, row);
    for (std::int32_t x = 0; x < width; ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] < 255.0F) {
        return true;
      }
    }
  }
  return false;
}

std::optional<Rect> trim_bounds(const PixelBuffer& composite, TrimBasis basis, TrimSides sides, int tolerance) {
  const auto width = composite.width();
  const auto height = composite.height();
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  tolerance = std::clamp(tolerance, 0, 255);
  // Tolerance 0 compares at the composite's own domain (exact). A tolerance compares
  // display-encoded values, so a 32-bit composite is sRGB-encoded first, as the Magic Wand
  // does: N means the same step on every depth.
  const auto domain = tolerance > 0 ? DeepDomain::Encoded : composite_domain(composite);
  std::vector<float> row(static_cast<std::size_t>(width) * 4U);
  std::array<float, 4> reference{0.0F, 0.0F, 0.0F, 0.0F};
  if (basis != TrimBasis::TransparentPixels) {
    const auto corner_x = basis == TrimBasis::TopLeftColor ? 0 : width - 1;
    const auto corner_y = basis == TrimBasis::TopLeftColor ? 0 : height - 1;
    load_rgba_row(composite, corner_y, corner_x, 1, domain, row);
    std::copy_n(row.begin(), 4, reference.begin());
  }
  // A transparent pixel's color is meaningless, so a transparent corner matches every
  // transparent pixel whatever color the composite left under it.
  const auto match_transparent = basis == TrimBasis::TransparentPixels || reference[3] <= 0.0F;
  const auto limit = static_cast<float>(tolerance);
  const auto matches = [&](const float* px) {
    if (match_transparent) {
      return px[3] <= limit;
    }
    if (tolerance == 0) {
      return px[0] == reference[0] && px[1] == reference[1] && px[2] == reference[2] && px[3] == reference[3];
    }
    for (std::size_t c = 0; c < 4U; ++c) {
      if (std::abs(px[c] - reference[c]) > limit) {
        return false;
      }
    }
    return true;
  };

  std::int32_t min_x = width;
  std::int32_t max_x = -1;
  std::int32_t min_y = height;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < height; ++y) {
    load_rgba_row(composite, y, 0, width, domain, row);
    std::int32_t first = -1;
    std::int32_t last = -1;
    for (std::int32_t x = 0; x < width; ++x) {
      if (!matches(row.data() + static_cast<std::size_t>(x) * 4U)) {
        first = x;
        break;
      }
    }
    if (first < 0) {
      continue;
    }
    for (std::int32_t x = width - 1; x >= first; --x) {
      if (!matches(row.data() + static_cast<std::size_t>(x) * 4U)) {
        last = x;
        break;
      }
    }
    min_x = std::min(min_x, first);
    max_x = std::max(max_x, last);
    min_y = std::min(min_y, y);
    max_y = y;
  }
  if (max_y < 0) {
    return std::nullopt;
  }
  const auto left = sides.left ? min_x : 0;
  const auto right = sides.right ? max_x + 1 : width;
  const auto top = sides.top ? min_y : 0;
  const auto bottom = sides.bottom ? max_y + 1 : height;
  return Rect{left, top, right - left, bottom - top};
}

}  // namespace patchy
