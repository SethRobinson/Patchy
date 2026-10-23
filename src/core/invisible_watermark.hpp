#pragma once

#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Detection of the open invisible watermarks Stable Diffusion's reference scripts and
// SDXL embed (File > File Info's Watermarks page; docs/file-info.md). Patchy-owned
// reimplementation of the decoding side of the MIT-licensed invisible-watermark
// library's "dwtDct" method (docs/legal-constraints.md, "Watermark detection"):
// integer math only, deterministic on every toolchain, no model. Nothing here
// embeds or removes a watermark.
namespace patchy {

// One payload Patchy knows. `id` is a permanent scripting identifier.
struct KnownInvisibleWatermark {
  std::string_view id;    // "stableDiffusion1", "stableDiffusion2", "stableDiffusionXL"
  std::string_view name;  // display name (a product name, not translated)
  std::vector<std::uint8_t> bits;  // 0/1, in embedding order
};

[[nodiscard]] const std::vector<KnownInvisibleWatermark>& known_invisible_watermarks();

struct InvisibleWatermarkMatch {
  std::string_view id;
  std::string_view name;
  int bit_count{0};
  int differing_bits{0};       // Hamming distance between the decoded bits and the payload
  bool red_blue_swapped{false};  // matched with the red and blue channels exchanged
};

struct InvisibleWatermarkScan {
  bool scanned{false};  // false when the image is under 256 x 256 pixels (the library's minimum)
  std::vector<InvisibleWatermarkMatch> matches;
};

// The most differing bits a payload of `bit_count` bits may show and still count as
// found: one in twelve, rounded down (docs/file-info.md explains the choice).
[[nodiscard]] int invisible_watermark_tolerance(int bit_count) noexcept;

// How the per-bit average of block scores becomes a bit. Library is EmbedMaxDct's
// `mean * 255 > 127`. TwoCluster puts the threshold where the averages split into two
// groups: a marked 1 reads 1 in nearly every block while a marked 0 reads 1 in about
// half of them (the embedding can lower the block's largest coefficient below another
// one), so the library's fixed midpoint misreads many 0 bits (docs/file-info.md).
enum class DwtDctBitRule : std::uint8_t { Library, TwoCluster };

struct DwtDctDecodeOptions {
  // Read the red and blue channels exchanged (a caller that gave the library RGB as BGR).
  bool red_blue_swapped{false};
  // Measure U from OpenCV's float chroma zero (127.5) instead of 0: SDXL embeds in
  // float BGR, where U is centered on 0.5 and coefficients can be negative, while the
  // SD 1.x/2.x scripts embed in 8 bits, where U is centered on 128.
  bool centered_chroma{false};
  DwtDctBitRule rule{DwtDctBitRule::Library};
};

// Decodes `bit_count` bits from 8-bit interleaved RGB or RGBA rows (`channels` 3 or 4)
// the way the library's EmbedMaxDct.decode does: OpenCV's integer BGR-to-YUV, a level-1
// Haar approximation of U, and per 4 x 4 block the largest non-DC coefficient's
// position within its quantization step, averaged per bit. Empty when the image is too
// small.
[[nodiscard]] std::vector<std::uint8_t> decode_dwt_dct_bits(std::span<const std::uint8_t> pixels, int width,
                                                            int height, std::size_t stride, int channels,
                                                            int bit_count, const DwtDctDecodeOptions& options = {});

// Every known payload in all eight readings (both channel orders, both chroma
// origins, both bit rules); a payload counts as found when any reading's bits are
// within the tolerance.
[[nodiscard]] InvisibleWatermarkScan scan_invisible_watermarks(std::span<const std::uint8_t> pixels, int width,
                                                               int height, std::size_t stride, int channels);
// An 8-bit RGB or RGBA buffer (Compositor::flatten_rgb8's output).
[[nodiscard]] InvisibleWatermarkScan scan_invisible_watermarks(const PixelBuffer& rgb8);

}  // namespace patchy
