#include "core/font_style_class.hpp"

#include <cstddef>

namespace patchy {

namespace {

// OS/2 field offsets (every table version has them).
constexpr std::size_t kFamilyClassOffset = 30;  // sFamilyClass: class in the high byte
constexpr std::size_t kPanoseOffset = 32;       // panose[10]
constexpr std::size_t kPanoseFamilyType = kPanoseOffset;
constexpr std::size_t kPanoseSerifStyle = kPanoseOffset + 1;

constexpr std::uint8_t kPanoseLatinText = 2;
constexpr std::uint8_t kPanoseLatinHandWritten = 3;
constexpr std::uint8_t kPanoseLatinDecorative = 4;
constexpr std::uint8_t kPanoseLatinSymbol = 5;

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
  return (std::uint32_t{bytes[at]} << 24) | (std::uint32_t{bytes[at + 1]} << 16) |
         (std::uint32_t{bytes[at + 2]} << 8) | std::uint32_t{bytes[at + 3]};
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
  return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
}

// IBM font class (the high byte of sFamilyClass). Class 4 (Clarendon) is serif, not slab:
// Georgia and Century carry it.
[[nodiscard]] FontStyleCategory category_from_family_class(std::uint8_t font_class) noexcept {
  switch (font_class) {
    case 1:  // Oldstyle
    case 2:  // Transitional
    case 3:  // Modern
    case 4:  // Clarendon
    case 7:  // Freeform
      return FontStyleCategory::serif;
    case 5:
      return FontStyleCategory::slab_serif;
    case 8:
      return FontStyleCategory::sans_serif;
    case 9:  // Ornamentals
      return FontStyleCategory::decorative;
    case 10:
      return FontStyleCategory::script;
    case 12:  // Symbolic
      return FontStyleCategory::symbol;
    default:
      return FontStyleCategory::unknown;
  }
}

[[nodiscard]] FontStyleCategory category_from_panose(std::uint8_t family_type, std::uint8_t serif_style) noexcept {
  switch (family_type) {
    case kPanoseLatinHandWritten:
      return FontStyleCategory::script;
    case kPanoseLatinDecorative:
      return FontStyleCategory::decorative;
    case kPanoseLatinSymbol:
      return FontStyleCategory::symbol;
    case kPanoseLatinText:
      if (serif_style == 6) {  // Square
        return FontStyleCategory::slab_serif;
      }
      if (serif_style >= 2 && serif_style <= 10) {  // Cove through Triangle
        return FontStyleCategory::serif;
      }
      if (serif_style >= 11 && serif_style <= 15) {  // Normal/Obtuse/Perpendicular Sans, Flared, Rounded
        return FontStyleCategory::sans_serif;
      }
      return FontStyleCategory::unknown;
    default:
      return FontStyleCategory::unknown;
  }
}

}  // namespace

FontStyleCategory font_style_category_from_os2(std::span<const std::uint8_t> os2) noexcept {
  if (os2.size() < kPanoseOffset + 10) {
    return FontStyleCategory::unknown;
  }
  const auto family_type = os2[kPanoseFamilyType];
  // Icon fonts often keep a text class (Segoe MDL2 Assets says Sans Serif); a pictorial PANOSE
  // is the more specific claim.
  if (family_type == kPanoseLatinSymbol) {
    return FontStyleCategory::symbol;
  }
  const auto from_class = category_from_family_class(os2[kFamilyClassOffset]);
  if (from_class != FontStyleCategory::unknown) {
    return from_class;
  }
  return category_from_panose(family_type, os2[kPanoseSerifStyle]);
}

FontStyleCategory resolve_font_style_category(FontStyleCategory os2_category, bool symbol_only,
                                              FontClass name_class) noexcept {
  if (os2_category != FontStyleCategory::unknown) {
    return os2_category;
  }
  if (symbol_only) {
    return FontStyleCategory::symbol;
  }
  switch (name_class) {
    case FontClass::serif:
      return FontStyleCategory::serif;
    case FontClass::sans:
      return FontStyleCategory::sans_serif;
    case FontClass::script:
      return FontStyleCategory::script;
    case FontClass::display:
      return FontStyleCategory::decorative;
    case FontClass::mono:
    case FontClass::unknown:
      break;
  }
  return FontStyleCategory::unknown;
}

std::optional<std::span<const std::uint8_t>> find_sfnt_table(std::span<const std::uint8_t> font_file,
                                                             std::string_view tag) noexcept {
  if (tag.size() != 4 || font_file.size() < 12) {
    return std::nullopt;
  }
  std::size_t directory = 0;
  if (read_u32(font_file, 0) == 0x74746366u) {  // 'ttcf': the first face's directory
    if (font_file.size() < 16) {
      return std::nullopt;
    }
    directory = read_u32(font_file, 12);
    if (directory > font_file.size() || font_file.size() - directory < 12) {
      return std::nullopt;
    }
  }
  const auto version = read_u32(font_file, directory);
  if (version != 0x00010000u && version != 0x4F54544Fu /* OTTO */ && version != 0x74727565u /* true */) {
    return std::nullopt;
  }
  const std::size_t table_count = read_u16(font_file, directory + 4);
  const auto records = directory + 12;
  if (font_file.size() - records < table_count * 16) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < table_count; ++index) {
    const auto record = records + index * 16;
    if (std::string_view(reinterpret_cast<const char*>(font_file.data() + record), 4) != tag) {
      continue;
    }
    const std::size_t offset = read_u32(font_file, record + 8);
    const std::size_t length = read_u32(font_file, record + 12);
    if (offset > font_file.size() || font_file.size() - offset < length) {
      return std::nullopt;
    }
    return font_file.subspan(offset, length);
  }
  return std::nullopt;
}

}  // namespace patchy
