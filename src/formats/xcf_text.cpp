// GIMP text layers (docs/xcf.md, "Text layers"): the "gimp-text-layer" parasite is a list of
// (key value...) forms. This file parses it and writes Patchy's patchy.text.* metadata;
// the layer keeps GIMP's raster until the text is edited. The keys come from files GIMP
// writes, read as data.

#include "core/layer_metadata.hpp"
#include "formats/xcf_internal.hpp"
#include "psd/psd_text_runs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace patchy::xcf::detail {

namespace {

// --- the parasite's S-expression form --------------------------------------------------------

struct Node {
  bool list{false};
  bool quoted{false};
  std::string atom;
  std::vector<Node> items;
};

class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  [[nodiscard]] std::vector<Node> parse_all() {
    std::vector<Node> nodes;
    while (true) {
      skip_space();
      if (at_ >= text_.size()) {
        return nodes;
      }
      nodes.push_back(parse(0));
      if (nodes.size() > 10000) {
        throw ImportError("the text parasite is too long");
      }
    }
  }

private:
  void skip_space() {
    while (at_ < text_.size()) {
      const auto c = static_cast<unsigned char>(text_[at_]);
      if (c == '#' || c == ';') {  // comments
        while (at_ < text_.size() && text_[at_] != '\n') {
          ++at_;
        }
        continue;
      }
      if (c == 0 || std::isspace(c) != 0) {
        ++at_;
        continue;
      }
      break;
    }
  }

  [[nodiscard]] Node parse(int depth) {
    if (depth > 32) {
      throw ImportError("the text parasite nests too deeply");
    }
    skip_space();
    if (at_ >= text_.size()) {
      throw ImportError("the text parasite ends early");
    }
    Node node;
    if (text_[at_] == '(') {
      node.list = true;
      ++at_;
      while (true) {
        skip_space();
        if (at_ >= text_.size()) {
          throw ImportError("the text parasite ends inside a list");
        }
        if (text_[at_] == ')') {
          ++at_;
          return node;
        }
        node.items.push_back(parse(depth + 1));
      }
    }
    if (text_[at_] == '"') {
      node.quoted = true;
      ++at_;
      while (true) {
        if (at_ >= text_.size()) {
          throw ImportError("the text parasite ends inside a string");
        }
        const char c = text_[at_++];
        if (c == '"') {
          return node;
        }
        if (c != '\\') {
          node.atom.push_back(c);
          continue;
        }
        if (at_ >= text_.size()) {
          throw ImportError("the text parasite ends inside an escape");
        }
        const char e = text_[at_++];
        if (e >= '0' && e <= '7') {
          int value = e - '0';
          for (int i = 0; i < 2 && at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '7'; ++i) {
            value = value * 8 + (text_[at_++] - '0');
          }
          node.atom.push_back(static_cast<char>(value & 0xFF));
        } else if (e == 'n') {
          node.atom.push_back('\n');
        } else if (e == 't') {
          node.atom.push_back('\t');
        } else if (e == 'r') {
          node.atom.push_back('\r');
        } else if (e == 'b') {
          node.atom.push_back('\b');
        } else if (e == 'f') {
          node.atom.push_back('\f');
        } else if (e == 'v') {
          node.atom.push_back('\v');
        } else {
          node.atom.push_back(e);
        }
      }
    }
    while (at_ < text_.size()) {
      const auto c = static_cast<unsigned char>(text_[at_]);
      if (c == '(' || c == ')' || c == '"' || std::isspace(c) != 0 || c == 0) {
        break;
      }
      node.atom.push_back(static_cast<char>(c));
      ++at_;
    }
    if (node.atom.empty()) {
      throw ImportError("the text parasite has a stray character");
    }
    return node;
  }

  std::string_view text_;
  std::size_t at_{0};
};

[[nodiscard]] std::optional<double> number_of(const Node& node) {
  if (node.list || node.atom.empty()) {
    return std::nullopt;
  }
  char* end = nullptr;
  const std::string text = node.atom;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] float little_float(const std::string& bytes, std::size_t index) {
  float value = 0.0F;
  if ((index + 1) * 4 <= bytes.size()) {
    unsigned char raw[4];
    for (int i = 0; i < 4; ++i) {
      raw[i] = static_cast<unsigned char>(bytes[index * 4 + static_cast<std::size_t>(i)]);
    }
    const std::uint32_t bits = static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8U) |
                               (static_cast<std::uint32_t>(raw[2]) << 16U) | (static_cast<std::uint32_t>(raw[3]) << 24U);
    std::memcpy(&value, &bits, sizeof(value));
  }
  return value;
}

[[nodiscard]] double little_double(const std::string& bytes, std::size_t index) {
  double value = 0.0;
  if ((index + 1) * 8 <= bytes.size()) {
    std::uint64_t bits = 0;
    for (int i = 7; i >= 0; --i) {
      bits = (bits << 8U) | static_cast<unsigned char>(bytes[index * 8 + static_cast<std::size_t>(i)]);
    }
    std::memcpy(&value, &bits, sizeof(value));
  }
  return value;
}

[[nodiscard]] double encode_srgb(double linear) {
  linear = std::clamp(linear, 0.0, 1.0);
  return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

enum class ColorModel { Rgb, Gray, Hsv, Hsl, Lab, Lch };

// HSV and HSL in babl are the textbook models over the perceptual (encoded) RGB values: a
// GIMP 3 text color picked in HSV matches the hex GIMP shows for it.
[[nodiscard]] std::array<double, 3> rgb_from_hsv(double h, double s, double v) {
  h = (h - std::floor(h)) * 6.0;
  const int sector = std::min(5, static_cast<int>(h));
  const double f = h - sector;
  const double p = v * (1.0 - s);
  const double q = v * (1.0 - s * f);
  const double t = v * (1.0 - s * (1.0 - f));
  switch (sector) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
  }
}

[[nodiscard]] std::array<double, 3> rgb_from_hsl(double h, double s, double l) {
  const double v = l + s * std::min(l, 1.0 - l);
  const double sv = v <= 0.0 ? 0.0 : 2.0 * (1.0 - l / v);
  return rgb_from_hsv(h, sv, v);
}

// CIE Lab with a D50 white to encoded sRGB through the Bradford-adapted matrix (the space
// docs/xcf.md names for GIMP's LCh modes).
[[nodiscard]] std::array<double, 3> rgb_from_lab(double l, double a, double b) {
  const double fy = (l + 16.0) / 116.0;
  const double fx = fy + a / 500.0;
  const double fz = fy - b / 200.0;
  const auto inverse = [](double f) {
    constexpr double kEpsilon = 6.0 / 29.0;
    return f > kEpsilon ? f * f * f : 3.0 * kEpsilon * kEpsilon * (f - 4.0 / 29.0);
  };
  const double x = 0.96422 * inverse(fx);
  const double y = inverse(fy);
  const double z = 0.82521 * inverse(fz);
  return {encode_srgb(3.1338561 * x - 1.6168667 * y - 0.4906146 * z),
          encode_srgb(-0.9787684 * x + 1.9161415 * y + 0.0334540 * z),
          encode_srgb(0.0719453 * x - 0.2289914 * y + 1.4052427 * z)};
}

// A color value: GIMP 3's (color "<babl format>" <size> "<bytes>" <profile size> [profile])
// or GIMP 2's (color-rgb r g b) / (color-rgba r g b a). Returns encoded sRGB RGBA. GIMP 3
// keeps the model the color was picked in: "R'G'B'A float", "HSVA float", "CIE Lab float"...
[[nodiscard]] std::optional<std::array<double, 4>> color_of(const Node& node) {
  if (!node.list || node.items.empty() || node.items[0].list) {
    return std::nullopt;
  }
  const auto& head = node.items[0].atom;
  if (head == "color-rgb" || head == "color-rgba") {
    std::array<double, 4> rgba{0.0, 0.0, 0.0, 1.0};
    for (std::size_t i = 1; i < node.items.size() && i <= 4; ++i) {
      rgba[i - 1] = std::clamp(number_of(node.items[i]).value_or(0.0), 0.0, 1.0);
    }
    return rgba;
  }
  if (head != "color" || node.items.size() < 4 || !node.items[1].quoted || !node.items[3].quoted) {
    return std::nullopt;
  }
  const auto& format = node.items[1].atom;
  const auto& bytes = node.items[3].atom;
  const auto starts = [&format](std::string_view prefix) { return format.rfind(prefix, 0) == 0; };
  ColorModel model = ColorModel::Rgb;
  if (starts("HSV")) {
    model = ColorModel::Hsv;
  } else if (starts("HSL")) {
    model = ColorModel::Hsl;
  } else if (starts("CIE LCH(ab)")) {
    model = ColorModel::Lch;
  } else if (starts("CIE Lab")) {
    model = ColorModel::Lab;
  } else if (starts("Y")) {
    model = ColorModel::Gray;
  } else if (!starts("R")) {
    return std::nullopt;  // CMYK and other models: GIMP's raster shows the color until an edit
  }
  const bool perceptual = format.find('\'') != std::string::npos || format.find('~') != std::string::npos;
  const bool gray = model == ColorModel::Gray;
  const bool has_alpha = format.find("A ") != std::string::npos || format.find(" alpha") != std::string::npos;
  const std::size_t channels = (gray ? 1U : 3U) + (has_alpha ? 1U : 0U);
  std::array<double, 4> values{0.0, 0.0, 0.0, 1.0};
  std::array<double, 4> raw{0.0, 0.0, 0.0, 1.0};
  for (std::size_t c = 0; c < channels; ++c) {
    if (format.find("double") != std::string::npos) {
      raw[c] = little_double(bytes, c);
    } else if (format.find("float") != std::string::npos) {
      raw[c] = little_float(bytes, c);
    } else if (format.find("u8") != std::string::npos) {
      raw[c] = c < bytes.size() ? static_cast<unsigned char>(bytes[c]) / 255.0 : 0.0;
    } else if (format.find("u16") != std::string::npos) {
      raw[c] = c * 2 + 1 < bytes.size()
                   ? (static_cast<unsigned char>(bytes[c * 2]) | (static_cast<unsigned char>(bytes[c * 2 + 1]) << 8U)) /
                         65535.0
                   : 0.0;
    } else {
      return std::nullopt;
    }
    if (!std::isfinite(raw[c])) {
      raw[c] = 0.0;
    }
  }
  const std::size_t color_count = gray ? 1U : 3U;
  std::array<double, 3> rgb{};
  switch (model) {
    case ColorModel::Hsv:
      rgb = rgb_from_hsv(raw[0], std::clamp(raw[1], 0.0, 1.0), std::clamp(raw[2], 0.0, 1.0));
      break;
    case ColorModel::Hsl:
      rgb = rgb_from_hsl(raw[0], std::clamp(raw[1], 0.0, 1.0), std::clamp(raw[2], 0.0, 1.0));
      break;
    case ColorModel::Lab:
      rgb = rgb_from_lab(raw[0], raw[1], raw[2]);
      break;
    case ColorModel::Lch: {
      constexpr double kPi = 3.14159265358979323846;
      const double hue = raw[2] * kPi / 180.0;
      rgb = rgb_from_lab(raw[0], raw[1] * std::cos(hue), raw[1] * std::sin(hue));
      break;
    }
    case ColorModel::Rgb:
    case ColorModel::Gray:
      for (std::size_t c = 0; c < 3; ++c) {
        const double v = raw[gray ? 0 : c];
        rgb[c] = perceptual ? v : encode_srgb(v);
      }
      break;
  }
  for (std::size_t c = 0; c < 3; ++c) {
    values[c] = std::isfinite(rgb[c]) ? std::clamp(rgb[c], 0.0, 1.0) : 0.0;
  }
  values[3] = has_alpha ? std::clamp(raw[color_count], 0.0, 1.0) : 1.0;
  return values;
}

[[nodiscard]] const Node* value_of(const Node& form) {
  return form.items.size() >= 2 ? &form.items[1] : nullptr;
}

// --- Pango markup -------------------------------------------------------------------------

struct MarkupStyle {
  bool bold{false};
  bool italic{false};
  bool underline{false};
  bool strikethrough{false};
  std::optional<RgbColor> color;
  std::optional<double> size;  // document pixels
  std::optional<std::string> family;
  std::optional<std::string> style;  // the face a span's font names ("Bold Condensed")
  double rise{0.0};                  // document pixels, up
  std::optional<double> letter_spacing;  // document pixels
};

// What markup spans resolve against: GIMP's font table and the image resolution (span sizes
// are points, 1024 per point, at the image's resolution).
struct MarkupContext {
  const std::vector<TextParasite::MarkupFont>* fonts{nullptr};
  double ppi{72.0};
};

struct MarkupRun {
  std::string text;
  MarkupStyle style;
};

[[nodiscard]] std::string decode_entities(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }
    const auto end = text.find(';', i);
    if (end == std::string_view::npos || end - i > 10) {
      out.push_back('&');
      continue;
    }
    const auto entity = text.substr(i + 1, end - i - 1);
    if (entity == "amp") {
      out.push_back('&');
    } else if (entity == "lt") {
      out.push_back('<');
    } else if (entity == "gt") {
      out.push_back('>');
    } else if (entity == "quot") {
      out.push_back('"');
    } else if (entity == "apos") {
      out.push_back('\'');
    } else if (!entity.empty() && entity[0] == '#') {
      unsigned long code = 0;
      try {
        code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                   ? std::stoul(std::string(entity.substr(2)), nullptr, 16)
                   : std::stoul(std::string(entity.substr(1)), nullptr, 10);
      } catch (const std::exception&) {
        code = 0xFFFD;
      }
      if (code > 0x10FFFF) {
        code = 0xFFFD;
      }
      if (code < 0x80) {
        out.push_back(static_cast<char>(code));
      } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6U)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
      } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12U)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18U)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
      }
    } else {
      out.append(text.substr(i, end - i + 1));
    }
    i = end;
  }
  return out;
}

[[nodiscard]] std::optional<std::string> attribute(std::string_view tag, std::string_view name) {
  std::size_t at = 0;
  while ((at = tag.find(name, at)) != std::string_view::npos) {
    const bool start_ok = at == 0 || std::isspace(static_cast<unsigned char>(tag[at - 1])) != 0;
    auto after = at + name.size();
    while (after < tag.size() && std::isspace(static_cast<unsigned char>(tag[after])) != 0) {
      ++after;
    }
    if (start_ok && after < tag.size() && tag[after] == '=') {
      ++after;
      while (after < tag.size() && std::isspace(static_cast<unsigned char>(tag[after])) != 0) {
        ++after;
      }
      if (after < tag.size() && (tag[after] == '"' || tag[after] == '\'')) {
        const char quote = tag[after];
        const auto close = tag.find(quote, after + 1);
        if (close != std::string_view::npos) {
          return decode_entities(tag.substr(after + 1, close - after - 1));
        }
      }
    }
    at += name.size();
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<RgbColor> parse_hex_color(const std::string& value) {
  if (value.size() < 4 || value[0] != '#') {
    return std::nullopt;
  }
  const auto hex = value.substr(1);
  const auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (hex.size() == 6 || hex.size() == 8) {
    std::array<int, 3> v{};
    for (int i = 0; i < 3; ++i) {
      const int hi = digit(hex[static_cast<std::size_t>(i * 2)]);
      const int lo = digit(hex[static_cast<std::size_t>(i * 2 + 1)]);
      if (hi < 0 || lo < 0) return std::nullopt;
      v[static_cast<std::size_t>(i)] = hi * 16 + lo;
    }
    return RgbColor{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]), static_cast<std::uint8_t>(v[2])};
  }
  if (hex.size() == 12) {  // #rrrrggggbbbb
    std::array<int, 3> v{};
    for (int i = 0; i < 3; ++i) {
      const int hi = digit(hex[static_cast<std::size_t>(i * 4)]);
      const int lo = digit(hex[static_cast<std::size_t>(i * 4 + 1)]);
      if (hi < 0 || lo < 0) return std::nullopt;
      v[static_cast<std::size_t>(i)] = hi * 16 + lo;
    }
    return RgbColor{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]), static_cast<std::uint8_t>(v[2])};
  }
  return std::nullopt;
}

struct FontChoice {
  std::string family;
  std::string style;
  bool bold{false};
  bool italic{false};
};

[[nodiscard]] FontChoice choose_font_from(const std::string& description, const std::string& family_name,
                                          const std::string& style_name);

// A Pango number attribute ("12288", "-3072"), in 1024ths.
[[nodiscard]] std::optional<double> pango_units(const std::string& text) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value / 1024.0;
}

[[nodiscard]] std::vector<MarkupRun> parse_markup(std::string_view markup, const MarkupStyle& base,
                                                  const MarkupContext& context) {
  std::vector<MarkupRun> runs;
  std::vector<MarkupStyle> stack{base};
  std::size_t at = 0;
  std::string pending;
  const auto flush = [&] {
    if (!pending.empty()) {
      runs.push_back(MarkupRun{decode_entities(pending), stack.back()});
      pending.clear();
    }
  };
  while (at < markup.size()) {
    if (markup[at] != '<') {
      pending.push_back(markup[at++]);
      continue;
    }
    const auto close = markup.find('>', at);
    if (close == std::string_view::npos) {
      pending.append(markup.substr(at));
      break;
    }
    flush();
    auto tag = markup.substr(at + 1, close - at - 1);
    at = close + 1;
    if (!tag.empty() && tag[0] == '/') {
      if (stack.size() > 1) {
        stack.pop_back();
      }
      continue;
    }
    const bool self_closing = !tag.empty() && tag.back() == '/';
    std::size_t name_end = 0;
    while (name_end < tag.size() && std::isalpha(static_cast<unsigned char>(tag[name_end])) != 0) {
      ++name_end;
    }
    const auto name = std::string(tag.substr(0, name_end));
    MarkupStyle style = stack.back();
    if (name == "b") {
      style.bold = true;
    } else if (name == "i") {
      style.italic = true;
    } else if (name == "u") {
      style.underline = true;
    } else if (name == "s") {
      style.strikethrough = true;
    } else if (name == "span") {
      // GIMP 3 names a span's font by an id from its (markupfont ...) table; GIMP 2 and other
      // writers put a Pango description ("Serif Bold Italic 12") there.
      if (auto font = attribute(tag, "font").has_value() ? attribute(tag, "font") : attribute(tag, "font_desc")) {
        const TextParasite::MarkupFont* known = nullptr;
        if (context.fonts != nullptr) {
          for (const auto& entry : *context.fonts) {
            if (entry.id == *font) {
              known = &entry;
            }
          }
        }
        const auto choice = known != nullptr ? choose_font_from(known->full_name, known->family, known->style)
                                             : choose_font_from(*font, std::string(), std::string());
        style.family = choice.family;
        style.style = choice.style;
        style.bold = choice.bold;
        style.italic = choice.italic;
      }
      if (auto color = attribute(tag, "foreground"); color.has_value()) {
        style.color = parse_hex_color(*color);
      } else if (auto fg = attribute(tag, "color"); fg.has_value()) {
        style.color = parse_hex_color(*fg);
      }
      if (auto weight = attribute(tag, "weight")) {
        style.bold = *weight == "bold" || *weight == "heavy" || *weight == "ultrabold" ||
                     (std::isdigit(static_cast<unsigned char>((*weight)[0])) != 0 && std::atoi(weight->c_str()) >= 600);
      }
      if (auto slant = attribute(tag, "style")) {
        style.italic = *slant == "italic" || *slant == "oblique";
      }
      if (auto size = attribute(tag, "size")) {
        // 1024ths of a point ("12pt" is points), at the image resolution: GIMP's 50 px at
        // 300 ppi is size="12288".
        auto points = pango_units(*size);
        if (points.has_value() && size->size() > 2 && size->compare(size->size() - 2, 2, "pt") == 0) {
          points = *points * 1024.0;
        }
        if (points.has_value() && *points > 0.0) {
          style.size = *points * context.ppi / 72.0;
        }
      }
      if (auto family = attribute(tag, "font_family")) {
        style.family = *family;
        style.style.reset();
      } else if (auto face = attribute(tag, "face")) {
        style.family = *face;
        style.style.reset();
      }
      if (auto underline = attribute(tag, "underline")) {
        style.underline = *underline != "none" && *underline != "false";
      }
      if (auto strikethrough = attribute(tag, "strikethrough")) {
        style.strikethrough = *strikethrough == "true";
      }
      // rise and letter_spacing are 1024ths of a device pixel (GIMP's Baseline 17 is 17408).
      if (auto rise = attribute(tag, "rise")) {
        style.rise = pango_units(*rise).value_or(style.rise);
      }
      if (auto spacing = attribute(tag, "letter_spacing")) {
        if (const auto value = pango_units(*spacing)) {
          style.letter_spacing = *value;
        }
      }
    }
    if (!self_closing && name != "markup") {
      stack.push_back(style);
    }
    if (stack.size() > 64) {
      throw ImportError("the text markup nests too deeply");
    }
  }
  flush();
  return runs;
}

// --- fonts --------------------------------------------------------------------------------

[[nodiscard]] bool word_is_style(std::string_view word) {
  static constexpr std::array kWords{"bold",      "italic",    "oblique", "light",  "regular",   "medium",
                                      "semibold", "semi-bold", "demibold", "heavy", "black",     "thin",
                                      "book",     "condensed", "ultra-light", "extra-light", "ultra-bold",
                                      "extra-bold", "normal",  "roman"};
  std::string lower;
  for (const char c : word) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return std::find(kWords.begin(), kWords.end(), lower) != kWords.end();
}

[[nodiscard]] std::string lower_of(std::string_view text) {
  std::string lower;
  for (const char c : text) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return lower;
}

// GIMP 2 stores a Pango description ("Sans-serif Bold Italic"); GIMP 3 a full name with the
// family and style beside it. Generic family aliases (Sans-serif, Serif, Monospace) name no
// face, so the full name minus its style words wins over them.
[[nodiscard]] FontChoice choose_font_from(const std::string& description, const std::string& family_name,
                                          const std::string& style_name) {
  FontChoice choice;
  std::string style = style_name;
  std::string family = family_name;
  const auto generic = [](const std::string& name) {
    const auto lower = lower_of(name);
    return lower == "sans-serif" || lower == "sans" || lower == "serif" || lower == "monospace" || lower == "mono";
  };
  std::vector<std::string> words;
  std::string word;
  const std::string& source = !family_name.empty() && !generic(family_name) ? family_name : description;
  for (const char c : source) {
    if (c == ' ') {
      if (!word.empty()) words.push_back(word);
      word.clear();
    } else if (c != ',') {
      word.push_back(c);
    }
  }
  if (!word.empty()) words.push_back(word);
  // A trailing size ("Sans 12") belongs to Pango, not the family.
  while (!words.empty() && std::isdigit(static_cast<unsigned char>(words.back()[0])) != 0) {
    words.pop_back();
  }
  std::string trailing_style;
  if (family.empty() || generic(family)) {
    while (words.size() > 1 && word_is_style(words.back())) {
      trailing_style = words.back() + (trailing_style.empty() ? "" : " " + trailing_style);
      words.pop_back();
    }
    std::string joined;
    for (const auto& w : words) {
      joined += (joined.empty() ? "" : " ") + w;
    }
    if (!joined.empty() && (family.empty() || (generic(family) && !generic(joined)))) {
      family = joined;
    }
  }
  if (style.empty()) {
    style = trailing_style;
  }
  // fontconfig's generic names as Qt's.
  const auto lower_family = lower_of(family);
  if (family.empty() || family == "GimpFont" || lower_family == "sans-serif" || lower_family == "sans") {
    family = "Sans Serif";
  } else if (lower_family == "monospace" || lower_family == "mono") {
    family = "Monospace";
  } else if (lower_family == "serif") {
    family = "Serif";
  }
  const auto lower_style = lower_of(style);
  choice.family = family;
  choice.bold = lower_style.find("bold") != std::string::npos || lower_style.find("black") != std::string::npos ||
                lower_style.find("heavy") != std::string::npos;
  choice.italic = lower_style.find("italic") != std::string::npos || lower_style.find("oblique") != std::string::npos;
  if (lower_style != "regular" && lower_style != "normal" && lower_style != "roman" && lower_style != "book") {
    choice.style = style;
  }
  return choice;
}

[[nodiscard]] FontChoice choose_font(const TextParasite& parasite) {
  return choose_font_from(parasite.font, parasite.font_family, parasite.font_style);
}

// A length in a GIMP unit ("pixels", "points", ...) as document pixels at `ppi`.
[[nodiscard]] std::optional<double> unit_scale(const std::string& unit, double ppi) {
  if (unit == "points") {
    return ppi / 72.0;
  }
  if (unit == "inches") {
    return ppi;
  }
  if (unit == "millimeters") {
    return ppi / 25.4;
  }
  if (unit == "picas") {
    return ppi / 6.0;
  }
  if (unit == "pixels" || unit == "pixel") {
    return 1.0;
  }
  return std::nullopt;
}

[[nodiscard]] int utf16_length(std::string_view text) {
  int count = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if ((c & 0xC0U) == 0x80U) {
      continue;
    }
    count += (c >= 0xF0U) ? 2 : 1;
  }
  return count;
}

[[nodiscard]] std::string hex_of(RgbColor color) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string hex = "#......";
  hex[1] = kHex[color.red >> 4U];
  hex[2] = kHex[color.red & 15U];
  hex[3] = kHex[color.green >> 4U];
  hex[4] = kHex[color.green & 15U];
  hex[5] = kHex[color.blue >> 4U];
  hex[6] = kHex[color.blue & 15U];
  return hex;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> parse_config_forms(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> forms;
  try {
    Parser parser(text);
    for (const auto& form : parser.parse_all()) {
      if (!form.list || form.items.empty() || form.items[0].list) {
        continue;
      }
      std::string value;
      if (form.items.size() >= 2 && !form.items[1].list) {
        value = form.items[1].atom;
      }
      forms.emplace_back(form.items[0].atom, std::move(value));
    }
  } catch (const ImportError& error) {
    trace(std::string("filter config: ") + error.what());
    forms.clear();
  }
  return forms;
}

TextParasite parse_text_parasite(std::string_view text) {
  TextParasite parasite;
  // GIMP 3 follows the markup string with (markupfont "font1136" (font (fullname ...)
  // (family ...) (style ...))) forms, one per font its spans name.
  const auto read_markup_font = [&parasite](const Node& form) {
    if (form.items.size() < 3 || form.items[1].list || !form.items[2].list || parasite.markup_fonts.size() >= 1000) {
      return;
    }
    TextParasite::MarkupFont font;
    font.id = form.items[1].atom;
    for (const auto& sub : form.items[2].items) {
      if (!sub.list || sub.items.size() < 2 || sub.items[0].list || sub.items[1].list) {
        continue;
      }
      if (sub.items[0].atom == "fullname") {
        font.full_name = sub.items[1].atom;
      } else if (sub.items[0].atom == "family") {
        font.family = sub.items[1].atom;
      } else if (sub.items[0].atom == "style") {
        font.style = sub.items[1].atom;
      }
    }
    parasite.markup_fonts.push_back(std::move(font));
  };
  Parser parser(text);
  for (const auto& form : parser.parse_all()) {
    if (!form.list || form.items.empty() || form.items[0].list) {
      continue;
    }
    const auto& key = form.items[0].atom;
    const auto* value = value_of(form);
    if (value == nullptr) {
      continue;
    }
    if (key == "text" && !value->list) {
      parasite.text = value->atom;
    } else if (key == "markup" && !value->list) {
      parasite.markup = value->atom;
      for (std::size_t i = 2; i < form.items.size(); ++i) {
        const auto& sub = form.items[i];
        if (sub.list && !sub.items.empty() && !sub.items[0].list && sub.items[0].atom == "markupfont") {
          read_markup_font(sub);
        }
      }
    } else if (key == "font" && !value->list) {
      parasite.font = value->atom;
      for (std::size_t i = 2; i < form.items.size(); ++i) {
        const auto& sub = form.items[i];
        if (!sub.list || sub.items.size() < 2 || sub.items[0].list || sub.items[1].list) {
          continue;
        }
        if (sub.items[0].atom == "fullname") {
          parasite.font = sub.items[1].atom;
        } else if (sub.items[0].atom == "family") {
          parasite.font_family = sub.items[1].atom;
        } else if (sub.items[0].atom == "style") {
          parasite.font_style = sub.items[1].atom;
        } else if (sub.items[0].atom == "psname") {
          parasite.font_psname = sub.items[1].atom;
        }
      }
    } else if (key == "font-size") {
      parasite.font_size = number_of(*value).value_or(0.0);
    } else if (key == "font-size-unit" && !value->list) {
      parasite.font_size_unit = value->atom;
    } else if (key == "antialias" && !value->list) {
      parasite.antialias = value->atom != "no";
    } else if (key == "justify" && !value->list) {
      parasite.justify = value->atom;
    } else if (key == "indent") {
      parasite.indent = number_of(*value).value_or(0.0);
    } else if (key == "line-spacing") {
      parasite.line_spacing = number_of(*value).value_or(0.0);
    } else if (key == "letter-spacing") {
      parasite.letter_spacing = number_of(*value).value_or(0.0);
    } else if (key == "box-mode" && !value->list) {
      parasite.box_mode = value->atom;
    } else if (key == "box-width") {
      parasite.box_width = number_of(*value).value_or(0.0);
    } else if (key == "box-height") {
      parasite.box_height = number_of(*value).value_or(0.0);
    } else if (key == "base-direction" && !value->list) {
      parasite.base_direction = value->atom;
    } else if (key == "color") {
      if (const auto color = color_of(*value)) {
        parasite.have_color = true;
        for (int c = 0; c < 4; ++c) {
          parasite.color[c] = (*color)[static_cast<std::size_t>(c)];
        }
      }
    } else if (key == "outline" && !value->list) {
      parasite.outline = value->atom;
    } else if (key == "outline-direction" && !value->list) {
      parasite.outline_direction = value->atom;
    } else if (key == "outline-width") {
      parasite.outline_width = number_of(*value).value_or(0.0);
    } else if (key == "outline-unit" && !value->list) {
      parasite.outline_unit = value->atom;
    } else if (key == "outline-custom-style" && !value->list) {
      parasite.outline_custom_style = value->atom;
    } else if (key == "outline-dash-info") {
      parasite.outline_dashed = number_of(*value).value_or(0.0) > 0.0;  // the dash count first
    } else if (key == "outline-foreground") {
      if (const auto color = color_of(*value)) {
        parasite.have_outline_color = true;
        for (int c = 0; c < 4; ++c) {
          parasite.outline_color[c] = (*color)[static_cast<std::size_t>(c)];
        }
      }
    } else if (key == "markupfont") {
      read_markup_font(form);
    }
  }
  return parasite;
}

TextImport apply_text_parasite(const TextParasite& parasite, const TextLayerContext& context, Layer& layer) {
  const auto unit = unit_scale(parasite.font_size_unit, context.ppi);
  if (!unit.has_value()) {
    return TextImport::Unreadable;
  }
  const double scale = *unit;  // the font size unit to document pixels
  const double size = parasite.font_size * scale;
  if (!std::isfinite(size) || size < 0.5 || size > 4000.0) {
    return TextImport::Unreadable;
  }
  // GIMP 3's outline becomes a Stroke style. GIMP's raster already holds it, so MainWindow
  // redraws the text post-open; pattern and dashed outlines have no Stroke form.
  const bool outlined = !parasite.outline.empty() && parasite.outline != "none";
  double outline_px = 0.0;
  if (outlined) {
    const auto outline_unit = unit_scale(parasite.outline_unit, context.ppi);
    outline_px = parasite.outline_width * outline_unit.value_or(0.0);
    if (parasite.outline_custom_style != "solid-color" || parasite.outline_dashed || !std::isfinite(outline_px) ||
        outline_px <= 0.0 || outline_px > 250.0) {
      return TextImport::OutlineUnsupported;
    }
  }
  const auto font = choose_font(parasite);
  const auto to_byte = [](double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
  RgbColor color{0, 0, 0};
  if (parasite.have_color) {
    color = RgbColor{to_byte(parasite.color[0]), to_byte(parasite.color[1]), to_byte(parasite.color[2])};
  }
  MarkupStyle base;
  base.bold = font.bold;
  base.italic = font.italic;
  base.color = color;
  std::vector<MarkupRun> runs;
  if (!parasite.markup.empty()) {
    runs = parse_markup(parasite.markup, base, MarkupContext{&parasite.markup_fonts, context.ppi});
  } else {
    runs.push_back(MarkupRun{parasite.text, base});
  }
  std::string text;
  for (const auto& run : runs) {
    text += run.text;
  }
  if (text.empty() || text.find_first_not_of(" \t\r\n") == std::string::npos) {
    return TextImport::Unreadable;
  }
  const int size_px = std::clamp(static_cast<int>(std::lround(size)), 1, 4000);
  const double layer_spacing = parasite.letter_spacing * scale;
  int justification = 0;
  if (parasite.justify == "right") {
    justification = 1;
  } else if (parasite.justify == "center") {
    justification = 2;
  } else if (parasite.justify == "fill") {
    justification = 3;
  }

  // Pango grows a risen line's box by the rise, so text risen as a whole draws where it would
  // without it (GIMP's Baseline on all of a layer's text); only a rise against the rest of the
  // line moves glyphs, which is Patchy's baseline shift.
  bool uniform_rise = true;
  const MarkupRun* first_run = nullptr;
  for (const auto& run : runs) {
    if (run.text.empty()) {
      continue;
    }
    if (first_run == nullptr) {
      first_run = &run;
    }
    uniform_rise = uniform_rise && run.style.rise == first_run->style.rise;
  }

  std::vector<psd::PsdTextStyleRun> style_runs;
  int start = 0;
  bool mixed = false;
  bool styled = false;  // anything only the runs carry
  for (const auto& run : runs) {
    if (run.text.empty()) {
      continue;
    }
    psd::PsdTextStyleRun style;
    style.start = start;
    style.length = utf16_length(run.text);
    start += style.length;
    style.family = run.style.family.value_or(font.family);
    style.size = run.style.size.has_value() ? std::clamp(*run.style.size, 1.0, 4000.0) : size_px;
    style.size = std::round(style.size);
    style.color = run.style.color.value_or(color);
    style.bold = run.style.bold;
    style.italic = run.style.italic;
    if (run.style.style.has_value()) {
      style.style = *run.style.style;
    } else if (!run.style.family.has_value() && run.style.bold == font.bold && run.style.italic == font.italic) {
      style.style = font.style;
    }
    style.underline = run.style.underline;
    style.strikethrough = run.style.strikethrough;
    style.baseline_shift = uniform_rise ? 0.0 : run.style.rise;
    // GIMP's letter spacing is pixels between glyphs; Patchy's tracking is 1/1000 em.
    const double spacing = run.style.letter_spacing.value_or(layer_spacing);
    style.tracking = std::abs(spacing) > 1e-6 ? spacing / style.size * 1000.0 : 0.0;
    if (std::abs(parasite.line_spacing) > 1e-6) {
      // GIMP adds the spacing to the font's natural line height; 1.2 em stands in for it.
      style.leading = std::max(1.0, style.size * 1.2 + parasite.line_spacing * scale);
    }
    styled = styled || !style.style.empty() || style.underline || style.strikethrough ||
             std::abs(style.baseline_shift) > 1e-6 || std::abs(style.tracking) > 1e-6;
    if (!style_runs.empty()) {
      const auto& first = style_runs.front();
      mixed = mixed || first.family != style.family || first.size != style.size || first.color != style.color ||
              first.bold != style.bold || first.italic != style.italic || first.style != style.style ||
              first.underline != style.underline || first.strikethrough != style.strikethrough ||
              first.baseline_shift != style.baseline_shift || first.tracking != style.tracking;
    }
    style_runs.push_back(std::move(style));
  }

  // The layer-wide values follow the first run: a span can set the font of all the text.
  const auto& lead = style_runs.front();
  auto& metadata = layer.metadata();
  metadata[kLayerMetadataText] = text;
  metadata[kLayerMetadataTextFont] = lead.family;
  metadata[kLayerMetadataTextSize] = std::to_string(static_cast<int>(lead.size));
  metadata[kLayerMetadataTextColor] = hex_of(lead.color);
  metadata[kLayerMetadataTextBold] = lead.bold ? "true" : "false";
  metadata[kLayerMetadataTextItalic] = lead.italic ? "true" : "false";
  metadata[kLayerMetadataTextAntiAlias] = std::to_string(parasite.antialias ? kTextAntiAliasSmooth : kTextAntiAliasNone);
  const bool boxed = parasite.box_mode == "fixed" && parasite.box_width > 0.0 && parasite.box_height > 0.0;
  metadata[kLayerMetadataTextFlow] = boxed ? "box" : "point";
  const auto box_width = boxed ? parasite.box_width : static_cast<double>(context.layer_width);
  const auto box_height = boxed ? parasite.box_height : static_cast<double>(context.layer_height);
  metadata[kLayerMetadataTextBoxWidth] = std::to_string(std::max(1, static_cast<int>(std::lround(box_width))));
  metadata[kLayerMetadataTextBoxHeight] = std::to_string(std::max(1, static_cast<int>(std::lround(box_height))));

  std::vector<psd::PsdTextParagraphRun> paragraphs;
  const bool paragraph_layout = justification != 0 || std::abs(parasite.indent) > 1e-6 || parasite.base_direction == "rtl";
  if (paragraph_layout) {
    psd::PsdTextParagraphRun paragraph;
    paragraph.start = 0;
    paragraph.length = utf16_length(text);
    paragraph.justification = justification;
    paragraph.first_line_indent = parasite.indent * scale;
    if (parasite.base_direction == "rtl") {
      paragraph.direction = "rtl";
    }
    paragraphs.push_back(paragraph);
  }
  const bool needs_runs = mixed || styled || paragraph_layout || std::abs(parasite.line_spacing) > 1e-6;
  if (needs_runs) {
    metadata[kLayerMetadataTextRuns] = psd::serialize_patchy_text_runs(style_runs);
  }
  if (paragraph_layout) {
    metadata[kLayerMetadataTextParagraphRuns] = psd::serialize_patchy_paragraph_runs(paragraphs);
  }
  if (mixed || justification != 0) {
    metadata[kLayerMetadataTextHtml] = psd::html_from_text_runs(text, style_runs, paragraphs);
  }
  if (!outlined) {
    return TextImport::Editable;
  }
  RgbColor outline_color{0, 0, 0};
  double outline_opacity = 1.0;
  if (parasite.have_outline_color) {
    outline_color = RgbColor{to_byte(parasite.outline_color[0]), to_byte(parasite.outline_color[1]),
                             to_byte(parasite.outline_color[2])};
    outline_opacity = parasite.outline_color[3];
  }
  // Measured on GIMP 3.2's renders: an outer outline of width w is a band w wide outside the
  // glyphs, as an Outside stroke of size w.
  const char* position = parasite.outline_direction == "inner"   ? "inside"
                         : parasite.outline_direction == "outer" ? "outside"
                                                                 : "center";
  const auto number = [](double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << value;
    return out.str();
  };
  metadata[kLayerMetadataXcfPendingOutline] = std::string(position) + " " + number(outline_px) + " " +
                                              hex_of(outline_color) + " " + number(outline_opacity) + " " +
                                              (parasite.outline == "stroke-only" ? "nofill" : "fill");
  return TextImport::OutlineRedrawn;
}

}  // namespace patchy::xcf::detail
