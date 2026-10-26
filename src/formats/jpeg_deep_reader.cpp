#include "formats/jpeg_deep_reader.hpp"

#include "formats/jpeg_deep_glue.h"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <optional>
#include <stdexcept>

namespace patchy::jpeg_deep {

namespace {

// The same pixel cap as the JPEG XL and AVIF readers.
constexpr std::uint64_t kMaxPixels = 268'435'456ULL;

struct Frame {
  std::uint8_t marker{0};
  int precision{0};
};

// The first SOF marker and its precision byte, walking the marker segments after SOI.
[[nodiscard]] std::optional<Frame> first_frame(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < 4 || bytes[0] != 0xff || bytes[1] != 0xd8) {
    return std::nullopt;
  }
  std::size_t offset = 2;
  while (offset + 1 < bytes.size()) {
    if (bytes[offset] != 0xff) {
      return std::nullopt;
    }
    while (offset + 1 < bytes.size() && bytes[offset + 1] == 0xff) {
      ++offset;  // fill bytes
    }
    if (offset + 1 >= bytes.size()) {
      return std::nullopt;
    }
    const auto marker = bytes[offset + 1];
    if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) {
      offset += 2;  // standalone markers
      continue;
    }
    if (marker == 0xd9 || marker == 0xda || offset + 4 >= bytes.size()) {
      return std::nullopt;  // EOI or scan data before any frame header
    }
    const auto length = static_cast<std::size_t>(bytes[offset + 2]) << 8U | bytes[offset + 3];
    if (length < 2) {
      return std::nullopt;
    }
    if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
      return Frame{marker, bytes[offset + 4]};
    }
    offset += 2 + length;
  }
  return std::nullopt;
}

[[nodiscard]] bool is_lossless(std::uint8_t marker) noexcept {
  return marker == 0xc3 || marker == 0xcb;
}

struct AllocContext {
  std::vector<std::uint16_t>* samples{nullptr};
  PatchyJpegDeepHeader header{};
};

unsigned short* allocate_samples(void* context, const PatchyJpegDeepHeader* header) {
  auto* alloc = static_cast<AllocContext*>(context);
  alloc->header = *header;
  try {
    alloc->samples->assign(static_cast<std::size_t>(header->width) * header->height *
                               static_cast<std::size_t>(header->components),
                           0);
  } catch (const std::bad_alloc&) {
    return nullptr;
  } catch (const std::length_error&) {
    return nullptr;
  }
  return alloc->samples->data();
}

// Full-range (JFIF) YCbCr to RGB on 0..max integers, for lossless files, whose color
// libjpeg-turbo leaves alone.
void ycc_to_rgb(std::vector<std::uint16_t>& samples, int max) noexcept {
  const double half = static_cast<double>(max + 1) / 2.0;
  for (std::size_t i = 0; i + 2 < samples.size(); i += 3) {
    const double y = samples[i];
    const double cb = samples[i + 1] - half;
    const double cr = samples[i + 2] - half;
    const auto clamp = [max](double v) {
      return static_cast<std::uint16_t>(std::clamp(std::lround(v), 0L, static_cast<long>(max)));
    };
    samples[i] = clamp(y + 1.402 * cr);
    samples[i + 1] = clamp(y - 0.344136 * cb - 0.714136 * cr);
    samples[i + 2] = clamp(y + 1.772 * cb);
  }
}

}  // namespace

bool needs_deep_decoder(std::span<const std::uint8_t> bytes) noexcept {
  const auto frame = first_frame(bytes);
  if (!frame.has_value()) {
    return false;
  }
  // Baseline, extended, progressive and lossless; Huffman or arithmetic. Hierarchical
  // frames (SOF5-7, SOF13-15) are not decoded by libjpeg-turbo either.
  constexpr std::array<std::uint8_t, 7> kDecodable{0xc0, 0xc1, 0xc2, 0xc3, 0xc9, 0xca, 0xcb};
  if (std::find(kDecodable.begin(), kDecodable.end(), frame->marker) == kDecodable.end()) {
    return false;
  }
  return is_lossless(frame->marker) || frame->precision != 8;
}

Image read_deep_jpeg(std::span<const std::uint8_t> bytes) {
  const auto frame = first_frame(bytes);
  if (!frame.has_value()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG file is damaged or truncated"));
  }
  Image image;
  AllocContext context;
  context.samples = &image.samples;
  std::array<char, 256> message{};
  const int status =
      patchy_jpeg_deep_decode(bytes.data(), bytes.size(), is_lossless(frame->marker) ? 1 : 0, kMaxPixels,
                              allocate_samples, &context, message.data(), message.size());
  switch (status) {
    case PATCHY_JPEG_DEEP_OK:
      break;
    case PATCHY_JPEG_DEEP_CMYK:
      throw std::runtime_error(
          PATCHY_TRANSLATE_NOOP("QObject", "This JPEG stores CMYK samples deeper than 8 bits, which Patchy cannot open"));
    case PATCHY_JPEG_DEEP_NO_MEMORY:
    case PATCHY_JPEG_DEEP_TOO_LARGE:
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG image is too large to open"));
    default:
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The JPEG file is damaged or truncated"));
  }
  const auto& header = context.header;
  image.width = static_cast<int>(header.width);
  image.height = static_cast<int>(header.height);
  image.channels = header.components;
  image.precision = header.precision;
  image.lossless = header.lossless != 0;
  image.output_bits = header.precision <= 8 ? 8 : 16;
  const int file_max = (1 << header.precision) - 1;
  if (header.ycc != 0) {
    ycc_to_rgb(image.samples, file_max);
  }
  const std::uint64_t out_max = image.output_bits == 8 ? 255U : 65535U;
  if (static_cast<std::uint64_t>(file_max) != out_max) {
    const auto divisor = static_cast<std::uint64_t>(file_max);
    for (auto& sample : image.samples) {
      const auto value = std::min<std::uint64_t>(sample, divisor);
      sample = static_cast<std::uint16_t>((value * out_max + divisor / 2U) / divisor);
    }
  }
  return image;
}

}  // namespace patchy::jpeg_deep
