#include "formats/palette_io.hpp"

#include "formats/bmp_document_io.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace patchy::palette_io {
namespace {

inline constexpr std::size_t kMaxColors = 256;

[[nodiscard]] bool starts_with(std::span<const std::uint8_t> bytes, std::string_view signature) noexcept {
  if (bytes.size() < signature.size()) {
    return false;
  }
  return std::memcmp(bytes.data(), signature.data(), signature.size()) == 0;
}

[[nodiscard]] std::uint16_t read_u16be(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    throw std::runtime_error("Palette file is truncated");
  }
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
}

[[nodiscard]] std::uint32_t read_u32be(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("Palette file is truncated");
  }
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] float read_f32be(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return std::bit_cast<float>(read_u32be(bytes, offset));
}

[[nodiscard]] std::uint8_t float_channel(float value) noexcept {
  return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value * 255.0F + 0.5F), 0, 255));
}

void require_color_count(std::size_t count) {
  if (count == 0) {
    throw std::runtime_error("Palette file does not contain any colors");
  }
  if (count > kMaxColors) {
    throw std::runtime_error("Palette file has more than 256 colors");
  }
}

[[nodiscard]] std::vector<std::string> split_text_lines(std::span<const std::uint8_t> bytes) {
  std::vector<std::string> lines;
  std::string current;
  for (const auto byte : bytes) {
    if (byte == '\n') {
      lines.push_back(std::move(current));
      current.clear();
      continue;
    }
    if (byte != '\r') {
      current.push_back(static_cast<char>(byte));
    }
  }
  if (!current.empty()) {
    lines.push_back(std::move(current));
  }
  return lines;
}

[[nodiscard]] std::string trimmed(const std::string& text) {
  const auto begin = text.find_first_not_of(" \t");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t");
  return text.substr(begin, end - begin + 1);
}

// --- RIFF PAL (moved from bmp_document_io.cpp) ---

[[nodiscard]] PaletteFileData read_riff_pal(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 24U || !starts_with(bytes, "RIFF") ||
      std::memcmp(bytes.data() + 8U, "PAL ", 4) != 0) {
    throw std::runtime_error("Not a RIFF PAL file");
  }

  std::size_t offset = 12;
  while (offset + 8U <= bytes.size()) {
    const std::string_view chunk_id(reinterpret_cast<const char*>(bytes.data() + offset), 4);
    const auto chunk_size = static_cast<std::uint32_t>(bytes[offset + 4U]) |
                            (static_cast<std::uint32_t>(bytes[offset + 5U]) << 8U) |
                            (static_cast<std::uint32_t>(bytes[offset + 6U]) << 16U) |
                            (static_cast<std::uint32_t>(bytes[offset + 7U]) << 24U);
    offset += 8U;
    if (chunk_size > bytes.size() - offset) {
      throw std::runtime_error("PAL data chunk is truncated");
    }
    if (chunk_id == "data") {
      if (chunk_size < 4U) {
        throw std::runtime_error("PAL data chunk is too short");
      }
      const auto count = static_cast<std::uint16_t>(bytes[offset + 2U]) |
                         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 3U]) << 8U);
      if (count == 0 || 4ULL + static_cast<std::uint64_t>(count) * 4ULL > chunk_size) {
        throw std::runtime_error("PAL color count is invalid");
      }
      require_color_count(count);
      PaletteFileData data;
      data.colors.reserve(count);
      for (std::uint16_t index = 0; index < count; ++index) {
        const auto color_offset = offset + 4U + static_cast<std::size_t>(index) * 4U;
        data.colors.push_back(RgbColor{bytes[color_offset], bytes[color_offset + 1U], bytes[color_offset + 2U]});
      }
      return data;
    }
    offset += chunk_size + (chunk_size % 2U);
  }
  throw std::runtime_error("PAL file does not contain a data chunk");
}

// --- JASC PAL (moved from bmp_document_io.cpp) ---

[[nodiscard]] PaletteFileData read_jasc_pal(std::span<const std::uint8_t> bytes) {
  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::istringstream stream(text);
  std::string header;
  std::string version;
  std::size_t count = 0;
  if (!std::getline(stream, header) || !std::getline(stream, version) || !(stream >> count)) {
    throw std::runtime_error("Not a JASC PAL file");
  }
  if (!header.empty() && header.back() == '\r') {
    header.pop_back();
  }
  if (!version.empty() && version.back() == '\r') {
    version.pop_back();
  }
  if (header != "JASC-PAL" || count == 0 || count > kMaxColors) {
    throw std::runtime_error("JASC PAL header is invalid");
  }

  PaletteFileData data;
  data.colors.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    int red = 0;
    int green = 0;
    int blue = 0;
    if (!(stream >> red >> green >> blue) || red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 ||
        blue > 255) {
      throw std::runtime_error("JASC PAL color entry is invalid");
    }
    data.colors.push_back(RgbColor{static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                   static_cast<std::uint8_t>(blue)});
  }
  return data;
}

// --- GIMP .gpl ---

[[nodiscard]] PaletteFileData read_gpl(std::span<const std::uint8_t> bytes) {
  const auto lines = split_text_lines(bytes);
  if (lines.empty() || trimmed(lines.front()).rfind("GIMP Palette", 0) != 0) {
    throw std::runtime_error("Not a GIMP palette file");
  }

  PaletteFileData data;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const auto line = trimmed(lines[i]);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.rfind("Name:", 0) == 0) {
      data.name = trimmed(line.substr(5));
      continue;
    }
    if (line.rfind("Columns:", 0) == 0) {
      continue;
    }
    std::istringstream entry(line);
    int red = 0;
    int green = 0;
    int blue = 0;
    if (!(entry >> red >> green >> blue) || red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 ||
        blue > 255) {
      throw std::runtime_error("GIMP palette color entry is invalid");
    }
    data.colors.push_back(RgbColor{static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                   static_cast<std::uint8_t>(blue)});
    if (data.colors.size() > kMaxColors) {
      throw std::runtime_error("Palette file has more than 256 colors");
    }
  }
  require_color_count(data.colors.size());
  return data;
}

// --- Lospec .hex ---

[[nodiscard]] int hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

[[nodiscard]] PaletteFileData read_hex(std::span<const std::uint8_t> bytes) {
  const auto lines = split_text_lines(bytes);
  PaletteFileData data;
  for (const auto& raw : lines) {
    auto line = trimmed(raw);
    if (line.empty()) {
      continue;
    }
    if (line.front() == '#') {
      line.erase(line.begin());
    }
    if (line.size() != 6 && line.size() != 8) {
      throw std::runtime_error("Not a hex palette file");
    }
    std::array<int, 8> digits{};
    for (std::size_t i = 0; i < line.size(); ++i) {
      digits[i] = hex_digit(line[i]);
      if (digits[i] < 0) {
        throw std::runtime_error("Not a hex palette file");
      }
    }
    // 8-digit lines are RRGGBBAA; the alpha digits are ignored.
    data.colors.push_back(RgbColor{static_cast<std::uint8_t>(digits[0] * 16 + digits[1]),
                                   static_cast<std::uint8_t>(digits[2] * 16 + digits[3]),
                                   static_cast<std::uint8_t>(digits[4] * 16 + digits[5])});
    if (data.colors.size() > kMaxColors) {
      throw std::runtime_error("Palette file has more than 256 colors");
    }
  }
  require_color_count(data.colors.size());
  return data;
}

// --- Adobe .act ---

[[nodiscard]] PaletteFileData read_act(std::span<const std::uint8_t> bytes) {
  if (bytes.size() != 768 && bytes.size() != 772) {
    throw std::runtime_error("Not an Adobe color table file");
  }
  std::size_t count = kMaxColors;
  PaletteFileData data;
  if (bytes.size() == 772) {
    const auto declared = read_u16be(bytes, 768);
    const auto transparent = read_u16be(bytes, 770);
    if (declared != 0 && declared <= kMaxColors) {
      count = declared;
    }
    if (transparent != 0xFFFF && transparent < count) {
      data.transparent_index = transparent;
    }
  }
  data.colors.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto offset = index * 3U;
    data.colors.push_back(RgbColor{bytes[offset], bytes[offset + 1U], bytes[offset + 2U]});
  }
  return data;
}

// --- Adobe .aco ---

[[nodiscard]] PaletteFileData read_aco(std::span<const std::uint8_t> bytes) {
  const auto version = read_u16be(bytes, 0);
  if (version != 1 && version != 2) {
    throw std::runtime_error("Not an Adobe color swatch file");
  }
  const auto count = read_u16be(bytes, 2);
  if (count == 0 || count > kMaxColors) {
    throw std::runtime_error("Adobe color swatch count is invalid");
  }

  PaletteFileData data;
  std::size_t offset = 4;
  for (std::uint16_t index = 0; index < count; ++index) {
    const auto space = read_u16be(bytes, offset);
    const auto w = read_u16be(bytes, offset + 2);
    const auto x = read_u16be(bytes, offset + 4);
    const auto y = read_u16be(bytes, offset + 6);
    offset += 10;
    if (version == 2) {
      // Version-2 entries append a UTF-16BE name: u32 length in characters
      // (including the terminator) followed by the characters.
      const auto name_length = read_u32be(bytes, offset);
      offset += 4 + static_cast<std::size_t>(name_length) * 2U;
      if (offset > bytes.size()) {
        throw std::runtime_error("Adobe color swatch file is truncated");
      }
    }
    switch (space) {
      case 0:  // RGB, channels are value * 257
        data.colors.push_back(RgbColor{static_cast<std::uint8_t>(w / 257), static_cast<std::uint8_t>(x / 257),
                                       static_cast<std::uint8_t>(y / 257)});
        break;
      case 8: {  // Grayscale, 0..10000
        const auto gray = static_cast<std::uint8_t>(
            std::clamp<long>(std::lround(static_cast<double>(std::min<std::uint16_t>(w, 10000)) * 255.0 / 10000.0),
                             0L, 255L));
        data.colors.push_back(RgbColor{gray, gray, gray});
        break;
      }
      default:
        break;  // HSB/CMYK/Lab entries are skipped
    }
  }
  require_color_count(data.colors.size());
  return data;
}

// --- Adobe .ase ---

[[nodiscard]] PaletteFileData read_ase(std::span<const std::uint8_t> bytes) {
  if (!starts_with(bytes, "ASEF")) {
    throw std::runtime_error("Not an Adobe swatch exchange file");
  }
  const auto block_count = read_u32be(bytes, 8);
  PaletteFileData data;
  std::size_t offset = 12;
  for (std::uint32_t block = 0; block < block_count && offset + 6 <= bytes.size(); ++block) {
    const auto type = read_u16be(bytes, offset);
    const auto length = read_u32be(bytes, offset + 2);
    const auto payload_offset = offset + 6;
    if (length > bytes.size() - payload_offset) {
      throw std::runtime_error("Adobe swatch exchange file is truncated");
    }
    offset = payload_offset + length;
    if (type != 0x0001) {
      continue;  // group start/end blocks; groups are flattened
    }
    const auto name_length = read_u16be(bytes, payload_offset);
    const auto model_offset = payload_offset + 2 + static_cast<std::size_t>(name_length) * 2U;
    if (model_offset + 4 > bytes.size()) {
      throw std::runtime_error("Adobe swatch exchange file is truncated");
    }
    const std::string_view model(reinterpret_cast<const char*>(bytes.data() + model_offset), 4);
    if (model == "RGB ") {
      data.colors.push_back(RgbColor{float_channel(read_f32be(bytes, model_offset + 4)),
                                     float_channel(read_f32be(bytes, model_offset + 8)),
                                     float_channel(read_f32be(bytes, model_offset + 12))});
    } else if (model == "Gray") {
      const auto gray = float_channel(read_f32be(bytes, model_offset + 4));
      data.colors.push_back(RgbColor{gray, gray, gray});
    }
    // CMYK/LAB entries are skipped.
    if (data.colors.size() > kMaxColors) {
      throw std::runtime_error("Palette file has more than 256 colors");
    }
  }
  require_color_count(data.colors.size());
  return data;
}

// --- indexed BMP color table ---

[[nodiscard]] PaletteFileData read_bmp_palette(std::span<const std::uint8_t> bytes) {
  const auto document = bmp::DocumentIo::read(bytes);
  const auto& imported = document.indexed_palette();
  if (!imported.has_value() || imported->colors.empty()) {
    throw std::runtime_error("BMP palette file must be an indexed BMP");
  }
  PaletteFileData data;
  data.colors = imported->colors;
  return data;
}

// A byte layout that parses as .aco version 1: exactly the v1 section, optionally
// followed by a version-2 section.
[[nodiscard]] bool looks_like_aco(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < 14) {
    return false;
  }
  const auto version = static_cast<std::uint16_t>((bytes[0] << 8U) | bytes[1]);
  const auto count = static_cast<std::uint16_t>((bytes[2] << 8U) | bytes[3]);
  if ((version != 1 && version != 2) || count == 0 || count > kMaxColors) {
    return false;
  }
  if (version == 1) {
    const auto v1_size = 4ULL + static_cast<std::uint64_t>(count) * 10ULL;
    return bytes.size() == v1_size || bytes.size() > v1_size + 4ULL;
  }
  return true;
}

}  // namespace

PaletteFileData read_palette_bytes(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    throw std::runtime_error("Palette file is empty");
  }
  if (starts_with(bytes, "BM")) {
    return read_bmp_palette(bytes);
  }
  if (starts_with(bytes, "RIFF")) {
    return read_riff_pal(bytes);
  }
  if (starts_with(bytes, "JASC-PAL")) {
    return read_jasc_pal(bytes);
  }
  if (starts_with(bytes, "GIMP Palette")) {
    return read_gpl(bytes);
  }
  if (starts_with(bytes, "ASEF")) {
    return read_ase(bytes);
  }
  if (looks_like_aco(bytes)) {
    return read_aco(bytes);
  }
  if (bytes.size() == 768 || bytes.size() == 772) {
    return read_act(bytes);
  }
  try {
    return read_hex(bytes);
  } catch (const std::exception&) {
  }
  throw std::runtime_error("Unrecognized palette file format");
}

PaletteFileData read_palette_file(const std::filesystem::path& path) {
  if (path.empty()) {
    throw std::runtime_error("A palette file path is required");
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open palette file");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return read_palette_bytes(bytes);
}

std::span<const std::string_view> readable_palette_extensions() noexcept {
  static constexpr std::array<std::string_view, 7> kExtensions = {"pal", "gpl", "hex", "act", "aco", "ase", "bmp"};
  return kExtensions;
}

std::vector<std::uint8_t> write_palette_bytes(std::span<const RgbColor> colors, PaletteFileFormat format,
                                              std::string_view name) {
  if (colors.empty()) {
    throw std::runtime_error("Cannot save an empty palette");
  }
  if (colors.size() > kMaxColors) {
    throw std::runtime_error("Palette has more than 256 colors");
  }

  std::string text;
  std::vector<std::uint8_t> bytes;
  const auto append_u16be = [&bytes](std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
  };

  switch (format) {
    case PaletteFileFormat::JascPal: {
      text = "JASC-PAL\r\n0100\r\n" + std::to_string(colors.size()) + "\r\n";
      for (const auto& color : colors) {
        text += std::to_string(color.red) + " " + std::to_string(color.green) + " " + std::to_string(color.blue) +
                "\r\n";
      }
      return {text.begin(), text.end()};
    }
    case PaletteFileFormat::Gpl: {
      text = "GIMP Palette\nName: ";
      text += name.empty() ? std::string_view{"Patchy Palette"} : name;
      text += "\nColumns: 16\n#\n";
      for (const auto& color : colors) {
        text += std::to_string(color.red) + " " + std::to_string(color.green) + " " + std::to_string(color.blue) +
                "\n";
      }
      return {text.begin(), text.end()};
    }
    case PaletteFileFormat::Hex: {
      static constexpr char kDigits[] = "0123456789abcdef";
      for (const auto& color : colors) {
        for (const auto channel : {color.red, color.green, color.blue}) {
          text.push_back(kDigits[channel >> 4U]);
          text.push_back(kDigits[channel & 0x0fU]);
        }
        text.push_back('\n');
      }
      return {text.begin(), text.end()};
    }
    case PaletteFileFormat::Act: {
      bytes.reserve(772);
      for (const auto& color : colors) {
        bytes.push_back(color.red);
        bytes.push_back(color.green);
        bytes.push_back(color.blue);
      }
      bytes.resize(768, 0);
      append_u16be(static_cast<std::uint16_t>(colors.size()));
      append_u16be(0xFFFF);  // no transparent index
      return bytes;
    }
    case PaletteFileFormat::Aco: {
      append_u16be(1);
      append_u16be(static_cast<std::uint16_t>(colors.size()));
      for (const auto& color : colors) {
        append_u16be(0);  // RGB colorspace
        append_u16be(static_cast<std::uint16_t>(color.red * 257));
        append_u16be(static_cast<std::uint16_t>(color.green * 257));
        append_u16be(static_cast<std::uint16_t>(color.blue * 257));
        append_u16be(0);
      }
      return bytes;
    }
  }
  throw std::runtime_error("Unsupported palette file format");
}

void write_palette_file(const std::filesystem::path& path, std::span<const RgbColor> colors,
                        PaletteFileFormat format, std::string_view name) {
  const auto bytes = write_palette_bytes(colors, format, name);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("Could not create palette file");
  }
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    throw std::runtime_error("Could not write palette file");
  }
}

std::optional<PaletteFileFormat> palette_format_for_extension(std::string_view extension) noexcept {
  std::string lower(extension);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "pal") {
    return PaletteFileFormat::JascPal;
  }
  if (lower == "gpl") {
    return PaletteFileFormat::Gpl;
  }
  if (lower == "hex") {
    return PaletteFileFormat::Hex;
  }
  if (lower == "act") {
    return PaletteFileFormat::Act;
  }
  if (lower == "aco") {
    return PaletteFileFormat::Aco;
  }
  return std::nullopt;
}

}  // namespace patchy::palette_io
