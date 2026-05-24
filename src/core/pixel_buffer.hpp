#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace photoslop {

enum class ColorMode {
  Grayscale,
  RGB,
  CMYK,
  Lab
};

enum class BitDepth {
  UInt8 = 8,
  UInt16 = 16,
  Float32 = 32
};

struct PixelFormat {
  ColorMode color_mode{ColorMode::RGB};
  BitDepth bit_depth{BitDepth::UInt8};
  std::uint16_t channels{3};

  static PixelFormat rgb8();
  static PixelFormat rgba8();
  static PixelFormat gray8();
  static PixelFormat rgb16();
  static PixelFormat rgbf32();
};

bool operator==(const PixelFormat& lhs, const PixelFormat& rhs);
bool operator!=(const PixelFormat& lhs, const PixelFormat& rhs);

std::size_t bytes_per_channel(BitDepth depth);
std::size_t bytes_per_pixel(PixelFormat format);

class PixelBuffer {
public:
  PixelBuffer() = default;
  PixelBuffer(std::int32_t width, std::int32_t height, PixelFormat format);

  [[nodiscard]] std::int32_t width() const noexcept;
  [[nodiscard]] std::int32_t height() const noexcept;
  [[nodiscard]] PixelFormat format() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t byte_size() const noexcept;
  [[nodiscard]] std::size_t stride_bytes() const;

  [[nodiscard]] std::span<std::uint8_t> data() noexcept;
  [[nodiscard]] std::span<const std::uint8_t> data() const noexcept;
  [[nodiscard]] std::span<std::uint8_t> row(std::int32_t y);
  [[nodiscard]] std::span<const std::uint8_t> row(std::int32_t y) const;

  [[nodiscard]] std::uint8_t* pixel(std::int32_t x, std::int32_t y);
  [[nodiscard]] const std::uint8_t* pixel(std::int32_t x, std::int32_t y) const;

  void clear(std::uint8_t value = 0);

private:
  void validate_coordinates(std::int32_t x, std::int32_t y) const;

  std::int32_t width_{0};
  std::int32_t height_{0};
  PixelFormat format_{};
  std::vector<std::uint8_t> bytes_;
};

}  // namespace photoslop
