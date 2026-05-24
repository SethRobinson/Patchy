#include "core/pixel_buffer.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace photoslop {

PixelFormat PixelFormat::rgb8() {
  return PixelFormat{ColorMode::RGB, BitDepth::UInt8, 3};
}

PixelFormat PixelFormat::rgba8() {
  return PixelFormat{ColorMode::RGB, BitDepth::UInt8, 4};
}

PixelFormat PixelFormat::gray8() {
  return PixelFormat{ColorMode::Grayscale, BitDepth::UInt8, 1};
}

PixelFormat PixelFormat::rgb16() {
  return PixelFormat{ColorMode::RGB, BitDepth::UInt16, 3};
}

PixelFormat PixelFormat::rgbf32() {
  return PixelFormat{ColorMode::RGB, BitDepth::Float32, 3};
}

bool operator==(const PixelFormat& lhs, const PixelFormat& rhs) {
  return lhs.color_mode == rhs.color_mode && lhs.bit_depth == rhs.bit_depth &&
         lhs.channels == rhs.channels;
}

bool operator!=(const PixelFormat& lhs, const PixelFormat& rhs) {
  return !(lhs == rhs);
}

std::size_t bytes_per_channel(BitDepth depth) {
  switch (depth) {
    case BitDepth::UInt8:
      return 1;
    case BitDepth::UInt16:
      return 2;
    case BitDepth::Float32:
      return 4;
  }
  throw std::invalid_argument("Unsupported bit depth");
}

std::size_t bytes_per_pixel(PixelFormat format) {
  if (format.channels == 0) {
    throw std::invalid_argument("Pixel format must have at least one channel");
  }
  return bytes_per_channel(format.bit_depth) * format.channels;
}

PixelBuffer::PixelBuffer(std::int32_t width, std::int32_t height, PixelFormat format)
    : width_(width), height_(height), format_(format) {
  if (width < 0 || height < 0) {
    throw std::invalid_argument("PixelBuffer dimensions cannot be negative");
  }

  const auto pixel_bytes = bytes_per_pixel(format);
  const auto pixel_count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  const auto max_size = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
  if (pixel_count > max_size / pixel_bytes) {
    throw std::overflow_error("PixelBuffer allocation would overflow");
  }

  bytes_.resize(static_cast<std::size_t>(pixel_count) * pixel_bytes);
}

std::int32_t PixelBuffer::width() const noexcept {
  return width_;
}

std::int32_t PixelBuffer::height() const noexcept {
  return height_;
}

PixelFormat PixelBuffer::format() const noexcept {
  return format_;
}

bool PixelBuffer::empty() const noexcept {
  return width_ == 0 || height_ == 0 || bytes_.empty();
}

std::size_t PixelBuffer::byte_size() const noexcept {
  return bytes_.size();
}

std::size_t PixelBuffer::stride_bytes() const {
  return static_cast<std::size_t>(width_) * bytes_per_pixel(format_);
}

std::span<std::uint8_t> PixelBuffer::data() noexcept {
  return std::span<std::uint8_t>(bytes_.data(), bytes_.size());
}

std::span<const std::uint8_t> PixelBuffer::data() const noexcept {
  return std::span<const std::uint8_t>(bytes_.data(), bytes_.size());
}

std::span<std::uint8_t> PixelBuffer::row(std::int32_t y) {
  if (y < 0 || y >= height_) {
    throw std::out_of_range("PixelBuffer row is out of range");
  }
  const auto stride = stride_bytes();
  return std::span<std::uint8_t>(bytes_.data() + static_cast<std::size_t>(y) * stride, stride);
}

std::span<const std::uint8_t> PixelBuffer::row(std::int32_t y) const {
  if (y < 0 || y >= height_) {
    throw std::out_of_range("PixelBuffer row is out of range");
  }
  const auto stride = stride_bytes();
  return std::span<const std::uint8_t>(bytes_.data() + static_cast<std::size_t>(y) * stride, stride);
}

std::uint8_t* PixelBuffer::pixel(std::int32_t x, std::int32_t y) {
  validate_coordinates(x, y);
  const auto offset = static_cast<std::size_t>(y) * stride_bytes() +
                      static_cast<std::size_t>(x) * bytes_per_pixel(format_);
  return bytes_.data() + offset;
}

const std::uint8_t* PixelBuffer::pixel(std::int32_t x, std::int32_t y) const {
  validate_coordinates(x, y);
  const auto offset = static_cast<std::size_t>(y) * stride_bytes() +
                      static_cast<std::size_t>(x) * bytes_per_pixel(format_);
  return bytes_.data() + offset;
}

void PixelBuffer::clear(std::uint8_t value) {
  std::fill(bytes_.begin(), bytes_.end(), value);
}

void PixelBuffer::validate_coordinates(std::int32_t x, std::int32_t y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    throw std::out_of_range("PixelBuffer coordinate is out of range");
  }
}

}  // namespace photoslop
