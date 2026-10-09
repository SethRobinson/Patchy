#include "core/document.hpp"
#include "formats/binary_le.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/dds_document_io.hpp"
#include "formats/format_registry.hpp"
#include "formats/jxr_document_io.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace dds = patchy::dds;
using dds::Compression;
using dds::WriteOptions;

// A gradient with a distinct top-left pixel, so a flipped or shifted decode produces wrong
// values instead of a plausible picture. `translucent` ramps alpha left to right and clears
// the bottom row, which is what flips Automatic to BC3 and exercises every alpha path.
[[nodiscard]] patchy::Document gradient_document(std::int32_t width, std::int32_t height, bool translucent) {
  patchy::Document document(width, height, translucent ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
      pixel[0] = static_cast<std::uint8_t>((x * 37 + 5) & 255);
      pixel[1] = static_cast<std::uint8_t>((y * 59 + 9) & 255);
      pixel[2] = static_cast<std::uint8_t>(((x + y) * 13 + 200) & 255);
      pixel[3] = 255;
      if (translucent) {
        pixel[3] = static_cast<std::uint8_t>(std::min(255, (x * 255) / std::max(1, width - 1)));
        if (y == height - 1) {
          pixel[3] = 0;
        }
      }
    }
  }
  pixels.row(0)[0] = 250;
  pixels.row(0)[1] = 10;
  pixels.row(0)[2] = 20;
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

// A gradient that varies slowly (at most 16 levels per texel step): what block compression
// is designed for, so BC1/BC3 quality measurements use it. gradient_document above changes
// by 37 and 59 levels per step in two independent directions inside every 4x4 block, which
// no four-colour line can follow (Pillow decodes it at 16 dB too); it stays the input for
// the exactness tests, never for a quality bound.
[[nodiscard]] patchy::Document smooth_document(std::int32_t width, std::int32_t height, bool translucent) {
  patchy::Document document(width, height, translucent ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
      pixel[0] = static_cast<std::uint8_t>(std::min(255, 20 + x * 8));
      pixel[1] = static_cast<std::uint8_t>(std::min(255, 30 + y * 12));
      pixel[2] = static_cast<std::uint8_t>(std::max(0, 200 - (x + y) * 4));
      pixel[3] = 255;
      if (translucent) {
        pixel[3] = static_cast<std::uint8_t>(std::min(255, (x * 255) / std::max(1, width - 1)));
        if (y == height - 1) {
          pixel[3] = 0;
        }
      }
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

[[nodiscard]] std::array<int, 4> pixel_at(const patchy::Document& document, std::int32_t x, std::int32_t y,
                                          std::size_t layer_index = 0) {
  const auto& pixels = std::as_const(document.layers()[layer_index]).pixels();
  const auto channels = pixels.format().channels;
  const auto* source = pixels.row(y).data() + static_cast<std::size_t>(x) * channels;
  return {source[0], source[1], source[2], channels >= 4 ? source[3] : 255};
}

[[nodiscard]] int channel_count(const patchy::Document& document) {
  return std::as_const(document.layers().front()).pixels().format().channels;
}

[[nodiscard]] std::string metadata_value(const patchy::Document& document, const char* key) {
  const auto& values = document.metadata().values;
  const auto found = values.find(key);
  return found == values.end() ? std::string() : found->second;
}

[[nodiscard]] std::uint32_t u32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool has_notice(const std::vector<std::string>& notices, const char* fragment) {
  return std::any_of(notices.begin(), notices.end(),
                     [fragment](const std::string& notice) { return notice.find(fragment) != std::string::npos; });
}

struct HeaderSpec {
  std::uint32_t width{4};
  std::uint32_t height{4};
  std::uint32_t flags{dds::kFlagCaps | dds::kFlagHeight | dds::kFlagWidth | dds::kFlagPixelFormat};
  std::uint32_t pitch_or_linear_size{0};
  std::uint32_t depth{0};
  std::uint32_t mipmap_count{0};
  std::uint32_t pf_flags{dds::kPixelFormatRgb | dds::kPixelFormatAlphaPixels};
  std::uint32_t fourcc{0};
  std::uint32_t bit_count{32};
  std::uint32_t r_mask{0x00ff0000U};
  std::uint32_t g_mask{0x0000ff00U};
  std::uint32_t b_mask{0x000000ffU};
  std::uint32_t a_mask{0xff000000U};
  std::uint32_t caps{dds::kCapsTexture};
  std::uint32_t caps2{0};
  bool dx10{false};
  std::uint32_t dxgi_format{0};
  std::uint32_t resource_dimension{dds::kResourceDimensionTexture2D};
  std::uint32_t misc_flag{0};
  std::uint32_t array_size{1};
  std::uint32_t misc_flags2{0};
};

[[nodiscard]] std::vector<std::uint8_t> build_header(const HeaderSpec& spec) {
  patchy::LittleEndianWriter writer;
  writer.write_u32(dds::kMagic);
  writer.write_u32(124);
  writer.write_u32(spec.flags);
  writer.write_u32(spec.height);
  writer.write_u32(spec.width);
  writer.write_u32(spec.pitch_or_linear_size);
  writer.write_u32(spec.depth);
  writer.write_u32(spec.mipmap_count);
  for (int i = 0; i < 11; ++i) {
    writer.write_u32(0);
  }
  writer.write_u32(32);
  writer.write_u32(spec.dx10 ? dds::kPixelFormatFourCc : spec.pf_flags);
  writer.write_u32(spec.dx10 ? dds::kFourCcDx10 : spec.fourcc);
  writer.write_u32(spec.dx10 ? 0U : spec.bit_count);
  writer.write_u32(spec.dx10 ? 0U : spec.r_mask);
  writer.write_u32(spec.dx10 ? 0U : spec.g_mask);
  writer.write_u32(spec.dx10 ? 0U : spec.b_mask);
  writer.write_u32(spec.dx10 ? 0U : spec.a_mask);
  writer.write_u32(spec.caps);
  writer.write_u32(spec.caps2);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  if (spec.dx10) {
    writer.write_u32(spec.dxgi_format);
    writer.write_u32(spec.resource_dimension);
    writer.write_u32(spec.misc_flag);
    writer.write_u32(spec.array_size);
    writer.write_u32(spec.misc_flags2);
  }
  return writer.bytes();
}

[[nodiscard]] HeaderSpec fourcc_spec(std::uint32_t code, std::uint32_t width, std::uint32_t height) {
  HeaderSpec spec;
  spec.width = width;
  spec.height = height;
  spec.pf_flags = dds::kPixelFormatFourCc;
  spec.fourcc = code;
  spec.bit_count = 0;
  spec.r_mask = spec.g_mask = spec.b_mask = spec.a_mask = 0;
  return spec;
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

// A BC1 colour block: two 565 endpoints and 2-bit indices, texel 0 in the lowest bits.
void append_bc1_block(std::vector<std::uint8_t>& bytes, std::uint16_t c0, std::uint16_t c1, std::uint32_t indices) {
  append_u16(bytes, c0);
  append_u16(bytes, c1);
  append_u32(bytes, indices);
}

[[nodiscard]] std::uint32_t repeated_indices(std::uint32_t index) {
  std::uint32_t value = 0;
  for (unsigned texel = 0; texel < 16; ++texel) {
    value |= (index & 3U) << (2U * texel);
  }
  return value;
}

constexpr std::uint16_t kRed565 = 0xf800;
constexpr std::uint16_t kBlue565 = 0x001f;

void check_rgba(const std::array<int, 4>& actual, const std::array<int, 4>& expected, int tolerance) {
  for (std::size_t channel = 0; channel < 4; ++channel) {
    const bool ok = std::abs(actual[channel] - expected[channel]) <= tolerance;
    if (!ok) {
      std::cout << "  channel " << channel << ": got " << actual[channel] << " expected " << expected[channel] << "\n";
    }
    CHECK(ok);
  }
}

// ---------------------------------------------------------------------------------------

void dds_extensions_sniff_and_registry_routing() {
  CHECK(dds::dds_extensions() == std::vector<std::string>{"dds"});
  CHECK(dds::is_dds_extension("dds"));
  CHECK(dds::is_dds_extension(".DDS"));
  CHECK(!dds::is_dds_extension("rttex"));

  const auto* handler = patchy::builtin_format_registry().find_by_extension("dds");
  CHECK(handler != nullptr);
  CHECK(handler->identifier == "patchy.formats.dds");
  CHECK(handler->can_write());
  CHECK(handler->sniff != nullptr);

  const auto bytes = dds::write_dds(gradient_document(5, 4, /*translucent*/ false));
  CHECK(dds::sniff(bytes));
  CHECK(handler->sniff(bytes));
  std::vector<std::uint8_t> junk(200, 0x42);
  CHECK(!dds::sniff(junk));
  auto wrong_size = bytes;
  wrong_size[4] = 123;
  CHECK(!dds::sniff(wrong_size));

  // Tokens are compatibility contracts; the reader never emits "auto".
  CHECK(dds::compression_token(Compression::Automatic) == "auto");
  CHECK(dds::compression_token(Compression::Uncompressed) == "uncompressed");
  CHECK(dds::compression_token(Compression::Bc1) == "bc1");
  CHECK(dds::compression_token(Compression::Bc3) == "bc3");
  CHECK(dds::compression_token(Compression::Bc4) == "bc4");
  CHECK(dds::compression_token(Compression::Bc5) == "bc5");
  CHECK(dds::compression_token(Compression::Bc7) == "bc7");
  for (const auto compression : {Compression::Automatic, Compression::Uncompressed, Compression::Bc1, Compression::Bc3,
                                 Compression::Bc4, Compression::Bc5, Compression::Bc7}) {
    CHECK(dds::compression_from_token(dds::compression_token(compression)) == compression);
  }
  CHECK(!dds::compression_from_token("bogus").has_value());
  CHECK(!dds::compression_from_token("").has_value());
}

void dds_header_parse_validates_legacy_and_dx10_shapes() {
  {
    HeaderSpec spec;
    spec.width = 7;
    spec.height = 5;
    spec.mipmap_count = 0;  // reads as 1
    const auto header = dds::parse_header(build_header(spec));
    CHECK(header.width == 7);
    CHECK(header.height == 5);
    CHECK(header.mipmap_count == 1);
    CHECK(header.depth == 1);
    CHECK(!header.dx10.has_value());
    CHECK(header.data_offset == dds::kFileHeaderBytes);
    CHECK(header.pixel_format.a_mask == 0xff000000U);
    CHECK(dds::texture_layout(header) == dds::TextureLayout::Texture2D);
    CHECK(dds::alpha_mode(header) == dds::AlphaMode::Unknown);
    CHECK(dds::image_count(header) == 1);
    const auto format = dds::resolve_source_format(header);
    CHECK(format.kind == dds::SourceFormat::Kind::Masked);
    CHECK(format.name == "A8R8G8B8");
    CHECK(format.has_alpha);
    CHECK(dds::image_byte_size(format, 7, 5) == 7U * 5U * 4U);
  }
  {
    // Legacy cubemap with every face; the mip chain size covers the whole pyramid.
    auto spec = fourcc_spec(dds::kFourCcDxt5, 16, 16);
    spec.mipmap_count = 5;
    spec.caps = dds::kCapsTexture | dds::kCapsComplex | dds::kCapsMipmap;
    spec.caps2 = dds::kCaps2Cubemap | dds::kCaps2CubemapAllFaces;
    const auto header = dds::parse_header(build_header(spec));
    CHECK(dds::texture_layout(header) == dds::TextureLayout::Cubemap);
    CHECK(dds::image_count(header) == 6);
    const auto format = dds::resolve_source_format(header);
    CHECK(format.kind == dds::SourceFormat::Kind::Bc3);
    CHECK(format.name == "DXT5");
    CHECK(dds::image_byte_size(format, 16, 16) == 16U * 16U);  // 16 blocks of 16 bytes
    CHECK(dds::image_byte_size(format, 5, 3) == 2U * 16U);     // partial blocks round up
    CHECK(dds::mip_chain_byte_size(format, 16, 16, 5) == 256U + 64U + 16U + 16U + 16U);
    // Two face bits only: a partial cubemap holds two images.
    spec.caps2 = dds::kCaps2Cubemap | dds::kCaps2CubemapPositiveX | (dds::kCaps2CubemapPositiveX << 5U);
    CHECK(dds::image_count(dds::parse_header(build_header(spec))) == 2);
  }
  {
    // DXT2 and DXT4 are the premultiplied twins; DXT1 is not.
    CHECK(dds::alpha_mode(dds::parse_header(build_header(fourcc_spec(dds::kFourCcDxt2, 4, 4)))) ==
          dds::AlphaMode::Premultiplied);
    CHECK(dds::alpha_mode(dds::parse_header(build_header(fourcc_spec(dds::kFourCcDxt4, 4, 4)))) ==
          dds::AlphaMode::Premultiplied);
    CHECK(dds::alpha_mode(dds::parse_header(build_header(fourcc_spec(dds::kFourCcDxt1, 4, 4)))) ==
          dds::AlphaMode::Unknown);
    CHECK(dds::resolve_source_format(dds::parse_header(build_header(fourcc_spec(dds::kFourCcDxt2, 4, 4)))).premultiplied);
  }
  {
    // Legacy volume: depth from the header.
    HeaderSpec spec;
    spec.flags |= dds::kFlagDepth;
    spec.depth = 3;
    spec.caps2 = dds::kCaps2Volume;
    const auto header = dds::parse_header(build_header(spec));
    CHECK(dds::texture_layout(header) == dds::TextureLayout::Volume);
    CHECK(header.depth == 3);
    CHECK(dds::image_count(header) == 3);
  }
  {
    // DX10: array, cubemap array, 3D, alpha modes, data offset 148.
    HeaderSpec spec;
    spec.dx10 = true;
    spec.dxgi_format = dds::kDxgiB8G8R8A8Unorm;
    spec.array_size = 3;
    spec.misc_flags2 = 2;
    auto header = dds::parse_header(build_header(spec));
    CHECK(header.data_offset == dds::kFileHeaderBytes + dds::kDx10HeaderSize);
    CHECK(dds::texture_layout(header) == dds::TextureLayout::Array);
    CHECK(dds::image_count(header) == 3);
    CHECK(dds::alpha_mode(header) == dds::AlphaMode::Premultiplied);
    CHECK(dds::resolve_source_format(header).name == "B8G8R8A8_UNORM");

    spec.misc_flag = dds::kMiscFlagTextureCube;
    spec.array_size = 2;
    spec.misc_flags2 = 3;
    header = dds::parse_header(build_header(spec));
    CHECK(dds::texture_layout(header) == dds::TextureLayout::Cubemap);
    CHECK(dds::image_count(header) == 12);
    CHECK(dds::alpha_mode(header) == dds::AlphaMode::Opaque);

    spec.misc_flag = 0;
    spec.array_size = 1;
    spec.resource_dimension = dds::kResourceDimensionTexture3D;
    spec.flags |= dds::kFlagDepth;
    spec.depth = 2;
    spec.misc_flags2 = 4;
    header = dds::parse_header(build_header(spec));
    CHECK(dds::texture_layout(header) == dds::TextureLayout::Volume);
    CHECK(dds::image_count(header) == 2);
    CHECK(dds::alpha_mode(header) == dds::AlphaMode::Custom);

    spec.resource_dimension = dds::kResourceDimensionTexture1D;
    spec.flags = dds::kFlagCaps | dds::kFlagHeight | dds::kFlagWidth | dds::kFlagPixelFormat;
    spec.depth = 0;
    spec.height = 9;  // ignored for 1D
    spec.misc_flags2 = 1;
    header = dds::parse_header(build_header(spec));
    CHECK(header.height == 1);
    CHECK(dds::alpha_mode(header) == dds::AlphaMode::Straight);

    // TYPELESS block formats read as UNORM (Pillow writes BC5_TYPELESS).
    spec.resource_dimension = dds::kResourceDimensionTexture2D;
    spec.dxgi_format = 82;
    CHECK(dds::resolve_source_format(dds::parse_header(build_header(spec))).kind == dds::SourceFormat::Kind::Bc5);
    spec.dxgi_format = 97;
    CHECK(dds::resolve_source_format(dds::parse_header(build_header(spec))).name == "BC7_UNORM");
    spec.dxgi_format = dds::kDxgiBc4Snorm;
    const auto bc4s = dds::resolve_source_format(dds::parse_header(build_header(spec)));
    CHECK(bc4s.kind == dds::SourceFormat::Kind::Bc4);
    CHECK(bc4s.is_signed);
    CHECK(!bc4s.has_alpha);
  }
}

void dds_rejects_bad_magic_sizes_unknown_formats_and_truncation() {
  const auto expect_throw = [](const std::vector<std::uint8_t>& bytes, const char* fragment) {
    bool threw = false;
    try {
      (void)dds::read_dds(bytes);
    } catch (const std::exception& error) {
      threw = true;
      const bool matches = std::string(error.what()).find(fragment) != std::string::npos;
      if (!matches) {
        std::cout << "  unexpected message: " << error.what() << " (wanted '" << fragment << "')\n";
      }
      CHECK(matches);
    }
    CHECK(threw);
  };

  std::vector<std::uint8_t> not_dds(300, 0);
  std::memcpy(not_dds.data(), "RIFF", 4);
  expect_throw(not_dds, "signature");
  expect_throw({}, "signature");

  auto header = build_header(HeaderSpec{});
  header[4] = 120;
  expect_throw(header, "unexpected size");
  header = build_header(HeaderSpec{});
  header[76] = 31;
  expect_throw(header, "pixel format block");
  header = build_header(HeaderSpec{});
  expect_throw(std::vector<std::uint8_t>(header.begin(), header.begin() + 100), "truncated");

  HeaderSpec zero;
  zero.width = 0;
  expect_throw(build_header(zero), "invalid size");
  HeaderSpec huge;
  huge.width = 16385;
  expect_throw(build_header(huge), "invalid size");
  HeaderSpec too_many_pixels;
  too_many_pixels.width = 16384;
  too_many_pixels.height = 16384;
  too_many_pixels.flags |= dds::kFlagDepth;
  too_many_pixels.depth = 2;
  too_many_pixels.caps2 = dds::kCaps2Volume;
  expect_throw(build_header(too_many_pixels), "too many pixels");
  HeaderSpec many_mips;
  many_mips.mipmap_count = 40;
  expect_throw(build_header(many_mips), "mip levels");
  HeaderSpec deep;
  deep.flags |= dds::kFlagDepth;
  deep.depth = 5000;
  deep.caps2 = dds::kCaps2Volume;
  expect_throw(build_header(deep), "depth");
  HeaderSpec huge_array;
  huge_array.dx10 = true;
  huge_array.dxgi_format = dds::kDxgiR8G8B8A8Unorm;
  huge_array.array_size = 4000;
  expect_throw(build_header(huge_array), "array size");
  // Pillow writes arraySize 0 for a plain 2D texture; it reads as 1.
  huge_array.array_size = 0;
  CHECK(dds::parse_header(build_header(huge_array)).dx10->array_size == 1);
  HeaderSpec odd_dimension = huge_array;
  odd_dimension.array_size = 1;
  odd_dimension.resource_dimension = 7;
  expect_throw(build_header(odd_dimension), "resource dimension");

  // Unsupported pixel formats, by every spelling.
  HeaderSpec yuv;
  yuv.pf_flags = dds::kPixelFormatYuv;
  expect_throw(build_header(yuv), "YUV");
  HeaderSpec palette;
  palette.pf_flags = dds::kPixelFormatPaletteIndexed8;
  palette.bit_count = 8;
  expect_throw(build_header(palette), "Palettized");
  HeaderSpec bump;
  bump.pf_flags = dds::kPixelFormatBumpDuDv;
  expect_throw(build_header(bump), "Bump-map");
  HeaderSpec twelve_bits;
  twelve_bits.bit_count = 12;
  expect_throw(build_header(twelve_bits), "12 bits per pixel");
  HeaderSpec no_masks;
  no_masks.pf_flags = dds::kPixelFormatRgb;
  no_masks.r_mask = no_masks.g_mask = no_masks.b_mask = no_masks.a_mask = 0;
  expect_throw(build_header(no_masks), "no colour or alpha");
  HeaderSpec wide_mask;
  wide_mask.bit_count = 16;
  wide_mask.r_mask = 0xffff0000U;  // 16 bits wide but the pixel is 16 bits: slides down, fine
  wide_mask.g_mask = 0x0000ffffU;
  wide_mask.a_mask = 0;
  wide_mask.b_mask = 0;
  (void)dds::resolve_source_format(dds::parse_header(build_header(wide_mask)));
  wide_mask.r_mask = 0x3ffffU;  // 18 bits: too wide for any supported pixel
  expect_throw(build_header(wide_mask), "do not fit");
  expect_throw(build_header(fourcc_spec(dds::fourcc('U', 'Y', 'V', 'Y'), 4, 4)), "'UYVY'");
  expect_throw(build_header(fourcc_spec(111, 4, 4)), "D3D format 111");  // R16F
  HeaderSpec typeless;
  typeless.dx10 = true;
  typeless.dxgi_format = 1;  // R32G32B32A32_TYPELESS
  expect_throw(build_header(typeless), "DXGI format 1");
  typeless.dxgi_format = 999;
  expect_throw(build_header(typeless), "DXGI format 999");

  // Mip 0 must be complete.
  auto truncated = dds::write_dds(gradient_document(8, 8, false));
  truncated.resize(truncated.size() - 1);
  expect_throw(truncated, "truncated");
  // Later mip levels may be cut short: the top level still opens.
  WriteOptions mips;
  mips.generate_mipmaps = true;
  auto cut_mips = dds::write_dds(gradient_document(8, 8, false), mips);
  cut_mips.resize(cut_mips.size() - 3);
  const auto opened = dds::read_dds(cut_mips);
  CHECK(opened.document.width() == 8);
  CHECK(has_notice(opened.notices, "mip levels"));
}

void dds_bc1_decodes_four_color_and_punch_through_blocks() {
  // c0 > c1: the four-colour mode. Index 2 is 2/3 red + 1/3 blue, index 3 the reverse.
  auto bytes = build_header(fourcc_spec(dds::kFourCcDxt1, 4, 4));
  std::uint32_t indices = 0;
  for (unsigned texel = 0; texel < 16; ++texel) {
    indices |= (texel % 4U) << (2U * texel);
  }
  append_bc1_block(bytes, kRed565, kBlue565, indices);
  auto result = dds::read_dds(bytes);
  CHECK(channel_count(result.document) == 4);  // DXT1 may carry 1-bit alpha
  check_rgba(pixel_at(result.document, 0, 0), {255, 0, 0, 255}, 0);
  check_rgba(pixel_at(result.document, 1, 0), {0, 0, 255, 255}, 0);
  check_rgba(pixel_at(result.document, 2, 0), {170, 0, 85, 255}, 1);
  check_rgba(pixel_at(result.document, 3, 0), {85, 0, 170, 255}, 1);
  CHECK(metadata_value(result.document, dds::kMetadataCompression) == "bc1");
  CHECK(metadata_value(result.document, dds::kMetadataSourceFormat) == "DXT1");

  // c0 <= c1: the three-colour mode, index 3 transparent black.
  bytes = build_header(fourcc_spec(dds::kFourCcDxt1, 4, 4));
  append_bc1_block(bytes, kBlue565, kRed565, indices);
  result = dds::read_dds(bytes);
  check_rgba(pixel_at(result.document, 0, 1), {0, 0, 255, 255}, 0);
  check_rgba(pixel_at(result.document, 1, 1), {255, 0, 0, 255}, 0);
  check_rgba(pixel_at(result.document, 2, 1), {127, 0, 127, 255}, 1);
  check_rgba(pixel_at(result.document, 3, 1), {0, 0, 0, 0}, 0);

  // A 5x3 texture holds two blocks; only the in-image overlap is written.
  bytes = build_header(fourcc_spec(dds::kFourCcDxt1, 5, 3));
  append_bc1_block(bytes, kRed565, kRed565, repeated_indices(0));
  append_bc1_block(bytes, kBlue565, kBlue565, repeated_indices(0));
  result = dds::read_dds(bytes);
  CHECK(result.document.width() == 5);
  CHECK(result.document.height() == 3);
  check_rgba(pixel_at(result.document, 3, 2), {255, 0, 0, 255}, 0);
  check_rgba(pixel_at(result.document, 4, 0), {0, 0, 255, 255}, 0);
  check_rgba(pixel_at(result.document, 4, 2), {0, 0, 255, 255}, 0);
}

void dds_bc2_bc3_bc4_bc5_decode_hand_built_blocks() {
  // BC2: 16 explicit 4-bit alphas (nibble * 17) then a colour block.
  auto bytes = build_header(fourcc_spec(dds::kFourCcDxt3, 4, 4));
  for (int row = 0; row < 4; ++row) {
    append_u16(bytes, static_cast<std::uint16_t>(0xF840));  // alphas 0, 4, 8, 15 across the row
  }
  append_bc1_block(bytes, kRed565, kRed565, repeated_indices(0));
  auto result = dds::read_dds(bytes);
  check_rgba(pixel_at(result.document, 0, 0), {255, 0, 0, 0}, 0);
  check_rgba(pixel_at(result.document, 1, 0), {255, 0, 0, 68}, 0);
  check_rgba(pixel_at(result.document, 2, 0), {255, 0, 0, 136}, 0);
  check_rgba(pixel_at(result.document, 3, 0), {255, 0, 0, 255}, 0);
  CHECK(metadata_value(result.document, dds::kMetadataCompression) == "bc3");

  // BC3: alpha0 > alpha1 selects the 8-value ramp; index 2 is 6/7 of the way to alpha0.
  bytes = build_header(fourcc_spec(dds::kFourCcDxt5, 4, 4));
  bytes.push_back(255);  // alpha0
  bytes.push_back(0);    // alpha1
  // 3-bit indices, texel 0 lowest: 0, 1, 2, 7 then zeros.
  const std::uint64_t alpha_indices = 0ULL | (1ULL << 3U) | (2ULL << 6U) | (7ULL << 9U);
  for (unsigned byte = 0; byte < 6; ++byte) {
    bytes.push_back(static_cast<std::uint8_t>((alpha_indices >> (8U * byte)) & 0xffU));
  }
  append_bc1_block(bytes, kBlue565, kBlue565, repeated_indices(0));
  result = dds::read_dds(bytes);
  check_rgba(pixel_at(result.document, 0, 0), {0, 0, 255, 255}, 0);
  check_rgba(pixel_at(result.document, 1, 0), {0, 0, 255, 0}, 0);
  check_rgba(pixel_at(result.document, 2, 0), {0, 0, 255, 219}, 1);
  check_rgba(pixel_at(result.document, 3, 0), {0, 0, 255, 36}, 1);
  CHECK(metadata_value(result.document, dds::kMetadataSourceFormat) == "DXT5");

  // BC4: one BC3-style alpha block read as gray; the result has no alpha channel.
  bytes = build_header(fourcc_spec(dds::kFourCcAti1, 4, 4));
  bytes.push_back(255);
  bytes.push_back(0);
  for (unsigned byte = 0; byte < 6; ++byte) {
    bytes.push_back(static_cast<std::uint8_t>((alpha_indices >> (8U * byte)) & 0xffU));
  }
  result = dds::read_dds(bytes);
  CHECK(channel_count(result.document) == 3);
  check_rgba(pixel_at(result.document, 0, 0), {255, 255, 255, 255}, 0);
  check_rgba(pixel_at(result.document, 1, 0), {0, 0, 0, 255}, 0);
  check_rgba(pixel_at(result.document, 2, 0), {219, 219, 219, 255}, 1);
  CHECK(metadata_value(result.document, dds::kMetadataCompression) == "bc4");

  // BC5: red block then green block, blue forced to 0 with a notice.
  bytes = build_header(fourcc_spec(dds::kFourCcAti2, 4, 4));
  bytes.push_back(200);  // red: constant 200 (alpha0 == alpha1 selects the 6-value ramp, index 0 = alpha0)
  bytes.push_back(200);
  bytes.resize(bytes.size() + 6, 0);
  bytes.push_back(255);  // green ramp as above
  bytes.push_back(0);
  for (unsigned byte = 0; byte < 6; ++byte) {
    bytes.push_back(static_cast<std::uint8_t>((alpha_indices >> (8U * byte)) & 0xffU));
  }
  result = dds::read_dds(bytes);
  CHECK(channel_count(result.document) == 3);
  check_rgba(pixel_at(result.document, 0, 0), {200, 255, 0, 255}, 0);
  check_rgba(pixel_at(result.document, 1, 0), {200, 0, 0, 255}, 0);
  CHECK(has_notice(result.notices, "BC5"));

  // Signed BC4 (DX10 BC4_SNORM): -127 reads as 0, 127 as 255, 0 as 128.
  HeaderSpec snorm;
  snorm.dx10 = true;
  snorm.dxgi_format = dds::kDxgiBc4Snorm;
  bytes = build_header(snorm);
  bytes.push_back(static_cast<std::uint8_t>(static_cast<signed char>(127)));
  bytes.push_back(static_cast<std::uint8_t>(static_cast<signed char>(-127)));
  const std::uint64_t snorm_indices = 0ULL | (1ULL << 3U) | (6ULL << 6U) | (7ULL << 9U);
  for (unsigned byte = 0; byte < 6; ++byte) {
    bytes.push_back(static_cast<std::uint8_t>((snorm_indices >> (8U * byte)) & 0xffU));
  }
  result = dds::read_dds(bytes);
  check_rgba(pixel_at(result.document, 0, 0), {255, 255, 255, 255}, 0);
  check_rgba(pixel_at(result.document, 1, 0), {0, 0, 0, 255}, 0);
  // alpha0 > alpha1 is the 8-value ramp, so indices 6 and 7 interpolate (2/7 and 1/7 of the
  // way from a1 to a0: -54 and -91) instead of being the -1/+1 constants.
  check_rgba(pixel_at(result.document, 2, 0), {73, 73, 73, 255}, 1);
  check_rgba(pixel_at(result.document, 3, 0), {36, 36, 36, 255}, 1);
  CHECK(metadata_value(result.document, dds::kMetadataSourceFormat) == "BC4_SNORM");
}

void dds_premultiplied_sources_unpremultiply_with_notice() {
  // The integer rule on a bare span.
  std::vector<std::uint8_t> rgba = {100, 50, 0, 200, 10, 10, 10, 0, 64, 64, 64, 64, 255, 255, 255, 255};
  dds::unpremultiply_rgba8_in_place(rgba);
  CHECK((rgba[0] == 128 && rgba[1] == 64 && rgba[2] == 0 && rgba[3] == 200));  // (100*255+100)/200 = 128
  CHECK((rgba[4] == 0 && rgba[5] == 0 && rgba[6] == 0 && rgba[7] == 0));       // alpha 0 clears the colour
  CHECK((rgba[8] == 255 && rgba[9] == 255 && rgba[10] == 255 && rgba[11] == 64));
  CHECK((rgba[12] == 255 && rgba[15] == 255));

  // A DX10 R8G8B8A8 file flagged premultiplied: the pixels come back straight, once.
  HeaderSpec spec;
  spec.dx10 = true;
  spec.dxgi_format = dds::kDxgiR8G8B8A8Unorm;
  spec.width = 2;
  spec.height = 1;
  spec.misc_flags2 = 2;
  auto bytes = build_header(spec);
  const std::uint8_t pixels[8] = {50, 25, 0, 100, 0, 0, 0, 0};
  bytes.insert(bytes.end(), pixels, pixels + 8);
  auto result = dds::read_dds(bytes);
  check_rgba(pixel_at(result.document, 0, 0), {128, 64, 0, 100}, 0);  // (50*255+50)/100 = 128
  check_rgba(pixel_at(result.document, 1, 0), {0, 0, 0, 0}, 0);
  CHECK(has_notice(result.notices, "Premultiplied alpha"));
  CHECK(result.notices.size() == 1);

  // Opaque mode: alpha ignored, the layer has three channels. Custom: kept straight.
  spec.misc_flags2 = 3;
  bytes = build_header(spec);
  bytes.insert(bytes.end(), pixels, pixels + 8);
  result = dds::read_dds(bytes);
  CHECK(channel_count(result.document) == 3);
  check_rgba(pixel_at(result.document, 0, 0), {50, 25, 0, 255}, 0);
  CHECK(has_notice(result.notices, "unused"));
  spec.misc_flags2 = 4;
  bytes = build_header(spec);
  bytes.insert(bytes.end(), pixels, pixels + 8);
  result = dds::read_dds(bytes);
  CHECK(channel_count(result.document) == 4);
  check_rgba(pixel_at(result.document, 0, 0), {50, 25, 0, 100}, 0);
  CHECK(has_notice(result.notices, "custom data"));
  // Straight (1) and unknown (0): no alpha notice at all.
  spec.misc_flags2 = 1;
  bytes = build_header(spec);
  bytes.insert(bytes.end(), pixels, pixels + 8);
  CHECK(dds::read_dds(bytes).notices.empty());
}

void dds_reads_committed_fixtures() {
  // Files written by Pillow, by texconv (DirectXTex) and by scripts/dev/dds/make_dds_fixtures.py;
  // the expected values come from Pillow's independent decoders, or for the masked formats
  // from the quantized codes the generator wrote. Pillow and bcdec differ by at most 1 on
  // interpolated BC texels, and the premultiplied files allow 2 for the division.
  struct Sample {
    std::int32_t x;
    std::int32_t y;
    std::array<int, 4> rgba;
  };
  struct Fixture {
    const char* name;
    std::int32_t width;
    std::int32_t height;
    int channels;
    const char* compression;
    const char* source_format;
    int tolerance;
    std::vector<Sample> samples;
  };
  const std::vector<Fixture> fixtures = {
      {"pillow-a8r8g8b8-9x7.dds", 9, 7, 4, "uncompressed", "A8R8G8B8", 0,
       {{0, 0, {250, 10, 20, 0}}, {0, 6, {5, 107, 22, 0}}, {4, 3, {153, 186, 35, 127}}, {8, 0, {45, 9, 48, 255}},
        {8, 6, {45, 107, 126, 0}}}},
      {"pillow-r8g8b8-9x7.dds", 9, 7, 3, "uncompressed", "R8G8B8", 0,
       {{0, 0, {250, 10, 20, 255}}, {0, 6, {5, 107, 22, 255}}, {4, 3, {153, 186, 35, 255}}, {8, 0, {45, 9, 48, 255}},
        {8, 6, {45, 107, 126, 255}}}},
      // Pillow writes its luminance mask as 0xFF000000 on an 8-bit pixel; the reader slides it down.
      {"pillow-l8-8x8.dds", 8, 8, 3, "uncompressed", "L8", 0,
       {{0, 0, {83, 83, 83, 255}}, {0, 7, {103, 103, 103, 255}}, {4, 4, {195, 195, 195, 255}},
        {7, 0, {12, 12, 12, 255}}, {7, 7, {114, 114, 114, 255}}}},
      {"pillow-l8a8-8x8.dds", 8, 8, 4, "uncompressed", "A8L8", 0,
       {{0, 0, {83, 83, 83, 0}}, {0, 7, {103, 103, 103, 0}}, {4, 4, {195, 195, 195, 145}}, {7, 0, {12, 12, 12, 255}},
        {7, 7, {114, 114, 114, 0}}}},
      {"pillow-dxt1-opaque-16x16.dds", 16, 16, 4, "bc1", "DXT1", 1,
       {{0, 0, {255, 8, 16, 255}}, {0, 15, {76, 140, 131, 255}}, {8, 8, {117, 160, 181, 255}},
        {15, 0, {82, 67, 134, 255}}, {15, 15, {231, 207, 8, 255}}}},
      {"pillow-dxt3-16x16.dds", 16, 16, 4, "bc3", "DXT3", 1,
       {{0, 0, {255, 8, 16, 0}}, {0, 15, {76, 140, 131, 0}}, {8, 8, {117, 160, 181, 136}}, {15, 0, {82, 67, 134, 255}},
        {15, 15, {231, 207, 8, 0}}}},
      {"pillow-dxt5-16x16.dds", 16, 16, 4, "bc3", "DXT5", 1,
       {{0, 0, {255, 8, 16, 0}}, {0, 15, {76, 140, 131, 0}}, {8, 8, {117, 160, 181, 146}}, {15, 0, {82, 67, 134, 255}},
        {15, 15, {231, 207, 8, 0}}}},
      {"pillow-bc5-typeless-8x8.dds", 8, 8, 3, "bc5", "BC5_UNORM", 1,
       {{0, 0, {201, 44, 0, 255}}, {0, 7, {27, 166, 0, 255}}, {4, 4, {139, 205, 0, 255}}, {7, 0, {51, 44, 0, 255}},
        {7, 7, {51, 166, 0, 255}}}},
      {"synth-dxt2-premultiplied-8x8.dds", 8, 8, 4, "bc3", "DXT2", 2,
       {{0, 0, {0, 0, 0, 0}}, {0, 7, {0, 0, 0, 0}}, {4, 4, {255, 255, 255, 17}}, {7, 0, {8, 12, 33, 255}},
        {7, 7, {0, 0, 0, 0}}}},
      {"synth-dxt4-premultiplied-8x8.dds", 8, 8, 4, "bc3", "DXT4", 2,
       {{0, 0, {0, 0, 0, 0}}, {0, 7, {0, 0, 0, 0}}, {4, 4, {220, 238, 73, 153}}, {7, 0, {8, 12, 33, 255}},
        {7, 7, {0, 0, 0, 0}}}},
      {"synth-x8r8g8b8-5x3.dds", 5, 3, 3, "uncompressed", "X8R8G8B8", 0,
       {{0, 0, {250, 10, 20, 255}}, {0, 2, {12, 134, 233, 255}}, {2, 1, {86, 75, 246, 255}}, {4, 0, {160, 16, 3, 255}},
        {4, 2, {160, 134, 29, 255}}}},
      {"synth-r5g6b5-5x3.dds", 5, 3, 3, "uncompressed", "R5G6B5", 0,
       {{0, 0, {247, 8, 16, 255}}, {0, 2, {8, 134, 231, 255}}, {2, 1, {82, 77, 247, 255}}, {4, 0, {156, 16, 0, 255}},
        {4, 2, {156, 134, 33, 255}}}},
      {"synth-a1r5g5b5-5x3.dds", 5, 3, 4, "uncompressed", "A1R5G5B5", 0,
       {{0, 0, {247, 8, 16, 0}}, {0, 2, {8, 132, 231, 0}}, {2, 1, {82, 74, 247, 0}}, {4, 0, {156, 16, 0, 255}},
        {4, 2, {156, 132, 33, 0}}}},
      {"synth-a4r4g4b4-5x3.dds", 5, 3, 4, "uncompressed", "A4R4G4B4", 0,
       {{0, 0, {255, 17, 17, 0}}, {0, 2, {17, 136, 238, 0}}, {2, 1, {85, 68, 238, 119}}, {4, 0, {153, 17, 0, 255}},
        {4, 2, {153, 136, 34, 0}}}},
      {"synth-a2r10g10b10-5x3.dds", 5, 3, 4, "uncompressed", "A2R10G10B10", 0,
       {{0, 0, {250, 10, 20, 0}}, {0, 2, {12, 134, 233, 0}}, {2, 1, {86, 75, 246, 85}}, {4, 0, {160, 16, 3, 255}},
        {4, 2, {160, 134, 29, 0}}}},
      {"synth-l8-5x3.dds", 5, 3, 3, "uncompressed", "L8", 0,
       {{0, 0, {82, 82, 82, 255}}, {0, 2, {108, 108, 108, 255}}, {2, 1, {97, 97, 97, 255}}, {4, 0, {57, 57, 57, 255}},
        {4, 2, {129, 129, 129, 255}}}},
      {"synth-a8l8-5x3.dds", 5, 3, 4, "uncompressed", "A8L8", 0,
       {{0, 0, {82, 82, 82, 0}}, {0, 2, {108, 108, 108, 0}}, {2, 1, {97, 97, 97, 127}}, {4, 0, {57, 57, 57, 255}},
        {4, 2, {129, 129, 129, 0}}}},
      {"synth-a8-5x3.dds", 5, 3, 4, "uncompressed", "A8", 0,
       {{0, 0, {255, 255, 255, 0}}, {0, 2, {255, 255, 255, 0}}, {2, 1, {255, 255, 255, 127}}, {4, 0, {255, 255, 255, 255}},
        {4, 2, {255, 255, 255, 0}}}},
      {"synth-dx10-r8g8b8a8-premultiplied-flag-4x4.dds", 4, 4, 4, "uncompressed", "R8G8B8A8_UNORM", 0,
       {{0, 0, {0, 0, 0, 0}}, {0, 3, {0, 0, 0, 0}}, {2, 2, {90, 138, 8, 170}}, {3, 0, {127, 20, 250, 255}},
        {3, 3, {0, 0, 0, 0}}}},
      {"synth-dx10-r8g8b8a8-opaque-flag-4x4.dds", 4, 4, 3, "uncompressed", "R8G8B8A8_UNORM", 0,
       {{0, 0, {250, 10, 20, 255}}, {0, 3, {16, 197, 250, 255}}, {2, 2, {90, 138, 7, 255}}, {3, 0, {127, 20, 250, 255}},
        {3, 3, {127, 197, 33, 255}}}},
      {"synth-dx10-r8g8b8a8-custom-flag-4x4.dds", 4, 4, 4, "uncompressed", "R8G8B8A8_UNORM", 0,
       {{0, 0, {250, 10, 20, 0}}, {0, 3, {16, 197, 250, 0}}, {2, 2, {90, 138, 7, 170}}, {3, 0, {127, 20, 250, 255}},
        {3, 3, {127, 197, 33, 0}}}},
      {"synth-dx10-r16g16b16a16-unorm-4x4.dds", 4, 4, 4, "uncompressed", "R16G16B16A16_UNORM", 0,
       {{0, 0, {250, 10, 20, 0}}, {0, 3, {16, 197, 250, 0}}, {2, 2, {90, 138, 7, 170}}, {3, 0, {127, 20, 250, 255}},
        {3, 3, {127, 197, 33, 0}}}},
      {"synth-dxt1-mipmapped-16x16.dds", 16, 16, 4, "bc1", "DXT1", 1,
       {{0, 0, {255, 8, 16, 255}}, {0, 15, {46, 78, 126, 255}}, {8, 8, {126, 164, 189, 255}}, {15, 0, {90, 71, 140, 255}},
        {15, 15, {164, 144, 24, 255}}}},
      {"texconv-bc7-16x16.dds", 16, 16, 4, "bc7", "BC7_UNORM", 1,
       {{0, 0, {150, 118, 4, 28}}, {0, 15, {31, 150, 119, 7}}, {8, 8, {57, 235, 165, 140}}, {15, 0, {20, 20, 134, 239}},
        {15, 15, {91, 133, 62, 3}}}},
      {"texconv-bc4-8x8.dds", 8, 8, 3, "bc4", "BC4U", 1,
       {{0, 0, {84, 84, 84, 255}}, {0, 7, {111, 111, 111, 255}}, {4, 4, {62, 62, 62, 255}}, {7, 0, {23, 23, 23, 255}},
        {7, 7, {134, 134, 134, 255}}}},
  };
  for (const auto& fixture : fixtures) {
    std::cout << "  " << fixture.name << "\n";
    const auto bytes = patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("dds", fixture.name));
    CHECK(!bytes.empty());
    CHECK(dds::sniff(bytes));
    const auto result = dds::read_dds(bytes);
    CHECK(result.document.width() == fixture.width);
    CHECK(result.document.height() == fixture.height);
    CHECK(result.document.layers().size() == 1);
    CHECK(channel_count(result.document) == fixture.channels);
    CHECK(metadata_value(result.document, dds::kMetadataCompression) == fixture.compression);
    CHECK(metadata_value(result.document, dds::kMetadataCompression) != "auto");
    CHECK(metadata_value(result.document, dds::kMetadataSourceFormat) == fixture.source_format);
    const bool mipmapped = std::string(fixture.name).find("mipmapped") != std::string::npos;
    CHECK(metadata_value(result.document, dds::kMetadataMipmaps) == (mipmapped ? "1" : "0"));
    for (const auto& sample : fixture.samples) {
      check_rgba(pixel_at(result.document, sample.x, sample.y), sample.rgba, fixture.tolerance);
    }
  }
}

void dds_float_sources_tone_map_like_jpeg_xr() {
  // half_to_float is bit-exact, subnormals and specials included.
  CHECK(dds::half_to_float(0x3C00) == 1.0F);
  CHECK(dds::half_to_float(0xC000) == -2.0F);
  CHECK(dds::half_to_float(0x0000) == 0.0F);
  CHECK(dds::half_to_float(0x0001) == 5.960464477539063e-08F);
  CHECK(dds::half_to_float(0x3555) == 0.333251953125F);
  CHECK(dds::half_to_float(0x7BFF) == 65504.0F);
  CHECK(std::isinf(dds::half_to_float(0x7C00)));
  CHECK(std::isnan(dds::half_to_float(0x7E00)));

  // The committed half-float ramp: row 0 walks 0..1 (SDR, identity through the knee), row 1
  // walks 1..12.5 (HDR, rolled off). The expected bytes are the JPEG XR tone map of the same
  // half-rounded floats.
  const auto bytes = patchy::test::read_binary_file(
      patchy::test::committed_format_fixture_path("dds", "synth-dx10-r16g16b16a16-float-ramp-8x2.dds"));
  const auto result = dds::read_dds(bytes);
  CHECK(result.document.width() == 8);
  CHECK(result.document.height() == 2);
  CHECK(channel_count(result.document) == 4);
  CHECK(has_notice(result.notices, "tone mapped"));
  std::vector<float> floats;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 8; ++x) {
      const double v = y == 0 ? x / 7.0 : 1.0 + x * (11.5 / 7.0);
      for (const double component : {v, v * 0.5, v * 0.25, 1.0}) {
        // Round through binary16 exactly as the generator did.
        const float as_float = static_cast<float>(component);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &as_float, 4);
        const std::uint32_t sign = bits >> 31U;
        const int exponent = static_cast<int>((bits >> 23U) & 0xffU) - 127;
        std::uint32_t mantissa = bits & 0x7fffffU;
        std::uint16_t half = 0;
        if (as_float == 0.0F) {
          half = 0;
        } else if (exponent < -14) {
          half = static_cast<std::uint16_t>(sign << 15U);  // the ramp has no subnormals
        } else {
          // Round to nearest even on the 13 dropped bits.
          const std::uint32_t dropped = mantissa & 0x1fffU;
          mantissa >>= 13U;
          std::uint32_t biased = static_cast<std::uint32_t>(exponent + 15);
          if (dropped > 0x1000U || (dropped == 0x1000U && (mantissa & 1U) != 0)) {
            ++mantissa;
            if (mantissa == 0x400U) {
              mantissa = 0;
              ++biased;
            }
          }
          half = static_cast<std::uint16_t>((sign << 15U) | (biased << 10U) | mantissa);
        }
        floats.push_back(dds::half_to_float(half));
      }
    }
  }
  const auto expected = patchy::jxr::tone_map_scrgb_to_rgba8(floats, 8, 2);
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 8; ++x) {
      const auto* want = expected.data() + (static_cast<std::size_t>(y) * 8U + static_cast<std::size_t>(x)) * 4U;
      check_rgba(pixel_at(result.document, x, y), {want[0], want[1], want[2], want[3]}, 0);
    }
  }
  // Below the curve's knee (0.5) the map is the plain sRGB transfer: 0.25 encodes to 137.
  CHECK(pixel_at(result.document, 7, 0)[2] == 137);
  CHECK(pixel_at(result.document, 7, 1)[0] == 255);  // the HDR ceiling (12.5) lands on white
  CHECK(pixel_at(result.document, 0, 0)[0] == 0);

  // BC6H (texconv, unsigned half floats of a gray ramp stored as linear values): Pillow's
  // decode of the same blocks, scaled to 0..1, through the tone map.
  const auto bc6h = dds::read_dds(patchy::test::read_binary_file(
      patchy::test::committed_format_fixture_path("dds", "texconv-bc6h-8x8.dds")));
  CHECK(bc6h.document.width() == 8);
  CHECK(channel_count(bc6h.document) == 3);
  CHECK(metadata_value(bc6h.document, dds::kMetadataSourceFormat) == "BC6H_UF16");
  CHECK(metadata_value(bc6h.document, dds::kMetadataCompression) == "bc7");
  CHECK(has_notice(bc6h.notices, "tone mapped"));
  struct GraySample {
    int x;
    int y;
    int pillow_gray;
  };
  for (const auto& sample : {GraySample{0, 0, 85}, GraySample{0, 7, 122}, GraySample{4, 4, 52}, GraySample{7, 0, 24},
                             GraySample{7, 7, 123}}) {
    const float linear = static_cast<float>(sample.pillow_gray) / 255.0F;
    const std::vector<float> one = {linear, linear, linear, 1.0F};
    const auto mapped = patchy::jxr::tone_map_scrgb_to_rgba8(one, 1, 1);
    // Pillow quantized the half floats to 8 bits before we saw them, so allow the slope of the
    // sRGB curve over half a code.
    check_rgba(pixel_at(bc6h.document, sample.x, sample.y), {mapped[0], mapped[1], mapped[2], 255}, 3);
  }
}

void dds_mips_cubemaps_volumes_and_arrays_import_as_layers() {
  const auto read_fixture = [](const char* name) {
    return dds::read_dds(patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("dds", name)));
  };

  const auto mipmapped = read_fixture("synth-dxt1-mipmapped-16x16.dds");
  CHECK(mipmapped.document.layers().size() == 1);
  CHECK(has_notice(mipmapped.notices, "Only the first of 5 mip levels"));
  CHECK(metadata_value(mipmapped.document, dds::kMetadataMipmaps) == "1");

  const auto cubemap = read_fixture("synth-cubemap-a8r8g8b8-4x4.dds");
  const auto& faces = cubemap.document.layers();
  CHECK(faces.size() == 6);
  const char* face_names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
  const std::array<std::array<int, 4>, 6> face_colors = {{{255, 0, 0, 255},
                                                          {0, 255, 0, 255},
                                                          {0, 0, 255, 255},
                                                          {255, 255, 0, 255},
                                                          {0, 255, 255, 255},
                                                          {255, 0, 255, 255}}};
  for (std::size_t face = 0; face < 6; ++face) {
    CHECK(faces[face].name() == face_names[face]);
    CHECK(faces[face].visible() == (face == 0));
    check_rgba(pixel_at(cubemap.document, 1, 2, face), face_colors[face], 0);
  }
  CHECK(has_notice(cubemap.notices, "cubemap face"));

  const auto volume = read_fixture("synth-volume-x8r8g8b8-4x4x3.dds");
  CHECK(volume.document.layers().size() == 3);
  CHECK(channel_count(volume.document) == 3);
  for (std::size_t slice = 0; slice < 3; ++slice) {
    CHECK(volume.document.layers()[slice].name() == "Slice " + std::to_string(slice + 1));
    CHECK(volume.document.layers()[slice].visible() == (slice == 0));
    const int step = static_cast<int>(slice) + 1;
    check_rgba(pixel_at(volume.document, 3, 3, slice), {30 * step, 20 * step, 10 * step, 255}, 0);
  }
  CHECK(has_notice(volume.notices, "volume slice"));

  const auto array = read_fixture("synth-dx10-array3-b8g8r8a8-4x4.dds");
  CHECK(array.document.layers().size() == 3);
  for (std::size_t element = 0; element < 3; ++element) {
    CHECK(array.document.layers()[element].name() == "Element " + std::to_string(element + 1));
    const int step = static_cast<int>(element) + 1;
    check_rgba(pixel_at(array.document, 0, 0, element), {60 * step, 50 * step, 40 * step, 255}, 0);
  }
  CHECK(has_notice(array.notices, "array element"));

  // A legacy cubemap with only +X and -Z present names the two faces it holds.
  HeaderSpec partial;
  partial.caps = dds::kCapsTexture | dds::kCapsComplex;
  partial.caps2 = dds::kCaps2Cubemap | dds::kCaps2CubemapPositiveX | (dds::kCaps2CubemapPositiveX << 5U);
  auto bytes = build_header(partial);
  for (int face = 0; face < 2; ++face) {
    for (int texel = 0; texel < 16; ++texel) {
      const std::uint8_t bgra[4] = {0, 0, static_cast<std::uint8_t>(face == 0 ? 255 : 0), 255};
      bytes.insert(bytes.end(), bgra, bgra + 4);
    }
  }
  const auto two_faces = dds::read_dds(bytes);
  CHECK(two_faces.document.layers().size() == 2);
  CHECK(two_faces.document.layers()[0].name() == "+X");
  CHECK(two_faces.document.layers()[1].name() == "-Z");
  check_rgba(pixel_at(two_faces.document, 0, 0, 0), {255, 0, 0, 255}, 0);
  check_rgba(pixel_at(two_faces.document, 0, 0, 1), {0, 0, 0, 255}, 0);
}

void dds_uncompressed_writer_layout_matches_spec() {
  const auto document = gradient_document(5, 3, /*translucent*/ true);
  WriteOptions options;
  options.compression = Compression::Uncompressed;
  const auto bytes = dds::write_dds(document, options);
  CHECK(bytes.size() == dds::kFileHeaderBytes + 5U * 3U * 4U);
  CHECK(u32_at(bytes, 0) == dds::kMagic);
  CHECK(u32_at(bytes, 4) == 124);
  CHECK(u32_at(bytes, 8) == (dds::kFlagCaps | dds::kFlagHeight | dds::kFlagWidth | dds::kFlagPixelFormat | dds::kFlagPitch));
  CHECK(u32_at(bytes, 12) == 3);   // height
  CHECK(u32_at(bytes, 16) == 5);   // width
  CHECK(u32_at(bytes, 20) == 20);  // pitch
  CHECK(u32_at(bytes, 24) == 0);   // depth
  CHECK(u32_at(bytes, 28) == 0);   // mip count (single level, flag absent)
  CHECK(u32_at(bytes, 76) == 32);
  CHECK(u32_at(bytes, 80) == (dds::kPixelFormatRgb | dds::kPixelFormatAlphaPixels));
  CHECK(u32_at(bytes, 84) == 0);
  CHECK(u32_at(bytes, 88) == 32);
  CHECK(u32_at(bytes, 92) == 0x00ff0000U);
  CHECK(u32_at(bytes, 96) == 0x0000ff00U);
  CHECK(u32_at(bytes, 100) == 0x000000ffU);
  CHECK(u32_at(bytes, 104) == 0xff000000U);
  CHECK(u32_at(bytes, 108) == dds::kCapsTexture);
  CHECK(u32_at(bytes, 112) == 0);
  // Pixel (0, 0) has alpha 0, and colours under zero alpha do not survive the flatten.
  CHECK(bytes[131] == 0);
  // Pixel (4, 0) is (153, 9, 252, alpha 255) stored as B, G, R, A.
  CHECK(bytes[128 + 4 * 4] == 252);
  CHECK(bytes[128 + 4 * 4 + 1] == 9);
  CHECK(bytes[128 + 4 * 4 + 2] == 153);
  CHECK(bytes[128 + 4 * 4 + 3] == 255);

  // With mipmaps: 5x3 -> 2x1 -> 1x1, the count and caps flags set, every level appended.
  options.generate_mipmaps = true;
  const auto with_mips = dds::write_dds(document, options);
  CHECK(dds::mip_count_for(5, 3) == 3);
  CHECK(u32_at(with_mips, 8) & dds::kFlagMipmapCount);
  CHECK(u32_at(with_mips, 28) == 3);
  CHECK(u32_at(with_mips, 108) == (dds::kCapsTexture | dds::kCapsComplex | dds::kCapsMipmap));
  CHECK(with_mips.size() == dds::kFileHeaderBytes + (15U + 2U + 1U) * 4U);

  // Block formats: linear size of level 0, the FourCC, no masks; partial blocks round up.
  options.generate_mipmaps = false;
  options.compression = Compression::Bc1;
  const auto bc1 = dds::write_dds(document, options);
  CHECK(bc1.size() == dds::kFileHeaderBytes + 2U * 8U);
  CHECK(u32_at(bc1, 8) & dds::kFlagLinearSize);
  CHECK(u32_at(bc1, 20) == 16);
  CHECK(u32_at(bc1, 80) == dds::kPixelFormatFourCc);
  CHECK(u32_at(bc1, 84) == dds::kFourCcDxt1);
  CHECK(u32_at(bc1, 88) == 0);
  options.compression = Compression::Bc3;
  options.generate_mipmaps = true;
  const auto bc3 = dds::write_dds(document, options);
  CHECK(u32_at(bc3, 84) == dds::kFourCcDxt5);
  CHECK(bc3.size() == dds::kFileHeaderBytes + (2U + 1U + 1U) * 16U);  // 5x3, 2x1, 1x1: one block each below 4 px

  // BC4 and BC5 take the legacy ATI1/ATI2 spellings; BC7 needs the DX10 header (DXGI 98,
  // 2D, one array element, alpha mode unknown) and its pixels start at 148.
  options.generate_mipmaps = false;
  options.compression = Compression::Bc4;
  const auto bc4 = dds::write_dds(document, options);
  CHECK(u32_at(bc4, 84) == dds::kFourCcAti1);
  CHECK(u32_at(bc4, 20) == 16);
  CHECK(bc4.size() == dds::kFileHeaderBytes + 2U * 8U);
  options.compression = Compression::Bc5;
  const auto bc5 = dds::write_dds(document, options);
  CHECK(u32_at(bc5, 84) == dds::kFourCcAti2);
  CHECK(bc5.size() == dds::kFileHeaderBytes + 2U * 16U);
  options.compression = Compression::Bc7;
  const auto bc7 = dds::write_dds(document, options);
  CHECK(u32_at(bc7, 84) == dds::kFourCcDx10);
  CHECK(u32_at(bc7, 20) == 32);
  CHECK(u32_at(bc7, 128) == dds::kDxgiBc7Unorm);
  CHECK(u32_at(bc7, 132) == dds::kResourceDimensionTexture2D);
  CHECK(u32_at(bc7, 136) == 0);
  CHECK(u32_at(bc7, 140) == 1);
  CHECK(u32_at(bc7, 144) == 0);
  CHECK(bc7.size() == dds::kFileHeaderBytes + dds::kDx10HeaderSize + 2U * 16U);
  const auto bc7_header = dds::parse_header(bc7);
  CHECK(bc7_header.data_offset == 148);
  CHECK(dds::resolve_source_format(bc7_header).name == "BC7_UNORM");
}

void dds_bc7_bc4_bc5_writers_round_trip() {
  // BC7 keeps full RGBA at high quality: a smooth translucent gradient comes back well above
  // the BC3 bound, alpha within a few levels, and the file reopens with the bc7 token.
  const auto translucent = smooth_document(16, 16, /*translucent*/ true);
  WriteOptions bc7;
  bc7.compression = Compression::Bc7;
  const auto decoded7 = dds::read_dds(dds::write_dds(translucent, bc7));
  CHECK(channel_count(decoded7.document) == 4);
  CHECK(metadata_value(decoded7.document, dds::kMetadataCompression) == "bc7");
  CHECK(metadata_value(decoded7.document, dds::kMetadataSourceFormat) == "BC7_UNORM");
  int alpha_max_error = 0;
  double squared_error = 0.0;
  int counted = 0;
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const auto expected = pixel_at(translucent, x, y);
      const auto actual = pixel_at(decoded7.document, x, y);
      // BC7 shares one index line between colour and alpha (mode 6) or gives alpha two index
      // bits (mode 5), so where colour and alpha vary in different directions inside a block,
      // as in this gradient, alpha lands within about 12: better than BC3's 16 to 18 on the
      // same content, not exact.
      alpha_max_error = std::max(alpha_max_error, std::abs(actual[3] - expected[3]));
      if (expected[3] > 0) {
        for (int channel = 0; channel < 3; ++channel) {
          const double delta = actual[channel] - expected[channel];
          squared_error += delta * delta;
          ++counted;
        }
      }
    }
  }
  const double psnr = 10.0 * std::log10(255.0 * 255.0 / std::max(squared_error / std::max(1, counted), 1e-9));
  std::cout << "  BC7 colour PSNR " << psnr << " dB, alpha max error " << alpha_max_error << "\n";
  CHECK(psnr >= 33.0);
  CHECK(alpha_max_error <= 16);

  // BC4 stores the luminance; the file reopens as gray within the 8-level block quantization.
  const auto opaque = smooth_document(16, 16, /*translucent*/ false);
  WriteOptions bc4;
  bc4.compression = Compression::Bc4;
  std::vector<std::string> notices;
  const auto bytes4 = dds::write_dds(opaque, bc4, &notices);
  CHECK(has_notice(notices, "BC4 keeps one channel"));
  const auto decoded4 = dds::read_dds(bytes4);
  CHECK(channel_count(decoded4.document) == 3);
  CHECK(metadata_value(decoded4.document, dds::kMetadataCompression) == "bc4");
  CHECK(metadata_value(decoded4.document, dds::kMetadataSourceFormat) == "ATI1");
  for (std::int32_t y = 0; y < 16; y += 3) {
    for (std::int32_t x = 0; x < 16; x += 5) {
      const auto source = pixel_at(opaque, x, y);
      const int gray = dds::luminance8(static_cast<std::uint8_t>(source[0]), static_cast<std::uint8_t>(source[1]),
                                       static_cast<std::uint8_t>(source[2]));
      const auto actual = pixel_at(decoded4.document, x, y);
      CHECK(actual[0] == actual[1] && actual[1] == actual[2]);
      CHECK(std::abs(actual[0] - gray) <= 3);
    }
  }
  CHECK(dds::luminance8(255, 255, 255) == 255);
  CHECK(dds::luminance8(0, 0, 0) == 0);
  CHECK(dds::luminance8(255, 0, 0) == 76);
  // A gray opaque source produces no BC4 notice.
  patchy::Document gray_document(4, 4, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer gray_pixels(4, 4, patchy::PixelFormat::rgba8());
  std::fill(gray_pixels.data().begin(), gray_pixels.data().end(), std::uint8_t{90});
  for (std::int32_t i = 0; i < 16; ++i) {
    gray_pixels.pixel(i % 4, i / 4)[3] = 255;
  }
  gray_document.add_pixel_layer("Background", std::move(gray_pixels));
  notices.clear();
  (void)dds::write_dds(gray_document, bc4, &notices);
  CHECK(notices.empty());

  // BC5 keeps red and green; blue reads back as 0 with the two-channel notice.
  WriteOptions bc5;
  bc5.compression = Compression::Bc5;
  notices.clear();
  const auto bytes5 = dds::write_dds(opaque, bc5, &notices);
  CHECK(has_notice(notices, "BC5 keeps the red and green channels only"));
  const auto decoded5 = dds::read_dds(bytes5);
  CHECK(channel_count(decoded5.document) == 3);
  CHECK(metadata_value(decoded5.document, dds::kMetadataCompression) == "bc5");
  CHECK(metadata_value(decoded5.document, dds::kMetadataSourceFormat) == "ATI2");
  CHECK(has_notice(decoded5.notices, "BC5"));
  for (std::int32_t y = 0; y < 16; y += 3) {
    for (std::int32_t x = 0; x < 16; x += 5) {
      const auto source = pixel_at(opaque, x, y);
      const auto actual = pixel_at(decoded5.document, x, y);
      CHECK(std::abs(actual[0] - source[0]) <= 3);
      CHECK(std::abs(actual[1] - source[1]) <= 3);
      CHECK(actual[2] == 0);
    }
  }
}

void dds_uncompressed_writer_round_trips_translucent_gradient_exactly() {
  const auto document = gradient_document(13, 9, /*translucent*/ true);
  WriteOptions options;
  options.compression = Compression::Uncompressed;
  const auto decoded = dds::read_dds(dds::write_dds(document, options));
  CHECK(decoded.notices.empty());
  CHECK(decoded.document.width() == 13);
  CHECK(decoded.document.height() == 9);
  CHECK(channel_count(decoded.document) == 4);
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 0; x < 13; ++x) {
      const auto expected = pixel_at(document, x, y);
      const auto actual = pixel_at(decoded.document, x, y);
      if (expected[3] == 0) {
        CHECK(actual[3] == 0);  // colours under zero alpha do not survive a flatten
      } else {
        check_rgba(actual, expected, 0);
      }
    }
  }
  CHECK(metadata_value(decoded.document, dds::kMetadataCompression) == "uncompressed");
  CHECK(metadata_value(decoded.document, dds::kMetadataSourceFormat) == "A8R8G8B8");

  // An opaque document writes the same header shape (A8R8G8B8 with alpha 255 everywhere).
  const auto opaque = dds::read_dds(dds::write_dds(gradient_document(6, 6, false), options));
  CHECK(channel_count(opaque.document) == 4);
  for (std::int32_t y = 0; y < 6; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      CHECK(pixel_at(opaque.document, x, y)[3] == 255);
    }
  }
}

void dds_bc1_cutout_rule_and_automatic_compression() {
  // Alpha columns 0, 100, 127, 128, 200, 255: below 128 cuts out, the rest stays opaque.
  patchy::Document document(6, 4, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(6, 4, patchy::PixelFormat::rgba8());
  const std::uint8_t alphas[6] = {0, 100, 127, 128, 200, 255};
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 200;
      pixel[1] = 40;
      pixel[2] = 60;
      pixel[3] = alphas[x];
    }
  }
  document.add_pixel_layer("Background", std::move(pixels));

  WriteOptions bc1;
  bc1.compression = Compression::Bc1;
  std::vector<std::string> notices;
  const auto bytes = dds::write_dds(document, bc1, &notices);
  CHECK(u32_at(bytes, 84) == dds::kFourCcDxt1);
  // 100, 127, 128 and 200 are partial in 4 rows each: 16 pixels cut or kept at the threshold.
  CHECK(has_notice(notices, "BC1 keeps only 1-bit transparency: 16 partially transparent pixels"));
  const auto decoded = dds::read_dds(bytes);
  for (std::int32_t y = 0; y < 4; ++y) {
    for (std::int32_t x = 0; x < 6; ++x) {
      const auto actual = pixel_at(decoded.document, x, y);
      if (alphas[x] < 128) {
        CHECK(actual[3] == 0);
      } else {
        CHECK(actual[3] == 255);
        check_rgba(actual, {200, 40, 60, 255}, 8);  // 565 quantization of one flat colour
      }
    }
  }

  // The block encoder directly: a fully transparent block is all index 3 with zero endpoints,
  // and an opaque block through stb_dxt never yields alpha 0.
  std::array<std::uint8_t, 64> block{};
  std::array<std::uint8_t, 8> encoded{};
  CHECK(dds::bc1_block_has_cutout(block.data()));
  dds::encode_bc1_block(block.data(), true, encoded.data());
  CHECK(u32_at(std::vector<std::uint8_t>(encoded.begin(), encoded.end()), 4) == 0xffffffffU);
  for (auto& byte : block) {
    byte = 255;
  }
  CHECK(!dds::bc1_block_has_cutout(block.data()));

  // Automatic: an opaque image writes DXT1, a translucent one DXT5, both reopening with the
  // matching metadata so a plain Save keeps the choice.
  WriteOptions automatic;
  const auto opaque = dds::write_dds(gradient_document(8, 8, false), automatic);
  CHECK(u32_at(opaque, 84) == dds::kFourCcDxt1);
  CHECK(metadata_value(dds::read_dds(opaque).document, dds::kMetadataCompression) == "bc1");
  const auto translucent = dds::write_dds(gradient_document(8, 8, true), automatic);
  CHECK(u32_at(translucent, 84) == dds::kFourCcDxt5);
  CHECK(metadata_value(dds::read_dds(translucent).document, dds::kMetadataCompression) == "bc3");
  // A single translucent pixel is enough to pick BC3.
  patchy::Document nearly_opaque(4, 4, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer nearly(4, 4, patchy::PixelFormat::rgba8());
  std::fill(nearly.data().begin(), nearly.data().end(), std::uint8_t{255});
  nearly.pixel(3, 3)[3] = 254;
  nearly_opaque.add_pixel_layer("Background", std::move(nearly));
  CHECK(u32_at(dds::write_dds(nearly_opaque, automatic), 84) == dds::kFourCcDxt5);
  // The registry default is Automatic too.
  CHECK(u32_at(dds::write_dds(gradient_document(4, 4, false)), 84) == dds::kFourCcDxt1);
}

void dds_bc3_writer_keeps_alpha_within_tolerance() {
  // A smooth alpha ramp (17 per column, no transparent row): BC3 interpolates 8 alpha levels
  // across each block's range, so a block spanning 51 levels lands within 4. A block that
  // mixes alpha 0 with alpha 200 would spread its 8 levels over the whole range and be off
  // by up to 18, which is BC3's nature, not a writer bug; verify_dds.py allows that case.
  patchy::Document document(16, 16, patchy::PixelFormat::rgba8());
  {
    patchy::PixelBuffer pixels(16, 16, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 16; ++y) {
      for (std::int32_t x = 0; x < 16; ++x) {
        auto* pixel = pixels.pixel(x, y);
        pixel[0] = static_cast<std::uint8_t>(20 + x * 8);
        pixel[1] = static_cast<std::uint8_t>(30 + y * 12);
        pixel[2] = static_cast<std::uint8_t>(200 - (x + y) * 4);
        pixel[3] = static_cast<std::uint8_t>(x * 17);
      }
    }
    document.add_pixel_layer("Background", std::move(pixels));
  }
  WriteOptions bc3;
  bc3.compression = Compression::Bc3;
  const auto decoded = dds::read_dds(dds::write_dds(document, bc3));
  CHECK(channel_count(decoded.document) == 4);
  int alpha_max_error = 0;
  double squared_error = 0.0;
  int counted = 0;
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const auto expected = pixel_at(document, x, y);
      const auto actual = pixel_at(decoded.document, x, y);
      alpha_max_error = std::max(alpha_max_error, std::abs(actual[3] - expected[3]));
      if (expected[3] == 0 || expected[3] == 255) {
        CHECK(actual[3] == expected[3]);  // the ramp's endpoints are exact
      }
      if (expected[3] > 0) {
        for (int channel = 0; channel < 3; ++channel) {
          const double delta = actual[channel] - expected[channel];
          squared_error += delta * delta;
          ++counted;
        }
      }
    }
  }
  CHECK(alpha_max_error <= 8);
  const double mse = squared_error / std::max(1, counted);
  const double psnr = 10.0 * std::log10(255.0 * 255.0 / std::max(mse, 1e-9));
  std::cout << "  BC3 colour PSNR " << psnr << " dB, alpha max error " << alpha_max_error << "\n";
  CHECK(psnr >= 30.0);
}

void dds_mipmap_chain_box_filter_rules() {
  CHECK(dds::mip_count_for(1, 1) == 1);
  CHECK(dds::mip_count_for(2, 1) == 2);
  CHECK(dds::mip_count_for(5, 3) == 3);
  CHECK(dds::mip_count_for(16, 16) == 5);
  CHECK(dds::mip_count_for(17, 1) == 5);
  CHECK(dds::mip_count_for(16384, 1) == 15);

  // 5x3: the last destination column absorbs columns 2..4, the single row absorbs all three.
  patchy::PixelBuffer source(5, 3, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 3; ++y) {
    for (std::int32_t x = 0; x < 5; ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(x * 10);
      pixel[1] = static_cast<std::uint8_t>(y * 10);
      pixel[2] = 7;
      pixel[3] = 255;
    }
  }
  const auto chain = dds::generate_mip_chain(source);
  CHECK(chain.size() == 3);
  CHECK((chain[1].width() == 2 && chain[1].height() == 1));
  CHECK((chain[2].width() == 1 && chain[2].height() == 1));
  // Destination (0,0): columns 0,1 -> red (0+10)/2 = 5; rows 0..2 -> green (0+10+20)/3 = 10.
  CHECK(chain[1].pixel(0, 0)[0] == 5);
  CHECK(chain[1].pixel(0, 0)[1] == 10);
  // Destination (1,0): columns 2,3,4 -> red (20+30+40)/3 = 30.
  CHECK(chain[1].pixel(1, 0)[0] == 30);
  CHECK(chain[1].pixel(1, 0)[2] == 7);
  CHECK(chain[1].pixel(1, 0)[3] == 255);

  // Alpha weighting: a red opaque texel next to three transparent blue ones stays red.
  patchy::PixelBuffer weighted(2, 2, patchy::PixelFormat::rgba8());
  const std::uint8_t red[4] = {255, 0, 0, 255};
  const std::uint8_t blue[4] = {0, 0, 255, 0};
  std::memcpy(weighted.pixel(0, 0), red, 4);
  std::memcpy(weighted.pixel(1, 0), blue, 4);
  std::memcpy(weighted.pixel(0, 1), blue, 4);
  std::memcpy(weighted.pixel(1, 1), blue, 4);
  const auto shrunk = dds::box_downsample(weighted);
  CHECK((shrunk.width() == 1 && shrunk.height() == 1));
  CHECK(shrunk.pixel(0, 0)[0] == 255);
  CHECK(shrunk.pixel(0, 0)[2] == 0);
  CHECK(shrunk.pixel(0, 0)[3] == 64);  // (255 + 2) / 4

  // Every texel transparent: the plain mean keeps a sensible colour under zero alpha.
  for (std::int32_t y = 0; y < 2; ++y) {
    for (std::int32_t x = 0; x < 2; ++x) {
      weighted.pixel(x, y)[3] = 0;
    }
  }
  const auto transparent = dds::box_downsample(weighted);
  CHECK(transparent.pixel(0, 0)[0] == 64);   // (255 + 2) / 4
  CHECK(transparent.pixel(0, 0)[2] == 191);  // (765 + 2) / 4
  CHECK(transparent.pixel(0, 0)[3] == 0);

  // A 1xN strip only shrinks along its long side.
  patchy::PixelBuffer strip(1, 4, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 4; ++y) {
    auto* pixel = strip.pixel(0, y);
    pixel[0] = pixel[1] = pixel[2] = static_cast<std::uint8_t>(y * 20);
    pixel[3] = 255;
  }
  const auto half = dds::box_downsample(strip);
  CHECK((half.width() == 1 && half.height() == 2));
  CHECK(half.pixel(0, 0)[0] == 10);
  CHECK(half.pixel(0, 1)[0] == 50);
}

void dds_automatic_choices_resolve_against_the_opened_file() {
  using dds::MipmapChoice;
  // The persisted mipmap tokens, a compatibility contract like the compression tokens.
  CHECK(dds::mipmap_choice_token(MipmapChoice::Automatic) == "auto");
  CHECK(dds::mipmap_choice_token(MipmapChoice::Generate) == "on");
  CHECK(dds::mipmap_choice_token(MipmapChoice::None) == "off");
  for (const auto choice : {MipmapChoice::Automatic, MipmapChoice::Generate, MipmapChoice::None}) {
    CHECK(dds::mipmap_choice_from_token(dds::mipmap_choice_token(choice)) == choice);
  }
  CHECK(!dds::mipmap_choice_from_token("bogus").has_value());

  // A document that never came from a .dds has no source shape: Automatic stays Automatic
  // for the writer's alpha rule and writes a mip chain.
  const dds::SourceShape none;
  CHECK(!none.compression.has_value());
  CHECK(!none.mipmaps.has_value());
  CHECK(dds::resolve_compression(Compression::Automatic, none) == Compression::Automatic);
  CHECK(dds::resolve_compression(Compression::Bc7, none) == Compression::Bc7);
  CHECK(dds::resolve_mipmaps(MipmapChoice::Automatic, none));
  CHECK(dds::resolve_mipmaps(MipmapChoice::Generate, none));
  CHECK(!dds::resolve_mipmaps(MipmapChoice::None, none));

  // The reader's metadata describes the opened file: a BC3 texture without a chain and a
  // mipmapped BC1 one. Automatic follows it; explicit choices pass through untouched.
  const auto bc3 = dds::read_dds(
      patchy::test::read_binary_file(patchy::test::committed_format_fixture_path("dds", "pillow-dxt5-16x16.dds")));
  const auto bc3_source = dds::source_shape_from_metadata(bc3.document.metadata().values);
  CHECK(bc3_source.compression == Compression::Bc3);
  CHECK(bc3_source.mipmaps == false);
  CHECK(dds::resolve_compression(Compression::Automatic, bc3_source) == Compression::Bc3);
  CHECK(dds::resolve_compression(Compression::Bc1, bc3_source) == Compression::Bc1);
  CHECK(!dds::resolve_mipmaps(MipmapChoice::Automatic, bc3_source));
  CHECK(dds::resolve_mipmaps(MipmapChoice::Generate, bc3_source));

  const auto bc1 = dds::read_dds(patchy::test::read_binary_file(
      patchy::test::committed_format_fixture_path("dds", "synth-dxt1-mipmapped-16x16.dds")));
  const auto bc1_source = dds::source_shape_from_metadata(bc1.document.metadata().values);
  CHECK(bc1_source.compression == Compression::Bc1);
  CHECK(bc1_source.mipmaps == true);
  CHECK(dds::resolve_compression(Compression::Automatic, bc1_source) == Compression::Bc1);
  CHECK(dds::resolve_mipmaps(MipmapChoice::Automatic, bc1_source));
  CHECK(!dds::resolve_mipmaps(MipmapChoice::None, bc1_source));

  // Metadata written by hand: an "auto" or unknown compression token is no source format.
  std::map<std::string, std::string> odd;
  odd[dds::kMetadataCompression] = "auto";
  odd[dds::kMetadataMipmaps] = "1";
  const auto odd_source = dds::source_shape_from_metadata(odd);
  CHECK(!odd_source.compression.has_value());
  CHECK(odd_source.mipmaps == true);
}

void dds_preview_levels_match_the_written_file() {
  // The preview is the save: every level is encoded by the writer's block encoders and
  // decoded by the reader's decoder, so level 0 equals a real round trip texel for texel
  // and the byte sizes add up to the file's payload.
  const auto document = gradient_document(13, 9, /*translucent*/ true);
  const auto preview = dds::preview_levels(document, Compression::Bc3, /*mipmaps*/ true);
  CHECK(preview.compression == Compression::Bc3);
  CHECK(preview.levels.size() == dds::mip_count_for(13, 9));
  CHECK((preview.levels.front().rgba8.width() == 13 && preview.levels.front().rgba8.height() == 9));
  CHECK((preview.levels.back().rgba8.width() == 1 && preview.levels.back().rgba8.height() == 1));
  WriteOptions options;
  options.compression = Compression::Bc3;
  options.generate_mipmaps = true;
  const auto bytes = dds::write_dds(document, options);
  std::uint64_t payload = 0;
  for (const auto& level : preview.levels) {
    payload += level.byte_size;
  }
  CHECK(payload == bytes.size() - dds::kFileHeaderBytes);
  CHECK(preview.levels[0].byte_size == 4U * 3U * 16U);  // 13x9 is 4x3 blocks of 16 bytes
  const auto round_trip = dds::read_dds(bytes);
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 0; x < 13; ++x) {
      const auto expected = pixel_at(round_trip.document, x, y);
      const auto* actual = preview.levels[0].rgba8.pixel(x, y);
      for (int channel = 0; channel < 4; ++channel) {
        CHECK(actual[channel] == expected[static_cast<std::size_t>(channel)]);
      }
    }
  }

  // Without a chain only level 0 is produced; Uncompressed is the identity; Automatic
  // resolves by the alpha rule (BC1 for an opaque image) and reports what it picked.
  const auto single = dds::preview_levels(document, Compression::Uncompressed, /*mipmaps*/ false);
  CHECK(single.levels.size() == 1);
  CHECK(single.compression == Compression::Uncompressed);
  CHECK(single.levels[0].byte_size == 13U * 9U * 4U);
  const auto flat = std::as_const(document.layers().front()).pixels();
  for (std::int32_t y = 0; y < 9; ++y) {
    for (std::int32_t x = 0; x < 13; ++x) {
      const auto* expected = flat.pixel(x, y);
      const auto* actual = single.levels[0].rgba8.pixel(x, y);
      CHECK(actual[3] == expected[3]);
      if (expected[3] != 0) {  // colours under zero alpha do not survive a flatten
        CHECK((actual[0] == expected[0] && actual[1] == expected[1] && actual[2] == expected[2]));
      }
    }
  }
  const auto opaque = dds::preview_levels(gradient_document(8, 8, false), Compression::Automatic, true);
  CHECK(opaque.compression == Compression::Bc1);
  CHECK(opaque.levels.size() == 4);
  CHECK(opaque.levels[0].byte_size == 4U * 8U);
  const auto translucent = dds::preview_levels(gradient_document(8, 8, true), Compression::Automatic, false);
  CHECK(translucent.compression == Compression::Bc3);
  // BC4 previews as gray and BC5 as red and green with blue 0, as a reader sees them.
  const auto gray = dds::preview_levels(gradient_document(8, 8, false), Compression::Bc4, false);
  const auto* gray_pixel = gray.levels[0].rgba8.pixel(3, 3);
  CHECK((gray_pixel[0] == gray_pixel[1] && gray_pixel[1] == gray_pixel[2] && gray_pixel[3] == 255));
  const auto red_green = dds::preview_levels(gradient_document(8, 8, false), Compression::Bc5, false);
  CHECK(red_green.levels[0].rgba8.pixel(3, 3)[2] == 0);
}

void dds_writer_bytes_are_stable() {
  // Cross-platform byte-identical rule: the exact writer output (header, stb_dxt's BC1 and
  // BC3 blocks, Patchy's cut-out blocks, the box-filtered mip chain) is pinned by an FNV-1a
  // hash. A change here means the DDS bytes changed; re-pin only for a deliberate encoder
  // change (the failure output prints the new hash). The remotes run this too, which is what
  // checks stb_dxt's float search against other toolchains.
  struct Pin {
    const char* label;
    std::uint64_t expected;
    Compression compression;
    bool mipmaps;
    bool translucent;
  };
  const std::vector<Pin> pins = {
      {"uncompressed+mips", 0xb04b884dd2fa5fdbULL, Compression::Uncompressed, true, true},
      {"bc1 opaque", 0x6586865beef202d0ULL, Compression::Bc1, false, false},
      {"bc1 cut-out", 0x88c397c251894c36ULL, Compression::Bc1, false, true},
      {"bc3+mips", 0x070038e35b2c463bULL, Compression::Bc3, true, true},
      {"bc7", 0x62400dd6c26584f1ULL, Compression::Bc7, false, true},
      {"bc4", 0x91b0a0ee19c5542eULL, Compression::Bc4, false, false},
      {"bc5+mips", 0x5669a0d962f6580dULL, Compression::Bc5, true, false},
  };
  std::vector<std::uint64_t> hashes;
  for (const auto& pin : pins) {
    WriteOptions options;
    options.compression = pin.compression;
    options.generate_mipmaps = pin.mipmaps;
    const auto bytes = dds::write_dds(gradient_document(23, 17, pin.translucent), options);
    hashes.push_back(patchy::test::fnv1a_hash_bytes(bytes));
    if (hashes.back() != pin.expected) {
      std::cout << "  dds writer hash (" << pin.label << "): 0x" << std::hex << hashes.back() << std::dec
                << " size " << bytes.size() << "\n";
    }
  }
  for (std::size_t i = 0; i < pins.size(); ++i) {
    CHECK(hashes[i] == pins[i].expected);
  }
}

void dds_unicode_path_round_trips() {
  // Every new file-writing entry point gets a Unicode-path test (AGENTS.md).
  const auto dir = patchy::test::unicode_artifact_dir(u8"dds");
  const auto path = dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem).concat(".dds");
  const auto document = gradient_document(5, 4, /*translucent*/ true);
  WriteOptions options;
  options.compression = Compression::Uncompressed;
  dds::write_dds_file(document, path, options);
  CHECK(std::filesystem::exists(path));
  CHECK(patchy::test::directory_holds_only(dir, {path}));

  const auto decoded = dds::read_dds_file(path);
  CHECK(decoded.document.width() == 5);
  CHECK(decoded.document.height() == 4);
  check_rgba(pixel_at(decoded.document, 3, 2), pixel_at(document, 3, 2), 0);
  // The file form names the layer after the stem, the flat-format convention.
  CHECK(decoded.document.layers().front().name() == patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem));
}

void dds_writes_inspection_artifacts() {
  // One texture per compression under test-artifacts/dds/ plus the source as a BMP, the input
  // for scripts/dev/dds/verify_dds.py (Pillow decodes the .dds files independently and
  // compares them with the .bmp references; see docs/dds.md).
  const auto dir = std::filesystem::path("test-artifacts") / "dds";
  std::filesystem::create_directories(dir);
  const auto translucent = smooth_document(30, 20, /*translucent*/ true);
  const auto opaque = smooth_document(30, 20, /*translucent*/ false);
  patchy::bmp::DocumentIo::write_file(translucent, dir / "source-translucent.bmp");
  patchy::bmp::DocumentIo::write_file(opaque, dir / "source-opaque.bmp");
  struct Variant {
    const char* name;
    const patchy::Document* document;
    Compression compression;
    bool mipmaps;
  };
  const std::vector<Variant> variants = {
      {"uncompressed-translucent.dds", &translucent, Compression::Uncompressed, false},
      {"uncompressed-opaque-mips.dds", &opaque, Compression::Uncompressed, true},
      {"bc1-opaque.dds", &opaque, Compression::Bc1, false},
      {"bc1-cutout.dds", &translucent, Compression::Bc1, false},
      {"bc3-translucent.dds", &translucent, Compression::Bc3, false},
      {"bc3-translucent-mips.dds", &translucent, Compression::Bc3, true},
      {"bc7-translucent.dds", &translucent, Compression::Bc7, false},
      {"bc4-opaque.dds", &opaque, Compression::Bc4, false},
      {"bc5-opaque.dds", &opaque, Compression::Bc5, false},
  };
  for (const auto& variant : variants) {
    WriteOptions options;
    options.compression = variant.compression;
    options.generate_mipmaps = variant.mipmaps;
    const auto path = dir / variant.name;
    dds::write_dds_file(*variant.document, path, options);
    const auto decoded = dds::read_dds_file(path);
    CHECK(decoded.document.width() == 30);
    CHECK(decoded.document.height() == 20);
  }
}

void dds_local_fixture_sweep_if_available() {
  const auto dir = patchy::test::local_format_fixture_path("dds", "").parent_path();
  if (!std::filesystem::exists(dir)) {
    std::cout << "[SKIP] dds_local_fixture_sweep_if_available: no local-test-fixtures/dds\n";
    return;
  }
  int decoded_count = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file() || !dds::is_dds_extension(entry.path().extension().string())) {
      continue;
    }
    const auto bytes = patchy::test::read_binary_file(entry.path());
    try {
      const auto result = dds::read_dds(bytes);
      CHECK(result.document.width() > 0);
      CHECK(result.document.height() > 0);
      CHECK(!result.document.layers().empty());
      std::cout << "  " << entry.path().filename().string() << ": " << result.document.width() << 'x'
                << result.document.height() << ' ' << metadata_value(result.document, dds::kMetadataSourceFormat)
                << " layers " << result.document.layers().size() << " channels " << channel_count(result.document);
      for (const auto& notice : result.notices) {
        std::cout << " | " << notice;
      }
      std::cout << '\n';
    } catch (const std::exception& error) {
      // Only the documented refusals (unsupported pixel formats) may fail here.
      std::cout << "  " << entry.path().filename().string() << ": " << error.what() << '\n';
      CHECK(std::string(error.what()).find("not supported") != std::string::npos ||
            std::string(error.what()).find("Unsupported") != std::string::npos);
    }
    ++decoded_count;
  }
  if (decoded_count == 0) {
    std::cout << "[SKIP] dds_local_fixture_sweep_if_available: no .dds files in local-test-fixtures/dds\n";
  }
}

}  // namespace

std::vector<patchy::test::TestCase> dds_tests() {
  return {
      {"dds_extensions_sniff_and_registry_routing", dds_extensions_sniff_and_registry_routing},
      {"dds_header_parse_validates_legacy_and_dx10_shapes", dds_header_parse_validates_legacy_and_dx10_shapes},
      {"dds_rejects_bad_magic_sizes_unknown_formats_and_truncation",
       dds_rejects_bad_magic_sizes_unknown_formats_and_truncation},
      {"dds_bc1_decodes_four_color_and_punch_through_blocks", dds_bc1_decodes_four_color_and_punch_through_blocks},
      {"dds_bc2_bc3_bc4_bc5_decode_hand_built_blocks", dds_bc2_bc3_bc4_bc5_decode_hand_built_blocks},
      {"dds_premultiplied_sources_unpremultiply_with_notice", dds_premultiplied_sources_unpremultiply_with_notice},
      {"dds_reads_committed_fixtures", dds_reads_committed_fixtures},
      {"dds_float_sources_tone_map_like_jpeg_xr", dds_float_sources_tone_map_like_jpeg_xr},
      {"dds_mips_cubemaps_volumes_and_arrays_import_as_layers", dds_mips_cubemaps_volumes_and_arrays_import_as_layers},
      {"dds_uncompressed_writer_layout_matches_spec", dds_uncompressed_writer_layout_matches_spec},
      {"dds_bc7_bc4_bc5_writers_round_trip", dds_bc7_bc4_bc5_writers_round_trip},
      {"dds_uncompressed_writer_round_trips_translucent_gradient_exactly",
       dds_uncompressed_writer_round_trips_translucent_gradient_exactly},
      {"dds_bc1_cutout_rule_and_automatic_compression", dds_bc1_cutout_rule_and_automatic_compression},
      {"dds_bc3_writer_keeps_alpha_within_tolerance", dds_bc3_writer_keeps_alpha_within_tolerance},
      {"dds_mipmap_chain_box_filter_rules", dds_mipmap_chain_box_filter_rules},
      {"dds_automatic_choices_resolve_against_the_opened_file", dds_automatic_choices_resolve_against_the_opened_file},
      {"dds_preview_levels_match_the_written_file", dds_preview_levels_match_the_written_file},
      {"dds_writer_bytes_are_stable", dds_writer_bytes_are_stable},
      {"dds_unicode_path_round_trips", dds_unicode_path_round_trips},
      {"dds_writes_inspection_artifacts", dds_writes_inspection_artifacts},
      {"dds_local_fixture_sweep_if_available", dds_local_fixture_sweep_if_available},
  };
}
