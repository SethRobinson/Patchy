#include "psd/psd_text_engine_block.hpp"
#include "core/text_area.hpp"

#include "psd/psd_io_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace patchy::psd {

namespace {

// Photoshop 2026's serialization version and engine version (docs/txt2.md).
EngineNode engine_version_node() {
  return engine_dict_of({{"0", engine_integer(7)},
                         {"1", engine_integer(11)},
                         {"2", engine_integer(0)},
                         {"3", engine_string("Photoshop")}});
}

EngineNode paint_node(RgbColor color) {
  auto values = engine_list_of({engine_number(1.0), engine_number(color.red / 255.0),
                                engine_number(color.green / 255.0), engine_number(color.blue / 255.0)});
  return engine_dict_of({{"99", engine_name("SimplePaint")},
                         {"0", engine_dict_of({{"0", engine_integer(1)}, {"1", std::move(values)}})}});
}

EngineNode triple(double a, double b, double c) {
  return engine_list_of({engine_number(a), engine_number(b), engine_number(c)});
}

// The sparse paragraph sheet Photoshop 2026 writes for a layer of ours (30 keys, docs/txt2.md).
EngineNode paragraph_sheet(const PsdTextParagraphRun& run) {
  const auto fraction = std::isfinite(run.auto_leading_fraction) && run.auto_leading_fraction > 0.01 &&
                                run.auto_leading_fraction < 10.0
                            ? run.auto_leading_fraction
                            : 1.2;
  return engine_dict_of({
      {"0", engine_integer(std::clamp(run.justification, 0, 6))},
      {"1", engine_number(run.first_line_indent)},
      {"2", engine_number(run.start_indent)},
      {"3", engine_number(run.end_indent)},
      {"4", engine_number(run.space_before)},
      {"5", engine_number(run.space_after)},
      {"7", engine_number(fraction)},
      {"8", engine_integer(0)},
      // Hyphenate and its settings, kept from the file (GitHub issue 683): Patchy does not
      // hyphenate, and a new layer has Hyphenate off like Photoshop 2026's.
      {"9", engine_bool(run.auto_hyphenate)},
      {"10", engine_integer(std::clamp(run.hyphenated_word_size, 0, 1000))},
      {"11", engine_integer(std::clamp(run.pre_hyphen, 0, 1000))},
      {"12", engine_integer(std::clamp(run.post_hyphen, 0, 1000))},
      {"13", engine_integer(std::clamp(run.consecutive_hyphens, 0, 1000))},
      {"14", engine_number(std::isfinite(run.hyphenation_zone) ? run.hyphenation_zone : 36.0)},
      {"15", engine_bool(run.hyphenate_capitalized)},
      {"17", triple(0.8, 1.0, 1.33)},
      {"18", triple(0.0, 0.0, 0.0)},
      {"19", triple(1.0, 1.0, 1.0)},
      {"21", engine_bool(false)},
      {"23", engine_bool(true)},
      {"24", engine_integer(0)},
      {"25", engine_integer(0)},
      {"27", engine_nil()},
      {"28", engine_nil()},
      // The line composer (true Every-line, false Single-line), kept from the file.
      {"29", engine_bool(run.every_line_composer)},
      {"33", engine_integer(run.direction == "rtl" ? 1 : 0)},
      {"35", engine_integer(std::clamp(run.composer, 0, 2))},
      {"36", engine_nil()},
      {"37", engine_integer(0)},
      // Kashida width (0 none to 4 stylistic, 2 medium by default), kept from the file.
      {"40", engine_integer(std::clamp(run.kashida_width, 0, 4))},
  });
}

// The sparse style sheet Photoshop 2026 writes for a layer of ours (47 keys, docs/txt2.md).
EngineNode style_sheet(const PsdTextStyleRun& run, int font_index, double auto_leading_fraction) {
  const auto size = std::max(1.0, std::isfinite(run.size) ? run.size : 1.0);
  const bool fixed_leading = !run.auto_leading && run.leading.has_value() && std::isfinite(*run.leading) &&
                             *run.leading > 0.0;
  const auto leading = fixed_leading ? *run.leading : size * auto_leading_fraction;
  const auto tracking = std::isfinite(run.tracking) ? static_cast<long long>(std::lround(run.tracking)) : 0LL;
  const auto scale = [](double value) { return std::isfinite(value) && value > 0.0 ? value : 1.0; };
  // Kerning, kept from the file: /11 0 manual, 1 Metrics, 2 Optical.
  const auto kerning_mode = run.kerning_mode == kTextKerningManual || run.kerning_mode == kTextKerningOptical
                                ? run.kerning_mode
                                : kTextKerningMetrics;
  auto sheet = engine_dict_of({
      {"0", engine_integer(std::max(0, font_index))},
      {"1", engine_number(size)},
      {"2", engine_bool(run.faux_bold)},
      {"3", engine_bool(run.faux_italic)},
      {"4", engine_bool(!fixed_leading)},
      {"5", engine_number(leading)},
      {"6", engine_number(scale(run.horizontal_scale))},
      {"7", engine_number(scale(run.vertical_scale))},
      {"8", engine_integer(tracking)},
      {"9", engine_number(std::isfinite(run.baseline_shift) ? run.baseline_shift : 0.0)},
      {"11", engine_integer(kerning_mode)},
      // Character-panel styles (GitHub issue 346, Photoshop 2026 captures): /12 FontCaps, /13
      // FontBaseline, /15 strikethrough (1 on), /16 underline (2 on, Photoshop's horizontal
      // "underline left"), /18 Ligatures (on by default), /19 Discretionary Ligatures, /20
      // Contextual Alternates, /23 Fractions, /24 Ordinals, /25 Swash, /26 Titling, /28 Stylistic
      // Alternates.
      {"12", engine_integer(run.font_caps == 1 || run.font_caps == 2 ? run.font_caps : 0)},
      {"13", engine_integer(run.font_baseline == 1 || run.font_baseline == 2 ? run.font_baseline : 0)},
      {"14", engine_integer(0)},
      {"15", engine_integer(run.strikethrough ? 1 : 0)},
      {"16", engine_integer(run.underline ? 2 : 0)},
      {"18", engine_bool(run.ligatures)},
      {"19", engine_bool(run.discretionary_ligatures)},
      {"20", engine_bool(run.contextual_alternates)},
      {"23", engine_bool(run.fractions)},
      {"24", engine_bool(run.ordinals)},
      {"25", engine_bool(run.swash)},
      {"26", engine_bool(run.titling_alternates)},
      {"27", engine_bool(false)},
      {"28", engine_bool(run.stylistic_alternates)},
      {"29", engine_bool(false)},
      {"30", engine_integer(0)},
      {"31", engine_bool(false)},
      {"32", engine_bool(false)},
      {"33", engine_bool(false)},
      {"35", engine_integer(1)},
      {"36", engine_number(0.0)},
      {"37", engine_integer(2)},
      // Hyphenation and spelling language (GitHub issue 682; docs/txt2.md, "Languages").
      {"38", engine_integer(run.language.value_or(kPhotoshopTextDefaultLanguage))},
      {"39", engine_integer(0)},
      {"52", engine_bool(false)},
      {"53", paint_node(run.color)},
      {"54", paint_node(RgbColor{0, 0, 0})},
      {"68", engine_integer(0)},
      // Middle Eastern settings, kept from the file (GitHub issue 683): /70 digit set, /72 and
      // /73 diacritic offsets, /75 Justification Alternates, /87 the mark distance from the
      // baseline with /88 false (an explicit distance, as Photoshop 2026 writes every run).
      {"70", engine_integer(std::clamp(run.digit_set, 0, 15))},
      {"72", engine_number(std::isfinite(run.diacritic_x_offset) ? run.diacritic_x_offset : 0.0)},
      {"73", engine_number(std::isfinite(run.diacritic_y_offset) ? run.diacritic_y_offset : 0.0)},
      {"75", engine_bool(run.justification_alternates)},
      {"85", engine_integer(0)},
      {"87", engine_number(std::isfinite(run.mark_y_distance) ? run.mark_y_distance : 100.0)},
      {"88", engine_bool(false)},
      {"92", engine_number(0.0)},
  });
  if (kerning_mode == kTextKerningManual && std::isfinite(run.kerning) && run.kerning != 0.0) {
    // The manual number, before the mode as Photoshop orders the keys; a manual run without one
    // is manual 0, which Photoshop 2026 writes with no /10 at all.
    EngineDictEntry entry;
    entry.key_ws = " ";
    entry.key = "10";
    entry.value = engine_number(run.kerning);
    const auto mode = std::find_if(sheet.dict.begin(), sheet.dict.end(),
                                   [](const EngineDictEntry& candidate) { return candidate.key == "11"; });
    sheet.dict.insert(mode, std::move(entry));
  }
  return sheet;
}

// Run lengths that cover the engine text exactly (the TySh writer's normalization).
template <typename Run>
std::vector<std::pair<const Run*, int>> covering_runs(std::span<const Run> runs, int units, const Run& fallback) {
  std::vector<std::pair<const Run*, int>> out;
  int covered = 0;
  for (const auto& run : runs) {
    if (run.length <= 0) {
      continue;
    }
    out.emplace_back(&run, run.length);
    covered += run.length;
  }
  if (out.empty()) {
    out.emplace_back(&fallback, units);
  } else if (covered < units) {
    out.back().second += units - covered;
  } else if (covered > units) {
    // Trim from the end so the lengths never exceed the text.
    int excess = covered - units;
    while (excess > 0 && !out.empty()) {
      const auto take = std::min(excess, out.back().second);
      out.back().second -= take;
      excess -= take;
      if (out.back().second <= 0) {
        out.pop_back();
      }
    }
    if (out.empty()) {
      out.emplace_back(&fallback, units);
    }
  }
  return out;
}

}  // namespace

std::optional<TextEngineBlock> TextEngineBlock::parse(std::span<const std::uint8_t> payload) {
  auto root = parse_engine_data(payload, /*bare_root=*/true);
  if (!root.has_value()) {
    return std::nullopt;
  }
  TextEngineBlock block(std::move(*root));
  if (block.objects() == nullptr || block.frames() == nullptr || block.root_.at_path({"0", "1", "0"}) == nullptr) {
    return std::nullopt;
  }
  return block;
}

TextEngineBlock TextEngineBlock::from_template() {
  auto block = parse(text_engine_template_bytes());
  // The generated template always parses; a broken build of it is a programming error.
  return block.has_value() ? std::move(*block) : TextEngineBlock(engine_dict());
}

std::vector<std::uint8_t> TextEngineBlock::serialize() const {
  return serialize_engine_data_bytes(root_, /*bare_root=*/true);
}

EngineNode* TextEngineBlock::objects() {
  return root_.at_path({"1", "1"});
}

const EngineNode* TextEngineBlock::objects() const {
  return root_.at_path({"1", "1"});
}

EngineNode* TextEngineBlock::frames() {
  return root_.at_path({"0", "8", "0"});
}

std::size_t TextEngineBlock::object_count() const {
  const auto* list = objects();
  return list != nullptr && list->is_list() ? list->list.size() : 0U;
}

std::optional<std::string> TextEngineBlock::object_text(std::size_t index) const {
  const auto* list = objects();
  if (list == nullptr || index >= list->list.size()) {
    return std::nullopt;
  }
  const auto* text = list->list[index].at_path({"0", "0"});
  return text != nullptr ? text->string_utf8() : std::nullopt;
}

std::optional<std::size_t> TextEngineBlock::object_frame_index(std::size_t index) const {
  const auto* list = objects();
  if (list == nullptr || index >= list->list.size()) {
    return std::nullopt;
  }
  const auto* frame = list->list[index].at_path({"1", "0", "0", "0"});
  const auto value = frame != nullptr ? frame->integer() : std::nullopt;
  if (!value.has_value() || *value < 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

int TextEngineBlock::font_index(const std::string& postscript_name) {
  auto* fonts = root_.at_path({"0", "1", "0"});
  if (fonts == nullptr || !fonts->is_list()) {
    return 1;
  }
  for (std::size_t index = 0; index < fonts->list.size(); ++index) {
    const auto* name = fonts->list[index].at_path({"0", "0", "0"});
    if (name != nullptr && name->string_utf8() == postscript_name) {
      return static_cast<int>(index);
    }
  }
  auto entry = engine_dict_of(
      {{"0", engine_dict_of({{"99", engine_name("CoolTypeFont")},
                             {"0", engine_dict_of({{"0", engine_string(postscript_name)}, {"2", engine_integer(1)}})}})}});
  fonts->list.push_back(std::move(entry));
  return static_cast<int>(fonts->list.size() - 1U);
}

// A path-text frame (docs/path-text.md): /0/2 holds only the type (/0 2), the start/end range
// (/6) and an optional affine (/2); /0/1/0 is the contour, eight numbers per cubic segment.
// Any other key (vertical type's /1, settings Patchy has not seen) leaves the layer protected.
static TextFrameGeometry path_frame_geometry(const EngineNode& frame) {
  const auto* settings = frame.at_path({"0", "2"});
  if (!settings || !settings->is_dict()) return {};
  for (const auto& entry : settings->dict) {
    if (entry.key != "0" && entry.key != "2" && entry.key != "6") return {};
  }
  const auto* marker = frame.at_path({"0", "2", "6"});
  const auto start = marker->list[0].number();
  const auto end = marker->list[1].number();
  if (!start || !end || !std::isfinite(*start) || !std::isfinite(*end) || !(*end > *start)) return {};
  const auto* coords = frame.at_path({"0", "1", "0"});
  if (!coords || !coords->is_list() || coords->list.size() < 8 || coords->list.size() % 8 != 0 ||
      coords->list.size() > kTextPathMaxAnchors * 8U) return {};
  std::vector<double> values;
  values.reserve(coords->list.size());
  for (const auto& node : coords->list) {
    const auto value = node.number();
    if (!value || !std::isfinite(*value) || std::abs(*value) > 1000000.0) return {};
    values.push_back(*value);
  }
  // Consecutive segments share their end points; the contour is closed when the last one ends
  // where the first starts (the frame has no closed flag).
  for (std::size_t i = 8; i < values.size(); i += 8) {
    if (std::abs(values[i] - values[i - 2]) > 0.0001 || std::abs(values[i + 1] - values[i - 1]) > 0.0001) return {};
  }
  const bool closed = std::abs(values[0] - values[values.size() - 2]) <= 0.0001 &&
                      std::abs(values[1] - values[values.size() - 1]) <= 0.0001;
  VectorPath path;
  auto& sub = path.subpaths.emplace_back();
  sub.op = PathCombineOp::Xor;
  sub.closed = closed;
  const std::size_t segments = values.size() / 8;
  for (std::size_t s = 0; s < segments; ++s) {
    const std::size_t i = s * 8;
    PathAnchor anchor{values[i], values[i + 1], values[i], values[i + 1], values[i + 2], values[i + 3], false};
    if (s > 0 || closed) {
      const std::size_t previous = (s == 0 ? segments - 1 : s - 1) * 8;
      anchor.in_x = values[previous + 4];
      anchor.in_y = values[previous + 5];
    }
    sub.anchors.push_back(anchor);
  }
  if (!closed) {
    const std::size_t last = (segments - 1) * 8;
    sub.anchors.push_back({values[last + 6], values[last + 7], values[last + 4], values[last + 5],
                           values[last + 6], values[last + 7], false});
  }
  if (const auto* matrix = frame.at_path({"0", "2", "2"})) {
    if (!matrix->is_list() || matrix->list.size() != 6) return {};
    std::array<double, 6> affine{};
    for (std::size_t i = 0; i < affine.size(); ++i) {
      const auto value = matrix->list[i].number();
      if (!value || !std::isfinite(*value)) return {};
      affine[i] = *value;
    }
    transform_vector_path(path, affine);
  }
  if (!valid_text_path(path)) return {};
  TextFrameGeometry geometry;
  geometry.kind = TextFrameGeometry::Kind::Path;
  geometry.path_range = normalized_text_path_range(path, {*start, *end});
  geometry.path = std::move(path);
  return geometry;
}

TextFrameGeometry TextEngineBlock::object_geometry(std::size_t index) const {
  const auto fi = object_frame_index(index);
  const auto* frame_list = root_.at_path({"0", "8", "0"});
  if (!fi || !frame_list || !frame_list->is_list() || *fi >= frame_list->list.size()) return {};
  const auto& frame = frame_list->list[*fi];
  const auto* marker = frame.at_path({"0", "2", "6"});
  if (!marker || !marker->is_list() || marker->list.size() != 2) return {};
  const auto mode = marker->list[0].number();
  if (const auto* type = frame.at_path({"0", "2", "0"}); type && type->integer() == 2) {
    return path_frame_geometry(frame);
  }
  if (!mode || marker->list[1].number() != mode) return {};
  if (*mode == -1.0) return {TextFrameGeometry::Kind::Point, std::nullopt};
  if (*mode == -2.0) return {TextFrameGeometry::Kind::Box, std::nullopt};
  if (*mode != -3.0) return {};
  const auto* coords = frame.at_path({"0", "1", "0"});
  if (!coords || !coords->is_list() || coords->list.size() < 16 ||
      coords->list.size() % 8 != 0 || coords->list.size() > 800000) return {};
  std::vector<double> values;
  values.reserve(coords->list.size());
  for (const auto& node : coords->list) {
    const auto value = node.number();
    if (!value || !std::isfinite(*value) || std::abs(*value) > 1000000.0) return {};
    values.push_back(*value);
  }
  VectorPath area;
  auto& sub = area.subpaths.emplace_back();
  sub.op = PathCombineOp::Xor;
  for (std::size_t i = 0; i < values.size(); i += 8) {
    const auto previous = (i + values.size() - 8) % values.size();
    if (std::abs(values[i] - values[previous + 6]) > 0.0001 ||
        std::abs(values[i + 1] - values[previous + 7]) > 0.0001) return {};
    sub.anchors.push_back({values[i], values[i + 1], values[previous + 4], values[previous + 5],
                           values[i + 2], values[i + 3], false});
  }
  // Photoshop may store absolute source-path coordinates plus a frame-local
  // affine. The TySh transform is applied later, exactly once, by the renderer.
  if (const auto* matrix = frame.at_path({"0", "2", "2"})) {
    if (!matrix->is_list() || matrix->list.size() != 6) return {};
    std::array<double, 6> affine{};
    for (std::size_t i = 0; i < affine.size(); ++i) {
      const auto value = matrix->list[i].number();
      if (!value || !std::isfinite(*value)) return {};
      affine[i] = *value;
    }
    transform_vector_path(area, affine);
  }
  if (!valid_text_area(area)) return {};
  return {TextFrameGeometry::Kind::Area, std::move(area)};
}

std::vector<TextParagraphFontSize> TextEngineBlock::object_paragraph_font_sizes(std::size_t index) const {
  std::vector<TextParagraphFontSize> result;
  const auto* list = objects();
  const auto* runs = list != nullptr && index < list->list.size() ? list->list[index].at_path({"0", "5", "0"}) : nullptr;
  if (runs == nullptr || !runs->is_list()) return result;
  const auto* sheets = root_.at_path({"0", "6", "0"});
  const auto character_size = [](const EngineNode* paragraph_sheet) -> std::optional<double> {
    const auto* node = paragraph_sheet != nullptr ? paragraph_sheet->at_path({"32", "1"}) : nullptr;
    const auto value = node != nullptr ? node->number() : std::nullopt;
    if (value && std::isfinite(*value) && *value > 0.0 && *value < 100000.0) return value;
    return std::nullopt;
  };
  bool any = false;
  for (const auto& run : runs->list) {
    TextParagraphFontSize entry;
    const auto* length = run.at_path({"1"});
    entry.length = static_cast<int>(std::clamp<long long>(length ? length->integer().value_or(0) : 0, 0, 1 << 30));
    // A run's sheet: /5 its own sparse paragraph properties, /6 the parent ParagraphSheetSet index.
    const auto* sheet = run.at_path({"0", "0"});
    entry.size = character_size(sheet != nullptr ? sheet->at_path({"5"}) : nullptr);
    if (!entry.size && sheet != nullptr && sheets != nullptr && sheets->is_list()) {
      const auto* parent = sheet->at_path({"6"});
      const auto parent_index = parent != nullptr ? parent->integer() : std::nullopt;
      if (parent_index && *parent_index >= 0 && static_cast<std::size_t>(*parent_index) < sheets->list.size()) {
        entry.size = character_size(sheets->list[static_cast<std::size_t>(*parent_index)].at_path({"0", "5"}));
      }
    }
    any = any || entry.size.has_value();
    result.push_back(entry);
  }
  if (!any) result.clear();
  return result;
}

std::vector<TextStyleRunFeatures> TextEngineBlock::object_style_features(std::size_t index) const {
  std::vector<TextStyleRunFeatures> result;
  const auto* list = objects();
  const auto* runs = list != nullptr && index < list->list.size() ? list->list[index].at_path({"0", "6", "0"}) : nullptr;
  if (runs == nullptr || !runs->is_list()) return result;
  const auto flag = [](const EngineNode* sheet, std::string_view key) {
    const auto* node = sheet != nullptr ? sheet->find(key) : nullptr;
    return node != nullptr && node->boolean().value_or(false);
  };
  // Language /38: the run's sheet, else its parent StyleSheetSet entry (/0/0/5, the index Photoshop
  // writes beside the sheet), else the document's default style sheet (/1/2).
  const auto language_of = [](const EngineNode* sheet) -> std::optional<int> {
    const auto* node = sheet != nullptr ? sheet->find("38") : nullptr;
    const auto value = node != nullptr ? node->integer() : std::nullopt;
    if (value && valid_photoshop_text_language(static_cast<int>(std::clamp<long long>(*value, -1, 256)))) {
      return static_cast<int>(*value);
    }
    return std::nullopt;
  };
  const auto* style_sheets = root_.at_path({"0", "5", "0"});
  const auto* document_sheet = root_.at_path({"1", "2"});
  const auto document_language = language_of(document_sheet);
  bool any = false;
  for (const auto& run : runs->list) {
    TextStyleRunFeatures entry;
    const auto* length = run.at_path({"1"});
    entry.length = static_cast<int>(std::clamp<long long>(length ? length->integer().value_or(0) : 0, 0, 1 << 30));
    // A run's sparse style sheet is /0/0/6; a key it leaves out is off in every default sheet.
    const auto* sheet = run.at_path({"0", "0", "6"});
    const EngineNode* parent_sheet = nullptr;
    if (style_sheets != nullptr && style_sheets->is_list()) {
      const auto* parent = run.at_path({"0", "0", "5"});
      const auto parent_index = parent != nullptr ? parent->integer() : std::nullopt;
      if (parent_index && *parent_index >= 0 && static_cast<std::size_t>(*parent_index) < style_sheets->list.size()) {
        parent_sheet = style_sheets->list[static_cast<std::size_t>(*parent_index)].at_path({"0", "6"});
      }
    }
    auto language = language_of(sheet);
    if (!language) language = language_of(parent_sheet);
    if (!language) language = document_language;
    if (language && *language != kPhotoshopTextDefaultLanguage) entry.language = language;
    // The Middle Eastern settings (GitHub issue 683): the run's sheet, its parent, the document's.
    const auto resolved = [&](std::string_view key) -> const EngineNode* {
      for (const auto* candidate : {sheet, parent_sheet, document_sheet}) {
        if (const auto* node = candidate != nullptr ? candidate->find(key) : nullptr; node != nullptr) {
          return node;
        }
      }
      return nullptr;
    };
    const auto offset_of = [&](std::string_view key) -> std::optional<double> {
      const auto* node = resolved(key);
      const auto value = node != nullptr ? node->number() : std::nullopt;
      if (value && std::isfinite(*value) && std::abs(*value) <= 100000.0) return value;
      return std::nullopt;
    };
    if (const auto* node = resolved("70"); node != nullptr) {
      if (const auto value = node->integer(); value && *value >= 0 && *value <= 15) {
        entry.digit_set = static_cast<int>(*value);
      }
    }
    entry.diacritic_x_offset = offset_of("72");
    entry.diacritic_y_offset = offset_of("73");
    if (const auto* node = resolved("75"); node != nullptr) {
      entry.justification_alternates = node->boolean();
    }
    // /88 true leaves the mark distance to the font: the same as the default 100.
    const auto* automatic = resolved("88");
    entry.mark_y_distance = automatic != nullptr && automatic->boolean().value_or(false) ? std::optional<double>(100.0)
                                                                                         : offset_of("87");
    const bool middle_eastern = entry.digit_set.value_or(0) != 0 || entry.diacritic_x_offset.value_or(0.0) != 0.0 ||
                                entry.diacritic_y_offset.value_or(0.0) != 0.0 ||
                                entry.mark_y_distance.value_or(100.0) != 100.0 ||
                                entry.justification_alternates.value_or(false);
    entry.ordinals = flag(sheet, "24");
    entry.fractions = flag(sheet, "23");
    entry.contextual_alternates = flag(sheet, "20");
    entry.swash = flag(sheet, "25");
    entry.titling_alternates = flag(sheet, "26");
    entry.stylistic_alternates = flag(sheet, "28");
    any = any || entry.ordinals || entry.fractions || entry.contextual_alternates || entry.swash ||
          entry.titling_alternates || entry.stylistic_alternates || entry.language.has_value() || middle_eastern;
    result.push_back(entry);
  }
  if (!any) result.clear();
  return result;
}

std::vector<TextParagraphComposer> TextEngineBlock::object_paragraph_composers(std::size_t index) const {
  std::vector<TextParagraphComposer> result;
  const auto* list = objects();
  const auto* runs = list != nullptr && index < list->list.size() ? list->list[index].at_path({"0", "5", "0"}) : nullptr;
  if (runs == nullptr || !runs->is_list()) return result;
  const auto* sheets = root_.at_path({"0", "6", "0"});
  const auto key_value = [](const EngineNode* paragraph_sheet, const char* key, int last) -> std::optional<int> {
    const auto* node = paragraph_sheet != nullptr ? paragraph_sheet->find(key) : nullptr;
    const auto value = node != nullptr ? node->integer() : std::nullopt;
    if (value && *value >= 0 && *value <= last) return static_cast<int>(*value);
    return std::nullopt;
  };
  const auto* document_sheet = root_.at_path({"1", "3"});
  bool any = false;
  for (const auto& run : runs->list) {
    TextParagraphComposer entry;
    const auto* length = run.at_path({"1"});
    entry.length = static_cast<int>(std::clamp<long long>(length ? length->integer().value_or(0) : 0, 0, 1 << 30));
    const auto* sheet = run.at_path({"0", "0"});
    const EngineNode* parent_sheet = nullptr;
    if (sheet != nullptr && sheets != nullptr && sheets->is_list()) {
      const auto* parent = sheet->at_path({"6"});
      const auto parent_index = parent != nullptr ? parent->integer() : std::nullopt;
      if (parent_index && *parent_index >= 0 && static_cast<std::size_t>(*parent_index) < sheets->list.size()) {
        parent_sheet = sheets->list[static_cast<std::size_t>(*parent_index)].at_path({"0", "5"});
      }
    }
    const auto resolve = [&](const char* key, int last) {
      auto value = key_value(sheet != nullptr ? sheet->at_path({"5"}) : nullptr, key, last);
      if (!value) value = key_value(parent_sheet, key, last);
      if (!value) value = key_value(document_sheet, key, last);
      return value;
    };
    entry.composer = resolve("35", 2);
    entry.direction = resolve("33", 1);
    // Hyphenate and the Kashida width resolve like the composer; the hyphenation settings are
    // the run's own only (GitHub issue 683, see TextParagraphComposer).
    const auto* own = sheet != nullptr ? sheet->at_path({"5"}) : nullptr;
    for (const auto* candidate : {own, parent_sheet, document_sheet}) {
      if (const auto* node = candidate != nullptr ? candidate->find("9") : nullptr; node != nullptr) {
        entry.auto_hyphenate = node->boolean();
        break;
      }
    }
    entry.kashida_width = resolve("40", 4);
    // The line composer (/29) resolves the same way: a run that leaves it out takes its parent
    // sheet's (Photoshop 2026 render probes: the parent's value decides, not the TySh run's).
    for (const auto* candidate : {own, parent_sheet, document_sheet}) {
      if (const auto* node = candidate != nullptr ? candidate->find("29") : nullptr; node != nullptr) {
        entry.every_line_composer = node->boolean();
        break;
      }
    }
    entry.hyphenated_word_size = key_value(own, "10", 1000);
    entry.pre_hyphen = key_value(own, "11", 1000);
    entry.post_hyphen = key_value(own, "12", 1000);
    entry.consecutive_hyphens = key_value(own, "13", 1000);
    if (const auto* node = own != nullptr ? own->find("14") : nullptr; node != nullptr) {
      if (const auto value = node->number(); value && std::isfinite(*value) && *value >= 0.0 && *value <= 100000.0) {
        entry.hyphenation_zone = value;
      }
    }
    if (const auto* node = own != nullptr ? own->find("15") : nullptr; node != nullptr) {
      entry.hyphenate_capitalized = node->boolean();
    }
    any = any || entry.composer.has_value() || entry.direction.has_value() || entry.auto_hyphenate.has_value() ||
          entry.kashida_width.has_value() || entry.hyphenated_word_size.has_value() || entry.pre_hyphen.has_value() ||
          entry.post_hyphen.has_value() || entry.consecutive_hyphens.has_value() ||
          entry.hyphenation_zone.has_value() || entry.hyphenate_capitalized.has_value() ||
          entry.every_line_composer.has_value();
    result.push_back(entry);
  }
  if (!any) result.clear();
  return result;
}

std::vector<TextStyleRunKerning> TextEngineBlock::object_style_kerning(std::size_t index) const {
  std::vector<TextStyleRunKerning> result;
  const auto* list = objects();
  const auto* object = list != nullptr && index < list->list.size() ? &list->list[index] : nullptr;
  const auto* runs = object != nullptr ? object->at_path({"0", "6", "0"}) : nullptr;
  if (runs == nullptr || !runs->is_list()) return result;
  const auto* style_sheets = root_.at_path({"0", "5", "0"});
  const auto* paragraph_sheets = root_.at_path({"0", "6", "0"});
  const auto* document_sheet = root_.at_path({"1", "2"});
  const auto* paragraph_runs = object->at_path({"0", "5", "0"});
  // The character style (/32) of the paragraph a text position sits in: the paragraph run's own
  // sheet first, then its parent ParagraphSheetSet entry's.
  const auto paragraph_styles_at = [&](int position) -> std::pair<const EngineNode*, const EngineNode*> {
    if (paragraph_runs == nullptr || !paragraph_runs->is_list()) return {nullptr, nullptr};
    int start = 0;
    for (std::size_t p = 0; p < paragraph_runs->list.size(); ++p) {
      const auto& run = paragraph_runs->list[p];
      const auto* length = run.at_path({"1"});
      const auto run_length =
          static_cast<int>(std::clamp<long long>(length ? length->integer().value_or(0) : 0, 0, 1 << 30));
      if (position < start + run_length || p + 1 == paragraph_runs->list.size()) {
        const auto* sheet = run.at_path({"0", "0"});
        const auto* own = sheet != nullptr ? sheet->at_path({"5", "32"}) : nullptr;
        const EngineNode* parent_style = nullptr;
        if (sheet != nullptr && paragraph_sheets != nullptr && paragraph_sheets->is_list()) {
          const auto* parent = sheet->at_path({"6"});
          const auto parent_index = parent != nullptr ? parent->integer() : std::nullopt;
          if (parent_index && *parent_index >= 0 &&
              static_cast<std::size_t>(*parent_index) < paragraph_sheets->list.size()) {
            parent_style = paragraph_sheets->list[static_cast<std::size_t>(*parent_index)].at_path({"0", "5", "32"});
          }
        }
        return {own, parent_style};
      }
      start += run_length;
    }
    return {nullptr, nullptr};
  };
  bool any = false;
  int position = 0;
  for (const auto& run : runs->list) {
    TextStyleRunKerning entry;
    const auto* length = run.at_path({"1"});
    entry.length = static_cast<int>(std::clamp<long long>(length ? length->integer().value_or(0) : 0, 0, 1 << 30));
    const auto* sheet = run.at_path({"0", "0", "6"});
    const EngineNode* parent_sheet = nullptr;
    if (style_sheets != nullptr && style_sheets->is_list()) {
      const auto* parent = run.at_path({"0", "0", "5"});
      const auto parent_index = parent != nullptr ? parent->integer() : std::nullopt;
      if (parent_index && *parent_index >= 0 && static_cast<std::size_t>(*parent_index) < style_sheets->list.size()) {
        parent_sheet = style_sheets->list[static_cast<std::size_t>(*parent_index)].at_path({"0", "6"});
      }
    }
    const auto [paragraph_own, paragraph_parent] = paragraph_styles_at(position);
    for (const auto* candidate : {sheet, paragraph_own, paragraph_parent, parent_sheet, document_sheet}) {
      const auto* node = candidate != nullptr && candidate->is_dict() ? candidate->find("11") : nullptr;
      const auto mode = node != nullptr ? node->integer() : std::nullopt;
      if (!mode || *mode < kTextKerningManual || *mode > kTextKerningOptical) continue;
      entry.mode = static_cast<int>(*mode);
      if (*mode == kTextKerningManual) {
        // The number sits beside the mode that names it; a sheet without one means 0.
        const auto* number = candidate->find("10");
        const auto value = number != nullptr ? number->number() : std::nullopt;
        entry.kerning = value && std::isfinite(*value) && std::abs(*value) <= 100000.0 ? *value : 0.0;
      }
      break;
    }
    any = any || entry.mode.has_value();
    result.push_back(entry);
    position += entry.length;
  }
  if (!any) result.clear();
  return result;
}

std::vector<TextFrameGeometry> read_text_frame_geometries(std::span<const std::uint8_t> payload) {
  std::vector<TextFrameGeometry> result;
  if (const auto engine = TextEngineBlock::parse(payload)) {
    result.reserve(engine->object_count());
    for (std::size_t i = 0; i < engine->object_count(); ++i) {
      auto geometry = engine->object_geometry(i);
      geometry.paragraph_font_sizes = engine->object_paragraph_font_sizes(i);
      geometry.style_features = engine->object_style_features(i);
      geometry.paragraph_composers = engine->object_paragraph_composers(i);
      geometry.style_kerning = engine->object_style_kerning(i);
      result.push_back(std::move(geometry));
    }
    return result;
  }
  // Photoshop 6-era blocks use names instead of the modern numeric key map.
  // Dungeon Scroll's point frames use TextOnPathTRange [-1 -1]. Leaving these
  // editable preserves the established legacy typography and raster behavior.
  const auto root = parse_engine_data(payload, true);
  const auto* objects = root ? root->at_path({"DocumentObjects", "TextObjects"}) : nullptr;
  const auto* frames = root ? root->at_path({"DocumentResources", "TextFrameSet", "Resources"}) : nullptr;
  if (!objects || !objects->is_list() || !frames || !frames->is_list()) return result;
  for (const auto& object : objects->list) {
    TextFrameGeometry geometry;
    const auto* frame_index = object.at_path({"View", "Frames", "0", "Resource"});
    const auto index = frame_index ? frame_index->integer() : std::nullopt;
    if (index && *index >= 0 && static_cast<std::size_t>(*index) < frames->list.size()) {
      const auto* marker = frames->list[static_cast<std::size_t>(*index)].at_path({"Resource", "Data", "TextOnPathTRange"});
      if (marker && marker->is_list() && marker->list.size() == 2) {
        const auto mode = marker->list[0].number();
        if (mode && marker->list[1].number() == mode) {
          if (*mode == -1.0) geometry.kind = TextFrameGeometry::Kind::Point;
          if (*mode == -2.0) geometry.kind = TextFrameGeometry::Kind::Box;
        }
      }
    }
    result.push_back(std::move(geometry));
  }
  return result;
}

bool text_engine_block_is_named_key_engine(std::span<const std::uint8_t> payload) {
  if (TextEngineBlock::parse(payload).has_value()) {
    return false;
  }
  const auto root = parse_engine_data(payload, true);
  const auto* objects = root ? root->at_path({"DocumentObjects", "TextObjects"}) : nullptr;
  return objects != nullptr && objects->is_list();
}

void import_text_frame_geometry(Document& document) {
  const auto& blocks = std::as_const(document).metadata().unknown_psd_resources;
  const auto found = std::find_if(blocks.begin(), blocks.end(), [](const auto& b) { return b.key == "Txt2"; });
  if (found == blocks.end()) return;
  const auto frames = read_text_frame_geometries(found->payload);
  const auto visit = [&](auto&& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      const auto& metadata = std::as_const(layer).metadata();
      if (const auto i = metadata.find(kLayerMetadataPsdTextIndex); i != metadata.end()) {
        char* end = nullptr;
        const auto index = std::strtoll(i->second.c_str(), &end, 10);
        // A regenerated layer beside a verbatim legacy engine uses the existing
        // writer's deliberately unaddressed 100000+n TySh index.
        if (index >= kRegeneratedTextIndexBase && static_cast<std::size_t>(index) >= frames.size()) continue;
        auto geometry = end != i->second.c_str() && *end == '\0' && index >= 0 &&
                            static_cast<std::size_t>(index) < frames.size()
                                  ? frames[static_cast<std::size_t>(index)]
                                  : TextFrameGeometry{};
        if (geometry.kind == TextFrameGeometry::Kind::Area) {
          // Native frames carry cubic coordinates, not the editor's handle-link
          // flags. Restore those private flags only when the native contour is
          // exactly the one Patchy saved; native geometry always wins.
          if (const auto stored = text_area_for_layer(std::as_const(layer)); stored &&
              stored->subpaths.front().anchors.size() == geometry.area->subpaths.front().anchors.size()) {
            auto linked = *geometry.area;
            for (std::size_t a = 0; a < linked.subpaths.front().anchors.size(); ++a)
              linked.subpaths.front().anchors[a].smooth = stored->subpaths.front().anchors[a].smooth;
            if (linked == *stored) geometry.area = std::move(linked);
          }
          layer.metadata()[kLayerMetadataTextArea] = serialize_vector_path(*geometry.area);
          layer.metadata()[kLayerMetadataTextFlow] = "box";
          layer.metadata().erase(kLayerMetadataTextGeometryProtected);
        } else if (geometry.kind == TextFrameGeometry::Kind::Path) {
          // The same rule as the area contour above: Patchy's own handle-link flags come back
          // only when the native contour is exactly the one Patchy saved.
          if (const auto stored = text_path_for_layer(std::as_const(layer)); stored &&
              stored->subpaths.front().anchors.size() == geometry.path->subpaths.front().anchors.size()) {
            auto linked = *geometry.path;
            for (std::size_t a = 0; a < linked.subpaths.front().anchors.size(); ++a)
              linked.subpaths.front().anchors[a].smooth = stored->subpaths.front().anchors[a].smooth;
            if (linked == *stored) geometry.path = std::move(linked);
          }
          layer.metadata()[kLayerMetadataTextPath] = serialize_vector_path(*geometry.path);
          layer.metadata()[kLayerMetadataTextPathRange] = serialize_text_path_range(geometry.path_range);
          layer.metadata().erase(kLayerMetadataTextArea);
          layer.metadata()[kLayerMetadataTextFlow] = "box";
          layer.metadata().erase(kLayerMetadataTextGeometryProtected);
        } else if (geometry.kind == TextFrameGeometry::Kind::Unsupported) {
          layer.metadata()[kLayerMetadataTextGeometryProtected] = "true";
        }
      }
      self(self, layer.children());
    }
  };
  visit(visit, document.layers());
}

EngineNode TextEngineBlock::author_frame(const TextEngineInputs& inputs) const {
  if (inputs.path && valid_text_path(*inputs.path)) {
    // Photoshop's own path-text frame, key for key: the contour's cubic segments, the frame
    // type and the range. It carries no /11 and no baseline rule (docs/path-text.md).
    auto contour = engine_list();
    const auto& sub = inputs.path->subpaths.front();
    const auto segments = static_cast<std::size_t>(text_path_segment_count(*inputs.path));
    for (std::size_t i = 0; i < segments; ++i) {
      const auto& a = sub.anchors[i];
      const auto& b = sub.anchors[(i + 1) % sub.anchors.size()];
      for (double v : {a.anchor_x, a.anchor_y, a.out_x, a.out_y, b.in_x, b.in_y, b.anchor_x, b.anchor_y}) {
        contour.list.push_back(engine_number(v));
      }
    }
    const auto range = normalized_text_path_range(*inputs.path, inputs.path_range);
    auto path_settings = engine_dict_of(
        {{"0", engine_integer(2)}, {"6", engine_list_of({engine_number(range.start), engine_number(range.end)})}});
    auto path_frame = engine_dict_of({{"1", engine_dict_of({{"0", std::move(contour)}})}, {"2", std::move(path_settings)}});
    return engine_dict_of({{"0", std::move(path_frame)}});
  }
  const double k = inputs.area ? 3.0 : inputs.boxed ? 2.0 : 1.0;
  auto settings = engine_dict();
  if (inputs.boxed) {
    settings.set("0", engine_integer(1));
  }
  if (inputs.vertical) {
    settings.set("1", engine_integer(2));
  }
  settings.set("6", engine_list_of({engine_number(-k), engine_number(-k)}));
  if (inputs.boxed && !inputs.area && !inputs.photoshop_layout) {
    // The frame's first-baseline rule (Action Manager `frameBaselineAlignment`): /10 /0 2 is the
    // cap height, which is where the writer's baseline inset puts a Qt-natural Patchy box
    // (text_geometry_for_layer). A layer laid out by Photoshop's rules has its first baseline at
    // the ascent, Photoshop's own default, which is a frame WITHOUT the key (every box frame
    // Photoshop writes): with the key Photoshop read alignByCapHeight for a re-rendered
    // Photoshop layer and moved its first line up by the ascent less the cap height.
    settings.set("10", engine_dict_of({{"0", engine_integer(2)}, {"1", engine_number(0.0)}}));
  }
  settings.set("11", engine_dict_of({{"4", engine_integer(static_cast<long long>(-k))},
                                     {"18", engine_number(-k)},
                                     {"22", engine_number(0.025)}}));
  auto frame = engine_dict();
  if (inputs.area) {
    auto path = engine_list();
    const auto& anchors = inputs.area->subpaths.front().anchors;
    for (std::size_t i = 0; i < anchors.size(); ++i) {
      const auto& a = anchors[i];
      const auto& b = anchors[(i + 1) % anchors.size()];
      for (double v : {a.anchor_x, a.anchor_y, a.out_x, a.out_y,
                       b.in_x, b.in_y, b.anchor_x, b.anchor_y}) {
        path.list.push_back(engine_number(v));
      }
    }
    frame.set("1", engine_dict_of({{"0", std::move(path)}}));
  } else if (inputs.boxed) {
    // The box path: one straight cubic segment per edge, start, both handles, end, like the
    // area path above (Photoshop's own frames: (0,0) (0,0) (w,0) (w,0), then the right edge).
    // Four degenerate segments, each corner repeated, read as text but a warped box layer
    // opened with the "text layers might need to be updated" prompt (PS 2025 and 2026).
    const double w = std::max(1.0, std::isfinite(inputs.box_width) ? inputs.box_width : 1.0);
    const double h = std::max(1.0, std::isfinite(inputs.box_height) ? inputs.box_height : 1.0);
    auto path = engine_list();
    const double corners[4][2] = {{0.0, 0.0}, {w, 0.0}, {w, h}, {0.0, h}};
    for (std::size_t i = 0; i < 4; ++i) {
      const double* segment[4] = {corners[i], corners[i], corners[(i + 1) % 4], corners[(i + 1) % 4]};
      for (const double* point : segment) {
        path.list.push_back(engine_number(point[0]));
        path.list.push_back(engine_number(point[1]));
      }
    }
    frame.set("1", engine_dict_of({{"0", std::move(path)}}));
  }
  frame.set("2", std::move(settings));
  return engine_dict_of({{"0", std::move(frame)}});
}

EngineNode TextEngineBlock::author_object(const TextEngineInputs& inputs, std::size_t frame_index) {
  const auto units = static_cast<int>(utf8_to_utf16(inputs.text).size());
  const PsdTextParagraphRun paragraph_fallback;
  const PsdTextStyleRun style_fallback;
  const auto paragraphs = covering_runs<PsdTextParagraphRun>(inputs.paragraph_runs, units, paragraph_fallback);
  const auto styles = covering_runs<PsdTextStyleRun>(inputs.runs, units, style_fallback);

  auto paragraph_runs = engine_list();
  auto view_runs = engine_list();
  for (const auto& [run, length] : paragraphs) {
    // /6 = 0: the paragraph metrics are document pixels, like the style runs' /5 below.
    auto sheet = engine_dict_of({{"0", engine_string("")}, {"5", paragraph_sheet(*run)}, {"6", engine_integer(0)}});
    paragraph_runs.list.push_back(engine_dict_of(
        {{"0", engine_dict_of({{"0", std::move(sheet)}})}, {"1", engine_integer(length)}}));
    view_runs.list.push_back(
        engine_dict_of({{"0", engine_dict_of({{"1", engine_integer(1)}})}, {"1", engine_integer(length)}}));
  }

  // A run takes the auto-leading fraction of the paragraph its first character sits in.
  const auto fraction_at = [&](int position) {
    int start = 0;
    for (const auto& [run, length] : paragraphs) {
      if (position < start + length) {
        const auto fraction = run->auto_leading_fraction;
        return std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0 ? fraction : 1.2;
      }
      start += length;
    }
    return 1.2;
  };
  auto style_runs = engine_list();
  int position = 0;
  for (const auto& [run, length] : styles) {
    const auto run_index = static_cast<std::size_t>(run - (inputs.runs.empty() ? &style_fallback : inputs.runs.data()));
    std::string font_name = run == &style_fallback || run_index >= inputs.run_font_names.size()
                                ? std::string("Arial")
                                : inputs.run_font_names[run_index];
    if (font_name.empty()) {
      font_name = "Arial";
    }
    // /5 is the unit of the sheet's sizes: 1 = points at the document PPI (a 28 px layer read
    // back as 116.67 px in a 300 ppi document), 0 = document pixels, which is what the TySh
    // engine units and every value here are (Photoshop writes 0 for its own objects of a
    // Patchy layer; September 27, 2026 readback).
    auto sheet = engine_dict_of({{"0", engine_string("")},
                                 {"5", engine_integer(0)},
                                 {"6", style_sheet(*run, font_index(font_name), fraction_at(position))}});
    style_runs.list.push_back(
        engine_dict_of({{"0", engine_dict_of({{"0", std::move(sheet)}})}, {"1", engine_integer(length)}}));
    position += length;
  }

  // Standard Vertical Roman Alignment (rotated Latin) is the object's /10 /0 = 4 in Photoshop's
  // vertical captures; upright and horizontal text carry 1.
  const bool rotated_roman = inputs.vertical && std::any_of(inputs.runs.begin(), inputs.runs.end(), [](const auto& run) {
                               return run.baseline_direction == 2;
                             });
  auto model = engine_dict_of({{"0", engine_string(inputs.text)},
                               {"5", engine_dict_of({{"0", std::move(paragraph_runs)}})},
                               {"6", engine_dict_of({{"0", std::move(style_runs)}})},
                               {"10", engine_dict_of({{"0", engine_integer(rotated_roman ? 4 : 1)}, {"2", engine_bool(true)}})}});
  auto view = engine_dict_of({{"4", engine_version_node()},
                              {"5", engine_integer(0)},
                              {"0", engine_list_of({engine_dict_of({{"0", engine_integer(static_cast<long long>(frame_index))}})})},
                              {"1", engine_dict_of({{"0", std::move(view_runs)}})},
                              {"2", engine_list()}});
  return engine_dict_of({{"0", std::move(model)}, {"1", std::move(view)}});
}

std::size_t TextEngineBlock::set_object(std::size_t index, const TextEngineInputs& inputs) {
  auto* list = objects();
  auto* frame_list = frames();
  if (list == nullptr || frame_list == nullptr) {
    return 0;
  }
  if (index >= list->list.size()) {
    return append_object(inputs);
  }
  // Reuse the frame the old object referenced when it exists, else give it a fresh one.
  auto frame_index = object_frame_index(index);
  if (!frame_index.has_value() || *frame_index >= frame_list->list.size()) {
    frame_list->list.push_back(author_frame(inputs));
    frame_index = frame_list->list.size() - 1U;
  } else {
    frame_list->list[*frame_index] = author_frame(inputs);
  }
  list->list[index] = author_object(inputs, *frame_index);
  return index;
}

std::size_t TextEngineBlock::append_object(const TextEngineInputs& inputs) {
  auto* list = objects();
  auto* frame_list = frames();
  if (list == nullptr || frame_list == nullptr) {
    return 0;
  }
  frame_list->list.push_back(author_frame(inputs));
  const auto frame_index = frame_list->list.size() - 1U;
  list->list.push_back(author_object(inputs, frame_index));
  return list->list.size() - 1U;
}

void TextEngineBlock::strip_layout_caches() {
  auto* list = objects();
  if (list == nullptr) {
    return;
  }
  // An empty list, not a missing key: Photoshop 2025 (26.9) crashes opening a block whose
  // objects all lack the /2 key, and recomposes from the model when it is empty (GitHub
  // issue 341); Photoshop 2026 accepts both.
  for (auto& object : list->list) {
    if (auto* view = object.find("1"); view != nullptr) {
      if (const auto* cache = view->find("2"); cache == nullptr || !cache->is_list() || !cache->list.empty()) {
        view->set("2", engine_list());
      }
    }
  }
}

}  // namespace patchy::psd
