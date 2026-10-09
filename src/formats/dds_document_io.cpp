#include "formats/dds_document_io.hpp"

#include "core/worker_budget.hpp"
#include "formats/binary_le.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "formats/jxr_document_io.hpp"
#include "support/string_utils.hpp"
#include "support/translate_noop.hpp"

// The precise entry points carry the signed/unsigned flag; bcdec_impl.c defines the same
// macro so the declarations match the compiled bodies.
#define BCDEC_BC4BC5_PRECISE
#include "formats/bc7enc/bc7enc.h"
#include "formats/bcdec/bcdec.h"
#include "formats/stb/stb_dxt.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>

namespace patchy::dds {

namespace {

constexpr std::uint32_t kLegacyFourCcA16B16G16R16 = 36;      // D3DFMT_A16B16G16R16
constexpr std::uint32_t kLegacyFourCcA16B16G16R16F = 113;    // D3DFMT_A16B16G16R16F
constexpr std::uint32_t kLegacyFourCcA32B32G32R32F = 116;    // D3DFMT_A32B32G32R32F
constexpr std::uint32_t kMaxMipLevels = 15;                  // 1 + log2(kMaxSide)
constexpr std::uint8_t kCutoutThreshold = 128;               // BC1: alpha below this is cut out

[[nodiscard]] std::string fourcc_text(std::uint32_t code) {
  std::string text;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const auto byte = static_cast<char>((code >> shift) & 0xffU);
    text.push_back(byte >= 32 && byte < 127 ? byte : '?');
  }
  return text;
}

[[nodiscard]] bool is_block_kind(SourceFormat::Kind kind) noexcept {
  switch (kind) {
    case SourceFormat::Kind::Bc1:
    case SourceFormat::Kind::Bc2:
    case SourceFormat::Kind::Bc3:
    case SourceFormat::Kind::Bc4:
    case SourceFormat::Kind::Bc5:
    case SourceFormat::Kind::Bc6h:
    case SourceFormat::Kind::Bc7:
      return true;
    case SourceFormat::Kind::Masked:
    case SourceFormat::Kind::Rgba16:
    case SourceFormat::Kind::Float:
      break;
  }
  return false;
}

[[nodiscard]] std::uint32_t block_bytes(SourceFormat::Kind kind) noexcept {
  switch (kind) {
    case SourceFormat::Kind::Bc1:
    case SourceFormat::Kind::Bc4:
      return 8;
    case SourceFormat::Kind::Bc2:
    case SourceFormat::Kind::Bc3:
    case SourceFormat::Kind::Bc5:
    case SourceFormat::Kind::Bc6h:
    case SourceFormat::Kind::Bc7:
      return 16;
    case SourceFormat::Kind::Masked:
    case SourceFormat::Kind::Rgba16:
    case SourceFormat::Kind::Float:
      break;
  }
  return 0;
}

struct ChannelMask {
  unsigned shift{0};
  unsigned bits{0};
};

[[nodiscard]] ChannelMask analyze_mask(std::uint32_t mask) noexcept {
  ChannelMask result;
  if (mask == 0) {
    return result;
  }
  while ((mask & 1U) == 0) {
    mask >>= 1U;
    ++result.shift;
  }
  while (mask != 0) {
    result.bits += mask & 1U;
    mask >>= 1U;
  }
  return result;
}

// Replicates an n-bit code into 8 bits (n < 8), the GPU's own expansion: 0 and the top
// code land on exactly 0 and 255, and 4-bit codes become code * 17.
[[nodiscard]] std::uint8_t replicate_bits(std::uint32_t value, unsigned bits) noexcept {
  std::uint32_t out = 0;
  unsigned filled = 0;
  while (filled < 8) {
    out = (out << bits) | value;
    filled += bits;
  }
  return static_cast<std::uint8_t>(out >> (filled - 8));
}

[[nodiscard]] std::uint8_t channel_to_8bit(std::uint32_t raw, const ChannelMask& mask) noexcept {
  if (mask.bits == 0) {
    return 0;
  }
  const std::uint32_t value = (raw >> mask.shift) & ((mask.bits >= 32) ? 0xffffffffU : ((1U << mask.bits) - 1U));
  if (mask.bits == 8) {
    return static_cast<std::uint8_t>(value);
  }
  if (mask.bits < 8) {
    return replicate_bits(value, mask.bits);
  }
  const std::uint64_t top = (1ULL << mask.bits) - 1ULL;
  return static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) * 255ULL + top / 2ULL) / top);
}

[[nodiscard]] std::uint8_t unorm16_to_8bit(std::uint32_t value) noexcept {
  return static_cast<std::uint8_t>((value * 255U + 32767U) / 65535U);
}

// -127..127 (bcdec clamps -128) to 0..255, rounded.
[[nodiscard]] std::uint8_t snorm8_to_unorm8(int value) noexcept {
  return static_cast<std::uint8_t>(((value + 127) * 255 + 127) / 254);
}

[[nodiscard]] std::string masked_format_name(const PixelFormatDescriptor& pf, bool luminance, bool alpha_only) {
  struct Part {
    char letter;
    ChannelMask mask;
  };
  std::vector<Part> parts;
  const auto add = [&](char letter, std::uint32_t mask) {
    if (mask != 0) {
      parts.push_back({letter, analyze_mask(mask)});
    }
  };
  add('A', pf.a_mask);
  if (luminance) {
    add('L', pf.r_mask);
  } else if (!alpha_only) {
    add('R', pf.r_mask);
    add('G', pf.g_mask);
    add('B', pf.b_mask);
  }
  std::stable_sort(parts.begin(), parts.end(),
                   [](const Part& a, const Part& b) { return a.mask.shift > b.mask.shift; });
  unsigned total = 0;
  std::string name;
  for (const auto& part : parts) {
    name.push_back(part.letter);
    name += std::to_string(part.mask.bits);
    total += part.mask.bits;
  }
  if (total < pf.rgb_bit_count) {
    name = "X" + std::to_string(pf.rgb_bit_count - total) + name;
  }
  return name;
}

[[nodiscard]] PixelFormatDescriptor masks_of(std::uint32_t bits, std::uint32_t r, std::uint32_t g, std::uint32_t b,
                                             std::uint32_t a) noexcept {
  PixelFormatDescriptor pf;
  pf.rgb_bit_count = bits;
  pf.r_mask = r;
  pf.g_mask = g;
  pf.b_mask = b;
  pf.a_mask = a;
  pf.flags = kPixelFormatRgb | (a != 0 ? kPixelFormatAlphaPixels : 0U);
  return pf;
}

[[nodiscard]] SourceFormat masked_source(const char* name, std::uint32_t bits, std::uint32_t r, std::uint32_t g,
                                         std::uint32_t b, std::uint32_t a) {
  SourceFormat format;
  format.kind = SourceFormat::Kind::Masked;
  format.name = name;
  format.bits_per_pixel = bits;
  format.masks = masks_of(bits, r, g, b, a);
  format.has_alpha = a != 0;
  return format;
}

[[nodiscard]] SourceFormat block_source(SourceFormat::Kind kind, const char* name, bool has_alpha, bool is_signed) {
  SourceFormat format;
  format.kind = kind;
  format.name = name;
  format.has_alpha = has_alpha;
  format.is_signed = is_signed;
  return format;
}

[[nodiscard]] SourceFormat float_source(const char* name, std::uint32_t channels, std::uint32_t bits) {
  SourceFormat format;
  format.kind = SourceFormat::Kind::Float;
  format.name = name;
  format.float_channels = channels;
  format.float_bits = bits;
  format.bits_per_pixel = channels * bits;
  format.has_alpha = channels == 4;
  return format;
}

// TYPELESS block and 8-bit formats carry no interpretation of their own; every reader (and
// Pillow's writer, which emits BC*_TYPELESS) treats them as UNORM, so map them before the
// table lookup.
[[nodiscard]] std::uint32_t untyped_dxgi(std::uint32_t dxgi) noexcept {
  switch (dxgi) {
    case 27:  // R8G8B8A8_TYPELESS
      return kDxgiR8G8B8A8Unorm;
    case 70:  // BC1_TYPELESS
      return kDxgiBc1Unorm;
    case 73:  // BC2_TYPELESS
      return kDxgiBc2Unorm;
    case 76:  // BC3_TYPELESS
      return kDxgiBc3Unorm;
    case 79:  // BC4_TYPELESS
      return kDxgiBc4Unorm;
    case 82:  // BC5_TYPELESS
      return kDxgiBc5Unorm;
    case 90:  // B8G8R8A8_TYPELESS
      return kDxgiB8G8R8A8Unorm;
    case 92:  // B8G8R8X8_TYPELESS
      return kDxgiB8G8R8X8Unorm;
    case 94:  // BC6H_TYPELESS
      return kDxgiBc6hUf16;
    case 97:  // BC7_TYPELESS
      return kDxgiBc7Unorm;
    default:
      return dxgi;
  }
}

[[nodiscard]] SourceFormat resolve_dxgi(std::uint32_t dxgi) {
  switch (untyped_dxgi(dxgi)) {
    case kDxgiR32G32B32A32Float:
      return float_source("R32G32B32A32_FLOAT", 4, 32);
    case kDxgiR32G32B32Float:
      return float_source("R32G32B32_FLOAT", 3, 32);
    case kDxgiR16G16B16A16Float:
      return float_source("R16G16B16A16_FLOAT", 4, 16);
    case kDxgiR16G16B16A16Unorm: {
      SourceFormat format;
      format.kind = SourceFormat::Kind::Rgba16;
      format.name = "R16G16B16A16_UNORM";
      format.bits_per_pixel = 64;
      format.has_alpha = true;
      return format;
    }
    case kDxgiR10G10B10A2Unorm:
      return masked_source("R10G10B10A2_UNORM", 32, 0x000003ffU, 0x000ffc00U, 0x3ff00000U, 0xc0000000U);
    case kDxgiR8G8B8A8Unorm:
      return masked_source("R8G8B8A8_UNORM", 32, 0x000000ffU, 0x0000ff00U, 0x00ff0000U, 0xff000000U);
    case kDxgiR8G8B8A8UnormSrgb:
      return masked_source("R8G8B8A8_UNORM_SRGB", 32, 0x000000ffU, 0x0000ff00U, 0x00ff0000U, 0xff000000U);
    case kDxgiR8G8Unorm:
      return masked_source("R8G8_UNORM", 16, 0x00ffU, 0xff00U, 0, 0);
    case kDxgiR8Unorm: {
      auto format = masked_source("R8_UNORM", 8, 0xffU, 0, 0, 0);
      format.luminance = true;
      return format;
    }
    case kDxgiA8Unorm: {
      auto format = masked_source("A8_UNORM", 8, 0, 0, 0, 0xffU);
      format.alpha_only = true;
      return format;
    }
    case kDxgiBc1Unorm:
      return block_source(SourceFormat::Kind::Bc1, "BC1_UNORM", true, false);
    case kDxgiBc1UnormSrgb:
      return block_source(SourceFormat::Kind::Bc1, "BC1_UNORM_SRGB", true, false);
    case kDxgiBc2Unorm:
      return block_source(SourceFormat::Kind::Bc2, "BC2_UNORM", true, false);
    case kDxgiBc2UnormSrgb:
      return block_source(SourceFormat::Kind::Bc2, "BC2_UNORM_SRGB", true, false);
    case kDxgiBc3Unorm:
      return block_source(SourceFormat::Kind::Bc3, "BC3_UNORM", true, false);
    case kDxgiBc3UnormSrgb:
      return block_source(SourceFormat::Kind::Bc3, "BC3_UNORM_SRGB", true, false);
    case kDxgiBc4Unorm:
      return block_source(SourceFormat::Kind::Bc4, "BC4_UNORM", false, false);
    case kDxgiBc4Snorm:
      return block_source(SourceFormat::Kind::Bc4, "BC4_SNORM", false, true);
    case kDxgiBc5Unorm:
      return block_source(SourceFormat::Kind::Bc5, "BC5_UNORM", false, false);
    case kDxgiBc5Snorm:
      return block_source(SourceFormat::Kind::Bc5, "BC5_SNORM", false, true);
    case kDxgiB5G6R5Unorm:
      return masked_source("B5G6R5_UNORM", 16, 0xf800U, 0x07e0U, 0x001fU, 0);
    case kDxgiB5G5R5A1Unorm:
      return masked_source("B5G5R5A1_UNORM", 16, 0x7c00U, 0x03e0U, 0x001fU, 0x8000U);
    case kDxgiB8G8R8A8Unorm:
      return masked_source("B8G8R8A8_UNORM", 32, 0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0xff000000U);
    case kDxgiB8G8R8X8Unorm:
      return masked_source("B8G8R8X8_UNORM", 32, 0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0);
    case kDxgiB8G8R8A8UnormSrgb:
      return masked_source("B8G8R8A8_UNORM_SRGB", 32, 0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0xff000000U);
    case kDxgiB8G8R8X8UnormSrgb:
      return masked_source("B8G8R8X8_UNORM_SRGB", 32, 0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0);
    case kDxgiBc6hUf16:
      return block_source(SourceFormat::Kind::Bc6h, "BC6H_UF16", false, false);
    case kDxgiBc6hSf16:
      return block_source(SourceFormat::Kind::Bc6h, "BC6H_SF16", false, true);
    case kDxgiBc7Unorm:
      return block_source(SourceFormat::Kind::Bc7, "BC7_UNORM", true, false);
    case kDxgiBc7UnormSrgb:
      return block_source(SourceFormat::Kind::Bc7, "BC7_UNORM_SRGB", true, false);
    case kDxgiB4G4R4A4Unorm:
      return masked_source("B4G4R4A4_UNORM", 16, 0x0f00U, 0x00f0U, 0x000fU, 0xf000U);
    default:
      break;
  }
  throw std::runtime_error("Unsupported DDS pixel format: DXGI format " + std::to_string(dxgi));
}

[[nodiscard]] SourceFormat resolve_fourcc(std::uint32_t code) {
  switch (code) {
    case kFourCcDxt1:
      return block_source(SourceFormat::Kind::Bc1, "DXT1", true, false);
    case kFourCcDxt2: {
      auto format = block_source(SourceFormat::Kind::Bc2, "DXT2", true, false);
      format.premultiplied = true;
      return format;
    }
    case kFourCcDxt3:
      return block_source(SourceFormat::Kind::Bc2, "DXT3", true, false);
    case kFourCcDxt4: {
      auto format = block_source(SourceFormat::Kind::Bc3, "DXT4", true, false);
      format.premultiplied = true;
      return format;
    }
    case kFourCcDxt5:
      return block_source(SourceFormat::Kind::Bc3, "DXT5", true, false);
    case kFourCcAti1:
      return block_source(SourceFormat::Kind::Bc4, "ATI1", false, false);
    case kFourCcBc4U:
      return block_source(SourceFormat::Kind::Bc4, "BC4U", false, false);
    case kFourCcBc4S:
      return block_source(SourceFormat::Kind::Bc4, "BC4S", false, true);
    case kFourCcAti2:
      return block_source(SourceFormat::Kind::Bc5, "ATI2", false, false);
    case kFourCcBc5U:
      return block_source(SourceFormat::Kind::Bc5, "BC5U", false, false);
    case kFourCcBc5S:
      return block_source(SourceFormat::Kind::Bc5, "BC5S", false, true);
    case kLegacyFourCcA16B16G16R16: {
      auto format = resolve_dxgi(kDxgiR16G16B16A16Unorm);
      format.name = "A16B16G16R16";
      return format;
    }
    case kLegacyFourCcA16B16G16R16F: {
      auto format = resolve_dxgi(kDxgiR16G16B16A16Float);
      format.name = "A16B16G16R16F";
      return format;
    }
    case kLegacyFourCcA32B32G32R32F: {
      auto format = resolve_dxgi(kDxgiR32G32B32A32Float);
      format.name = "A32B32G32R32F";
      return format;
    }
    default:
      break;
  }
  if (code < 256) {
    throw std::runtime_error("Unsupported DDS pixel format: D3D format " + std::to_string(code));
  }
  throw std::runtime_error("Unsupported DDS pixel format '" + fourcc_text(code) + "'");
}

[[nodiscard]] SourceFormat resolve_masked(const PixelFormatDescriptor& pf) {
  if ((pf.flags & (kPixelFormatPaletteIndexed4 | kPixelFormatPaletteIndexed8)) != 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Palettized DDS textures are not supported"));
  }
  if ((pf.flags & kPixelFormatYuv) != 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "YUV DDS textures are not supported"));
  }
  if ((pf.flags & kPixelFormatBumpDuDv) != 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Bump-map (signed) DDS textures are not supported"));
  }
  if (pf.rgb_bit_count != 8 && pf.rgb_bit_count != 16 && pf.rgb_bit_count != 24 && pf.rgb_bit_count != 32) {
    throw std::runtime_error("Unsupported DDS pixel format: " + std::to_string(pf.rgb_bit_count) +
                             " bits per pixel with bit masks");
  }
  SourceFormat format;
  format.kind = SourceFormat::Kind::Masked;
  format.bits_per_pixel = pf.rgb_bit_count;
  format.masks = pf;
  const bool colour_masks = pf.r_mask != 0 || pf.g_mask != 0 || pf.b_mask != 0;
  if ((pf.flags & kPixelFormatLuminance) != 0 || ((pf.flags & kPixelFormatRgb) == 0 && pf.r_mask != 0 &&
                                                   pf.g_mask == 0 && pf.b_mask == 0)) {
    format.luminance = true;
  } else if (!colour_masks) {
    if ((pf.flags & (kPixelFormatAlpha | kPixelFormatAlphaPixels)) == 0 || (pf.a_mask == 0 && pf.rgb_bit_count != 8)) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "DDS pixel format has no colour or alpha channel masks"));
    }
    format.alpha_only = true;
    if (pf.a_mask == 0) {
      format.masks.a_mask = 0xffU;  // an A8 file that left the mask empty
    }
  }
  // A mask placed above the pixel size is slid down to fit (Pillow writes its 8-bit
  // luminance mask as 0xFF000000 and the LA alpha mask likewise); one that is still too wide
  // is a corrupt header.
  const auto fit_mask = [&](std::uint32_t& mask) {
    if (mask == 0) {
      return true;
    }
    const auto analysis = analyze_mask(mask);
    if (analysis.bits > 16 || analysis.bits > pf.rgb_bit_count) {
      return false;
    }
    if (analysis.shift + analysis.bits > pf.rgb_bit_count) {
      mask >>= analysis.shift + analysis.bits - pf.rgb_bit_count;
    }
    return true;
  };
  if (!fit_mask(format.masks.r_mask) || !fit_mask(format.masks.g_mask) || !fit_mask(format.masks.b_mask) ||
      !fit_mask(format.masks.a_mask)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "DDS pixel format bit masks do not fit the pixel size"));
  }
  format.has_alpha = format.masks.a_mask != 0;
  format.name = masked_format_name(format.masks, format.luminance, format.alpha_only);
  return format;
}

struct ImageInfo {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint64_t bytes{0};
};

// Fills `rgba` (width*height*4, top-down) from one uncompressed or block-compressed image.
void decode_image(const SourceFormat& format, std::span<const std::uint8_t> payload, std::uint32_t width,
                  std::uint32_t height, std::vector<std::uint8_t>& rgba) {
  rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, std::uint8_t{0});

  if (format.kind == SourceFormat::Kind::Masked) {
    const auto bytes_per_pixel = static_cast<std::size_t>(format.bits_per_pixel / 8U);
    const auto r = analyze_mask(format.masks.r_mask);
    const auto g = analyze_mask(format.masks.g_mask);
    const auto b = analyze_mask(format.masks.b_mask);
    const auto a = analyze_mask(format.masks.a_mask);
    const auto* source = payload.data();
    auto* destination = rgba.data();
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
      std::uint32_t raw = 0;
      for (std::size_t byte = 0; byte < bytes_per_pixel; ++byte) {
        raw |= static_cast<std::uint32_t>(source[byte]) << (8U * byte);
      }
      source += bytes_per_pixel;
      if (format.alpha_only) {
        destination[0] = destination[1] = destination[2] = 255;
      } else if (format.luminance) {
        const auto value = channel_to_8bit(raw, r);
        destination[0] = destination[1] = destination[2] = value;
      } else {
        destination[0] = channel_to_8bit(raw, r);
        destination[1] = channel_to_8bit(raw, g);
        destination[2] = channel_to_8bit(raw, b);
      }
      destination[3] = a.bits != 0 ? channel_to_8bit(raw, a) : std::uint8_t{255};
      destination += 4;
    }
    return;
  }

  if (format.kind == SourceFormat::Kind::Rgba16) {
    LittleEndianReader reader(payload, "DDS texture is truncated");
    auto* destination = rgba.data();
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
      for (int channel = 0; channel < 4; ++channel) {
        destination[channel] = unorm16_to_8bit(reader.read_u16());
      }
      destination += 4;
    }
    return;
  }

  if (format.kind == SourceFormat::Kind::Float) {
    // Tone map a strip of rows at a time (the curve is per pixel, so strips give the same
    // bytes as the whole image) to bound the float buffer.
    constexpr std::uint32_t kStripRows = 64;
    LittleEndianReader reader(payload, "DDS texture is truncated");
    std::vector<float> strip;
    for (std::uint32_t y0 = 0; y0 < height; y0 += kStripRows) {
      const auto rows = std::min(kStripRows, height - y0);
      strip.assign(static_cast<std::size_t>(rows) * static_cast<std::size_t>(width) * 4U, 1.0F);
      for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(rows) * width; ++pixel) {
        for (std::uint32_t channel = 0; channel < format.float_channels; ++channel) {
          float value = 0.0F;
          if (format.float_bits == 16) {
            value = half_to_float(reader.read_u16());
          } else {
            const auto bits = reader.read_u32();
            std::memcpy(&value, &bits, sizeof(value));
          }
          strip[pixel * 4U + channel] = value;
        }
      }
      const auto mapped = jxr::tone_map_scrgb_to_rgba8(strip, static_cast<std::int32_t>(width),
                                                       static_cast<std::int32_t>(rows));
      std::memcpy(rgba.data() + static_cast<std::size_t>(y0) * width * 4U, mapped.data(), mapped.size());
    }
    return;
  }

  // Block formats: decode each 4x4 block into a scratch and copy the in-image overlap, so
  // non-multiple-of-4 sizes (legal for the top level under DX10, and for every small mip)
  // never write outside the image.
  const auto blocks_x = (width + 3U) / 4U;
  const auto blocks_y = (height + 3U) / 4U;
  const auto bytes_per_block = block_bytes(format.kind);
  std::array<std::uint8_t, 64> scratch{};
  std::array<std::uint8_t, 16> scratch_r{};
  std::array<std::uint8_t, 32> scratch_rg{};
  std::array<float, 48> scratch_float{};
  std::vector<float> hdr_strip;
  if (format.kind == SourceFormat::Kind::Bc6h) {
    hdr_strip.resize(static_cast<std::size_t>(blocks_x) * 16U * 4U);
  }
  const auto* block = payload.data();
  for (std::uint32_t by = 0; by < blocks_y; ++by) {
    for (std::uint32_t bx = 0; bx < blocks_x; ++bx, block += bytes_per_block) {
      switch (format.kind) {
        case SourceFormat::Kind::Bc1:
          bcdec_bc1(block, scratch.data(), 16);
          break;
        case SourceFormat::Kind::Bc2:
          bcdec_bc2(block, scratch.data(), 16);
          break;
        case SourceFormat::Kind::Bc3:
          bcdec_bc3(block, scratch.data(), 16);
          break;
        case SourceFormat::Kind::Bc7:
          bcdec_bc7(block, scratch.data(), 16);
          break;
        case SourceFormat::Kind::Bc4:
          bcdec_bc4(block, scratch_r.data(), 4, format.is_signed ? 1 : 0);
          for (std::size_t i = 0; i < 16; ++i) {
            const auto value = format.is_signed ? snorm8_to_unorm8(static_cast<signed char>(scratch_r[i])) : scratch_r[i];
            scratch[i * 4U] = scratch[i * 4U + 1U] = scratch[i * 4U + 2U] = value;
            scratch[i * 4U + 3U] = 255;
          }
          break;
        case SourceFormat::Kind::Bc5:
          bcdec_bc5(block, scratch_rg.data(), 8, format.is_signed ? 1 : 0);
          for (std::size_t i = 0; i < 16; ++i) {
            const auto red = scratch_rg[i * 2U];
            const auto green = scratch_rg[i * 2U + 1U];
            scratch[i * 4U] = format.is_signed ? snorm8_to_unorm8(static_cast<signed char>(red)) : red;
            scratch[i * 4U + 1U] = format.is_signed ? snorm8_to_unorm8(static_cast<signed char>(green)) : green;
            scratch[i * 4U + 2U] = 0;
            scratch[i * 4U + 3U] = 255;
          }
          break;
        case SourceFormat::Kind::Bc6h: {
          bcdec_bc6h_float(block, scratch_float.data(), 12, format.is_signed ? 1 : 0);
          // Gather the block row's floats as a 4-row strip and tone map it once per row of
          // blocks: identical bytes to a whole-image pass, bounded memory.
          for (std::size_t ty = 0; ty < 4; ++ty) {
            for (std::size_t tx = 0; tx < 4; ++tx) {
              const auto strip_pixel = (ty * blocks_x * 4U) + static_cast<std::size_t>(bx) * 4U + tx;
              float* out = hdr_strip.data() + strip_pixel * 4U;
              const float* in = scratch_float.data() + (ty * 4U + tx) * 3U;
              out[0] = in[0];
              out[1] = in[1];
              out[2] = in[2];
              out[3] = 1.0F;
            }
          }
          if (bx + 1 == blocks_x) {
            const auto mapped =
                jxr::tone_map_scrgb_to_rgba8(hdr_strip, static_cast<std::int32_t>(blocks_x * 4U), 4);
            const auto rows = std::min<std::uint32_t>(4U, height - by * 4U);
            for (std::uint32_t ty = 0; ty < rows; ++ty) {
              std::memcpy(rgba.data() + (static_cast<std::size_t>(by) * 4U + ty) * width * 4U,
                          mapped.data() + static_cast<std::size_t>(ty) * blocks_x * 16U,
                          static_cast<std::size_t>(width) * 4U);
            }
          }
          continue;
        }
        case SourceFormat::Kind::Masked:
        case SourceFormat::Kind::Rgba16:
        case SourceFormat::Kind::Float:
          break;
      }
      const auto columns = std::min<std::uint32_t>(4U, width - bx * 4U);
      const auto rows = std::min<std::uint32_t>(4U, height - by * 4U);
      for (std::uint32_t ty = 0; ty < rows; ++ty) {
        std::memcpy(rgba.data() + ((static_cast<std::size_t>(by) * 4U + ty) * width + static_cast<std::size_t>(bx) * 4U) * 4U,
                    scratch.data() + ty * 16U, static_cast<std::size_t>(columns) * 4U);
      }
    }
  }
}

// --- writer helpers ---

[[nodiscard]] std::uint16_t pack565(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
  const auto r5 = (static_cast<std::uint32_t>(r) * 31U + 127U) / 255U;
  const auto g6 = (static_cast<std::uint32_t>(g) * 63U + 127U) / 255U;
  const auto b5 = (static_cast<std::uint32_t>(b) * 31U + 127U) / 255U;
  return static_cast<std::uint16_t>((r5 << 11U) | (g6 << 5U) | b5);
}

void unpack565(std::uint16_t value, std::uint8_t* rgb) noexcept {
  rgb[0] = replicate_bits((value >> 11U) & 31U, 5);
  rgb[1] = replicate_bits((value >> 5U) & 63U, 6);
  rgb[2] = replicate_bits(value & 31U, 5);
}

// Patchy's own 3-colour BC1 encoder for blocks with cut-out texels: a range fit over the
// opaque texels (bounding-box endpoints, c0 <= c1 selects the 3-colour mode) and index 3
// for every transparent texel. Integer math only, deterministic across toolchains.
void encode_bc1_punch_through_block(const std::uint8_t* rgba, std::uint8_t* out) {
  std::array<int, 3> low = {255, 255, 255};
  std::array<int, 3> high = {0, 0, 0};
  bool any_opaque = false;
  for (int texel = 0; texel < 16; ++texel) {
    const auto* pixel = rgba + texel * 4;
    if (pixel[3] < kCutoutThreshold) {
      continue;
    }
    any_opaque = true;
    for (int channel = 0; channel < 3; ++channel) {
      low[channel] = std::min<int>(low[channel], pixel[channel]);
      high[channel] = std::max<int>(high[channel], pixel[channel]);
    }
  }
  std::uint16_t c0 = 0;
  std::uint16_t c1 = 0;
  if (any_opaque) {
    c0 = pack565(static_cast<std::uint8_t>(low[0]), static_cast<std::uint8_t>(low[1]), static_cast<std::uint8_t>(low[2]));
    c1 = pack565(static_cast<std::uint8_t>(high[0]), static_cast<std::uint8_t>(high[1]),
                 static_cast<std::uint8_t>(high[2]));
    if (c0 > c1) {
      std::swap(c0, c1);
    }
  }
  std::array<std::array<int, 3>, 3> palette{};
  std::array<std::uint8_t, 3> rgb{};
  unpack565(c0, rgb.data());
  palette[0] = {rgb[0], rgb[1], rgb[2]};
  unpack565(c1, rgb.data());
  palette[1] = {rgb[0], rgb[1], rgb[2]};
  for (int channel = 0; channel < 3; ++channel) {
    palette[2][channel] = (palette[0][channel] + palette[1][channel]) / 2;
  }
  std::uint32_t indices = 0;
  for (int texel = 0; texel < 16; ++texel) {
    const auto* pixel = rgba + texel * 4;
    std::uint32_t index = 3;
    if (pixel[3] >= kCutoutThreshold) {
      int best = 0x7fffffff;
      for (std::uint32_t candidate = 0; candidate < 3; ++candidate) {
        int distance = 0;
        for (int channel = 0; channel < 3; ++channel) {
          const int delta = pixel[channel] - palette[candidate][channel];
          distance += delta * delta;
        }
        if (distance < best) {
          best = distance;
          index = candidate;
        }
      }
    }
    indices |= index << (2U * static_cast<unsigned>(texel));
  }
  out[0] = static_cast<std::uint8_t>(c0 & 0xffU);
  out[1] = static_cast<std::uint8_t>(c0 >> 8U);
  out[2] = static_cast<std::uint8_t>(c1 & 0xffU);
  out[3] = static_cast<std::uint8_t>(c1 >> 8U);
  for (unsigned byte = 0; byte < 4; ++byte) {
    out[4 + byte] = static_cast<std::uint8_t>((indices >> (8U * byte)) & 0xffU);
  }
}

// 4x4 RGBA8 texels of the block at (bx, by), coordinates clamped to the image so partial
// edge blocks (and levels smaller than 4 px) repeat their last texel instead of reading
// zeroes that would pull the endpoints toward black.
void gather_block(const PixelBuffer& rgba8, std::int32_t bx, std::int32_t by, std::uint8_t* out) {
  for (std::int32_t ty = 0; ty < 4; ++ty) {
    const auto y = std::min(by * 4 + ty, rgba8.height() - 1);
    const auto row = rgba8.row(y);
    for (std::int32_t tx = 0; tx < 4; ++tx) {
      const auto x = std::min(bx * 4 + tx, rgba8.width() - 1);
      std::memcpy(out + (static_cast<std::size_t>(ty) * 4U + static_cast<std::size_t>(tx)) * 4U,
                  row.data() + static_cast<std::size_t>(x) * 4U, 4);
    }
  }
}

void append_uncompressed_level(std::vector<std::uint8_t>& payload, const PixelBuffer& rgba8) {
  for (std::int32_t y = 0; y < rgba8.height(); ++y) {
    const auto row = rgba8.row(y);
    for (std::int32_t x = 0; x < rgba8.width(); ++x) {
      const auto* pixel = row.data() + static_cast<std::size_t>(x) * 4U;
      payload.push_back(pixel[2]);
      payload.push_back(pixel[1]);
      payload.push_back(pixel[0]);
      payload.push_back(pixel[3]);
    }
  }
}

[[nodiscard]] std::uint32_t written_block_bytes(Compression compression) noexcept {
  switch (compression) {
    case Compression::Bc1:
    case Compression::Bc4:
      return 8;
    case Compression::Bc3:
    case Compression::Bc5:
    case Compression::Bc7:
      return 16;
    case Compression::Automatic:
    case Compression::Uncompressed:
      break;
  }
  return 0;
}

void encode_block(const std::uint8_t* block, Compression compression, std::uint8_t* encoded) {
  switch (compression) {
    case Compression::Bc3:
      encode_bc3_block(block, encoded);
      break;
    case Compression::Bc4:
      encode_bc4_block(block, encoded);
      break;
    case Compression::Bc5:
      encode_bc5_block(block, encoded);
      break;
    case Compression::Bc7:
      encode_bc7_block(block, encoded);
      break;
    case Compression::Bc1:
    case Compression::Automatic:
    case Compression::Uncompressed:
      encode_bc1_block(block, bc1_block_has_cutout(block), encoded);
      break;
  }
}

// Encodes one level's blocks row-major after `payload`'s current end. Every block is
// independent and lands in its own slot, so block rows fan out across threads without
// changing a byte of the output (the byte canary covers this path). The first block row
// runs on the calling thread before the fan-out: stb_dxt and bc7enc build their lookup
// tables on first use, and that first use must not race.
void append_block_level(std::vector<std::uint8_t>& payload, const PixelBuffer& rgba8, Compression compression) {
  const auto blocks_x = static_cast<std::size_t>((rgba8.width() + 3) / 4);
  const auto blocks_y = static_cast<std::size_t>((rgba8.height() + 3) / 4);
  const auto bytes = static_cast<std::size_t>(written_block_bytes(compression));
  const auto offset = payload.size();
  payload.resize(offset + blocks_x * blocks_y * bytes);
  auto* const out = payload.data() + offset;
  const auto encode_rows = [&rgba8, compression, blocks_x, bytes, out](std::size_t begin, std::size_t end) {
    std::array<std::uint8_t, 64> block{};
    std::array<std::uint8_t, 16> encoded{};
    for (std::size_t by = begin; by < end; ++by) {
      for (std::size_t bx = 0; bx < blocks_x; ++bx) {
        gather_block(rgba8, static_cast<std::int32_t>(bx), static_cast<std::int32_t>(by), block.data());
        encode_block(block.data(), compression, encoded.data());
        std::memcpy(out + (by * blocks_x + bx) * bytes, encoded.data(), bytes);
      }
    }
  };
  encode_rows(0, std::min<std::size_t>(1, blocks_y));
  if (blocks_y <= 1) {
    return;
  }
  const std::size_t remaining = blocks_y - 1;
  // About 4096 texels per worker before another thread pays off; at most 8 workers like the
  // trace pipeline, and only what the wasm fan-out budget allows.
  const auto min_rows_per_worker = std::max<std::size_t>(1, 1024 / std::max<std::size_t>(1, blocks_x));
  const int wanted = static_cast<int>(std::min<std::size_t>(
      std::max<std::size_t>(1, remaining / min_rows_per_worker),
      static_cast<std::size_t>(std::min(hardware_worker_threads(), 8))));
  const int workers = max_blocking_fanout_workers(wanted);
  if (workers < 2) {
    encode_rows(1, blocks_y);
    return;
  }
  std::vector<std::future<void>> futures;
  futures.reserve(static_cast<std::size_t>(workers));
  const auto chunk = (remaining + static_cast<std::size_t>(workers) - 1) / static_cast<std::size_t>(workers);
  for (int w = 0; w < workers; ++w) {
    const auto begin = 1 + static_cast<std::size_t>(w) * chunk;
    if (begin >= blocks_y) {
      break;
    }
    const auto end = std::min(blocks_y, begin + chunk);
    futures.push_back(std::async(std::launch::async, [&encode_rows, begin, end] { encode_rows(begin, end); }));
  }
  for (auto& future : futures) {
    future.get();  // rethrows a worker's bad_alloc instead of writing a truncated level
  }
}

// The reader's view of one level the writer produced: decodes `payload` (one level of
// `compression` blocks, or A8R8G8B8 rows) back to RGBA8 through the same decoder read_dds
// uses, so the preview and a real round trip agree byte for byte.
[[nodiscard]] PixelBuffer decode_written_level(std::span<const std::uint8_t> payload, std::int32_t width,
                                               std::int32_t height, Compression compression) {
  SourceFormat format;
  switch (compression) {
    case Compression::Uncompressed:
      format = masked_source("A8R8G8B8", 32, 0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0xff000000U);
      break;
    case Compression::Bc3:
      format = block_source(SourceFormat::Kind::Bc3, "DXT5", true, false);
      break;
    case Compression::Bc4:
      format = block_source(SourceFormat::Kind::Bc4, "ATI1", false, false);
      break;
    case Compression::Bc5:
      format = block_source(SourceFormat::Kind::Bc5, "ATI2", false, false);
      break;
    case Compression::Bc7:
      format = block_source(SourceFormat::Kind::Bc7, "BC7_UNORM", true, false);
      break;
    case Compression::Bc1:
    case Compression::Automatic:
      format = block_source(SourceFormat::Kind::Bc1, "DXT1", true, false);
      break;
  }
  std::vector<std::uint8_t> rgba;
  decode_image(format, payload, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), rgba);
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    std::memcpy(pixels.row(y).data(), rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U,
                static_cast<std::size_t>(width) * 4U);
  }
  return pixels;
}

}  // namespace

const std::vector<std::string>& dds_extensions() {
  static const std::vector<std::string> extensions = {"dds"};
  return extensions;
}

bool is_dds_extension(std::string_view extension) {
  const auto normalized = normalized_extension(extension, false);
  const auto& extensions = dds_extensions();
  return std::find(extensions.begin(), extensions.end(), normalized) != extensions.end();
}

bool sniff(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kFileHeaderBytes) {
    return false;
  }
  LittleEndianReader reader(bytes);
  if (reader.read_u32() != kMagic || reader.read_u32() != kHeaderSize) {
    return false;
  }
  reader.seek(kPixelFormatOffset);
  return reader.read_u32() == kPixelFormatSize;
}

Header parse_header(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 4 || LittleEndianReader(bytes).read_u32() != kMagic) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Not a DDS texture: the 'DDS ' signature is missing"));
  }
  LittleEndianReader reader(bytes, PATCHY_TRANSLATE_NOOP("QObject", "DDS texture is truncated: the header is incomplete"));
  reader.skip(4);
  if (reader.read_u32() != kHeaderSize) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "DDS header has an unexpected size"));
  }
  Header header;
  header.flags = reader.read_u32();
  header.height = reader.read_u32();
  header.width = reader.read_u32();
  header.pitch_or_linear_size = reader.read_u32();
  header.depth = reader.read_u32();
  header.mipmap_count = reader.read_u32();
  reader.skip(11 * 4);  // dwReserved1
  if (reader.read_u32() != kPixelFormatSize) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "DDS pixel format block has an unexpected size"));
  }
  header.pixel_format.flags = reader.read_u32();
  header.pixel_format.fourcc = reader.read_u32();
  header.pixel_format.rgb_bit_count = reader.read_u32();
  header.pixel_format.r_mask = reader.read_u32();
  header.pixel_format.g_mask = reader.read_u32();
  header.pixel_format.b_mask = reader.read_u32();
  header.pixel_format.a_mask = reader.read_u32();
  header.caps = reader.read_u32();
  header.caps2 = reader.read_u32();
  reader.skip(3 * 4);  // dwCaps3, dwCaps4, dwReserved2
  header.data_offset = kFileHeaderBytes;

  if ((header.pixel_format.flags & kPixelFormatFourCc) != 0 && header.pixel_format.fourcc == kFourCcDx10) {
    Dx10Header dx10;
    dx10.dxgi_format = reader.read_u32();
    dx10.resource_dimension = reader.read_u32();
    dx10.misc_flag = reader.read_u32();
    dx10.array_size = reader.read_u32();
    dx10.misc_flags2 = reader.read_u32();
    header.data_offset = kFileHeaderBytes + kDx10HeaderSize;
    if (dx10.resource_dimension != kResourceDimensionTexture1D && dx10.resource_dimension != kResourceDimensionTexture2D &&
        dx10.resource_dimension != kResourceDimensionTexture3D) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "DDS texture has an unknown resource dimension"));
    }
    if (dx10.array_size == 0) {
      dx10.array_size = 1;  // Pillow writes 0 for a plain 2D texture
    }
    if (dx10.array_size > kMaxArrayOrDepth) {
      throw std::runtime_error("DDS texture array size " + std::to_string(dx10.array_size) + " is out of range");
    }
    if (dx10.resource_dimension == kResourceDimensionTexture1D) {
      header.height = 1;
    }
    header.dx10 = dx10;
  }

  if (header.width == 0 || header.height == 0 || header.width > static_cast<std::uint32_t>(kMaxSide) ||
      header.height > static_cast<std::uint32_t>(kMaxSide) ||
      static_cast<std::uint64_t>(header.width) * static_cast<std::uint64_t>(header.height) > kMaxPixels) {
    throw std::runtime_error("DDS texture has an invalid size (" + std::to_string(header.width) + "x" +
                             std::to_string(header.height) + ")");
  }
  if (header.mipmap_count == 0) {
    header.mipmap_count = 1;
  }
  if (header.mipmap_count > kMaxMipLevels) {
    throw std::runtime_error("DDS texture claims " + std::to_string(header.mipmap_count) + " mip levels");
  }
  const bool volume = (header.caps2 & kCaps2Volume) != 0 ||
                      (header.dx10.has_value() && header.dx10->resource_dimension == kResourceDimensionTexture3D);
  if (!volume || header.depth == 0) {
    header.depth = 1;
  }
  if (header.depth == 0 || header.depth > kMaxArrayOrDepth) {
    throw std::runtime_error("DDS volume texture depth " + std::to_string(header.depth) + " is out of range");
  }
  const auto images = image_count(header);
  if (static_cast<std::uint64_t>(images) * header.width * header.height > kMaxPixels) {
    throw std::runtime_error("DDS texture holds too many pixels across its " + std::to_string(images) + " images");
  }
  return header;
}

TextureLayout texture_layout(const Header& header) noexcept {
  if (header.dx10.has_value()) {
    if (header.dx10->resource_dimension == kResourceDimensionTexture3D) {
      return TextureLayout::Volume;
    }
    if ((header.dx10->misc_flag & kMiscFlagTextureCube) != 0) {
      return TextureLayout::Cubemap;
    }
    if (header.dx10->array_size > 1) {
      return TextureLayout::Array;
    }
    return TextureLayout::Texture2D;
  }
  if ((header.caps2 & kCaps2Volume) != 0) {
    return TextureLayout::Volume;
  }
  if ((header.caps2 & kCaps2Cubemap) != 0) {
    return TextureLayout::Cubemap;
  }
  return TextureLayout::Texture2D;
}

AlphaMode alpha_mode(const Header& header) noexcept {
  if (header.dx10.has_value()) {
    switch (header.dx10->misc_flags2 & kAlphaModeMask) {
      case 1:
        return AlphaMode::Straight;
      case 2:
        return AlphaMode::Premultiplied;
      case 3:
        return AlphaMode::Opaque;
      case 4:
        return AlphaMode::Custom;
      default:
        return AlphaMode::Unknown;
    }
  }
  if ((header.pixel_format.flags & kPixelFormatFourCc) != 0 &&
      (header.pixel_format.fourcc == kFourCcDxt2 || header.pixel_format.fourcc == kFourCcDxt4)) {
    return AlphaMode::Premultiplied;
  }
  return AlphaMode::Unknown;
}

std::uint32_t image_count(const Header& header) noexcept {
  switch (texture_layout(header)) {
    case TextureLayout::Volume:
      return std::max<std::uint32_t>(1, header.depth);
    case TextureLayout::Cubemap: {
      if (header.dx10.has_value()) {
        return 6U * std::max<std::uint32_t>(1, header.dx10->array_size);
      }
      std::uint32_t faces = 0;
      for (std::uint32_t bit = kCaps2CubemapPositiveX; bit <= (kCaps2CubemapPositiveX << 5U); bit <<= 1U) {
        faces += (header.caps2 & bit) != 0 ? 1U : 0U;
      }
      return faces == 0 ? 6U : faces;  // writers that set the cubemap bit without face bits mean all six
    }
    case TextureLayout::Array:
      return header.dx10->array_size;
    case TextureLayout::Texture2D:
      break;
  }
  return 1;
}

SourceFormat resolve_source_format(const Header& header) {
  const auto& pf = header.pixel_format;
  if (header.dx10.has_value()) {
    return resolve_dxgi(header.dx10->dxgi_format);
  }
  if ((pf.flags & kPixelFormatFourCc) != 0) {
    return resolve_fourcc(pf.fourcc);
  }
  return resolve_masked(pf);
}

std::uint64_t image_byte_size(const SourceFormat& format, std::uint32_t width, std::uint32_t height) noexcept {
  if (is_block_kind(format.kind)) {
    const std::uint64_t blocks_x = (static_cast<std::uint64_t>(width) + 3U) / 4U;
    const std::uint64_t blocks_y = (static_cast<std::uint64_t>(height) + 3U) / 4U;
    return blocks_x * blocks_y * block_bytes(format.kind);
  }
  return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * (format.bits_per_pixel / 8U);
}

std::uint64_t mip_chain_byte_size(const SourceFormat& format, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t levels) noexcept {
  std::uint64_t total = 0;
  for (std::uint32_t level = 0; level < levels; ++level) {
    total += image_byte_size(format, width, height);
    width = std::max<std::uint32_t>(1, width / 2U);
    height = std::max<std::uint32_t>(1, height / 2U);
  }
  return total;
}

float half_to_float(std::uint16_t half) noexcept {
  const std::uint32_t sign = (static_cast<std::uint32_t>(half) & 0x8000U) << 16U;
  const std::uint32_t exponent = (half >> 10U) & 0x1fU;
  std::uint32_t mantissa = half & 0x3ffU;
  std::uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;  // signed zero
    } else {
      // Subnormal: normalize by shifting the mantissa up until its implicit bit appears.
      int shift = 0;
      while ((mantissa & 0x400U) == 0) {
        mantissa <<= 1U;
        ++shift;
      }
      mantissa &= 0x3ffU;
      const std::uint32_t biased = static_cast<std::uint32_t>(127 - 15 - shift + 1);
      bits = sign | (biased << 23U) | (mantissa << 13U);
    }
  } else if (exponent == 0x1fU) {
    bits = sign | 0x7f800000U | (mantissa << 13U);  // infinity or NaN
  } else {
    bits = sign | ((exponent + (127U - 15U)) << 23U) | (mantissa << 13U);
  }
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void unpremultiply_rgba8_in_place(std::span<std::uint8_t> rgba) {
  for (std::size_t offset = 0; offset + 3 < rgba.size(); offset += 4) {
    const std::uint32_t alpha = rgba[offset + 3];
    if (alpha == 0) {
      rgba[offset] = rgba[offset + 1] = rgba[offset + 2] = 0;
      continue;
    }
    if (alpha == 255) {
      continue;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const std::uint32_t value = (static_cast<std::uint32_t>(rgba[offset + channel]) * 255U + alpha / 2U) / alpha;
      rgba[offset + channel] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value));
    }
  }
}

FormatReadResult read_dds(std::span<const std::uint8_t> bytes) {
  const auto header = parse_header(bytes);
  const auto format = resolve_source_format(header);
  const auto layout = texture_layout(header);
  const auto mode = alpha_mode(header);
  const auto images = image_count(header);
  const auto width = header.width;
  const auto height = header.height;

  const auto image_bytes = image_byte_size(format, width, height);
  const auto chain_bytes = mip_chain_byte_size(format, width, height, header.mipmap_count);
  // Mip 0 of every image must be present; a file cut short inside its later mip levels still
  // opens. Volume slices of level 0 are contiguous, every other layout stores one whole mip
  // chain per image.
  const std::uint64_t image_stride = layout == TextureLayout::Volume ? image_bytes : chain_bytes;
  const std::uint64_t required = header.data_offset + static_cast<std::uint64_t>(images - 1U) * image_stride + image_bytes;
  if (required > bytes.size()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "DDS texture is truncated: the pixel data is shorter than the header promises"));
  }

  FormatReadResult result;
  auto& notices = result.notices;
  if (header.mipmap_count > 1) {
    notices.push_back("Only the first of " + std::to_string(header.mipmap_count) +
                      " mip levels was read; Patchy regenerates mipmaps on save");
  }
  bool has_alpha = format.has_alpha;
  bool premultiplied = format.premultiplied;
  if (has_alpha) {
    switch (mode) {
      case AlphaMode::Premultiplied:
        premultiplied = true;
        break;
      case AlphaMode::Opaque:
        has_alpha = false;
        notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "The file marks its alpha channel as unused; it was ignored"));
        break;
      case AlphaMode::Custom:
        notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "The file marks its alpha channel as custom data; it was read as straight alpha"));
        break;
      case AlphaMode::Straight:
      case AlphaMode::Unknown:
        break;
    }
  }
  if (premultiplied && has_alpha) {
    notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Premultiplied alpha was converted to straight alpha"));
  }
  switch (format.kind) {
    case SourceFormat::Kind::Rgba16:
      notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "16-bit channels were converted to 8 bits"));
      break;
    case SourceFormat::Kind::Float:
    case SourceFormat::Kind::Bc6h:
      notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Floating-point HDR pixels were tone mapped to 8 bits"));
      break;
    case SourceFormat::Kind::Bc5:
      notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Two-channel BC5 texture: the blue channel was set to 0"));
      break;
    case SourceFormat::Kind::Masked:
      if (format.alpha_only) {
        notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Alpha-only texture opened as white with its alpha channel"));
      }
      break;
    case SourceFormat::Kind::Bc1:
    case SourceFormat::Kind::Bc2:
    case SourceFormat::Kind::Bc3:
    case SourceFormat::Kind::Bc4:
    case SourceFormat::Kind::Bc7:
      break;
  }

  const auto pixel_format = has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8();
  const auto channels = static_cast<std::size_t>(pixel_format.channels);
  Document document(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), pixel_format);
  static constexpr const char* kFaceNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
  std::vector<std::uint8_t> rgba;
  for (std::uint32_t image = 0; image < images; ++image) {
    const auto offset = static_cast<std::size_t>(header.data_offset + image * image_stride);
    decode_image(format, bytes.subspan(offset, static_cast<std::size_t>(image_bytes)), width, height, rgba);
    if (premultiplied && has_alpha) {
      unpremultiply_rgba8_in_place(rgba);
    }
    PixelBuffer pixels(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), pixel_format);
    for (std::uint32_t y = 0; y < height; ++y) {
      auto row = pixels.row(static_cast<std::int32_t>(y));
      const auto* source = rgba.data() + static_cast<std::size_t>(y) * width * 4U;
      for (std::uint32_t x = 0; x < width; ++x) {
        std::memcpy(row.data() + static_cast<std::size_t>(x) * channels, source + static_cast<std::size_t>(x) * 4U, channels);
      }
    }
    std::string name = "Background";
    switch (layout) {
      case TextureLayout::Cubemap: {
        // Legacy files may carry a subset of faces; name the present ones in file order.
        std::uint32_t face = image % 6U;
        if (!header.dx10.has_value()) {
          std::uint32_t seen = 0;
          for (std::uint32_t candidate = 0; candidate < 6; ++candidate) {
            const auto bit = kCaps2CubemapPositiveX << candidate;
            const bool present = (header.caps2 & kCaps2CubemapAllFaces) == 0 || (header.caps2 & bit) != 0;
            if (present && seen++ == image) {
              face = candidate;
              break;
            }
          }
        }
        name = kFaceNames[face];
        if (images > 6) {
          name += " " + std::to_string(image / 6U + 1U);
        }
        break;
      }
      case TextureLayout::Volume:
        name = "Slice " + std::to_string(image + 1U);
        break;
      case TextureLayout::Array:
        name = "Element " + std::to_string(image + 1U);
        break;
      case TextureLayout::Texture2D:
        break;
    }
    auto& layer = document.add_pixel_layer(std::move(name), std::move(pixels));
    layer.set_visible(image == 0);
  }
  if (images > 1) {
    switch (layout) {
      case TextureLayout::Cubemap:
        notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Each cubemap face imported as its own layer; only the first is visible"));
        break;
      case TextureLayout::Volume:
        notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Each volume slice imported as its own layer; only the first is visible"));
        break;
      case TextureLayout::Array:
        notices.push_back(PATCHY_TRANSLATE_NOOP("QObject", "Each array element imported as its own layer; only the first is visible"));
        break;
      case TextureLayout::Texture2D:
        break;
    }
  }

  auto& metadata = document.metadata().values;
  Compression nearest = Compression::Uncompressed;
  switch (format.kind) {
    case SourceFormat::Kind::Bc1:
      nearest = Compression::Bc1;
      break;
    case SourceFormat::Kind::Bc2:
    case SourceFormat::Kind::Bc3:
      nearest = Compression::Bc3;
      break;
    case SourceFormat::Kind::Bc4:
      nearest = Compression::Bc4;
      break;
    case SourceFormat::Kind::Bc5:
      nearest = Compression::Bc5;
      break;
    case SourceFormat::Kind::Bc6h:  // HDR has no export; BC7 is the nearest DX10 block format
    case SourceFormat::Kind::Bc7:
      nearest = Compression::Bc7;
      break;
    case SourceFormat::Kind::Masked:
    case SourceFormat::Kind::Rgba16:
    case SourceFormat::Kind::Float:
      break;
  }
  metadata[kMetadataCompression] = std::string(compression_token(nearest));
  metadata[kMetadataMipmaps] = header.mipmap_count > 1 ? "1" : "0";
  metadata[kMetadataSourceFormat] = format.name;
  result.document = std::move(document);
  return result;
}

FormatReadResult read_dds_file(const std::filesystem::path& path) {
  const auto bytes = formats::read_file_bytes(path, "DDS");
  auto result = read_dds(bytes);
  formats::rename_first_layer_to_stem(result.document, path);
  return result;
}

std::string_view compression_token(Compression compression) noexcept {
  switch (compression) {
    case Compression::Uncompressed:
      return "uncompressed";
    case Compression::Bc1:
      return "bc1";
    case Compression::Bc3:
      return "bc3";
    case Compression::Bc4:
      return "bc4";
    case Compression::Bc5:
      return "bc5";
    case Compression::Bc7:
      return "bc7";
    case Compression::Automatic:
      break;
  }
  return "auto";
}

std::optional<Compression> compression_from_token(std::string_view token) noexcept {
  if (token == "auto") {
    return Compression::Automatic;
  }
  if (token == "uncompressed") {
    return Compression::Uncompressed;
  }
  if (token == "bc1") {
    return Compression::Bc1;
  }
  if (token == "bc3") {
    return Compression::Bc3;
  }
  if (token == "bc4") {
    return Compression::Bc4;
  }
  if (token == "bc5") {
    return Compression::Bc5;
  }
  if (token == "bc7") {
    return Compression::Bc7;
  }
  return std::nullopt;
}

std::uint32_t mip_count_for(std::int32_t width, std::int32_t height) noexcept {
  std::uint32_t count = 1;
  auto side = std::max(width, height);
  while (side > 1) {
    side /= 2;
    ++count;
  }
  return count;
}

PixelBuffer box_downsample(const PixelBuffer& rgba8) {
  const auto source_width = rgba8.width();
  const auto source_height = rgba8.height();
  const auto width = std::max(1, source_width / 2);
  const auto height = std::max(1, source_height / 2);
  PixelBuffer result(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    // The last destination row absorbs an odd leftover source row.
    const auto y0 = source_height == 1 ? 0 : y * 2;
    const auto y1 = source_height == 1 ? 1 : (y + 1 == height ? source_height : y * 2 + 2);
    auto destination_row = result.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto x0 = source_width == 1 ? 0 : x * 2;
      const auto x1 = source_width == 1 ? 1 : (x + 1 == width ? source_width : x * 2 + 2);
      std::uint64_t weighted[3] = {0, 0, 0};
      std::uint64_t plain[3] = {0, 0, 0};
      std::uint64_t alpha_sum = 0;
      std::uint64_t count = 0;
      for (auto sy = y0; sy < y1; ++sy) {
        const auto row = rgba8.row(sy);
        for (auto sx = x0; sx < x1; ++sx) {
          const auto* pixel = row.data() + static_cast<std::size_t>(sx) * 4U;
          const std::uint64_t alpha = pixel[3];
          for (int channel = 0; channel < 3; ++channel) {
            weighted[channel] += pixel[channel] * alpha;
            plain[channel] += pixel[channel];
          }
          alpha_sum += alpha;
          ++count;
        }
      }
      auto* out = destination_row.data() + static_cast<std::size_t>(x) * 4U;
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] = static_cast<std::uint8_t>(alpha_sum == 0 ? (plain[channel] + count / 2) / count
                                                                : (weighted[channel] + alpha_sum / 2) / alpha_sum);
      }
      out[3] = static_cast<std::uint8_t>((alpha_sum + count / 2) / count);
    }
  }
  return result;
}

std::vector<PixelBuffer> generate_mip_chain(const PixelBuffer& rgba8) {
  std::vector<PixelBuffer> chain;
  chain.push_back(rgba8);
  while (chain.back().width() > 1 || chain.back().height() > 1) {
    chain.push_back(box_downsample(chain.back()));
  }
  return chain;
}

bool bc1_block_has_cutout(const std::uint8_t* rgba) noexcept {
  for (int texel = 0; texel < 16; ++texel) {
    if (rgba[texel * 4 + 3] < kCutoutThreshold) {
      return true;
    }
  }
  return false;
}

void encode_bc1_block(const std::uint8_t* rgba, bool punch_through, std::uint8_t* out) {
  if (punch_through) {
    encode_bc1_punch_through_block(rgba, out);
    return;
  }
  stb_compress_dxt_block(out, rgba, 0, STB_DXT_HIGHQUAL);
}

void encode_bc3_block(const std::uint8_t* rgba, std::uint8_t* out) {
  stb_compress_dxt_block(out, rgba, 1, STB_DXT_HIGHQUAL);
}

std::uint8_t luminance8(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(r) * 299U + static_cast<std::uint32_t>(g) * 587U +
                                    static_cast<std::uint32_t>(b) * 114U + 500U) /
                                   1000U);
}

void encode_bc4_block(const std::uint8_t* rgba, std::uint8_t* out) {
  std::array<std::uint8_t, 16> gray{};
  for (int texel = 0; texel < 16; ++texel) {
    gray[static_cast<std::size_t>(texel)] = luminance8(rgba[texel * 4], rgba[texel * 4 + 1], rgba[texel * 4 + 2]);
  }
  stb_compress_bc4_block(out, gray.data());
}

void encode_bc5_block(const std::uint8_t* rgba, std::uint8_t* out) {
  std::array<std::uint8_t, 32> red_green{};
  for (int texel = 0; texel < 16; ++texel) {
    red_green[static_cast<std::size_t>(texel) * 2U] = rgba[texel * 4];
    red_green[static_cast<std::size_t>(texel) * 2U + 1U] = rgba[texel * 4 + 1];
  }
  stb_compress_bc5_block(out, red_green.data());
}

void encode_bc7_block(const std::uint8_t* rgba, std::uint8_t* out) {
  // bc7enc builds its lookup tables once; the writer runs on one thread, so a function-local
  // static is enough. Linear (not perceptual) weights keep every channel, alpha included,
  // equally important, which is what a texture round trip wants.
  static const bool initialized = [] {
    bc7enc_compress_block_init();
    return true;
  }();
  (void)initialized;
  bc7enc_compress_block_params params;
  bc7enc_compress_block_params_init(&params);
  bc7enc_compress_block_params_init_linear_weights(&params);
  params.m_uber_level = 1;
  (void)bc7enc_compress_block(out, rgba, &params);
}

std::vector<std::uint8_t> write_dds(const Document& document, const WriteOptions& options,
                                    std::vector<std::string>* notices) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty document as a DDS texture"));
  }
  const PixelBuffer flat = flatten_document_rgba8(document);
  if (flat.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty document as a DDS texture"));
  }
  if (flat.width() > kMaxSide || flat.height() > kMaxSide) {
    throw std::runtime_error("DDS texture would be " + std::to_string(flat.width()) + "x" +
                             std::to_string(flat.height()) + ", larger than the supported size");
  }

  bool any_translucent = false;
  std::uint64_t partial_alpha_pixels = 0;
  {
    const auto data = flat.data();
    for (std::size_t offset = 3; offset < data.size(); offset += 4) {
      const auto alpha = data[offset];
      any_translucent = any_translucent || alpha != 255;
      partial_alpha_pixels += (alpha != 0 && alpha != 255) ? 1U : 0U;
    }
  }
  auto compression = options.compression;
  if (compression == Compression::Automatic) {
    compression = any_translucent ? Compression::Bc3 : Compression::Bc1;
  }
  if (compression == Compression::Bc1 && partial_alpha_pixels > 0 && notices != nullptr) {
    notices->push_back("BC1 keeps only 1-bit transparency: " + std::to_string(partial_alpha_pixels) +
                       " partially transparent pixels were cut out at 50 percent");
  }
  if (notices != nullptr && (compression == Compression::Bc4 || compression == Compression::Bc5)) {
    bool colourful = false;
    bool has_blue = false;
    const auto data = flat.data();
    for (std::size_t offset = 0; offset + 3 < data.size(); offset += 4) {
      colourful = colourful || data[offset] != data[offset + 1] || data[offset + 1] != data[offset + 2];
      has_blue = has_blue || data[offset + 2] != 0;
    }
    if (compression == Compression::Bc4 && (colourful || any_translucent)) {
      notices->push_back(PATCHY_TRANSLATE_NOOP("QObject", "BC4 keeps one channel: the image was saved as its grayscale luminance without transparency"));
    }
    if (compression == Compression::Bc5 && (has_blue || any_translucent)) {
      notices->push_back(PATCHY_TRANSLATE_NOOP("QObject", "BC5 keeps the red and green channels only: blue and transparency were dropped"));
    }
  }

  std::vector<PixelBuffer> levels;
  if (options.generate_mipmaps) {
    levels = generate_mip_chain(flat);
  } else {
    levels.push_back(flat);
  }

  std::vector<std::uint8_t> payload;
  for (const auto& level : levels) {
    if (compression == Compression::Uncompressed) {
      append_uncompressed_level(payload, level);
    } else {
      append_block_level(payload, level, compression);
    }
  }

  const auto width = static_cast<std::uint32_t>(flat.width());
  const auto height = static_cast<std::uint32_t>(flat.height());
  const auto level_count = static_cast<std::uint32_t>(levels.size());
  std::uint32_t flags = kFlagCaps | kFlagHeight | kFlagWidth | kFlagPixelFormat;
  std::uint32_t pitch_or_linear_size = 0;
  if (compression == Compression::Uncompressed) {
    flags |= kFlagPitch;
    pitch_or_linear_size = width * 4U;
  } else {
    flags |= kFlagLinearSize;
    pitch_or_linear_size = ((width + 3U) / 4U) * ((height + 3U) / 4U) * written_block_bytes(compression);
  }
  std::uint32_t caps = kCapsTexture;
  if (level_count > 1) {
    flags |= kFlagMipmapCount;
    caps |= kCapsComplex | kCapsMipmap;
  }

  LittleEndianWriter writer;
  writer.write_u32(kMagic);
  writer.write_u32(static_cast<std::uint32_t>(kHeaderSize));
  writer.write_u32(flags);
  writer.write_u32(height);
  writer.write_u32(width);
  writer.write_u32(pitch_or_linear_size);
  writer.write_u32(0);  // depth
  writer.write_u32(level_count > 1 ? level_count : 0U);
  for (int index = 0; index < 11; ++index) {
    writer.write_u32(0);  // dwReserved1
  }
  writer.write_u32(static_cast<std::uint32_t>(kPixelFormatSize));
  if (compression == Compression::Uncompressed) {
    writer.write_u32(kPixelFormatRgb | kPixelFormatAlphaPixels);
    writer.write_u32(0);   // no FourCC
    writer.write_u32(32);  // A8R8G8B8
    writer.write_u32(0x00ff0000U);
    writer.write_u32(0x0000ff00U);
    writer.write_u32(0x000000ffU);
    writer.write_u32(0xff000000U);
  } else {
    writer.write_u32(kPixelFormatFourCc);
    std::uint32_t code = kFourCcDxt1;
    switch (compression) {
      case Compression::Bc3:
        code = kFourCcDxt5;
        break;
      case Compression::Bc4:
        code = kFourCcAti1;  // the legacy spelling every reader knows (texconv, Pillow, the old plug-ins)
        break;
      case Compression::Bc5:
        code = kFourCcAti2;
        break;
      case Compression::Bc7:
        code = kFourCcDx10;  // BC7 exists only under a DX10 header
        break;
      case Compression::Bc1:
      case Compression::Automatic:
      case Compression::Uncompressed:
        break;
    }
    writer.write_u32(code);
    for (int index = 0; index < 5; ++index) {
      writer.write_u32(0);
    }
  }
  writer.write_u32(caps);
  writer.write_u32(0);  // caps2
  writer.write_u32(0);  // caps3
  writer.write_u32(0);  // caps4
  writer.write_u32(0);  // reserved2
  if (compression == Compression::Bc7) {
    writer.write_u32(kDxgiBc7Unorm);
    writer.write_u32(kResourceDimensionTexture2D);
    writer.write_u32(0);  // miscFlag
    writer.write_u32(1);  // arraySize
    writer.write_u32(0);  // miscFlags2: alpha mode unknown, the way texconv writes it
  }
  writer.write_bytes(payload);
  return std::move(writer.bytes());
}

std::vector<std::uint8_t> write_dds(const Document& document) {
  return write_dds(document, WriteOptions{});
}

void write_dds_file(const Document& document, const std::filesystem::path& path, const WriteOptions& options,
                    std::vector<std::string>* notices) {
  formats::write_file_bytes(path, write_dds(document, options, notices), "DDS");
}

std::string_view mipmap_choice_token(MipmapChoice choice) noexcept {
  switch (choice) {
    case MipmapChoice::Generate:
      return "on";
    case MipmapChoice::None:
      return "off";
    case MipmapChoice::Automatic:
      break;
  }
  return "auto";
}

std::optional<MipmapChoice> mipmap_choice_from_token(std::string_view token) noexcept {
  if (token == "auto") {
    return MipmapChoice::Automatic;
  }
  if (token == "on") {
    return MipmapChoice::Generate;
  }
  if (token == "off") {
    return MipmapChoice::None;
  }
  return std::nullopt;
}

SourceShape source_shape_from_metadata(const std::map<std::string, std::string>& metadata) {
  SourceShape shape;
  if (const auto found = metadata.find(kMetadataCompression); found != metadata.end()) {
    const auto compression = compression_from_token(found->second);
    if (compression.has_value() && *compression != Compression::Automatic) {
      shape.compression = compression;
    }
  }
  if (const auto found = metadata.find(kMetadataMipmaps); found != metadata.end()) {
    shape.mipmaps = found->second == "1";
  }
  return shape;
}

Compression resolve_compression(Compression choice, const SourceShape& source) noexcept {
  if (choice == Compression::Automatic && source.compression.has_value() &&
      *source.compression != Compression::Automatic) {
    return *source.compression;
  }
  return choice;
}

bool resolve_mipmaps(MipmapChoice choice, const SourceShape& source) noexcept {
  switch (choice) {
    case MipmapChoice::Generate:
      return true;
    case MipmapChoice::None:
      return false;
    case MipmapChoice::Automatic:
      break;
  }
  return source.mipmaps.value_or(true);
}

Preview preview_levels(const Document& document, Compression compression, bool mipmaps) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty document as a DDS texture"));
  }
  const PixelBuffer flat = flatten_document_rgba8(document);
  if (flat.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot write an empty document as a DDS texture"));
  }
  Preview preview;
  preview.compression = compression;
  if (preview.compression == Compression::Automatic) {
    // write_dds's rule: BC1 for an opaque image, BC3 as soon as any alpha is below 255.
    bool any_translucent = false;
    const auto data = flat.data();
    for (std::size_t offset = 3; offset < data.size() && !any_translucent; offset += 4) {
      any_translucent = data[offset] != 255;
    }
    preview.compression = any_translucent ? Compression::Bc3 : Compression::Bc1;
  }
  std::vector<PixelBuffer> levels;
  if (mipmaps) {
    levels = generate_mip_chain(flat);
  } else {
    levels.push_back(flat);
  }
  preview.levels.reserve(levels.size());
  std::vector<std::uint8_t> payload;
  for (const auto& level : levels) {
    payload.clear();
    if (preview.compression == Compression::Uncompressed) {
      append_uncompressed_level(payload, level);
    } else {
      append_block_level(payload, level, preview.compression);
    }
    PreviewLevel entry;
    entry.byte_size = payload.size();
    entry.rgba8 = decode_written_level(payload, level.width(), level.height(), preview.compression);
    preview.levels.push_back(std::move(entry));
  }
  return preview;
}

}  // namespace patchy::dds
