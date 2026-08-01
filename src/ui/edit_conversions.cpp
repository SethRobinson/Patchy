#include "ui/edit_conversions.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace patchy::ui {

EditColor edit_color(QColor color) {
  return EditColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(std::max(1, color.alpha()))};
}

QImage qimage_from_pixel_buffer(const PixelBuffer& pixels) {
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

}  // namespace patchy::ui
