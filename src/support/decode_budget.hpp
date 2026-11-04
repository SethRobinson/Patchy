#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

// Untrusted-input sizing for file readers (docs/psd-reader-hardening.md, "Decode budget").
// A reader sizes buffers from dimensions the file declares, so a few bytes can ask for
// terabytes. Every reader that allocates from declared dimensions checks two things first:
//
// 1. The product fits: width x height x bytes is formed in 64 bits with overflow checks and
//    capped at kMaxDecodePixels, so later size_t arithmetic on it (byte offsets up to 16
//    bytes per pixel) cannot wrap, including on a 32-bit size_t (wasm32).
// 2. The file can describe it: decoded sample bytes may exceed the encoded bytes behind
//    them by at most kMaxDecodeExpansion (deflate's 1032:1 ceiling, which also covers
//    PackBits at 64:1 and Radiance RLE), with a fixed allowance for small images. Damaged
//    but honest files always pass; a tiny file declaring a huge image does not.
//
// A failed check rejects through the reader's normal error, so the open fails cleanly.

namespace patchy {

// Deflate emits at most 258 bytes per match and needs at least about 2 bits per match, so a
// valid stream expands at most 1032:1. PackBits (64:1), PCX RLE (31:1) and Radiance RLE
// (about 64:1 per channel) stay below it.
inline constexpr std::uint64_t kMaxDecodeExpansion = 1032;

// Decoded bytes any image may have regardless of its encoded size (a 4096 x 4096 RGBA8
// image). Readers that allocate for image parts with no data behind them (a PSD layer whose
// channels are empty) charge them against one such allowance per file, not one per part.
inline constexpr std::uint64_t kDecodeAllowanceBytes = 64ULL << 20U;

// Pixel caps for a single image or plane. 2^28 on a 32-bit size_t keeps pixel_count x 16
// representable; 64-bit builds use 2^36, far past anything that can be opened, so products
// stay in range there too.
inline constexpr std::uint64_t kMaxDecodePixels32 = 1ULL << 28U;
inline constexpr std::uint64_t kMaxDecodePixels64 = 1ULL << 36U;
inline constexpr std::uint64_t kMaxDecodePixels =
    sizeof(std::size_t) >= 8 ? kMaxDecodePixels64 : kMaxDecodePixels32;

// The largest buffer a reader sizes: no std::vector holds more than PTRDIFF_MAX bytes, which
// is 2 GiB - 1 on a 32-bit size_t (wasm32). Tests pass kMaxBufferBytes32 to model wasm32.
inline constexpr std::uint64_t kMaxBufferBytes32 = (1ULL << 31U) - 1U;
inline constexpr std::uint64_t kMaxBufferBytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());

// a x b, or nothing when the product overflows 64 bits.
[[nodiscard]] constexpr std::optional<std::uint64_t> checked_multiply(std::uint64_t a, std::uint64_t b) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
    return std::nullopt;
  }
  return a * b;
}

// count x element_bytes as a buffer length in bytes, or nothing when the product overflows
// or passes max_bytes. Readers form every buffer size from file-supplied counts through
// this rather than multiplying in size_t, which wraps on wasm32 and sizes a short buffer.
[[nodiscard]] constexpr std::optional<std::size_t> checked_buffer_bytes(
    std::uint64_t count, std::uint64_t element_bytes, std::uint64_t max_bytes = kMaxBufferBytes) noexcept {
  const auto bytes = checked_multiply(count, element_bytes);
  if (!bytes.has_value() || *bytes > max_bytes ||
      *bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*bytes);
}

// width x height, or nothing when either side is negative or the count passes max_pixels.
// Tests pass kMaxDecodePixels32 to check the 32-bit behavior on a 64-bit host.
[[nodiscard]] constexpr std::optional<std::uint64_t> checked_pixel_count(
    std::int64_t width, std::int64_t height, std::uint64_t max_pixels = kMaxDecodePixels) noexcept {
  if (width < 0 || height < 0) {
    return std::nullopt;
  }
  const auto count = checked_multiply(static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height));
  if (!count.has_value() || *count > max_pixels) {
    return std::nullopt;
  }
  return count;
}

// True when `encoded_bytes` of file data can decode to `decoded_bytes` at `max_expansion`,
// or the decoded size is within `allowance`.
[[nodiscard]] constexpr bool decoded_size_plausible(std::uint64_t decoded_bytes, std::uint64_t encoded_bytes,
                                                    std::uint64_t max_expansion = kMaxDecodeExpansion,
                                                    std::uint64_t allowance = kDecodeAllowanceBytes) noexcept {
  if (decoded_bytes <= allowance) {
    return true;
  }
  const auto reachable = checked_multiply(encoded_bytes, max_expansion);
  return !reachable.has_value() || decoded_bytes <= *reachable;
}

// Both checks for one image or plane of width x height x bytes_per_pixel decoded from
// encoded_bytes. Returns the decoded byte count (which fits size_t) or nothing.
[[nodiscard]] constexpr std::optional<std::size_t> checked_decode_bytes(
    std::int64_t width, std::int64_t height, std::uint64_t bytes_per_pixel, std::uint64_t encoded_bytes,
    std::uint64_t max_expansion = kMaxDecodeExpansion, std::uint64_t allowance = kDecodeAllowanceBytes,
    std::uint64_t max_pixels = kMaxDecodePixels) noexcept {
  const auto pixels = checked_pixel_count(width, height, max_pixels);
  const auto bytes = pixels.has_value() ? checked_multiply(*pixels, bytes_per_pixel) : std::nullopt;
  if (!bytes.has_value() || *bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
      !decoded_size_plausible(*bytes, encoded_bytes, max_expansion, allowance)) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*bytes);
}

// A running allowance for decoded bytes that no encoded data stands behind. charge() takes
// from it and reports false (taking nothing) once the total would pass the allowance.
class DecodeAllowance {
public:
  explicit constexpr DecodeAllowance(std::uint64_t bytes = kDecodeAllowanceBytes) noexcept : remaining_(bytes) {}

  [[nodiscard]] constexpr bool charge(std::uint64_t bytes) noexcept {
    if (bytes > remaining_) {
      return false;
    }
    remaining_ -= bytes;
    return true;
  }

  [[nodiscard]] constexpr std::uint64_t remaining() const noexcept { return remaining_; }

private:
  std::uint64_t remaining_;
};

}  // namespace patchy
