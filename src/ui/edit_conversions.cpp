#include "ui/edit_conversions.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace patchy::ui {

EditColor edit_color(QColor color) {
  return EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(std::max(1, color.alpha()))};
}

QImage qimage_from_pixel_buffer(const PixelBuffer& pixels) {
  if (!pixels.empty() && pixels.format().channels >= 3 && pixels.format().bit_depth != BitDepth::UInt8) {
    // 16/32-bit: the same values in a deep QImage (straight alpha), no transfer.
    const bool linear = pixels.format().bit_depth == BitDepth::Float32;
    QImage deep(pixels.width(), pixels.height(), linear ? QImage::Format_RGBA32FPx4 : QImage::Format_RGBA64);
    std::vector<float> row(static_cast<std::size_t>(pixels.width()) * 4U);
    for (int y = 0; y < pixels.height(); ++y) {
      load_rgba_row(pixels, y, 0, pixels.width(), linear ? DeepDomain::Linear : DeepDomain::Encoded, row);
      if (linear) {
        auto* dst = reinterpret_cast<float*>(deep.scanLine(y));
        for (std::size_t i = 0; i < row.size(); ++i) {
          dst[i] = row[i] / 255.0F;
        }
      } else {
        auto* dst = reinterpret_cast<std::uint16_t*>(deep.scanLine(y));
        for (std::size_t i = 0; i < row.size(); ++i) {
          dst[i] = static_cast<std::uint16_t>(std::clamp(row[i] * 257.0F + 0.5F, 0.0F, 65535.0F));
        }
      }
    }
    return deep;
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return image;
  }

  // Scanline copies instead of per-pixel QColor round-trips: RGBA8888 stores
  // bytes in R,G,B,A order, matching the buffer's leading channels. This runs
  // over the whole layer buffer at transform/warp session start.
  const int width = pixels.width();
  const std::size_t channels = pixels.format().channels;
  for (int y = 0; y < pixels.height(); ++y) {
    const auto src = pixels.row(y);
    auto* dst = image.scanLine(y);
    if (channels == 4U) {
      std::memcpy(dst, src.data(), static_cast<std::size_t>(width) * 4U);
    } else if (channels == 3U) {
      for (int x = 0; x < width; ++x) {
        dst[x * 4 + 0] = src[static_cast<std::size_t>(x) * 3U + 0U];
        dst[x * 4 + 1] = src[static_cast<std::size_t>(x) * 3U + 1U];
        dst[x * 4 + 2] = src[static_cast<std::size_t>(x) * 3U + 2U];
        dst[x * 4 + 3] = 255U;
      }
    } else {
      // Wider formats (extra channels beyond RGBA) keep a per-pixel copy of
      // the leading four bytes; the source stride is `channels`, not 4.
      for (int x = 0; x < width; ++x) {
        std::memcpy(dst + static_cast<std::size_t>(x) * 4U,
                    src.data() + static_cast<std::size_t>(x) * channels, 4U);
      }
    }
  }
  return image;
}

QImage display_qimage_from_pixel_buffer(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().channels < 3 || pixels.format().bit_depth == BitDepth::UInt8) {
    return qimage_from_pixel_buffer(pixels);
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  std::vector<float> row(static_cast<std::size_t>(pixels.width()) * 4U);
  for (int y = 0; y < pixels.height(); ++y) {
    load_rgba_row(pixels, y, 0, pixels.width(), DeepDomain::Encoded, row);
    auto* dst = image.scanLine(y);
    for (std::size_t i = 0; i < row.size(); ++i) {
      dst[i] = static_cast<std::uint8_t>(std::clamp(row[i] + 0.5F, 0.0F, 255.0F));
    }
  }
  return image;
}

QImage grayscale_qimage_from_pixel_buffer(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 1) {
    return {};
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_Grayscale8);
  const int width = pixels.width();
  const std::size_t channels = pixels.format().channels;
  for (int y = 0; y < pixels.height(); ++y) {
    const auto src = pixels.row(y);
    auto* dst = image.scanLine(y);
    if (channels == 1U) {
      std::memcpy(dst, src.data(), static_cast<std::size_t>(width));
    } else {
      // A multi-channel buffer is not a mask; keep the leading channel so the
      // result stays defined instead of reading past the row.
      for (int x = 0; x < width; ++x) {
        dst[x] = src[static_cast<std::size_t>(x) * channels];
      }
    }
  }
  return image;
}

}  // namespace patchy::ui
