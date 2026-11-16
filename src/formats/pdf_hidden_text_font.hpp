#pragma once

// The font behind a PDF page's invisible text (docs/pdf-searchable-text.md). Text a reader
// can find and select but never sees needs a font to name its characters and their widths,
// and nothing else: this one is a TrueType file built here, byte by byte, with one glyph, a
// square one em wide, so every character advances by exactly its size. No other project's
// font file is read or copied. Qt-free and deterministic.

#include <cstdint>
#include <vector>

namespace patchy::pdf {

inline constexpr int kHiddenTextFontUnitsPerEm = 1000;

// The font file: glyf, head, hhea, hmtx, loca and maxp, the six tables a TrueType font
// embedded as a CIDFontType2 needs (ISO 32000-1, 9.9). 276 bytes.
[[nodiscard]] std::vector<std::uint8_t> hidden_text_font();

}  // namespace patchy::pdf
