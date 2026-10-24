#include "formats/apng_writer.hpp"

#include "formats/miniz/miniz.h"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

namespace patchy::apng {
namespace {

// Compressed data longer than this is split over several IDAT / fdAT chunks.
constexpr std::size_t kMaxChunkData = std::size_t{1} << 20;

void append_be16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// Length, type, data, then the CRC of type and data.
void append_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data) {
  append_be32(out, static_cast<std::uint32_t>(data.size()));
  const auto crc_start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const auto crc = mz_crc32(MZ_CRC32_INIT, out.data() + crc_start, out.size() - crc_start);
  append_be32(out, static_cast<std::uint32_t>(crc));
}

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

// One filtered row: filter type byte, then the filtered samples (4 bytes per pixel).
void filter_row(std::uint8_t type, const std::uint8_t* row, const std::uint8_t* prior, std::size_t length,
                std::uint8_t* out) {
  for (std::size_t i = 0; i < length; ++i) {
    const int a = i >= 4 ? row[i - 4] : 0;
    const int b = prior != nullptr ? prior[i] : 0;
    const int c = (i >= 4 && prior != nullptr) ? prior[i - 4] : 0;
    int predicted = 0;
    switch (type) {
      case 1: predicted = a; break;
      case 2: predicted = b; break;
      case 3: predicted = (a + b) / 2; break;
      case 4: predicted = paeth(a, b, c); break;
      default: break;
    }
    out[i] = static_cast<std::uint8_t>(row[i] - predicted);
  }
}

// The rows of one frame rectangle, each with the filter whose output has the smallest
// sum of absolute signed values (the PNG specification's recommended heuristic), then
// zlib-compressed.
std::vector<std::uint8_t> compress_rect(const std::vector<std::uint8_t>& canvas, std::int32_t canvas_width,
                                        std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  const auto stride = static_cast<std::size_t>(canvas_width) * 4;
  const auto length = static_cast<std::size_t>(width) * 4;
  std::vector<std::uint8_t> raw;
  raw.reserve((length + 1) * static_cast<std::size_t>(height));
  std::vector<std::uint8_t> candidate(length);
  std::vector<std::uint8_t> best(length);
  for (std::int32_t row_index = 0; row_index < height; ++row_index) {
    const auto* row = canvas.data() + static_cast<std::size_t>(y + row_index) * stride + static_cast<std::size_t>(x) * 4;
    const auto* prior = row_index > 0 ? row - stride : nullptr;
    std::uint64_t best_cost = std::numeric_limits<std::uint64_t>::max();
    std::uint8_t best_type = 0;
    for (std::uint8_t type = 0; type <= 4; ++type) {
      filter_row(type, row, prior, length, candidate.data());
      std::uint64_t cost = 0;
      for (const auto value : candidate) {
        cost += value < 128 ? value : 256U - value;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_type = type;
        best.swap(candidate);
      }
    }
    raw.push_back(best_type);
    raw.insert(raw.end(), best.begin(), best.end());
  }
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<std::uint8_t> compressed(bound);
  if (mz_compress2(compressed.data(), &bound, raw.data(), static_cast<mz_ulong>(raw.size()), MZ_DEFAULT_LEVEL) !=
      MZ_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Could not encode animated PNG frame."));
  }
  compressed.resize(bound);
  return compressed;
}

// fcTL keeps the delay as a 16-bit fraction of a second.
struct DelayFraction {
  std::uint16_t num{};
  std::uint16_t den{};
};

DelayFraction delay_fraction(std::uint32_t delay_ms) {
  if (delay_ms <= 0xFFFF) return {static_cast<std::uint16_t>(delay_ms), std::uint16_t{1000}};
  if (const auto cs = (delay_ms + 5) / 10; cs <= 0xFFFF) return {static_cast<std::uint16_t>(cs), std::uint16_t{100}};
  return {static_cast<std::uint16_t>(std::min<std::uint32_t>((delay_ms + 500) / 1000, 0xFFFF)), std::uint16_t{1}};
}

struct StoredFrame {
  std::int32_t x{};
  std::int32_t y{};
  std::int32_t width{};
  std::int32_t height{};
  std::uint32_t delay_ms{};
  std::vector<std::uint8_t> compressed;
};

}  // namespace

struct AnimationEncoder::Impl {
  std::int32_t width{};
  std::int32_t height{};
  std::uint32_t play_count{};
  std::int32_t dots_per_meter_x{};
  std::int32_t dots_per_meter_y{};
  std::vector<std::uint8_t> previous;  // the canvas after the last frame, RGBA
  std::vector<StoredFrame> frames;
};

AnimationEncoder::AnimationEncoder(std::int32_t width, std::int32_t height, std::uint32_t play_count,
                                   std::int32_t dots_per_meter_x, std::int32_t dots_per_meter_y)
    : impl_(std::make_unique<Impl>()) {
  if (width <= 0 || height <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Invalid animated PNG frame or duration."));
  }
  impl_->width = width;
  impl_->height = height;
  impl_->play_count = play_count;
  impl_->dots_per_meter_x = dots_per_meter_x;
  impl_->dots_per_meter_y = dots_per_meter_y;
}

AnimationEncoder::~AnimationEncoder() = default;

void AnimationEncoder::add(const PixelBuffer& rgba, std::uint32_t delay_ms) {
  auto& state = *impl_;
  if (rgba.width() != state.width || rgba.height() != state.height || rgba.format() != PixelFormat::rgba8()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Invalid animated PNG frame or duration."));
  }
  const auto stride = static_cast<std::size_t>(state.width) * 4;
  std::vector<std::uint8_t> canvas(stride * static_cast<std::size_t>(state.height));
  for (std::int32_t y = 0; y < state.height; ++y) {
    const auto row = rgba.row(y);
    std::memcpy(canvas.data() + static_cast<std::size_t>(y) * stride, row.data(), stride);
  }

  StoredFrame frame;
  frame.delay_ms = delay_ms;
  frame.width = state.width;
  frame.height = state.height;
  if (!state.frames.empty()) {
    // Only the rectangle that changed; dispose none keeps the rest of the previous frame.
    std::int32_t left = state.width;
    std::int32_t right = -1;
    std::int32_t top = state.height;
    std::int32_t bottom = -1;
    for (std::int32_t y = 0; y < state.height; ++y) {
      const auto* now = canvas.data() + static_cast<std::size_t>(y) * stride;
      const auto* before = state.previous.data() + static_cast<std::size_t>(y) * stride;
      if (std::memcmp(now, before, stride) == 0) continue;
      top = std::min(top, y);
      bottom = y;
      for (std::int32_t x = 0; x < state.width; ++x) {
        if (std::memcmp(now + static_cast<std::size_t>(x) * 4, before + static_cast<std::size_t>(x) * 4, 4) != 0) {
          left = std::min(left, x);
          right = std::max(right, x);
        }
      }
    }
    if (bottom < 0) {
      // Identical to the frame before: a frame needs at least one pixel, so store the
      // unchanged top-left pixel. The frame count stays one per layer.
      frame.width = 1;
      frame.height = 1;
    } else {
      frame.x = left;
      frame.y = top;
      frame.width = right - left + 1;
      frame.height = bottom - top + 1;
    }
  }
  frame.compressed = compress_rect(canvas, state.width, frame.x, frame.y, frame.width, frame.height);
  state.frames.push_back(std::move(frame));
  state.previous = std::move(canvas);
}

std::vector<std::uint8_t> AnimationEncoder::finish() {
  auto& state = *impl_;
  if (state.frames.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Animated PNG needs at least one frame."));
  }
  std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<std::uint8_t> data;
  append_be32(data, static_cast<std::uint32_t>(state.width));
  append_be32(data, static_cast<std::uint32_t>(state.height));
  data.insert(data.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, deflate, adaptive filters, no interlace
  append_chunk(png, "IHDR", data);
  if (state.dots_per_meter_x > 0 && state.dots_per_meter_y > 0) {
    data.clear();
    append_be32(data, static_cast<std::uint32_t>(state.dots_per_meter_x));
    append_be32(data, static_cast<std::uint32_t>(state.dots_per_meter_y));
    data.push_back(1);  // the unit is the meter
    append_chunk(png, "pHYs", data);
  }
  data.clear();
  append_be32(data, static_cast<std::uint32_t>(state.frames.size()));
  append_be32(data, state.play_count);
  append_chunk(png, "acTL", data);

  std::uint32_t sequence = 0;
  for (std::size_t index = 0; index < state.frames.size(); ++index) {
    const auto& frame = state.frames[index];
    data.clear();
    append_be32(data, sequence++);
    append_be32(data, static_cast<std::uint32_t>(frame.width));
    append_be32(data, static_cast<std::uint32_t>(frame.height));
    append_be32(data, static_cast<std::uint32_t>(frame.x));
    append_be32(data, static_cast<std::uint32_t>(frame.y));
    const auto delay = delay_fraction(frame.delay_ms);
    append_be16(data, delay.num);
    append_be16(data, delay.den);
    data.push_back(0);  // dispose none
    data.push_back(0);  // blend source
    append_chunk(png, "fcTL", data);
    const std::span<const std::uint8_t> compressed(frame.compressed);
    for (std::size_t at = 0; at < compressed.size(); at += kMaxChunkData) {
      const auto piece = compressed.subspan(at, std::min(kMaxChunkData, compressed.size() - at));
      if (index == 0) {
        // Frame 1 is the default image too.
        append_chunk(png, "IDAT", piece);
      } else {
        data.clear();
        append_be32(data, sequence++);
        data.insert(data.end(), piece.begin(), piece.end());
        append_chunk(png, "fdAT", data);
      }
    }
  }
  append_chunk(png, "IEND", {});
  return png;
}

}  // namespace patchy::apng
