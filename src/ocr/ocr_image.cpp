#include "ocr/ocr_image.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace patchy::ocr {

namespace {

// One source pixel as blue, green, red over white.
inline std::array<std::uint32_t, 3> flattened_bgr(const std::uint8_t* pixel, PixelLayout layout) noexcept {
  const auto over_white = [](std::uint32_t value, std::uint32_t alpha) {
    return (value * alpha + 255U * (255U - alpha) + 127U) / 255U;
  };
  switch (layout) {
    case PixelLayout::Gray8:
      return {pixel[0], pixel[0], pixel[0]};
    case PixelLayout::Rgb8:
      return {pixel[2], pixel[1], pixel[0]};
    case PixelLayout::Rgba8:
      return {over_white(pixel[2], pixel[3]), over_white(pixel[1], pixel[3]), over_white(pixel[0], pixel[3])};
    case PixelLayout::Bgra8:
      return {over_white(pixel[0], pixel[3]), over_white(pixel[1], pixel[3]), over_white(pixel[2], pixel[3])};
  }
  return {255U, 255U, 255U};
}

// The source range [first, last) that scaled index `index` averages: at least one pixel.
inline std::pair<std::int64_t, std::int64_t> source_span(std::int64_t index, std::int64_t source,
                                                         std::int64_t scaled) noexcept {
  const std::int64_t first = std::min(source - 1, index * source / scaled);
  const std::int64_t last = std::clamp((index + 1) * source / scaled, first + 1, source);
  return {first, last};
}

}  // namespace

ImageView BgraImage::view() const noexcept {
  return ImageView{pixels.data(), width, height, static_cast<std::ptrdiff_t>(width) * 4, PixelLayout::Bgra8};
}

BgraImage scaled_rows_bgra(const ImageView& source, std::int32_t scaled_width, std::int32_t scaled_height,
                           std::int32_t top, std::int32_t rows) {
  if (source.data == nullptr || source.width <= 0 || source.height <= 0 || scaled_width <= 0 ||
      scaled_height <= 0 || scaled_width > source.width || scaled_height > source.height || top < 0 ||
      rows <= 0 || top > scaled_height - rows) {
    throw std::invalid_argument("text recognition was given an empty or badly sized image");
  }
  const int channels = channel_count(source.layout);
  BgraImage out;
  out.width = scaled_width;
  out.height = rows;
  out.pixels.resize(static_cast<std::size_t>(scaled_width) * static_cast<std::size_t>(rows) * 4U);

  std::vector<std::pair<std::int64_t, std::int64_t>> columns(static_cast<std::size_t>(scaled_width));
  for (std::int32_t x = 0; x < scaled_width; ++x) {
    columns[static_cast<std::size_t>(x)] = source_span(x, source.width, scaled_width);
  }
  const bool same_size = scaled_width == source.width && scaled_height == source.height;
  for (std::int32_t row = 0; row < rows; ++row) {
    std::uint8_t* target = out.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(scaled_width) * 4U;
    if (same_size) {
      const std::uint8_t* line = source.data + static_cast<std::ptrdiff_t>(top + row) * source.stride;
      for (std::int32_t x = 0; x < scaled_width; ++x) {
        const auto bgr = flattened_bgr(line + static_cast<std::ptrdiff_t>(x) * channels, source.layout);
        target[0] = static_cast<std::uint8_t>(bgr[0]);
        target[1] = static_cast<std::uint8_t>(bgr[1]);
        target[2] = static_cast<std::uint8_t>(bgr[2]);
        target[3] = 255;
        target += 4;
      }
      continue;
    }
    const auto [first_row, last_row] = source_span(static_cast<std::int64_t>(top) + row, source.height, scaled_height);
    for (std::int32_t x = 0; x < scaled_width; ++x) {
      const auto [first_column, last_column] = columns[static_cast<std::size_t>(x)];
      std::array<std::uint64_t, 3> sum{0, 0, 0};
      for (std::int64_t sy = first_row; sy < last_row; ++sy) {
        const std::uint8_t* line = source.data + static_cast<std::ptrdiff_t>(sy) * source.stride;
        for (std::int64_t sx = first_column; sx < last_column; ++sx) {
          const auto bgr = flattened_bgr(line + static_cast<std::ptrdiff_t>(sx) * channels, source.layout);
          sum[0] += bgr[0];
          sum[1] += bgr[1];
          sum[2] += bgr[2];
        }
      }
      const auto count = static_cast<std::uint64_t>((last_row - first_row) * (last_column - first_column));
      target[0] = static_cast<std::uint8_t>((sum[0] + count / 2U) / count);
      target[1] = static_cast<std::uint8_t>((sum[1] + count / 2U) / count);
      target[2] = static_cast<std::uint8_t>((sum[2] + count / 2U) / count);
      target[3] = 255;
      target += 4;
    }
  }
  return out;
}

}  // namespace patchy::ocr
