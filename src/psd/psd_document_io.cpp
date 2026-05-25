#include "psd/psd_document_io.hpp"

#include "psd/psd_binary.hpp"
#include "render/compositor.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace photoslop::psd {

namespace {

constexpr std::uint16_t kColorModeRgb = 3;
constexpr std::uint16_t kCompressionRaw = 0;
constexpr std::uint16_t kCompressionRle = 1;
constexpr std::uint16_t kChannelRed = 0;
constexpr std::uint16_t kChannelGreen = 1;
constexpr std::uint16_t kChannelBlue = 2;
constexpr std::uint16_t kChannelTransparency = 0xFFFFU;
constexpr std::uint16_t kImageResourceIccProfile = 1039;

struct LayerChannelInfo {
  std::uint16_t id{0};
  std::uint32_t length{0};
};

struct LayerRecord {
  Rect bounds;
  std::vector<LayerChannelInfo> channels;
  BlendMode blend_mode{BlendMode::Normal};
  std::uint8_t opacity{255};
  bool visible{true};
  std::string name;
  std::uint32_t section_divider_type{0};
  std::vector<UnknownPsdBlock> additional_blocks;
  std::optional<std::string> text;
  std::optional<int> text_size;
  LayerStyle layer_style;
};

struct DecodedLayer {
  Layer layer;
  std::uint32_t section_divider_type{0};
};

enum class EncodedLayerKind {
  Pixel,
  Group,
  GroupBoundary
};

struct EncodedLayer {
  const Layer* layer{nullptr};
  EncodedLayerKind kind{EncodedLayerKind::Pixel};
  Rect bounds;
  std::vector<std::uint16_t> channel_ids;
  std::vector<std::vector<std::uint8_t>> channel_data;
};

struct ImageResource {
  std::array<char, 4> signature{'8', 'B', 'I', 'M'};
  std::uint16_t id{0};
  std::string name;
  std::vector<std::uint8_t> payload;
};

struct DescriptorObject;

struct DescriptorValue {
  enum class Type {
    Empty,
    Bool,
    Integer,
    Double,
    UnitFloat,
    String,
    Enum,
    Object,
    List,
    Raw
  };

  Type type{Type::Empty};
  bool bool_value{false};
  std::int32_t integer_value{0};
  double double_value{0.0};
  std::string unit;
  std::string string_value;
  std::string enum_type;
  std::string enum_value;
  std::shared_ptr<DescriptorObject> object_value;
  std::vector<DescriptorValue> list_value;
  std::vector<std::uint8_t> raw_value;
};

struct DescriptorObject {
  std::string name;
  std::string class_id;
  std::map<std::string, DescriptorValue> values;
};

std::uint32_t checked_u32(std::size_t value, const char* field) {
  if (value > 0xFFFFFFFFULL) {
    throw std::runtime_error(std::string("PSD field is too large: ") + field);
  }
  return static_cast<std::uint32_t>(value);
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
  if (header.large_document) {
    throw std::runtime_error("The starter PSD reader does not yet support PSB length fields");
  }
  if (header.depth != 8) {
    throw std::runtime_error("The starter PSD reader currently supports 8-bit files only");
  }
  if (header.color_mode != kColorModeRgb) {
    throw std::runtime_error("The starter PSD reader currently supports RGB files only");
  }
  if (header.channels < 3) {
    throw std::runtime_error("RGB PSD file must contain at least 3 channels");
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

std::vector<std::uint8_t> decode_packbits(std::span<const std::uint8_t> encoded, std::size_t expected_size) {
  std::vector<std::uint8_t> decoded;
  decoded.reserve(expected_size);
  std::size_t cursor = 0;
  while (cursor < encoded.size() && decoded.size() < expected_size) {
    const auto header = static_cast<std::int8_t>(encoded[cursor++]);
    if (header >= 0) {
      const auto count = static_cast<std::size_t>(header) + 1U;
      if (cursor + count > encoded.size()) {
        throw std::runtime_error("PSD PackBits literal run is truncated");
      }
      decoded.insert(decoded.end(), encoded.begin() + static_cast<std::ptrdiff_t>(cursor),
                     encoded.begin() + static_cast<std::ptrdiff_t>(cursor + count));
      cursor += count;
    } else if (header != -128) {
      const auto count = static_cast<std::size_t>(1 - header);
      if (cursor >= encoded.size()) {
        throw std::runtime_error("PSD PackBits repeat run is truncated");
      }
      decoded.insert(decoded.end(), count, encoded[cursor++]);
    }
  }

  if (decoded.size() != expected_size) {
    throw std::runtime_error("PSD PackBits row decoded to the wrong length");
  }
  return decoded;
}

std::vector<std::uint8_t> read_channel_data(BigEndianReader& reader, std::uint16_t compression, std::int32_t width,
                                            std::int32_t height) {
  if (compression == kCompressionRaw) {
    const auto byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return reader.read_bytes(byte_count);
  }

  if (compression != kCompressionRle) {
    throw std::runtime_error("Unsupported PSD channel compression");
  }

  std::vector<std::uint16_t> row_lengths;
  row_lengths.reserve(static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    row_lengths.push_back(reader.read_u16());
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

std::vector<std::uint8_t> read_rle_channel_from_counts(BigEndianReader& reader,
                                                       std::span<const std::uint16_t> row_lengths,
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


std::uint32_t read_section_length(BigEndianReader& reader, const char* section_name) {
  const auto length = reader.read_u32();
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

std::array<char, 4> read_signature(BigEndianReader& reader) {
  const auto bytes = reader.read_bytes(4);
  return {static_cast<char>(bytes[0]), static_cast<char>(bytes[1]), static_cast<char>(bytes[2]),
          static_cast<char>(bytes[3])};
}

std::string key_string(const std::array<char, 4>& key) {
  return std::string(key.begin(), key.end());
}

std::vector<std::uint8_t> unescape_engine_bytes(std::span<const std::uint8_t> bytes) {
  std::vector<std::uint8_t> unescaped;
  unescaped.reserve(bytes.size());
  bool escaped = false;
  for (const auto byte : bytes) {
    const auto ch = static_cast<char>(byte);
    if (escaped) {
      switch (ch) {
        case 'r':
          unescaped.push_back('\n');
          break;
        case 'n':
          unescaped.push_back('\n');
          break;
        case 't':
          unescaped.push_back('\t');
          break;
        case '\\':
        case '(':
        case ')':
          unescaped.push_back(byte);
          break;
        default:
          unescaped.push_back(byte);
          break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    unescaped.push_back(byte);
  }
  return unescaped;
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
  if (codepoint == '\r') {
    return;
  }
  if (codepoint <= 0x7FU) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FFU) {
    output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else if (codepoint <= 0xFFFFU) {
    output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else {
    output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
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

std::string decode_engine_string(std::span<const std::uint8_t> bytes) {
  const auto unescaped = unescape_engine_bytes(bytes);
  if (unescaped.size() >= 2 && unescaped[0] == 0xFEU && unescaped[1] == 0xFFU) {
    std::string decoded;
    for (std::size_t index = 2; index + 1 < unescaped.size(); index += 2) {
      const auto codepoint = (static_cast<std::uint32_t>(unescaped[index]) << 8U) |
                             static_cast<std::uint32_t>(unescaped[index + 1]);
      append_utf8(decoded, codepoint);
    }
    return decoded;
  }
  if (unescaped.size() >= 2 && unescaped[0] == 0xFFU && unescaped[1] == 0xFEU) {
    std::string decoded;
    for (std::size_t index = 2; index + 1 < unescaped.size(); index += 2) {
      const auto codepoint = (static_cast<std::uint32_t>(unescaped[index + 1]) << 8U) |
                             static_cast<std::uint32_t>(unescaped[index]);
      append_utf8(decoded, codepoint);
    }
    return decoded;
  }
  return std::string(unescaped.begin(), unescaped.end());
}

std::optional<std::string> extract_engine_data_text(std::span<const std::uint8_t> payload) {
  constexpr std::string_view marker = "/Text";
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  auto found = std::search(begin, end, marker.begin(), marker.end());
  while (found != end) {
    auto cursor = found + static_cast<std::ptrdiff_t>(marker.size());
    while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
      ++cursor;
    }
    if (cursor < end && *cursor == '(') {
      ++cursor;
      const auto text_begin = cursor;
      int depth = 1;
      bool escaped = false;
      while (cursor < end && depth > 0) {
        const auto ch = *cursor;
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '(') {
          ++depth;
        } else if (ch == ')') {
          --depth;
          if (depth == 0) {
            break;
          }
        }
        ++cursor;
      }
      if (cursor > text_begin) {
        auto text =
            decode_engine_string(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text_begin),
                                                               static_cast<std::size_t>(cursor - text_begin)));
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
          text.pop_back();
        }
        if (!text.empty()) {
          return text;
        }
      }
    }
    found = std::search(cursor, end, marker.begin(), marker.end());
  }
  return std::nullopt;
}

std::optional<int> extract_engine_data_font_size(std::span<const std::uint8_t> payload) {
  constexpr std::string_view marker = "/FontSize";
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  auto found = std::search(begin, end, marker.begin(), marker.end());
  while (found != end) {
    auto cursor = found + static_cast<std::ptrdiff_t>(marker.size());
    while (cursor < end &&
           (std::isspace(static_cast<unsigned char>(*cursor)) != 0 || *cursor == '[' || *cursor == '(')) {
      ++cursor;
    }
    if (cursor < end) {
      const auto remaining = static_cast<std::size_t>(end - cursor);
      const std::string number(cursor, cursor + std::min<std::size_t>(remaining, 48U));
      char* parsed_end = nullptr;
      const auto parsed = std::strtod(number.c_str(), &parsed_end);
      if (parsed_end != number.c_str() && std::isfinite(parsed) && parsed > 0.0) {
        return std::clamp(static_cast<int>(std::lround(parsed)), 1, 300);
      }
    }
    found = std::search(cursor, end, marker.begin(), marker.end());
  }
  return std::nullopt;
}

const std::array<std::uint8_t, 7>* glyph_for(char ch) {
  static const std::map<char, std::array<std::uint8_t, 7>> glyphs = {
      {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
      {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
      {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
      {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
      {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
      {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
      {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
      {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
      {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
      {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
      {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
      {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
      {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
      {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
      {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
      {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
      {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
      {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
      {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
      {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
      {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
      {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
      {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
      {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
      {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
      {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
      {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
      {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
      {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
      {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
      {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
      {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
      {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
      {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
      {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
      {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
      {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
      {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
      {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
      {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
      {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
  };
  const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  const auto found = glyphs.find(upper);
  return found == glyphs.end() ? nullptr : &found->second;
}

PixelBuffer render_placeholder_text(std::string_view text, std::int32_t width, std::int32_t height) {
  width = std::max<std::int32_t>(width, static_cast<std::int32_t>(std::min<std::size_t>(text.size(), 64) * 14U + 8U));
  height = std::max<std::int32_t>(height, 28);
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  pixels.clear(0);

  constexpr int scale = 2;
  constexpr int glyph_width = 5;
  constexpr int glyph_height = 7;
  const int advance = (glyph_width + 1) * scale;
  int cursor_x = 2;
  int cursor_y = 4;
  for (const auto ch : text) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      cursor_x = 2;
      cursor_y += (glyph_height + 2) * scale;
      continue;
    }
    if (ch == ' ') {
      cursor_x += advance;
      continue;
    }
    if (cursor_x + glyph_width * scale >= width) {
      cursor_x = 2;
      cursor_y += (glyph_height + 2) * scale;
    }
    if (cursor_y + glyph_height * scale >= height) {
      break;
    }

    const auto* glyph = glyph_for(ch);
    if (glyph != nullptr) {
      for (int gy = 0; gy < glyph_height; ++gy) {
        for (int gx = 0; gx < glyph_width; ++gx) {
          if (((*glyph)[static_cast<std::size_t>(gy)] & (1U << (glyph_width - 1 - gx))) == 0U) {
            continue;
          }
          for (int sy = 0; sy < scale; ++sy) {
            for (int sx = 0; sx < scale; ++sx) {
              const auto x = cursor_x + gx * scale + sx;
              const auto y = cursor_y + gy * scale + sy;
              if (x >= 0 && y >= 0 && x < width && y < height) {
                auto* px = pixels.pixel(x, y);
                px[0] = 0;
                px[1] = 0;
                px[2] = 0;
                px[3] = 255;
              }
            }
          }
        }
      }
    }
    cursor_x += advance;
  }
  return pixels;
}

bool has_visible_alpha(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (pixels.format().channels < 4) {
    for (const auto byte : pixels.data()) {
      if (byte != 0U) {
        return true;
      }
    }
    return false;
  }
  for (std::size_t offset = 3; offset < pixels.data().size(); offset += pixels.format().channels) {
    if (pixels.data()[offset] != 0U) {
      return true;
    }
  }
  return false;
}

int estimate_text_size_from_alpha(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 4) {
    return 48;
  }

  std::vector<int> visible_runs;
  bool in_run = false;
  int run_start = 0;
  int blank_rows = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    bool row_has_ink = false;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] >= 8) {
        row_has_ink = true;
        break;
      }
    }

    if (row_has_ink) {
      if (!in_run) {
        in_run = true;
        run_start = y;
      }
      blank_rows = 0;
      continue;
    }

    if (in_run) {
      ++blank_rows;
      if (blank_rows >= 3) {
        visible_runs.push_back(std::max(1, y - blank_rows + 1 - run_start));
        in_run = false;
        blank_rows = 0;
      }
    }
  }
  if (in_run) {
    visible_runs.push_back(std::max(1, pixels.height() - blank_rows - run_start));
  }

  if (visible_runs.empty()) {
    return 48;
  }

  std::sort(visible_runs.begin(), visible_runs.end());
  const auto median_ink_height = visible_runs[visible_runs.size() / 2U];
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(median_ink_height) * 1.35)), 8, 220);
}

bool is_background_layer_name(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lower == "background";
}

bool is_full_canvas_background(const Layer& layer, std::int32_t canvas_width, std::int32_t canvas_height) {
  if (!is_background_layer_name(layer.name())) {
    return false;
  }
  const auto bounds = layer.bounds();
  return bounds.x == 0 && bounds.y == 0 && bounds.width >= canvas_width && bounds.height >= canvas_height;
}

bool records_look_like_legacy_top_to_bottom(const std::vector<Layer>& layers, std::int32_t canvas_width,
                                            std::int32_t canvas_height) {
  return !layers.empty() && is_full_canvas_background(layers.back(), canvas_width, canvas_height);
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
  }
  return {'n', 'o', 'r', 'm'};
}

std::optional<std::array<char, 4>> block_key_from_string(std::string_view key);

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
  return BlendMode::Normal;
}

double read_f64(BigEndianReader& reader) {
  const auto bits = reader.read_u64();
  return std::bit_cast<double>(bits);
}

std::string read_descriptor_unicode_string(BigEndianReader& reader) {
  const auto code_unit_count = reader.read_u32();
  if (code_unit_count > reader.remaining() / 2U) {
    throw std::runtime_error("PSD descriptor string is truncated");
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
  return decoded;
}

std::string read_descriptor_id(BigEndianReader& reader) {
  const auto length = reader.read_u32();
  if (length == 0) {
    return key_string(read_signature(reader));
  }
  const auto bytes = reader.read_bytes(length);
  return std::string(bytes.begin(), bytes.end());
}

DescriptorObject read_descriptor(BigEndianReader& reader);

DescriptorValue read_descriptor_value(BigEndianReader& reader, const std::array<char, 4>& type) {
  DescriptorValue value;
  const auto type_key = key_string(type);
  if (type_key == "bool") {
    value.type = DescriptorValue::Type::Bool;
    value.bool_value = reader.read_u8() != 0;
    return value;
  }
  if (type_key == "long") {
    value.type = DescriptorValue::Type::Integer;
    value.integer_value = static_cast<std::int32_t>(reader.read_u32());
    return value;
  }
  if (type_key == "comp") {
    value.type = DescriptorValue::Type::Integer;
    value.integer_value = static_cast<std::int32_t>(reader.read_u64());
    return value;
  }
  if (type_key == "doub") {
    value.type = DescriptorValue::Type::Double;
    value.double_value = read_f64(reader);
    return value;
  }
  if (type_key == "UntF") {
    value.type = DescriptorValue::Type::UnitFloat;
    value.unit = key_string(read_signature(reader));
    value.double_value = read_f64(reader);
    return value;
  }
  if (type_key == "TEXT") {
    value.type = DescriptorValue::Type::String;
    value.string_value = read_descriptor_unicode_string(reader);
    return value;
  }
  if (type_key == "enum") {
    value.type = DescriptorValue::Type::Enum;
    value.enum_type = read_descriptor_id(reader);
    value.enum_value = read_descriptor_id(reader);
    return value;
  }
  if (type_key == "Objc" || type_key == "GlbO") {
    value.type = DescriptorValue::Type::Object;
    value.object_value = std::make_shared<DescriptorObject>(read_descriptor(reader));
    return value;
  }
  if (type_key == "VlLs") {
    value.type = DescriptorValue::Type::List;
    const auto count = reader.read_u32();
    value.list_value.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      value.list_value.push_back(read_descriptor_value(reader, read_signature(reader)));
    }
    return value;
  }
  if (type_key == "tdta" || type_key == "alis") {
    value.type = DescriptorValue::Type::Raw;
    const auto length = reader.read_u32();
    value.raw_value = reader.read_bytes(length);
    return value;
  }
  if (type_key == "type" || type_key == "GlbC") {
    value.type = DescriptorValue::Type::String;
    (void)read_descriptor_unicode_string(reader);
    value.string_value = read_descriptor_id(reader);
    return value;
  }
  throw std::runtime_error("Unsupported PSD descriptor value type: " + type_key);
}

DescriptorObject read_descriptor(BigEndianReader& reader) {
  DescriptorObject object;
  object.name = read_descriptor_unicode_string(reader);
  object.class_id = read_descriptor_id(reader);
  const auto item_count = reader.read_u32();
  for (std::uint32_t index = 0; index < item_count; ++index) {
    const auto key = read_descriptor_id(reader);
    object.values[key] = read_descriptor_value(reader, read_signature(reader));
  }
  return object;
}

const DescriptorValue* descriptor_value(const DescriptorObject& object, std::string_view key) {
  const auto found = object.values.find(std::string(key));
  return found == object.values.end() ? nullptr : &found->second;
}

const DescriptorObject* descriptor_object(const DescriptorObject& object, std::string_view key) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::Object || value->object_value == nullptr) {
    return nullptr;
  }
  return value->object_value.get();
}

bool descriptor_bool(const DescriptorObject& object, std::string_view key, bool fallback = false) {
  const auto* value = descriptor_value(object, key);
  return value != nullptr && value->type == DescriptorValue::Type::Bool ? value->bool_value : fallback;
}

double descriptor_number(const DescriptorObject& object, std::string_view key, double fallback = 0.0) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type == DescriptorValue::Type::UnitFloat || value->type == DescriptorValue::Type::Double) {
    return value->double_value;
  }
  if (value->type == DescriptorValue::Type::Integer) {
    return static_cast<double>(value->integer_value);
  }
  return fallback;
}

std::string descriptor_enum(const DescriptorObject& object, std::string_view key, std::string fallback = {}) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::Enum) {
    return fallback;
  }
  return value->enum_value;
}

float percent_to_unit(double value) {
  if (!std::isfinite(value)) {
    return 1.0F;
  }
  return std::clamp(static_cast<float>(value / 100.0), 0.0F, 1.0F);
}

RgbColor descriptor_rgb_color(const DescriptorObject& object, std::string_view key, RgbColor fallback = {}) {
  const auto* color_object = descriptor_object(object, key);
  if (color_object == nullptr) {
    return fallback;
  }
  return RgbColor{static_cast<std::uint8_t>(std::clamp(std::lround(descriptor_number(*color_object, "Rd  ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(descriptor_number(*color_object, "Grn ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(descriptor_number(*color_object, "Bl  ")), 0L, 255L))};
}

LayerStyleGradientType gradient_type_from_descriptor(std::string_view value) {
  if (value == "Rdl ") {
    return LayerStyleGradientType::Radial;
  }
  if (value == "Angl") {
    return LayerStyleGradientType::Angle;
  }
  if (value == "Rflc") {
    return LayerStyleGradientType::Reflected;
  }
  if (value == "Dmnd") {
    return LayerStyleGradientType::Diamond;
  }
  return LayerStyleGradientType::Linear;
}

LayerStyleGradient parse_gradient(const DescriptorObject& effect) {
  LayerStyleGradient gradient;
  if (const auto* gradient_object = descriptor_object(effect, "Grad"); gradient_object != nullptr) {
    if (const auto* colors = descriptor_value(*gradient_object, "Clrs");
        colors != nullptr && colors->type == DescriptorValue::Type::List) {
      for (const auto& item : colors->list_value) {
        if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
          continue;
        }
        const auto& stop = *item.object_value;
        gradient.color_stops.push_back(
            GradientColorStop{std::clamp(static_cast<float>(descriptor_number(stop, "Lctn") / 4096.0), 0.0F, 1.0F),
                              descriptor_rgb_color(stop, "Clr ")});
      }
    }
    if (const auto* transparency = descriptor_value(*gradient_object, "Trns");
        transparency != nullptr && transparency->type == DescriptorValue::Type::List) {
      for (const auto& item : transparency->list_value) {
        if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
          continue;
        }
        const auto& stop = *item.object_value;
        gradient.alpha_stops.push_back(
            GradientAlphaStop{std::clamp(static_cast<float>(descriptor_number(stop, "Lctn") / 4096.0), 0.0F, 1.0F),
                              percent_to_unit(descriptor_number(stop, "Opct", 100.0))});
      }
    }
  }
  gradient.angle_degrees = static_cast<float>(descriptor_number(effect, "Angl", 90.0));
  gradient.scale = std::max(0.01F, static_cast<float>(descriptor_number(effect, "Scl ", 100.0) / 100.0));
  gradient.reverse = descriptor_bool(effect, "Rvrs", false);
  gradient.type = gradient_type_from_descriptor(descriptor_enum(effect, "Type", "Lnr "));
  std::sort(gradient.color_stops.begin(), gradient.color_stops.end(),
            [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::sort(gradient.alpha_stops.begin(), gradient.alpha_stops.end(),
            [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });
  return gradient;
}

std::optional<LayerDropShadow> parse_drop_shadow(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = blend_mode_from_key(block_key_from_string(descriptor_enum(effect, "Md  ", "mul ")).value_or(
      std::array<char, 4>{'m', 'u', 'l', ' '}));
  shadow.color = descriptor_rgb_color(effect, "Clr ", RgbColor{0, 0, 0});
  shadow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  shadow.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  shadow.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 5.0)));
  shadow.spread = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  shadow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return shadow;
}

std::optional<LayerOuterGlow> parse_outer_glow(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = blend_mode_from_key(block_key_from_string(descriptor_enum(effect, "Md  ", "scrn")).value_or(
      std::array<char, 4>{'s', 'c', 'r', 'n'}));
  glow.color = descriptor_rgb_color(effect, "Clr ", RgbColor{255, 255, 190});
  glow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  glow.spread = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  glow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return glow;
}

std::optional<LayerBevelEmboss> parse_bevel_emboss(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode =
      blend_mode_from_key(block_key_from_string(descriptor_enum(effect, "hglM", "scrn")).value_or(
          std::array<char, 4>{'s', 'c', 'r', 'n'}));
  bevel.highlight_color = descriptor_rgb_color(effect, "hglC", RgbColor{255, 255, 255});
  bevel.highlight_opacity = percent_to_unit(descriptor_number(effect, "hglO", 75.0));
  bevel.shadow_blend_mode =
      blend_mode_from_key(block_key_from_string(descriptor_enum(effect, "sdwM", "mul ")).value_or(
          std::array<char, 4>{'m', 'u', 'l', ' '}));
  bevel.shadow_color = descriptor_rgb_color(effect, "sdwC", RgbColor{0, 0, 0});
  bevel.shadow_opacity = percent_to_unit(descriptor_number(effect, "sdwO", 75.0));
  bevel.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  bevel.altitude_degrees = static_cast<float>(descriptor_number(effect, "Lald", 30.0));
  bevel.depth = std::max(0.01F, static_cast<float>(descriptor_number(effect, "srgR", 100.0) / 100.0));
  bevel.size = std::max(1.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  bevel.direction_up = descriptor_enum(effect, "bvlD", "In  ") != "Out ";
  return bevel;
}

std::optional<LayerGradientFill> parse_gradient_fill(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = blend_mode_from_key(block_key_from_string(descriptor_enum(effect, "Md  ", "norm")).value_or(
      std::array<char, 4>{'n', 'o', 'r', 'm'}));
  fill.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  fill.gradient = parse_gradient(effect);
  return fill;
}

LayerStrokePosition stroke_position_from_descriptor(std::string_view value) {
  if (value == "InsF") {
    return LayerStrokePosition::Inside;
  }
  if (value == "CtrF") {
    return LayerStrokePosition::Center;
  }
  return LayerStrokePosition::Outside;
}

std::optional<LayerStroke> parse_stroke(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = blend_mode_from_key(block_key_from_string(descriptor_enum(effect, "Md  ", "norm")).value_or(
      std::array<char, 4>{'n', 'o', 'r', 'm'}));
  stroke.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  stroke.size = std::max(1.0F, static_cast<float>(descriptor_number(effect, "Sz  ", 3.0)));
  stroke.position = stroke_position_from_descriptor(descriptor_enum(effect, "Styl", "OutF"));
  stroke.color = descriptor_rgb_color(effect, "Clr ", RgbColor{0, 0, 0});
  stroke.uses_gradient = descriptor_enum(effect, "PntT", "SClr") == "GrFl";
  if (stroke.uses_gradient) {
    stroke.gradient = parse_gradient(effect);
  }
  return stroke;
}

LayerStyle parse_lfx2_layer_style(std::span<const std::uint8_t> payload) {
  LayerStyle style;
  try {
    BigEndianReader reader(payload);
    (void)reader.read_u32();  // object effects version
    const auto descriptor_version = reader.read_u32();
    if (descriptor_version != 16) {
      return style;
    }
    const auto root = read_descriptor(reader);
    style.effects_visible = descriptor_bool(root, "masterFXSwitch", true);
    if (const auto* effect = descriptor_object(root, "DrSh"); effect != nullptr) {
      if (const auto shadow = parse_drop_shadow(*effect); shadow.has_value()) {
        style.drop_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "OrGl"); effect != nullptr) {
      if (const auto glow = parse_outer_glow(*effect); glow.has_value()) {
        style.outer_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "outerGlow"); effect != nullptr) {
      if (const auto glow = parse_outer_glow(*effect); glow.has_value()) {
        style.outer_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "ebbl"); effect != nullptr) {
      if (const auto bevel = parse_bevel_emboss(*effect); bevel.has_value()) {
        style.bevels.push_back(*bevel);
      }
    }
    if (const auto* effect = descriptor_object(root, "bevelEmboss"); effect != nullptr) {
      if (const auto bevel = parse_bevel_emboss(*effect); bevel.has_value()) {
        style.bevels.push_back(*bevel);
      }
    }
    if (const auto* effect = descriptor_object(root, "GrFl"); effect != nullptr) {
      if (const auto fill = parse_gradient_fill(*effect); fill.has_value()) {
        style.gradient_fills.push_back(*fill);
      }
    }
    if (const auto* effect = descriptor_object(root, "FrFX"); effect != nullptr) {
      if (const auto stroke = parse_stroke(*effect); stroke.has_value()) {
        style.strokes.push_back(*stroke);
      }
    }
    if (const auto* value = descriptor_value(root, "dropShadowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto shadow = parse_drop_shadow(*item.object_value); shadow.has_value()) {
            style.drop_shadows.push_back(*shadow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "outerGlowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto glow = parse_outer_glow(*item.object_value); glow.has_value()) {
            style.outer_glows.push_back(*glow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "bevelEmbossMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto bevel = parse_bevel_emboss(*item.object_value); bevel.has_value()) {
            style.bevels.push_back(*bevel);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "gradientFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto fill = parse_gradient_fill(*item.object_value); fill.has_value()) {
            style.gradient_fills.push_back(*fill);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "frameFXMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto stroke = parse_stroke(*item.object_value); stroke.has_value()) {
            style.strokes.push_back(*stroke);
          }
        }
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return style;
}

RgbColor read_legacy_effect_color(BigEndianReader& reader) {
  (void)reader.read_u16();
  const auto red = reader.read_u16();
  const auto green = reader.read_u16();
  const auto blue = reader.read_u16();
  (void)reader.read_u16();
  return RgbColor{static_cast<std::uint8_t>(red / 257U), static_cast<std::uint8_t>(green / 257U),
                  static_cast<std::uint8_t>(blue / 257U)};
}

LayerStyle parse_lrfx_layer_style(std::span<const std::uint8_t> payload) {
  LayerStyle style;
  try {
    BigEndianReader reader(payload);
    (void)reader.read_u16();
    const auto effect_count = reader.read_u16();
    for (std::uint16_t index = 0; index < effect_count && reader.remaining() >= 12; ++index) {
      const auto signature = read_signature(reader);
      const auto key = read_signature(reader);
      if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        return style;
      }
      const auto effect_payload_size = reader.read_u32();
      if (effect_payload_size > reader.remaining()) {
        return style;
      }
      const auto effect_payload = reader.read_bytes(effect_payload_size);
      if (key != std::array<char, 4>{'d', 's', 'd', 'w'}) {
        continue;
      }
      BigEndianReader effect_reader(effect_payload);
      (void)effect_reader.read_u32();
      LayerDropShadow shadow;
      shadow.enabled = true;
      shadow.size = static_cast<float>(effect_reader.read_u32());
      (void)effect_reader.read_u32();
      shadow.angle_degrees = static_cast<float>(effect_reader.read_u32());
      shadow.distance = static_cast<float>(effect_reader.read_u32());
      shadow.color = read_legacy_effect_color(effect_reader);
      (void)read_signature(effect_reader);
      shadow.blend_mode = blend_mode_from_key(read_signature(effect_reader));
      shadow.enabled = effect_reader.read_u8() != 0;
      (void)effect_reader.read_u8();
      shadow.opacity = percent_to_unit(effect_reader.read_u8());
      if (shadow.enabled) {
        style.drop_shadows.push_back(shadow);
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return style;
}

void merge_missing_layer_style_effects(LayerStyle& target, LayerStyle source) {
  if (!source.effects_visible) {
    target.effects_visible = false;
  }
  if (target.drop_shadows.empty()) {
    target.drop_shadows = std::move(source.drop_shadows);
  }
  if (target.outer_glows.empty()) {
    target.outer_glows = std::move(source.outer_glows);
  }
  if (target.gradient_fills.empty()) {
    target.gradient_fills = std::move(source.gradient_fills);
  }
  if (target.strokes.empty()) {
    target.strokes = std::move(source.strokes);
  }
  if (target.bevels.empty()) {
    target.bevels = std::move(source.bevels);
  }
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

std::optional<std::vector<ImageResource>> read_image_resources(std::span<const std::uint8_t> bytes) {
  BigEndianReader reader(bytes);
  std::vector<ImageResource> resources;
  while (reader.remaining() > 0) {
    if (reader.remaining() < 12) {
      return std::nullopt;
    }
    ImageResource resource;
    resource.signature = read_signature(reader);
    if (resource.signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
        resource.signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
      return std::nullopt;
    }
    resource.id = reader.read_u16();
    resource.name = read_pascal_string(reader, 2);
    const auto payload_length = reader.read_u32();
    if (payload_length > reader.remaining()) {
      return std::nullopt;
    }
    resource.payload = reader.read_bytes(payload_length);
    if ((payload_length % 2U) != 0) {
      if (reader.remaining() == 0) {
        return std::nullopt;
      }
      reader.skip(1);
    }
    resources.push_back(std::move(resource));
  }
  return resources;
}

void write_image_resource(BigEndianWriter& writer, const ImageResource& resource) {
  write_signature(writer, resource.signature);
  writer.write_u16(resource.id);
  write_pascal_string(writer, resource.name, 2);
  writer.write_u32(checked_u32(resource.payload.size(), "image resource payload"));
  writer.write_bytes(resource.payload);
  if ((resource.payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

std::vector<std::uint8_t> write_image_resources(std::span<const ImageResource> resources) {
  BigEndianWriter writer;
  for (const auto& resource : resources) {
    write_image_resource(writer, resource);
  }
  return writer.bytes();
}

std::optional<std::vector<std::uint8_t>> find_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id) {
  auto parsed = read_image_resources(resources);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  for (const auto& resource : *parsed) {
    if (resource.id == id) {
      return resource.payload;
    }
  }
  return std::nullopt;
}

std::vector<std::uint8_t> image_resources_for_document(const Document& document) {
  auto resources = document.metadata().raw_psd_image_resources;
  if (document.color_state().embedded_icc_profile.empty()) {
    return resources;
  }

  auto parsed = read_image_resources(resources);
  if (!parsed.has_value()) {
    parsed = std::vector<ImageResource>{};
  }

  bool replaced = false;
  for (auto& resource : *parsed) {
    if (resource.id == kImageResourceIccProfile) {
      resource.payload = document.color_state().embedded_icc_profile;
      resource.name.clear();
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    parsed->push_back(
        ImageResource{std::array<char, 4>{'8', 'B', 'I', 'M'}, kImageResourceIccProfile, {},
                      document.color_state().embedded_icc_profile});
  }
  return write_image_resources(*parsed);
}

std::optional<std::array<char, 4>> block_key_from_string(std::string_view key) {
  if (key.size() != 4U) {
    return std::nullopt;
  }
  return std::array<char, 4>{key[0], key[1], key[2], key[3]};
}

void write_additional_layer_block(BigEndianWriter& writer, const std::array<char, 4>& key,
                                  std::span<const std::uint8_t> payload) {
  write_signature(writer, {'8', 'B', 'I', 'M'});
  write_signature(writer, key);
  writer.write_u32(checked_u32(payload.size(), "additional layer block length"));
  writer.write_bytes(payload);
  if ((payload.size() % 2U) != 0) {
    writer.write_u8(0);
  }
}

LayerRecord read_layer_record(BigEndianReader& reader) {
  LayerRecord record;
  const auto top = static_cast<std::int32_t>(reader.read_u32());
  const auto left = static_cast<std::int32_t>(reader.read_u32());
  const auto bottom = static_cast<std::int32_t>(reader.read_u32());
  const auto right = static_cast<std::int32_t>(reader.read_u32());
  record.bounds = Rect{left, top, right - left, bottom - top};

  const auto channel_count = reader.read_u16();
  for (std::uint16_t i = 0; i < channel_count; ++i) {
    record.channels.push_back(LayerChannelInfo{reader.read_u16(), reader.read_u32()});
  }

  const auto signature = read_signature(reader);
  if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
      signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
    throw std::runtime_error("Invalid PSD layer blend mode signature");
  }
  record.blend_mode = blend_mode_from_key(read_signature(reader));
  record.opacity = reader.read_u8();
  reader.skip(1);  // clipping
  const auto flags = reader.read_u8();
  record.visible = (flags & 0x02U) == 0;
  reader.skip(1);  // filler

  const auto extra_length = read_section_length(reader, "layer extra data");
  const auto extra_end = reader.position() + extra_length;
  if (extra_length >= 8) {
    const auto mask_length = read_section_length(reader, "layer mask data");
    reader.skip(mask_length);
    const auto blending_ranges_length = read_section_length(reader, "layer blending ranges");
    reader.skip(blending_ranges_length);
    if (reader.position() < extra_end) {
      record.name = read_pascal_string(reader, 4);
    }
    while (reader.position() + 12 <= extra_end) {
      const auto block_signature = read_signature(reader);
      if (block_signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          block_signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        break;
      }
      const auto block_key = read_signature(reader);
      const auto block_length = reader.read_u32();
      if (block_length > extra_end - reader.position()) {
        break;
      }
      auto payload = reader.read_bytes(block_length);
      const auto key = key_string(block_key);
      record.additional_blocks.push_back(UnknownPsdBlock{key, payload});
      if (key == "luni") {
        if (auto unicode_name = read_unicode_string_payload(record.additional_blocks.back().payload);
            unicode_name.has_value()) {
          record.name = *unicode_name;
        }
      }
      if (key == "TySh" || key == "tySh") {
        const auto& text_payload = record.additional_blocks.back().payload;
        if (!record.text.has_value()) {
          record.text = extract_engine_data_text(text_payload);
        }
        if (!record.text_size.has_value()) {
          record.text_size = extract_engine_data_font_size(text_payload);
        }
      }
      if (key == "lfx2") {
        merge_missing_layer_style_effects(record.layer_style, parse_lfx2_layer_style(record.additional_blocks.back().payload));
      } else if (key == "lrFX") {
        merge_missing_layer_style_effects(record.layer_style, parse_lrfx_layer_style(record.additional_blocks.back().payload));
      }
      if (key == "lsct" || key == "lsdk") {
        const auto& section_payload = record.additional_blocks.back().payload;
        if (section_payload.size() >= 4U) {
          BigEndianReader section_reader(section_payload);
          record.section_divider_type = section_reader.read_u32();
          if (section_reader.remaining() >= 8U) {
            const auto section_signature = read_signature(section_reader);
            if (section_signature == std::array<char, 4>{'8', 'B', 'I', 'M'} ||
                section_signature == std::array<char, 4>{'8', 'B', '6', '4'}) {
              record.blend_mode = blend_mode_from_key(read_signature(section_reader));
            }
          }
        }
      }
      if ((block_length % 2U) != 0 && reader.position() < extra_end) {
        reader.skip(1);
      }
    }
  }
  if (reader.position() < extra_end) {
    reader.skip(extra_end - reader.position());
  }
  if (record.name.empty()) {
    record.name = "Layer";
  }
  return record;
}

bool encoded_layer_uses_source_state(const EncodedLayer& encoded) noexcept {
  return encoded.layer != nullptr && encoded.kind != EncodedLayerKind::GroupBoundary;
}

std::string encoded_layer_name(const EncodedLayer& encoded) {
  return encoded.kind == EncodedLayerKind::GroupBoundary ? "</Layer group>" : encoded.layer->name();
}

BlendMode encoded_layer_blend_mode(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->blend_mode() : BlendMode::Normal;
}

float encoded_layer_opacity(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->opacity() : 1.0F;
}

bool encoded_layer_visible(const EncodedLayer& encoded) noexcept {
  return encoded_layer_uses_source_state(encoded) ? encoded.layer->visible() : true;
}

std::uint32_t group_section_divider_type(const Layer& layer) {
  if (const auto found = layer.metadata().find("photoslop.layer_group_expanded");
      found != layer.metadata().end() && found->second == "false") {
    return 2U;
  }
  return 1U;
}

std::vector<std::uint8_t> section_divider_payload(std::uint32_t type, BlendMode blend_mode,
                                                  bool include_blend_mode) {
  BigEndianWriter payload;
  payload.write_u32(type);
  if (include_blend_mode) {
    write_signature(payload, {'8', 'B', 'I', 'M'});
    write_signature(payload, blend_mode_key(blend_mode));
  }
  return payload.bytes();
}

bool should_skip_layer_block(const EncodedLayer& encoded, const UnknownPsdBlock& block) {
  if (block.key == "luni") {
    return true;
  }
  return encoded.kind == EncodedLayerKind::Group && (block.key == "lsct" || block.key == "lsdk");
}

void write_layer_record(BigEndianWriter& writer, const EncodedLayer& encoded) {
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.y));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.x));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.y + encoded.bounds.height));
  writer.write_u32(static_cast<std::uint32_t>(encoded.bounds.x + encoded.bounds.width));
  writer.write_u16(static_cast<std::uint16_t>(encoded.channel_ids.size()));

  for (std::size_t i = 0; i < encoded.channel_ids.size(); ++i) {
    writer.write_u16(encoded.channel_ids[i]);
    writer.write_u32(checked_u32(encoded.channel_data[i].size() + 2, "layer channel data length"));
  }

  write_signature(writer, {'8', 'B', 'I', 'M'});
  write_signature(writer, blend_mode_key(encoded_layer_blend_mode(encoded)));
  writer.write_u8(
      static_cast<std::uint8_t>(std::clamp(std::lround(encoded_layer_opacity(encoded) * 255.0F), 0L, 255L)));
  writer.write_u8(0);  // clipping
  writer.write_u8(encoded_layer_visible(encoded) ? 0 : 0x02U);
  writer.write_u8(0);

  BigEndianWriter extra;
  extra.write_u32(0);  // layer mask data
  extra.write_u32(0);  // layer blending ranges
  const auto name = encoded_layer_name(encoded);
  write_pascal_string(extra, name, 4);
  auto unicode_name = unicode_string_payload(name);
  write_additional_layer_block(extra, {'l', 'u', 'n', 'i'}, unicode_name);

  if (encoded.kind == EncodedLayerKind::GroupBoundary) {
    const auto payload = section_divider_payload(3U, BlendMode::Normal, false);
    write_additional_layer_block(extra, {'l', 's', 'c', 't'}, payload);
  } else if (encoded.kind == EncodedLayerKind::Group) {
    const auto payload =
        section_divider_payload(group_section_divider_type(*encoded.layer), encoded.layer->blend_mode(), true);
    write_additional_layer_block(extra, {'l', 's', 'c', 't'}, payload);
  }

  if (encoded.layer != nullptr) {
    for (const auto& block : encoded.layer->unknown_psd_blocks()) {
      if (should_skip_layer_block(encoded, block)) {
        continue;
      }
      if (auto key = block_key_from_string(block.key); key.has_value()) {
        write_additional_layer_block(extra, *key, block.payload);
      }
    }
  }
  write_length_prefixed_block(writer, extra.bytes());
}

EncodedLayer encode_layer(const Layer& layer) {
  if (layer.kind() != LayerKind::Pixel) {
    throw std::runtime_error("Layered PSD export currently supports pixel and group layers only");
  }
  const auto& pixels = layer.pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3 || pixels.format().channels > 4) {
    throw std::runtime_error("Layered PSD export currently supports RGB/RGBA 8-bit layers only");
  }

  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Pixel;
  encoded.bounds = layer.bounds().empty() ? Rect::from_size(pixels.width(), pixels.height()) : layer.bounds();
  encoded.channel_ids = {kChannelRed, kChannelGreen, kChannelBlue};
  if (pixels.format().channels >= 4) {
    encoded.channel_ids.push_back(kChannelTransparency);
  }

  encoded.channel_data.resize(encoded.channel_ids.size());
  const auto pixel_count = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  for (std::size_t channel_index = 0; channel_index < encoded.channel_ids.size(); ++channel_index) {
    auto& channel = encoded.channel_data[channel_index];
    channel.resize(pixel_count);
    const auto source_channel = encoded.channel_ids[channel_index] == kChannelTransparency ? 3 : channel_index;
    for (std::size_t i = 0; i < pixel_count; ++i) {
      channel[i] = pixels.data()[i * pixels.format().channels + source_channel];
    }
  }
  return encoded;
}

EncodedLayer encode_group_boundary() {
  EncodedLayer encoded;
  encoded.kind = EncodedLayerKind::GroupBoundary;
  return encoded;
}

EncodedLayer encode_group(const Layer& layer) {
  EncodedLayer encoded;
  encoded.layer = &layer;
  encoded.kind = EncodedLayerKind::Group;
  encoded.bounds = layer.bounds();
  return encoded;
}

void append_encoded_layers(const Layer& layer, std::vector<EncodedLayer>& encoded_layers) {
  if (layer.kind() == LayerKind::Pixel) {
    encoded_layers.push_back(encode_layer(layer));
    return;
  }

  if (layer.kind() == LayerKind::Group) {
    encoded_layers.push_back(encode_group_boundary());
    for (const auto& child : layer.children()) {
      append_encoded_layers(child, encoded_layers);
    }
    encoded_layers.push_back(encode_group(layer));
    return;
  }

  throw std::runtime_error("Layered PSD export currently supports pixel and group layers only");
}

Document read_flat_composite(BigEndianReader& reader, const Header& header) {
  const auto format = format_from_header(header);
  const auto compression = reader.read_u16();

  Document document(static_cast<std::int32_t>(header.width), static_cast<std::int32_t>(header.height), format);
  PixelBuffer pixels(static_cast<std::int32_t>(header.width), static_cast<std::int32_t>(header.height), format);

  if (compression == kCompressionRaw) {
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      const auto channel_data = read_channel_data(reader, compression, static_cast<std::int32_t>(header.width),
                                                  static_cast<std::int32_t>(header.height));
      for (std::size_t i = 0; i < channel_data.size(); ++i) {
        pixels.data()[i * 3 + channel] = channel_data[i];
      }
    }
    for (std::uint16_t channel = 3; channel < header.channels; ++channel) {
      (void)read_channel_data(reader, compression, static_cast<std::int32_t>(header.width),
                              static_cast<std::int32_t>(header.height));
    }
  } else if (compression == kCompressionRle) {
    std::vector<std::uint16_t> row_lengths;
    row_lengths.reserve(static_cast<std::size_t>(header.channels) * static_cast<std::size_t>(header.height));
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      for (std::uint32_t y = 0; y < header.height; ++y) {
        row_lengths.push_back(reader.read_u16());
      }
    }
    for (std::uint16_t channel = 0; channel < header.channels; ++channel) {
      const auto offset = static_cast<std::size_t>(channel) * static_cast<std::size_t>(header.height);
      const auto rows = std::span<const std::uint16_t>(row_lengths.data() + offset, static_cast<std::size_t>(header.height));
      const auto channel_data = read_rle_channel_from_counts(reader, rows, static_cast<std::int32_t>(header.width));
      if (channel < 3) {
        for (std::size_t i = 0; i < channel_data.size(); ++i) {
          pixels.data()[i * 3 + channel] = channel_data[i];
        }
      }
    }
  } else {
    throw std::runtime_error("Unsupported PSD composite compression");
  }

  document.add_pixel_layer("Background", std::move(pixels));
  return document;
}

bool is_section_divider_folder(std::uint32_t type) noexcept {
  return type == 1U || type == 2U;
}

bool is_section_divider_boundary(std::uint32_t type) noexcept {
  return type == 3U;
}

void copy_layer_state(Layer& target, const Layer& source) {
  target.set_bounds(source.bounds());
  target.set_blend_mode(source.blend_mode());
  target.set_opacity(source.opacity());
  target.set_visible(source.visible());
  target.layer_style() = source.layer_style();
  target.metadata() = source.metadata();
  target.unknown_psd_blocks() = source.unknown_psd_blocks();
}

std::vector<Layer> build_group_hierarchy(std::vector<DecodedLayer> flat_layers) {
  std::vector<std::vector<Layer>> stack;
  stack.emplace_back();

  for (auto& decoded : flat_layers) {
    if (is_section_divider_boundary(decoded.section_divider_type)) {
      stack.emplace_back();
      continue;
    }

    if (is_section_divider_folder(decoded.section_divider_type)) {
      std::vector<Layer> children;
      if (stack.size() > 1U) {
        children = std::move(stack.back());
        stack.pop_back();
      }

      Layer group(0, decoded.layer.name(), LayerKind::Group);
      copy_layer_state(group, decoded.layer);
      group.metadata()["photoslop.layer_group_expanded"] =
          decoded.section_divider_type == 1U ? "true" : "false";
      group.children() = std::move(children);
      stack.back().push_back(std::move(group));
      continue;
    }

    stack.back().push_back(std::move(decoded.layer));
  }

  while (stack.size() > 1U) {
    auto orphaned_children = std::move(stack.back());
    stack.pop_back();
    stack.back().insert(stack.back().end(), std::make_move_iterator(orphaned_children.begin()),
                        std::make_move_iterator(orphaned_children.end()));
  }

  return std::move(stack.front());
}

Layer clone_layer_with_document_ids(Document& document, const Layer& source) {
  Layer cloned = source.kind() == LayerKind::Pixel
                     ? Layer(document.allocate_layer_id(), source.name(), source.pixels())
                     : Layer(document.allocate_layer_id(), source.name(), source.kind());
  copy_layer_state(cloned, source);
  if (source.kind() == LayerKind::Group) {
    for (const auto& child : source.children()) {
      cloned.add_child(clone_layer_with_document_ids(document, child));
    }
  }
  return cloned;
}

std::vector<Layer> read_layers(BigEndianReader& layer_reader, std::int32_t canvas_width, std::int32_t canvas_height) {
  const auto layer_info_length = read_section_length(layer_reader, "layer info");
  if (layer_info_length == 0) {
    return {};
  }

  const auto layer_info_end = layer_reader.position() + layer_info_length;
  auto layer_count_raw = static_cast<std::int16_t>(layer_reader.read_u16());
  const auto layer_count = static_cast<std::uint16_t>(std::abs(layer_count_raw));
  std::vector<LayerRecord> records;
  records.reserve(layer_count);
  for (std::uint16_t i = 0; i < layer_count; ++i) {
    records.push_back(read_layer_record(layer_reader));
  }

  std::vector<DecodedLayer> decoded_layers;
  decoded_layers.reserve(layer_count);
  for (const auto& record : records) {
    const auto width = std::max(0, record.bounds.width);
    const auto height = std::max(0, record.bounds.height);
    const auto has_rgb = std::any_of(record.channels.begin(), record.channels.end(), [](LayerChannelInfo channel) {
      return channel.id == kChannelRed || channel.id == kChannelGreen || channel.id == kChannelBlue;
    });
    const auto has_alpha = std::any_of(record.channels.begin(), record.channels.end(), [](LayerChannelInfo channel) {
      return channel.id == kChannelTransparency;
    });
    PixelBuffer pixels(width, height, (has_alpha || !has_rgb) ? PixelFormat::rgba8() : PixelFormat::rgb8());
    if (has_alpha) {
      for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
          pixels.pixel(x, y)[3] = 255;
        }
      }
    }

    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (const auto channel : record.channels) {
      if (channel.length < 2) {
        throw std::runtime_error("Invalid PSD layer channel length");
      }
      const auto compression = layer_reader.read_u16();
      const auto payload_length = channel.length - 2;
      if (compression != kCompressionRaw && compression != kCompressionRle) {
        layer_reader.skip(payload_length);
        continue;
      }
      const auto channel_start = layer_reader.position();
      if (compression == kCompressionRaw && payload_length < pixel_count) {
        throw std::runtime_error("PSD layer channel data is truncated");
      }
      const auto channel_data = read_channel_data(layer_reader, compression, width, height);
      const auto target_channel = channel.id == kChannelRed      ? 0
                                  : channel.id == kChannelGreen  ? 1
                                  : channel.id == kChannelBlue   ? 2
                                  : channel.id == kChannelTransparency ? 3
                                                                       : -1;
      for (std::size_t i = 0; i < channel_data.size(); ++i) {
        if (target_channel >= 0 && target_channel < pixels.format().channels) {
          pixels.data()[i * pixels.format().channels + static_cast<std::size_t>(target_channel)] = channel_data[i];
        }
      }
      const auto consumed = layer_reader.position() - channel_start;
      if (payload_length > consumed) {
        layer_reader.skip(payload_length - consumed);
      }
    }

    if (record.text.has_value() && !has_visible_alpha(pixels)) {
      pixels = render_placeholder_text(*record.text, width, height);
    }

    Layer layer(0, record.name, std::move(pixels));
    const auto layer_width = std::max(width, layer.pixels().width());
    const auto layer_height = std::max(height, layer.pixels().height());
    layer.set_bounds(Rect{std::clamp(record.bounds.x, -canvas_width, canvas_width * 2),
                          std::clamp(record.bounds.y, -canvas_height, canvas_height * 2), layer_width, layer_height});
    layer.set_blend_mode(record.blend_mode);
    layer.set_opacity(static_cast<float>(record.opacity) / 255.0F);
    layer.set_visible(record.visible);
    layer.layer_style() = record.layer_style;
    for (auto& block : record.additional_blocks) {
      layer.unknown_psd_blocks().push_back(std::move(block));
    }
    if (record.text.has_value()) {
      layer.metadata()["photoslop.text"] = *record.text;
      layer.metadata()["photoslop.text.font"] = "PSD Text";
      layer.metadata()["photoslop.text.size"] =
          std::to_string(record.text_size.value_or(estimate_text_size_from_alpha(layer.pixels())));
      layer.metadata()["photoslop.text.color"] = "#000000";
    }
    decoded_layers.push_back(DecodedLayer{std::move(layer), record.section_divider_type});
  }

  if (layer_reader.position() < layer_info_end) {
    layer_reader.skip(layer_info_end - layer_reader.position());
  }
  if ((layer_info_length % 2U) != 0 && layer_reader.remaining() > 0) {
    layer_reader.skip(1);
  }
  return build_group_hierarchy(std::move(decoded_layers));
}

}  // namespace

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == '8' && bytes[1] == 'B' && bytes[2] == 'P' && bytes[3] == 'S';
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, ReadOptions /*options*/) {
  BigEndianReader reader(bytes);
  const auto header = read_header(reader);
  const auto format = format_from_header(header);

  skip_length_block(reader, "color mode data");
  auto image_resources = read_length_block(reader, "image resources");

  Document document(static_cast<std::int32_t>(header.width), static_cast<std::int32_t>(header.height), format);
  document.metadata().raw_psd_image_resources = image_resources;
  if (auto icc_profile = find_image_resource_payload(image_resources, kImageResourceIccProfile);
      icc_profile.has_value()) {
    document.color_state().embedded_icc_profile = std::move(*icc_profile);
  }
  const auto layer_mask_length = read_section_length(reader, "layer and mask information");
  if (layer_mask_length > 0) {
    auto layer_mask_payload = reader.read_bytes(layer_mask_length);
    BigEndianReader layer_reader(layer_mask_payload);
    auto layers = read_layers(layer_reader, document.width(), document.height());
    const auto add_layer = [&document](const Layer& source) {
      document.add_layer(clone_layer_with_document_ids(document, source));
    };

    // Photoshop stores layer records bottom-to-top. Older Photoslop builds wrote
    // them top-to-bottom, which is detectable when a full Background record is last.
    if (records_look_like_legacy_top_to_bottom(layers, document.width(), document.height())) {
      for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        add_layer(*it);
      }
    } else {
      for (const auto& source : layers) {
        add_layer(source);
      }
    }
  }

  if (document.layers().empty()) {
    auto metadata = std::move(document.metadata());
    auto color_state = std::move(document.color_state());
    document = read_flat_composite(reader, header);
    document.metadata() = std::move(metadata);
    document.color_state().embedded_icc_profile = std::move(color_state.embedded_icc_profile);
    document.color_state().ocio_view = std::move(color_state.ocio_view);
  } else {
    // Skip the compatibility composite image. The editable layered data is authoritative.
    if (reader.remaining() >= 2) {
      const auto compression = reader.read_u16();
      if (compression == kCompressionRaw) {
        const auto channel_pixels = static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height);
        const auto composite_bytes = channel_pixels * static_cast<std::size_t>(header.channels);
        if (composite_bytes <= reader.remaining()) {
          reader.skip(composite_bytes);
        }
      }
    }
  }

  document.metadata().values["psd.version"] = header.large_document ? "PSB" : "PSD";
  document.metadata().values["psd.color_mode"] = color_mode_name(header.color_mode);
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, ReadOptions options) {
  const auto bytes = read_file_bytes(path);
  return read(bytes, options);
}

std::vector<std::uint8_t> DocumentIo::write_flat_rgb8(const Document& document, WriteOptions options) {
  if (options.large_document) {
    throw std::runtime_error("The starter PSD writer does not yet support PSB length fields");
  }

  const auto flattened = Compositor{}.flatten_rgb8(document);

  BigEndianWriter writer;
  write_header(writer, Header{options.large_document,
                              3,
                              static_cast<std::uint32_t>(flattened.height()),
                              static_cast<std::uint32_t>(flattened.width()),
                              8,
                              kColorModeRgb});

  writer.write_u32(0);  // Color mode data section.
  write_length_prefixed_block(writer, image_resources_for_document(document));
  writer.write_u32(0);  // Layer and mask information section.
  writer.write_u16(kCompressionRaw);

  const auto channel_pixels = static_cast<std::size_t>(flattened.width()) * static_cast<std::size_t>(flattened.height());
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    for (std::size_t i = 0; i < channel_pixels; ++i) {
      writer.write_u8(flattened.data()[i * 3 + channel]);
    }
  }

  return writer.bytes();
}

void DocumentIo::write_flat_rgb8_file(const Document& document, const std::filesystem::path& path,
                                      WriteOptions options) {
  const auto bytes = write_flat_rgb8(document, options);
  write_file_bytes(path, bytes);
}

std::vector<std::uint8_t> DocumentIo::write_layered_rgb8(const Document& document, WriteOptions options) {
  if (options.large_document) {
    throw std::runtime_error("The layered PSD writer does not yet support PSB length fields");
  }

  std::vector<EncodedLayer> encoded_layers;
  encoded_layers.reserve(document.layers().size());
  // Photoshop stores layer records in stack order from bottom to top. Photoslop's
  // document model uses the same order, so write it directly instead of reversing.
  for (const auto& layer : document.layers()) {
    append_encoded_layers(layer, encoded_layers);
  }

  BigEndianWriter layer_info;
  layer_info.write_u16(static_cast<std::uint16_t>(encoded_layers.size()));
  for (const auto& encoded : encoded_layers) {
    write_layer_record(layer_info, encoded);
  }
  for (const auto& encoded : encoded_layers) {
    for (const auto& channel : encoded.channel_data) {
      layer_info.write_u16(kCompressionRaw);
      layer_info.write_bytes(channel);
    }
  }
  if ((layer_info.bytes().size() % 2U) != 0) {
    layer_info.write_u8(0);
  }

  BigEndianWriter layer_mask;
  write_length_prefixed_block(layer_mask, layer_info.bytes());
  layer_mask.write_u32(0);  // global layer mask info

  const auto flattened = Compositor{}.flatten_rgb8(document);
  BigEndianWriter writer;
  write_header(writer, Header{false,
                              3,
                              static_cast<std::uint32_t>(document.height()),
                              static_cast<std::uint32_t>(document.width()),
                              8,
                              kColorModeRgb});
  writer.write_u32(0);
  write_length_prefixed_block(writer, image_resources_for_document(document));
  write_length_prefixed_block(writer, layer_mask.bytes());
  writer.write_u16(kCompressionRaw);

  const auto channel_pixels = static_cast<std::size_t>(flattened.width()) * static_cast<std::size_t>(flattened.height());
  for (std::uint16_t channel = 0; channel < 3; ++channel) {
    for (std::size_t i = 0; i < channel_pixels; ++i) {
      writer.write_u8(flattened.data()[i * 3 + channel]);
    }
  }
  return writer.bytes();
}

void DocumentIo::write_layered_rgb8_file(const Document& document, const std::filesystem::path& path,
                                         WriteOptions options) {
  const auto bytes = write_layered_rgb8(document, options);
  write_file_bytes(path, bytes);
}

}  // namespace photoslop::psd
