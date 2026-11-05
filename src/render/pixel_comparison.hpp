#pragma once

#include "core/pixel_buffer.hpp"

#include <cstddef>
#include <cstdint>

namespace patchy {

struct PixelComparisonPolicy {
  // Display-preview shaders may eventually differ by a small integer amount;
  // export and byte-identity paths should keep this at zero.
  int max_channel_delta{0};
  std::uint64_t max_differing_pixels{0};
  double max_mean_abs_channel_delta{0.0};
  double max_differing_fraction{0.0};
};

struct PixelComparisonReport {
  bool comparable{false};
  std::uint64_t compared_pixels{0};
  std::uint64_t compared_channels{0};
  std::uint64_t differing_pixels{0};
  std::uint64_t differing_channels{0};
  std::uint64_t total_abs_channel_delta{0};
  int max_channel_delta{0};
  double mean_abs_channel_delta{0.0};

  [[nodiscard]] double differing_fraction() const noexcept;
  [[nodiscard]] bool within(const PixelComparisonPolicy& policy = {}) const noexcept;
};

// Compares two byte-addressable pixel buffers without involving Qt or a GPU.
// A format mismatch is reported as non-comparable instead of being coerced.
[[nodiscard]] PixelComparisonReport compare_pixel_buffers(const PixelBuffer& reference,
                                                          const PixelBuffer& candidate) noexcept;

}  // namespace patchy
