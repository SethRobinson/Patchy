#pragma once

#include "formats/pdf_file.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// PDF font resources (ISO 32000-1 clause 9.5-9.10), reduced to what an importer
// needs: turn the bytes inside a Tj string into characters, advances, and a font
// name Patchy can resolve to something installed.
//
// This is deliberately NOT a font rasterizer. Embedded font programs are never
// parsed; the glyphs are drawn by whatever installed face the family name resolves
// to, which is what Affinity does too and why it prompts about missing fonts.

namespace patchy::pdf {

enum class FontKind {
  Type1,
  TrueType,
  Type3,
  Type0,  // composite: multi-byte codes indexing CIDs
  Unknown,
};

// One decoded character: what the file said, what it means, and how far the text
// position advances afterwards.
struct Glyph {
  std::uint32_t code{0};
  // U+FFFD when the font gave no usable mapping. The importer keeps such runs as
  // shapes rather than pretending it recovered the text.
  char32_t unicode{0xFFFD};
  // Advance in text-space units (glyph space / 1000), before font size, character
  // spacing, word spacing, and horizontal scale are applied.
  double width{0.0};
  // True for the single-byte code 32 in a simple font, which /Tw applies to.
  bool is_word_space{false};
};

struct Font {
  FontKind kind{FontKind::Unknown};
  // Exactly as written, subset tag included: "AAAAAA+SegoeUI-Bold".
  std::string base_font;
  // Subset tag stripped and the style suffix split off: "SegoeUI" + "Bold".
  std::string family;
  std::string style;
  bool bold{false};
  bool italic{false};
  bool serif{false};
  bool fixed_pitch{false};
  bool symbolic{false};
  // Codes are two bytes wide (Identity-H and the other CMaps we accept).
  bool two_byte{false};
  // No /Widths and no /W: advances are unknown, so the importer must not try to
  // correct tracking against them.
  bool has_widths{false};
  double default_width{500.0};
  // Type3 fonts carry their own glyph procedures and matrix; they are rasterized
  // rather than turned into text layers.
  double font_matrix[6]{0.001, 0.0, 0.0, 0.001, 0.0, 0.0};

  [[nodiscard]] std::vector<Glyph> decode(std::string_view bytes) const;
  // Sum of the advances, in text-space units.
  [[nodiscard]] double string_width(std::string_view bytes) const;

  // code -> advance (glyph space, 1/1000 em)
  std::map<std::uint32_t, double> widths;
  // code -> Unicode, from /ToUnicode when present, else from the encoding
  std::map<std::uint32_t, char32_t> to_unicode;
};

// Reads one font resource. Never fails: an unreadable font still yields a Font with
// enough defaults that text keeps its position, because losing the layout is worse
// than losing the characters.
[[nodiscard]] Font load_font(const File& file, const Object& font_dict);

// "AAAAAA+SegoeUI-Bold" -> family "SegoeUI", style "Bold", bold true.
// The subset tag is exactly six uppercase letters and a '+' (clause 9.6.4).
struct ParsedFontName {
  std::string family;
  std::string style;
  bool bold{false};
  bool italic{false};
};
[[nodiscard]] ParsedFontName parse_base_font_name(std::string_view base_font);

// Adobe glyph name to Unicode: the algorithmic uniXXXX / uXXXXXX forms, a table of
// the names the standard encodings use, and a single-character name as itself.
// Returns 0 when the name carries no Unicode meaning (gXX, cidXX, index names).
[[nodiscard]] char32_t unicode_for_glyph_name(std::string_view name);

// Parses a /ToUnicode CMap's bfchar and bfrange sections into code -> Unicode.
[[nodiscard]] std::map<std::uint32_t, char32_t> parse_to_unicode_cmap(std::span<const std::uint8_t> data);

}  // namespace patchy::pdf
