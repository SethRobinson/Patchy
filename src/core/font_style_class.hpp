#pragma once

// The design category a font file states about itself (GitHub issue 570): the font picker's
// Style filter. Read from the OS/2 table (IBM sFamilyClass, then PANOSE), with the name-based
// FontClass as the last resort. Qt-free; the UI reads the tables through QRawFont. See
// docs/font-picker.md, "Style classification".

#include "core/font_substitutes.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace patchy {

enum class FontStyleCategory : std::uint8_t { unknown, serif, sans_serif, slab_serif, script, decorative, symbol };

// The category the OS/2 table states: sFamilyClass first (1-4 and 7 serif, 5 slab, 8 sans,
// 9 decorative, 10 script, 12 symbol), except that PANOSE family type 5 (Latin Symbol) always
// wins; then PANOSE (family type 3 script, 4 decorative; Latin Text with serif style 2-10 serif,
// 6 slab, 11-15 sans). unknown for a short table or when neither field says.
[[nodiscard]] FontStyleCategory font_style_category_from_os2(std::span<const std::uint8_t> os2) noexcept;

// The final category: the OS/2 answer, else a symbol-only character map (`symbol_only`: the
// family claims the Symbol writing system and not Latin), else the name-based class (script and
// display map to script and decorative; mono and unknown say nothing about style).
[[nodiscard]] FontStyleCategory resolve_font_style_category(FontStyleCategory os2_category, bool symbol_only,
                                                            FontClass name_class) noexcept;

// The bytes of table `tag` (four characters) in an sfnt file (TrueType or OpenType; a collection's
// first face). nullopt when the file is not an sfnt, the table is missing or runs past the end.
[[nodiscard]] std::optional<std::span<const std::uint8_t>> find_sfnt_table(std::span<const std::uint8_t> font_file,
                                                                           std::string_view tag) noexcept;

}  // namespace patchy
