// The font picker's Style filter classification (GitHub issue 570). See docs/font-picker.md,
// "Style classification".
#include "core/font_style_class.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using patchy::FontClass;
using patchy::FontStyleCategory;
using patchy::font_style_category_from_os2;
using patchy::resolve_font_style_category;

// A version 4 OS/2 table with only sFamilyClass and the first two PANOSE bytes set.
std::vector<std::uint8_t> os2_table(std::uint8_t family_class, std::uint8_t panose_family, std::uint8_t serif_style) {
  std::vector<std::uint8_t> table(96, 0);
  table[1] = 4;
  table[30] = family_class;
  table[32] = panose_family;
  table[33] = serif_style;
  return table;
}

FontStyleCategory category(std::uint8_t family_class, std::uint8_t panose_family, std::uint8_t serif_style) {
  return font_style_category_from_os2(os2_table(family_class, panose_family, serif_style));
}

void font_style_class_reads_family_class_then_panose() {
  // IBM family classes.
  CHECK(category(1, 0, 0) == FontStyleCategory::serif);       // Oldstyle (Times New Roman)
  CHECK(category(4, 0, 0) == FontStyleCategory::serif);       // Clarendon (Georgia), not slab
  CHECK(category(7, 0, 0) == FontStyleCategory::serif);       // Freeform
  CHECK(category(5, 0, 0) == FontStyleCategory::slab_serif);  // Rockwell, Courier New
  CHECK(category(8, 0, 0) == FontStyleCategory::sans_serif);
  CHECK(category(9, 0, 0) == FontStyleCategory::decorative);  // Old English Text
  CHECK(category(10, 0, 0) == FontStyleCategory::script);
  CHECK(category(12, 0, 0) == FontStyleCategory::symbol);
  CHECK(category(6, 0, 0) == FontStyleCategory::unknown);  // reserved
  // The class wins over a disagreeing PANOSE, except a pictorial (symbol) PANOSE.
  CHECK(category(8, 2, 2) == FontStyleCategory::sans_serif);
  CHECK(category(8, 5, 10) == FontStyleCategory::symbol);  // Segoe MDL2 Assets
  // PANOSE when the class is 0.
  CHECK(category(0, 3, 11) == FontStyleCategory::script);
  CHECK(category(0, 4, 4) == FontStyleCategory::decorative);
  CHECK(category(0, 5, 0) == FontStyleCategory::symbol);
  CHECK(category(0, 2, 2) == FontStyleCategory::serif);
  CHECK(category(0, 2, 6) == FontStyleCategory::slab_serif);  // Square serifs
  CHECK(category(0, 2, 11) == FontStyleCategory::sans_serif);
  CHECK(category(0, 2, 15) == FontStyleCategory::sans_serif);  // Rounded (Calibri, Carlito)
  CHECK(category(0, 2, 0) == FontStyleCategory::unknown);
  CHECK(category(0, 0, 0) == FontStyleCategory::unknown);
  // A table cut short says nothing.
  CHECK(font_style_category_from_os2(std::vector<std::uint8_t>(40, 8)) == FontStyleCategory::unknown);
}

void font_style_class_falls_back_to_symbol_map_and_name() {
  CHECK(resolve_font_style_category(FontStyleCategory::serif, true, FontClass::sans) == FontStyleCategory::serif);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, true, FontClass::sans) == FontStyleCategory::symbol);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, false, FontClass::sans) ==
        FontStyleCategory::sans_serif);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, false, FontClass::serif) == FontStyleCategory::serif);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, false, FontClass::script) ==
        FontStyleCategory::script);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, false, FontClass::display) ==
        FontStyleCategory::decorative);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, false, FontClass::mono) == FontStyleCategory::unknown);
  CHECK(resolve_font_style_category(FontStyleCategory::unknown, false, FontClass::unknown) ==
        FontStyleCategory::unknown);
}

std::vector<std::uint8_t> read_font(const std::string& relative) {
  const auto path = std::filesystem::path(PATCHY_SOURCE_DIR) / "third_party" / "fonts-web" / relative;
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The bundled web fonts, read from their own files.
void font_style_class_classifies_bundled_fonts() {
  struct Expected {
    const char* file;
    FontStyleCategory category;
  };
  constexpr std::array cases{
      Expected{"liberation/LiberationSerif-Regular.ttf", FontStyleCategory::serif},      // class 1
      Expected{"liberation/LiberationSans-Regular.ttf", FontStyleCategory::sans_serif},  // class 8
      Expected{"noto_serif/NotoSerif-Regular.ttf", FontStyleCategory::serif},            // PANOSE 2/2
      Expected{"noto_sans/NotoSans-Regular.ttf", FontStyleCategory::sans_serif},         // PANOSE 2/11
      Expected{"carlito/Carlito-Regular.ttf", FontStyleCategory::sans_serif},            // PANOSE 2/15
      Expected{"caveat/Caveat-Regular.ttf", FontStyleCategory::unknown},                 // all zero
  };
  for (const auto& expected : cases) {
    const auto bytes = read_font(expected.file);
    const auto os2 = patchy::find_sfnt_table(bytes, "OS/2");
    CHECK(os2.has_value());
    CHECK(font_style_category_from_os2(*os2) == expected.category);
    CHECK(!patchy::find_sfnt_table(bytes, "fvar").has_value());  // none of them is variable
  }
  // Not an sfnt, a truncated directory, a bad tag.
  const std::vector<std::uint8_t> junk(64, 0x41);
  CHECK(!patchy::find_sfnt_table(junk, "OS/2").has_value());
  auto truncated = read_font("liberation/LiberationSans-Regular.ttf");
  truncated.resize(20);
  CHECK(!patchy::find_sfnt_table(truncated, "OS/2").has_value());
  CHECK(!patchy::find_sfnt_table(read_font("liberation/LiberationSans-Regular.ttf"), "OS2").has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> font_style_class_tests() {
  return {
      {"font_style_class_reads_family_class_then_panose", font_style_class_reads_family_class_then_panose},
      {"font_style_class_falls_back_to_symbol_map_and_name", font_style_class_falls_back_to_symbol_map_and_name},
      {"font_style_class_classifies_bundled_fonts", font_style_class_classifies_bundled_fonts},
  };
}
