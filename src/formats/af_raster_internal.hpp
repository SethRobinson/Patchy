#pragma once

// Private Affinity raster interface; tile layouts and codec details stay local.
#include "core/layer.hpp"
#include "formats/af_container_internal.hpp"
#include "formats/af_tree.hpp"
#include "support/decode_budget.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace patchy::af::detail {

constexpr std::int32_t kMaxLayerSide = 300000;      // matches the PSB dimension cap

struct EmbeddedOriginal {
  PixelBuffer pixels;  // decoded, orientation-applied
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;  // the untouched file
  std::string name;                                        // source path basename
};

struct DecodedBitmap {
  PixelBuffer rgba;          // straight-alpha RGBA8 (empty for mask formats)
  PixelBuffer mask;          // gray8 plane (mask formats only)
  bool approximate_color{false};  // CMYK/Lab converted without an ICC profile
  // Set when the base plane came ENTIRELY from the embedded placed original
  // (every base tile code 5/fill, none hand-painted): the untouched file and
  // its source filename, for the smart-object wrapper.
  std::shared_ptr<const std::vector<std::uint8_t>> original_bytes;
  std::string original_name;
};

struct PlacedRaster {
  PixelBuffer pixels;
  std::int32_t x{0};
  std::int32_t y{0};
};

[[nodiscard]] std::uint32_t tag_of(const std::string& s);

[[nodiscard]] std::optional<EmbeddedOriginal> decode_embedded_original(
    std::span<const std::uint8_t> bytes, const Container& container, const af::AfClass& dybm);

// `max_plane_bytes` caps each channel plane's buffer (kMaxBufferBytes32 models wasm32): a
// plane past it fails as an invalid tile layout.
[[nodiscard]] std::optional<DecodedBitmap> decode_bitmap(std::span<const std::uint8_t> bytes,
                                                         const Container& container,
                                                         const af::AfClass& dybm,
                                                         const std::string& layer_name,
                                                         std::vector<std::string>* notices,
                                                         std::string* fail_reason,
                                                         std::uint64_t max_plane_bytes = kMaxBufferBytes);

[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> layer_origin(
    const std::vector<double>& xfrm);

[[nodiscard]] std::optional<PlacedRaster> resample_affine(const PixelBuffer& source,
                                                          const std::vector<double>& xfrm,
                                                          bool gray_mask);

}  // namespace patchy::af::detail
