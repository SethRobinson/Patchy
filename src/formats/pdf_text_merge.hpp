#pragma once

#include <cstdint>
#include <vector>

namespace patchy::pdf {

// Qt's PDF engine positions every glyph with its own "x y Td <g> Tj", so every
// importer (Patchy's included, and Affinity's) reads each letter as a separate text
// object. This pass rewrites each page's content so consecutive glyphs of one font on
// one baseline become a single "[<g1> k1 <g2> ...] TJ" run, the kerning numbers
// computed from the font's /W (or /Widths) table so the glyphs land exactly where Qt
// put them; the rendering is unchanged, the structure is one object per line of text.
//
// Written for the file QPdfWriter produces: a classic xref table in one section, one
// content stream per page (Flate or raw) whose dictionary sits directly before its
// data, Type0/CIDFontType2 or simple fonts with width tables. Any page that does not
// match is left exactly as it was; any file whose trailer does not match is left
// untouched. Returns true when `bytes` changed.
bool merge_glyph_runs_in_qt_pdf(std::vector<std::uint8_t>& bytes);

}  // namespace patchy::pdf
