#include "formats/pdf_fonts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <unordered_map>

namespace patchy::pdf {
namespace {

// --- Encodings (Annex D) -------------------------------------------------------
//
// Stored as the deltas from ASCII/Latin-1 rather than as three full 256-entry
// tables: WinAnsi IS Latin-1 above 0x9F, and all three are ASCII across 0x20-0x7E
// bar a couple of quote positions. Less data to get wrong, and each block below is
// independently checkable against the spec's tables.

// WinAnsi 0x80-0x9F, the CP1252 block. Everything else in WinAnsi is Latin-1.
constexpr std::array<char32_t, 32> kWinAnsiHighBlock = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80-87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,  // 88-8F
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90-97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,  // 98-9F
};

// MacRoman 0x80-0xFF.
constexpr std::array<char32_t, 128> kMacRomanHighBlock = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,  // 80
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,  // 88
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,  // 90
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,  // 98
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,  // A0
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,  // A8
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,  // B0
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,  // B8
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,  // C0
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,  // C8
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,  // D0
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,  // D8
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,  // E0
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,  // E8
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,  // F0
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,  // F8
};

// StandardEncoding above 0xA0 is sparse; the pairs it does define are listed.
// Both members are char32_t so the brace initializers below cannot narrow; the
// codes really are single bytes.
constexpr std::array<std::pair<char32_t, char32_t>, 46> kStandardHighPairs = {{
    {0xA1, 0x00A1}, {0xA2, 0x00A2}, {0xA3, 0x00A3}, {0xA4, 0x2044}, {0xA5, 0x00A5}, {0xA6, 0x0192},
    {0xA7, 0x00A7}, {0xA8, 0x00A4}, {0xA9, 0x0027}, {0xAA, 0x201C}, {0xAB, 0x00AB}, {0xAC, 0x2039},
    {0xAD, 0x203A}, {0xAE, 0xFB01}, {0xAF, 0xFB02}, {0xB1, 0x2013}, {0xB2, 0x2020}, {0xB3, 0x2021},
    {0xB4, 0x00B7}, {0xB6, 0x00B6}, {0xB7, 0x2022}, {0xB8, 0x201A}, {0xB9, 0x201E}, {0xBA, 0x201D},
    {0xBB, 0x00BB}, {0xBC, 0x2026}, {0xBD, 0x2030}, {0xBF, 0x00BF}, {0xC1, 0x0060}, {0xC2, 0x00B4},
    {0xC3, 0x02C6}, {0xC4, 0x02DC}, {0xC5, 0x00AF}, {0xC6, 0x02D8}, {0xC7, 0x02D9}, {0xC8, 0x00A8},
    {0xCA, 0x02DA}, {0xCB, 0x00B8}, {0xCD, 0x02DD}, {0xCE, 0x02DB}, {0xCF, 0x02C7}, {0xD0, 0x2014},
    {0xE1, 0x00C6}, {0xE9, 0x00D8}, {0xF1, 0x00E6}, {0xF9, 0x00F8},
}};

enum class BaseEncoding { Standard, WinAnsi, MacRoman, PdfDoc };

char32_t base_encoding_unicode(BaseEncoding encoding, std::uint8_t code) {
  if (code < 0x20) {
    return 0;
  }
  if (code <= 0x7E) {
    // Standard differs from ASCII at exactly two quote positions (Annex D.2).
    if (encoding == BaseEncoding::Standard) {
      if (code == 0x27) {
        return 0x2019;  // quoteright
      }
      if (code == 0x60) {
        return 0x2018;  // quoteleft
      }
    }
    return code;
  }
  switch (encoding) {
    case BaseEncoding::WinAnsi:
      if (code <= 0x9F) {
        return kWinAnsiHighBlock[static_cast<std::size_t>(code - 0x80)];
      }
      return code;  // 0xA0-0xFF is Latin-1, i.e. the code point itself
    case BaseEncoding::MacRoman: return kMacRomanHighBlock[static_cast<std::size_t>(code - 0x80)];
    case BaseEncoding::PdfDoc:
      // PDFDocEncoding matches Latin-1 in this range closely enough for text
      // extraction; it is only ever used for metadata strings, never page content.
      return code >= 0xA0 ? code : 0;
    case BaseEncoding::Standard: {
      for (const auto& [key, value] : kStandardHighPairs) {
        if (key == code) {
          return value;
        }
      }
      return 0;
    }
  }
  return 0;
}

BaseEncoding base_encoding_from_name(std::string_view name, BaseEncoding fallback) {
  if (name == "WinAnsiEncoding") {
    return BaseEncoding::WinAnsi;
  }
  if (name == "MacRomanEncoding") {
    return BaseEncoding::MacRoman;
  }
  if (name == "StandardEncoding" || name == "MacExpertEncoding") {
    // MacExpert is an old-style-figures set we do not model; Standard keeps the
    // ASCII range right, which is the part that matters.
    return BaseEncoding::Standard;
  }
  if (name == "PDFDocEncoding") {
    return BaseEncoding::PdfDoc;
  }
  return fallback;
}

// The glyph names the standard encodings use for non-ASCII characters, plus the
// ASCII punctuation names. Anything algorithmic (uniXXXX) is handled separately.
const std::unordered_map<std::string_view, char32_t>& glyph_name_table() {
  static const std::unordered_map<std::string_view, char32_t> table = {
      {"space", 0x0020},        {"exclam", 0x0021},       {"quotedbl", 0x0022},    {"numbersign", 0x0023},
      {"dollar", 0x0024},       {"percent", 0x0025},      {"ampersand", 0x0026},   {"quotesingle", 0x0027},
      {"quoteright", 0x2019},   {"parenleft", 0x0028},    {"parenright", 0x0029},  {"asterisk", 0x002A},
      {"plus", 0x002B},         {"comma", 0x002C},        {"hyphen", 0x002D},      {"period", 0x002E},
      {"slash", 0x002F},        {"zero", 0x0030},         {"one", 0x0031},         {"two", 0x0032},
      {"three", 0x0033},        {"four", 0x0034},         {"five", 0x0035},        {"six", 0x0036},
      {"seven", 0x0037},        {"eight", 0x0038},        {"nine", 0x0039},        {"colon", 0x003A},
      {"semicolon", 0x003B},    {"less", 0x003C},         {"equal", 0x003D},       {"greater", 0x003E},
      {"question", 0x003F},     {"at", 0x0040},           {"bracketleft", 0x005B}, {"backslash", 0x005C},
      {"bracketright", 0x005D}, {"asciicircum", 0x005E},  {"underscore", 0x005F},  {"grave", 0x0060},
      {"quoteleft", 0x2018},    {"braceleft", 0x007B},    {"bar", 0x007C},         {"braceright", 0x007D},
      {"asciitilde", 0x007E},
      // Latin-1 and the common typographic set.
      {"exclamdown", 0x00A1},   {"cent", 0x00A2},         {"sterling", 0x00A3},    {"fraction", 0x2044},
      {"yen", 0x00A5},          {"florin", 0x0192},       {"section", 0x00A7},     {"currency", 0x00A4},
      {"quotedblleft", 0x201C}, {"guillemotleft", 0x00AB},{"guilsinglleft", 0x2039},
      {"guilsinglright", 0x203A}, {"fi", 0xFB01},         {"fl", 0xFB02},          {"endash", 0x2013},
      {"dagger", 0x2020},       {"daggerdbl", 0x2021},    {"periodcentered", 0x00B7}, {"paragraph", 0x00B6},
      {"bullet", 0x2022},       {"quotesinglbase", 0x201A}, {"quotedblbase", 0x201E},
      {"quotedblright", 0x201D}, {"guillemotright", 0x00BB}, {"ellipsis", 0x2026}, {"perthousand", 0x2030},
      {"questiondown", 0x00BF}, {"acute", 0x00B4},        {"circumflex", 0x02C6},  {"tilde", 0x02DC},
      {"macron", 0x00AF},       {"breve", 0x02D8},        {"dotaccent", 0x02D9},   {"dieresis", 0x00A8},
      {"ring", 0x02DA},         {"cedilla", 0x00B8},      {"hungarumlaut", 0x02DD},{"ogonek", 0x02DB},
      {"caron", 0x02C7},        {"emdash", 0x2014},       {"AE", 0x00C6},          {"ordfeminine", 0x00AA},
      {"Lslash", 0x0141},       {"Oslash", 0x00D8},       {"OE", 0x0152},          {"ordmasculine", 0x00BA},
      {"ae", 0x00E6},           {"dotlessi", 0x0131},     {"lslash", 0x0142},      {"oslash", 0x00F8},
      {"oe", 0x0153},           {"germandbls", 0x00DF},   {"degree", 0x00B0},      {"plusminus", 0x00B1},
      {"mu", 0x00B5},           {"trademark", 0x2122},    {"registered", 0x00AE},  {"copyright", 0x00A9},
      {"logicalnot", 0x00AC},   {"divide", 0x00F7},       {"multiply", 0x00D7},    {"onequarter", 0x00BC},
      {"onehalf", 0x00BD},      {"threequarters", 0x00BE},{"onesuperior", 0x00B9}, {"twosuperior", 0x00B2},
      {"threesuperior", 0x00B3},{"brokenbar", 0x00A6},    {"nbspace", 0x00A0},     {"euro", 0x20AC},
      {"Euro", 0x20AC},         {"minus", 0x2212},        {"Delta", 0x2206},       {"Omega", 0x03A9},
      {"pi", 0x03C0},           {"radical", 0x221A},      {"infinity", 0x221E},    {"notequal", 0x2260},
      {"lessequal", 0x2264},    {"greaterequal", 0x2265}, {"partialdiff", 0x2202}, {"summation", 0x2211},
      {"product", 0x220F},      {"integral", 0x222B},     {"approxequal", 0x2248}, {"lozenge", 0x25CA},
      {"apple", 0xF8FF},
  };
  return table;
}

// The accented Latin names follow a regular "letter + accent" scheme, so they are
// composed rather than tabulated one by one.
char32_t composed_accented_name(std::string_view name) {
  struct Accent {
    std::string_view suffix;
    // Index into the per-letter tables below.
    int slot;
  };
  static constexpr std::array<Accent, 8> kAccents = {{
      {"acute", 0}, {"grave", 1}, {"circumflex", 2}, {"dieresis", 3},
      {"tilde", 4}, {"ring", 5}, {"cedilla", 6}, {"caron", 7},
  }};
  for (const auto& accent : kAccents) {
    if (name.size() <= accent.suffix.size() || !name.ends_with(accent.suffix)) {
      continue;
    }
    const auto base = name.substr(0, name.size() - accent.suffix.size());
    if (base.size() != 1) {
      continue;
    }
    const char letter = base[0];
    const bool upper = letter >= 'A' && letter <= 'Z';
    const char lower_letter = upper ? static_cast<char>(letter - 'A' + 'a') : letter;
    // Only the combinations the Latin-1 and Latin Extended-A blocks actually define.
    struct Entry {
      char letter;
      char32_t upper_code;
      char32_t lower_code;
    };
    static constexpr std::array<std::array<Entry, 6>, 8> kTables = {{
        // acute
        {{{'a', 0x00C1, 0x00E1}, {'e', 0x00C9, 0x00E9}, {'i', 0x00CD, 0x00ED}, {'o', 0x00D3, 0x00F3},
          {'u', 0x00DA, 0x00FA}, {'y', 0x00DD, 0x00FD}}},
        // grave
        {{{'a', 0x00C0, 0x00E0}, {'e', 0x00C8, 0x00E8}, {'i', 0x00CC, 0x00EC}, {'o', 0x00D2, 0x00F2},
          {'u', 0x00D9, 0x00F9}, {0, 0, 0}}},
        // circumflex
        {{{'a', 0x00C2, 0x00E2}, {'e', 0x00CA, 0x00EA}, {'i', 0x00CE, 0x00EE}, {'o', 0x00D4, 0x00F4},
          {'u', 0x00DB, 0x00FB}, {0, 0, 0}}},
        // dieresis
        {{{'a', 0x00C4, 0x00E4}, {'e', 0x00CB, 0x00EB}, {'i', 0x00CF, 0x00EF}, {'o', 0x00D6, 0x00F6},
          {'u', 0x00DC, 0x00FC}, {'y', 0x0178, 0x00FF}}},
        // tilde
        {{{'a', 0x00C3, 0x00E3}, {'n', 0x00D1, 0x00F1}, {'o', 0x00D5, 0x00F5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
        // ring
        {{{'a', 0x00C5, 0x00E5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
        // cedilla
        {{{'c', 0x00C7, 0x00E7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
        // caron
        {{{'s', 0x0160, 0x0161}, {'z', 0x017D, 0x017E}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
    }};
    for (const auto& entry : kTables[static_cast<std::size_t>(accent.slot)]) {
      if (entry.letter == lower_letter) {
        return upper ? entry.upper_code : entry.lower_code;
      }
    }
  }
  return 0;
}

int hex_digit(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

// Big-endian bytes to a code, as CMap ranges and bfchar keys are written.
std::uint32_t code_from_bytes(std::string_view bytes) {
  std::uint32_t value = 0;
  for (const char byte : bytes) {
    value = (value << 8) | static_cast<unsigned char>(byte);
  }
  return value;
}

// A bfchar/bfrange destination is UTF-16BE. Take the first code point; ligature
// destinations that expand to several characters keep only their first, which is
// what every text extractor does short of full ligature expansion.
char32_t utf16be_first_code_point(std::string_view bytes) {
  if (bytes.size() < 2) {
    return 0;
  }
  const auto unit = static_cast<char32_t>((static_cast<unsigned char>(bytes[0]) << 8) |
                                          static_cast<unsigned char>(bytes[1]));
  if (unit >= 0xD800 && unit <= 0xDBFF && bytes.size() >= 4) {
    const auto low = static_cast<char32_t>((static_cast<unsigned char>(bytes[2]) << 8) |
                                           static_cast<unsigned char>(bytes[3]));
    if (low >= 0xDC00 && low <= 0xDFFF) {
      return 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
    }
  }
  return unit;
}

}  // namespace

char32_t unicode_for_glyph_name(std::string_view name) {
  if (name.empty()) {
    return 0;
  }
  // A name may carry a suffix like "a.sc" or "one.oldstyle"; the base name decides.
  if (const auto dot = name.find('.'); dot != std::string_view::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  if (name.size() == 1 && static_cast<unsigned char>(name[0]) < 0x80) {
    return static_cast<char32_t>(name[0]);
  }

  const auto& table = glyph_name_table();
  if (const auto it = table.find(name); it != table.end()) {
    return it->second;
  }

  // uniXXXX (one or more four-digit units) and uXXXX..uXXXXXX (clause 9.10.2).
  const auto parse_hex = [](std::string_view text) -> char32_t {
    char32_t value = 0;
    for (const char character : text) {
      const int digit = hex_digit(character);
      if (digit < 0) {
        return 0;
      }
      value = value * 16 + static_cast<char32_t>(digit);
    }
    return value;
  };
  if (name.starts_with("uni") && name.size() >= 7 && (name.size() - 3) % 4 == 0) {
    return parse_hex(name.substr(3, 4));
  }
  if (name.starts_with("u") && name.size() >= 5 && name.size() <= 7) {
    return parse_hex(name.substr(1));
  }

  return composed_accented_name(name);
}

ParsedFontName parse_base_font_name(std::string_view base_font) {
  ParsedFontName parsed;
  // A subset prefix is exactly six uppercase letters and a '+'.
  if (base_font.size() > 7 && base_font[6] == '+') {
    const auto tag = base_font.substr(0, 6);
    if (std::all_of(tag.begin(), tag.end(), [](char character) { return character >= 'A' && character <= 'Z'; })) {
      base_font.remove_prefix(7);
    }
  }

  // The style rides after a comma or hyphen: "Arial-BoldItalic", "Arial,Bold".
  std::string_view family = base_font;
  std::string_view style;
  if (const auto separator = base_font.find_first_of(",-"); separator != std::string_view::npos) {
    family = base_font.substr(0, separator);
    style = base_font.substr(separator + 1);
  }

  const auto contains_insensitive = [](std::string_view haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) {
      return false;
    }
    for (std::size_t offset = 0; offset + needle.size() <= haystack.size(); ++offset) {
      bool matched = true;
      for (std::size_t index = 0; index < needle.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(haystack[offset + index])) !=
            std::tolower(static_cast<unsigned char>(needle[index]))) {
          matched = false;
          break;
        }
      }
      if (matched) {
        return true;
      }
    }
    return false;
  };

  // Some producers write no separator at all ("ArialBold"), so the whole name is
  // searched when the split found nothing.
  const std::string_view style_source = style.empty() ? base_font : style;
  parsed.bold = contains_insensitive(style_source, "bold") || contains_insensitive(style_source, "black") ||
                contains_insensitive(style_source, "heavy");
  parsed.italic = contains_insensitive(style_source, "italic") || contains_insensitive(style_source, "oblique");

  parsed.family = std::string(family);
  parsed.style = std::string(style);
  return parsed;
}

std::map<std::uint32_t, char32_t> parse_to_unicode_cmap(std::span<const std::uint8_t> data) {
  std::map<std::uint32_t, char32_t> mapping;
  Lexer lexer(data);
  // bfchar entries come in pairs and bfrange entries in triples, both bracketed by
  // begin/end keywords. Operands accumulate until a complete entry is in hand; the
  // counts the sections declare are ignored because they are routinely wrong.
  enum class Section { None, BfChar, BfRange } section = Section::None;
  std::vector<Object> pending;
  while (true) {
    const auto before = lexer.position();
    auto token = lexer.next();
    if (!token.has_value() || lexer.position() == before) {
      break;
    }
    if (token->is_keyword()) {
      const auto& keyword = token->keyword;
      if (keyword == "beginbfchar") {
        section = Section::BfChar;
      } else if (keyword == "beginbfrange") {
        section = Section::BfRange;
      } else if (keyword == "endbfchar" || keyword == "endbfrange") {
        section = Section::None;
      }
      pending.clear();
      continue;
    }
    if (section == Section::None) {
      pending.clear();
      continue;
    }

    pending.push_back(std::move(token->object));
    if (section == Section::BfChar && pending.size() == 2) {
      if (pending[0].is_string() && pending[1].is_string()) {
        mapping[code_from_bytes(pending[0].string())] = utf16be_first_code_point(pending[1].string());
      }
      pending.clear();
      continue;
    }
    if (section == Section::BfRange && pending.size() == 3) {
      if (pending[0].is_string() && pending[1].is_string()) {
        const auto low = code_from_bytes(pending[0].string());
        const auto high = code_from_bytes(pending[1].string());
        // A pathological range would otherwise allocate forever.
        const auto span = std::min<std::uint32_t>(high >= low ? high - low : 0, 65535);
        if (pending[2].is_string()) {
          const auto start = utf16be_first_code_point(pending[2].string());
          for (std::uint32_t offset = 0; offset <= span; ++offset) {
            mapping[low + offset] = static_cast<char32_t>(start + offset);
          }
        } else if (const auto* array = pending[2].array(); array != nullptr) {
          for (std::uint32_t offset = 0; offset <= span && offset < array->size(); ++offset) {
            const auto& entry = (*array)[offset];
            if (entry.is_string()) {
              mapping[low + offset] = utf16be_first_code_point(entry.string());
            }
          }
        }
      }
      pending.clear();
    }
  }
  return mapping;
}

std::vector<Glyph> Font::decode(std::string_view bytes) const {
  std::vector<Glyph> glyphs;
  const std::size_t step = two_byte ? 2 : 1;
  glyphs.reserve(bytes.size() / step + 1);
  for (std::size_t index = 0; index + step <= bytes.size(); index += step) {
    Glyph glyph;
    glyph.code = two_byte ? static_cast<std::uint32_t>((static_cast<unsigned char>(bytes[index]) << 8) |
                                                       static_cast<unsigned char>(bytes[index + 1]))
                          : static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index]));
    // Word spacing applies only to the single-byte code 32 (clause 9.3.3).
    glyph.is_word_space = !two_byte && glyph.code == 32;

    if (const auto it = to_unicode.find(glyph.code); it != to_unicode.end() && it->second != 0) {
      glyph.unicode = it->second;
    } else {
      glyph.unicode = 0xFFFD;
    }

    if (const auto it = widths.find(glyph.code); it != widths.end()) {
      glyph.width = it->second / 1000.0;
    } else {
      glyph.width = default_width / 1000.0;
    }
    glyphs.push_back(glyph);
  }
  return glyphs;
}

double Font::string_width(std::string_view bytes) const {
  double total = 0.0;
  for (const auto& glyph : decode(bytes)) {
    total += glyph.width;
  }
  return total;
}

Font load_font(const File& file, const Object& font_dict) {
  Font font;
  const auto subtype = file.get(font_dict, "Subtype").name();
  if (subtype == "Type1" || subtype == "MMType1") {
    font.kind = FontKind::Type1;
  } else if (subtype == "TrueType") {
    font.kind = FontKind::TrueType;
  } else if (subtype == "Type3") {
    font.kind = FontKind::Type3;
  } else if (subtype == "Type0") {
    font.kind = FontKind::Type0;
  }

  font.base_font = std::string(file.get(font_dict, "BaseFont").name());
  const auto parsed = parse_base_font_name(font.base_font);
  font.family = parsed.family;
  font.style = parsed.style;
  font.bold = parsed.bold;
  font.italic = parsed.italic;

  // A composite font wraps a descendant that holds the real metrics.
  Object metrics_source = font_dict;
  if (font.kind == FontKind::Type0) {
    font.two_byte = true;  // corrected below if the CMap says otherwise
    const auto& descendants = file.get(font_dict, "DescendantFonts");
    if (const auto* array = descendants.array(); array != nullptr && !array->empty()) {
      metrics_source = file.resolve((*array)[0]);
    }
    const auto& encoding = file.get(font_dict, "Encoding");
    const auto encoding_name = encoding.name();
    if (!encoding_name.empty() && encoding_name != "Identity-H" && encoding_name != "Identity-V") {
      // A predefined CJK CMap. Its code widths vary by range; two bytes is right for
      // the overwhelming majority and keeps positions sane for the rest.
      font.two_byte = true;
    }
  }

  const auto& descriptor = file.get(metrics_source, "FontDescriptor");
  if (descriptor.is_dictionary()) {
    const auto flags = static_cast<std::uint32_t>(file.get(descriptor, "Flags").integer(0));
    font.fixed_pitch = (flags & 0x1U) != 0;
    font.serif = (flags & 0x2U) != 0;
    font.symbolic = (flags & 0x4U) != 0;
    if ((flags & 0x40000U) != 0) {  // ForceBold
      font.bold = true;
    }
    if (file.get(descriptor, "ItalicAngle").number(0.0) != 0.0) {
      font.italic = true;
    }
    if (const auto weight = file.get(descriptor, "StemV").number(0.0); weight > 120.0) {
      font.bold = true;
    }
    if (const auto missing = file.get(descriptor, "MissingWidth"); missing.is_number()) {
      font.default_width = missing.number(font.default_width);
    }
  }

  // --- Widths --------------------------------------------------------------
  if (font.kind == FontKind::Type0) {
    font.default_width = file.get(metrics_source, "DW").number(1000.0);
    // /W is [ c [w w ...] cFirst cLast w ... ] (clause 9.7.4.3).
    const auto& widths = file.get(metrics_source, "W");
    if (const auto* array = widths.array(); array != nullptr) {
      for (std::size_t index = 0; index < array->size();) {
        const auto first = file.resolve((*array)[index]);
        if (!first.is_number() || index + 1 >= array->size()) {
          break;
        }
        const auto start = static_cast<std::uint32_t>(first.integer(0));
        const auto& second = file.resolve((*array)[index + 1]);
        if (const auto* list = second.array(); list != nullptr) {
          for (std::size_t offset = 0; offset < list->size(); ++offset) {
            font.widths[start + static_cast<std::uint32_t>(offset)] = file.resolve((*list)[offset]).number(0.0);
          }
          font.has_widths = font.has_widths || !list->empty();
          index += 2;
          continue;
        }
        if (index + 2 >= array->size()) {
          break;
        }
        const auto last = static_cast<std::uint32_t>(second.integer(0));
        const auto width = file.resolve((*array)[index + 2]).number(0.0);
        // Cap a nonsense range rather than filling memory with it.
        const auto count = last >= start ? std::min<std::uint32_t>(last - start, 65535) : 0;
        for (std::uint32_t offset = 0; offset <= count; ++offset) {
          font.widths[start + offset] = width;
        }
        font.has_widths = true;
        index += 3;
      }
    }
  } else {
    const auto first_char = static_cast<std::uint32_t>(file.get(font_dict, "FirstChar").integer(0));
    const auto& widths = file.get(font_dict, "Widths");
    if (const auto* array = widths.array(); array != nullptr && !array->empty()) {
      for (std::size_t offset = 0; offset < array->size(); ++offset) {
        font.widths[first_char + static_cast<std::uint32_t>(offset)] = file.resolve((*array)[offset]).number(0.0);
      }
      font.has_widths = true;
    }
  }

  if (font.kind == FontKind::Type3) {
    if (auto matrix = file.numbers(font_dict.get("FontMatrix")); matrix.size() >= 6) {
      std::copy_n(matrix.begin(), 6, std::begin(font.font_matrix));
    }
    // Type3 widths are in glyph space, which /FontMatrix scales; normalize them to
    // the 1/1000 units every other font reports so the interpreter stays uniform.
    const double scale = font.font_matrix[0] * 1000.0;
    for (auto& [code, width] : font.widths) {
      width *= scale;
    }
    font.default_width = 0.0;
  }

  // --- Character codes to Unicode -----------------------------------------
  // The encoding first, so a partial /ToUnicode can override it entry by entry.
  if (font.kind != FontKind::Type0) {
    // A symbolic font's built-in encoding is inside the font program, which is not
    // parsed. Standard is the least-wrong starting point and /Differences usually
    // supplies the real names anyway.
    auto base = font.symbolic ? BaseEncoding::Standard : BaseEncoding::Standard;
    const auto& encoding = file.get(font_dict, "Encoding");
    if (encoding.is_name()) {
      base = base_encoding_from_name(encoding.name(), base);
    } else if (encoding.is_dictionary()) {
      base = base_encoding_from_name(file.get(encoding, "BaseEncoding").name(), base);
    }
    for (std::uint32_t code = 0; code < 256; ++code) {
      if (const auto unicode = base_encoding_unicode(base, static_cast<std::uint8_t>(code)); unicode != 0) {
        font.to_unicode[code] = unicode;
      }
    }
    if (encoding.is_dictionary()) {
      // /Differences is [ startCode /name /name ... startCode /name ... ].
      const auto& differences = file.get(encoding, "Differences");
      if (const auto* array = differences.array(); array != nullptr) {
        std::uint32_t code = 0;
        for (const auto& entry_object : *array) {
          const auto& entry = file.resolve(entry_object);
          if (entry.is_number()) {
            code = static_cast<std::uint32_t>(entry.integer(0));
            continue;
          }
          if (!entry.is_name()) {
            continue;
          }
          if (const auto unicode = unicode_for_glyph_name(entry.name()); unicode != 0) {
            font.to_unicode[code] = unicode;
          } else {
            font.to_unicode.erase(code);  // a name with no Unicode meaning (gXX, cidXX)
          }
          ++code;
        }
      }
    }
  }

  // /ToUnicode is authoritative wherever it speaks, which for a subset font is
  // everywhere and is the only thing that makes its text recoverable at all.
  const auto& to_unicode = file.get(font_dict, "ToUnicode");
  if (to_unicode.stream() != nullptr) {
    const auto data = file.stream_data(to_unicode);
    if (!data.data.empty()) {
      for (const auto& [code, unicode] : parse_to_unicode_cmap(data.data)) {
        if (unicode != 0) {
          font.to_unicode[code] = unicode;
        }
      }
    }
  }

  return font;
}

}  // namespace patchy::pdf
