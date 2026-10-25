// Krita 2 and 3 vector layers (docs/kra.md "Vector layers"): `layerN.shapelayer/content.xml`,
// an OpenDocument drawing (the OASIS ODF 1.2 standard, ISO/IEC 26300). Shapes become an SVG
// document in points that the SVG importer reads, like Krita 4's own content.svg; text
// boxes become Patchy box text, rendered after the open like Affinity's frame text.

#include "formats/kra_internal.hpp"

#include "core/layer_metadata.hpp"
#include "formats/svg_xml.hpp"
#include "psd/psd_text_runs.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace patchy::kra::detail {

namespace {

[[nodiscard]] std::string_view local(std::string_view name) {
  const auto colon = name.rfind(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

// An attribute by its local name, whatever prefix the file used.
[[nodiscard]] const std::string* attr(const svg::XmlNode& node, std::string_view name) {
  for (const auto& [key, value] : node.attributes) {
    if (local(key) == name) {
      return &value;
    }
  }
  return nullptr;
}

[[nodiscard]] std::optional<double> number_prefix(std::string_view text, std::string_view* rest = nullptr) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  std::size_t end = 0;
  while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) != 0 || text[end] == '.' ||
                               text[end] == '-' || text[end] == '+' || text[end] == 'e' || text[end] == 'E')) {
    // An 'e' only counts as an exponent when a digit or sign follows.
    if ((text[end] == 'e' || text[end] == 'E') &&
        (end + 1 >= text.size() ||
         !(std::isdigit(static_cast<unsigned char>(text[end + 1])) != 0 || text[end + 1] == '-' || text[end + 1] == '+'))) {
      break;
    }
    ++end;
  }
  if (end == 0) {
    return std::nullopt;
  }
  std::istringstream stream{std::string(text.substr(0, end))};
  stream.imbue(std::locale::classic());
  double value = 0.0;
  stream >> value;
  if (stream.fail() || !std::isfinite(value)) {
    return std::nullopt;
  }
  if (rest != nullptr) {
    *rest = text.substr(end);
  }
  return value;
}

// A length with its unit, in points.
[[nodiscard]] std::optional<double> length_pt(std::string_view text) {
  std::string_view unit;
  const auto value = number_prefix(text, &unit);
  if (!value.has_value()) {
    return std::nullopt;
  }
  while (!unit.empty() && unit.front() == ' ') {
    unit.remove_prefix(1);
  }
  if (unit.substr(0, 2) == "cm") return *value * 72.0 / 2.54;
  if (unit.substr(0, 2) == "mm") return *value * 72.0 / 25.4;
  if (unit.substr(0, 2) == "in") return *value * 72.0;
  if (unit.substr(0, 2) == "pc") return *value * 12.0;
  if (unit.substr(0, 2) == "px") return *value * 0.75;
  return *value;  // pt, or no unit
}

[[nodiscard]] double length_or(const svg::XmlNode& node, std::string_view name, double fallback) {
  const auto* value = attr(node, name);
  return value == nullptr ? fallback : length_pt(*value).value_or(fallback);
}

[[nodiscard]] std::string fmt(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(10) << value;
  return out.str();
}

[[nodiscard]] std::string escape(std::string_view text) {
  std::string out;
  for (const char ch : text) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += ch;
    }
  }
  return out;
}

// ODF's draw:transform: "rotate (a) translate (x y) ..." with lengths in units and angles
// in radians, counterclockwise. As an SVG transform list in points (SVG rotates clockwise).
[[nodiscard]] std::string svg_transform(std::string_view text) {
  std::string out;
  while (true) {
    const auto open = text.find('(');
    const auto close = text.find(')', open);
    if (open == std::string_view::npos || close == std::string_view::npos) {
      break;
    }
    auto name = text.substr(0, open);
    while (!name.empty() && (name.front() == ' ' || name.front() == ',')) name.remove_prefix(1);
    while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
    auto args_text = text.substr(open + 1, close - open - 1);
    std::vector<double> args;
    std::string current;
    for (std::size_t i = 0; i <= args_text.size(); ++i) {
      if (i == args_text.size() || args_text[i] == ' ' || args_text[i] == ',') {
        if (!current.empty()) {
          args.push_back(length_pt(current).value_or(0.0));
          current.clear();
        }
      } else {
        current += args_text[i];
      }
    }
    if (name == "rotate" && !args.empty()) {
      out += " rotate(" + fmt(-args[0] * 180.0 / std::numbers::pi) + ")";
    } else if (name == "translate" && !args.empty()) {
      out += " translate(" + fmt(args[0]) + " " + fmt(args.size() > 1 ? args[1] : 0.0) + ")";
    } else if (name == "scale" && !args.empty()) {
      out += " scale(" + fmt(args[0]) + " " + fmt(args.size() > 1 ? args[1] : args[0]) + ")";
    } else if (name == "skewX" && !args.empty()) {
      out += " skewX(" + fmt(-args[0] * 180.0 / std::numbers::pi) + ")";
    } else if (name == "skewY" && !args.empty()) {
      out += " skewY(" + fmt(-args[0] * 180.0 / std::numbers::pi) + ")";
    } else if (name == "matrix" && args.size() == 6) {
      out += " matrix(" + fmt(args[0]) + " " + fmt(args[1]) + " " + fmt(args[2]) + " " + fmt(args[3]) + " " +
             fmt(args[4]) + " " + fmt(args[5]) + ")";
    }
    text = text.substr(close + 1);
  }
  return out;
}

struct GraphicStyle {
  std::map<std::string, std::string> graphic;    // graphic-properties by local name
  std::map<std::string, std::string> text;       // text-properties by local name
  std::map<std::string, std::string> paragraph;  // paragraph-properties by local name
};

using Styles = std::map<std::string, GraphicStyle>;

void collect_styles(const svg::XmlNode& node, Styles& styles) {
  for (const auto& child : node.children) {
    if (child.is_text()) {
      continue;
    }
    if (local(child.name) == "style") {
      if (const auto* name = attr(child, "name"); name != nullptr) {
        auto& style = styles[*name];
        for (const auto& properties : child.children) {
          std::map<std::string, std::string>* target = nullptr;
          if (local(properties.name) == "graphic-properties") target = &style.graphic;
          if (local(properties.name) == "text-properties") target = &style.text;
          if (local(properties.name) == "paragraph-properties") target = &style.paragraph;
          if (target != nullptr) {
            for (const auto& [key, value] : properties.attributes) {
              (*target)[std::string(local(key))] = value;
            }
          }
        }
      }
    }
    collect_styles(child, styles);
  }
}

[[nodiscard]] std::string get(const std::map<std::string, std::string>& map, const std::string& key,
                              std::string fallback = {}) {
  const auto found = map.find(key);
  return found == map.end() ? std::move(fallback) : found->second;
}

// SVG presentation attributes for a shape's graphic style.
[[nodiscard]] std::string svg_paint(const GraphicStyle* style) {
  if (style == nullptr) {
    return " fill=\"#ffffff\" stroke=\"#000000\"";
  }
  const auto& g = style->graphic;
  std::string out;
  const auto fill = get(g, "fill", "solid");
  out += fill == "none" ? " fill=\"none\"" : " fill=\"" + escape(get(g, "fill-color", "#ffffff")) + "\"";
  if (const auto opacity = get(g, "opacity"); !opacity.empty()) {
    out += " fill-opacity=\"" + fmt(number_prefix(opacity).value_or(100.0) / 100.0) + "\"";
  }
  if (get(g, "fill-rule") == "evenodd") {
    out += " fill-rule=\"evenodd\"";
  }
  const auto stroke = get(g, "stroke", "solid");
  if (stroke == "none") {
    out += " stroke=\"none\"";
  } else {
    out += " stroke=\"" + escape(get(g, "stroke-color", "#000000")) + "\"";
    out += " stroke-width=\"" + fmt(length_pt(get(g, "stroke-width", "0")).value_or(0.0)) + "\"";
    if (const auto opacity = get(g, "stroke-opacity"); !opacity.empty()) {
      const auto value = number_prefix(opacity).value_or(1.0);
      out += " stroke-opacity=\"" + fmt(opacity.find('%') != std::string::npos ? value / 100.0 : value) + "\"";
    }
    if (const auto cap = get(g, "stroke-linecap"); !cap.empty()) {
      out += " stroke-linecap=\"" + escape(cap) + "\"";
    }
    if (const auto join = get(g, "stroke-linejoin"); !join.empty()) {
      out += " stroke-linejoin=\"" + escape(join) + "\"";
    }
  }
  return out;
}

struct TextBox {
  std::string text;
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};
  std::string family{"Sans Serif"};
  double size_pt{12.0};
  RgbColor color{0, 0, 0};
  bool bold{false};
  bool italic{false};
  int align{0};  // 0 left, 1 center, 2 right
};

[[nodiscard]] RgbColor parse_hex(std::string_view text, RgbColor fallback) {
  if (text.size() != 7 || text[0] != '#') {
    return fallback;
  }
  const auto byte = [&](std::size_t at) {
    return static_cast<std::uint8_t>(std::stoi(std::string(text.substr(at, 2)), nullptr, 16));
  };
  try {
    return RgbColor{byte(1), byte(3), byte(5)};
  } catch (const std::exception&) {
    return fallback;
  }
}

void apply_text_style(const GraphicStyle& style, TextBox& box) {
  if (const auto family = get(style.text, "font-family"); !family.empty()) {
    box.family = family;
  }
  if (const auto size = get(style.text, "font-size"); !size.empty()) {
    box.size_pt = length_pt(size).value_or(box.size_pt);
  }
  if (const auto color = get(style.text, "color"); !color.empty()) {
    box.color = parse_hex(color, box.color);
  }
  if (const auto weight = get(style.text, "font-weight"); !weight.empty()) {
    box.bold = weight == "bold" || number_prefix(weight).value_or(400.0) >= 600.0;
  }
  if (const auto italic = get(style.text, "font-style"); !italic.empty()) {
    box.italic = italic == "italic" || italic == "oblique";
  }
  if (const auto align = get(style.paragraph, "text-align"); !align.empty()) {
    box.align = align == "center" ? 1 : (align == "end" || align == "right") ? 2 : 0;
  }
}

void collect_text(const svg::XmlNode& node, std::string& out, const Styles& styles, TextBox& box) {
  for (const auto& child : node.children) {
    if (child.is_text()) {
      out += child.text;
      continue;
    }
    const auto name = local(child.name);
    if (name == "s") {
      const auto count = attr(child, "c") != nullptr ? std::max(1, std::atoi(attr(child, "c")->c_str())) : 1;
      out.append(static_cast<std::size_t>(std::min(count, 1000)), ' ');
    } else if (name == "tab") {
      out += '\t';
    } else if (name == "line-break") {
      out += '\n';
    } else {
      if (const auto* style = attr(child, "style-name"); style != nullptr) {
        if (const auto found = styles.find(*style); found != styles.end()) {
          apply_text_style(found->second, box);
        }
      }
      collect_text(child, out, styles, box);
    }
  }
}

struct Converter {
  const Styles& styles;
  std::string svg;
  std::vector<TextBox> boxes;
  int skipped{0};

  [[nodiscard]] const GraphicStyle* style_of(const svg::XmlNode& node) const {
    const auto* name = attr(node, "style-name");
    if (name == nullptr) {
      return nullptr;
    }
    const auto found = styles.find(*name);
    return found == styles.end() ? nullptr : &found->second;
  }

  // The shape's placement: its own transform, else its x/y as a translation; viewBox
  // content (paths, polygons) is scaled into the shape's width and height.
  [[nodiscard]] std::string placement(const svg::XmlNode& node) const {
    if (const auto* transform = attr(node, "transform"); transform != nullptr) {
      return svg_transform(*transform);
    }
    return " translate(" + fmt(length_or(node, "x", 0.0)) + " " + fmt(length_or(node, "y", 0.0)) + ")";
  }

  [[nodiscard]] std::string view_box_scale(const svg::XmlNode& node) const {
    const auto* view_box = attr(node, "viewBox");
    if (view_box == nullptr) {
      return {};
    }
    std::vector<double> values;
    std::string_view rest = *view_box;
    while (auto value = number_prefix(rest, &rest)) {
      values.push_back(*value);
      while (!rest.empty() && (rest.front() == ' ' || rest.front() == ',')) rest.remove_prefix(1);
    }
    if (values.size() != 4 || values[2] == 0.0 || values[3] == 0.0) {
      return {};
    }
    const double width = length_or(node, "width", values[2]);
    const double height = length_or(node, "height", values[3]);
    return " scale(" + fmt(width / values[2]) + " " + fmt(height / values[3]) + ") translate(" + fmt(-values[0]) + " " +
           fmt(-values[1]) + ")";
  }

  void shape(const svg::XmlNode& node) {
    const auto name = local(node.name);
    const auto* style = style_of(node);
    if (name == "g") {
      svg += "<g>";
      for (const auto& child : node.children) {
        if (!child.is_text()) shape(child);
      }
      svg += "</g>";
      return;
    }
    if (name == "rect") {
      const double radius = length_or(node, "corner-radius", 0.0);
      svg += "<rect transform=\"" + placement(node) + "\" width=\"" + fmt(length_or(node, "width", 0.0)) +
             "\" height=\"" + fmt(length_or(node, "height", 0.0)) + "\"" +
             (radius > 0.0 ? " rx=\"" + fmt(radius) + "\" ry=\"" + fmt(radius) + "\"" : std::string()) +
             svg_paint(style) + "/>";
    } else if (name == "ellipse" || name == "circle") {
      const double width = length_or(node, "width", 0.0);
      const double height = length_or(node, "height", 0.0);
      svg += "<ellipse transform=\"" + placement(node) + "\" cx=\"" + fmt(width / 2) + "\" cy=\"" + fmt(height / 2) +
             "\" rx=\"" + fmt(width / 2) + "\" ry=\"" + fmt(height / 2) + "\"" + svg_paint(style) + "/>";
    } else if (name == "path") {
      const auto* d = attr(node, "d");
      if (d == nullptr) return;
      svg += "<path transform=\"" + placement(node) + view_box_scale(node) + "\" d=\"" + escape(*d) + "\"" +
             svg_paint(style) + "/>";
    } else if (name == "polygon" || name == "polyline") {
      const auto* points = attr(node, "points");
      if (points == nullptr) return;
      svg += "<" + std::string(name) + " transform=\"" + placement(node) + view_box_scale(node) + "\" points=\"" +
             escape(*points) + "\"" + svg_paint(style) + "/>";
    } else if (name == "line") {
      svg += "<line x1=\"" + fmt(length_or(node, "x1", 0.0)) + "\" y1=\"" + fmt(length_or(node, "y1", 0.0)) +
             "\" x2=\"" + fmt(length_or(node, "x2", 0.0)) + "\" y2=\"" + fmt(length_or(node, "y2", 0.0)) + "\"" +
             (attr(node, "transform") != nullptr ? " transform=\"" + svg_transform(*attr(node, "transform")) + "\""
                                                 : std::string()) +
             svg_paint(style) + "/>";
    } else if (name == "frame") {
      frame(node);
    } else if (name != "glue-point") {
      ++skipped;
    }
  }

  void frame(const svg::XmlNode& node) {
    const svg::XmlNode* text_box = nullptr;
    for (const auto& child : node.children) {
      if (!child.is_text() && local(child.name) == "text-box") {
        text_box = &child;
      }
    }
    if (text_box == nullptr) {
      ++skipped;  // an image or object frame
      return;
    }
    TextBox box;
    box.x = length_or(node, "x", 0.0);
    box.y = length_or(node, "y", 0.0);
    box.width = length_or(node, "width", 0.0);
    box.height = length_or(node, "height", 0.0);
    std::string text;
    for (const auto& paragraph : text_box->children) {
      if (paragraph.is_text()) continue;
      if (const auto* style = attr(paragraph, "style-name"); style != nullptr) {
        if (const auto found = styles.find(*style); found != styles.end()) {
          apply_text_style(found->second, box);
        }
      }
      if (!text.empty()) text += '\n';
      collect_text(paragraph, text, styles, box);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.pop_back();
    if (!text.empty()) {
      box.text = std::move(text);
      boxes.push_back(std::move(box));
    }
  }
};

[[nodiscard]] int utf16_length(std::string_view text) {
  int count = 0;
  for (std::size_t i = 0; i < text.size();) {
    const auto byte = static_cast<unsigned char>(text[i]);
    const std::size_t width = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
    count += width == 4 ? 2 : 1;
    i += width;
  }
  return count;
}

}  // namespace

std::vector<Layer> read_odg_vector_layer(std::span<const std::uint8_t> content, Document& target,
                                         std::vector<std::string>& notices) {
  const auto root = svg::parse_xml(content);
  Styles styles;
  collect_styles(root, styles);
  const svg::XmlNode* page = nullptr;
  const auto find_page = [&](auto&& self, const svg::XmlNode& node) -> void {
    for (const auto& child : node.children) {
      if (page != nullptr || child.is_text()) continue;
      if (local(child.name) == "page") {
        page = &child;
        return;
      }
      self(self, child);
    }
  };
  find_page(find_page, root);
  if (page == nullptr) {
    throw std::runtime_error("the vector data has no drawing");
  }
  Converter converter{styles, {}, {}, 0};
  for (const auto& child : page->children) {
    if (!child.is_text()) converter.shape(child);
  }
  const auto& print = target.print_settings();
  const double width_pt = target.width() * 72.0 / print.horizontal_ppi;
  const double height_pt = target.height() * 72.0 / print.vertical_ppi;
  std::vector<Layer> layers;
  if (!converter.svg.empty()) {
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + fmt(width_pt) + "pt\" height=\"" +
                            fmt(height_pt) + "pt\" viewBox=\"0 0 " + fmt(width_pt) + " " + fmt(height_pt) + "\">" +
                            converter.svg + "</svg>";
    layers = read_vector_layer(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size()), target, notices);
  }
  // Text boxes: Patchy box text in pixels, placed after the open by the frame-text pass.
  const double sx = print.horizontal_ppi / 72.0;
  const double sy = print.vertical_ppi / 72.0;
  for (const auto& box : converter.boxes) {
    // Named after its first line, cut to 40 bytes on a character boundary.
    auto name = box.text.substr(0, std::min<std::size_t>(box.text.find('\n'), 40));
    if (name.size() == 40) {
      while (!name.empty() && (static_cast<unsigned char>(box.text[name.size()]) & 0xC0U) == 0x80U) {
        name.pop_back();
      }
    }
    Layer layer(target.allocate_layer_id(), name, LayerKind::Pixel);
    auto& metadata = layer.metadata();
    const int size = std::clamp(static_cast<int>(std::lround(box.size_pt * sy)), 1, 4000);
    const double left = box.x * sx;
    const double top = box.y * sy;
    const double right = (box.x + box.width) * sx;
    const double bottom = (box.y + box.height) * sy;
    metadata[kLayerMetadataText] = box.text;
    metadata[kLayerMetadataTextFont] = box.family;
    metadata[kLayerMetadataTextSize] = std::to_string(size);
    constexpr char kHex[] = "0123456789abcdef";
    std::string hex = "#......";
    hex[1] = kHex[box.color.red >> 4];
    hex[2] = kHex[box.color.red & 15];
    hex[3] = kHex[box.color.green >> 4];
    hex[4] = kHex[box.color.green & 15];
    hex[5] = kHex[box.color.blue >> 4];
    hex[6] = kHex[box.color.blue & 15];
    metadata[kLayerMetadataTextColor] = hex;
    metadata[kLayerMetadataTextBold] = box.bold ? "true" : "false";
    metadata[kLayerMetadataTextItalic] = box.italic ? "true" : "false";
    metadata[kLayerMetadataTextFlow] = "box";
    metadata[kLayerMetadataTextBoxWidth] = std::to_string(std::max(1, static_cast<int>(std::lround(right - left))));
    metadata[kLayerMetadataTextBoxHeight] = std::to_string(std::max(1, static_cast<int>(std::lround(bottom - top))));
    if (box.align > 0) {
      const int length = utf16_length(box.text);
      std::vector<psd::PsdTextStyleRun> style_runs(1);
      style_runs[0].start = 0;
      style_runs[0].length = length;
      style_runs[0].family = box.family;
      style_runs[0].size = size;
      style_runs[0].color = box.color;
      style_runs[0].bold = box.bold;
      style_runs[0].italic = box.italic;
      std::vector<psd::PsdTextParagraphRun> paragraph_runs(1);
      paragraph_runs[0].start = 0;
      paragraph_runs[0].length = length;
      // Photoshop's justification codes: 1 right, 2 center.
      paragraph_runs[0].justification = box.align == 1 ? 2 : 1;
      metadata[kLayerMetadataTextRuns] = psd::serialize_patchy_text_runs(style_runs);
      metadata[kLayerMetadataTextParagraphRuns] = psd::serialize_patchy_paragraph_runs(paragraph_runs);
      metadata[kLayerMetadataTextHtml] = psd::html_from_text_runs(box.text, style_runs, paragraph_runs);
      metadata[kLayerMetadataAfTextAlign] = std::to_string(box.align);
    }
    metadata[kLayerMetadataAfPendingText] = "1";
    metadata[kLayerMetadataAfTextFrame] = fmt(left) + " " + fmt(top) + " " + fmt(right) + " " + fmt(bottom);
    layer.set_bounds(Rect{static_cast<std::int32_t>(std::lround(left)), static_cast<std::int32_t>(std::lround(top)), 1, 1});
    layers.push_back(std::move(layer));
  }
  if (converter.skipped > 0) {
    notices.push_back(std::to_string(converter.skipped) + " OpenDocument shapes Patchy cannot read were left out");
  }
  return layers;
}

}  // namespace patchy::kra::detail
