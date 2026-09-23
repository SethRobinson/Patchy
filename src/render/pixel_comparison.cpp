#include "render/pixel_comparison.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace patchy {

double PixelComparisonReport::differing_fraction() const noexcept {
  if (compared_pixels == 0) {
    return 0.0;
  }
  return static_cast<double>(differing_pixels) / static_cast<double>(compared_pixels);
}

bool PixelComparisonReport::within(const PixelComparisonPolicy& policy) const noexcept {
  return comparable && max_channel_delta <= policy.max_channel_delta &&
         differing_pixels <= policy.max_differing_pixels &&
         mean_abs_channel_delta <= policy.max_mean_abs_channel_delta &&
         differing_fraction() <= policy.max_differing_fraction;
}

PixelComparisonReport compare_pixel_buffers(const PixelBuffer& reference,
                                            const PixelBuffer& candidate) noexcept {
  PixelComparisonReport report;
  if (reference.width() != candidate.width() || reference.height() != candidate.height() ||
      reference.format() != candidate.format() || reference.format().channels < 3 ||
      reference.format().bit_depth != BitDepth::UInt8) {
    return report;
  }

  report.comparable = true;
  report.compared_pixels = static_cast<std::uint64_t>(reference.width()) *
                           static_cast<std::uint64_t>(reference.height());
  report.compared_channels = report.compared_pixels * 3U;
  for (std::int32_t y = 0; y < reference.height(); ++y) {
    for (std::int32_t x = 0; x < reference.width(); ++x) {
      const auto* expected = reference.pixel(x, y);
      const auto* actual = candidate.pixel(x, y);
      bool pixel_differs = false;
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto delta = std::abs(static_cast<int>(expected[channel]) -
                                    static_cast<int>(actual[channel]));
        if (delta != 0) {
          pixel_differs = true;
          ++report.differing_channels;
        }
        report.total_abs_channel_delta += static_cast<std::uint64_t>(delta);
        report.max_channel_delta = std::max(report.max_channel_delta, delta);
      }
      if (pixel_differs) {
        ++report.differing_pixels;
      }
    }
  }
  if (report.compared_channels != 0) {
    report.mean_abs_channel_delta = static_cast<double>(report.total_abs_channel_delta) /
                                    static_cast<double>(report.compared_channels);
  }
  return report;
}

}  // namespace patchy
