// Flat TIFF fallback decoder (docs/tiff.md, "Flat fallback decoder"): one image file
// directory decoded without Qt, for the layouts Qt's TIFF plug-in refuses (float data with
// extra samples, planar float, 32-bit integers, ...). Every offset, count and size is
// checked against the file; samples are read in the file's byte order.

#include "psd/psd_tiff.hpp"

#include "core/document.hpp"
#include "core/document_channel.hpp"
#include "core/pixel_depth.hpp"
#include "psd/psd_io_internal.hpp"
#include "psd/psd_tiff_internal.hpp"
#include "support/decode_budget.hpp"
#include "support/translate_noop.hpp"

#include "formats/miniz/miniz.h"
#include "formats/zstd/zstd.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::psd {
namespace {

using namespace tiff_detail;

constexpr std::uint16_t kTagFillOrder = 266;
constexpr std::uint16_t kTagColorMap = 320;
constexpr std::uint16_t kTagTileWidth = 322;
constexpr std::uint16_t kTagTileLength = 323;
constexpr std::uint16_t kTagTileOffsets = 324;
constexpr std::uint16_t kTagTileByteCounts = 325;

constexpr std::uint32_t kMaxSamplesPerPixel = 64;
constexpr std::uint32_t kMaxTileSide = 65536;

[[noreturn]] void throw_damaged() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The TIFF's image data is damaged"));
}

[[noreturn]] void throw_unsupported() {
  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Patchy cannot read this TIFF's sample layout"));
}

// UPacked: 9 to 15-bit samples packed most significant bit first (Layout::bits), widened to
// two bytes on decode.
enum class SampleKind { U8, UPacked, U16, U32, F16, F32, F64 };

std::size_t sample_bytes_of(SampleKind kind) noexcept {
  switch (kind) {
    case SampleKind::U8: return 1;
    case SampleKind::UPacked:
    case SampleKind::U16:
    case SampleKind::F16: return 2;
    case SampleKind::U32:
    case SampleKind::F32: return 4;
    case SampleKind::F64: return 8;
  }
  return 1;
}

float half_to_float(std::uint16_t half) noexcept {
  const auto sign = (half & 0x8000U) != 0U ? -1.0F : 1.0F;
  const auto exponent = static_cast<int>((half >> 10U) & 0x1FU);
  const auto mantissa = static_cast<float>(half & 0x3FFU);
  if (exponent == 0) {
    return sign * std::ldexp(mantissa, -24);
  }
  if (exponent == 31) {
    return mantissa == 0.0F ? sign * std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
  }
  return sign * std::ldexp(1024.0F + mantissa, exponent - 25);
}

// NaN and infinities open as 0, as no editing path expects them.
float finite_or_zero(double value) noexcept {
  return std::isfinite(value) ? static_cast<float>(value) : 0.0F;
}

// ---------------------------------------------------------------------------------------
// Decompression. Each returns the decoded bytes, at most `expected` of them; fewer means
// the data ended early.

std::vector<std::uint8_t> unpack_bits(std::span<const std::uint8_t> in, std::size_t expected) {
  std::vector<std::uint8_t> out;
  out.reserve(expected);
  std::size_t at = 0;
  while (at < in.size() && out.size() < expected) {
    const auto control = static_cast<std::int8_t>(in[at++]);
    if (control >= 0) {
      const auto count = std::min<std::size_t>({static_cast<std::size_t>(control) + 1U, in.size() - at,
                                                expected - out.size()});
      out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(at),
                 in.begin() + static_cast<std::ptrdiff_t>(at + count));
      at += static_cast<std::size_t>(control) + 1U;
    } else if (control != -128) {
      if (at >= in.size()) {
        break;
      }
      const auto count = std::min<std::size_t>(static_cast<std::size_t>(1 - control), expected - out.size());
      out.insert(out.end(), count, in[at++]);
    }
  }
  return out;
}

// TIFF 6.0 LZW: MSB-first codes of 9 to 12 bits, Clear 256, EndOfInformation 257, the code
// width growing one code early.
std::vector<std::uint8_t> decode_lzw(std::span<const std::uint8_t> in, std::size_t expected) {
  constexpr std::uint32_t kClear = 256;
  constexpr std::uint32_t kEnd = 257;
  constexpr std::uint32_t kTableSize = 4096;
  std::vector<std::uint16_t> prefix(kTableSize, 0);
  std::vector<std::uint8_t> suffix(kTableSize, 0);
  std::vector<std::uint16_t> length(kTableSize, 1);
  for (std::uint32_t code = 0; code < 256U; ++code) {
    suffix[code] = static_cast<std::uint8_t>(code);
  }
  std::vector<std::uint8_t> out;
  out.reserve(expected);
  std::uint64_t bit = 0;
  const std::uint64_t total_bits = static_cast<std::uint64_t>(in.size()) * 8U;
  std::uint32_t width = 9;
  std::uint32_t next = 258;
  std::optional<std::uint32_t> previous;
  std::array<std::uint8_t, kTableSize> scratch{};

  // Appends the string for `code`; returns its first byte.
  const auto emit = [&](std::uint32_t code) {
    const auto count = static_cast<std::size_t>(length[code]);
    auto walk = code;
    for (std::size_t index = count; index-- > 0U;) {
      scratch[index] = suffix[walk];
      walk = prefix[walk];
    }
    const auto room = std::min(count, expected - out.size());
    out.insert(out.end(), scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(room));
    return scratch[0];
  };

  while (out.size() < expected && bit + width <= total_bits) {
    std::uint32_t code = 0;
    for (std::uint32_t index = 0; index < width; ++index, ++bit) {
      code = (code << 1U) | ((in[static_cast<std::size_t>(bit >> 3U)] >> (7U - (bit & 7U))) & 1U);
    }
    if (code == kEnd) {
      break;
    }
    if (code == kClear) {
      width = 9;
      next = 258;
      previous.reset();
      continue;
    }
    if (!previous.has_value()) {
      if (code > 255U) {
        break;  // damaged: the first code after a Clear must be a literal
      }
      (void)emit(code);
      previous = code;
      continue;
    }
    std::uint8_t first = 0;
    if (code < next) {
      first = emit(code);
    } else if (code == next && next < kTableSize) {
      // The string being defined: the previous one plus its own first byte.
      auto walk = *previous;
      while (length[walk] > 1U) {
        walk = prefix[walk];
      }
      first = suffix[walk];
      prefix[next] = static_cast<std::uint16_t>(*previous);
      suffix[next] = first;
      length[next] = static_cast<std::uint16_t>(length[*previous] + 1U);
      (void)emit(next);
      ++next;
      previous = code;
      if (next + 1U >= (1U << width) && width < 12U) {
        ++width;
      }
      continue;
    } else {
      break;  // damaged
    }
    if (next < kTableSize) {
      prefix[next] = static_cast<std::uint16_t>(*previous);
      suffix[next] = first;
      length[next] = static_cast<std::uint16_t>(length[*previous] + 1U);
      ++next;
    }
    previous = code;
    if (next + 1U >= (1U << width) && width < 12U) {
      ++width;
    }
  }
  return out;
}

std::vector<std::uint8_t> inflate(std::span<const std::uint8_t> in, std::size_t expected) {
  std::vector<std::uint8_t> out(expected);
  mz_stream stream{};
  if (mz_inflateInit(&stream) != MZ_OK) {
    throw_damaged();
  }
  stream.next_in = in.data();
  stream.avail_in = static_cast<unsigned int>(std::min<std::size_t>(in.size(), std::numeric_limits<unsigned int>::max()));
  stream.next_out = out.data();
  stream.avail_out = static_cast<unsigned int>(std::min<std::size_t>(expected, std::numeric_limits<unsigned int>::max()));
  while (stream.avail_out > 0U) {
    const auto status = mz_inflate(&stream, MZ_SYNC_FLUSH);
    if (status != MZ_OK) {
      break;  // the end of the stream, or damage: keep what decoded
    }
  }
  out.resize(static_cast<std::size_t>(stream.total_out));
  mz_inflateEnd(&stream);
  return out;
}

// Zstandard (50000; 34926 in older lists): frame after frame into at most `expected` bytes.
// Each frame decodes straight into the output, so no window buffer is allocated whatever
// window the frame header asks for; a frame that fails to decode ends the segment.
std::vector<std::uint8_t> decode_zstd(std::span<const std::uint8_t> in, std::size_t expected) {
  std::vector<std::uint8_t> out(expected);
  std::size_t produced = 0;
  auto* context = ZSTD_createDCtx();
  if (context == nullptr) {
    throw_damaged();
  }
  while (!in.empty() && produced < expected) {
    const auto frame = ZSTD_findFrameCompressedSize(in.data(), in.size());
    if (ZSTD_isError(frame) != 0U || frame == 0U || frame > in.size()) {
      break;
    }
    const auto written = ZSTD_decompressDCtx(context, out.data() + produced, expected - produced, in.data(), frame);
    if (ZSTD_isError(written) != 0U) {
      break;
    }
    produced += written;
    in = in.subspan(frame);
  }
  ZSTD_freeDCtx(context);
  out.resize(produced);
  return out;
}

// ---------------------------------------------------------------------------------------

struct Layout {
  bool little{false};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t samples{1};        // per pixel
  std::uint32_t color_samples{1};  // 1 (gray, palette) or 3 (RGB)
  std::uint32_t photometric{1};
  SampleKind kind{SampleKind::U8};
  std::uint32_t bits{8};  // BitsPerSample
  std::size_t sample_bytes{1};
  bool planar{false};
  std::uint32_t compression{1};
  std::uint32_t predictor{1};
  std::vector<std::uint32_t> extra_types;  // ExtraSamples, one per extra sample
};

// One row of `bits`-wide samples (9 to 15; packed most significant bit first, the row
// starting on a byte) widened to two bytes each in the file's byte order, still on the
// file's scale.
void unpack_packed_row(std::span<const std::uint8_t> packed, std::span<std::uint8_t> out, std::uint32_t bits,
                       bool little) {
  const auto count = out.size() / 2U;
  const auto mask = (1U << bits) - 1U;
  for (std::size_t index = 0; index < count; ++index) {
    const auto bit = index * bits;
    const auto byte = bit / 8U;
    if (byte >= packed.size()) {
      break;
    }
    // A sample of at most 15 bits starting anywhere in a byte spans at most three bytes.
    std::uint32_t window = 0;
    for (std::size_t at = byte; at < byte + 3U; ++at) {
      window = (window << 8U) | (at < packed.size() ? static_cast<std::uint32_t>(packed[at]) : 0U);
    }
    const auto value = (window >> (24U - bits - static_cast<std::uint32_t>(bit % 8U))) & mask;
    out[index * 2U + (little ? 0U : 1U)] = static_cast<std::uint8_t>(value & 0xFFU);
    out[index * 2U + (little ? 1U : 0U)] = static_cast<std::uint8_t>(value >> 8U);
  }
}

// Predictor 2 (horizontal differencing) on one row in the file's byte order: integer
// addition at the sample width, `stride` samples apart.
void undo_horizontal_differencing(std::span<std::uint8_t> row, std::size_t stride, std::size_t sample_bytes,
                                  bool little) {
  const auto values = row.size() / sample_bytes;
  const auto get = [&](std::size_t index) {
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < sample_bytes; ++byte) {
      const auto shift = little ? 8U * byte : 8U * (sample_bytes - 1U - byte);
      value |= static_cast<std::uint64_t>(row[index * sample_bytes + byte]) << shift;
    }
    return value;
  };
  const auto put = [&](std::size_t index, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sample_bytes; ++byte) {
      const auto shift = little ? 8U * byte : 8U * (sample_bytes - 1U - byte);
      row[index * sample_bytes + byte] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
  };
  for (std::size_t index = stride; index < values; ++index) {
    put(index, get(index) + get(index - stride));
  }
}

// Predictor 3 (floating point): bytes differenced `stride` apart, then each sample's bytes
// split into planes, most significant first. Restores the file's byte order.
void undo_floating_point_prediction(std::span<std::uint8_t> row, std::size_t stride, std::size_t sample_bytes,
                                    bool little) {
  for (std::size_t index = stride; index < row.size(); ++index) {
    row[index] = static_cast<std::uint8_t>(row[index] + row[index - stride]);
  }
  const std::vector<std::uint8_t> planes(row.begin(), row.end());
  const auto values = row.size() / sample_bytes;
  for (std::size_t index = 0; index < values; ++index) {
    for (std::size_t byte = 0; byte < sample_bytes; ++byte) {
      const auto plane = little ? sample_bytes - 1U - byte : byte;
      row[index * sample_bytes + byte] = planes[plane * values + index];
    }
  }
}

struct Segment {
  std::uint32_t left{0};
  std::uint32_t top{0};
  std::uint32_t width{0};   // decoded columns (a tile's full width)
  std::uint32_t rows{0};    // decoded rows
  std::uint32_t plane{0};   // the sample a planar segment holds
};

class Decoder {
 public:
  Decoder(const TiffBytes& tiff, const Directory& directory, Layout layout)
      : tiff_(tiff), directory_(directory), layout_(std::move(layout)) {}

  // The image as chunky samples in the file's byte order (width x height x samples).
  std::vector<std::uint8_t> decode(bool& damaged) {
    const auto pixel_bytes = static_cast<std::uint64_t>(layout_.samples) * layout_.sample_bytes;
    const auto size = checked_decode_bytes(layout_.width, layout_.height, pixel_bytes, tiff_.size());
    if (!size.has_value()) {
      throw_damaged();
    }
    image_.assign(*size, 0);
    const bool tiled = directory_.find(kTagTileOffsets) != nullptr;
    const auto* offsets = directory_.find(tiled ? kTagTileOffsets : kTagStripOffsets);
    const auto* counts = directory_.find(tiled ? kTagTileByteCounts : kTagStripByteCounts);
    if (offsets == nullptr || offsets->count == 0U) {
      throw_damaged();
    }
    std::uint32_t segment_width = layout_.width;
    std::uint32_t segment_height = 0;
    std::uint64_t across = 1;
    if (tiled) {
      segment_width = unsigned_value(tiff_, directory_.find(kTagTileWidth)).value_or(0U);
      segment_height = unsigned_value(tiff_, directory_.find(kTagTileLength)).value_or(0U);
      if (segment_width == 0U || segment_height == 0U || segment_width > kMaxTileSide || segment_height > kMaxTileSide) {
        throw_damaged();
      }
      across = (static_cast<std::uint64_t>(layout_.width) + segment_width - 1U) / segment_width;
    } else {
      segment_height = unsigned_value(tiff_, directory_.find(kTagRowsPerStrip)).value_or(layout_.height);
      segment_height = std::clamp<std::uint32_t>(segment_height, 1U, layout_.height);
    }
    const auto down = (static_cast<std::uint64_t>(layout_.height) + segment_height - 1U) / segment_height;
    const auto planes = layout_.planar ? layout_.samples : 1U;
    const auto per_plane = across * down;
    const auto expected_segments = per_plane * planes;
    // Segments the offsets list lacks stay empty; the list's length is bounded by the file.
    if (offsets->count < expected_segments) {
      damaged = true;
    }
    const auto listed = std::min<std::uint64_t>(expected_segments, offsets->count);
    for (std::uint64_t index = 0; index < listed; ++index) {
      const auto plane = static_cast<std::uint32_t>(index / per_plane);
      const auto within = index % per_plane;
      Segment segment;
      segment.plane = plane;
      segment.left = static_cast<std::uint32_t>((within % across) * segment_width);
      segment.top = static_cast<std::uint32_t>((within / across) * segment_height);
      segment.width = segment_width;
      segment.rows = tiled ? segment_height : std::min(segment_height, layout_.height - segment.top);
      const auto offset = unsigned_value(tiff_, offsets, static_cast<std::uint32_t>(index)).value_or(0U);
      std::optional<std::uint32_t> count;
      if (counts != nullptr && index < counts->count) {
        count = unsigned_value(tiff_, counts, static_cast<std::uint32_t>(index));
      }
      if (!decode_segment(segment, offset, count)) {
        damaged = true;
      }
    }
    return std::move(image_);
  }

 private:
  // False when the segment's data was missing or ended early (the rest stays zero).
  bool decode_segment(const Segment& segment, std::uint32_t offset, std::optional<std::uint32_t> count) {
    const auto segment_samples = layout_.planar ? 1U : layout_.samples;
    const auto row_samples = static_cast<std::uint64_t>(segment.width) * segment_samples;
    const bool packed = layout_.kind == SampleKind::UPacked;
    auto row_bytes = checked_buffer_bytes(row_samples, packed ? layout_.bits : layout_.sample_bytes);
    if (packed && row_bytes.has_value()) {
      *row_bytes = (*row_bytes + 7U) / 8U;
    }
    const auto expected = row_bytes.has_value() ? checked_buffer_bytes(*row_bytes, segment.rows) : std::nullopt;
    if (!expected.has_value()) {
      throw_damaged();
    }
    if (!count.has_value()) {
      if (layout_.compression != 1U) {
        return false;
      }
      count = static_cast<std::uint32_t>(std::min<std::size_t>(*expected, std::numeric_limits<std::uint32_t>::max()));
    }
    bool complete = true;
    auto available = static_cast<std::uint64_t>(*count);
    if (offset >= tiff_.size()) {
      return false;
    }
    if (!tiff_.fits(offset, available)) {
      available = tiff_.size() - offset;
      complete = false;
    }
    const auto encoded = tiff_.span(offset, static_cast<std::size_t>(available));
    // A compressed segment's decoded size must be reachable from its bytes before it is
    // allocated (docs/psd-reader-hardening.md, "Decode budget").
    if (layout_.compression != 1U && !decoded_size_plausible(*expected, encoded.size())) {
      return false;
    }
    std::vector<std::uint8_t> data;
    switch (layout_.compression) {
      case 1:
        data.assign(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(std::min(encoded.size(), *expected)));
        break;
      case 5:
        data = decode_lzw(encoded, *expected);
        break;
      case 8:
      case 32946:
        data = inflate(encoded, *expected);
        break;
      case 32773:
        data = unpack_bits(encoded, *expected);
        break;
      case 34926:
      case 50000:
        data = decode_zstd(encoded, *expected);
        break;
      default:
        throw_unsupported();
    }
    if (data.size() < *expected) {
      complete = false;
    }
    // Whole rows only: a partial last row stays zero.
    const auto rows = static_cast<std::uint32_t>(data.size() / *row_bytes);
    std::vector<std::uint8_t> widened(packed ? static_cast<std::size_t>(row_samples) * 2U : 0U);
    for (std::uint32_t row = 0; row < rows; ++row) {
      auto bytes = std::span<std::uint8_t>(data).subspan(static_cast<std::size_t>(row) * *row_bytes, *row_bytes);
      if (layout_.predictor == 2U) {
        undo_horizontal_differencing(bytes, segment_samples, layout_.sample_bytes, layout_.little);
      } else if (layout_.predictor == 3U) {
        undo_floating_point_prediction(bytes, segment_samples, layout_.sample_bytes, layout_.little);
      }
      const auto y = static_cast<std::uint64_t>(segment.top) + row;
      if (y >= layout_.height) {
        break;
      }
      if (packed) {
        unpack_packed_row(bytes, widened, layout_.bits, layout_.little);
        place_row(widened, segment, static_cast<std::uint32_t>(y));
        continue;
      }
      place_row(bytes, segment, static_cast<std::uint32_t>(y));
    }
    return complete;
  }

  void place_row(std::span<const std::uint8_t> row, const Segment& segment, std::uint32_t y) {
    if (segment.left >= layout_.width) {
      return;
    }
    const auto columns = std::min(segment.width, layout_.width - segment.left);
    const auto sample_bytes = layout_.sample_bytes;
    const auto pixel_bytes = static_cast<std::size_t>(layout_.samples) * sample_bytes;
    auto* target = image_.data() + (static_cast<std::size_t>(y) * layout_.width + segment.left) * pixel_bytes;
    if (!layout_.planar) {
      std::memcpy(target, row.data(), static_cast<std::size_t>(columns) * pixel_bytes);
      return;
    }
    target += static_cast<std::size_t>(segment.plane) * sample_bytes;
    for (std::uint32_t x = 0; x < columns; ++x) {
      std::memcpy(target + static_cast<std::size_t>(x) * pixel_bytes, row.data() + static_cast<std::size_t>(x) * sample_bytes,
                  sample_bytes);
    }
  }

  const TiffBytes& tiff_;
  const Directory& directory_;
  Layout layout_;
  std::vector<std::uint8_t> image_;
};

// One sample in the document's storage scale: 8-bit and 16-bit integers as they are,
// 32-bit integers narrowed to 16 bits, floats as floats.
struct SampleReader {
  SampleKind kind;
  std::uint32_t bits;
  bool little;

  [[nodiscard]] std::uint64_t raw(const std::uint8_t* at, std::size_t width) const noexcept {
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < width; ++byte) {
      const auto shift = little ? 8U * byte : 8U * (width - 1U - byte);
      value |= static_cast<std::uint64_t>(at[byte]) << shift;
    }
    return value;
  }
  [[nodiscard]] std::uint32_t integer(const std::uint8_t* at) const noexcept {
    switch (kind) {
      case SampleKind::U8: return at[0];
      case SampleKind::UPacked: {
        // Scaled to the full 16-bit range, rounding to nearest (12-bit: (v * 65535 + 2047) / 4095).
        const auto max = (std::uint64_t{1} << bits) - 1U;
        return static_cast<std::uint32_t>((raw(at, 2) * 65535U + max / 2U) / max);
      }
      case SampleKind::U16: return static_cast<std::uint32_t>(raw(at, 2));
      case SampleKind::U32:
        return static_cast<std::uint32_t>((raw(at, 4) * 65535U + 2147483647U) / 4294967295U);
      default: return 0;
    }
  }
  [[nodiscard]] float real(const std::uint8_t* at) const noexcept {
    switch (kind) {
      case SampleKind::F16: return finite_or_zero(half_to_float(static_cast<std::uint16_t>(raw(at, 2))));
      case SampleKind::F32: return finite_or_zero(std::bit_cast<float>(static_cast<std::uint32_t>(raw(at, 4))));
      case SampleKind::F64: return finite_or_zero(std::bit_cast<double>(raw(at, 8)));
      default: return 0.0F;
    }
  }
};

Layout read_layout(const TiffBytes& tiff, const Directory& directory) {
  Layout layout;
  layout.little = tiff.little_endian();
  const auto width = unsigned_value(tiff, directory.find(kTagImageWidth));
  const auto height = unsigned_value(tiff, directory.find(kTagImageLength));
  if (!width.has_value() || !height.has_value() || *width == 0U || *height == 0U) {
    throw_damaged();
  }
  if (*width > static_cast<std::uint32_t>(kMaxPsbDimension) || *height > static_cast<std::uint32_t>(kMaxPsbDimension)) {
    throw_unsupported();
  }
  layout.width = *width;
  layout.height = *height;
  layout.samples = unsigned_value(tiff, directory.find(kTagSamplesPerPixel)).value_or(1U);
  if (layout.samples == 0U || layout.samples > kMaxSamplesPerPixel) {
    throw_unsupported();
  }
  // Every sample shares one width and one format.
  const auto* bits_entry = directory.find(kTagBitsPerSample);
  const auto bits = unsigned_value(tiff, bits_entry).value_or(1U);
  const auto* format_entry = directory.find(kTagSampleFormat);
  const auto format = unsigned_value(tiff, format_entry).value_or(1U);
  for (std::uint32_t sample = 1; sample < layout.samples; ++sample) {
    if ((bits_entry != nullptr && sample < bits_entry->count && unsigned_value(tiff, bits_entry, sample) != bits) ||
        (format_entry != nullptr && sample < format_entry->count && unsigned_value(tiff, format_entry, sample) != format)) {
      throw_unsupported();
    }
  }
  if (format == 1U && bits == 8U) {
    layout.kind = SampleKind::U8;
  } else if (format == 1U && bits >= 9U && bits <= 15U) {
    layout.kind = SampleKind::UPacked;
  } else if (format == 1U && bits == 16U) {
    layout.kind = SampleKind::U16;
  } else if (format == 1U && bits == 32U) {
    layout.kind = SampleKind::U32;
  } else if (format == 3U && bits == 16U) {
    layout.kind = SampleKind::F16;
  } else if (format == 3U && bits == 32U) {
    layout.kind = SampleKind::F32;
  } else if (format == 3U && bits == 64U) {
    layout.kind = SampleKind::F64;
  } else {
    throw_unsupported();
  }
  layout.bits = bits;
  layout.sample_bytes = sample_bytes_of(layout.kind);
  layout.photometric = unsigned_value(tiff, directory.find(kTagPhotometric)).value_or(layout.samples >= 3U ? 2U : 1U);
  switch (layout.photometric) {
    case 0:
    case 1: layout.color_samples = 1; break;
    case 2: layout.color_samples = 3; break;
    case 3:
      if (layout.kind != SampleKind::U8) {
        throw_unsupported();
      }
      layout.color_samples = 1;
      break;
    default: throw_unsupported();
  }
  if (layout.samples < layout.color_samples) {
    throw_damaged();
  }
  layout.planar = unsigned_value(tiff, directory.find(kTagPlanarConfiguration)).value_or(1U) == 2U;
  layout.compression = unsigned_value(tiff, directory.find(kTagCompression)).value_or(1U);
  if (layout.compression != 1U && layout.compression != 5U && layout.compression != 8U &&
      layout.compression != 32946U && layout.compression != 32773U && layout.compression != 34926U &&
      layout.compression != 50000U) {
    throw_unsupported();
  }
  layout.predictor = unsigned_value(tiff, directory.find(kTagPredictor)).value_or(1U);
  if (layout.compression == 1U || layout.compression == 32773U || layout.predictor > 3U) {
    layout.predictor = 1U;  // no predictor applies to uncompressed or PackBits data
  }
  if (layout.kind == SampleKind::UPacked && layout.predictor != 1U) {
    throw_unsupported();  // differencing is defined at 8, 16, 32 and 64 bits only (libtiff too)
  }
  if (unsigned_value(tiff, directory.find(kTagFillOrder)).value_or(1U) != 1U) {
    throw_unsupported();  // bit-reversed bytes
  }
  const auto* extra_entry = directory.find(kTagExtraSamples);
  for (std::uint32_t sample = layout.color_samples; sample < layout.samples; ++sample) {
    const auto index = sample - layout.color_samples;
    layout.extra_types.push_back(
        extra_entry != nullptr && index < extra_entry->count ? unsigned_value(tiff, extra_entry, index).value_or(0U) : 0U);
  }
  return layout;
}

BitDepth document_depth_for(SampleKind kind) noexcept {
  switch (kind) {
    case SampleKind::U8: return BitDepth::UInt8;
    case SampleKind::UPacked:
    case SampleKind::U16:
    case SampleKind::U32: return BitDepth::UInt16;
    default: return BitDepth::Float32;
  }
}

double ppi_from(const TiffBytes& tiff, const Directory& directory, std::uint16_t tag) {
  const auto value = rational_value(tiff, directory.find(tag));
  const auto unit = unsigned_value(tiff, directory.find(kTagResolutionUnit)).value_or(2U);
  if (!value.has_value() || unit == 1U) {
    return 72.0;  // untagged: Photoshop's default, as for other flat imports
  }
  return std::clamp(*value * (unit == 3U ? 2.54 : 1.0), 1.0, 30000.0);
}

}  // namespace

Document read_flat_tiff(std::span<const std::uint8_t> bytes, const FlatTiffReadOptions& options) {
  const auto little = tiff_little_endian(bytes);
  if (!little.has_value()) {
    throw_unsupported();  // not a TIFF, or BigTIFF
  }
  const TiffBytes tiff(bytes, *little);
  const auto directory = read_directory(tiff, options.directory_offset != 0U ? options.directory_offset : tiff.u32(4));
  if (!directory.has_value()) {
    throw_damaged();
  }
  const auto layout = read_layout(tiff, *directory);

  // Palette: the color map holds 2^8 entries per component, 16 bits each.
  std::array<std::array<std::uint8_t, 3>, 256> palette{};
  if (layout.photometric == 3U) {
    const auto* map = directory->find(kTagColorMap);
    if (map == nullptr || map->type != 3 || map->count < 3U * 256U) {
      throw_damaged();
    }
    for (std::uint32_t index = 0; index < 256U; ++index) {
      for (std::uint32_t component = 0; component < 3U; ++component) {
        palette[index][component] =
            narrow_u16_to_u8(static_cast<std::uint16_t>(*unsigned_value(tiff, map, component * 256U + index)));
      }
    }
  }

  bool damaged = false;
  const auto image = Decoder(tiff, *directory, layout).decode(damaged);

  // The first associated (1) or unassociated (2) extra sample is the alpha; every other
  // extra sample becomes a saved alpha channel, as Photoshop opens them.
  std::optional<std::uint32_t> alpha;
  for (std::uint32_t index = 0; index < layout.extra_types.size(); ++index) {
    if (layout.extra_types[index] == 1U || layout.extra_types[index] == 2U) {
      alpha = index;
      break;
    }
  }
  const bool associated = alpha.has_value() && layout.extra_types[*alpha] == 1U;
  std::vector<std::uint32_t> channel_samples;
  bool dropped_channels = false;
  for (std::uint32_t index = 0; index < layout.extra_types.size(); ++index) {
    if (alpha.has_value() && index == *alpha) {
      continue;
    }
    if (3U + (alpha.has_value() ? 1U : 0U) + channel_samples.size() >= kMaximumPhotoshopChannelCount) {
      dropped_channels = true;
      continue;
    }
    channel_samples.push_back(layout.color_samples + index);
  }

  const auto depth = document_depth_for(layout.kind);
  const auto width = static_cast<std::int32_t>(layout.width);
  const auto height = static_cast<std::int32_t>(layout.height);
  const auto format = with_bit_depth(alpha.has_value() ? PixelFormat::rgba8() : PixelFormat::rgb8(), depth);
  PixelBuffer pixels(width, height, format);
  std::vector<PixelBuffer> channels;
  for (std::size_t index = 0; index < channel_samples.size(); ++index) {
    channels.emplace_back(width, height, with_bit_depth(PixelFormat::gray8(), depth));
  }

  const SampleReader reader{layout.kind, layout.bits, layout.little};
  const auto sample_bytes = layout.sample_bytes;
  const auto pixel_bytes = static_cast<std::size_t>(layout.samples) * sample_bytes;
  const auto out_channels = static_cast<std::size_t>(format.channels);
  const bool invert = layout.photometric == 0U;
  const auto alpha_sample = alpha.has_value() ? layout.color_samples + *alpha : 0U;
  const double integer_max = depth == BitDepth::UInt8 ? 255.0 : 65535.0;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* source_row = image.data() + static_cast<std::size_t>(y) * layout.width * pixel_bytes;
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* source = source_row + static_cast<std::size_t>(x) * pixel_bytes;
      const auto sample_at = [&](std::uint32_t sample) { return source + static_cast<std::size_t>(sample) * sample_bytes; };
      if (depth == BitDepth::Float32) {
        std::array<float, 4> value{0.0F, 0.0F, 0.0F, 1.0F};
        for (std::uint32_t component = 0; component < 3U; ++component) {
          auto sample = reader.real(sample_at(layout.color_samples == 3U ? component : 0U));
          value[component] = invert ? 1.0F - sample : sample;
        }
        if (alpha.has_value()) {
          value[3] = std::clamp(reader.real(sample_at(alpha_sample)), 0.0F, 1.0F);
          if (associated) {
            for (std::uint32_t component = 0; component < 3U; ++component) {
              value[component] = value[3] > 0.0F ? value[component] / value[3] : 0.0F;
            }
          }
        }
        std::memcpy(row.data() + static_cast<std::size_t>(x) * out_channels * 4U, value.data(), out_channels * 4U);
        for (std::size_t index = 0; index < channel_samples.size(); ++index) {
          const auto coverage = std::clamp(reader.real(sample_at(channel_samples[index])), 0.0F, 1.0F);
          std::memcpy(channels[index].row(y).data() + static_cast<std::size_t>(x) * 4U, &coverage, 4U);
        }
        continue;
      }
      std::array<std::uint32_t, 4> value{0, 0, 0, static_cast<std::uint32_t>(integer_max)};
      if (layout.photometric == 3U) {
        const auto& entry = palette[reader.integer(sample_at(0))];
        value = {entry[0], entry[1], entry[2], value[3]};
      } else {
        for (std::uint32_t component = 0; component < 3U; ++component) {
          const auto sample = reader.integer(sample_at(layout.color_samples == 3U ? component : 0U));
          value[component] = invert ? static_cast<std::uint32_t>(integer_max) - sample : sample;
        }
      }
      if (alpha.has_value()) {
        value[3] = reader.integer(sample_at(alpha_sample));
        if (associated) {
          for (std::uint32_t component = 0; component < 3U; ++component) {
            value[component] =
                value[3] == 0U
                    ? 0U
                    : static_cast<std::uint32_t>(std::min(integer_max, std::round(value[component] * integer_max / value[3])));
          }
        }
      }
      for (std::size_t component = 0; component < out_channels; ++component) {
        if (depth == BitDepth::UInt8) {
          row[static_cast<std::size_t>(x) * out_channels + component] = static_cast<std::uint8_t>(value[component]);
        } else {
          const auto sample = static_cast<std::uint16_t>(value[component]);
          std::memcpy(row.data() + (static_cast<std::size_t>(x) * out_channels + component) * 2U, &sample, 2U);
        }
      }
      for (std::size_t index = 0; index < channel_samples.size(); ++index) {
        const auto coverage = reader.integer(sample_at(channel_samples[index]));
        auto channel_row = channels[index].row(y);
        if (depth == BitDepth::UInt8) {
          channel_row[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(coverage);
        } else {
          const auto sample = static_cast<std::uint16_t>(coverage);
          std::memcpy(channel_row.data() + static_cast<std::size_t>(x) * 2U, &sample, 2U);
        }
      }
    }
  }

  Document document(width, height, format);
  document.color_state().bit_depth = depth;
  document.print_settings().horizontal_ppi = ppi_from(tiff, *directory, kTagXResolution);
  document.print_settings().vertical_ppi = ppi_from(tiff, *directory, kTagYResolution);
  document.add_pixel_layer(options.layer_name, std::move(pixels));
  for (std::size_t index = 0; index < channels.size(); ++index) {
    document.add_channel(DocumentChannel(document.allocate_channel_id(), "Alpha " + std::to_string(index + 1U),
                                         DocumentChannelKind::Alpha, std::move(channels[index])));
  }
  if (options.notices != nullptr) {
    if (damaged) {
      options.notices->push_back(PATCHY_TRANSLATE_NOOP(
          "QObject", "The TIFF's image data is cut short or damaged; the missing part opened empty."));
    }
    if (dropped_channels) {
      options.notices->push_back(PATCHY_TRANSLATE_NOOP(
          "QObject", "Some of the TIFF's extra channels were left out: a document keeps at most 56 channels."));
    }
  }
  return document;
}

Document read_flat_tiff_file(const std::filesystem::path& path, const FlatTiffReadOptions& options) {
  const auto bytes = read_file_bytes(path);
  return read_flat_tiff(bytes, options);
}

}  // namespace patchy::psd
