// GIMP text layers (docs/xcf.md, "Text layers"): the "gimp-text-layer" parasite is a list of
// (key value...) forms. This file parses it and writes Patchy's patchy.text.* metadata;
// the layer keeps GIMP's raster until the text is edited. The keys come from files GIMP
// writes, read as data.

#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "formats/xcf_internal.hpp"
#include "psd/psd_text_runs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
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

// A color value: GIMP 3's (color "<babl format>" <size> "<bytes>" <profile size> [profile])
// or GIMP 2's (color-rgb r g b) / (color-rgba r g b a). Returns encoded sRGB RGBA.
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
  const auto& bytes = node.items[3].atom;
  return gegl_color_to_srgb(node.items[1].atom,
                            std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

[[nodiscard]] const Node* value_of(const Node& form) {
  return form.items.size() >= 2 ? &form.items[1] : nullptr;
}

// --- Pango markup -------------------------------------------------------------------------

struct MarkupStyle {
  bool bold{false};
  bool italic{false};
  std::optional<RgbColor> color;
  std::optional<double> size;  // in the layer's font size unit
  std::optional<std::string> family;
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

[[nodiscard]] std::vector<MarkupRun> parse_markup(std::string_view markup, const MarkupStyle& base) {
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
    } else if (name == "span") {
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
        // Pango units: 1024 per unit of the layer's size.
        char* end = nullptr;
        const double value = std::strtod(size->c_str(), &end);
        if (end != size->c_str() && std::isfinite(value) && value > 0.0) {
          style.size = value / 1024.0;
        }
      }
      if (auto family = attribute(tag, "font_family")) {
        style.family = *family;
      } else if (auto face = attribute(tag, "face")) {
        style.family = *face;
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

struct FontChoice {
  std::string family;
  std::string style;
  bool bold{false};
  bool italic{false};
};

// GIMP 2 stores a Pango description ("Sans-serif Bold Italic"); GIMP 3 a full name with the
// family and style beside it. Generic family aliases (Sans-serif, Serif, Monospace) name no
// face, so the full name minus its style words wins over them.
[[nodiscard]] FontChoice choose_font(const TextParasite& parasite) {
  FontChoice choice;
  std::string description = parasite.font;
  std::string style = parasite.font_style;
  std::string family = parasite.font_family;
  const auto generic = [](const std::string& name) {
    const auto lower = lower_of(name);
    return lower == "sans-serif" || lower == "sans" || lower == "serif" || lower == "monospace" || lower == "mono";
  };
  std::vector<std::string> words;
  std::string word;
  const std::string& source =
      !parasite.font_family.empty() && !generic(parasite.font_family) ? parasite.font_family : description;
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

std::optional<std::array<double, 4>> gegl_color_to_srgb(std::string_view format, std::span<const std::uint8_t> bytes) {
  // "<model> <type>": the type is the last word.
  const auto space = format.rfind(' ');
  if (space == std::string_view::npos) {
    return std::nullopt;
  }
  const auto model = format.substr(0, space);
  const auto type = format.substr(space + 1);
  std::size_t width = 0;
  if (type == "float") {
    width = 4;
  } else if (type == "double") {
    width = 8;
  } else if (type == "u8") {
    width = 1;
  } else if (type == "u16") {
    width = 2;
  } else {
    return std::nullopt;
  }
  const auto sample = [&](std::size_t index) {
    double value = 0.0;
    if ((index + 1) * width > bytes.size()) {
      return value;
    }
    std::uint64_t bits = 0;
    for (std::size_t i = width; i-- > 0;) {
      bits = (bits << 8U) | bytes[index * width + i];  // little-endian
    }
    if (width == 4) {
      float narrow = 0.0F;
      const auto word = static_cast<std::uint32_t>(bits);
      std::memcpy(&narrow, &word, sizeof(narrow));
      value = narrow;
    } else if (width == 8) {
      std::memcpy(&value, &bits, sizeof(value));
    } else {
      value = static_cast<double>(bits) / (width == 1 ? 255.0 : 65535.0);
    }
    return std::isfinite(value) ? value : 0.0;
  };
  enum class Kind { Rgb, Gray, Hsv, Hsl, Lab, Lch };
  Kind kind = Kind::Rgb;
  bool alpha = false;
  bool premultiplied = false;
  bool linear = false;
  if (model == "HSV" || model == "HSVA" || model == "HSL" || model == "HSLA") {
    kind = model[2] == 'V' ? Kind::Hsv : Kind::Hsl;
    alpha = model.size() == 4;
  } else if (model == "CIE Lab" || model == "CIE Lab alpha" || model == "CIE LCH(ab)" ||
             model == "CIE LCH(ab) alpha") {
    if (width < 4) {
      return std::nullopt;
    }
    kind = model.find("LCH") != std::string_view::npos ? Kind::Lch : Kind::Lab;
    alpha = model.ends_with(" alpha");
  } else {
    // R'G'B'A, RGBA, R~G~B~, Y'A, RaGaBaA, ...: ' and ~ mark the sRGB curve (babl's
    // perceptual curve is sRGB's), an "a" after a channel premultiplies it.
    std::string letters;
    for (const char c : model) {
      if (c == '\'' || c == '~') {
        continue;
      }
      if (c == 'a') {
        premultiplied = true;
        continue;
      }
      letters.push_back(c);
    }
    linear = model.find('\'') == std::string_view::npos && model.find('~') == std::string_view::npos;
    if (letters == "RGB" || letters == "RGBA") {
      alpha = letters.size() == 4;
    } else if (letters == "Y" || letters == "YA") {
      kind = Kind::Gray;
      alpha = letters.size() == 2;
    } else {
      return std::nullopt;
    }
  }
  const std::size_t colors = kind == Kind::Gray ? 1U : 3U;
  std::array<double, 4> raw{0.0, 0.0, 0.0, 1.0};
  for (std::size_t c = 0; c < colors + (alpha ? 1U : 0U); ++c) {
    raw[c] = sample(c);
  }
  const double a = alpha ? std::clamp(raw[colors], 0.0, 1.0) : 1.0;
  std::array<double, 4> rgba{0.0, 0.0, 0.0, a};
  const auto from_linear = [](double v) { return srgb_encode(std::clamp(v, 0.0, 1.0)); };
  switch (kind) {
    case Kind::Rgb:
    case Kind::Gray:
      for (std::size_t c = 0; c < 3; ++c) {
        double v = raw[kind == Kind::Gray ? 0 : c];
        if (premultiplied) {
          v = a > 0.0 ? v / a : 0.0;
        }
        rgba[c] = linear ? from_linear(v) : std::clamp(v, 0.0, 1.0);
      }
      break;
    case Kind::Hsv:
    case Kind::Hsl: {
      // babl's HSV and HSL are on the sRGB-encoded values, the hue in turns.
      const double h = raw[0] - std::floor(raw[0]);
      const double s = std::clamp(raw[1], 0.0, 1.0);
      const double v = std::clamp(raw[2], 0.0, 1.0);
      const double chroma = kind == Kind::Hsv ? v * s : (1.0 - std::abs(2.0 * v - 1.0)) * s;
      const double base = kind == Kind::Hsv ? v - chroma : v - chroma / 2.0;
      const double sector = h * 6.0;
      const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
      std::array<double, 3> rgb{};
      switch (static_cast<int>(sector) % 6) {
        case 0:
          rgb = {chroma, x, 0.0};
          break;
        case 1:
          rgb = {x, chroma, 0.0};
          break;
        case 2:
          rgb = {0.0, chroma, x};
          break;
        case 3:
          rgb = {0.0, x, chroma};
          break;
        case 4:
          rgb = {x, 0.0, chroma};
          break;
        default:
          rgb = {chroma, 0.0, x};
          break;
      }
      for (std::size_t c = 0; c < 3; ++c) {
        rgba[c] = std::clamp(rgb[c] + base, 0.0, 1.0);
      }
      break;
    }
    case Kind::Lab:
    case Kind::Lch: {
      // CIE Lab with a D50 white, to sRGB through the Bradford-adapted matrix.
      double lab_a = raw[1];
      double lab_b = raw[2];
      if (kind == Kind::Lch) {
        constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
        lab_a = raw[1] * std::cos(raw[2] * kRadiansPerDegree);
        lab_b = raw[1] * std::sin(raw[2] * kRadiansPerDegree);
      }
      const auto inverse = [](double t) {
        constexpr double kDelta = 6.0 / 29.0;
        return t > kDelta ? t * t * t : 3.0 * kDelta * kDelta * (t - 4.0 / 29.0);
      };
      const double fy = (raw[0] + 16.0) / 116.0;
      const double x = 0.96422 * inverse(fy + lab_a / 500.0);
      const double y = inverse(fy);
      const double z = 0.82521 * inverse(fy - lab_b / 200.0);
      rgba[0] = from_linear(3.1338561 * x - 1.6168667 * y - 0.4906146 * z);
      rgba[1] = from_linear(-0.9787684 * x + 1.9161415 * y + 0.0334540 * z);
      rgba[2] = from_linear(0.0719453 * x - 0.2289914 * y + 1.4052427 * z);
      break;
    }
  }
  return rgba;
}

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
      parasite.outline = value->atom != "none";
    }
  }
  return parasite;
}

bool apply_text_parasite(const TextParasite& parasite, const TextLayerContext& context, Layer& layer) {
  if (parasite.outline) {
    return false;  // the outline is in GIMP's raster only: a pixel layer keeps it
  }
  double scale = 1.0;  // the font size unit to document pixels
  const auto& unit = parasite.font_size_unit;
  if (unit == "points") {
    scale = context.ppi / 72.0;
  } else if (unit == "inches") {
    scale = context.ppi;
  } else if (unit == "millimeters") {
    scale = context.ppi / 25.4;
  } else if (unit == "picas") {
    scale = context.ppi / 6.0;
  } else if (unit != "pixels" && unit != "pixel") {
    return false;
  }
  const double size = parasite.font_size * scale;
  if (!std::isfinite(size) || size < 0.5 || size > 4000.0) {
    return false;
  }
  const auto font = choose_font(parasite);
  RgbColor color{0, 0, 0};
  if (parasite.have_color) {
    const auto to_byte = [](double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
    color = RgbColor{to_byte(parasite.color[0]), to_byte(parasite.color[1]), to_byte(parasite.color[2])};
  }
  MarkupStyle base;
  base.bold = font.bold;
  base.italic = font.italic;
  base.color = color;
  std::vector<MarkupRun> runs;
  if (!parasite.markup.empty()) {
    runs = parse_markup(parasite.markup, base);
  } else {
    runs.push_back(MarkupRun{parasite.text, base});
  }
  std::string text;
  for (const auto& run : runs) {
    text += run.text;
  }
  if (text.empty() || text.find_first_not_of(" \t\r\n") == std::string::npos) {
    return false;
  }
  const int size_px = std::clamp(static_cast<int>(std::lround(size)), 1, 4000);
  const double tracking = std::abs(parasite.letter_spacing) > 1e-6 ? parasite.letter_spacing * scale / size * 1000.0 : 0.0;
  int justification = 0;
  if (parasite.justify == "right") {
    justification = 1;
  } else if (parasite.justify == "center") {
    justification = 2;
  } else if (parasite.justify == "fill") {
    justification = 3;
  }

  std::vector<psd::PsdTextStyleRun> style_runs;
  int start = 0;
  bool mixed = false;
  for (const auto& run : runs) {
    if (run.text.empty()) {
      continue;
    }
    psd::PsdTextStyleRun style;
    style.start = start;
    style.length = utf16_length(run.text);
    start += style.length;
    style.family = run.style.family.value_or(font.family);
    style.size = run.style.size.has_value() ? std::clamp(*run.style.size * scale, 1.0, 4000.0) : size_px;
    style.size = std::round(style.size);
    style.color = run.style.color.value_or(color);
    style.bold = run.style.bold;
    style.italic = run.style.italic;
    if (!run.style.family.has_value() && run.style.bold == font.bold && run.style.italic == font.italic) {
      style.style = font.style;
    }
    style.tracking = tracking;
    if (std::abs(parasite.line_spacing) > 1e-6) {
      // GIMP adds the spacing to the font's natural line height; 1.2 em stands in for it.
      style.leading = std::max(1.0, style.size * 1.2 + parasite.line_spacing * scale);
    }
    if (!style_runs.empty()) {
      const auto& first = style_runs.front();
      mixed = mixed || first.family != style.family || first.size != style.size || first.color != style.color ||
              first.bold != style.bold || first.italic != style.italic;
    }
    style_runs.push_back(std::move(style));
  }

  auto& metadata = layer.metadata();
  metadata[kLayerMetadataText] = text;
  metadata[kLayerMetadataTextFont] = font.family;
  metadata[kLayerMetadataTextSize] = std::to_string(size_px);
  metadata[kLayerMetadataTextColor] = hex_of(style_runs.empty() ? color : style_runs.front().color);
  metadata[kLayerMetadataTextBold] = font.bold ? "true" : "false";
  metadata[kLayerMetadataTextItalic] = font.italic ? "true" : "false";
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
  const bool needs_runs = mixed || paragraph_layout || std::abs(tracking) > 1e-6 ||
                          std::abs(parasite.line_spacing) > 1e-6 || !font.style.empty();
  if (needs_runs) {
    metadata[kLayerMetadataTextRuns] = psd::serialize_patchy_text_runs(style_runs);
  }
  if (paragraph_layout) {
    metadata[kLayerMetadataTextParagraphRuns] = psd::serialize_patchy_paragraph_runs(paragraphs);
  }
  if (mixed || justification != 0) {
    metadata[kLayerMetadataTextHtml] = psd::html_from_text_runs(text, style_runs, paragraphs);
  }
  return true;
}

}  // namespace patchy::xcf::detail
