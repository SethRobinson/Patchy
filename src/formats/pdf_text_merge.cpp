#include "formats/pdf_text_merge.hpp"

#include "formats/miniz/miniz.h"
#include "formats/pdf_file.hpp"
#include "formats/pdf_syntax.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace patchy::pdf {
namespace {

// Glyph advance widths of one font resource, in 1/1000 text-space units.
struct FontWidths {
  double default_width{1000.0};
  std::unordered_map<std::uint32_t, double> widths;

  [[nodiscard]] double width(std::uint32_t code) const {
    const auto found = widths.find(code);
    return found == widths.end() ? default_width : found->second;
  }
};

using FontTable = std::unordered_map<std::string, FontWidths>;

// /W (clause 9.7.4.3): "c [w1 w2 ...]" runs and "c_first c_last w" ranges, mixed.
void read_cid_widths(const File& file, const Object& w_array, FontWidths& out) {
  const auto* items = w_array.array();
  if (items == nullptr) {
    return;
  }
  std::size_t i = 0;
  while (i < items->size()) {
    const auto& first = file.resolve((*items)[i]);
    if (!first.is_number() || i + 1 >= items->size()) {
      return;
    }
    const auto& second = file.resolve((*items)[i + 1]);
    if (const auto* run = second.array()) {
      auto code = static_cast<std::uint32_t>(std::max(0.0, first.number()));
      for (const auto& entry : *run) {
        out.widths[code++] = file.resolve(entry).number(out.default_width);
      }
      i += 2;
      continue;
    }
    if (!second.is_number() || i + 2 >= items->size()) {
      return;
    }
    const auto& third = file.resolve((*items)[i + 2]);
    const auto lo = static_cast<std::int64_t>(first.number());
    const auto hi = static_cast<std::int64_t>(second.number());
    if (hi >= lo && hi - lo < 65536) {
      for (std::int64_t code = lo; code <= hi; ++code) {
        out.widths[static_cast<std::uint32_t>(code)] = third.number(out.default_width);
      }
    }
    i += 3;
  }
}

std::optional<FontWidths> font_widths(const File& file, const Object& font) {
  const auto subtype = file.get(font, "Subtype").name();
  FontWidths result;
  if (subtype == "Type0") {
    const auto* descendants = file.get(font, "DescendantFonts").array();
    if (descendants == nullptr || descendants->empty()) {
      return std::nullopt;
    }
    const auto& cid_font = file.resolve(descendants->front());
    if (!cid_font.is_dictionary()) {
      return std::nullopt;
    }
    result.default_width = file.get(cid_font, "DW").number(1000.0);
    read_cid_widths(file, file.get(cid_font, "W"), result);
    return result;
  }
  // Simple fonts: /FirstChar + /Widths; a missing width falls back to the
  // descriptor's /MissingWidth (0 when absent, per the spec).
  const auto* widths = file.get(font, "Widths").array();
  if (widths == nullptr) {
    return std::nullopt;
  }
  result.default_width = file.get(file.get(font, "FontDescriptor"), "MissingWidth").number(0.0);
  auto code = static_cast<std::uint32_t>(std::max(0.0, file.get(font, "FirstChar").number(0.0)));
  for (const auto& entry : *widths) {
    result.widths[code++] = file.resolve(entry).number(result.default_width);
  }
  return result;
}

FontTable page_fonts(const File& file, const Page& page) {
  FontTable table;
  const auto& fonts = file.get(page.resources, "Font");
  const auto* dict = fonts.dictionary();
  if (dict == nullptr) {
    return table;
  }
  for (const auto& [name, value] : *dict) {
    const auto& font = file.resolve(value);
    if (!font.is_dictionary()) {
      continue;
    }
    if (auto widths = font_widths(file, font); widths.has_value()) {
      table.emplace(name, std::move(*widths));
    }
  }
  return table;
}

// --- content rewriting ---------------------------------------------------------

struct Glyph {
  double x{0.0};
  double y{0.0};
  std::uint32_t code{0};
  std::string hex;
};

bool parse_number(std::string_view text, double& value) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const std::string owned(text);
  value = std::strtod(owned.c_str(), &end);
  return end != nullptr && *end == '\0' && std::isfinite(value);
}

std::vector<std::string_view> split_tokens(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
      ++i;
    }
    const std::size_t start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
      ++i;
    }
    if (i > start) {
      tokens.push_back(line.substr(start, i - start));
    }
  }
  return tokens;
}

// "x y Td <hex> Tj" exactly, the one form Qt emits per glyph.
bool parse_glyph_line(std::string_view line, double& dx, double& dy, std::string& hex, std::uint32_t& code) {
  const auto tokens = split_tokens(line);
  if (tokens.size() != 5 || tokens[2] != "Td" || tokens[4] != "Tj") {
    return false;
  }
  if (!parse_number(tokens[0], dx) || !parse_number(tokens[1], dy)) {
    return false;
  }
  const auto text = tokens[3];
  if (text.size() < 4 || text.size() > 10 || text.front() != '<' || text.back() != '>' || text.size() % 2 != 0) {
    return false;
  }
  hex.assign(text.substr(1, text.size() - 2));
  code = 0;
  for (const char c : hex) {
    int digit = 0;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      return false;
    }
    code = code * 16U + static_cast<std::uint32_t>(digit);
  }
  return true;
}

std::string format_number(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.4f", value);
  std::string text(buffer);
  // Trim trailing zeros and a bare point: "18.6719", "14", "-0.5".
  if (text.find('.') != std::string::npos) {
    while (!text.empty() && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
  }
  if (text == "-0") {
    text = "0";
  }
  return text;
}

struct ContentRewriter {
  explicit ContentRewriter(const FontTable& font_table) : fonts(font_table) {}

  const FontTable& fonts;
  std::string out;
  bool changed{false};

  bool in_text{false};
  const FontWidths* font{nullptr};
  double size{0.0};
  double line_x{0.0};  // the input's current line start (Qt moves it to every glyph)
  double line_y{0.0};
  double out_x{0.0};  // the output's line start (moved only at run starts)
  double out_y{0.0};
  std::vector<Glyph> run;

  void emit_td(double x, double y) {
    out += format_number(x - out_x) + " " + format_number(y - out_y) + " Td ";
    out_x = x;
    out_y = y;
  }

  void flush() {
    if (run.empty()) {
      return;
    }
    if (font == nullptr || size <= 0.0 || run.size() == 1) {
      for (const auto& glyph : run) {
        emit_td(glyph.x, glyph.y);
        out += "<" + glyph.hex + "> Tj\n";
      }
      run.clear();
      return;
    }
    emit_td(run.front().x, run.front().y);
    out += "[<";
    for (std::size_t i = 0; i < run.size(); ++i) {
      out += run[i].hex;
      if (i + 1 < run.size()) {
        // TJ numbers move the pen by -k/1000 * size: the kerning that turns this
        // glyph's nominal advance into the gap Qt actually left.
        const double gap = run[i + 1].x - run[i].x;
        const double kern = font->width(run[i].code) - gap * 1000.0 / size;
        if (std::abs(kern) >= 0.01) {
          out += "> " + format_number(kern) + " <";
        }
      }
    }
    out += ">] TJ\n";
    changed = true;
    run.clear();
  }

  void set_font(std::string_view tokens_line) {
    const auto tokens = split_tokens(tokens_line);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
      if (tokens[i] == "Tf") {
        font = nullptr;
        size = 0.0;
        if (i >= 2 && tokens[i - 2].size() > 1 && tokens[i - 2].front() == '/') {
          const auto found = fonts.find(std::string(tokens[i - 2].substr(1)));
          double parsed = 0.0;
          if (found != fonts.end() && parse_number(tokens[i - 1], parsed) && parsed > 0.0) {
            font = &found->second;
            size = parsed;
          }
        }
      }
      if (tokens[i] == "Tm" || tokens[i] == "Td" || tokens[i] == "TD" || tokens[i] == "T*") {
        // A new line matrix restarts the relative Td chain at the origin.
        line_x = line_y = out_x = out_y = 0.0;
      }
    }
  }

  void line(std::string_view text) {
    const auto trimmed = text.ends_with('\r') ? text.substr(0, text.size() - 1) : text;
    if (!in_text) {
      if (trimmed == "BT") {
        in_text = true;
        font = nullptr;
        size = 0.0;
        line_x = line_y = out_x = out_y = 0.0;
      }
      out.append(text);
      out.push_back('\n');
      return;
    }
    if (trimmed == "ET") {
      flush();
      in_text = false;
      out.append(text);
      out.push_back('\n');
      return;
    }
    double dx = 0.0;
    double dy = 0.0;
    std::string hex;
    std::uint32_t code = 0;
    if (parse_glyph_line(trimmed, dx, dy, hex, code)) {
      line_x += dx;
      line_y += dy;
      if (!run.empty() && std::abs(run.back().y - line_y) > 1e-9) {
        flush();
      }
      run.push_back(Glyph{line_x, line_y, code, std::move(hex)});
      return;
    }
    // Anything else inside BT..ET (font selection, matrices, colors) passes through
    // and ends the current run; Tf/Tm lines also update the font and line state.
    flush();
    const auto tokens = split_tokens(trimmed);
    const bool has_state_op = std::any_of(tokens.begin(), tokens.end(), [](std::string_view token) {
      return token == "Tf" || token == "Tm" || token == "Td" || token == "TD" || token == "T*";
    });
    if (has_state_op) {
      set_font(trimmed);
    }
    out.append(text);
    out.push_back('\n');
  }

  std::string run_all(std::string_view content) {
    out.reserve(content.size());
    std::size_t start = 0;
    while (start <= content.size()) {
      const auto newline = content.find('\n', start);
      const auto end = newline == std::string_view::npos ? content.size() : newline;
      line(content.substr(start, end - start));
      if (newline == std::string_view::npos) {
        break;
      }
      start = newline + 1;
    }
    flush();
    // The loop appends one '\n' per line, including after the last line, which the
    // original may not have had; a trailing newline is harmless in a content stream.
    return out;
  }
};

// --- ToUnicode repair -----------------------------------------------------------

// Qt builds a subset font's ToUnicode map by reverse-looking-up each glyph in the
// face's cmap and keeping the LOWEST code point, so the space glyph (which Arial and
// friends also use for tab, CR and LF) comes back as U+0009 and an importer reads
// "Patchy\tPDF". Control characters never draw, so any destination in a bfchar/bfrange
// that is one of them can only have meant the space glyph. Same-length rewrite.
bool repair_to_unicode(std::string& cmap) {
  bool changed = false;
  const auto fix_destination = [&changed](std::string& token) {
    if (token == "<0009>" || token == "<000A>" || token == "<000D>" || token == "<000a>" || token == "<000d>") {
      token = "<0020>";
      changed = true;
    }
  };
  std::string out;
  out.reserve(cmap.size());
  enum class Section { None, Char, Range } section = Section::None;
  std::size_t start = 0;
  while (start <= cmap.size()) {
    const auto newline = cmap.find('\n', start);
    const auto end = newline == std::string::npos ? cmap.size() : newline;
    std::string line = cmap.substr(start, end - start);
    const auto trimmed = split_tokens(line);
    if (!trimmed.empty() && trimmed.back() == "beginbfchar") {
      section = Section::Char;
    } else if (!trimmed.empty() && trimmed.back() == "beginbfrange") {
      section = Section::Range;
    } else if (!trimmed.empty() && (trimmed.front() == "endbfchar" || trimmed.front() == "endbfrange")) {
      section = Section::None;
    } else if (section != Section::None && trimmed.size() >= 2) {
      std::vector<std::string> tokens(trimmed.begin(), trimmed.end());
      // bfchar: <src> <dst>. bfrange: <lo> <hi> <dst> or <lo> <hi> [<d1> <d2> ...];
      // only destinations change, never the source codes.
      const std::size_t first_destination = section == Section::Char ? 1 : 2;
      for (std::size_t i = first_destination; i < tokens.size(); ++i) {
        std::string& token = tokens[i];
        std::string prefix;
        std::string suffix;
        while (!token.empty() && token.front() == '[') {
          prefix.push_back('[');
          token.erase(token.begin());
        }
        while (!token.empty() && token.back() == ']') {
          suffix.push_back(']');
          token.pop_back();
        }
        fix_destination(token);
        token = prefix + token + suffix;
      }
      line.clear();
      for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
          line.push_back(' ');
        }
        line += tokens[i];
      }
    }
    out += line;
    if (newline == std::string::npos) {
      break;
    }
    out.push_back('\n');
    start = newline + 1;
  }
  if (changed && out.size() == cmap.size()) {
    cmap = std::move(out);
    return true;
  }
  return false;  // a rebuilt line that changed length is not worth an xref shuffle
}

// --- file surgery -------------------------------------------------------------

std::vector<std::uint8_t> deflate(const std::string& data) {
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
  std::vector<std::uint8_t> result(bound);
  if (mz_compress2(result.data(), &bound, reinterpret_cast<const unsigned char*>(data.data()),
                   static_cast<mz_ulong>(data.size()), MZ_BEST_COMPRESSION) != MZ_OK) {
    return {};
  }
  result.resize(bound);
  return result;
}

struct Edit {
  std::size_t begin{0};
  std::size_t end{0};
  std::vector<std::uint8_t> replacement;
};

std::size_t find_last(const std::vector<std::uint8_t>& bytes, std::string_view needle, std::size_t before) {
  if (needle.empty() || bytes.size() < needle.size()) {
    return std::string::npos;
  }
  const std::size_t limit = std::min(before, bytes.size() - needle.size());
  for (std::size_t i = limit + 1; i-- > 0;) {
    if (std::equal(needle.begin(), needle.end(), bytes.begin() + static_cast<std::ptrdiff_t>(i))) {
      return i;
    }
  }
  return std::string::npos;
}

bool parse_size_at(const std::vector<std::uint8_t>& bytes, std::size_t& pos, std::size_t& value) {
  while (pos < bytes.size() && (bytes[pos] == ' ' || bytes[pos] == '\r' || bytes[pos] == '\n')) {
    ++pos;
  }
  const std::size_t start = pos;
  value = 0;
  while (pos < bytes.size() && bytes[pos] >= '0' && bytes[pos] <= '9') {
    value = value * 10 + static_cast<std::size_t>(bytes[pos] - '0');
    ++pos;
  }
  return pos > start;
}

// Applies the edits (ascending, non-overlapping) and shifts the classic xref table and
// startxref by what they displace. The table must be a single section that sits after
// every edit (where QPdfWriter puts it); otherwise nothing is written.
bool apply_edits(std::vector<std::uint8_t>& bytes, std::vector<Edit> edits) {
  if (edits.empty()) {
    return false;
  }
  std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
  const std::size_t startxref_pos = find_last(bytes, "startxref", bytes.size());
  if (startxref_pos == std::string::npos) {
    return false;
  }
  std::size_t cursor = startxref_pos + 9;
  std::size_t xref_offset = 0;
  if (!parse_size_at(bytes, cursor, xref_offset) || xref_offset + 4 > bytes.size() ||
      std::string_view(reinterpret_cast<const char*>(bytes.data() + xref_offset), 4) != "xref") {
    return false;
  }
  for (const auto& edit : edits) {
    if (edit.end > xref_offset || edit.begin > edit.end) {
      return false;
    }
  }
  cursor = xref_offset + 4;
  std::size_t first_object = 0;
  std::size_t count = 0;
  if (!parse_size_at(bytes, cursor, first_object) || !parse_size_at(bytes, cursor, count)) {
    return false;
  }
  while (cursor < bytes.size() && (bytes[cursor] == ' ' || bytes[cursor] == '\r' || bytes[cursor] == '\n')) {
    ++cursor;
  }
  const std::size_t entries_begin = cursor;
  if (entries_begin + count * 20 > bytes.size()) {
    return false;
  }
  const auto shifted = [&edits](std::size_t offset) {
    std::ptrdiff_t delta = 0;
    for (const auto& edit : edits) {
      if (edit.end <= offset) {
        delta += static_cast<std::ptrdiff_t>(edit.replacement.size()) - static_cast<std::ptrdiff_t>(edit.end - edit.begin);
      } else if (edit.begin < offset) {
        return std::optional<std::size_t>{};  // an object header inside an edited region
      }
    }
    return std::optional<std::size_t>{static_cast<std::size_t>(static_cast<std::ptrdiff_t>(offset) + delta)};
  };

  // The tail (xref table, trailer, startxref) is rebuilt with shifted entries.
  std::vector<std::uint8_t> tail(bytes.begin() + static_cast<std::ptrdiff_t>(xref_offset),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(startxref_pos));
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t entry = entries_begin - xref_offset + i * 20;
    if (entry + 18 > tail.size() || tail[entry + 17] != 'n') {
      continue;  // free entries keep their (meaningless) offsets
    }
    std::size_t offset = 0;
    for (std::size_t d = 0; d < 10; ++d) {
      const auto c = tail[entry + d];
      if (c < '0' || c > '9') {
        return false;
      }
      offset = offset * 10 + static_cast<std::size_t>(c - '0');
    }
    const auto moved = shifted(offset);
    if (!moved.has_value()) {
      return false;
    }
    char digits[12];
    std::snprintf(digits, sizeof(digits), "%010zu", *moved);
    std::copy_n(digits, 10, tail.begin() + static_cast<std::ptrdiff_t>(entry));
  }
  const auto new_xref_offset = shifted(xref_offset);
  if (!new_xref_offset.has_value()) {
    return false;
  }

  std::vector<std::uint8_t> result;
  result.reserve(bytes.size() + 1024);
  std::size_t copied = 0;
  for (const auto& edit : edits) {
    result.insert(result.end(), bytes.begin() + static_cast<std::ptrdiff_t>(copied),
                  bytes.begin() + static_cast<std::ptrdiff_t>(edit.begin));
    result.insert(result.end(), edit.replacement.begin(), edit.replacement.end());
    copied = edit.end;
  }
  result.insert(result.end(), bytes.begin() + static_cast<std::ptrdiff_t>(copied),
                bytes.begin() + static_cast<std::ptrdiff_t>(xref_offset));
  result.insert(result.end(), tail.begin(), tail.end());
  const std::string ending = "startxref\n" + std::to_string(*new_xref_offset) + "\n%%EOF\n";
  result.insert(result.end(), ending.begin(), ending.end());
  bytes = std::move(result);
  return true;
}

}  // namespace

bool merge_glyph_runs_in_qt_pdf(std::vector<std::uint8_t>& bytes) {
  std::vector<std::string> notices;
  auto file = File::open(bytes, &notices);
  if (!file.has_value() || file->is_encrypted() || !notices.empty()) {
    return false;  // damaged or unusual: not the file this pass was written for
  }
  std::vector<Edit> edits;
  for (const auto& page : file->pages()) {
    const auto& contents = file->get(page.dict, "Contents");
    const auto* raw = contents.stream();
    if (raw == nullptr) {
      continue;  // a content array or nothing; Qt writes one stream per page
    }
    const auto chain = file->filter_chain(contents);
    if (!chain.empty() && (chain.size() != 1 || chain.front().kind != FilterKind::Flate)) {
      continue;
    }
    const auto decoded = file->stream_data(contents);
    if (!decoded.error.empty() || decoded.image_codec != FilterKind::None) {
      continue;
    }
    const auto fonts = page_fonts(*file, page);
    if (fonts.empty()) {
      continue;
    }
    // The fonts' ToUnicode maps: uncompressed in Qt's output, repaired in place.
    if (const auto* font_dict = file->get(page.resources, "Font").dictionary()) {
      for (const auto& [name, value] : *font_dict) {
        const auto& to_unicode = file->get(file->resolve(value), "ToUnicode");
        const auto* cmap_stream = to_unicode.stream();
        if (cmap_stream == nullptr || !file->filter_chain(to_unicode).empty()) {
          continue;
        }
        // data_length is unresolved for an indirect /Length; raw_stream_bytes knows.
        const std::size_t begin = cmap_stream->data_offset;
        const std::size_t length = file->raw_stream_bytes(to_unicode).size();
        const std::size_t end = begin + length;
        if (length == 0 || end > bytes.size() ||
            std::any_of(edits.begin(), edits.end(), [begin](const Edit& e) { return e.begin == begin; })) {
          continue;
        }
        std::string cmap(reinterpret_cast<const char*>(bytes.data() + begin), length);
        if (repair_to_unicode(cmap)) {
          Edit edit;
          edit.begin = begin;
          edit.end = end;
          edit.replacement.assign(cmap.begin(), cmap.end());
          edits.push_back(std::move(edit));
        }
      }
    }
    ContentRewriter rewriter(fonts);
    const auto rewritten =
        rewriter.run_all(std::string_view(reinterpret_cast<const char*>(decoded.data.data()), decoded.data.size()));
    if (!rewriter.changed) {
      continue;
    }
    // The stream dictionary Qt writes sits directly before "stream": replace it and
    // the data together with a direct /Length so the indirect length object can
    // simply be left behind.
    const std::size_t data_begin = raw->data_offset;
    const std::size_t data_end = data_begin + file->raw_stream_bytes(contents).size();  // /Length resolved
    if (data_end > bytes.size() || data_begin < 2) {
      continue;
    }
    const std::size_t dict_begin = find_last(bytes, "<<", data_begin);
    if (dict_begin == std::string::npos) {
      continue;
    }
    const std::string_view between(reinterpret_cast<const char*>(bytes.data() + dict_begin), data_begin - dict_begin);
    if (between.find(">>") == std::string_view::npos || between.find("stream") == std::string_view::npos ||
        between.find("/Length") == std::string_view::npos || between.size() > 512) {
      continue;
    }
    auto compressed = deflate(rewritten);
    if (compressed.empty()) {
      continue;
    }
    const std::string header = "<< /Length " + std::to_string(compressed.size()) + " /Filter /FlateDecode >>\nstream\n";
    Edit edit;
    edit.begin = dict_begin;
    edit.end = data_end;
    edit.replacement.assign(header.begin(), header.end());
    edit.replacement.insert(edit.replacement.end(), compressed.begin(), compressed.end());
    edits.push_back(std::move(edit));
  }
  return apply_edits(bytes, std::move(edits));
}

}  // namespace patchy::pdf
