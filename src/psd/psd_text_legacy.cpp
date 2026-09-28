// Photoshop 5.0/5.5 'tySh' type record ("Type tool info") reader. Photoshop 6 replaced it
// with the descriptor + EngineData 'TySh' block that psd_text_read.cpp parses; PS 5 files
// (Title02.psd, the Cockpit Master title screen) carry only this fixed-layout record, so
// without this reader every one of their type layers imported as plain pixels. Layout,
// units and the Photoshop 2026 readings it was calibrated against: docs/psd-legacy-text.md.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_text_runs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::psd {

namespace {

constexpr std::uint16_t kLegacyTypeToolVersion = 1;
// Sanity ceiling for a style record's size in engine units; a record that fails it marks a
// misread style section (the layout retry below), not a real 8192 px face.
constexpr double kMaxLegacyStyleSize = static_cast<double>(kMaxTextSizePixels);
constexpr std::size_t kLegacyStyleRecordBytes = 26;
constexpr std::size_t kLegacyColorBytes = 10;
constexpr std::uint16_t kCarriageReturn = 0x000D;
constexpr std::uint16_t kLineFeed = 0x000A;

struct LegacyFace {
  std::uint16_t mark{0};
  std::string postscript_name;
  std::string family;
  std::string style;
};

struct LegacyStyle {
  std::uint16_t mark{0};
  std::uint16_t face_mark{0};
  double size{0.0};
  double tracking_em{0.0};
  double leading{0.0};
};

struct LegacyLine {
  std::uint16_t orientation{0};
  std::int16_t alignment{0};
  // (UTF-16 unit, style mark) per character; the line's '\r' terminator is one of them.
  std::vector<std::pair<std::uint16_t, std::uint16_t>> units;
};

struct LegacyRecord {
  std::array<double, 6> transform{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  std::vector<LegacyFace> faces;
  std::vector<LegacyStyle> styles;
  std::vector<LegacyLine> lines;
  RgbColor color{0, 0, 0};
  std::uint8_t anti_alias{0};
};

double signed_fixed_16_16(std::uint32_t raw) noexcept {
  return static_cast<double>(static_cast<std::int32_t>(raw)) / 65536.0;
}

bool face_mark_known(const std::vector<LegacyFace>& faces, std::uint16_t mark) {
  return std::any_of(faces.begin(), faces.end(), [mark](const LegacyFace& face) { return face.mark == mark; });
}

// The style records, the text section and the color, read from `reader` positioned at the
// style count. Returns false when the bytes do not describe a sane record from here, which
// the caller uses to try the alternative style-section layout. `strict` additionally
// requires every style's face mark to name a face, the check that tells the two layouts
// apart when a stray version word shifts the records by two bytes.
bool read_styles_text_and_color(BigEndianReader reader, const CmykColorConverter& cmyk, bool strict,
                                LegacyRecord& record) {
  try {
    const auto style_count = reader.read_u16();
    if (style_count == 0 || static_cast<std::size_t>(style_count) * kLegacyStyleRecordBytes > reader.remaining()) {
      return false;
    }
    std::vector<LegacyStyle> styles;
    styles.reserve(style_count);
    for (std::uint16_t index = 0; index < style_count; ++index) {
      LegacyStyle style;
      style.mark = reader.read_u16();
      style.face_mark = reader.read_u16();
      style.size = signed_fixed_16_16(reader.read_u32());
      style.tracking_em = signed_fixed_16_16(reader.read_u32());
      (void)reader.read_u32();  // kerning (manual pair kern, em): not modeled
      style.leading = signed_fixed_16_16(reader.read_u32());
      (void)reader.read_u32();  // base shift: not modeled
      (void)reader.read_u8();   // auto kern
      (void)reader.read_u8();   // rotate (vertical type)
      if (!std::isfinite(style.size) || style.size <= 0.0 || style.size > kMaxLegacyStyleSize ||
          !std::isfinite(style.tracking_em) || std::abs(style.tracking_em) > 10.0 || !std::isfinite(style.leading) ||
          style.leading < 0.0 || style.leading > kMaxLegacyStyleSize * 4.0) {
        return false;
      }
      if (strict && !face_mark_known(record.faces, style.face_mark)) {
        return false;
      }
      styles.push_back(style);
    }

    (void)reader.read_u16();  // text type: 0 point text (the only value PS 5 wrote)
    (void)reader.read_u32();  // scaling factor (16.16)
    const auto character_count = reader.read_u32();
    (void)reader.read_u32();  // horizontal placement
    (void)reader.read_u32();  // vertical placement
    (void)reader.read_u32();  // selection start
    (void)reader.read_u32();  // selection end
    if (static_cast<std::size_t>(character_count) > reader.remaining() / 4U) {
      return false;
    }
    const auto line_count = reader.read_u16();
    std::vector<LegacyLine> lines;
    lines.reserve(line_count);
    std::size_t units_seen = 0;
    for (std::uint16_t index = 0; index < line_count; ++index) {
      LegacyLine line;
      const auto count = reader.read_u32();
      line.orientation = reader.read_u16();
      line.alignment = static_cast<std::int16_t>(reader.read_u16());
      if (static_cast<std::size_t>(count) > reader.remaining() / 4U || units_seen + count > character_count) {
        return false;
      }
      line.units.reserve(count);
      for (std::uint32_t unit = 0; unit < count; ++unit) {
        const auto code_unit = reader.read_u16();
        const auto style_mark = reader.read_u16();
        line.units.emplace_back(code_unit, style_mark);
      }
      units_seen += count;
      lines.push_back(std::move(line));
    }
    if (reader.remaining() < kLegacyColorBytes + 1U) {
      return false;
    }
    record.color = read_legacy_effect_color(reader, cmyk);
    record.anti_alias = reader.read_u8();
    record.styles = std::move(styles);
    record.lines = std::move(lines);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::optional<LegacyRecord> parse_legacy_record(std::span<const std::uint8_t> payload, const CmykColorConverter& cmyk) {
  try {
    BigEndianReader reader(payload);
    if (reader.read_u16() != kLegacyTypeToolVersion) {
      return std::nullopt;
    }
    LegacyRecord record;
    for (double& value : record.transform) {
      value = read_f64(reader);
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    (void)reader.read_u16();  // font info version (6)
    const auto face_count = reader.read_u16();
    if (face_count == 0 || static_cast<std::size_t>(face_count) * 15U > reader.remaining()) {
      return std::nullopt;
    }
    record.faces.reserve(face_count);
    for (std::uint16_t index = 0; index < face_count; ++index) {
      LegacyFace face;
      face.mark = reader.read_u16();
      (void)reader.read_u32();  // font type
      face.postscript_name = read_pascal_string(reader, 1);
      face.family = read_pascal_string(reader, 1);
      face.style = read_pascal_string(reader, 1);
      (void)reader.read_u16();  // script
      const auto axis_count = reader.read_u32();
      if (static_cast<std::size_t>(axis_count) > reader.remaining() / 4U) {
        return std::nullopt;
      }
      reader.skip(static_cast<std::size_t>(axis_count) * 4U);  // design vector
      record.faces.push_back(std::move(face));
    }
    // Photoshop's specification lists a u16 style-info version before the style count, but
    // the PS 5.x files seen so far (Title02.psd) go straight from the last face to the count.
    // Read the count first; when the records that follow do not name known faces, or the
    // text section behind them is not consistent, retry with the version word skipped, and
    // finally accept the count-only layout with unknown face marks (they fall back to the
    // first face below).
    if (read_styles_text_and_color(reader, cmyk, true, record)) {
      return record;
    }
    if (reader.remaining() >= 2U) {
      BigEndianReader with_version_word = reader;
      with_version_word.skip(2);
      if (read_styles_text_and_color(with_version_word, cmyk, true, record)) {
        return record;
      }
    }
    if (read_styles_text_and_color(reader, cmyk, false, record)) {
      return record;
    }
    return std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string lowercase_ascii(std::string_view text) {
  std::string lowered(text);
  for (auto& ch : lowered) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return lowered;
}

// The face a PS 5 record names, as the run model stores it. The PostScript name goes through
// the modern chain first (DirectWrite, the registry, the font-database resolver); when only
// the suffix heuristic answered, the record's own family and style strings win: they are the
// GDI names Photoshop 5 wrote ("Futura BdCn BT" + "Bold", "ITC Anna" + "Regular"), the same
// names Windows lists the face under, where the heuristic's humanized PostScript name is a
// guess that no database lists.
ResolvedPhotoshopFont resolve_legacy_face(const LegacyFace& face) {
  ResolvedPhotoshopFont resolved =
      face.postscript_name.empty() ? ResolvedPhotoshopFont{} : resolve_photoshop_font_name(face.postscript_name);
  const ResolvedPhotoshopFont heuristic =
      face.postscript_name.empty() ? ResolvedPhotoshopFont{} : heuristic_resolved_photoshop_font(face.postscript_name);
  const bool only_heuristic = face.postscript_name.empty() ||
                              (resolved.family == heuristic.family && resolved.style == heuristic.style &&
                               resolved.bold == heuristic.bold && resolved.italic == heuristic.italic);
  if (!only_heuristic || face.family.empty()) {
    if (resolved.family.empty()) {
      resolved.family = face.family.empty() ? std::string("Arial") : face.family;
    }
    return resolved;
  }
  ResolvedPhotoshopFont from_record;
  from_record.family = face.family;
  const auto style = lowercase_ascii(face.style);
  from_record.bold = style.find("bold") != std::string::npos || style.find("black") != std::string::npos ||
                     style.find("heavy") != std::string::npos;
  from_record.italic = style.find("italic") != std::string::npos || style.find("oblique") != std::string::npos;
  if (!face.style.empty() && style != "regular" && style != "roman" && style != "plain") {
    from_record.style = face.style;
  }
  return from_record;
}

int justification_for_alignment(std::int16_t alignment) noexcept {
  switch (alignment) {
    case 1:
      return 2;  // center
    case -1:
      return 1;  // right
    default:
      return 0;  // left
  }
}

}  // namespace

int legacy_type_tool_anti_alias(std::uint8_t raw) noexcept {
  return raw == 0 ? 0 : 4;
}

std::optional<LegacyTypeToolInfo> extract_legacy_type_tool(std::span<const std::uint8_t> payload,
                                                           const CmykColorConverter& cmyk) {
  const auto record = parse_legacy_record(payload, cmyk);
  if (!record.has_value() || record->styles.empty() || record->faces.empty()) {
    return std::nullopt;
  }

  LegacyTypeToolInfo info;
  info.transform = record->transform;
  info.color = record->color;
  info.anti_alias_raw = record->anti_alias;

  // Flatten the lines into one unit stream. A non-final line that lacks its '\r' gets a
  // synthetic separator carrying the line's last style, so run and paragraph indices stay
  // consistent with the text the units become.
  std::vector<std::pair<std::uint16_t, std::uint16_t>> units;
  std::vector<std::pair<int, int>> line_spans;  // start, length in units
  for (std::size_t line_index = 0; line_index < record->lines.size(); ++line_index) {
    const auto& line = record->lines[line_index];
    if (line.orientation != 0) {
      info.unsupported_orientation = true;
    }
    const auto start = static_cast<int>(units.size());
    units.insert(units.end(), line.units.begin(), line.units.end());
    const bool last_line = line_index + 1 == record->lines.size();
    if (!last_line && (line.units.empty() || line.units.back().first != kCarriageReturn)) {
      const auto separator_style = line.units.empty() ? record->styles.front().mark : line.units.back().second;
      units.emplace_back(kCarriageReturn, separator_style);
    }
    if (last_line && !units.empty() && units.back().first == kCarriageReturn) {
      units.pop_back();
    }
    line_spans.emplace_back(start, static_cast<int>(units.size()) - start);
  }
  if (units.empty()) {
    return std::nullopt;
  }

  std::vector<std::uint16_t> code_units;
  code_units.reserve(units.size());
  for (const auto& unit : units) {
    code_units.push_back(unit.first == kCarriageReturn ? kLineFeed : unit.first);
  }
  info.text = utf16_units_to_utf8(code_units);
  if (info.text.empty()) {
    return std::nullopt;
  }

  std::vector<std::optional<ResolvedPhotoshopFont>> resolved_faces(record->faces.size());
  const auto face_for_style = [&](const LegacyStyle& style) -> const ResolvedPhotoshopFont& {
    std::size_t face_index = 0;
    for (std::size_t index = 0; index < record->faces.size(); ++index) {
      if (record->faces[index].mark == style.face_mark) {
        face_index = index;
        break;
      }
    }
    if (!resolved_faces[face_index].has_value()) {
      resolved_faces[face_index] = resolve_legacy_face(record->faces[face_index]);
    }
    return *resolved_faces[face_index];
  };
  const auto style_for_mark = [&](std::uint16_t mark) -> const LegacyStyle& {
    for (const auto& style : record->styles) {
      if (style.mark == mark) {
        return style;
      }
    }
    return record->styles.front();
  };

  std::size_t run_start = 0;
  while (run_start < units.size()) {
    const auto mark = units[run_start].second;
    std::size_t run_end = run_start + 1;
    while (run_end < units.size() && units[run_end].second == mark) {
      ++run_end;
    }
    const auto& style = style_for_mark(mark);
    const auto& face = face_for_style(style);
    PsdTextStyleRun run;
    run.start = static_cast<int>(run_start);
    run.length = static_cast<int>(run_end - run_start);
    run.family = face.family;
    run.style = face.style;
    run.bold = face.bold;
    run.italic = face.italic;
    run.size = std::clamp(style.size, 1.0, kMaxLegacyStyleSize);
    run.color = info.color;
    // PS 5 tracking is an em fraction; the run model (and Photoshop's Character panel) uses
    // thousandths of an em, always integral (0.1 -> 100, confirmed over COM).
    run.tracking = std::round(style.tracking_em * 1000.0);
    if (style.leading > 0.0) {
      run.leading = style.leading;
    } else {
      run.auto_leading = true;
    }
    info.runs.push_back(std::move(run));
    run_start = run_end;
  }

  for (std::size_t line_index = 0; line_index < line_spans.size(); ++line_index) {
    const auto [start, length] = line_spans[line_index];
    if (length <= 0) {
      continue;
    }
    PsdTextParagraphRun paragraph;
    paragraph.start = start;
    paragraph.length = length;
    paragraph.justification = justification_for_alignment(record->lines[line_index].alignment);
    info.paragraph_runs.push_back(paragraph);
  }
  return info;
}

}  // namespace patchy::psd
