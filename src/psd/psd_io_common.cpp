// Shared plumbing for the psd_*.cpp split of the PSD codec: byte-level
// read/write helpers, string codecs, the blend-mode key maps, and the generic
// descriptor-writing primitives. Declarations live in psd_io_internal.hpp.

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

// Tagged-block keys Photoshop stores with the '8B64' signature and an 8-byte length in
// PSB files. The spec documents the first thirteen; 'cinf' was pinned empirically (July
// 2026): Photoshop 2026 writes it 8B64+u64 in PSBs and its parser expects that width by
// KEY, so a PSB carrying a narrow 'cinf' fails to open ("open options are incorrect").
// The reader never consults this list — it trusts each block's own signature — but the
// writer uses it to upgrade blocks (authored, or preserved from a PSD) on PSB saves.
[[nodiscard]] bool tagged_block_length_is_u64(std::string_view key) noexcept {
  return key == "LMsk" || key == "Lr16" || key == "Lr32" || key == "Layr" || key == "Mt16" ||
         key == "Mt32" || key == "Mtrn" || key == "Alph" || key == "FMsk" || key == "lnk2" ||
         key == "FEid" || key == "FXid" || key == "PxSD" || key == "cinf";
}

std::uint32_t checked_u32(std::size_t value, const char* field) {
  if (value > 0xFFFFFFFFULL) {
    throw std::runtime_error(std::string("PSD field is too large: ") + field);
  }
  return static_cast<std::uint32_t>(value);
}

std::uint16_t checked_u16(std::size_t value, const char* field) {
  if (value > 0xFFFFULL) {
    throw std::runtime_error(std::string("PSD field is too large: ") + field);
  }
  return static_cast<std::uint16_t>(value);
}

void check_write_dimensions(const Document& document, bool large_document) {
  const auto limit = large_document ? kMaxPsbDimension : kMaxPsdDimension;
  if (document.width() > limit || document.height() > limit) {
    throw std::runtime_error(large_document
                                 ? "PSB documents are limited to 300,000 pixels per side"
                                 : "Documents over 30,000 pixels per side must be saved as PSB (.psb)");
  }
}

void skip_length_block(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  reader.skip(length);
}

std::vector<std::uint8_t> read_length_block(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  return reader.read_bytes(length);
}

PixelFormat format_from_header(const Header& header) {
  // 16- and 32-bit files decode by converting every channel to 8-bit at read time
  // (Patchy's pixel pipeline is 8-bit only), so the returned format is always 8-bit.
  if (header.depth != 8 && header.depth != 16 && header.depth != 32) {
    throw std::runtime_error("The starter PSD reader currently supports 8, 16, and 32-bit files only");
  }
  if (header.color_mode != kColorModeRgb && header.color_mode != kColorModeCmyk) {
    throw std::runtime_error("The starter PSD reader currently supports RGB and CMYK files only");
  }
  if (header.channels > kMaximumPhotoshopChannelCount) {
    throw std::runtime_error("PSD files cannot contain more than 56 channels");
  }
  if (header.color_mode == kColorModeRgb && header.channels < 3) {
    throw std::runtime_error("RGB PSD file must contain at least 3 channels");
  }
  if (header.color_mode == kColorModeCmyk && header.channels < 4) {
    throw std::runtime_error("CMYK PSD file must contain at least 4 channels");
  }
  return PixelFormat::rgb8();
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PSD file for reading");
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}

void write_file_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open PSD file for writing");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool is_source_color_channel(std::uint16_t channel_id, std::uint16_t source_color_mode) noexcept {
  return is_cmyk_color_mode(source_color_mode) ? channel_id <= kChannelBlack : channel_id <= kChannelBlue;
}

std::uint32_t read_section_length(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  return length;
}

// PSB widens a few section lengths (layer-and-mask info, layer info, per-layer channel
// data) to 8 bytes; large_document selects the width at each such call site.
std::uint64_t read_section_length_u64(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u64();
  if (length > reader.remaining()) {
    throw std::runtime_error(std::string("Invalid PSD ") + section_name + " length");
  }
  return length;
}

std::uint32_t write_length_prefixed_block(BigEndianWriter& writer, const std::vector<std::uint8_t>& payload) {
  writer.write_u32(checked_u32(payload.size(), "section length"));
  writer.write_bytes(payload);
  return static_cast<std::uint32_t>(payload.size());
}

void write_signature(BigEndianWriter& writer, const std::array<char, 4>& signature) {
  for (const auto ch : signature) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
}

std::vector<std::uint16_t> utf8_to_utf16(std::string_view text) {
  std::vector<std::uint16_t> units;
  units.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = 0x3FU;
    std::size_t consumed = 1;
    if (lead < 0x80U) {
      codepoint = lead;
    } else if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) {
      codepoint = ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
      consumed = 2;
    } else if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) {
      codepoint = ((lead & 0x0FU) << 12U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
      consumed = 3;
    } else if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) {
      codepoint = ((lead & 0x07U) << 18U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U) |
                  ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
      consumed = 4;
    }

    if (codepoint <= 0xFFFFU) {
      units.push_back(static_cast<std::uint16_t>(codepoint));
    } else {
      codepoint -= 0x10000U;
      units.push_back(static_cast<std::uint16_t>(0xD800U + (codepoint >> 10U)));
      units.push_back(static_cast<std::uint16_t>(0xDC00U + (codepoint & 0x3FFU)));
    }
    index += consumed;
  }
  return units;
}

std::optional<std::string> read_unicode_string_payload(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4) {
    return std::nullopt;
  }
  BigEndianReader reader(payload);
  const auto code_unit_count = reader.read_u32();
  if (code_unit_count > reader.remaining() / 2U) {
    return std::nullopt;
  }

  std::string decoded;
  for (std::uint32_t index = 0; index < code_unit_count; ++index) {
    auto codepoint = static_cast<std::uint32_t>(reader.read_u16());
    if (codepoint == 0) {
      continue;
    }
    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU && index + 1 < code_unit_count) {
      const auto low = static_cast<std::uint32_t>(reader.read_u16());
      ++index;
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
      } else {
        codepoint = '?';
      }
    }
    append_utf8(decoded, codepoint);
  }
  if (decoded.empty()) {
    return std::nullopt;
  }
  return decoded;
}

std::vector<std::uint8_t> unicode_string_payload(std::string_view text) {
  const auto units = utf8_to_utf16(text);
  BigEndianWriter writer;
  writer.write_u32(checked_u32(units.size(), "unicode string length"));
  for (const auto unit : units) {
    writer.write_u16(unit);
  }
  return writer.bytes();
}

std::array<char, 4> blend_mode_key(BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
      return {'p', 'a', 's', 's'};
    case BlendMode::Normal:
      return {'n', 'o', 'r', 'm'};
    case BlendMode::Multiply:
      return {'m', 'u', 'l', ' '};
    case BlendMode::Screen:
      return {'s', 'c', 'r', 'n'};
    case BlendMode::Overlay:
      return {'o', 'v', 'e', 'r'};
    case BlendMode::Darken:
      return {'d', 'a', 'r', 'k'};
    case BlendMode::Lighten:
      return {'l', 'i', 't', 'e'};
    case BlendMode::ColorDodge:
      return {'d', 'i', 'v', ' '};
    case BlendMode::ColorBurn:
      return {'i', 'd', 'i', 'v'};
    case BlendMode::HardLight:
      return {'h', 'L', 'i', 't'};
    case BlendMode::SoftLight:
      return {'s', 'L', 'i', 't'};
    case BlendMode::Difference:
      return {'d', 'i', 'f', 'f'};
    case BlendMode::LinearBurn:
      return {'l', 'b', 'r', 'n'};
    case BlendMode::PinLight:
      return {'p', 'L', 'i', 't'};
    case BlendMode::Saturation:
      return {'s', 'a', 't', ' '};
    case BlendMode::Luminosity:
      return {'l', 'u', 'm', ' '};
    case BlendMode::Exclusion:
      return {'s', 'm', 'u', 'd'};
    case BlendMode::Hue:
      return {'h', 'u', 'e', ' '};
    case BlendMode::Color:
      return {'c', 'o', 'l', 'r'};
    case BlendMode::LinearDodge:
      return {'l', 'd', 'd', 'g'};
    case BlendMode::Subtract:
      return {'f', 's', 'u', 'b'};
    case BlendMode::Divide:
      return {'f', 'd', 'i', 'v'};
    case BlendMode::VividLight:
      return {'v', 'L', 'i', 't'};
    case BlendMode::LinearLight:
      return {'l', 'L', 'i', 't'};
    case BlendMode::HardMix:
      return {'h', 'M', 'i', 'x'};
    case BlendMode::DarkerColor:
      return {'d', 'k', 'C', 'l'};
    case BlendMode::LighterColor:
      return {'l', 'g', 'C', 'l'};
  }
  return {'n', 'o', 'r', 'm'};
}

BlendMode blend_mode_from_key(const std::array<char, 4>& key) {
  if (key == std::array<char, 4>{'N', 'r', 'm', 'l'}) {
    return BlendMode::Normal;
  }
  if (key == std::array<char, 4>{'m', 'u', 'l', ' '}) {
    return BlendMode::Multiply;
  }
  if (key == std::array<char, 4>{'M', 'l', 't', 'p'}) {
    return BlendMode::Multiply;
  }
  if (key == std::array<char, 4>{'s', 'c', 'r', 'n'}) {
    return BlendMode::Screen;
  }
  if (key == std::array<char, 4>{'S', 'c', 'r', 'n'}) {
    return BlendMode::Screen;
  }
  if (key == std::array<char, 4>{'o', 'v', 'e', 'r'}) {
    return BlendMode::Overlay;
  }
  if (key == std::array<char, 4>{'O', 'v', 'r', 'l'}) {
    return BlendMode::Overlay;
  }
  if (key == std::array<char, 4>{'d', 'a', 'r', 'k'}) {
    return BlendMode::Darken;
  }
  if (key == std::array<char, 4>{'l', 'i', 't', 'e'}) {
    return BlendMode::Lighten;
  }
  if (key == std::array<char, 4>{'d', 'i', 'v', ' '}) {
    return BlendMode::ColorDodge;
  }
  if (key == std::array<char, 4>{'C', 'D', 'd', 'g'}) {
    return BlendMode::ColorDodge;
  }
  if (key == std::array<char, 4>{'i', 'd', 'i', 'v'}) {
    return BlendMode::ColorBurn;
  }
  if (key == std::array<char, 4>{'C', 'B', 'r', 'n'}) {
    return BlendMode::ColorBurn;
  }
  if (key == std::array<char, 4>{'h', 'L', 'i', 't'}) {
    return BlendMode::HardLight;
  }
  if (key == std::array<char, 4>{'s', 'L', 'i', 't'} || key == std::array<char, 4>{'S', 'f', 't', 'L'}) {
    return BlendMode::SoftLight;
  }
  if (key == std::array<char, 4>{'d', 'i', 'f', 'f'}) {
    return BlendMode::Difference;
  }
  if (key == std::array<char, 4>{'l', 'b', 'r', 'n'}) {
    return BlendMode::LinearBurn;
  }
  if (key == std::array<char, 4>{'p', 'L', 'i', 't'}) {
    return BlendMode::PinLight;
  }
  if (key == std::array<char, 4>{'s', 'a', 't', ' '}) {
    return BlendMode::Saturation;
  }
  if (key == std::array<char, 4>{'l', 'u', 'm', ' '}) {
    return BlendMode::Luminosity;
  }
  if (key == std::array<char, 4>{'p', 'a', 's', 's'}) {
    return BlendMode::PassThrough;
  }
  if (key == std::array<char, 4>{'s', 'm', 'u', 'd'}) {
    return BlendMode::Exclusion;
  }
  if (key == std::array<char, 4>{'h', 'u', 'e', ' '}) {
    return BlendMode::Hue;
  }
  if (key == std::array<char, 4>{'c', 'o', 'l', 'r'}) {
    return BlendMode::Color;
  }
  if (key == std::array<char, 4>{'l', 'd', 'd', 'g'}) {
    return BlendMode::LinearDodge;
  }
  if (key == std::array<char, 4>{'f', 's', 'u', 'b'}) {
    return BlendMode::Subtract;
  }
  if (key == std::array<char, 4>{'f', 'd', 'i', 'v'}) {
    return BlendMode::Divide;
  }
  if (key == std::array<char, 4>{'v', 'L', 'i', 't'}) {
    return BlendMode::VividLight;
  }
  if (key == std::array<char, 4>{'l', 'L', 'i', 't'}) {
    return BlendMode::LinearLight;
  }
  if (key == std::array<char, 4>{'h', 'M', 'i', 'x'}) {
    return BlendMode::HardMix;
  }
  if (key == std::array<char, 4>{'d', 'k', 'C', 'l'}) {
    return BlendMode::DarkerColor;
  }
  if (key == std::array<char, 4>{'l', 'g', 'C', 'l'}) {
    return BlendMode::LighterColor;
  }
  return BlendMode::Normal;
}

// Blend mode from an lfx2 descriptor 'BlnM' enum value. Modern Photoshop
// serializes these as full stringIDs ("multiply", "screen", ...); older files
// (including pre-2026 Patchy output) carry 4-char codes, which fall through to
// the legacy key mapping. Unknown values resolve through fallback_key.
BlendMode blend_mode_from_descriptor_enum(std::string_view value, const std::array<char, 4>& fallback_key) {
  if (value == "passThrough") {
    return BlendMode::PassThrough;
  }
  if (value == "normal") {
    return BlendMode::Normal;
  }
  if (value == "multiply") {
    return BlendMode::Multiply;
  }
  if (value == "screen") {
    return BlendMode::Screen;
  }
  if (value == "overlay") {
    return BlendMode::Overlay;
  }
  if (value == "darken") {
    return BlendMode::Darken;
  }
  if (value == "lighten") {
    return BlendMode::Lighten;
  }
  if (value == "colorDodge") {
    return BlendMode::ColorDodge;
  }
  if (value == "colorBurn") {
    return BlendMode::ColorBurn;
  }
  if (value == "hardLight") {
    return BlendMode::HardLight;
  }
  if (value == "softLight") {
    return BlendMode::SoftLight;
  }
  if (value == "difference") {
    return BlendMode::Difference;
  }
  if (value == "linearBurn") {
    return BlendMode::LinearBurn;
  }
  if (value == "pinLight") {
    return BlendMode::PinLight;
  }
  if (value == "saturation") {
    return BlendMode::Saturation;
  }
  if (value == "luminosity") {
    return BlendMode::Luminosity;
  }
  if (value == "exclusion") {
    return BlendMode::Exclusion;
  }
  if (value == "hue") {
    return BlendMode::Hue;
  }
  if (value == "color") {
    return BlendMode::Color;
  }
  if (value == "linearDodge") {
    return BlendMode::LinearDodge;
  }
  if (value == "blendSubtraction") {
    return BlendMode::Subtract;
  }
  if (value == "blendDivide") {
    return BlendMode::Divide;
  }
  if (value == "vividLight") {
    return BlendMode::VividLight;
  }
  if (value == "linearLight") {
    return BlendMode::LinearLight;
  }
  if (value == "hardMix") {
    return BlendMode::HardMix;
  }
  if (value == "darkerColor") {
    return BlendMode::DarkerColor;
  }
  if (value == "lighterColor") {
    return BlendMode::LighterColor;
  }
  return blend_mode_from_key(block_key_from_string(value).value_or(fallback_key));
}

std::string read_pascal_string(BigEndianReader& reader, std::size_t padded_multiple) {
  const auto start = reader.position();
  const auto length = reader.read_u8();
  auto bytes = reader.read_bytes(length);
  const auto consumed = reader.position() - start;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  if (padded > consumed) {
    reader.skip(padded - consumed);
  }
  return std::string(bytes.begin(), bytes.end());
}

void write_pascal_string(BigEndianWriter& writer, const std::string& value, std::size_t padded_multiple) {
  const auto length = std::min<std::size_t>(value.size(), 255);
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), length));
  const auto consumed = 1 + length;
  const auto padded = ((consumed + padded_multiple - 1) / padded_multiple) * padded_multiple;
  for (std::size_t i = consumed; i < padded; ++i) {
    writer.write_u8(0);
  }
}

std::optional<std::array<char, 4>> block_key_from_string(std::string_view key) {
  if (key.size() != 4U) {
    return std::nullopt;
  }
  return std::array<char, 4>{key[0], key[1], key[2], key[3]};
}

// force_wide carries a preserved block's original 8B64 form; PSD saves always downgrade
// to the narrow form (PSB-only widths cannot appear in a version-1 file).
void write_additional_layer_block(BigEndianWriter& writer, const std::array<char, 4>& key,
                                  std::span<const std::uint8_t> payload, bool large_document,
                                  bool force_wide) {
  const bool wide_length =
      large_document && (force_wide || tagged_block_length_is_u64(std::string_view(key.data(), key.size())));
  write_signature(writer, wide_length ? std::array<char, 4>{'8', 'B', '6', '4'}
                                      : std::array<char, 4>{'8', 'B', 'I', 'M'});
  write_signature(writer, key);
  if (wide_length) {
    writer.write_u64(payload.size());
  } else {
    writer.write_u32(checked_u32(payload.size(), "additional layer block length"));
  }
  writer.write_bytes(payload);
}

#ifdef _WIN32
std::wstring wide_from_utf8(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const auto input_size = static_cast<int>(std::min<std::size_t>(text.size(), static_cast<std::size_t>(INT_MAX)));
  const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
  if (wide_size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, wide.data(), wide_size) !=
      wide_size) {
    return {};
  }
  return wide;
}

std::string utf8_from_wide(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const auto input_size = static_cast<int>(std::min<std::size_t>(text.size(), static_cast<std::size_t>(INT_MAX)));
  const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, text.data(), input_size, nullptr, 0, nullptr, nullptr);
  if (utf8_size <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, text.data(), input_size, utf8.data(), utf8_size, nullptr, nullptr) !=
      utf8_size) {
    return {};
  }
  return utf8;
}
#endif

// write_descriptor_unicode_string / write_descriptor_id moved to psd_descriptor.{hpp,cpp}
// (identical byte behavior), where the generic write_descriptor also lives.

void write_descriptor_item_header(BigEndianWriter& writer, std::string_view key, const std::array<char, 4>& type) {
  write_descriptor_id(writer, key);
  write_signature(writer, type);
}

void write_descriptor_enum_item(BigEndianWriter& writer, std::string_view key, std::string_view enum_type,
                                std::string_view enum_value) {
  write_descriptor_item_header(writer, key, {'e', 'n', 'u', 'm'});
  write_descriptor_id(writer, enum_type);
  write_descriptor_id(writer, enum_value);
}

void write_descriptor_bool_item(BigEndianWriter& writer, std::string_view key, bool value) {
  write_descriptor_item_header(writer, key, {'b', 'o', 'o', 'l'});
  writer.write_u8(value ? 1U : 0U);
}

void write_descriptor_long_item(BigEndianWriter& writer, std::string_view key, std::int32_t value) {
  write_descriptor_item_header(writer, key, {'l', 'o', 'n', 'g'});
  writer.write_u32(static_cast<std::uint32_t>(value));
}

void write_descriptor_double_item(BigEndianWriter& writer, std::string_view key, double value) {
  write_descriptor_item_header(writer, key, {'d', 'o', 'u', 'b'});
  write_f64(writer, value);
}

void write_descriptor_unit_float_item(BigEndianWriter& writer, std::string_view key, const std::array<char, 4>& unit,
                                      double value) {
  write_descriptor_item_header(writer, key, {'U', 'n', 't', 'F'});
  write_signature(writer, unit);
  write_f64(writer, value);
}

void write_descriptor_unit_float_item(BigEndianWriter& writer, std::string_view key, double value) {
  write_descriptor_unit_float_item(writer, key, {'#', 'P', 'n', 't'}, value);
}

void write_descriptor_object_header(BigEndianWriter& writer, std::string_view name, std::string_view class_id,
                                    std::uint32_t item_count) {
  write_descriptor_unicode_string(writer, name);
  write_descriptor_id(writer, class_id);
  writer.write_u32(item_count);
}

void write_descriptor_raw_item(BigEndianWriter& writer, std::string_view key, std::span<const std::uint8_t> payload) {
  write_descriptor_item_header(writer, key, {'t', 'd', 't', 'a'});
  writer.write_u32(checked_u32(payload.size(), "descriptor raw data"));
  writer.write_bytes(payload);
}

namespace {

void write_bounds_descriptor(BigEndianWriter& writer, double left, double top, double right, double bottom) {
  write_descriptor_unicode_string(writer, "");
  write_descriptor_id(writer, "bounds");
  writer.write_u32(4);
  write_descriptor_unit_float_item(writer, "Left", left);
  write_descriptor_unit_float_item(writer, "Top ", top);
  write_descriptor_unit_float_item(writer, "Rght", right);
  write_descriptor_unit_float_item(writer, "Btom", bottom);
}

void write_bounds_descriptor(BigEndianWriter& writer, const PsdTextBoundsD& bounds) {
  write_bounds_descriptor(writer, bounds.left, bounds.top, bounds.right, bounds.bottom);
}

}  // namespace

void write_descriptor_object_item(BigEndianWriter& writer, std::string_view key, double left, double top,
                                  double right, double bottom) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_bounds_descriptor(writer, left, top, right, bottom);
}

void write_descriptor_object_item(BigEndianWriter& writer, std::string_view key, const PsdTextBoundsD& bounds) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_bounds_descriptor(writer, bounds);
}

void write_descriptor_text_item(BigEndianWriter& writer, std::string_view key, std::string_view text) {
  write_descriptor_item_header(writer, key, {'T', 'E', 'X', 'T'});
  write_descriptor_unicode_string(writer, text);
}

}  // namespace patchy::psd
