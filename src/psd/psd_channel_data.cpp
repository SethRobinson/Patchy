// Channel and composite image-data codec for the PSD reader/writer: PackBits
// row coding, planar channel encode/decode, the merged-composite builders, and
// CMYK-to-RGB plane conversion. Split out of psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document_depth.hpp"
#include "formats/miniz/miniz.h"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "core/worker_budget.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/srgb_transfer.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
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
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD channel dimensions cannot be negative"));
  }
  const auto row_width = static_cast<std::size_t>(width);
  const auto row_count = static_cast<std::size_t>(height) * static_cast<std::size_t>(channel_count);
  const auto channel_pixels = row_width * static_cast<std::size_t>(height);
  const auto expected_size = channel_pixels * static_cast<std::size_t>(channel_count);
  if (planar_channels.size() != expected_size) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD channel data length does not match its dimensions"));
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
        throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD PackBits row is too large"));
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
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD composite export requires RGB8 pixels"));
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
// any_depth: the deep writer's form, which reads the layer and mask at their own depth.
[[nodiscard]] const Layer* document_alpha_mask_layer(const Document& document, bool any_depth = false) noexcept {
  if (document.layers().size() != 1) {
    return nullptr;
  }
  const Layer& layer = document.layers().front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() || !layer_mask_is_document_alpha(layer)) {
    return nullptr;
  }
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled || mask->pixels.empty() || mask->pixels.format().channels != 1U ||
      (!any_depth && mask->pixels.format() != PixelFormat::gray8())) {
    return nullptr;
  }
  const auto width = document.width();
  const auto height = document.height();
  if (layer.pixels().width() != width || layer.pixels().height() != height) {
    return nullptr;
  }
  const auto pixel_format = layer.pixels().format();
  if (any_depth) {
    return pixel_format.channels == 3U || pixel_format.channels == 4U ? &layer : nullptr;
  }
  if (pixel_format.bit_depth != BitDepth::UInt8 ||
      (pixel_format != PixelFormat::rgb8() && pixel_format != PixelFormat::rgba8())) {
    return nullptr;
  }
  return &layer;
}

std::vector<std::uint8_t> read_rle_channel_from_counts(BigEndianReader& reader,
                                                       std::span<const std::uint32_t> row_lengths,
                                                       std::size_t row_bytes,
                                                       std::size_t* damaged_rows) {
  std::vector<std::uint8_t> channel;
  channel.reserve(row_bytes * row_lengths.size());
  for (const auto row_length : row_lengths) {
    const auto row = reader.read_bytes(row_length);
    bool damaged = false;
    auto decoded = decode_packbits_scanline(row, row_bytes, &damaged);
    if (damaged && damaged_rows != nullptr) {
      ++*damaged_rows;
    }
    channel.insert(channel.end(), decoded.begin(), decoded.end());
  }
  return channel;
}

[[nodiscard]] constexpr std::size_t bytes_per_sample(std::uint16_t depth) noexcept {
  return depth == 32 ? 4U : depth == 16 ? 2U : 1U;
}

// Inflates one zip/zip-prediction channel payload. Photoshop writes standard zlib
// streams and the decoded size is exactly the planar sample data.
[[nodiscard]] std::vector<std::uint8_t> inflate_zip_channel(std::span<const std::uint8_t> compressed,
                                                            std::size_t expected_size) {
  std::vector<std::uint8_t> data(expected_size);
  mz_ulong out_length = static_cast<mz_ulong>(expected_size);
  if (mz_uncompress(data.data(), &out_length, compressed.data(),
                    static_cast<mz_ulong>(compressed.size())) != MZ_OK ||
      out_length != expected_size) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD zip-compressed channel data is corrupt"));
  }
  return data;
}

// Undoes Photoshop's zip "prediction" filter in place. 16-bit rows are
// delta-encoded per big-endian u16; 32-bit rows are byte-delta-encoded and then
// stored as four byte planes per row (all MSBs first), which interleave back into
// big-endian floats here. 8-bit rows are plain byte deltas.
void unpredict_channel_rows(std::vector<std::uint8_t>& data, std::int32_t width, std::int32_t height,
                            std::uint16_t depth) {
  const auto columns = static_cast<std::size_t>(std::max(0, width));
  const auto row_bytes = columns * bytes_per_sample(depth);
  std::vector<std::uint8_t> shuffled(depth == 32 ? row_bytes : 0U);
  for (std::int32_t y = 0; y < height; ++y) {
    auto* row = data.data() + static_cast<std::size_t>(y) * row_bytes;
    if (depth == 16) {
      std::uint16_t previous = 0;
      for (std::size_t x = 0; x < columns; ++x) {
        previous = static_cast<std::uint16_t>(
            previous + static_cast<std::uint16_t>((row[x * 2U] << 8U) | row[x * 2U + 1U]));
        row[x * 2U] = static_cast<std::uint8_t>(previous >> 8U);
        row[x * 2U + 1U] = static_cast<std::uint8_t>(previous & 0xFFU);
      }
      continue;
    }
    for (std::size_t i = 1; i < row_bytes; ++i) {
      row[i] = static_cast<std::uint8_t>(row[i] + row[i - 1]);
    }
    if (depth == 32) {
      std::copy(row, row + row_bytes, shuffled.begin());
      for (std::size_t x = 0; x < columns; ++x) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
          row[x * 4U + byte] = shuffled[byte * columns + x];
        }
      }
    }
  }
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

std::vector<std::uint8_t> big_endian_plane(const PixelBuffer& buffer, std::size_t channel) {
  const auto format = buffer.format();
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  const auto pixel_count = static_cast<std::size_t>(buffer.width()) * static_cast<std::size_t>(buffer.height());
  std::vector<std::uint8_t> plane(pixel_count * sample_bytes);
  const auto stride = static_cast<std::size_t>(format.channels) * sample_bytes;
  const auto* in = buffer.data().data();
  for (std::size_t i = 0; i < pixel_count; ++i) {
    auto* out = plane.data() + i * sample_bytes;
    if (channel >= format.channels) {
      // Constant full scale.
      if (sample_bytes == 1U) {
        out[0] = 0xFFU;
      } else if (sample_bytes == 2U) {
        out[0] = 0xFFU;
        out[1] = 0xFFU;
      } else {
        const auto bits = std::bit_cast<std::uint32_t>(1.0F);
        out[0] = static_cast<std::uint8_t>(bits >> 24U);
        out[1] = static_cast<std::uint8_t>(bits >> 16U);
        out[2] = static_cast<std::uint8_t>(bits >> 8U);
        out[3] = static_cast<std::uint8_t>(bits);
      }
      continue;
    }
    const auto* sample = in + i * stride + channel * sample_bytes;
    if (sample_bytes == 1U) {
      out[0] = sample[0];
    } else if (sample_bytes == 2U) {
      std::uint16_t value = 0;
      std::memcpy(&value, sample, sizeof(value));
      out[0] = static_cast<std::uint8_t>(value >> 8U);
      out[1] = static_cast<std::uint8_t>(value);
    } else {
      float value = 0.0F;
      std::memcpy(&value, sample, sizeof(value));
      const auto bits = std::bit_cast<std::uint32_t>(value);
      out[0] = static_cast<std::uint8_t>(bits >> 24U);
      out[1] = static_cast<std::uint8_t>(bits >> 16U);
      out[2] = static_cast<std::uint8_t>(bits >> 8U);
      out[3] = static_cast<std::uint8_t>(bits);
    }
  }
  return plane;
}

EncodedChannel encode_channel_at_depth(std::uint16_t id, std::int32_t width, std::int32_t height,
                                       std::span<const std::uint8_t> big_endian_samples, BitDepth depth,
                                       bool wide_rle_counts) {
  if (depth == BitDepth::UInt8) {
    return encode_channel(id, width, height, big_endian_samples, wide_rle_counts);
  }
  if (depth == BitDepth::UInt16) {
    auto rle_data = encode_packbits_rows(big_endian_samples, width * 2, height, 1, wide_rle_counts);
    if (rle_data.size() < big_endian_samples.size()) {
      return EncodedChannel{id, width, height, kCompressionRle, std::move(rle_data)};
    }
    return EncodedChannel{id, width, height, kCompressionRaw,
                          std::vector<std::uint8_t>(big_endian_samples.begin(), big_endian_samples.end())};
  }
  // 32-bit layer channels: Photoshop 2026 refuses raw ones ("Cannot open the file because
  // the open options are incorrect") and writes zip with prediction itself, so Patchy does
  // too. The filter is the exact inverse of unpredict_channel_rows: each row's floats split
  // into four byte planes (all most significant bytes first), then byte deltas.
  if (big_endian_samples.empty()) {
    return EncodedChannel{id, width, height, kCompressionRaw, {}};
  }
  std::vector<std::uint8_t> predicted(big_endian_samples.size());
  const auto columns = static_cast<std::size_t>(std::max(0, width));
  const auto row_bytes = columns * 4U;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* in = big_endian_samples.data() + static_cast<std::size_t>(y) * row_bytes;
    auto* out = predicted.data() + static_cast<std::size_t>(y) * row_bytes;
    for (std::size_t x = 0; x < columns; ++x) {
      for (std::size_t byte = 0; byte < 4U; ++byte) {
        out[byte * columns + x] = in[x * 4U + byte];
      }
    }
    for (std::size_t i = row_bytes; i-- > 1;) {
      out[i] = static_cast<std::uint8_t>(out[i] - out[i - 1]);
    }
  }
  auto bound = mz_compressBound(static_cast<mz_ulong>(predicted.size()));
  std::vector<std::uint8_t> compressed(static_cast<std::size_t>(bound));
  if (mz_compress(compressed.data(), &bound, predicted.data(), static_cast<mz_ulong>(predicted.size())) != MZ_OK) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD zip-compressed channel data is corrupt"));
  }
  compressed.resize(static_cast<std::size_t>(bound));
  return EncodedChannel{id, width, height, kCompressionZipPrediction, std::move(compressed)};
}

void write_deep_image_data(BigEndianWriter& writer, const PixelBuffer& rgb,
                           const std::vector<std::vector<std::uint8_t>>& extra_planes, BitDepth depth,
                           bool wide_rle_counts) {
  std::vector<std::uint8_t> planar;
  for (std::size_t channel = 0; channel < rgb.format().channels; ++channel) {
    const auto plane = big_endian_plane(rgb, channel);
    planar.insert(planar.end(), plane.begin(), plane.end());
  }
  for (const auto& plane : extra_planes) {
    planar.insert(planar.end(), plane.begin(), plane.end());
  }
  if (depth == BitDepth::UInt16) {
    const auto plane_count = static_cast<std::uint16_t>(rgb.format().channels + extra_planes.size());
    const auto rle_data = encode_packbits_rows(planar, rgb.width() * 2, rgb.height(), plane_count,
                                               wide_rle_counts, /*even_rows=*/true);
    if (rle_data.size() < planar.size()) {
      writer.write_u16(kCompressionRle);
      writer.write_bytes(rle_data);
      return;
    }
  }
  writer.write_u16(kCompressionRaw);
  writer.write_bytes(planar);
}

namespace {
thread_local bool g_linear_descriptor_colors = false;
}  // namespace

ScopedLinearDescriptorColors::ScopedLinearDescriptorColors(bool linear) noexcept
    : previous_(g_linear_descriptor_colors) {
  g_linear_descriptor_colors = linear;
}

ScopedLinearDescriptorColors::~ScopedLinearDescriptorColors() {
  g_linear_descriptor_colors = previous_;
}

BlendMode writable_layer_blend_mode(BlendMode mode) noexcept {
  return g_linear_descriptor_colors && !blend_mode_supported_at_depth(mode, BitDepth::Float32) ? BlendMode::Normal
                                                                                                : mode;
}

double descriptor_rgb_component(std::uint8_t encoded) noexcept {
  if (!g_linear_descriptor_colors) {
    return static_cast<double>(encoded);
  }
  return srgb_decode(static_cast<double>(encoded) / 255.0) * 255.0;
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

namespace {

std::vector<std::uint8_t> narrowed_plane(const PixelBuffer& coverage) {
  const auto narrow = convert_pixel_buffer_depth(coverage, BitDepth::UInt8, SampleKind::Coverage);
  return {narrow.data().begin(), narrow.data().end()};
}

}  // namespace

std::optional<DeepDocumentComposite> deep_document_alpha_composite(const Document& document, BitDepth depth) {
  const Layer* layer = document_alpha_mask_layer(document, true);
  if (layer == nullptr) {
    return std::nullopt;
  }
  const auto width = document.width();
  const auto height = document.height();
  const auto domain = deep_domain_for(depth);
  DeepDocumentComposite composite{PixelBuffer(width, height, with_bit_depth(PixelFormat::rgb8(), depth)),
                                 PixelBuffer(width, height, with_bit_depth(PixelFormat::gray8(), depth)),
                                 {},
                                 "Alpha 1"};
  const auto& mask = *layer->mask();
  std::vector<float> row(static_cast<std::size_t>(width) * 4U);
  std::vector<float> coverage(static_cast<std::size_t>(width));
  for (std::int32_t y = 0; y < height; ++y) {
    load_rgba_row(layer->pixels(), y, 0, width, domain, row);
    store_rgba_row(composite.rgb, y, 0, width, domain, row);
    std::fill(coverage.begin(), coverage.end(), static_cast<float>(mask.default_color));
    const auto mask_y = y - mask.bounds.y;
    if (mask_y >= 0 && mask_y < mask.pixels.height()) {
      const auto first = std::max(0, mask.bounds.x);
      const auto last = std::min(width, mask.bounds.x + mask.pixels.width());
      if (last > first) {
        load_coverage_row(mask.pixels, mask_y, first - mask.bounds.x, last - first,
                          std::span<float>(coverage).subspan(static_cast<std::size_t>(first)));
      }
    }
    store_coverage_row(composite.alpha, y, 0, width, coverage);
  }
  composite.alpha8 = narrowed_plane(composite.alpha);
  return composite;
}

DeepDocumentComposite deep_merged_flatten_composite(const Document& document, BitDepth depth) {
  const auto width = document.width();
  const auto height = document.height();
  const auto domain = deep_domain_for(depth);
  const auto flat = Compositor{}.flatten_rgba_deep(document);
  DeepDocumentComposite composite{PixelBuffer(width, height, with_bit_depth(PixelFormat::rgb8(), depth)),
                                 PixelBuffer(width, height, with_bit_depth(PixelFormat::gray8(), depth)),
                                 {},
                                 std::string_view{}};
  bool transparent = false;
  std::vector<float> row(static_cast<std::size_t>(width) * 4U);
  std::vector<float> coverage(static_cast<std::size_t>(width));
  for (std::int32_t y = 0; y < height; ++y) {
    load_rgba_row(flat, y, 0, width, domain, row);
    store_rgba_row(composite.rgb, y, 0, width, domain, row);
    for (std::int32_t x = 0; x < width; ++x) {
      coverage[static_cast<std::size_t>(x)] = row[static_cast<std::size_t>(x) * 4U + 3U];
      transparent = transparent || coverage[static_cast<std::size_t>(x)] < kDeepScale;
    }
    store_coverage_row(composite.alpha, y, 0, width, coverage);
  }
  // A fully opaque flatten writes three channels, as the 8-bit writer does.
  if (!transparent) {
    composite.alpha = PixelBuffer{};
    return composite;
  }
  composite.alpha8 = narrowed_plane(composite.alpha);
  composite.channel_name = "Transparency";
  return composite;
}

// Writes RGB followed by any number of full-canvas grayscale planes. The merged
// image RLE layout has one compression marker, all row counts for all planes, then
// all encoded rows. Build the RLE candidate row-by-row so adding many saved
// channels does not require another full planar copy of every channel.
void write_rgb8_image_data_with_extra_channels(
    BigEndianWriter& writer, const PixelBuffer& pixels,
    std::span<const std::span<const std::uint8_t>> extra_channels, bool wide_rle_counts) {
  if (pixels.format() != PixelFormat::rgb8()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD composite export requires RGB8 pixels"));
  }
  const auto width = static_cast<std::size_t>(pixels.width());
  const auto height = static_cast<std::size_t>(pixels.height());
  const auto channel_pixels = width * height;
  for (const auto channel : extra_channels) {
    if (channel.size() != channel_pixels) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD saved channel dimensions do not match the document"));
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
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD PackBits row is too large"));
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

std::vector<std::uint8_t> convert_channel_to_8bit(std::vector<std::uint8_t>&& data, std::uint16_t depth,
                                                  bool color_channel) {
  if (depth == 16) {
    // Full-range big-endian u16; value/257 with rounding, like the Affinity importer.
    const auto samples = data.size() / 2U;
    for (std::size_t i = 0; i < samples; ++i) {
      const auto value =
          static_cast<std::uint32_t>((data[i * 2U] << 8U) | data[i * 2U + 1U]);
      data[i] = static_cast<std::uint8_t>((value + 128U) / 257U);
    }
    data.resize(samples);
  } else if (depth == 32) {
    const auto samples = data.size() / 4U;
    for (std::size_t i = 0; i < samples; ++i) {
      const auto bits = (static_cast<std::uint32_t>(data[i * 4U]) << 24U) |
                        (static_cast<std::uint32_t>(data[i * 4U + 1U]) << 16U) |
                        (static_cast<std::uint32_t>(data[i * 4U + 2U]) << 8U) |
                        static_cast<std::uint32_t>(data[i * 4U + 3U]);
      const auto value = std::bit_cast<float>(bits);
      // PSD 32-bit channels are linear-light floats; color planes bake through the shared
      // sRGB transfer, transparency/masks/saved channels scale linearly instead.
      data[i] = color_channel
                    ? linear_to_srgb8(value)
                    : static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    }
    data.resize(samples);
  }
  return std::move(data);
}

std::vector<std::uint8_t> read_channel_samples(BigEndianReader& reader, std::uint16_t compression, std::int32_t width,
                                               std::int32_t height, bool wide_rle_counts,
                                               const ChannelDecodeInfo& decode_info,
                                               std::size_t* damaged_rows) {
  const auto depth = decode_info.depth;
  const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_sample(depth);
  const auto byte_count = row_bytes * static_cast<std::size_t>(height);

  if (compression == kCompressionRaw) {
    return reader.read_bytes(byte_count);
  }

  if (compression == kCompressionZip || compression == kCompressionZipPrediction) {
    const auto payload = reader.read_bytes(static_cast<std::size_t>(decode_info.zip_payload_length));
    auto data = inflate_zip_channel(payload, byte_count);
    if (compression == kCompressionZipPrediction) {
      unpredict_channel_rows(data, width, height, depth);
    }
    return data;
  }

  if (compression != kCompressionRle) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported PSD channel compression"));
  }

  std::vector<std::uint32_t> row_lengths;
  row_lengths.reserve(static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    row_lengths.push_back(wide_rle_counts ? reader.read_u32() : reader.read_u16());
  }

  return read_rle_channel_from_counts(reader, row_lengths, row_bytes, damaged_rows);
}

std::vector<std::uint8_t> read_channel_data(BigEndianReader& reader, std::uint16_t compression, std::int32_t width,
                                            std::int32_t height, bool wide_rle_counts,
                                            const ChannelDecodeInfo& decode_info,
                                            std::size_t* damaged_rows) {
  return convert_channel_to_8bit(
      read_channel_samples(reader, compression, width, height, wide_rle_counts, decode_info, damaged_rows),
      decode_info.depth, decode_info.color_channel);
}

void store_native_plane(PixelBuffer& target, std::size_t channel, std::span<const std::uint8_t> plane,
                        std::uint16_t depth) {
  const auto format = target.format();
  const auto sample_bytes = bytes_per_channel(format.bit_depth);
  if (static_cast<std::size_t>(depth / 8U) != sample_bytes || channel >= format.channels) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "PSD channel depth does not match the document"));
  }
  const auto pixel_count = static_cast<std::size_t>(target.width()) * static_cast<std::size_t>(target.height());
  const auto samples = std::min(pixel_count, plane.size() / sample_bytes);
  auto* out = target.data().data();
  const auto stride = static_cast<std::size_t>(format.channels) * sample_bytes;
  for (std::size_t i = 0; i < samples; ++i) {
    const auto* in = plane.data() + i * sample_bytes;
    auto* sample = out + i * stride + channel * sample_bytes;
    if (sample_bytes == 2U) {
      const auto value = static_cast<std::uint16_t>((in[0] << 8U) | in[1]);
      std::memcpy(sample, &value, sizeof(value));
    } else if (sample_bytes == 4U) {
      const auto bits = (static_cast<std::uint32_t>(in[0]) << 24U) | (static_cast<std::uint32_t>(in[1]) << 16U) |
                        (static_cast<std::uint32_t>(in[2]) << 8U) | static_cast<std::uint32_t>(in[3]);
      const auto value = std::bit_cast<float>(bits);
      std::memcpy(sample, &value, sizeof(value));
    } else {
      *sample = in[0];
    }
  }
}

void append_damaged_row_notice(std::size_t damaged_rows, std::vector<std::string>* notices) {
  if (damaged_rows == 0 || notices == nullptr) {
    return;
  }
  const auto* subject = damaged_rows == 1 ? " in this file is damaged and was"
                                          : "s in this file are damaged and were";
  notices->push_back(std::to_string(damaged_rows) + " compressed image row" + subject +
                     " recovered as far as they decoded; those parts of the image may be wrong.");
}

bool is_cmyk_color_mode(std::uint16_t color_mode) noexcept {
  return color_mode == kColorModeCmyk;
}

bool is_grayscale_color_mode(std::uint16_t color_mode) noexcept {
  return color_mode == kColorModeGrayscale || color_mode == kColorModeDuotone ||
         color_mode == kColorModeBitmap || color_mode == kColorModeIndexed;
}

bool is_lab_color_mode(std::uint16_t color_mode) noexcept {
  return color_mode == kColorModeLab;
}

void convert_indexed_plane_to_rgb(PixelBuffer& pixels, const std::uint8_t* indices, std::size_t pixel_count,
                                  const std::uint8_t* palette) {
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data();
  for (std::size_t i = 0; i < pixel_count; ++i) {
    auto* pixel = target + i * channels;
    const auto index = static_cast<std::size_t>(indices[i]);
    pixel[0] = palette[index];
    pixel[1] = palette[256U + index];
    pixel[2] = palette[512U + index];
  }
}

// CIE Lab (D50, as Photoshop stores it) to sRGB (D65): Lab to XYZ, Bradford adaptation,
// the sRGB matrix, then the shared transfer function. Out-of-gamut colors clip.
std::array<std::uint8_t, 3> srgb8_from_lab(double lightness, double a, double b) {
  const auto inverse = [](double t) {
    constexpr double delta = 6.0 / 29.0;
    return t > delta ? t * t * t : 3.0 * delta * delta * (t - 4.0 / 29.0);
  };
  const double fy = (lightness + 16.0) / 116.0;
  const double x50 = 0.96422 * inverse(fy + a / 500.0);
  const double y50 = inverse(fy);
  const double z50 = 0.82521 * inverse(fy - b / 200.0);
  // Bradford D50 -> D65.
  const double x = 0.9555766 * x50 - 0.0230393 * y50 + 0.0631636 * z50;
  const double y = -0.0282895 * x50 + 1.0099416 * y50 + 0.0210077 * z50;
  const double z = 0.0122982 * x50 - 0.0204830 * y50 + 1.3299098 * z50;
  const double red = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
  const double green = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
  const double blue = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;
  return {linear_to_srgb8(static_cast<float>(red)), linear_to_srgb8(static_cast<float>(green)),
          linear_to_srgb8(static_cast<float>(blue))};
}

void convert_lab_pixels_to_rgb(PixelBuffer& pixels) {
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  if (channels < 3U) {
    return;
  }
  auto* target = pixels.data().data();
  const auto pixel_count = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  for (std::size_t i = 0; i < pixel_count; ++i) {
    auto* pixel = target + i * channels;
    const auto rgb = srgb8_from_lab(static_cast<double>(pixel[0]) * 100.0 / 255.0,
                                    static_cast<double>(pixel[1]) - 128.0, static_cast<double>(pixel[2]) - 128.0);
    pixel[0] = rgb[0];
    pixel[1] = rgb[1];
    pixel[2] = rgb[2];
  }
}

void convert_16_bit_color_planes_to_rgb(PixelBuffer& pixels,
                                       std::span<const std::vector<std::uint8_t>> planes,
                                       std::uint16_t color_mode, const CmykColorConverter& colors) {
  const bool gray = is_grayscale_color_mode(color_mode);
  const bool cmyk = is_cmyk_color_mode(color_mode);
  const bool lab = is_lab_color_mode(color_mode);
  if ((!gray && !cmyk && !lab) || (gray && colors.gray_icc == nullptr)) {
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto plane = gray ? 0U : c;
      if (plane < planes.size()) {
        store_native_plane(pixels, c, planes[plane], 16);
      }
    }
    return;
  }
  const auto lab_transform = lab ? LabToRgbTransform::create() : std::nullopt;
  if (lab && !lab_transform) {
    throw std::bad_alloc{};
  }
  const auto components = cmyk ? 4U : gray ? 1U : 3U;
  const auto pixel_count = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data();
  constexpr std::size_t kChunkPixels = 65536;
  std::vector<std::uint16_t> source(std::min(pixel_count, kChunkPixels) * components);
  std::vector<std::uint16_t> rgb(std::min(pixel_count, kChunkPixels) * 3U);
  for (std::size_t begin = 0; begin < pixel_count; begin += kChunkPixels) {
    const auto count = std::min(kChunkPixels, pixel_count - begin);
    for (std::size_t i = 0; i < count; ++i) {
      for (std::size_t c = 0; c < components; ++c) {
        const auto offset = (begin + i) * 2U;
        source[i * components + c] = c < planes.size() && offset + 1U < planes[c].size()
            ? static_cast<std::uint16_t>((static_cast<unsigned>(planes[c][offset]) << 8U) | planes[c][offset + 1U])
            : std::uint16_t{0};
      }
    }
    if (gray) {
      colors.gray_icc->convert16(source.data(), rgb.data(), count);
    } else if (lab) {
      lab_transform->convert16(source.data(), rgb.data(), count);
    } else if (colors.icc != nullptr) {
      colors.icc->convert16(source.data(), rgb.data(), count);
    } else {
      for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t c = 0; c < 3U; ++c) {
          rgb[i * 3U + c] = static_cast<std::uint16_t>(
              (static_cast<std::uint32_t>(source[i * 4U + c]) * source[i * 4U + 3U] + 32767U) / 65535U);
        }
      }
    }
    for (std::size_t i = 0; i < count; ++i) {
      std::memcpy(target + (begin + i) * channels * sizeof(std::uint16_t),
                  rgb.data() + i * 3U, 3U * sizeof(std::uint16_t));
    }
  }
}

void convert_multichannel_planes_to_rgb(PixelBuffer& pixels, std::span<const std::vector<std::uint8_t>> planes,
                                        std::size_t pixel_count) {
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data();
  for (std::size_t component = 0; component < 3U; ++component) {
    const auto* plane = component < planes.size() && planes[component].size() == pixel_count
                            ? planes[component].data()
                            : nullptr;
    for (std::size_t i = 0; i < pixel_count; ++i) {
      target[i * channels + component] = plane != nullptr ? plane[i] : std::uint8_t{255};
    }
  }
}

std::vector<std::uint8_t> read_bitmap_composite_plane(BigEndianReader& reader, const Header& header,
                                                      std::uint16_t compression, std::size_t* damaged_rows) {
  const auto width = static_cast<std::size_t>(header.width);
  const auto height = static_cast<std::size_t>(header.height);
  const auto row_bytes = (width + 7U) / 8U;
  std::vector<std::uint8_t> packed;
  if (compression == kCompressionRaw) {
    packed = reader.read_bytes(row_bytes * height);
  } else if (compression == kCompressionRle) {
    std::vector<std::uint32_t> row_lengths;
    row_lengths.reserve(static_cast<std::size_t>(header.channels) * height);
    for (std::size_t row = 0; row < static_cast<std::size_t>(header.channels) * height; ++row) {
      row_lengths.push_back(header.large_document ? reader.read_u32() : reader.read_u16());
    }
    packed = read_rle_channel_from_counts(reader, std::span<const std::uint32_t>(row_lengths.data(), height),
                                          row_bytes, damaged_rows);
  } else {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported PSD composite compression"));
  }
  if (packed.size() < row_bytes * height) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD composite channel data is truncated"));
  }
  std::vector<std::uint8_t> gray(width * height);
  for (std::size_t y = 0; y < height; ++y) {
    const auto* row = packed.data() + y * row_bytes;
    for (std::size_t x = 0; x < width; ++x) {
      const auto bit = (row[x / 8U] >> (7U - (x % 8U))) & 1U;
      gray[y * width + x] = bit != 0U ? std::uint8_t{0} : std::uint8_t{255};
    }
  }
  return gray;
}

// A gray profile maps each of the 256 input values to one sRGB triple, so the transform
// runs once over a ramp and the plane expands through that table: byte-identical to a
// per-pixel conversion and cheap enough that no parallel strips are needed.
void convert_gray_plane_to_rgb(PixelBuffer& pixels, const std::uint8_t* gray, std::size_t pixel_count,
                               const GrayToRgbTransform* icc) {
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto* target = pixels.data().data();
  if (icc == nullptr) {
    for (std::size_t i = 0; i < pixel_count; ++i) {
      auto* pixel = target + i * channels;
      pixel[0] = gray[i];
      pixel[1] = gray[i];
      pixel[2] = gray[i];
    }
    return;
  }
  std::array<std::uint8_t, 256> ramp{};
  for (std::size_t value = 0; value < ramp.size(); ++value) {
    ramp[value] = static_cast<std::uint8_t>(value);
  }
  std::array<std::uint8_t, 256 * 3> table{};
  icc->convert(ramp.data(), table.data(), ramp.size());
  for (std::size_t i = 0; i < pixel_count; ++i) {
    auto* pixel = target + i * channels;
    const auto* rgb = table.data() + static_cast<std::size_t>(gray[i]) * 3U;
    pixel[0] = rgb[0];
    pixel[1] = rgb[1];
    pixel[2] = rgb[2];
  }
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
  // Capped at 16 like the strip renderers, then clamped to the idle pthread
  // pool when running on the wasm main thread: the caller blocks on the
  // joins, and a fan-out that needs a lazily spawned worker while blocked
  // deadlocks the tab (see core/worker_budget.hpp). Output is byte-identical
  // for any worker count.
  const auto worker_count = static_cast<std::size_t>(std::max(
      1, max_blocking_fanout_workers(static_cast<int>(
             std::min<std::size_t>(static_cast<std::size_t>(hardware_worker_threads()), 16)))));
  // One worker means the split buys nothing; run inline instead of spawning a
  // single thread (which single-threaded wasm builds cannot create at all).
  if (worker_count <= 1) {
    convert_range(0, pixel_count);
    return;
  }
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
                                                                std::uint16_t compression,
                                                                std::size_t* damaged_rows, bool keep_native) {
  std::vector<std::vector<std::uint8_t>> channels;
  channels.reserve(header.channels);
  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);
  const auto color_channels = composite_color_channel_count(header.color_mode);
  const auto is_color = [color_channels](std::uint16_t channel) { return channel < color_channels; };

  if (compression == kCompressionRaw) {
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      const ChannelDecodeInfo info{header.depth, is_color(channel), 0U};
      channels.push_back(keep_native
                             ? read_channel_samples(reader, compression, width, height, header.large_document, info)
                             : read_channel_data(reader, compression, width, height, header.large_document, info));
    }
    return channels;
  }

  if (compression == kCompressionRle) {
    const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_sample(header.depth);
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
      auto samples = read_rle_channel_from_counts(reader, rows, row_bytes, damaged_rows);
      channels.push_back(keep_native ? std::move(samples)
                                     : convert_channel_to_8bit(std::move(samples), header.depth, is_color(channel)));
    }
    return channels;
  }

  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported PSD composite compression"));
}

// Reads only a contiguous suffix of the composite planes. Raw data can skip the
// color planes directly; RLE still needs the complete row-count table, but the
// encoded rows for unwanted planes are skipped without decoding or storing them.
std::vector<std::vector<std::uint8_t>> read_flat_image_channels_from(
    BigEndianReader& reader, const Header& header, std::uint16_t compression,
    std::uint16_t first_channel, std::size_t* damaged_rows, bool keep_native) {
  if (first_channel > header.channels) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Invalid PSD saved channel index"));
  }
  const auto width = static_cast<std::int32_t>(header.width);
  const auto height = static_cast<std::int32_t>(header.height);
  const auto channel_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                             bytes_per_sample(header.depth);
  const auto color_channels = composite_color_channel_count(header.color_mode);
  std::vector<std::vector<std::uint8_t>> channels;
  channels.reserve(static_cast<std::size_t>(header.channels - first_channel));

  if (compression == kCompressionRaw) {
    const auto skip_bytes = channel_bytes * static_cast<std::size_t>(first_channel);
    if (skip_bytes > reader.remaining()) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD composite channel data is truncated"));
    }
    reader.skip(skip_bytes);
    for (std::uint16_t channel = first_channel; channel < header.channels; ++channel) {
      const ChannelDecodeInfo info{header.depth, channel < color_channels, 0U};
      channels.push_back(keep_native
                             ? read_channel_samples(reader, compression, width, height, header.large_document, info)
                             : read_channel_data(reader, compression, width, height, header.large_document, info));
    }
    return channels;
  }

  if (compression == kCompressionRle) {
    const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_sample(header.depth);
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
            throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD composite channel data is truncated"));
          }
          encoded_size += row_length;
        }
        if (encoded_size > reader.remaining()) {
          throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "PSD composite channel data is truncated"));
        }
        reader.skip(encoded_size);
      } else {
        auto samples = read_rle_channel_from_counts(reader, rows, row_bytes, damaged_rows);
        channels.push_back(keep_native ? std::move(samples)
                                       : convert_channel_to_8bit(std::move(samples), header.depth,
                                                                 channel < color_channels));
      }
    }
    return channels;
  }

  throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported PSD composite compression"));
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
