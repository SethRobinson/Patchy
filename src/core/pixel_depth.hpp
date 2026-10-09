#pragma once

// Bit-depth primitives for 16-bit and 32-bit documents (docs/high-bit-depth.md).
//
// Every deep computation runs on floats on the "deep scale", where 255 is full scale,
// so formulas written for 8-bit values (255, 128, x / 255) carry over unchanged and
// only the final rounding differs. What a value means follows from the buffer's depth:
//
// - UInt8 and UInt16 buffers hold display-encoded (sRGB) samples. 16-bit values map to
//   the deep scale as v / 257, so 8-bit v and 16-bit v * 257 are the same value.
// - Float32 buffers hold linear light, 1.0 = full scale (Photoshop's 32-bit model),
//   unbounded above for color; alpha and coverage stay within 0..1.
//
// A render or an edit runs in one domain: Encoded for 8 and 16-bit documents, Linear
// for 32-bit documents. Reading a buffer into the other domain applies the sRGB
// transfer to color channels only; alpha, masks and channels are coverage and scale
// linearly at every depth.

#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace patchy {

inline constexpr float kDeepScale = 255.0F;

// The deep-editing gate. Off by default; PATCHY_DEEP_EDITING=1 (or the hidden
// preference, through set_deep_editing_override) turns it on. While it is off, deep
// files convert to 8 bits on open exactly as before.
[[nodiscard]] bool deep_editing_enabled();
void set_deep_editing_override(std::optional<bool> enabled);

// Whether this build edits documents at `depth`. The web build stops at 16 bits (its
// memory budget): 32-bit files open converted to 16 and Image > Mode offers no 32.
[[nodiscard]] constexpr bool depth_supported_on_platform(BitDepth depth) noexcept {
#ifdef __EMSCRIPTEN__
  return depth != BitDepth::Float32;
#else
  static_cast<void>(depth);
  return true;
#endif
}

enum class DeepDomain : std::uint8_t {
  Encoded,  // display-encoded sRGB values on the deep scale (8 and 16-bit documents)
  Linear    // linear light, 255 = 1.0, unbounded (32-bit documents)
};

[[nodiscard]] DeepDomain deep_domain_for(BitDepth document_depth) noexcept;

// Whether a buffer's channels carry color (RGB, RGBA: the sRGB transfer applies to the
// first three channels when crossing domains) or coverage (gray masks, channels,
// alpha: always linear scaling).
enum class SampleKind : std::uint8_t {
  Color,
  Coverage
};

[[nodiscard]] PixelFormat with_bit_depth(PixelFormat format, BitDepth depth) noexcept;

// Exact, deterministic sample conversions.
[[nodiscard]] constexpr std::uint16_t widen_u8_to_u16(std::uint8_t value) noexcept {
  return static_cast<std::uint16_t>(value * 257U);
}
// Round to nearest; the same rule the PSD reader has always used for 16-bit files.
[[nodiscard]] constexpr std::uint8_t narrow_u16_to_u8(std::uint16_t value) noexcept {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) + 128U) / 257U);
}
[[nodiscard]] double srgb_decode(double encoded) noexcept;  // 0..1 encoded -> linear
[[nodiscard]] double srgb_encode(double linear) noexcept;   // linear, clamped to 0..1 -> encoded

// Typed rows. Loads `count` pixels of row `y` from column `x` as straight RGBA on the
// deep scale in `domain` (4 floats per pixel; a 3-channel buffer reads alpha 255, a
// 1-channel buffer reads its value into all four). Stores round and clamp for integer
// depths (encoded values clamp to 0..255) and keep floats as they are (color may
// exceed 255 in the Linear domain; alpha clamps to 0..255).
void load_rgba_row(const PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                   DeepDomain domain, std::span<float> out);
void store_rgba_row(PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                    DeepDomain domain, std::span<const float> in);
// Single-channel coverage (masks, saved channels): values on the deep scale 0..255.
void load_coverage_row(const PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                       std::span<float> out);
void store_coverage_row(PixelBuffer& buffer, std::int32_t y, std::int32_t x, std::int32_t count,
                        std::span<const float> in);

// One sample of a coverage buffer (any depth) as 0..1.
[[nodiscard]] float coverage_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y);
// The alpha of one pixel of a color buffer (any depth) as 0..1; 1 without an alpha channel.
[[nodiscard]] float pixel_alpha_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y);
// One pixel at `px` of a color buffer in `format` (3 or 4 channels, any depth) as
// straight RGBA on the deep scale in the buffer's own domain (no transfer: 16-bit
// values / 257, 32-bit linear values * 255; alpha 255 without an alpha channel), and
// the store back (16 bits rounds and clamps, 32-bit color is kept as is, alpha clamps).
[[nodiscard]] std::array<float, 4> load_pixel(PixelFormat format, const std::uint8_t* px) noexcept;
void store_pixel(PixelFormat format, std::uint8_t* px, const std::array<float, 4>& values) noexcept;
// One pixel of a color buffer (any depth) as display-encoded RGBA bytes: 8-bit samples
// as they are, 16-bit narrowed, 32-bit clamped and sRGB-encoded. For previews.
[[nodiscard]] std::array<std::uint8_t, 4> display_rgba8_at(const PixelBuffer& buffer, std::int32_t x,
                                                         std::int32_t y);

// A copy of `source` at `depth`. Color: 8<->16 exact (v * 257, (v + 128) / 257); to
// 32-bit decodes sRGB; from 32-bit clamps to 0..1 and encodes sRGB (32 -> 8 uses the
// PSD reader's linear_to_srgb8, so a converted document matches an imported one).
// Coverage and the alpha channel scale linearly. Same depth returns a shared copy.
[[nodiscard]] PixelBuffer convert_pixel_buffer_depth(const PixelBuffer& source, BitDepth depth, SampleKind kind);

// The 8-bit-precision path for edits without a deep implementation (16-bit filters,
// Liquify; docs/high-bit-depth.md): `edit` runs on an 8-bit copy of a color buffer and
// every sample it changed moves by the same amount at depth, so untouched samples keep
// their full precision and changed ones land within the 8-bit result's precision.
void apply_eight_bit_edit_at_depth(PixelBuffer& pixels, const std::function<void(PixelBuffer&)>& edit);

}  // namespace patchy
