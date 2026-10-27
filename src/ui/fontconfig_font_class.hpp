#pragma once

// Linux only (compiled when PATCHY_HAVE_FONTCONFIG_QUERY is defined, see CMakeLists): asks the
// user's fontconfig which font type a missing family is and which installed families fontconfig
// itself would draw that type in (GitHub issue 463). libfontconfig is loaded with dlopen at run
// time, like libheif: Qt's Linux font database already loads the same library, and nothing new is
// linked or needed at build time (Flatpak SDK, AppImage container, native presets). See
// docs/font-resolution.md, "Font types".

#include "core/font_substitutes.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ui {

struct FontconfigFontClass {
  FontClass font_class{FontClass::unknown};
  // false when the type is only fontconfig's catch-all: a family no rule names gets sans-serif
  // appended, so sans-serif alone says nothing about the font.
  bool definite{false};
};

// The generic family (serif, sans-serif, monospace, cursive, fantasy) fontconfig's rules give
// `family` (45-latin.conf and its kind), read from the pattern after FcConfigSubstitute. nullopt
// when libfontconfig cannot be loaded.
[[nodiscard]] std::optional<FontconfigFontClass> fontconfig_font_class(std::string_view family);

// fontconfig's own order of installed families for a font type (FcFontSort on the generic
// family), without repeats. Empty for unknown, script and display types without a generic, or
// when libfontconfig cannot be loaded.
[[nodiscard]] std::vector<std::string> fontconfig_families_for_class(FontClass font_class);

}  // namespace patchy::ui
