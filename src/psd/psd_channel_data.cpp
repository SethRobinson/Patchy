// Channel and composite image-data codec for the PSD reader/writer: PackBits
// row coding, planar channel encode/decode, the merged-composite builders, and
// CMYK-to-RGB plane conversion. Split out of psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace patchy::psd {

namespace {

// encode_packbits_row moved to psd_descriptor.{hpp,cpp} (shared with the ILBM writer).

// Photoshop's smart-object embed parser reads a composite's RLE rows in two-byte
// units: one odd-length compressed row anywhere in the merged image data makes it
// reject the whole document ("Could not open ... because of a program error") as
// soon as a Smart Filter cache forces the embedded file to be parsed eagerly.
// Pinned against Photoshop 2026 by byte-level bisection (July 2026): evenizing
// every composite row of a rejected embed makes it open; evenizing all but one
// does not. Layer channels and FEid cache planes are exempt — Photoshop itself
// writes odd rows there. Splitting the first multi-byte literal packet re-encodes
// the row one byte longer with an identical decode.
void make_packbits_row_even(std::vector<std::uint8_t>& row) {
  if ((row.size() % 2U) == 0U) {
    return;
  }
  std::size_t i = 0;
  while (i < row.size()) {
    const auto flag = static_cast<std::int8_t>(row[i]);
    if (flag >= 1) {
      // Literal of flag+1 bytes: emit its first byte as a one-byte literal
      // packet of its own. [f, b0, b1, ...] -> [0, b0, f-1, b1, ...].
      row.insert(row.begin() + static_cast<std::ptrdiff_t>(i), 0);
      std::swap(row[i + 1], row[i + 2]);
      row[i + 2] = static_cast<std::uint8_t>(flag - 1);
      return;
    }
    if (flag == -128) {
      i += 1;  // no-op flag byte
    } else {
      i += 2;  // one-byte literal or repeat packet, both two bytes
    }
  }
  // Unreachable for rows from encode_packbits_row: every packet it emits except
  // an even-flag literal has an even size, so an odd row contains a splittable
  // literal. Leave a foreign row without one unchanged.
}

// RLE row byte counts are u16 in PSD and u32 in PSB (wide_rle_counts).
// even_rows applies the merged-composite constraint documented above.
std::vector<std::uint8_t> encode_packbits_rows(std::span<const std::uint8_t> planar_channels,
                                               std::int32_t width, std::int32_t height,
                                               std::uint16_t channel_count, bool wide_rle_counts,
                                               bool even_rows = false) {
  if (width < 0 || height < 0) {
    throw std::runtime_error("PSD channel dimensions cannot be negative");
  }
  const auto row_width = static_cast<std::size_t>(width);
  const auto row_count = static_cast<std::size_t>(height) * static_cast<std::size_t>(channel_count);
  const auto channel_pixels = row_width * static_cast<std::size_t>(height);
  const auto expected_size = channel_pixels * static_cast<std::size_t>(channel_count);
  if (planar_channels.size() != expected_size) {
    throw std::runtime_error("PSD channel data length does not match its dimensions");
  }

  std::vector<std::vector<std::uint8_t>> rows;
  rows.reserve(row_count);
  const auto max_row_bytes = wide_rle_counts ? 0xFFFFFFFFULL : 0xFFFFULL;
  for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
    const auto channel_offset = static_cast<std::size_t>(channel) * channel_pixels;
    for (std::int32_t y = 0; y < height; ++y) {
      const auto row_offset = channel_offset + static_cast<std::size_t>(y) * row_width;
      auto encoded = encode_packbits_row(planar_channels.subspan(row_offset, row_width));
      if (even_rows) {
        make_packbits_row_even(encoded);
      }
      if (encoded.size() > max_row_bytes) {
        throw std::runtime_error("PSD PackBits row is too large");
      }
      rows.push_back(std::move(encoded));
    }
  }

  BigEndianWriter writer;
  for (const auto& row : rows) {
    if (wide_rle_counts) {
      writer.write_u32(static_cast<std::uint32_t>(row.size()));
    } else {
      writer.write_u16(static_cast<std::uint16_t>(row.size()));
    }
  }
  for (const auto& row : rows) {
    writer.write_bytes(row);
  }
  return writer.bytes();
}

std::vector<std::uint8_t> planar_rgb8_data(const PixelBuffer& pixels) {
  if (pixels.format() != PixelFormat::rgb8()) {
    throw std::runtime_error("PSD composite export requires RGB8 pixels");
  }

  const auto channel_pixels = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  std::vector<std::uint8_t> planar(channel_pixels * 3U);
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    const auto channel_offset = static_cast<std::size_t>(channel) * channel_pixels;
    for (std::size_t i = 0; i < channel_pixels; ++i) {
      planar[channel_offset + i] = pixels.data()[i * 3U + channel];
    }
  }
  return planar;
}

// A flat export whose single pixel layer carries an enabled imported-alpha mask
// preserves that mask as a saved composite channel ("Alpha 1"). On PSD reopen it is
// a DocumentChannel, never an applied layer mask. This is the eligibility check.
[[nodiscard]] const Layer* document_alpha_mask_layer(const Document& document) noexcept {
  if (document.layers().size() != 1) {
    return nullptr;
  }
  const Layer& layer = document.layers().front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() || !layer_mask_is_document_alpha(layer)) {
    return nullptr;
  }
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled || mask->pixels.empty() ||
      mask->pixels.format() != PixelFormat::gray8()) {
    return nullptr;
  }
  const auto width = document.width();
  const auto height = document.height();
  if (layer.pixels().width() != width || layer.pixels().height() != height) {
    return nullptr;
  }
  const auto pixel_format = layer.pixels().format();
  if (pixel_format.bit_depth != BitDepth::UInt8 ||
      (pixel_format != PixelFormat::rgb8() && pixel_format != PixelFormat::rgba8())) {
    return nullptr;
  }
  return &layer;
}

std::vector<std::uint8_t> read_rle_channel_from_counts(BigEndianReader& reader,
                                                       std::span<const std::uint32_t> row_lengths,
                                                       std::int32_t width) {
  std::vector<std::uint8_t> channel;
  channel.reserve(static_cast<std::size_t>(width) * row_lengths.size());
  for (const auto row_length : row_lengths) {
    const auto row = reader.read_bytes(row_length);
    auto decoded = decode_packbits(row, static_cast<std::size_t>(width));
    channel.insert(channel.end(), decoded.begin(), decoded.end());
  }
  return channel;
}

std::uint8_t photoshop_cmyk_to_rgb_component(std::uint8_t colorant, std::uint8_t black) noexcept {
  // Photoshop stores CMYK channels as inverted ink values: 255 is 0% ink and 0 is 100% ink.
  return static_cast<std::uint8_t>((static_cast<int>(colorant) * static_cast<int>(black)) / 255);
}

void write_rgb_from_cmyk(PixelBuffer& pixels, std::size_t pixel_index, std::uint8_t cyan, std::uint8_t magenta,
                         std::uint8_t yellow, std::uint8_t black) {
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data() + pixel_index * channels;
  target[0] = photoshop_cmyk_to_rgb_component(cyan, black);
  target[1] = photoshop_cmyk_to_rgb_component(magenta, black);
  target[2] = photoshop_cmyk_to_rgb_component(yellow, black);
}

}  // namespace

EncodedChannel encode_channel(std::uint16_t id, std::int32_t width, std::int32_t height,
                              std::span<const std::uint8_t> raw_data, bool wide_rle_counts) {
  auto rle_data = encode_packbits_rows(raw_data, width, height, 1, wide_rle_counts);
  if (rle_data.size() < raw_data.size()) {
    return EncodedChannel{id, width, height, kCompressionRle, std::move(rle_data)};
  }

  return EncodedChannel{id, width, height, kCompressionRaw,
                        std::vector<std::uint8_t>(raw_data.begin(), raw_data.end())};
}

void write_rgb8_image_data(BigEndianWriter& writer, const PixelBuffer& pixels, bool wide_rle_counts) {
  const auto raw_data = planar_rgb8_data(pixels);
  const auto rle_data =
      encode_packbits_rows(raw_data, pixels.width(), pixels.height(), 3, wide_rle_counts,
                           /*even_rows=*/true);
  if (rle_data.size() < raw_data.size()) {
    writer.write_u16(kCompressionRle);
    writer.write_bytes(rle_data);
    return;
  }

  writer.write_u16(kCompressionRaw);
  writer.write_bytes(raw_data);
}

// Builds the composite RGB (with the layer's original colors, NOT the masked flatten) and
// the canvas-sized alpha plane sampled from the layer mask, honoring its bounds and
// default color. Returns nullopt when the document is not eligible (see eligibility above).
[[nodiscard]] std::optional<DocumentAlphaComposite> document_alpha_composite(const Document& document) {
  const Layer* layer = document_alpha_mask_layer(document);
  if (layer == nullptr) {
    return std::nullopt;
  }

  const auto width = document.width();
  const auto height = document.height();
  const auto channel_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  // RGB comes straight from the layer's own (unmasked) pixels so the colors beneath the
  // mask are preserved. pixel()[0..2] is the RGB triple for both rgb8 and rgba8 sources.
  PixelBuffer rgb(width, height, PixelFormat::rgb8());
  const PixelBuffer& source = layer->pixels();
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const std::uint8_t* src = source.pixel(x, y);
      std::uint8_t* dst = rgb.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    }
  }

  const LayerMask& mask = *layer->mask();
  std::vector<std::uint8_t> alpha(channel_pixels, mask.default_color);
  for (std::int32_t my = 0; my < mask.pixels.height(); ++my) {
    const std::int32_t doc_y = mask.bounds.y + my;
    if (doc_y < 0 || doc_y >= height) {
      continue;
    }
    for (std::int32_t mx = 0; mx < mask.pixels.width(); ++mx) {
      const std::int32_t doc_x = mask.bounds.x + mx;
      if (doc_x < 0 || doc_x >= width) {
        continue;
      }
      alpha[static_cast<std::size_t>(doc_y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(doc_x)] = mask.pixels.pixel(mx, my)[0];
    }
  }
  return DocumentAlphaComposite{std::move(rgb), std::move(alpha), "Alpha 1"};
}

// A document whose merged flatten has any transparent pixel writes its composite the
// way Photoshop does: four channels, with the merged coverage as the extra channel and
// resource 1006 naming it "Transparency". Without this, the stored composite mattes
// the canvas onto black, and readers that trust it (Patchy's own smart-object preview
// decode, external thumbnailers) show opaque black where the canvas was transparent.
// A fully opaque flatten returns an empty channel_name: those saves keep the
// historical 3-channel bytes bit for bit (requesting the alpha plane does not change
// the compositor's RGB output).
[[nodiscard]] DocumentAlphaComposite merged_flatten_composite(const Document& document) {
  std::vector<std::uint8_t> alpha;
  auto rgb = Compositor{}.flatten_rgb8(document, &alpha);
  const auto transparent =
      std::any_of(alpha.begin(), alpha.end(), [](std::uint8_t coverage) { return coverage != 255; });
  if (!transparent) {
    alpha.clear();
    return DocumentAlphaComposite{std::move(rgb), std::move(alpha), std::string_view{}};
  }
  return DocumentAlphaComposite{std::move(rgb), std::move(alpha), "Transparency"};
}

// Writes RGB followed by any number of full-canvas grayscale planes. The merged
// image RLE layout has one compression marker, all row counts for all planes, then
// all encoded rows. Build the RLE candidate row-by-row so adding many saved
// channels does not require another full planar copy of every channel.
void write_rgb8_image_data_with_extra_channels(
    BigEndianWriter& writer, const PixelBuffer& pixels,
    std::span<const std::span<const std::uint8_t>> extra_channels, bool wide_rle_counts) {
  if (pixels.format() != PixelFormat::rgb8()) {
    throw std::runtime_error("PSD composite export requires RGB8 pixels");
  }
  const auto width = static_cast<std::size_t>(pixels.width());
  const auto height = static_cast<std::size_t>(pixels.height());
  const auto channel_pixels = width * height;
  for (const auto channel : extra_channels) {
    if (channel.size() != channel_pixels) {
      throw std::runtime_error("PSD saved channel dimensions do not match the document");
    }
  }

  const auto channel_count = 3U + extra_channels.size();
  std::vector<std::uint32_t> row_lengths;
  row_lengths.reserve(height * channel_count);
  std::vector<std::uint8_t> encoded_rows;
  std::vector<std::uint8_t> rgb_row(width);
  const auto max_row_bytes = wide_rle_counts ? 0xFFFFFFFFULL : 0xFFFFULL;
  const auto append_encoded_row = [&](std::span<const std::uint8_t> row) {
    auto encoded = encode_packbits_row(row);
    make_packbits_row_even(encoded);
    if (encoded.size() > max_row_bytes) {
      throw std::runtime_error("PSD PackBits row is too large");
    }
    row_lengths.push_back(static_cast<std::uint32_t>(encoded.size()));
    encoded_rows.insert(encoded_rows.end(), encoded.begin(), encoded.end());
  };

  for (std::size_t component = 0; component < 3U; ++component) {
    for (std::size_t y = 0; y < height; ++y) {
      const auto first_pixel = y * width;
      for (std::size_t x = 0; x < width; ++x) {
        rgb_row[x] = pixels.data()[(first_pixel + x) * 3U + component];
      }
      append_encoded_row(rgb_row);
    }
  }
  for (const auto channel : extra_channels) {
    for (std::size_t y = 0; y < height; ++y) {
      append_encoded_row(channel.subspan(y * width, width));
    }
  }

  const auto count_width = wide_rle_counts ? 4U : 2U;
  const auto rle_size = row_lengths.size() * count_width + encoded_rows.size();
  const auto raw_size = channel_pixels * channel_count;
  if (rle_size < raw_size) {
    writer.write_u16(kCompressionRle);
    for (const auto length : row_lengths) {
      if (wide_rle_counts) {
        writer.write_u32(length);
      } else {
        writer.write_u16(static_cast<std::uint16_t>(length));
      }
    }
    writer.write_bytes(encoded_rows);
    return;
  }

  writer.write_u16(kCompressionRaw);
  for (std::size_t component = 0; component < 3U; ++component) {
    for (std::size_t pixel = 0; pixel < channel_pixels; ++pixel) {
      writer.write_u8(pixels.data()[pixel * 3U + component]);
    }
  }
  for (const auto channel : extra_channels) {
    writer.write_bytes(channel);
  }
}

std::vector<std::uint8_t> read_channel_data(BigEndianReader& reader, std::uint16_t compression, std::int32_t width,
                                            std::int32_t height, bool wide_rle_counts) {
  if (compression == kCompressionRaw) {
    const auto byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return reader.read_bytes(byte_count);
  }

  if (compression != kCompressionRle) {
    throw std::runtime_error("Unsupported PSD channel compression");
  }

  std::vector<std::uint32_t> row_lengths;
  row_lengths.reserve(static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    row_lengths.push_back(wide_rle_counts ? reader.read_u32() : reader.read_u16());
  }

  std::vector<std::uint8_t> channel;
  channel.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row = reader.read_bytes(row_lengths[static_cast<std::size_t>(y)]);
    auto decoded = decode_packbits(row, static_cast<std::size_t>(width));
    channel.insert(channel.end(), decoded.begin(), decoded.end());
  }
  return channel;
}

bool is_cmyk_color_mode(std::uint16_t color_mode) noexcept {
  return color_mode == kColorModeCmyk;
}

// CMYK-mode documents also carry CMYK colors in descriptors (lfx2 effect colors) and text
// engine data, as ink fractions. Convert with the same naive mix as the pixel decode above
// so effect/text colors keep their relationship to the converted pixels.
RgbColor rgb_from_cmyk_ink_fractions(double cyan, double magenta, double yellow, double black) {
  const auto paper = 1.0 - std::clamp(black, 0.0, 1.0);
  const auto component = [paper](double ink) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround((1.0 - std::clamp(ink, 0.0, 1.0)) * paper * 255.0), 0L, 255L));
  };
  return RgbColor{component(cyan), component(magenta), component(yellow)};
}

// Converts decoded planar inverted-CMYK channels into the RGB(A) pixel buffer: through the
// document's embedded ICC profile when one is usable (matching Photoshop), the naive ink
// mix otherwise. Large buffers convert in parallel strips; the ICC transform is cache-free
// fixed-point math, so the output is byte-identical regardless of chunking or thread count.
void convert_cmyk_planes_to_rgb(PixelBuffer& pixels, const std::uint8_t* cyan,
                                const std::uint8_t* magenta, const std::uint8_t* yellow,
                                const std::uint8_t* black, std::size_t pixel_count,
                                const CmykToRgbTransform* icc) {
  if (icc == nullptr) {
    for (std::size_t i = 0; i < pixel_count; ++i) {
      write_rgb_from_cmyk(pixels, i, cyan[i], magenta[i], yellow[i], black[i]);
    }
    return;
  }
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data();
  const auto convert_range = [&](std::size_t begin, std::size_t end) {
    constexpr std::size_t kChunkPixels = 65536;
    std::vector<std::uint8_t> cmyk(std::min(kChunkPixels, end - begin) * 4U);
    std::vector<std::uint8_t> rgb(std::min(kChunkPixels, end - begin) * 3U);
    for (std::size_t start = begin; start < end; start += kChunkPixels) {
      const auto count = std::min(kChunkPixels, end - start);
      for (std::size_t i = 0; i < count; ++i) {
        cmyk[i * 4U + 0U] = cyan[start + i];
        cmyk[i * 4U + 1U] = magenta[start + i];
        cmyk[i * 4U + 2U] = yellow[start + i];
        cmyk[i * 4U + 3U] = black[start + i];
      }
      icc->convert(cmyk.data(), rgb.data(), count);
      for (std::size_t i = 0; i < count; ++i) {
        auto* pixel = target + (start + i) * channels;
        pixel[0] = rgb[i * 3U + 0U];
        pixel[1] = rgb[i * 3U + 1U];
        pixel[2] = rgb[i * 3U + 2U];
      }
    }
  };
  constexpr std::size_t kParallelThresholdPixels = 4U << 20U;
  if (pixel_count < kParallelThresholdPixels) {
    convert_range(0, pixel_count);
    return;
  }
  const auto worker_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  const auto strip = (pixel_count + worker_count - 1) / worker_count;
  std::vector<std::future<void>> workers;
  workers.reserve(worker_count);
  for (std::size_t begin = 0; begin < pixel_count; begin += strip) {
    const auto end = std::min(pixel_count, begin + strip);
    workers.push_back(std::async(std::launch::async, convert_range, begin, end));
  }
  for (auto& worker : workers) {
    worker.get();
  }
}

std::vector<std::vector<std::uint8_t>> read_flat_image_channels(BigEndianReader& reader, const Header& header,
                                                                std::uint16_t compression) {
  std::vector<std::vector<std::uint8_t>> channels;
  channels.reserve(header.channels);
  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);

  if (compression == kCompressionRaw) {
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      channels.push_back(read_channel_data(reader, compression, width, height, header.large_document));
    }
    return channels;
  }

  if (compression == kCompressionRle) {
    std::vector<std::uint32_t> row_lengths;
    row_lengths.reserve(static_cast<std::size_t>(header.channels) * static_cast<std::size_t>(header.height));
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      for (std::uint32_t y = 0; y < header.height; ++y) {
        row_lengths.push_back(header.large_document ? reader.read_u32() : reader.read_u16());
      }
    }
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      const auto offset = static_cast<std::size_t>(channel) * static_cast<std::size_t>(header.height);
      const auto rows =
          std::span<const std::uint32_t>(row_lengths.data() + offset, static_cast<std::size_t>(header.height));
      channels.push_back(read_rle_channel_from_counts(reader, rows, width));
    }
    return channels;
  }

  throw std::runtime_error("Unsupported PSD composite compression");
}

// Reads only a contiguous suffix of the composite planes. Raw data can skip the
// color planes directly; RLE still needs the complete row-count table, but the
// encoded rows for unwanted planes are skipped without decoding or storing them.
std::vector<std::vector<std::uint8_t>> read_flat_image_channels_from(
    BigEndianReader& reader, const Header& header, std::uint16_t compression,
    std::uint16_t first_channel) {
  if (first_channel > header.channels) {
    throw std::runtime_error("Invalid PSD saved channel index");
  }
  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);
  const auto channel_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::vector<std::uint8_t>> channels;
  channels.reserve(static_cast<std::size_t>(header.channels - first_channel));

  if (compression == kCompressionRaw) {
    const auto skip_bytes = channel_pixels * static_cast<std::size_t>(first_channel);
    if (skip_bytes > reader.remaining()) {
      throw std::runtime_error("PSD composite channel data is truncated");
    }
    reader.skip(skip_bytes);
    for (std::uint16_t channel = first_channel; channel < header.channels; ++channel) {
      channels.push_back(read_channel_data(reader, compression, width, height, header.large_document));
    }
    return channels;
  }

  if (compression == kCompressionRle) {
    std::vector<std::uint32_t> row_lengths;
    row_lengths.reserve(static_cast<std::size_t>(header.channels) * static_cast<std::size_t>(header.height));
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      for (std::uint32_t y = 0; y < header.height; ++y) {
        row_lengths.push_back(header.large_document ? reader.read_u32() : reader.read_u16());
      }
    }
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      const auto offset = static_cast<std::size_t>(channel) * static_cast<std::size_t>(header.height);
      const auto rows =
          std::span<const std::uint32_t>(row_lengths.data() + offset, static_cast<std::size_t>(header.height));
      if (channel < first_channel) {
        std::size_t encoded_size = 0;
        for (const auto row_length : rows) {
          if (encoded_size > reader.remaining() ||
              static_cast<std::size_t>(row_length) > reader.remaining() - encoded_size) {
            throw std::runtime_error("PSD composite channel data is truncated");
          }
          encoded_size += row_length;
        }
        if (encoded_size > reader.remaining()) {
          throw std::runtime_error("PSD composite channel data is truncated");
        }
        reader.skip(encoded_size);
      } else {
        channels.push_back(read_rle_channel_from_counts(reader, rows, width));
      }
    }
    return channels;
  }

  throw std::runtime_error("Unsupported PSD composite compression");
}

std::optional<std::vector<std::uint8_t>> even_composite_rows_normalized(
    std::span<const std::uint8_t> file_bytes) {
  try {
    BigEndianReader reader(file_bytes);
    if (key_string(read_signature(reader)) != "8BPS") {
      return std::nullopt;
    }
    const auto version = reader.read_u16();
    if (version != 1 && version != 2) {
      return std::nullopt;
    }
    reader.skip(6);
    const auto channels = reader.read_u16();
    const auto height = reader.read_u32();
    (void)reader.read_u32();  // width
    const auto depth = reader.read_u16();
    (void)reader.read_u16();  // mode
    if (depth != 8 || channels == 0 || height == 0) {
      return std::nullopt;
    }
    reader.skip(reader.read_u32());  // color mode data
    reader.skip(reader.read_u32());  // image resources
    // Layer-and-mask section length is u32 in PSD, u64 in PSB.
    const auto layer_length = version == 2 ? reader.read_u64() : reader.read_u32();
    if (layer_length > reader.remaining()) {
      return std::nullopt;
    }
    reader.skip(static_cast<std::size_t>(layer_length));
    const auto composite_offset = reader.position();
    if (reader.read_u16() != kCompressionRle) {
      return std::nullopt;
    }
    const auto row_count = static_cast<std::size_t>(height) * channels;
    std::vector<std::uint32_t> row_lengths(row_count);
    for (auto& length : row_lengths) {
      length = version == 2 ? reader.read_u32() : reader.read_u16();
    }
    std::size_t odd_rows = 0;
    std::size_t data_size = 0;
    for (const auto length : row_lengths) {
      odd_rows += length & 1U;
      data_size += length;
    }
    // The composite is the file's final section; anything else fails closed.
    if (odd_rows == 0 || data_size != reader.remaining()) {
      return std::nullopt;
    }

    std::vector<std::vector<std::uint8_t>> rows;
    rows.reserve(row_count);
    for (const auto length : row_lengths) {
      const auto start = reader.position();
      reader.skip(length);
      std::vector<std::uint8_t> row(file_bytes.begin() + static_cast<std::ptrdiff_t>(start),
                                    file_bytes.begin() + static_cast<std::ptrdiff_t>(start + length));
      make_packbits_row_even(row);
      if ((row.size() % 2U) != 0U || (version == 1 && row.size() > 0xFFFFULL)) {
        return std::nullopt;  // no splittable literal or u16 count overflow
      }
      rows.push_back(std::move(row));
    }

    std::vector<std::uint8_t> normalized(file_bytes.begin(),
                                         file_bytes.begin() + static_cast<std::ptrdiff_t>(composite_offset));
    BigEndianWriter writer;
    writer.write_u16(kCompressionRle);
    for (const auto& row : rows) {
      if (version == 2) {
        writer.write_u32(static_cast<std::uint32_t>(row.size()));
      } else {
        writer.write_u16(static_cast<std::uint16_t>(row.size()));
      }
    }
    for (const auto& row : rows) {
      writer.write_bytes(row);
    }
    normalized.insert(normalized.end(), writer.bytes().begin(), writer.bytes().end());
    return normalized;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace patchy::psd
