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
      {"0", engine_integer(std::clamp(run.justification, 0, 3))},
      {"1", engine_number(run.first_line_indent)},
      {"2", engine_number(run.start_indent)},
      {"3", engine_number(run.end_indent)},
      {"4", engine_number(run.space_before)},
      {"5", engine_number(run.space_after)},
      {"7", engine_number(fraction)},
      {"8", engine_integer(0)},
      {"9", engine_bool(true)},
      {"10", engine_integer(6)},
      {"11", engine_integer(2)},
      {"12", engine_integer(2)},
      {"13", engine_integer(8)},
      {"14", engine_number(36.0)},
      {"15", engine_bool(true)},
      {"17", triple(0.8, 1.0, 1.33)},
      {"18", triple(0.0, 0.0, 0.0)},
      {"19", triple(1.0, 1.0, 1.0)},
      {"21", engine_bool(false)},
      {"23", engine_bool(true)},
      {"24", engine_integer(0)},
      {"25", engine_integer(0)},
      {"27", engine_nil()},
      {"28", engine_nil()},
      {"29", engine_bool(false)},
      {"33", engine_integer(run.direction == "rtl" ? 1 : 0)},
      {"35", engine_integer(1)},
      {"36", engine_nil()},
      {"37", engine_integer(0)},
      {"40", engine_integer(2)},
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
  return engine_dict_of({
      {"0", engine_integer(std::max(0, font_index))},
      {"1", engine_number(size)},
      {"2", engine_bool(run.faux_bold)},
      {"3", engine_bool(run.faux_italic)},
      {"4", engine_bool(!fixed_leading)},
      {"5", engine_number(leading)},
      {"6", engine_number(scale(run.horizontal_scale))},
      {"7", engine_number(scale(run.vertical_scale))},
      {"8", engine_integer(tracking)},
      {"9", engine_number(0.0)},
      {"11", engine_integer(1)},
      {"12", engine_integer(0)},
      {"13", engine_integer(0)},
      {"14", engine_integer(0)},
      {"15", engine_integer(0)},
      {"16", engine_integer(0)},
      {"18", engine_bool(true)},
      {"19", engine_bool(false)},
      {"20", engine_bool(false)},
      {"23", engine_bool(false)},
      {"24", engine_bool(false)},
      {"25", engine_bool(false)},
      {"26", engine_bool(false)},
      {"27", engine_bool(false)},
      {"28", engine_bool(false)},
      {"29", engine_bool(false)},
      {"30", engine_integer(0)},
      {"31", engine_bool(false)},
      {"32", engine_bool(false)},
      {"33", engine_bool(false)},
      {"35", engine_integer(1)},
      {"36", engine_number(0.0)},
      {"37", engine_integer(2)},
      {"38", engine_integer(14)},
      {"39", engine_integer(0)},
      {"52", engine_bool(false)},
      {"53", paint_node(run.color)},
      {"54", paint_node(RgbColor{0, 0, 0})},
      {"68", engine_integer(0)},
      {"70", engine_integer(0)},
      {"72", engine_number(0.0)},
      {"73", engine_number(0.0)},
      {"75", engine_bool(false)},
      {"85", engine_integer(0)},
      {"87", engine_number(100.0)},
      {"88", engine_bool(false)},
      {"92", engine_number(0.0)},
  });
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

TextFrameGeometry TextEngineBlock::object_geometry(std::size_t index) const {
  const auto fi = object_frame_index(index);
  const auto* frame_list = root_.at_path({"0", "8", "0"});
  if (!fi || !frame_list || !frame_list->is_list() || *fi >= frame_list->list.size()) return {};
  const auto& frame = frame_list->list[*fi];
  const auto* marker = frame.at_path({"0", "2", "6"});
  if (!marker || !marker->is_list() || marker->list.size() != 2) return {};
  const auto mode = marker->list[0].number();
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

std::vector<TextFrameGeometry> read_text_frame_geometries(std::span<const std::uint8_t> payload) {
  std::vector<TextFrameGeometry> result;
  if (const auto engine = TextEngineBlock::parse(payload)) {
    result.reserve(engine->object_count());
    for (std::size_t i = 0; i < engine->object_count(); ++i) result.push_back(engine->object_geometry(i));
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
  const double k = inputs.area ? 3.0 : inputs.boxed ? 2.0 : 1.0;
  auto settings = engine_dict();
  if (inputs.boxed) {
    settings.set("0", engine_integer(1));
  }
  if (inputs.vertical) {
    settings.set("1", engine_integer(2));
  }
  settings.set("6", engine_list_of({engine_number(-k), engine_number(-k)}));
  if (inputs.boxed && !inputs.area) {
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
    // The box path: four corners, each written four times (the point and its handles).
    const double w = std::max(1.0, std::isfinite(inputs.box_width) ? inputs.box_width : 1.0);
    const double h = std::max(1.0, std::isfinite(inputs.box_height) ? inputs.box_height : 1.0);
    auto path = engine_list();
    const double corners[4][2] = {{0.0, 0.0}, {w, 0.0}, {w, h}, {0.0, h}};
    for (const auto& corner : corners) {
      for (int repeat = 0; repeat < 4; ++repeat) {
        path.list.push_back(engine_number(corner[0]));
        path.list.push_back(engine_number(corner[1]));
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
                              {"1", engine_dict_of({{"0", std::move(view_runs)}})}});
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
  for (auto& object : list->list) {
    if (auto* view = object.find("1"); view != nullptr) {
      view->erase("2");
    }
  }
}

}  // namespace patchy::psd
