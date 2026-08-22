#include "formats/pdf_png_writer.hpp"

#include "formats/miniz/miniz.h"

#include <cstring>

namespace patchy::formats {
namespace {

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// Every PNG chunk is length, type, data, CRC of type+data (RFC 2083 clause 5.3).
void append_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data) {
  append_be32(out, static_cast<std::uint32_t>(data.size()));
  const auto crc_start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const auto crc = mz_crc32(MZ_CRC32_INIT, out.data() + crc_start, out.size() - crc_start);
  append_be32(out, static_cast<std::uint32_t>(crc));
}

}  // namespace

std::vector<std::uint8_t> encode_png_rgba8(std::span<const std::uint8_t> rgba, int width, int height) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  if (rgba.size() < expected) {
    return {};
  }

  // Filter type 0 (None) on every row: the samples came out of a PDF image that was
  // already compressed once, so a second filtering pass buys little and costs time.
  std::vector<std::uint8_t> raw;
  raw.reserve(expected + static_cast<std::size_t>(height));
  const auto stride = static_cast<std::size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    const auto row = static_cast<std::size_t>(y) * stride;
    raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(row),
               rgba.begin() + static_cast<std::ptrdiff_t>(row + stride));
  }

  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<std::uint8_t> compressed(bound);
  if (mz_compress(compressed.data(), &bound, raw.data(), static_cast<mz_ulong>(raw.size())) != MZ_OK) {
    return {};
  }
  compressed.resize(bound);

  std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

  std::vector<std::uint8_t> header;
  append_be32(header, static_cast<std::uint32_t>(width));
  append_be32(header, static_cast<std::uint32_t>(height));
  header.push_back(8);  // bit depth
  header.push_back(6);  // colour type 6: truecolour with alpha
  header.push_back(0);  // deflate
  header.push_back(0);  // adaptive filtering
  header.push_back(0);  // no interlace
  append_chunk(png, "IHDR", header);
  append_chunk(png, "IDAT", compressed);
  append_chunk(png, "IEND", {});
  return png;
}

}  // namespace patchy::formats
