#include "filters/filter_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace photoslop {

namespace {

void require_uint8(PixelBuffer& pixels) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument("Starter built-in filters support UInt8 buffers only");
  }
}

std::uint8_t clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void invert(PixelBuffer& pixels) {
  require_uint8(pixels);
  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto color_channels = std::min<std::uint16_t>(channels, 3);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
      }
    }
  }
}

void brightness_plus(PixelBuffer& pixels) {
  require_uint8(pixels);
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = clamp_byte(static_cast<int>(px[channel]) + 24);
      }
    }
  }
}

void contrast_plus(PixelBuffer& pixels) {
  require_uint8(pixels);
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        const auto value = static_cast<int>((static_cast<float>(px[channel]) - 128.0F) * 1.25F + 128.0F);
        px[channel] = clamp_byte(value);
      }
    }
  }
}

void grayscale(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto luminance = static_cast<std::uint8_t>((static_cast<int>(px[0]) * 30 +
                                                       static_cast<int>(px[1]) * 59 +
                                                       static_cast<int>(px[2]) * 11) /
                                                      100);
      px[0] = luminance;
      px[1] = luminance;
      px[2] = luminance;
    }
  }
}

void desaturate(PixelBuffer& pixels) {
  grayscale(pixels);
}

void auto_contrast(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.empty()) {
    return;
  }

  std::array<int, 3> min_channel = {255, 255, 255};
  std::array<int, 3> max_channel = {0, 0, 0};
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        min_channel[channel] = std::min(min_channel[channel], static_cast<int>(px[channel]));
        max_channel[channel] = std::max(max_channel[channel], static_cast<int>(px[channel]));
      }
    }
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        const auto range = max_channel[channel] - min_channel[channel];
        if (range <= 0) {
          continue;
        }
        px[channel] = clamp_byte(((static_cast<int>(px[channel]) - min_channel[channel]) * 255) / range);
      }
    }
  }
}

void sepia(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto r = static_cast<int>(px[0]);
      const auto g = static_cast<int>(px[1]);
      const auto b = static_cast<int>(px[2]);
      px[0] = clamp_byte((r * 393 + g * 769 + b * 189) / 1000);
      px[1] = clamp_byte((r * 349 + g * 686 + b * 168) / 1000);
      px[2] = clamp_byte((r * 272 + g * 534 + b * 131) / 1000);
    }
  }
}

void threshold(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto luminance =
          (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[2]) * 11) / 100;
      const auto value = luminance >= 128 ? 255 : 0;
      px[0] = static_cast<std::uint8_t>(value);
      px[1] = static_cast<std::uint8_t>(value);
      px[2] = static_cast<std::uint8_t>(value);
    }
  }
}

void posterize(PixelBuffer& pixels) {
  require_uint8(pixels);
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = static_cast<std::uint8_t>((static_cast<int>(px[channel]) / 64) * 85);
      }
    }
  }
}

void box_blur(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  const auto original = pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      std::array<int, 3> sum{0, 0, 0};
      int count = 0;
      for (std::int32_t yy = std::max<std::int32_t>(0, y - 1); yy <= std::min(pixels.height() - 1, y + 1); ++yy) {
        for (std::int32_t xx = std::max<std::int32_t>(0, x - 1); xx <= std::min(pixels.width() - 1, x + 1); ++xx) {
          const auto* src = original.pixel(xx, yy);
          sum[0] += src[0];
          sum[1] += src[1];
          sum[2] += src[2];
          ++count;
        }
      }
      auto* dst = pixels.pixel(x, y);
      dst[0] = static_cast<std::uint8_t>(sum[0] / count);
      dst[1] = static_cast<std::uint8_t>(sum[1] / count);
      dst[2] = static_cast<std::uint8_t>(sum[2] / count);
    }
  }
}

void sharpen(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  const auto original = pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        const auto center = static_cast<int>(original.pixel(x, y)[channel]) * 5;
        const auto left = x > 0 ? static_cast<int>(original.pixel(x - 1, y)[channel]) : static_cast<int>(original.pixel(x, y)[channel]);
        const auto right =
            x + 1 < pixels.width() ? static_cast<int>(original.pixel(x + 1, y)[channel]) : static_cast<int>(original.pixel(x, y)[channel]);
        const auto up = y > 0 ? static_cast<int>(original.pixel(x, y - 1)[channel]) : static_cast<int>(original.pixel(x, y)[channel]);
        const auto down =
            y + 1 < pixels.height() ? static_cast<int>(original.pixel(x, y + 1)[channel]) : static_cast<int>(original.pixel(x, y)[channel]);
        dst[channel] = clamp_byte(center - left - right - up - down);
      }
    }
  }
}

void gaussian_blur(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  constexpr std::array<int, 5> weights = {1, 4, 6, 4, 1};
  const auto original = pixels;
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        int sum = 0;
        for (int ky = -2; ky <= 2; ++ky) {
          const auto sy = std::clamp<std::int32_t>(y + ky, 0, pixels.height() - 1);
          for (int kx = -2; kx <= 2; ++kx) {
            const auto sx = std::clamp<std::int32_t>(x + kx, 0, pixels.width() - 1);
            sum += static_cast<int>(original.pixel(sx, sy)[channel]) *
                   weights[static_cast<std::size_t>(kx + 2)] * weights[static_cast<std::size_t>(ky + 2)];
          }
        }
        dst[channel] = clamp_byte((sum + 128) / 256);
      }
    }
  }
}

void edge_detect(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  constexpr std::array<int, 9> sobel_x = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
  constexpr std::array<int, 9> sobel_y = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
  const auto original = pixels;
  const auto luminance_at = [&original, &pixels](std::int32_t x, std::int32_t y) {
    x = std::clamp<std::int32_t>(x, 0, pixels.width() - 1);
    y = std::clamp<std::int32_t>(y, 0, pixels.height() - 1);
    const auto* px = original.pixel(x, y);
    return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[2]) * 11) / 100;
  };

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      int gx = 0;
      int gy = 0;
      int index = 0;
      for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
          const auto luminance = luminance_at(x + kx, y + ky);
          gx += luminance * sobel_x[static_cast<std::size_t>(index)];
          gy += luminance * sobel_y[static_cast<std::size_t>(index)];
          ++index;
        }
      }
      const auto magnitude = clamp_byte(static_cast<int>(std::lround(std::sqrt(gx * gx + gy * gy))));
      auto* dst = pixels.pixel(x, y);
      dst[0] = magnitude;
      dst[1] = magnitude;
      dst[2] = magnitude;
    }
  }
}

void emboss(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  constexpr std::array<int, 9> kernel = {-2, -1, 0, -1, 0, 1, 0, 1, 2};
  const auto original = pixels;
  const auto luminance_at = [&original, &pixels](std::int32_t x, std::int32_t y) {
    x = std::clamp<std::int32_t>(x, 0, pixels.width() - 1);
    y = std::clamp<std::int32_t>(y, 0, pixels.height() - 1);
    const auto* px = original.pixel(x, y);
    return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[2]) * 11) / 100;
  };

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      int relief = 128;
      int index = 0;
      for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
          relief += luminance_at(x + kx, y + ky) * kernel[static_cast<std::size_t>(index)];
          ++index;
        }
      }
      const auto value = clamp_byte(relief);
      auto* dst = pixels.pixel(x, y);
      dst[0] = value;
      dst[1] = value;
      dst[2] = value;
    }
  }
}

void pixelate(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  constexpr std::int32_t kBlockSize = 4;
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t block_y = 0; block_y < pixels.height(); block_y += kBlockSize) {
    for (std::int32_t block_x = 0; block_x < pixels.width(); block_x += kBlockSize) {
      const auto block_width = std::min(kBlockSize, pixels.width() - block_x);
      const auto block_height = std::min(kBlockSize, pixels.height() - block_y);
      const auto count = std::max<std::int32_t>(1, block_width * block_height);
      std::array<int, 3> sum = {0, 0, 0};
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          const auto* px = pixels.pixel(x, y);
          for (std::uint16_t channel = 0; channel < channels; ++channel) {
            sum[static_cast<std::size_t>(channel)] += px[channel];
          }
        }
      }
      std::array<std::uint8_t, 3> average = {0, 0, 0};
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        average[static_cast<std::size_t>(channel)] = clamp_byte(sum[static_cast<std::size_t>(channel)] / count);
      }
      for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
        for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
          auto* px = pixels.pixel(x, y);
          for (std::uint16_t channel = 0; channel < channels; ++channel) {
            px[channel] = average[static_cast<std::size_t>(channel)];
          }
        }
      }
    }
  }
}

std::uint32_t coordinate_hash(std::int32_t x, std::int32_t y, std::uint16_t channel) noexcept {
  auto value = static_cast<std::uint32_t>(x + 1) * 73856093U;
  value ^= static_cast<std::uint32_t>(y + 1) * 19349663U;
  value ^= static_cast<std::uint32_t>(channel + 1) * 83492791U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

void film_grain(PixelBuffer& pixels) {
  require_uint8(pixels);
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        const auto grain = static_cast<int>(coordinate_hash(x, y, channel) % 31U) - 15;
        px[channel] = clamp_byte(static_cast<int>(px[channel]) + grain);
      }
    }
  }
}

void vignette(PixelBuffer& pixels) {
  require_uint8(pixels);
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  const auto center_x = (static_cast<float>(pixels.width()) - 1.0F) * 0.5F;
  const auto center_y = (static_cast<float>(pixels.height()) - 1.0F) * 0.5F;
  const auto max_distance = std::sqrt(center_x * center_x + center_y * center_y);
  if (max_distance <= 0.0F) {
    return;
  }

  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<float>(x) - center_x;
      const auto dy = static_cast<float>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy) / max_distance;
      const auto darken = 1.0F - 0.55F * std::clamp(distance * distance, 0.0F, 1.0F);
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = clamp_byte(static_cast<int>(std::lround(static_cast<float>(px[channel]) * darken)));
      }
    }
  }
}

}  // namespace

void register_builtin_filters(FilterRegistry& registry) {
  registry.register_filter({"photoslop.filters.invert", "Invert", invert});
  registry.register_filter({"photoslop.filters.brightness_plus", "Brightness +24", brightness_plus});
  registry.register_filter({"photoslop.filters.contrast_plus", "Contrast +25%", contrast_plus});
  registry.register_filter({"photoslop.filters.grayscale", "Grayscale", grayscale});
  registry.register_filter({"photoslop.filters.desaturate", "Desaturate", desaturate});
  registry.register_filter({"photoslop.filters.auto_contrast", "Auto Contrast", auto_contrast});
  registry.register_filter({"photoslop.filters.sepia", "Sepia", sepia});
  registry.register_filter({"photoslop.filters.threshold", "Threshold", threshold});
  registry.register_filter({"photoslop.filters.posterize", "Posterize", posterize});
  registry.register_filter({"photoslop.filters.box_blur", "Box Blur", box_blur});
  registry.register_filter({"photoslop.filters.sharpen", "Sharpen", sharpen});
  registry.register_filter({"photoslop.filters.gaussian_blur", "Gaussian Blur", gaussian_blur});
  registry.register_filter({"photoslop.filters.edge_detect", "Edge Detect", edge_detect});
  registry.register_filter({"photoslop.filters.emboss", "Emboss", emboss});
  registry.register_filter({"photoslop.filters.pixelate", "Pixelate 4px", pixelate});
  registry.register_filter({"photoslop.filters.film_grain", "Film Grain", film_grain});
  registry.register_filter({"photoslop.filters.vignette", "Vignette", vignette});
}

}  // namespace photoslop
