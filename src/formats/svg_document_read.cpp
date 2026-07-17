#include "formats/svg_document_io.hpp"

#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "formats/miniz/miniz.h"
#include "formats/svg_io_internal.hpp"
#include "formats/svg_xml.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// SVG import: builds a Document of editable shape layers (groups -> folders,
// rect/circle/ellipse/line -> live shapes, gradients -> gradient fills, simple
// patterns -> PatternStore tiles, clip paths -> vector masks) from the parsed
// XML. Unsupported features degrade with an import notice instead of failing;
// files past the supported subset throw, and the UI's QImageReader fallback
// (the qsvg plugin) then imports them as flattened raster.
//
// Ordering: SVG paints first-to-last = bottom-to-top, exactly Patchy's
// layers()[0]-is-the-bottom convention, so elements append in document order
// with no reversal anywhere (the compositor's composite_sibling_layers walks
// index 0 first).
namespace patchy::svg {
namespace {

using detail::Affine;

// Above this many drawable elements the editable import refuses (one layer
// per element would drown the layer panel); the thrown message reaches the
// user only if the raster fallback also fails.
constexpr std::size_t kMaximumDrawables = 2000;
constexpr int kMaximumUseDepth = 32;
constexpr std::size_t kMaximumInflatedBytes = 256U * 1024U * 1024U;
constexpr std::int32_t kMaximumCanvasSize = 30000;  // matches the PSD writer's .psd cap
constexpr double kEpsilon = 1e-9;

// UI-side post-open markers (decoded/rendered by MainWindow after the open:
// the formats library has no PNG decoder or font engine). The shared key
// strings live in core/layer_metadata.hpp.
constexpr const char* kPendingImageKey = kLayerMetadataSvgPendingImage;
constexpr const char* kPendingTextKey = kLayerMetadataSvgPendingText;
constexpr const char* kTextAnchorKey = kLayerMetadataSvgTextAnchor;
constexpr const char* kTextBaselineXKey = kLayerMetadataSvgTextBaselineX;
constexpr const char* kTextBaselineYKey = kLayerMetadataSvgTextBaselineY;

bool is_ascii_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string_view trimmed(std::string_view value) noexcept {
  while (!value.empty() && is_ascii_space(value.front())) {
    value.remove_prefix(1);
  }
  while (!value.empty() && is_ascii_space(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

std::string lower_ascii(std::string_view value) {
  std::string result(value);
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return result;
}

// Strict whole-string parse (sizes, offsets: trailing junk is a failure).
bool parse_double(std::string_view text, double& value) {
  text = trimmed(text);
  if (text.empty()) {
    return false;
  }
  std::size_t consumed = 0;
  return detail::parse_double_prefix(text, consumed, value) && consumed == text.size();
}

// Leading-number parse with a fallback: CSS-ish values keep their trailing
// units ("stroke-width: 2px" reads as 2).
double number_or(std::string_view text, double fallback) {
  const auto lead = trimmed(text);
  std::size_t consumed = 0;
  double value = fallback;
  return detail::parse_double_prefix(lead, consumed, value) ? value : fallback;
}

// Numbers separated by whitespace and/or commas (the SVG list grammar).
std::vector<double> number_list(std::string_view text) {
  std::vector<double> result;
  std::size_t position = 0;
  while (position < text.size()) {
    while (position < text.size() && (is_ascii_space(text[position]) || text[position] == ',')) {
      ++position;
    }
    if (position >= text.size()) {
      break;
    }
    double value = 0.0;
    std::size_t consumed = 0;
    if (!detail::parse_double_prefix(text.substr(position), consumed, value)) {
      break;
    }
    result.push_back(value);
    position += consumed;
  }
  return result;
}

// --- CSS-lite ---------------------------------------------------------------

// "name: value; ..." declarations. Property names lower-case; values keep
// their case (paint references and font names are case-sensitive).
std::map<std::string, std::string, std::less<>> parse_declarations(std::string_view text) {
  std::map<std::string, std::string, std::less<>> result;
  std::size_t start = 0;
  while (start < text.size()) {
    auto end = text.find(';', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const auto declaration = text.substr(start, end - start);
    if (const auto colon = declaration.find(':'); colon != std::string_view::npos) {
      auto name = lower_ascii(trimmed(declaration.substr(0, colon)));
      auto value = std::string(trimmed(declaration.substr(colon + 1)));
      if (!name.empty()) {
        result[std::move(name)] = std::move(value);
      }
    }
    start = end + 1;
  }
  return result;
}

// The subset real exporters emit: flat type/.class/#id selectors (Illustrator
// writes ".st0{fill:#FF0000;}" blocks). Combinators and pseudo-classes are
// skipped whole.
struct CssRule {
  enum class Kind { Type, Class, Id } kind{Kind::Type};
  std::string selector;
  std::map<std::string, std::string, std::less<>> values;
  int order{0};
};

void collect_css_rules(const XmlNode& node, std::vector<CssRule>& rules, int& order) {
  if (node.name == "style") {
    const auto css = node.all_text();
    std::size_t position = 0;
    while (position < css.size()) {
      const auto open = css.find('{', position);
      if (open == std::string::npos) {
        break;
      }
      const auto close = css.find('}', open + 1);
      if (close == std::string::npos) {
        break;
      }
      const auto body = parse_declarations(std::string_view(css).substr(open + 1, close - open - 1));
      const std::string_view selectors = std::string_view(css).substr(position, open - position);
      std::size_t selector_start = 0;
      while (selector_start <= selectors.size()) {
        auto comma = selectors.find(',', selector_start);
        if (comma == std::string_view::npos) {
          comma = selectors.size();
        }
        const auto selector = trimmed(selectors.substr(selector_start, comma - selector_start));
        if (!selector.empty() && selector.find_first_of(" \t\n>+~[:") == std::string_view::npos) {
          CssRule rule;
          rule.kind = selector.front() == '#'   ? CssRule::Kind::Id
                      : selector.front() == '.' ? CssRule::Kind::Class
                                                : CssRule::Kind::Type;
          rule.selector = std::string(rule.kind == CssRule::Kind::Type ? selector : selector.substr(1));
          rule.values = body;
          rule.order = order++;
          rules.push_back(std::move(rule));
        }
        selector_start = comma + 1;
      }
      position = close + 1;
    }
  }
  for (const auto& child : node.children) {
    if (!child.is_text()) {
      collect_css_rules(child, rules, order);
    }
  }
}

bool node_has_class(const XmlNode& node, std::string_view wanted) {
  const auto* classes = node.attribute("class");
  if (classes == nullptr) {
    return false;
  }
  std::string_view remaining = *classes;
  while (!remaining.empty()) {
    while (!remaining.empty() && is_ascii_space(remaining.front())) {
      remaining.remove_prefix(1);
    }
    auto end = std::size_t{0};
    while (end < remaining.size() && !is_ascii_space(remaining[end])) {
      ++end;
    }
    if (remaining.substr(0, end) == wanted) {
      return true;
    }
    remaining.remove_prefix(end);
  }
  return false;
}

bool rule_matches(const CssRule& rule, const XmlNode& node) {
  switch (rule.kind) {
    case CssRule::Kind::Type:
      return node.name == rule.selector;
    case CssRule::Kind::Class:
      return node_has_class(node, rule.selector);
    case CssRule::Kind::Id: {
      const auto* id = node.attribute("id");
      return id != nullptr && *id == rule.selector;
    }
  }
  return false;
}

// --- style model ------------------------------------------------------------

struct Style {
  // Paint values keep their original case: url(#SVGID_1_) references are
  // case-sensitive ids.
  std::string fill{"black"};
  std::string stroke{"none"};
  std::string color{"black"};
  double fill_opacity{1.0};
  double stroke_opacity{1.0};
  double opacity{1.0};  // not inherited
  double stroke_width{1.0};
  double stroke_miterlimit{4.0};
  double stroke_dashoffset{0.0};
  std::vector<double> stroke_dashes;  // user units ("none" = empty)
  VectorStrokeCap cap{VectorStrokeCap::Butt};
  VectorStrokeJoin join{VectorStrokeJoin::Miter};
  bool even_odd{false};       // fill-rule
  bool display_none{false};   // not inherited (a hidden group hides via the folder)
  bool visible{true};         // visibility, inherited
  std::string blend{"normal"};  // mix-blend-mode, not inherited
  std::string font_family{"Arial"};
  double font_size{16.0};
  bool bold{false};
  bool italic{false};
  std::string text_anchor{"start"};
};

void apply_style_value(Style& style, std::string_view raw_name, std::string_view raw_value) {
  const auto name = lower_ascii(raw_name);
  const auto value = std::string(trimmed(raw_value));
  const auto keyword = lower_ascii(value);
  if (keyword == "inherit") {
    return;  // keep the inherited value already in `style`
  }
  if (name == "fill") {
    style.fill = value;
  } else if (name == "stroke") {
    style.stroke = value;
  } else if (name == "color") {
    style.color = value;
  } else if (name == "fill-opacity") {
    style.fill_opacity = std::clamp(number_or(value, style.fill_opacity), 0.0, 1.0);
  } else if (name == "stroke-opacity") {
    style.stroke_opacity = std::clamp(number_or(value, style.stroke_opacity), 0.0, 1.0);
  } else if (name == "opacity") {
    style.opacity = std::clamp(number_or(value, style.opacity), 0.0, 1.0);
  } else if (name == "stroke-width") {
    style.stroke_width = std::max(0.0, number_or(value, style.stroke_width));
  } else if (name == "stroke-miterlimit") {
    style.stroke_miterlimit = std::max(1.0, number_or(value, style.stroke_miterlimit));
  } else if (name == "stroke-dashoffset") {
    style.stroke_dashoffset = number_or(value, style.stroke_dashoffset);
  } else if (name == "stroke-dasharray") {
    style.stroke_dashes = keyword == "none" ? std::vector<double>{} : number_list(value);
  } else if (name == "stroke-linecap") {
    style.cap = keyword == "round"    ? VectorStrokeCap::Round
                : keyword == "square" ? VectorStrokeCap::Square
                                      : VectorStrokeCap::Butt;
  } else if (name == "stroke-linejoin") {
    style.join = keyword == "round"   ? VectorStrokeJoin::Round
                 : keyword == "bevel" ? VectorStrokeJoin::Bevel
                                      : VectorStrokeJoin::Miter;
  } else if (name == "fill-rule") {
    style.even_odd = keyword == "evenodd";
  } else if (name == "display") {
    style.display_none = keyword == "none";
  } else if (name == "visibility") {
    style.visible = keyword != "hidden" && keyword != "collapse";
  } else if (name == "mix-blend-mode") {
    style.blend = keyword;
  } else if (name == "font-family") {
    // First family of the list, quotes stripped, case preserved.
    auto family = value.substr(0, value.find(','));
    auto family_view = trimmed(family);
    if (family_view.size() >= 2 && (family_view.front() == '"' || family_view.front() == '\'') &&
        family_view.back() == family_view.front()) {
      family_view = family_view.substr(1, family_view.size() - 2);
    }
    if (!family_view.empty()) {
      style.font_family = std::string(family_view);
    }
  } else if (name == "font-size") {
    style.font_size = std::max(1.0, number_or(value, style.font_size));
  } else if (name == "font-weight") {
    style.bold = keyword == "bold" || keyword == "bolder" || number_or(keyword, 400.0) >= 600.0;
  } else if (name == "font-style") {
    style.italic = keyword == "italic" || keyword == "oblique";
  } else if (name == "text-anchor") {
    style.text_anchor = keyword;
  }
}

// CSS cascade order: presentation attributes lose to stylesheet rules, which
// lose to the inline style attribute (specificity within the sheet:
// type < class < id, later rules win ties).
Style resolve_style(const XmlNode& node, const Style& inherited, const std::vector<CssRule>& rules) {
  Style style = inherited;
  style.opacity = 1.0;
  style.display_none = false;
  style.blend = "normal";
  for (const auto& [name, value] : node.attributes) {
    if (name != "style") {
      apply_style_value(style, name, value);
    }
  }
  std::vector<const CssRule*> applicable;
  for (const auto& rule : rules) {
    if (rule_matches(rule, node)) {
      applicable.push_back(&rule);
    }
  }
  std::stable_sort(applicable.begin(), applicable.end(), [](const CssRule* left, const CssRule* right) {
    const auto rank = [](CssRule::Kind kind) {
      return kind == CssRule::Kind::Id ? 2 : kind == CssRule::Kind::Class ? 1 : 0;
    };
    if (rank(left->kind) != rank(right->kind)) {
      return rank(left->kind) < rank(right->kind);
    }
    return left->order < right->order;
  });
  for (const auto* rule : applicable) {
    for (const auto& [name, value] : rule->values) {
      apply_style_value(style, name, value);
    }
  }
  if (const auto* inline_style = node.attribute("style")) {
    for (const auto& [name, value] : parse_declarations(*inline_style)) {
      apply_style_value(style, name, value);
    }
  }
  return style;
}

// --- colors -----------------------------------------------------------------

struct ColorEntry {
  const char* name;
  std::uint32_t rgb;
};

// The CSS/SVG named colors (both gray/grey spellings ride the table).
constexpr ColorEntry kNamedColors[] = {
    {"aliceblue", 0xF0F8FF},    {"antiquewhite", 0xFAEBD7},    {"aqua", 0x00FFFF},
    {"aquamarine", 0x7FFFD4},   {"azure", 0xF0FFFF},           {"beige", 0xF5F5DC},
    {"bisque", 0xFFE4C4},       {"black", 0x000000},           {"blanchedalmond", 0xFFEBCD},
    {"blue", 0x0000FF},         {"blueviolet", 0x8A2BE2},      {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887},    {"cadetblue", 0x5F9EA0},       {"chartreuse", 0x7FFF00},
    {"chocolate", 0xD2691E},    {"coral", 0xFF7F50},           {"cornflowerblue", 0x6495ED},
    {"cornsilk", 0xFFF8DC},     {"crimson", 0xDC143C},         {"cyan", 0x00FFFF},
    {"darkblue", 0x00008B},     {"darkcyan", 0x008B8B},        {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9},     {"darkgreen", 0x006400},       {"darkgrey", 0xA9A9A9},
    {"darkkhaki", 0xBDB76B},    {"darkmagenta", 0x8B008B},     {"darkolivegreen", 0x556B2F},
    {"darkorange", 0xFF8C00},   {"darkorchid", 0x9932CC},      {"darkred", 0x8B0000},
    {"darksalmon", 0xE9967A},   {"darkseagreen", 0x8FBC8F},    {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F},{"darkslategrey", 0x2F4F4F},   {"darkturquoise", 0x00CED1},
    {"darkviolet", 0x9400D3},   {"deeppink", 0xFF1493},        {"deepskyblue", 0x00BFFF},
    {"dimgray", 0x696969},      {"dimgrey", 0x696969},         {"dodgerblue", 0x1E90FF},
    {"firebrick", 0xB22222},    {"floralwhite", 0xFFFAF0},     {"forestgreen", 0x228B22},
    {"fuchsia", 0xFF00FF},      {"gainsboro", 0xDCDCDC},       {"ghostwhite", 0xF8F8FF},
    {"gold", 0xFFD700},         {"goldenrod", 0xDAA520},       {"gray", 0x808080},
    {"green", 0x008000},        {"greenyellow", 0xADFF2F},     {"grey", 0x808080},
    {"honeydew", 0xF0FFF0},     {"hotpink", 0xFF69B4},         {"indianred", 0xCD5C5C},
    {"indigo", 0x4B0082},       {"ivory", 0xFFFFF0},           {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA},     {"lavenderblush", 0xFFF0F5},   {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD}, {"lightblue", 0xADD8E6},       {"lightcoral", 0xF08080},
    {"lightcyan", 0xE0FFFF},    {"lightgoldenrodyellow", 0xFAFAD2}, {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90},   {"lightgrey", 0xD3D3D3},       {"lightpink", 0xFFB6C1},
    {"lightsalmon", 0xFFA07A},  {"lightseagreen", 0x20B2AA},   {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899},{"lightslategrey", 0x778899}, {"lightsteelblue", 0xB0C4DE},
    {"lightyellow", 0xFFFFE0},  {"lime", 0x00FF00},            {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6},        {"magenta", 0xFF00FF},         {"maroon", 0x800000},
    {"mediumaquamarine", 0x66CDAA}, {"mediumblue", 0x0000CD},  {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB}, {"mediumseagreen", 0x3CB371},  {"mediumslateblue", 0x7B68EE},
    {"mediumspringgreen", 0x00FA9A}, {"mediumturquoise", 0x48D1CC}, {"mediumvioletred", 0xC71585},
    {"midnightblue", 0x191970}, {"mintcream", 0xF5FFFA},       {"mistyrose", 0xFFE4E1},
    {"moccasin", 0xFFE4B5},     {"navajowhite", 0xFFDEAD},     {"navy", 0x000080},
    {"oldlace", 0xFDF5E6},      {"olive", 0x808000},           {"olivedrab", 0x6B8E23},
    {"orange", 0xFFA500},       {"orangered", 0xFF4500},       {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA},{"palegreen", 0x98FB98},       {"paleturquoise", 0xAFEEEE},
    {"palevioletred", 0xDB7093},{"papayawhip", 0xFFEFD5},      {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F},         {"pink", 0xFFC0CB},            {"plum", 0xDDA0DD},
    {"powderblue", 0xB0E0E6},   {"purple", 0x800080},          {"rebeccapurple", 0x663399},
    {"red", 0xFF0000},          {"rosybrown", 0xBC8F8F},       {"royalblue", 0x4169E1},
    {"saddlebrown", 0x8B4513},  {"salmon", 0xFA8072},          {"sandybrown", 0xF4A460},
    {"seagreen", 0x2E8B57},     {"seashell", 0xFFF5EE},        {"sienna", 0xA0522D},
    {"silver", 0xC0C0C0},       {"skyblue", 0x87CEEB},         {"slateblue", 0x6A5ACD},
    {"slategray", 0x708090},    {"slategrey", 0x708090},       {"snow", 0xFFFAFA},
    {"springgreen", 0x00FF7F},  {"steelblue", 0x4682B4},       {"tan", 0xD2B48C},
    {"teal", 0x008080},         {"thistle", 0xD8BFD8},         {"tomato", 0xFF6347},
    {"turquoise", 0x40E0D0},    {"violet", 0xEE82EE},          {"wheat", 0xF5DEB3},
    {"white", 0xFFFFFF},        {"whitesmoke", 0xF5F5F5},      {"yellow", 0xFFFF00},
    {"yellowgreen", 0x9ACD32},
};

// RGBA in 0..1; nullopt for "none" and unparseable values.
std::optional<std::array<double, 4>> parse_color(std::string_view raw) {
  const auto value = lower_ascii(trimmed(raw));
  if (value.empty() || value == "none") {
    return std::nullopt;
  }
  if (value == "transparent") {
    return std::array<double, 4>{0.0, 0.0, 0.0, 0.0};
  }
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  };
  if (value.front() == '#') {
    const std::string_view hex = std::string_view(value).substr(1);
    if (hex.size() == 3 || hex.size() == 4) {
      std::array<double, 4> out{0.0, 0.0, 0.0, 1.0};
      for (std::size_t i = 0; i < hex.size(); ++i) {
        const int n = nibble(hex[i]);
        if (n < 0) {
          return std::nullopt;
        }
        out[i] = static_cast<double>(n * 17) / 255.0;
      }
      return out;
    }
    if (hex.size() == 6 || hex.size() == 8) {
      std::array<double, 4> out{0.0, 0.0, 0.0, 1.0};
      for (std::size_t i = 0; i < hex.size() / 2; ++i) {
        const int high = nibble(hex[i * 2]);
        const int low = nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
          return std::nullopt;
        }
        out[i] = static_cast<double>(high * 16 + low) / 255.0;
      }
      return out;
    }
    return std::nullopt;
  }
  const auto open = value.find('(');
  const auto close = value.rfind(')');
  if (open != std::string::npos && close != std::string::npos && close > open) {
    const auto function = value.substr(0, open);
    auto body = value.substr(open + 1, close - open - 1);
    std::replace(body.begin(), body.end(), ',', ' ');
    std::replace(body.begin(), body.end(), '/', ' ');
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < body.size()) {
      while (start < body.size() && is_ascii_space(body[start])) {
        ++start;
      }
      auto end = start;
      while (end < body.size() && !is_ascii_space(body[end])) {
        ++end;
      }
      if (end > start) {
        parts.push_back(body.substr(start, end - start));
      }
      start = end;
    }
    const auto channel = [](std::string part, double full_scale) {
      const bool percent = part.ends_with('%');
      if (percent) {
        part.pop_back();
      }
      return std::clamp(number_or(part, 0.0) / (percent ? 100.0 : full_scale), 0.0, 1.0);
    };
    if ((function == "rgb" || function == "rgba") && parts.size() >= 3) {
      std::array<double, 4> out{channel(parts[0], 255.0), channel(parts[1], 255.0), channel(parts[2], 255.0), 1.0};
      if (parts.size() > 3) {
        out[3] = channel(parts[3], 1.0);
      }
      return out;
    }
    if ((function == "hsl" || function == "hsla") && parts.size() >= 3) {
      double hue = number_or(parts[0], 0.0);
      hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0) / 360.0;
      const double saturation = channel(parts[1], 100.0);
      const double lightness = channel(parts[2], 100.0);
      const auto component = [](double p, double q, double t) {
        if (t < 0.0) {
          t += 1.0;
        }
        if (t > 1.0) {
          t -= 1.0;
        }
        if (t < 1.0 / 6.0) {
          return p + (q - p) * 6.0 * t;
        }
        if (t < 0.5) {
          return q;
        }
        if (t < 2.0 / 3.0) {
          return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        }
        return p;
      };
      std::array<double, 4> out{lightness, lightness, lightness, 1.0};
      if (saturation > 0.0) {
        const double q = lightness < 0.5 ? lightness * (1.0 + saturation) : lightness + saturation - lightness * saturation;
        const double p = 2.0 * lightness - q;
        out[0] = component(p, q, hue + 1.0 / 3.0);
        out[1] = component(p, q, hue);
        out[2] = component(p, q, hue - 1.0 / 3.0);
      }
      if (parts.size() > 3) {
        out[3] = channel(parts[3], 1.0);
      }
      return out;
    }
    return std::nullopt;
  }
  for (const auto& entry : kNamedColors) {
    if (value == entry.name) {
      return std::array<double, 4>{static_cast<double>((entry.rgb >> 16) & 255U) / 255.0,
                                   static_cast<double>((entry.rgb >> 8) & 255U) / 255.0,
                                   static_cast<double>(entry.rgb & 255U) / 255.0, 1.0};
    }
  }
  return std::nullopt;
}

RgbColor to_rgb(const std::array<double, 4>& color) {
  const auto byte = [](double v) {
    return static_cast<std::uint8_t>(std::clamp<long>(std::lround(v * 255.0), 0, 255));
  };
  return {byte(color[0]), byte(color[1]), byte(color[2])};
}

// --- transforms -------------------------------------------------------------

// transform="..." lists. Lenient: an unparseable tail stops the walk instead
// of failing the import.
Affine parse_transform(std::string_view text) {
  Affine result;
  std::size_t position = 0;
  while (position < text.size()) {
    while (position < text.size() && (is_ascii_space(text[position]) || text[position] == ',')) {
      ++position;
    }
    const auto open = text.find('(', position);
    if (open == std::string_view::npos) {
      break;
    }
    const auto close = text.find(')', open + 1);
    if (close == std::string_view::npos) {
      break;
    }
    const auto name = lower_ascii(trimmed(text.substr(position, open - position)));
    const auto values = number_list(text.substr(open + 1, close - open - 1));
    Affine part;
    if (name == "matrix" && values.size() == 6) {
      part = {values[0], values[1], values[2], values[3], values[4], values[5]};
    } else if (name == "translate" && !values.empty()) {
      part.e = values[0];
      part.f = values.size() > 1 ? values[1] : 0.0;
    } else if (name == "scale" && !values.empty()) {
      part.a = values[0];
      part.d = values.size() > 1 ? values[1] : values[0];
    } else if (name == "rotate" && !values.empty()) {
      const double radians = values[0] * std::numbers::pi / 180.0;
      const double c = std::cos(radians);
      const double s = std::sin(radians);
      const Affine rotation{c, s, -s, c, 0.0, 0.0};
      if (values.size() >= 3) {
        part = detail::multiply(Affine{1, 0, 0, 1, values[1], values[2]},
                                detail::multiply(rotation, Affine{1, 0, 0, 1, -values[1], -values[2]}));
      } else {
        part = rotation;
      }
    } else if (name == "skewx" && !values.empty()) {
      part.c = std::tan(values[0] * std::numbers::pi / 180.0);
    } else if (name == "skewy" && !values.empty()) {
      part.b = std::tan(values[0] * std::numbers::pi / 180.0);
    }
    result = detail::multiply(result, part);
    position = close + 1;
  }
  return result;
}

// --- path data --------------------------------------------------------------

PathAnchor corner_anchor(double x, double y) {
  return PathAnchor{x, y, x, y, x, y, false};
}

void append_line(PathSubpath& subpath, double x, double y) {
  if (subpath.anchors.empty()) {
    return;
  }
  subpath.anchors.push_back(corner_anchor(x, y));
}

void append_cubic(PathSubpath& subpath, double x1, double y1, double x2, double y2, double x, double y) {
  if (subpath.anchors.empty()) {
    return;
  }
  subpath.anchors.back().out_x = x1;
  subpath.anchors.back().out_y = y1;
  auto next = corner_anchor(x, y);
  next.in_x = x2;
  next.in_y = y2;
  subpath.anchors.push_back(next);
}

// Endpoint-parametrized elliptical arc -> center form -> <= 90-degree cubic
// segments (the standard construction; the kappa-style alpha factor comes
// from the tangent quarter-angle formula).
void append_arc(PathSubpath& subpath, double rx, double ry, double rotation_degrees, bool large_arc, bool sweep,
                double x2, double y2) {
  if (subpath.anchors.empty()) {
    return;
  }
  const double x1 = subpath.anchors.back().anchor_x;
  const double y1 = subpath.anchors.back().anchor_y;
  rx = std::abs(rx);
  ry = std::abs(ry);
  if (rx < kEpsilon || ry < kEpsilon || (std::abs(x2 - x1) < kEpsilon && std::abs(y2 - y1) < kEpsilon)) {
    append_line(subpath, x2, y2);
    return;
  }
  const double phi = rotation_degrees * std::numbers::pi / 180.0;
  const double cos_phi = std::cos(phi);
  const double sin_phi = std::sin(phi);
  const double dx = (x1 - x2) / 2.0;
  const double dy = (y1 - y2) / 2.0;
  const double xp = cos_phi * dx + sin_phi * dy;
  const double yp = -sin_phi * dx + cos_phi * dy;
  const double lambda = xp * xp / (rx * rx) + yp * yp / (ry * ry);
  if (lambda > 1.0) {
    const double grow = std::sqrt(lambda);
    rx *= grow;
    ry *= grow;
  }
  const double numerator = std::max(0.0, rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp);
  const double denominator = rx * rx * yp * yp + ry * ry * xp * xp;
  const double coefficient =
      (large_arc == sweep ? -1.0 : 1.0) * std::sqrt(denominator > 0.0 ? numerator / denominator : 0.0);
  const double cxp = coefficient * (rx * yp / ry);
  const double cyp = coefficient * (-ry * xp / rx);
  const double cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2.0;
  const double cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2.0;
  const auto angle_between = [](double ux, double uy, double vx, double vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const double theta1 = angle_between(1.0, 0.0, (xp - cxp) / rx, (yp - cyp) / ry);
  double delta = angle_between((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry);
  if (!sweep && delta > 0.0) {
    delta -= 2.0 * std::numbers::pi;
  }
  if (sweep && delta < 0.0) {
    delta += 2.0 * std::numbers::pi;
  }
  const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi / 2.0))));
  const double step = delta / segments;
  const auto mapped = [&](double u, double v) {
    return std::array<double, 2>{cx + cos_phi * rx * u - sin_phi * ry * v, cy + sin_phi * rx * u + cos_phi * ry * v};
  };
  for (int i = 0; i < segments; ++i) {
    const double t1 = theta1 + i * step;
    const double t2 = t1 + step;
    const double alpha = 4.0 / 3.0 * std::tan((t2 - t1) / 4.0);
    const double c1 = std::cos(t1);
    const double s1 = std::sin(t1);
    const double c2 = std::cos(t2);
    const double s2 = std::sin(t2);
    const auto control1 = mapped(c1 - alpha * s1, s1 + alpha * c1);
    const auto control2 = mapped(c2 + alpha * s2, s2 - alpha * c2);
    const auto endpoint = mapped(c2, s2);
    append_cubic(subpath, control1[0], control1[1], control2[0], control2[1], endpoint[0], endpoint[1]);
  }
}

struct PathScanner {
  std::string_view text;
  std::size_t position{0};

  void skip_separators() noexcept {
    while (position < text.size() && (is_ascii_space(text[position]) || text[position] == ',')) {
      ++position;
    }
  }
  [[nodiscard]] bool has_number() noexcept {
    skip_separators();
    if (position >= text.size()) {
      return false;
    }
    const char c = text[position];
    return c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9');
  }
  bool number(double& value) {
    skip_separators();
    if (position >= text.size()) {
      return false;
    }
    std::size_t consumed = 0;
    if (!detail::parse_double_prefix(text.substr(position), consumed, value)) {
      return false;
    }
    position += consumed;
    return true;
  }
  // Arc flags may run together without separators ("a1 1 0 011 0" is legal).
  bool flag(bool& value) noexcept {
    skip_separators();
    if (position >= text.size()) {
      return false;
    }
    const char c = text[position];
    if (c != '0' && c != '1') {
      return false;
    }
    value = c == '1';
    ++position;
    return true;
  }
};

// Full d= grammar: M/L/H/V/C/S/Q/T/A/Z with relative forms and implicit
// command repeats. Quadratics elevate to cubics; arcs convert above. Each
// subpath gets its own shape_group in document order (the caller regroups per
// the fill rule).
VectorPath parse_path_data(std::string_view data) {
  PathScanner scan{data};
  VectorPath result;
  PathSubpath current;
  double x = 0.0;
  double y = 0.0;
  double start_x = 0.0;
  double start_y = 0.0;
  double last_cx = 0.0;
  double last_cy = 0.0;
  double last_qx = 0.0;
  double last_qy = 0.0;
  char command = 0;
  char previous = 0;
  std::int32_t group = 0;
  const auto finish_subpath = [&] {
    if (!current.anchors.empty()) {
      current.shape_group = group++;
      result.subpaths.push_back(std::move(current));
      current = PathSubpath{};
      current.closed = false;
    }
  };
  while (true) {
    scan.skip_separators();
    if (scan.position >= data.size()) {
      break;
    }
    const char c = data[scan.position];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      command = c;
      ++scan.position;
    } else if (command == 0) {
      throw std::runtime_error("SVG path data must begin with a command letter");
    }
    const bool relative = command >= 'a' && command <= 'z';
    const char upper = relative ? static_cast<char>(command - 'a' + 'A') : command;
    if (upper == 'Z') {
      if (!current.anchors.empty()) {
        current.closed = true;
        finish_subpath();
      }
      x = start_x;
      y = start_y;
      previous = 'Z';
      command = 0;
      continue;
    }
    if (!scan.has_number()) {
      throw std::runtime_error("SVG path command is missing its coordinates");
    }
    bool first_iteration = true;
    while (scan.has_number()) {
      double a = 0.0;
      double b = 0.0;
      double c2 = 0.0;
      double d = 0.0;
      double e = 0.0;
      double f = 0.0;
      switch (upper) {
        case 'M':
          if (!scan.number(a) || !scan.number(b)) {
            throw std::runtime_error("Invalid SVG move command");
          }
          if (relative) {
            a += x;
            b += y;
          }
          if (first_iteration) {
            finish_subpath();
            current.closed = false;
            current.anchors.push_back(corner_anchor(a, b));
            start_x = a;
            start_y = b;
          } else {
            append_line(current, a, b);  // implicit repeats of M are line-tos
          }
          x = a;
          y = b;
          break;
        case 'L':
          if (!scan.number(a) || !scan.number(b)) {
            throw std::runtime_error("Invalid SVG line command");
          }
          if (relative) {
            a += x;
            b += y;
          }
          append_line(current, a, b);
          x = a;
          y = b;
          break;
        case 'H':
          if (!scan.number(a)) {
            throw std::runtime_error("Invalid SVG horizontal-line command");
          }
          if (relative) {
            a += x;
          }
          append_line(current, a, y);
          x = a;
          break;
        case 'V':
          if (!scan.number(a)) {
            throw std::runtime_error("Invalid SVG vertical-line command");
          }
          if (relative) {
            a += y;
          }
          append_line(current, x, a);
          y = a;
          break;
        case 'C':
          if (!scan.number(a) || !scan.number(b) || !scan.number(c2) || !scan.number(d) || !scan.number(e) ||
              !scan.number(f)) {
            throw std::runtime_error("Invalid SVG cubic-curve command");
          }
          if (relative) {
            a += x;
            b += y;
            c2 += x;
            d += y;
            e += x;
            f += y;
          }
          append_cubic(current, a, b, c2, d, e, f);
          last_cx = c2;
          last_cy = d;
          x = e;
          y = f;
          break;
        case 'S':
          if (!scan.number(c2) || !scan.number(d) || !scan.number(e) || !scan.number(f)) {
            throw std::runtime_error("Invalid SVG smooth-cubic command");
          }
          if (relative) {
            c2 += x;
            d += y;
            e += x;
            f += y;
          }
          if (previous != 'C' && previous != 'S') {
            last_cx = x;
            last_cy = y;
          }
          a = 2.0 * x - last_cx;
          b = 2.0 * y - last_cy;
          append_cubic(current, a, b, c2, d, e, f);
          last_cx = c2;
          last_cy = d;
          x = e;
          y = f;
          break;
        case 'Q':
          if (!scan.number(a) || !scan.number(b) || !scan.number(c2) || !scan.number(d)) {
            throw std::runtime_error("Invalid SVG quadratic-curve command");
          }
          if (relative) {
            a += x;
            b += y;
            c2 += x;
            d += y;
          }
          append_cubic(current, x + 2.0 * (a - x) / 3.0, y + 2.0 * (b - y) / 3.0, c2 + 2.0 * (a - c2) / 3.0,
                       d + 2.0 * (b - d) / 3.0, c2, d);
          last_qx = a;
          last_qy = b;
          x = c2;
          y = d;
          break;
        case 'T':
          if (!scan.number(c2) || !scan.number(d)) {
            throw std::runtime_error("Invalid SVG smooth-quadratic command");
          }
          if (relative) {
            c2 += x;
            d += y;
          }
          if (previous != 'Q' && previous != 'T') {
            last_qx = x;
            last_qy = y;
          }
          a = 2.0 * x - last_qx;
          b = 2.0 * y - last_qy;
          append_cubic(current, x + 2.0 * (a - x) / 3.0, y + 2.0 * (b - y) / 3.0, c2 + 2.0 * (a - c2) / 3.0,
                       d + 2.0 * (b - d) / 3.0, c2, d);
          last_qx = a;
          last_qy = b;
          x = c2;
          y = d;
          break;
        case 'A': {
          double rotation = 0.0;
          bool large = false;
          bool sweep = false;
          if (!scan.number(a) || !scan.number(b) || !scan.number(rotation) || !scan.flag(large) ||
              !scan.flag(sweep) || !scan.number(c2) || !scan.number(d)) {
            throw std::runtime_error("Invalid SVG arc command");
          }
          if (relative) {
            c2 += x;
            d += y;
          }
          append_arc(current, a, b, rotation, large, sweep, c2, d);
          x = c2;
          y = d;
          break;
        }
        default:
          throw std::runtime_error(std::string("Unsupported SVG path command '") + upper + "'");
      }
      previous = upper;
      first_iteration = false;
      scan.skip_separators();
      if (scan.position < data.size()) {
        const char next = data[scan.position];
        if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')) {
          break;
        }
      }
    }
  }
  finish_subpath();
  return result;
}

VectorPath polygon_path(std::string_view points, bool closed) {
  const auto values = number_list(points);
  VectorPath path;
  if (values.size() < 4) {
    return path;
  }
  PathSubpath subpath;
  subpath.closed = closed;
  for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
    subpath.anchors.push_back(corner_anchor(values[i], values[i + 1]));
  }
  path.subpaths.push_back(std::move(subpath));
  return path;
}

// --- nonzero winding decomposition ------------------------------------------

// Polyline approximation for orientation/containment: anchors plus two cubic
// samples per curved segment (enough to classify holes; exact geometry stays
// in the beziers).
std::vector<std::array<double, 2>> subpath_polyline(const PathSubpath& subpath) {
  std::vector<std::array<double, 2>> points;
  const auto count = subpath.anchors.size();
  if (count == 0) {
    return points;
  }
  points.reserve(count * 3);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& from = subpath.anchors[i];
    points.push_back({from.anchor_x, from.anchor_y});
    const auto& to = subpath.anchors[(i + 1) % count];
    if (i + 1 == count && !subpath.closed) {
      break;
    }
    const bool straight = std::abs(from.out_x - from.anchor_x) < kEpsilon &&
                          std::abs(from.out_y - from.anchor_y) < kEpsilon &&
                          std::abs(to.in_x - to.anchor_x) < kEpsilon && std::abs(to.in_y - to.anchor_y) < kEpsilon;
    if (straight) {
      continue;
    }
    for (const double t : {1.0 / 3.0, 2.0 / 3.0}) {
      const double u = 1.0 - t;
      const double bx = u * u * u * from.anchor_x + 3.0 * u * u * t * from.out_x + 3.0 * u * t * t * to.in_x +
                        t * t * t * to.anchor_x;
      const double by = u * u * u * from.anchor_y + 3.0 * u * u * t * from.out_y + 3.0 * u * t * t * to.in_y +
                        t * t * t * to.anchor_y;
      points.push_back({bx, by});
    }
  }
  return points;
}

double polyline_signed_area(const std::vector<std::array<double, 2>>& points) {
  if (points.size() < 3) {
    return 0.0;
  }
  double area = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    area += a[0] * b[1] - b[0] * a[1];
  }
  return area / 2.0;
}

bool polyline_contains(const std::vector<std::array<double, 2>>& points, double x, double y) {
  bool inside = false;
  for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    const auto& a = points[i];
    const auto& b = points[j];
    if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]) {
      inside = !inside;
    }
  }
  return inside;
}

// SVG's default nonzero rule vs Patchy's model (even-odd within a shape
// group, combine ops between groups): give each subpath its own group; a
// subpath contained in an opposite-winding subpath becomes a Subtract group
// (hole), everything else stays Add (union). Document order keeps holes after
// the outline they punch for every real-world export. A single subpath keeps
// even-odd semantics (self-intersecting nonzero geometry is the one
// approximated case).
void decompose_nonzero(VectorPath& path) {
  if (path.subpaths.size() < 2) {
    if (!path.subpaths.empty()) {
      path.subpaths.front().shape_group = 0;
      path.subpaths.front().op = PathCombineOp::Add;
    }
    return;
  }
  std::vector<std::vector<std::array<double, 2>>> polylines;
  std::vector<double> areas;
  polylines.reserve(path.subpaths.size());
  for (const auto& subpath : path.subpaths) {
    polylines.push_back(subpath_polyline(subpath));
    areas.push_back(polyline_signed_area(polylines.back()));
  }
  for (std::size_t i = 0; i < path.subpaths.size(); ++i) {
    path.subpaths[i].shape_group = static_cast<std::int32_t>(i);
    path.subpaths[i].op = PathCombineOp::Add;
    if (polylines[i].empty()) {
      continue;
    }
    // Innermost strictly-larger container decides hole-ness.
    int parent = -1;
    double parent_area = std::numeric_limits<double>::infinity();
    const auto& probe = polylines[i].front();
    for (std::size_t j = 0; j < path.subpaths.size(); ++j) {
      if (i == j || std::abs(areas[j]) <= std::abs(areas[i])) {
        continue;
      }
      if (polyline_contains(polylines[j], probe[0], probe[1]) && std::abs(areas[j]) < parent_area) {
        parent = static_cast<int>(j);
        parent_area = std::abs(areas[j]);
      }
    }
    if (parent >= 0 && (areas[i] < 0.0) != (areas[static_cast<std::size_t>(parent)] < 0.0)) {
      path.subpaths[i].op = PathCombineOp::Subtract;
    }
  }
}

void transform_path(VectorPath& path, const Affine& matrix) {
  transform_vector_path(path, {matrix.a, matrix.b, matrix.c, matrix.d, matrix.e, matrix.f});
}

// --- the importer -----------------------------------------------------------

// A resolved paint: the fill plus the paint's own alpha (solid-color alpha
// folds into the element's opacity; gradients and patterns carry alpha in
// their stops/tiles).
struct PaintResolution {
  VectorFill fill;
  double alpha{1.0};
};

struct Importer {
  const XmlNode& root;
  std::vector<std::string>* notices{};
  std::vector<CssRule> css;
  std::unordered_map<std::string, const XmlNode*> ids;
  Document document{1, 1, PixelFormat::rgba8()};
  Rect canvas{};
  std::size_t drawables{0};
  std::map<std::string, int, std::less<>> name_counters;
  std::set<std::string> use_stack;

  void notice(std::string text) {
    if (notices != nullptr && std::find(notices->begin(), notices->end(), text) == notices->end()) {
      notices->push_back(std::move(text));
    }
  }

  void index_ids(const XmlNode& node) {
    if (const auto* id = node.attribute("id"); id != nullptr && !id->empty()) {
      ids.emplace(*id, &node);  // first declaration wins, like browsers
    }
    for (const auto& child : node.children) {
      if (!child.is_text()) {
        index_ids(child);
      }
    }
  }

  const XmlNode* find_reference(std::string_view value) const {
    // "url(#id)" and plain "#id" forms.
    auto hash = value.find('#');
    if (hash == std::string_view::npos) {
      return nullptr;
    }
    auto id = value.substr(hash + 1);
    if (const auto close = id.find(')'); close != std::string_view::npos) {
      id = id.substr(0, close);
    }
    const auto found = ids.find(std::string(trimmed(id)));
    return found == ids.end() ? nullptr : found->second;
  }

  // Photoshop-style naming: the element id (then <title>) when present, else
  // per-kind counters ("Rectangle 1", "Ellipse 1", ...).
  std::string name_for(const XmlNode& node, const std::string& type) {
    if (const auto* id = node.attribute("id"); id != nullptr && !trimmed(*id).empty()) {
      return std::string(trimmed(*id));
    }
    for (const auto& child : node.children) {
      if (child.name == "title") {
        if (const auto title = trimmed(child.all_text()); !title.empty()) {
          return std::string(title);
        }
      }
    }
    return type + " " + std::to_string(++name_counters[type]);
  }

  void count_drawable() {
    if (++drawables > kMaximumDrawables) {
      throw std::runtime_error("SVG has more than " + std::to_string(kMaximumDrawables) +
                               " drawable elements; imported as flattened raster instead");
    }
  }

  // --- gradients ---

  VectorFill gradient_fill(const XmlNode& original, const VectorPath& path) {
    // href template inheritance: attributes and stops come from the nearest
    // node in the chain that defines them (the Illustrator/Inkscape pattern).
    std::vector<const XmlNode*> chain;
    std::set<std::string> seen;
    const XmlNode* node = &original;
    while (node != nullptr) {
      chain.push_back(node);
      const auto* href = node->attribute("href");
      if (href == nullptr || href->empty() || href->front() != '#' || !seen.insert(*href).second) {
        break;
      }
      const auto found = ids.find(href->substr(1));
      node = found == ids.end() ? nullptr : found->second;
    }
    const auto attribute = [&](std::string_view name, std::string fallback) {
      for (const auto* item : chain) {
        if (const auto* value = item->attribute(name)) {
          return *value;
        }
      }
      return fallback;
    };
    std::vector<const XmlNode*> stops;
    for (const auto* item : chain) {
      for (const auto& child : item->children) {
        if (child.name == "stop") {
          stops.push_back(&child);
        }
      }
      if (!stops.empty()) {
        break;
      }
    }

    VectorFill fill;
    fill.kind = VectorFillKind::Gradient;
    auto& gradient = fill.gradient;
    gradient.name = attribute("id", "SVG Gradient");
    gradient.form = GradientDefinitionForm::Solid;
    // Linear interpolation between stops: smoothness 0 turns the Classic
    // catmull-rom ease off in the fill renderer.
    gradient.smoothness = 0;
    gradient.interpolation = GradientInterpolationMethod::Linear;
    gradient.type = original.name == "radialGradient" ? LayerStyleGradientType::Radial : LayerStyleGradientType::Linear;

    for (const auto* stop : stops) {
      auto inline_values = std::map<std::string, std::string, std::less<>>{};
      if (const auto* style_attribute = stop->attribute("style")) {
        inline_values = parse_declarations(*style_attribute);
      }
      std::string color_text = "black";
      if (const auto* value = stop->attribute("stop-color")) {
        color_text = *value;
      }
      if (const auto found = inline_values.find("stop-color"); found != inline_values.end()) {
        color_text = found->second;
      }
      double stop_opacity = 1.0;
      if (const auto* value = stop->attribute("stop-opacity")) {
        stop_opacity = number_or(*value, 1.0);
      }
      if (const auto found = inline_values.find("stop-opacity"); found != inline_values.end()) {
        stop_opacity = number_or(found->second, stop_opacity);
      }
      std::string offset_text = "0";
      if (const auto* value = stop->attribute("offset")) {
        offset_text = *value;
      }
      const bool percent = offset_text.ends_with('%');
      if (percent) {
        offset_text.pop_back();
      }
      const double offset = std::clamp(number_or(offset_text, 0.0) / (percent ? 100.0 : 1.0), 0.0, 1.0);
      const auto color = parse_color(color_text).value_or(std::array<double, 4>{0.0, 0.0, 0.0, 1.0});
      gradient.color_stops.push_back({static_cast<float>(offset), to_rgb(color), 0.5F});
      gradient.alpha_stops.push_back(
          {static_cast<float>(offset), static_cast<float>(std::clamp(stop_opacity * color[3], 0.0, 1.0)), 0.5F});
    }
    if (gradient.color_stops.empty()) {
      gradient.color_stops = {GradientColorStop{0.0F, {0, 0, 0}, 0.5F}, GradientColorStop{1.0F, {255, 255, 255}, 0.5F}};
      gradient.alpha_stops = {GradientAlphaStop{0.0F, 1.0F, 0.5F}, GradientAlphaStop{1.0F, 1.0F, 0.5F}};
    }
    const auto by_location = [](const auto& a, const auto& b) { return a.location < b.location; };
    std::stable_sort(gradient.color_stops.begin(), gradient.color_stops.end(), by_location);
    std::stable_sort(gradient.alpha_stops.begin(), gradient.alpha_stops.end(), by_location);

    // Geometry: map the SVG gradient vector onto Patchy's calibrated model
    // (span = center chord of the aligned bounds; docs/vector-tools.md "GdFl
    // gradient fill geometry"). objectBoundingBox coordinates resolve against
    // the path bounds, userSpaceOnUse against the canvas.
    const auto bounds = path.bounds();
    const bool user_space = lower_ascii(attribute("gradientUnits", "objectBoundingBox")) == "userspaceonuse";
    gradient.align_with_layer = !user_space;
    const double bounds_left = bounds.has_value() ? bounds->left : canvas.x;
    const double bounds_top = bounds.has_value() ? bounds->top : canvas.y;
    const double bounds_width = bounds.has_value() ? std::max(1.0, bounds->right - bounds->left) : canvas.width;
    const double bounds_height = bounds.has_value() ? std::max(1.0, bounds->bottom - bounds->top) : canvas.height;
    const double ref_x = user_space ? canvas.x : bounds_left;
    const double ref_y = user_space ? canvas.y : bounds_top;
    const double ref_w = std::max(1.0, user_space ? static_cast<double>(canvas.width) : bounds_width);
    const double ref_h = std::max(1.0, user_space ? static_cast<double>(canvas.height) : bounds_height);
    const auto gradient_transform = parse_transform(attribute("gradientTransform", ""));
    const auto coordinate = [&](const std::string& text, double origin, double size, double fallback) {
      if (text.empty()) {
        return fallback;
      }
      std::string t = text;
      const bool percent = t.ends_with('%');
      if (percent) {
        t.pop_back();
      }
      const double v = number_or(t, fallback);
      if (percent) {
        return origin + v * size / 100.0;
      }
      return user_space ? v : origin + v * size;
    };
    if (gradient.type == LayerStyleGradientType::Linear) {
      const double x1 = coordinate(attribute("x1", ""), bounds_left, bounds_width, bounds_left);
      const double y1 = coordinate(attribute("y1", ""), bounds_top, bounds_height, bounds_top);
      const double x2 = coordinate(attribute("x2", ""), bounds_left, bounds_width, bounds_left + bounds_width);
      const double y2 = coordinate(attribute("y2", ""), bounds_top, bounds_height, bounds_top);
      const auto p1 = detail::map_point(gradient_transform, x1, y1);
      const auto p2 = detail::map_point(gradient_transform, x2, y2);
      const double dx = p2[0] - p1[0];
      const double dy = p2[1] - p1[1];
      // Screen y grows downward; Photoshop angles are counter-clockwise.
      gradient.angle_degrees = static_cast<float>(std::atan2(-dy, dx) * 180.0 / std::numbers::pi);
      const double center_x = (p1[0] + p2[0]) / 2.0;
      const double center_y = (p1[1] + p2[1]) / 2.0;
      gradient.offset_x_percent = static_cast<float>((center_x - (ref_x + ref_w / 2.0)) / ref_w * 100.0);
      gradient.offset_y_percent = static_cast<float>((center_y - (ref_y + ref_h / 2.0)) / ref_h * 100.0);
      const double radians = gradient.angle_degrees * std::numbers::pi / 180.0;
      const double abs_cos = std::abs(std::cos(radians));
      const double abs_sin = std::abs(std::sin(radians));
      const double span = std::min(abs_cos > kEpsilon ? ref_w / abs_cos : std::numeric_limits<double>::infinity(),
                                   abs_sin > kEpsilon ? ref_h / abs_sin : std::numeric_limits<double>::infinity());
      gradient.scale = static_cast<float>(std::clamp(std::hypot(dx, dy) / std::max(1.0, span), 0.01, 10.0));
    } else {
      const double cx = coordinate(attribute("cx", ""), bounds_left, bounds_width, bounds_left + bounds_width / 2.0);
      const double cy = coordinate(attribute("cy", ""), bounds_top, bounds_height, bounds_top + bounds_height / 2.0);
      const double radius_fallback = std::max(bounds_width, bounds_height) / 2.0;
      const double r = [&] {
        const auto text = attribute("r", "");
        if (text.empty()) {
          return radius_fallback;
        }
        std::string t = text;
        const bool percent = t.ends_with('%');
        if (percent) {
          t.pop_back();
        }
        const double v = number_or(t, radius_fallback);
        return percent ? v * std::max(bounds_width, bounds_height) / 100.0
                       : (user_space ? v : v * std::max(bounds_width, bounds_height));
      }();
      const auto center = detail::map_point(gradient_transform, cx, cy);
      gradient.offset_x_percent = static_cast<float>((center[0] - (ref_x + ref_w / 2.0)) / ref_w * 100.0);
      gradient.offset_y_percent = static_cast<float>((center[1] - (ref_y + ref_h / 2.0)) / ref_h * 100.0);
      gradient.scale = static_cast<float>(std::clamp(r / std::max(1.0, std::max(ref_w, ref_h) / 2.0), 0.01, 10.0));
      if (attribute("fx", "") != "" || attribute("fy", "") != "") {
        notice("SVG radial-gradient focal points are not supported; the center was used");
      }
    }
    const auto spread = lower_ascii(attribute("spreadMethod", "pad"));
    if (spread == "reflect" && gradient.type == LayerStyleGradientType::Linear) {
      // Patchy's Reflected style mirrors across the center at twice the ramp,
      // so the SVG vector maps to half of the Reflected span.
      gradient.type = LayerStyleGradientType::Reflected;
      gradient.scale = std::min(10.0F, gradient.scale * 2.0F);
    } else if (spread != "pad" && !spread.empty()) {
      notice("SVG gradient spreadMethod '" + spread + "' was approximated");
    }
    return fill;
  }

  // --- patterns ---

  // Simple <pattern> support: shape-only content rasterizes once into a
  // document PatternStore tile (SVG patterns anchor to the user-space origin,
  // which is exactly the PatternTileSampler's document-origin mode with
  // pattern_linked = false). Anything richer degrades to gray + notice.
  std::optional<VectorFill> pattern_fill(const XmlNode& original, const VectorPath& path) {
    std::vector<const XmlNode*> chain;
    std::set<std::string> seen;
    const XmlNode* node = &original;
    while (node != nullptr) {
      chain.push_back(node);
      const auto* href = node->attribute("href");
      if (href == nullptr || href->empty() || href->front() != '#' || !seen.insert(*href).second) {
        break;
      }
      const auto found = ids.find(href->substr(1));
      node = found == ids.end() ? nullptr : found->second;
    }
    const auto attribute = [&](std::string_view name, std::string fallback) {
      for (const auto* item : chain) {
        if (const auto* value = item->attribute(name)) {
          return *value;
        }
      }
      return fallback;
    };
    const XmlNode* content = nullptr;
    for (const auto* item : chain) {
      const bool has_elements = std::any_of(item->children.begin(), item->children.end(),
                                            [](const XmlNode& child) { return !child.is_text(); });
      if (has_elements) {
        content = item;
        break;
      }
    }
    if (content == nullptr) {
      return std::nullopt;
    }

    const auto bounds = path.bounds();
    const double bounds_width = bounds.has_value() ? std::max(1.0, bounds->right - bounds->left) : canvas.width;
    const double bounds_height = bounds.has_value() ? std::max(1.0, bounds->bottom - bounds->top) : canvas.height;
    const bool object_units = lower_ascii(attribute("patternUnits", "objectBoundingBox")) != "userspaceonuse";
    const auto dimension = [&](std::string_view name, double relative_to) {
      std::string text = attribute(name, "0");
      const bool percent = text.ends_with('%');
      if (percent) {
        text.pop_back();
      }
      const double v = number_or(text, 0.0);
      if (percent) {
        return v * relative_to / 100.0;
      }
      return object_units ? v * relative_to : v;
    };
    const double tile_width = dimension("width", bounds_width);
    const double tile_height = dimension("height", bounds_height);
    if (tile_width < 1.0 || tile_height < 1.0 || tile_width > 4096.0 || tile_height > 4096.0) {
      return std::nullopt;
    }

    // Rasterize the tile: shape children only, painted with their own solid
    // or gradient fills through the shared shape rasterizer.
    const auto tile_w = static_cast<std::int32_t>(std::lround(tile_width));
    const auto tile_h = static_cast<std::int32_t>(std::lround(tile_height));
    PixelBuffer tile(tile_w, tile_h, PixelFormat::rgba8());
    tile.clear(0);
    const Rect tile_rect{0, 0, tile_w, tile_h};
    Affine content_transform;
    const auto view = number_list(attribute("viewBox", ""));
    if (view.size() == 4 && view[2] > 0.0 && view[3] > 0.0) {
      content_transform = {tile_width / view[2], 0.0, 0.0, tile_height / view[3], -view[0] * tile_width / view[2],
                           -view[1] * tile_height / view[3]};
    }
    for (const auto& child : content->children) {
      if (child.is_text() || child.name == "title" || child.name == "desc") {
        continue;
      }
      auto parsed = element_geometry(child);
      if (!parsed.has_value()) {
        notice("SVG pattern content beyond plain shapes was skipped");
        return std::nullopt;
      }
      auto style = resolve_style(child, Style{}, css);
      auto child_transform = content_transform;
      if (const auto* transform_text = child.attribute("transform")) {
        child_transform = detail::multiply(content_transform, parse_transform(*transform_text));
      }
      transform_path(parsed->path, child_transform);
      const auto paint = resolve_paint(style.fill, style, parsed->path);
      if (paint.fill.kind == VectorFillKind::Pattern) {
        notice("Nested SVG patterns are not supported");
        return std::nullopt;
      }
      VectorShapeContent content_shape;
      content_shape.path = std::move(parsed->path);
      for (auto& subpath : content_shape.path.subpaths) {
        subpath.shape_group = 0;
        subpath.op = PathCombineOp::Add;
      }
      content_shape.fill = paint.fill;
      const auto rendered = rasterize_vector_shape(content_shape, tile_rect, nullptr, nullptr);
      if (rendered.pixels.empty()) {
        continue;
      }
      const double coverage_scale = std::clamp(style.fill_opacity * paint.alpha, 0.0, 1.0);
      for (std::int32_t y = 0; y < rendered.bounds.height; ++y) {
        for (std::int32_t x = 0; x < rendered.bounds.width; ++x) {
          const auto* source = rendered.pixels.pixel(x, y);
          const double source_alpha = source[3] / 255.0 * coverage_scale;
          if (source_alpha <= 0.0) {
            continue;
          }
          auto* destination = tile.pixel(rendered.bounds.x + x, rendered.bounds.y + y);
          const double destination_alpha = destination[3] / 255.0;
          const double out_alpha = source_alpha + destination_alpha * (1.0 - source_alpha);
          for (int channel = 0; channel < 3; ++channel) {
            const double blended =
                (source[channel] * source_alpha + destination[channel] * destination_alpha * (1.0 - source_alpha)) /
                std::max(out_alpha, 1e-6);
            destination[channel] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(blended), 0, 255));
          }
          destination[3] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(out_alpha * 255.0), 0, 255));
        }
      }
    }

    PatternResource resource;
    resource.id = generate_pattern_uuid();
    resource.name = attribute("id", "SVG Pattern");
    resource.tile = std::move(tile);
    resource.provenance = PatternProvenance::Authored;
    document.metadata().patterns.adopt(resource);

    VectorFill fill;
    fill.kind = VectorFillKind::Pattern;
    fill.pattern_id = resource.id;
    fill.pattern_name = resource.name;
    fill.pattern_scale = 1.0;
    fill.pattern_linked = false;  // SVG tiles anchor to the document origin
    const double phase_x = object_units ? (bounds.has_value() ? bounds->left : 0.0) + dimension("x", bounds_width)
                                        : dimension("x", bounds_width);
    const double phase_y = object_units ? (bounds.has_value() ? bounds->top : 0.0) + dimension("y", bounds_height)
                                        : dimension("y", bounds_height);
    fill.pattern_phase_x = phase_x;
    fill.pattern_phase_y = phase_y;
    if (!attribute("patternTransform", "").empty()) {
      const auto transform = parse_transform(attribute("patternTransform", ""));
      // Similarity transforms map onto the pattern placement model; anything
      // else is approximated by its rotation + uniform scale.
      fill.pattern_angle_degrees = std::atan2(transform.b, transform.a) * 180.0 / std::numbers::pi;
      fill.pattern_scale = std::clamp(std::sqrt(std::abs(detail::determinant(transform))), 0.01, 100.0);
      fill.pattern_phase_x += transform.e;
      fill.pattern_phase_y += transform.f;
      if (std::abs(std::hypot(transform.a, transform.b) - std::hypot(transform.c, transform.d)) > 0.01) {
        notice("SVG patternTransform skew was approximated");
      }
    }
    return fill;
  }

  // --- paint dispatch ---

  PaintResolution resolve_paint(const std::string& raw_paint, const Style& style, const VectorPath& path) {
    const auto keyword = lower_ascii(trimmed(raw_paint));
    if (keyword.empty() || keyword == "none") {
      return {VectorFill{.kind = VectorFillKind::None}, 1.0};
    }
    std::string paint = std::string(trimmed(raw_paint));
    if (keyword == "currentcolor") {
      paint = style.color;
    }
    if (paint.starts_with("url(")) {
      const auto* referenced = find_reference(paint);
      if (referenced == nullptr) {
        // The paint grammar allows a fallback after the reference.
        const auto close = paint.find(')');
        if (close != std::string::npos) {
          const auto fallback = trimmed(std::string_view(paint).substr(close + 1));
          if (!fallback.empty()) {
            return resolve_paint(std::string(fallback), style, path);
          }
        }
        notice("An SVG paint reference could not be resolved and was replaced with gray");
        return {VectorFill{.kind = VectorFillKind::Solid, .color = {128, 128, 128}}, 1.0};
      }
      if (referenced->name == "linearGradient" || referenced->name == "radialGradient") {
        return {gradient_fill(*referenced, path), 1.0};
      }
      if (referenced->name == "pattern") {
        if (auto pattern = pattern_fill(*referenced, path); pattern.has_value()) {
          return {std::move(*pattern), 1.0};
        }
        notice("An SVG pattern paint was approximated with gray");
        return {VectorFill{.kind = VectorFillKind::Solid, .color = {128, 128, 128}}, 1.0};
      }
      notice("An unsupported SVG paint definition was replaced with gray");
      return {VectorFill{.kind = VectorFillKind::Solid, .color = {128, 128, 128}}, 1.0};
    }
    const auto parsed = parse_color(paint);
    if (!parsed.has_value()) {
      notice("Unrecognized SVG color '" + paint + "' was replaced with black");
      return {VectorFill{.kind = VectorFillKind::Solid, .color = {0, 0, 0}}, 1.0};
    }
    return {VectorFill{.kind = VectorFillKind::Solid, .color = to_rgb(*parsed)}, (*parsed)[3]};
  }

  // --- clip paths and masks ---

  std::optional<LayerVectorMask> vector_mask_from_clip(const XmlNode& node, const Affine& transform) {
    const auto* clip = node.attribute("clip-path");
    if (clip == nullptr || lower_ascii(trimmed(*clip)) == "none") {
      return std::nullopt;
    }
    if (node.attribute("data-patchy-stroke-align") != nullptr) {
      // Patchy's own inside-stroke export trick: the clip IS the shape's
      // outline, not a real mask. The alignment hint restores the geometry.
      return std::nullopt;
    }
    const auto* referenced = find_reference(*clip);
    if (referenced == nullptr || referenced->name != "clipPath") {
      notice("An unsupported SVG clip-path was skipped");
      return std::nullopt;
    }
    if (lower_ascii(referenced->attribute("clipPathUnits") != nullptr ? *referenced->attribute("clipPathUnits") : "") ==
        "objectboundingbox") {
      notice("SVG clip paths in objectBoundingBox units are not supported and were skipped");
      return std::nullopt;
    }
    LayerVectorMask mask;
    std::int32_t group = 0;
    for (const auto& child : referenced->children) {
      if (child.is_text() || child.name == "title" || child.name == "desc") {
        continue;
      }
      auto parsed = element_geometry(child);
      if (!parsed.has_value()) {
        notice("SVG clip-path content beyond plain shapes was skipped");
        return std::nullopt;
      }
      auto child_transform = Affine{};
      if (const auto* transform_text = child.attribute("transform")) {
        child_transform = parse_transform(*transform_text);
      }
      transform_path(parsed->path, child_transform);
      for (auto& subpath : parsed->path.subpaths) {
        subpath.shape_group = group;
        subpath.op = PathCombineOp::Add;
        mask.path.subpaths.push_back(std::move(subpath));
      }
      ++group;
    }
    if (mask.path.subpaths.empty()) {
      return std::nullopt;
    }
    transform_path(mask.path, transform);
    return mask;
  }

  std::optional<LayerMask> raster_mask_from_mask(const XmlNode& node, const Affine& transform) {
    const auto* mask_reference = node.attribute("mask");
    if (mask_reference == nullptr || lower_ascii(trimmed(*mask_reference)) == "none") {
      return std::nullopt;
    }
    const auto* referenced = find_reference(*mask_reference);
    if (referenced == nullptr || referenced->name != "mask") {
      notice("An unsupported SVG mask was skipped");
      return std::nullopt;
    }
    // Luminance mask from shape-only content: rasterize each child's
    // coverage weighted by its fill luminance and alpha, painted over.
    PixelBuffer gray(canvas.width, canvas.height, PixelFormat::gray8());
    gray.clear(0);
    bool any = false;
    for (const auto& child : referenced->children) {
      if (child.is_text() || child.name == "title" || child.name == "desc") {
        continue;
      }
      auto parsed = element_geometry(child);
      if (!parsed.has_value()) {
        notice("SVG mask content beyond plain shapes was skipped");
        return std::nullopt;
      }
      auto style = resolve_style(child, Style{}, css);
      const auto color = parse_color(style.fill).value_or(std::array<double, 4>{1.0, 1.0, 1.0, 1.0});
      const double luminance =
          std::clamp((0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2]) * color[3] * style.fill_opacity, 0.0,
                     1.0);
      auto child_transform = transform;
      if (const auto* transform_text = child.attribute("transform")) {
        child_transform = detail::multiply(transform, parse_transform(*transform_text));
      }
      transform_path(parsed->path, child_transform);
      for (auto& subpath : parsed->path.subpaths) {
        subpath.shape_group = 0;
        subpath.op = PathCombineOp::Add;
      }
      const auto coverage = rasterize_vector_path(parsed->path, VectorRasterOptions{canvas});
      if (coverage.pixels.empty()) {
        continue;
      }
      any = true;
      for (std::int32_t y = 0; y < coverage.bounds.height; ++y) {
        for (std::int32_t x = 0; x < coverage.bounds.width; ++x) {
          const double alpha = coverage.pixels.pixel(x, y)[0] / 255.0;
          if (alpha <= 0.0) {
            continue;
          }
          auto* destination = gray.pixel(coverage.bounds.x - canvas.x + x, coverage.bounds.y - canvas.y + y);
          const double blended = luminance * 255.0 * alpha + destination[0] * (1.0 - alpha);
          destination[0] = static_cast<std::uint8_t>(std::clamp<long>(std::lround(blended), 0, 255));
        }
      }
    }
    if (!any) {
      return std::nullopt;
    }
    LayerMask mask;
    mask.bounds = canvas;
    mask.pixels = std::move(gray);
    mask.default_color = 0;  // outside the mask nothing shows, the SVG rule
    return mask;
  }

  // --- element geometry ---

  struct ElementGeometry {
    VectorPath path;
    std::optional<LiveShapeParams> live;
  };

  std::optional<ElementGeometry> element_geometry(const XmlNode& node) {
    const auto attr = [&](std::string_view name, double fallback = 0.0) {
      const auto* value = node.attribute(name);
      return value != nullptr ? number_or(*value, fallback) : fallback;
    };
    ElementGeometry geometry;
    if (node.name == "path") {
      const auto* d = node.attribute("d");
      if (d == nullptr || trimmed(*d).empty()) {
        return std::nullopt;
      }
      geometry.path = parse_path_data(*d);
    } else if (node.name == "polygon" || node.name == "polyline") {
      const auto* points = node.attribute("points");
      if (points == nullptr) {
        return std::nullopt;
      }
      geometry.path = polygon_path(*points, node.name == "polygon");
    } else if (node.name == "rect") {
      const double x = attr("x");
      const double y = attr("y");
      const double width = attr("width");
      const double height = attr("height");
      if (width <= 0.0 || height <= 0.0) {
        return std::nullopt;
      }
      double rx = std::max(0.0, attr("rx", node.attribute("ry") != nullptr ? attr("ry") : 0.0));
      double ry = std::max(0.0, attr("ry", rx));
      rx = std::min(rx, width / 2.0);
      ry = std::min(ry, height / 2.0);
      LiveShapeParams params;
      params.left = x;
      params.top = y;
      params.right = x + width;
      params.bottom = y + height;
      params.index = 0;
      if (rx > kEpsilon || ry > kEpsilon) {
        params.kind = LiveShapeKind::RoundedRectangle;
        const double radius = std::min(std::max(rx, ry), std::min(width, height) / 2.0);
        params.corner_radii = {radius, radius, radius, radius};
        if (std::abs(rx - ry) > kEpsilon) {
          notice("An SVG rectangle with different rx/ry corner radii was approximated");
        }
      } else {
        params.kind = LiveShapeKind::Rectangle;
      }
      populate_live_shape_box_corners(params);
      geometry.path.subpaths = generate_live_shape_subpaths(params);
      geometry.live = params;
    } else if (node.name == "circle" || node.name == "ellipse") {
      const double cx = attr("cx");
      const double cy = attr("cy");
      const double rx = node.name == "circle" ? attr("r") : attr("rx");
      const double ry = node.name == "circle" ? attr("r") : attr("ry");
      if (rx <= 0.0 || ry <= 0.0) {
        return std::nullopt;
      }
      LiveShapeParams params;
      params.kind = LiveShapeKind::Ellipse;
      params.left = cx - rx;
      params.top = cy - ry;
      params.right = cx + rx;
      params.bottom = cy + ry;
      params.index = 0;
      populate_live_shape_box_corners(params);
      geometry.path.subpaths = generate_live_shape_subpaths(params);
      geometry.live = params;
    } else if (node.name == "line") {
      PathSubpath subpath;
      subpath.closed = false;
      subpath.anchors = {corner_anchor(attr("x1"), attr("y1")), corner_anchor(attr("x2"), attr("y2"))};
      geometry.path.subpaths.push_back(std::move(subpath));
    } else {
      return std::nullopt;
    }
    return geometry;
  }

  // --- layer construction ---

  std::optional<Layer> import_shape(const XmlNode& node, const Style& style, const Affine& parent_transform) {
    auto geometry = element_geometry(node);
    if (!geometry.has_value() || geometry->path.subpaths.empty()) {
      // Degenerate geometry (zero-size rect, two-value polygon) draws
      // nothing; importing an empty path would mean a full-canvas fill layer.
      return std::nullopt;
    }
    count_drawable();
    Affine local;
    if (const auto* transform_text = node.attribute("transform")) {
      local = parse_transform(*transform_text);
    }
    const auto transform = detail::multiply(parent_transform, local);
    const double stroke_scale = std::sqrt(std::abs(detail::determinant(transform)));
    if (style.stroke_width > 0.0 && lower_ascii(style.stroke) != "none" &&
        std::abs(std::hypot(transform.a, transform.b) - std::hypot(transform.c, transform.d)) >
            0.05 * std::max(1.0, stroke_scale)) {
      notice("An anisotropic SVG transform approximated a stroke width by its area scale");
    }

    // Group the subpaths per the element's fill rule before transforming.
    if (style.even_odd) {
      for (auto& subpath : geometry->path.subpaths) {
        subpath.shape_group = 0;
        subpath.op = PathCombineOp::Add;
      }
    } else {
      decompose_nonzero(geometry->path);
    }

    // Live parameters survive only translate + positive axis-aligned scale
    // (the vogk keyShapeInvalidated rule); rounded rects additionally need a
    // uniform scale to keep scalar corner radii truthful.
    auto live = geometry->live;
    if (live.has_value()) {
      const bool axis_aligned = detail::positive_axis_scale_translate(transform);
      const bool uniform = std::abs(transform.a - transform.d) < 1e-6;
      if (!axis_aligned || (live->kind == LiveShapeKind::RoundedRectangle && !uniform)) {
        live.reset();
      } else {
        live->left = live->left * transform.a + transform.e;
        live->right = live->right * transform.a + transform.e;
        live->top = live->top * transform.d + transform.f;
        live->bottom = live->bottom * transform.d + transform.f;
        for (auto& radius : live->corner_radii) {
          radius *= std::sqrt(transform.a * transform.d);
        }
        populate_live_shape_box_corners(*live);
      }
    }
    transform_path(geometry->path, transform);

    VectorShapeContent content;
    content.path = std::move(geometry->path);
    const auto fill_paint = resolve_paint(style.fill, style, content.path);
    const auto stroke_paint = resolve_paint(style.stroke, style, content.path);
    content.fill = fill_paint.fill;

    auto& stroke = content.stroke;
    stroke.enabled = stroke_paint.fill.kind != VectorFillKind::None && style.stroke_width * stroke_scale > kEpsilon;
    stroke.width = std::max(0.01, style.stroke_width * stroke_scale);
    stroke.cap = style.cap;
    stroke.join = style.join;
    stroke.miter_limit = style.stroke_miterlimit;
    stroke.alignment = VectorStrokeAlignment::Center;  // SVG strokes are always centered...
    // ...except for Patchy's own exports: inside/outside strokes leave at
    // double width with a data-patchy hint carrying the true geometry, so a
    // round trip restores the exact alignment and width.
    if (const auto* align_hint = node.attribute("data-patchy-stroke-align")) {
      if (*align_hint == "inside") {
        stroke.alignment = VectorStrokeAlignment::Inside;
      } else if (*align_hint == "outside") {
        stroke.alignment = VectorStrokeAlignment::Outside;
      }
      if (stroke.alignment != VectorStrokeAlignment::Center) {
        const auto* width_hint = node.attribute("data-patchy-stroke-width");
        const double true_width =
            width_hint != nullptr ? number_or(*width_hint, stroke.width / 2.0) * stroke_scale : stroke.width / 2.0;
        stroke.width = std::max(0.01, true_width);
      }
    }
    stroke.content = stroke_paint.fill;
    stroke.dashes = style.stroke_dashes;
    for (auto& dash : stroke.dashes) {
      dash = std::max(0.0, dash * stroke_scale) / stroke.width;  // user units -> width multiples
    }
    stroke.dash_offset = style.stroke_dashoffset * stroke_scale / stroke.width;

    // Opacity model: the layer carries opacity x fill-opacity (x solid-fill
    // alpha); the stroke's own opacity divides that back out. A stroke more
    // opaque than the fill cannot be represented and clamps with a notice.
    const bool has_fill = content.fill.kind != VectorFillKind::None;
    const double fill_factor = has_fill ? style.fill_opacity * fill_paint.alpha : 1.0;
    const double layer_opacity = std::clamp(style.opacity * fill_factor, 0.0, 1.0);
    const double stroke_target = style.stroke_opacity * stroke_paint.alpha;
    stroke.opacity = std::clamp(fill_factor > 1e-6 ? stroke_target / fill_factor : stroke_target, 0.0, 1.0);
    if (stroke.enabled && stroke_target > fill_factor + 1e-6) {
      notice("An SVG stroke more opaque than its fill was clamped to the fill opacity");
    }

    // A plain stroked <line> becomes Photoshop's live Line shape: the weight-w
    // filled quad, with the stroke paint as the fill.
    if (node.name == "line" && stroke.enabled && stroke.dashes.empty() && stroke.cap == VectorStrokeCap::Butt &&
        content.path.subpaths.size() == 1 && content.path.subpaths.front().anchors.size() == 2) {
      LiveShapeParams line_params;
      line_params.kind = LiveShapeKind::Line;
      const auto& anchors = content.path.subpaths.front().anchors;
      line_params.line_start_x = anchors[0].anchor_x;
      line_params.line_start_y = anchors[0].anchor_y;
      line_params.line_end_x = anchors[1].anchor_x;
      line_params.line_end_y = anchors[1].anchor_y;
      line_params.line_weight = stroke.width;
      line_params.left = std::min(line_params.line_start_x, line_params.line_end_x);
      line_params.top = std::min(line_params.line_start_y, line_params.line_end_y);
      line_params.right = std::max(line_params.line_start_x, line_params.line_end_x);
      line_params.bottom = std::max(line_params.line_start_y, line_params.line_end_y);
      line_params.index = 0;
      populate_live_shape_box_corners(line_params);
      content.path.subpaths = generate_live_shape_subpaths(line_params);
      content.fill = stroke.content;
      const double line_opacity = std::clamp(style.opacity * stroke_target, 0.0, 1.0);
      stroke = VectorStroke{};
      content.origination = {line_params};
      Layer layer = make_shape_layer(node, "Line", std::move(content), style, transform);
      layer.set_opacity(static_cast<float>(line_opacity));
      return layer;
    }
    if (live.has_value()) {
      content.origination = {*live};
    }
    const std::string type = node.name == "rect"                                  ? "Rectangle"
                             : (node.name == "circle" || node.name == "ellipse") ? "Ellipse"
                             : node.name == "line"                                ? "Line"
                                                                                  : "Shape";
    Layer layer = make_shape_layer(node, type, std::move(content), style, transform);
    layer.set_opacity(static_cast<float>(layer_opacity));
    return layer;
  }

  Layer make_shape_layer(const XmlNode& node, const std::string& type, VectorShapeContent content, const Style& style,
                         const Affine& transform) {
    Layer layer(document.allocate_layer_id(), name_for(node, type), LayerKind::Pixel);
    layer.metadata()[kLayerMetadataVectorShape] = "1";
    mark_layer_vector_block_dirty(layer);  // no preserved PSD blocks: always regenerate on save
    layer.set_blend_mode(detail::blend_mode_from_css(style.blend));
    layer.set_visible(!style.display_none && style.visible);
    layer.set_vector_shape(std::move(content));
    if (auto mask = vector_mask_from_clip(node, transform); mask.has_value()) {
      layer.set_vector_mask(std::move(*mask));
    }
    if (auto mask = raster_mask_from_mask(node, transform); mask.has_value()) {
      layer.set_mask(std::move(*mask));
    }
    update_vector_shape_raster(layer, canvas, &document.metadata().patterns);
    update_vector_mask_raster(layer, canvas);
    return layer;
  }

  std::optional<Layer> import_text(const XmlNode& node, const Style& style, const Affine& parent_transform) {
    // Whitespace collapses per the default xml:space handling.
    std::string collapsed;
    for (const char c : node.all_text()) {
      if (is_ascii_space(c)) {
        if (!collapsed.empty() && collapsed.back() != ' ') {
          collapsed.push_back(' ');
        }
      } else {
        collapsed.push_back(c);
      }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') {
      collapsed.pop_back();
    }
    if (collapsed.empty()) {
      return std::nullopt;
    }
    count_drawable();
    const auto x_values = number_list(node.attribute("x") != nullptr ? *node.attribute("x") : "0");
    const auto y_values = number_list(node.attribute("y") != nullptr ? *node.attribute("y") : "0");
    if (x_values.size() > 1 || y_values.size() > 1 || node.attribute("textLength") != nullptr) {
      notice("Complex SVG text positioning was reduced to a plain text layer");
    }
    for (const auto& child : node.children) {
      if (child.name == "textPath") {
        notice("SVG textPath was imported as ordinary text");
      }
    }
    Affine local;
    if (const auto* transform_text = node.attribute("transform")) {
      local = parse_transform(*transform_text);
    }
    const auto transform = detail::multiply(parent_transform, local);
    const auto baseline = detail::map_point(transform, x_values.empty() ? 0.0 : x_values.front(),
                                            y_values.empty() ? 0.0 : y_values.front());
    const double scale = std::sqrt(std::abs(detail::determinant(transform)));
    const double font_size = std::max(1.0, style.font_size * (scale > kEpsilon ? scale : 1.0));
    const auto fill = parse_color(style.fill).value_or(std::array<double, 4>{0.0, 0.0, 0.0, 1.0});

    // Text layers follow the shape/smart-object pattern: LayerKind::Pixel
    // with patchy.text.* metadata (layer_is_text is the real predicate;
    // set_pixels() demotes any other kind to Pixel anyway).
    Layer layer(document.allocate_layer_id(), name_for(node, "Text"), LayerKind::Pixel);
    layer.metadata()[kLayerMetadataText] = collapsed;
    layer.metadata()[kLayerMetadataTextFont] = style.font_family;
    layer.metadata()[kLayerMetadataTextSize] = std::to_string(std::max(1, static_cast<int>(std::lround(font_size))));
    // #rrggbb, the QColor-parseable form text_render_inputs_from_layer expects.
    constexpr char kHex[] = "0123456789abcdef";
    const auto color = to_rgb(fill);
    std::string hex = "#......";
    hex[1] = kHex[color.red >> 4];
    hex[2] = kHex[color.red & 15];
    hex[3] = kHex[color.green >> 4];
    hex[4] = kHex[color.green & 15];
    hex[5] = kHex[color.blue >> 4];
    hex[6] = kHex[color.blue & 15];
    layer.metadata()[kLayerMetadataTextColor] = hex;
    layer.metadata()[kLayerMetadataTextBold] = style.bold ? "true" : "false";
    layer.metadata()[kLayerMetadataTextItalic] = style.italic ? "true" : "false";
    layer.metadata()[kPendingTextKey] = "1";
    layer.metadata()[kTextAnchorKey] = style.text_anchor;
    layer.metadata()[kTextBaselineXKey] = detail::format_number(baseline[0]);
    layer.metadata()[kTextBaselineYKey] = detail::format_number(baseline[1]);
    // Rough placement until the UI post-open pass renders the text and
    // repositions using real glyph metrics.
    layer.set_bounds(Rect{static_cast<std::int32_t>(std::lround(baseline[0])),
                          static_cast<std::int32_t>(std::lround(baseline[1] - font_size)), 1, 1});
    layer.set_opacity(static_cast<float>(std::clamp(style.opacity * style.fill_opacity * fill[3], 0.0, 1.0)));
    layer.set_blend_mode(detail::blend_mode_from_css(style.blend));
    layer.set_visible(!style.display_none && style.visible);
    return layer;
  }

  std::optional<Layer> import_image(const XmlNode& node, const Style& style, const Affine& parent_transform) {
    const auto* href = node.attribute("href");
    if (href == nullptr) {
      return std::nullopt;
    }
    const auto lower = lower_ascii(std::string_view(*href).substr(0, 64));
    if (!lower.starts_with("data:image/")) {
      notice("An external SVG image reference was skipped (only embedded data URIs import)");
      return std::nullopt;
    }
    count_drawable();
    Affine local;
    if (const auto* transform_text = node.attribute("transform")) {
      local = parse_transform(*transform_text);
    }
    const auto transform = detail::multiply(parent_transform, local);
    const double x = number_or(node.attribute("x") != nullptr ? *node.attribute("x") : "0", 0.0);
    const double y = number_or(node.attribute("y") != nullptr ? *node.attribute("y") : "0", 0.0);
    const double width = number_or(node.attribute("width") != nullptr ? *node.attribute("width") : "0", 0.0);
    const double height = number_or(node.attribute("height") != nullptr ? *node.attribute("height") : "0", 0.0);
    if (width <= 0.0 || height <= 0.0) {
      return std::nullopt;
    }
    // Map both corners so the placement rect carries the transform's scale;
    // rotation/skew of images degrades to the axis-aligned box with a notice.
    const auto corner_a = detail::map_point(transform, x, y);
    const auto corner_b = detail::map_point(transform, x + width, y + height);
    if (std::abs(transform.b) > 1e-6 || std::abs(transform.c) > 1e-6) {
      notice("A rotated or skewed SVG image was imported axis-aligned");
    }
    const auto left = static_cast<std::int32_t>(std::lround(std::min(corner_a[0], corner_b[0])));
    const auto top = static_cast<std::int32_t>(std::lround(std::min(corner_a[1], corner_b[1])));
    const auto right = static_cast<std::int32_t>(std::lround(std::max(corner_a[0], corner_b[0])));
    const auto bottom = static_cast<std::int32_t>(std::lround(std::max(corner_a[1], corner_b[1])));

    Layer layer(document.allocate_layer_id(), name_for(node, "Image"), LayerKind::Pixel);
    layer.metadata()[kPendingImageKey] = *href;
    layer.set_bounds(Rect{left, top, std::max(1, right - left), std::max(1, bottom - top)});
    layer.set_opacity(static_cast<float>(std::clamp(style.opacity, 0.0, 1.0)));
    layer.set_blend_mode(detail::blend_mode_from_css(style.blend));
    layer.set_visible(!style.display_none && style.visible);
    return layer;
  }

  // Should a <switch> child be taken? Conditional attributes must all pass;
  // absent attributes pass, requiredExtensions/-Features fail when present
  // (Patchy implements none), and systemLanguage passes on an English entry.
  static bool switch_child_selectable(const XmlNode& child) {
    if (child.attribute("requiredExtensions") != nullptr || child.attribute("requiredFeatures") != nullptr) {
      return false;
    }
    if (const auto* language = child.attribute("systemLanguage")) {
      return language->starts_with("en") || language->find(",en") != std::string::npos;
    }
    return true;
  }

  // Walks one container's children in document order and returns the layers
  // bottom-to-top (identical to both SVG paint order and Patchy's layers()
  // order, so no reversal happens anywhere).
  std::vector<Layer> import_children(const XmlNode& node, const Style& inherited, const Affine& parent_transform,
                                     int use_depth) {
    std::vector<Layer> layers;
    for (const auto& child : node.children) {
      if (child.is_text() || child.name == "defs" || child.name == "style" || child.name == "title" ||
          child.name == "desc" || child.name == "metadata" || child.name == "clipPath" || child.name == "mask" ||
          child.name == "linearGradient" || child.name == "radialGradient" || child.name == "pattern" ||
          child.name == "symbol" || child.name == "marker" || child.name == "filter" ||
          child.name == "foreignObject" || child.name.find(':') != std::string::npos) {
        continue;  // non-rendered/definition content and foreign-namespace elements
      }
      const auto style = resolve_style(child, inherited, css);
      if (child.name == "switch") {
        for (const auto& candidate : child.children) {
          if (candidate.is_text() || !switch_child_selectable(candidate)) {
            continue;
          }
          XmlNode wrapper;
          wrapper.name = "g";
          wrapper.children.push_back(candidate);
          auto picked = import_children(wrapper, style, parent_transform, use_depth);
          std::move(picked.begin(), picked.end(), std::back_inserter(layers));
          break;
        }
        continue;
      }
      if (child.name == "use") {
        if (use_depth >= kMaximumUseDepth) {
          notice("SVG <use> nesting is too deep; the deepest references were skipped");
          continue;
        }
        const auto* href = child.attribute("href");
        if (href == nullptr || href->empty() || href->front() != '#') {
          notice("An SVG <use> without a local reference was skipped");
          continue;
        }
        if (!use_stack.insert(*href).second) {
          notice("A cyclic SVG <use> reference was skipped");
          continue;
        }
        const auto found = ids.find(href->substr(1));
        if (found == ids.end()) {
          notice("An SVG <use> references a missing element");
        } else {
          Affine placement{1, 0, 0, 1,
                           number_or(child.attribute("x") != nullptr ? *child.attribute("x") : "0", 0.0),
                           number_or(child.attribute("y") != nullptr ? *child.attribute("y") : "0", 0.0)};
          if (const auto* transform_text = child.attribute("transform")) {
            placement = detail::multiply(parse_transform(*transform_text), placement);
          }
          XmlNode wrapper;
          wrapper.name = "g";
          if (found->second->name == "symbol" || found->second->name == "svg") {
            // An instantiated symbol renders like a <g> (it never renders
            // directly); its viewBox scaling is not modeled.
            XmlNode instance = *found->second;
            instance.name = "g";
            if (instance.attribute("viewBox") != nullptr) {
              notice("An SVG symbol viewBox was ignored (contents placed unscaled)");
            }
            wrapper.children.push_back(std::move(instance));
          } else {
            wrapper.children.push_back(*found->second);
          }
          auto cloned = import_children(wrapper, style, detail::multiply(parent_transform, placement), use_depth + 1);
          std::move(cloned.begin(), cloned.end(), std::back_inserter(layers));
        }
        use_stack.erase(*href);
        continue;
      }
      if (child.name == "a") {
        // Links are transparent containers: children inline, no folder.
        auto nested = import_children(child, style, parent_transform, use_depth);
        std::move(nested.begin(), nested.end(), std::back_inserter(layers));
        continue;
      }
      if (child.name == "g" || child.name == "svg") {
        Affine local;
        if (const auto* transform_text = child.attribute("transform")) {
          local = parse_transform(*transform_text);
        }
        if (child.name == "svg") {
          // Nested <svg> viewports: honor x/y placement, skip inner viewBox.
          local = detail::multiply(local,
                                   Affine{1, 0, 0, 1,
                                          number_or(child.attribute("x") != nullptr ? *child.attribute("x") : "0", 0.0),
                                          number_or(child.attribute("y") != nullptr ? *child.attribute("y") : "0", 0.0)});
          if (child.attribute("viewBox") != nullptr) {
            notice("A nested SVG viewBox was ignored");
          }
        }
        const auto child_transform = detail::multiply(parent_transform, local);
        auto nested = import_children(child, style, child_transform, use_depth);
        Layer group(document.allocate_layer_id(), name_for(child, "Group"), LayerKind::Group);
        group.set_opacity(static_cast<float>(style.opacity));
        group.set_blend_mode(detail::blend_mode_from_css(style.blend));
        group.set_visible(!style.display_none && style.visible);
        for (auto& nested_layer : nested) {
          group.add_child(std::move(nested_layer));
        }
        if (auto mask = vector_mask_from_clip(child, child_transform); mask.has_value()) {
          group.set_vector_mask(std::move(*mask));
          update_vector_mask_raster(group, canvas);
        }
        if (auto mask = raster_mask_from_mask(child, child_transform); mask.has_value()) {
          group.set_mask(std::move(*mask));
        }
        layers.push_back(std::move(group));
        continue;
      }
      std::optional<Layer> layer;
      if (child.name == "text") {
        layer = import_text(child, style, parent_transform);
      } else if (child.name == "image") {
        layer = import_image(child, style, parent_transform);
      } else {
        layer = import_shape(child, style, parent_transform);
      }
      if (layer.has_value()) {
        layers.push_back(std::move(*layer));
      } else {
        static const std::set<std::string, std::less<>> kKnownDrawables = {
            "text", "image", "path", "rect", "circle", "ellipse", "line", "polygon", "polyline"};
        if (!kKnownDrawables.contains(child.name)) {
          notice("Unsupported SVG element <" + child.name + "> was skipped");
        }
      }
    }
    return layers;
  }

  Document run() {
    if (root.name != "svg") {
      throw std::runtime_error("Not an SVG document (root element is <" + root.name + ">)");
    }
    index_ids(root);
    int order = 0;
    collect_css_rules(root, css, order);

    // Canvas size: CSS pixels at 96/in for physical units (the print size
    // survives via 96 PPI); unitless/percent sizes fall back to the viewBox
    // at the untagged-import 72 PPI.
    bool physical = false;
    const auto parse_length = [&physical](const std::string* value) -> std::optional<double> {
      if (value == nullptr) {
        return std::nullopt;
      }
      const auto text = trimmed(*value);
      if (text.empty() || text.ends_with('%')) {
        return std::nullopt;
      }
      std::size_t split = 0;
      while (split < text.size() &&
             (text[split] == '+' || text[split] == '-' || text[split] == '.' || text[split] == 'e' ||
              text[split] == 'E' || (text[split] >= '0' && text[split] <= '9'))) {
        ++split;
      }
      double number = 0.0;
      if (!parse_double(text.substr(0, split), number)) {
        return std::nullopt;
      }
      const auto unit = lower_ascii(text.substr(split));
      constexpr double kCssPixelsPerInch = 96.0;
      if (unit.empty() || unit == "px") {
        return number;
      }
      if (unit == "in") {
        physical = true;
        return number * kCssPixelsPerInch;
      }
      if (unit == "cm") {
        physical = true;
        return number * kCssPixelsPerInch / 2.54;
      }
      if (unit == "mm") {
        physical = true;
        return number * kCssPixelsPerInch / 25.4;
      }
      if (unit == "pt") {
        physical = true;
        return number * kCssPixelsPerInch / 72.0;
      }
      if (unit == "pc") {
        physical = true;
        return number * kCssPixelsPerInch / 6.0;
      }
      return std::nullopt;  // em/ex and friends: fall through to the viewBox
    };
    const auto view = number_list(root.attribute("viewBox") != nullptr ? *root.attribute("viewBox") : "");
    auto width = parse_length(root.attribute("width"));
    auto height = parse_length(root.attribute("height"));
    if (!width.has_value() && view.size() == 4 && view[2] > 0.0) {
      width = view[2];
    }
    if (!height.has_value() && view.size() == 4 && view[3] > 0.0) {
      height = view[3];
    }
    if (!width.has_value() || !height.has_value() || *width <= 0.0 || *height <= 0.0) {
      // The CSS default replaced-element size.
      width = 300.0;
      height = 150.0;
      notice("SVG has no usable width/height or viewBox; opened at 300 x 150");
    }
    double scale_clamp = 1.0;
    if (*width > kMaximumCanvasSize || *height > kMaximumCanvasSize) {
      scale_clamp = std::min(kMaximumCanvasSize / *width, kMaximumCanvasSize / *height);
      *width *= scale_clamp;
      *height *= scale_clamp;
      notice("SVG canvas was scaled down to Patchy's 30000 px document limit");
    }
    document = Document(std::max(1, static_cast<int>(std::lround(*width))),
                        std::max(1, static_cast<int>(std::lround(*height))), PixelFormat::rgba8());
    document.print_settings().horizontal_ppi = physical ? 96.0 : 72.0;
    document.print_settings().vertical_ppi = physical ? 96.0 : 72.0;
    canvas = Rect::from_size(document.width(), document.height());

    // viewBox -> viewport mapping with the full preserveAspectRatio grammar
    // (default xMidYMid meet).
    Affine viewport{scale_clamp, 0, 0, scale_clamp, 0, 0};
    if (view.size() == 4 && view[2] > 0.0 && view[3] > 0.0) {
      const double scale_x = *width / view[2];
      const double scale_y = *height / view[3];
      const auto par = lower_ascii(root.attribute("preserveAspectRatio") != nullptr
                                       ? trimmed(*root.attribute("preserveAspectRatio"))
                                       : "xmidymid meet");
      if (par.find("none") != std::string::npos) {
        viewport = {scale_x, 0, 0, scale_y, -view[0] * scale_x, -view[1] * scale_y};
      } else {
        const bool slice = par.find("slice") != std::string::npos;
        const double scale = slice ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);
        double align_x = 0.5;
        double align_y = 0.5;
        if (par.find("xmin") != std::string::npos) {
          align_x = 0.0;
        } else if (par.find("xmax") != std::string::npos) {
          align_x = 1.0;
        }
        if (par.find("ymin") != std::string::npos) {
          align_y = 0.0;
        } else if (par.find("ymax") != std::string::npos) {
          align_y = 1.0;
        }
        viewport = {scale, 0, 0, scale, -view[0] * scale + (*width - view[2] * scale) * align_x,
                    -view[1] * scale + (*height - view[3] * scale) * align_y};
      }
    }

    const Style root_style = resolve_style(root, Style{}, css);
    auto imported = import_children(root, root_style, viewport, 0);
    for (auto& layer : imported) {
      document.add_layer(std::move(layer));
    }
    if (!document.layers().empty()) {
      document.set_active_layer(document.layers().back().id());  // topmost, like a fresh Photoshop open
    }
    return std::move(document);
  }
};

// .svgz / gzip-compressed .svg (RFC 1952 member header + raw deflate body).
std::vector<std::uint8_t> maybe_inflate(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 2 || bytes[0] != 0x1F || bytes[1] != 0x8B) {
    return {bytes.begin(), bytes.end()};
  }
  if (bytes.size() < 18 || bytes[2] != 8) {
    throw std::runtime_error("SVGZ gzip header is invalid");
  }
  const auto flags = bytes[3];
  std::size_t position = 10;
  if ((flags & 0x04U) != 0) {  // FEXTRA
    if (position + 2 > bytes.size()) {
      throw std::runtime_error("SVGZ gzip header is truncated");
    }
    const std::size_t extra = bytes[position] | (static_cast<std::size_t>(bytes[position + 1]) << 8);
    position += 2 + extra;
  }
  const auto skip_zero_terminated = [&] {
    while (position < bytes.size() && bytes[position] != 0) {
      ++position;
    }
    if (position >= bytes.size()) {
      throw std::runtime_error("SVGZ gzip header is truncated");
    }
    ++position;
  };
  if ((flags & 0x08U) != 0) {  // FNAME
    skip_zero_terminated();
  }
  if ((flags & 0x10U) != 0) {  // FCOMMENT
    skip_zero_terminated();
  }
  if ((flags & 0x02U) != 0) {  // FHCRC
    position += 2;
  }
  if (position + 8 > bytes.size()) {
    throw std::runtime_error("SVGZ gzip data is truncated");
  }
  std::size_t output_size = 0;
  void* output = tinfl_decompress_mem_to_heap(bytes.data() + position, bytes.size() - position - 8, &output_size, 0);
  if (output == nullptr) {
    throw std::runtime_error("SVGZ data could not be decompressed");
  }
  if (output_size > kMaximumInflatedBytes) {
    mz_free(output);
    throw std::runtime_error("SVGZ data decompresses to an unreasonable size");
  }
  std::vector<std::uint8_t> result(static_cast<std::uint8_t*>(output),
                                   static_cast<std::uint8_t*>(output) + output_size);
  mz_free(output);
  return result;
}

}  // namespace

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  const auto xml = maybe_inflate(bytes);
  const auto root = parse_xml(xml);
  Importer importer{root, notices};
  return importer.run();
}

const std::vector<std::string>& svg_extensions() {
  static const std::vector<std::string> extensions{".svg", ".svgz"};
  return extensions;
}

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B) {
    return true;  // gzip: assume svgz (nothing else gzipped claims these extensions)
  }
  const auto limit = std::min<std::size_t>(bytes.size(), 512);
  const std::string_view prefix(reinterpret_cast<const char*>(bytes.data()), limit);
  return prefix.find("<svg") != std::string_view::npos || prefix.find("<?xml") != std::string_view::npos;
}

}  // namespace patchy::svg
