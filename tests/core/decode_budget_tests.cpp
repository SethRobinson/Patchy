// Untrusted-input sizing (docs/psd-reader-hardening.md, "Decode budget"): crafted files whose
// headers declare far more pixels than their data can hold are rejected with the reader's
// normal error before any buffer is sized from the declaration, and honest files still open.
#include "core/document.hpp"
#include "formats/exr_document_io.hpp"
#include "formats/hdr_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"
#include "support/decode_budget.hpp"
#include "core_test_support.hpp"
#include "psd_test_support.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// True when `read` fails with a runtime_error: the reader's normal "cannot open" path. A
// bad_alloc (or anything else) means a buffer was sized from the declaration first.
bool rejected_cleanly(const std::function<void()>& read) {
  try {
    read();
  } catch (const std::runtime_error&) {
    return true;
  } catch (const std::bad_alloc&) {
    std::cout << "  allocation failed instead of a clean rejection\n";
    return false;
  } catch (const std::exception& error) {
    std::cout << "  unexpected exception: " << error.what() << '\n';
    return false;
  }
  std::cout << "  the file opened\n";
  return false;
}

void decode_budget_checks_products_and_the_32_bit_cap() {
  CHECK(patchy::checked_multiply(0U, ~std::uint64_t{0}) == std::uint64_t{0});
  CHECK(!patchy::checked_multiply(std::uint64_t{1} << 32U, std::uint64_t{1} << 32U).has_value());
  CHECK(!patchy::checked_pixel_count(-1, 4).has_value());
  CHECK(patchy::kMaxDecodePixels ==
        (sizeof(std::size_t) >= 8 ? patchy::kMaxDecodePixels64 : patchy::kMaxDecodePixels32));
  CHECK(patchy::checked_pixel_count(300000, 200000, patchy::kMaxDecodePixels64) == std::uint64_t{60000000000});
  CHECK(!patchy::checked_pixel_count(300000, 300000, patchy::kMaxDecodePixels64).has_value());  // past 2^36
  // The PCX case that wraps a 32-bit size_t: 3 x 43691 bytes per row x 32768 rows is
  // 2^32 + 32768. A 32-bit build refuses it; the 64-bit cap still allows it.
  CHECK(!patchy::checked_pixel_count(3 * 43691, 32768, patchy::kMaxDecodePixels32).has_value());
  CHECK(patchy::checked_pixel_count(3 * 43691, 32768, patchy::kMaxDecodePixels64).has_value());
  CHECK(patchy::checked_pixel_count(16384, 16384, patchy::kMaxDecodePixels32).has_value());

  // Within the allowance anything goes; past it the encoded bytes must reach the size.
  CHECK(patchy::decoded_size_plausible(patchy::kDecodeAllowanceBytes, 0U));
  CHECK(!patchy::decoded_size_plausible(patchy::kDecodeAllowanceBytes + 1U, 0U));
  CHECK(patchy::decoded_size_plausible(1032U * 100000U, 100000U));
  CHECK(!patchy::decoded_size_plausible(1032U * 100000U + 1U, 100000U, patchy::kMaxDecodeExpansion, 0U));
  CHECK(patchy::checked_decode_bytes(4, 4, 4U, 0U) == std::size_t{64});
  CHECK(!patchy::checked_decode_bytes(4, 4, 4U, 0U, patchy::kMaxDecodeExpansion, 0U).has_value());
  CHECK(!patchy::checked_decode_bytes(60000, 60000, 4U, 1024U).has_value());

  patchy::DecodeAllowance allowance(100U);
  CHECK(allowance.charge(60U));
  CHECK(!allowance.charge(41U));
  CHECK(allowance.remaining() == 40U);
}

// ---- PSD -------------------------------------------------------------------------------

struct CraftedLayer {
  std::int32_t width{0};
  std::int32_t height{0};
  // RLE channel bytes (compression marker included) for R, G, B; empty means a zero-length
  // channel the way old Photoshop stores an empty layer.
  std::vector<Bytes> channels;
  // An optional user mask: its rectangle and the bytes of its -2 channel.
  std::optional<std::pair<std::int32_t, std::int32_t>> mask_size;
  Bytes mask_channel;
};

Bytes rle_channel(std::int32_t width, std::int32_t height, std::uint8_t value) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u16(1);  // RLE
  std::vector<Bytes> rows;
  for (std::int32_t y = 0; y < height; ++y) {
    rows.push_back(patchy::psd::encode_packbits_row(Bytes(static_cast<std::size_t>(width), value)));
  }
  for (const auto& row : rows) {
    writer.write_u16(static_cast<std::uint16_t>(row.size()));
  }
  for (const auto& row : rows) {
    writer.write_bytes(row);
  }
  return writer.bytes();
}

// A layered 8-bit RGB PSD with a 4 x 3 canvas holding the given layers.
Bytes crafted_layered_psd(const std::vector<CraftedLayer>& layers) {
  constexpr std::uint32_t kCanvasWidth = 4;
  constexpr std::uint32_t kCanvasHeight = 3;
  patchy::psd::BigEndianWriter layer_info;
  layer_info.write_u16(static_cast<std::uint16_t>(layers.size()));
  for (const auto& layer : layers) {
    layer_info.write_u32(0);
    layer_info.write_u32(0);
    layer_info.write_u32(static_cast<std::uint32_t>(layer.height));
    layer_info.write_u32(static_cast<std::uint32_t>(layer.width));
    const bool masked = layer.mask_size.has_value();
    layer_info.write_u16(static_cast<std::uint16_t>(layer.channels.size() + (masked ? 1U : 0U)));
    for (std::size_t channel = 0; channel < layer.channels.size(); ++channel) {
      layer_info.write_u16(static_cast<std::uint16_t>(channel));
      layer_info.write_u32(static_cast<std::uint32_t>(layer.channels[channel].size()));
    }
    if (masked) {
      layer_info.write_u16(0xFFFEU);  // user mask (-2)
      layer_info.write_u32(static_cast<std::uint32_t>(layer.mask_channel.size()));
    }
    patchy::test::write_ascii4(layer_info, "8BIM");
    patchy::test::write_ascii4(layer_info, "norm");
    layer_info.write_u8(255);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    layer_info.write_u8(0);
    patchy::psd::BigEndianWriter extra;
    if (masked) {
      extra.write_u32(20);
      extra.write_u32(0);
      extra.write_u32(0);
      extra.write_u32(static_cast<std::uint32_t>(layer.mask_size->second));
      extra.write_u32(static_cast<std::uint32_t>(layer.mask_size->first));
      extra.write_u8(0);  // default color
      extra.write_u8(0);  // flags
      extra.write_u16(0);
    } else {
      extra.write_u32(0);
    }
    extra.write_u32(0);  // no blending ranges
    patchy::test::write_pascal_padded(extra, "Crafted", 4);
    layer_info.write_u32(static_cast<std::uint32_t>(extra.bytes().size()));
    layer_info.write_bytes(extra.bytes());
  }
  for (const auto& layer : layers) {
    for (const auto& channel : layer.channels) {
      layer_info.write_bytes(channel);
    }
    layer_info.write_bytes(layer.mask_channel);
  }
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }
  patchy::psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(static_cast<std::uint32_t>(layer_info.bytes().size()));
  layer_mask.write_bytes(layer_info.bytes());
  layer_mask.write_u32(0);  // no global layer mask info

  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_header(writer, patchy::psd::Header{false, 3, kCanvasHeight, kCanvasWidth, 8, 3});
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  writer.write_bytes(layer_mask.bytes());
  writer.write_u16(0);  // raw composite
  writer.write_bytes(Bytes(3U * kCanvasWidth * kCanvasHeight, 0));
  return writer.bytes();
}

CraftedLayer honest_layer(std::int32_t width, std::int32_t height) {
  CraftedLayer layer{width, height, {}, std::nullopt, {}};
  for (std::uint8_t channel = 0; channel < 3; ++channel) {
    layer.channels.push_back(rle_channel(width, height, static_cast<std::uint8_t>(50 + channel)));
  }
  return layer;
}

CraftedLayer empty_channel_layer(std::int32_t width, std::int32_t height) {
  return CraftedLayer{width, height, {Bytes{}, Bytes{}, Bytes{}}, std::nullopt, {}};
}

void psd_layer_rect_larger_than_its_channel_data_is_rejected() {
  // The control: an honest small layer opens.
  const auto control = patchy::psd::DocumentIo::read(crafted_layered_psd({honest_layer(4, 3)}));
  CHECK(std::as_const(control).layers().size() == 1U);

  // 200000 x 200000 in a v1 PSD (160 GB as RGBA8) behind about a hundred bytes of RLE.
  auto oversized = honest_layer(4, 3);
  oversized.width = 200000;
  oversized.height = 200000;
  const auto bytes = crafted_layered_psd({oversized});
  CHECK(bytes.size() < 400U);
  CHECK(rejected_cleanly([&] { (void)patchy::psd::DocumentIo::read(bytes); }));
}

void psd_layers_without_channel_data_share_one_allowance() {
  // An old-Photoshop empty layer (zero-length channels) with a modest rectangle still opens.
  const auto small = patchy::psd::DocumentIo::read(crafted_layered_psd({empty_channel_layer(100, 80)}));
  CHECK(std::as_const(small).layers().size() == 1U);

  // One rectangle past the allowance (81 Mpx), then two that pass it only together.
  CHECK(rejected_cleanly(
      [] { (void)patchy::psd::DocumentIo::read(crafted_layered_psd({empty_channel_layer(9000, 9000)})); }));
  CHECK(rejected_cleanly([] {
    (void)patchy::psd::DocumentIo::read(
        crafted_layered_psd({empty_channel_layer(6000, 6000), empty_channel_layer(6000, 6000)}));
  }));
}

void psd_mask_channel_that_cannot_fill_its_rect_is_skipped_as_damaged() {
  auto layer = honest_layer(4, 3);
  layer.mask_size = std::make_pair(200000, 200000);
  layer.mask_channel = rle_channel(4, 3, 255);
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read(crafted_layered_psd({layer}), options);
  CHECK(std::as_const(document).layers().size() == 1U);
  CHECK(!std::as_const(document).layers().front().mask().has_value());
  bool damaged_notice = false;
  for (const auto& notice : notices) {
    damaged_notice = damaged_notice || notice.find("damaged") != std::string::npos;
  }
  CHECK(damaged_notice);
}

void psd_flat_composite_larger_than_its_data_is_rejected() {
  // A PSB with no layers whose header alone declares 300000 x 300000 RGB (270 GB).
  {
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_header(writer, patchy::psd::Header{true, 3, 300000, 300000, 8, 3});
    writer.write_u32(0);  // color mode data
    writer.write_u32(0);  // image resources
    writer.write_u64(0);  // empty layer and mask section
    writer.write_u16(0);  // raw composite
    writer.write_bytes(Bytes(16, 0));
    const auto bytes = writer.bytes();
    CHECK(rejected_cleanly([&] { (void)patchy::psd::DocumentIo::read(bytes); }));
  }
  // A v1 PSD at the 30000 cap whose RLE composite is only its row-count table.
  {
    constexpr std::uint32_t kSide = 30000;
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_header(writer, patchy::psd::Header{false, 3, kSide, kSide, 8, 3});
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u16(1);  // RLE
    writer.write_bytes(Bytes(3U * kSide * 2U, 0));
    const auto bytes = writer.bytes();
    CHECK(rejected_cleanly([&] { (void)patchy::psd::DocumentIo::read(bytes); }));
  }
  // The control: a small honest flat file opens.
  {
    patchy::psd::BigEndianWriter writer;
    patchy::psd::write_header(writer, patchy::psd::Header{false, 3, 3, 4, 8, 3});
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u32(0);
    writer.write_u16(0);
    writer.write_bytes(Bytes(3U * 4U * 3U, 77));
    const auto document = patchy::psd::DocumentIo::read(writer.bytes());
    CHECK(document.width() == 4 && document.height() == 3);
  }
}

// ---- Radiance HDR -----------------------------------------------------------------------

void hdr_resolution_larger_than_its_data_is_rejected() {
  const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 65535 +X 65535\n";
  Bytes bytes(header.begin(), header.end());
  bytes.insert(bytes.end(), 16U, std::uint8_t{0x80});
  CHECK(rejected_cleanly([&] { (void)patchy::hdr::read_hdr(bytes); }));
}

// ---- OpenEXR ----------------------------------------------------------------------------

struct ExrAttribute {
  std::string name;
  std::string type;
  Bytes value;
};

struct ExrParts {
  Bytes preamble;  // magic and version
  std::vector<ExrAttribute> attributes;
  Bytes rest;  // offset table and chunks
};

ExrParts split_exr(const Bytes& bytes) {
  ExrParts parts;
  parts.preamble.assign(bytes.begin(), bytes.begin() + 8);
  std::size_t at = 8;
  const auto read_string = [&] {
    std::string text;
    while (bytes.at(at) != 0) {
      text.push_back(static_cast<char>(bytes[at++]));
    }
    ++at;
    return text;
  };
  while (bytes.at(at) != 0) {
    ExrAttribute attribute;
    attribute.name = read_string();
    attribute.type = read_string();
    const auto size = static_cast<std::size_t>(bytes[at]) | (static_cast<std::size_t>(bytes[at + 1]) << 8U) |
                      (static_cast<std::size_t>(bytes[at + 2]) << 16U) |
                      (static_cast<std::size_t>(bytes[at + 3]) << 24U);
    at += 4;
    attribute.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                           bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
    at += size;
    parts.attributes.push_back(std::move(attribute));
  }
  ++at;  // header terminator
  parts.rest.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
  return parts;
}

void append_le32(Bytes& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

// Reassembles the file; the single scanline block's offset moves by the header growth.
Bytes join_exr(const ExrParts& parts, std::size_t original_header_end) {
  Bytes out = parts.preamble;
  for (const auto& attribute : parts.attributes) {
    out.insert(out.end(), attribute.name.begin(), attribute.name.end());
    out.push_back(0);
    out.insert(out.end(), attribute.type.begin(), attribute.type.end());
    out.push_back(0);
    append_le32(out, static_cast<std::uint32_t>(attribute.value.size()));
    out.insert(out.end(), attribute.value.begin(), attribute.value.end());
  }
  out.push_back(0);
  const auto growth = out.size() - original_header_end;
  Bytes rest = parts.rest;
  std::uint64_t offset = 0;
  for (int i = 7; i >= 0; --i) {
    offset = (offset << 8U) | rest[static_cast<std::size_t>(i)];
  }
  offset += growth;
  for (std::size_t i = 0; i < 8U; ++i) {
    rest[i] = static_cast<std::uint8_t>(offset >> (8U * i));
  }
  out.insert(out.end(), rest.begin(), rest.end());
  return out;
}

ExrAttribute chunk_count_attribute(std::uint32_t count) {
  ExrAttribute attribute{"chunkCount", "int", {}};
  append_le32(attribute.value, count);
  return attribute;
}

void exr_declared_size_larger_than_its_data_is_rejected() {
  patchy::Document document(13, 7, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", patchy::PixelBuffer(13, 7, patchy::PixelFormat::rgba8()));
  const auto written = patchy::exr::write_exr(document);
  const auto parts = split_exr(written);
  const auto header_end = written.size() - parts.rest.size();
  CHECK(parts.rest.size() > 8U);

  // The control: the file reassembled with a matching chunkCount (one ZIP block) opens.
  {
    auto with_count = parts;
    with_count.attributes.push_back(chunk_count_attribute(1));
    const auto reopened = patchy::exr::read_exr(join_exr(with_count, header_end));
    CHECK(reopened.document.width() == 13 && reopened.document.height() == 7);
  }
  // chunkCount sizes tinyexr's offset table before anything is read: 2^31 - 1 entries.
  {
    auto huge_count = parts;
    huge_count.attributes.push_back(chunk_count_attribute(0x7FFFFFFFU));
    const auto bytes = join_exr(huge_count, header_end);
    CHECK(rejected_cleanly([&] { (void)patchy::exr::read_exr(bytes); }));
  }
  // A data window of 8388608 x 400 (tinyexr's width cap): 48 GB of float channels from
  // a file of a few hundred bytes.
  {
    auto wide = parts;
    bool patched = false;
    for (auto& attribute : wide.attributes) {
      if (attribute.name == "dataWindow" && attribute.value.size() == 16U) {
        attribute.value.clear();
        append_le32(attribute.value, 0);
        append_le32(attribute.value, 0);
        append_le32(attribute.value, 8388607U);
        append_le32(attribute.value, 399U);
        patched = true;
      }
    }
    CHECK(patched);
    const auto bytes = join_exr(wide, header_end);
    CHECK(rejected_cleanly([&] { (void)patchy::exr::read_exr(bytes); }));
  }
}

// ---- PCX --------------------------------------------------------------------------------

// A 24-bit RLE PCX whose rows (3 planes x 43691 bytes) times 32768 lines is 2^32 + 32768
// bytes: on a 32-bit size_t that wrapped to a 32 KB decode that the row loop then read far
// past. The body holds exactly those 32768 bytes of runs, so only the size check stops it.
void pcx_decoded_size_that_wraps_32_bits_is_rejected() {
  Bytes bytes(128, 0);
  bytes[0] = 0x0A;
  bytes[1] = 5;
  bytes[2] = 1;  // RLE
  bytes[3] = 8;
  const std::uint16_t x_max = 43690;
  const std::uint16_t y_max = 32767;
  bytes[8] = static_cast<std::uint8_t>(x_max & 0xFFU);
  bytes[9] = static_cast<std::uint8_t>(x_max >> 8U);
  bytes[10] = static_cast<std::uint8_t>(y_max & 0xFFU);
  bytes[11] = static_cast<std::uint8_t>(y_max >> 8U);
  bytes[65] = 3;  // planes
  const std::uint16_t bytes_per_line = 43691;
  bytes[66] = static_cast<std::uint8_t>(bytes_per_line & 0xFFU);
  bytes[67] = static_cast<std::uint8_t>(bytes_per_line >> 8U);
  for (std::size_t written = 0; written < 32768U; written += 63U) {
    const auto run = std::min<std::size_t>(63U, 32768U - written);
    bytes.push_back(static_cast<std::uint8_t>(0xC0U | run));
    bytes.push_back(0x11);
  }
  CHECK(patchy::pcx::DocumentIo::can_read(bytes));
  CHECK(rejected_cleanly([&] { (void)patchy::pcx::DocumentIo::read(bytes); }));
}

}  // namespace

std::vector<patchy::test::TestCase> decode_budget_tests() {
  return {
      {"decode_budget_checks_products_and_the_32_bit_cap", decode_budget_checks_products_and_the_32_bit_cap},
      {"psd_layer_rect_larger_than_its_channel_data_is_rejected",
       psd_layer_rect_larger_than_its_channel_data_is_rejected},
      {"psd_layers_without_channel_data_share_one_allowance", psd_layers_without_channel_data_share_one_allowance},
      {"psd_mask_channel_that_cannot_fill_its_rect_is_skipped_as_damaged",
       psd_mask_channel_that_cannot_fill_its_rect_is_skipped_as_damaged},
      {"psd_flat_composite_larger_than_its_data_is_rejected", psd_flat_composite_larger_than_its_data_is_rejected},
      {"hdr_resolution_larger_than_its_data_is_rejected", hdr_resolution_larger_than_its_data_is_rejected},
      {"exr_declared_size_larger_than_its_data_is_rejected", exr_declared_size_larger_than_its_data_is_rejected},
      {"pcx_decoded_size_that_wraps_32_bits_is_rejected", pcx_decoded_size_that_wraps_32_bits_is_rejected},
  };
}
