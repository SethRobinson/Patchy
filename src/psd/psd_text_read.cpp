// Engine-data (text engine) READ side of the PSD text codec: TySh engine-data
// parsing (text, style/paragraph runs, fill color, anti-alias), Photoshop
// font-name resolution (DirectWrite/registry/heuristic), the placeholder-glyph
// preview renderer, and TySh descriptor-geometry extraction. Split out of
// psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace patchy::psd {

namespace {

std::vector<std::uint8_t> unescape_engine_bytes(std::span<const std::uint8_t> bytes) {
  std::vector<std::uint8_t> unescaped;
  unescaped.reserve(bytes.size());
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto byte = bytes[index];
    const auto ch = static_cast<char>(byte);
    if (ch != '\\') {
      unescaped.push_back(byte);
      continue;
    }
    if (index + 1 >= bytes.size()) {
      break;
    }

    const auto next = static_cast<char>(bytes[++index]);
    if (next >= '0' && next <= '7') {
      int value = next - '0';
      for (int digit = 0; digit < 2 && index + 1 < bytes.size(); ++digit) {
        const auto octal = static_cast<char>(bytes[index + 1]);
        if (octal < '0' || octal > '7') {
          break;
        }
        value = value * 8 + (octal - '0');
        ++index;
      }
      unescaped.push_back(static_cast<std::uint8_t>(value & 0xFF));
      continue;
    }

    switch (next) {
      case 'r':
        unescaped.push_back('\n');
        break;
      case 'n':
        unescaped.push_back('\n');
        break;
      case 't':
        unescaped.push_back('\t');
        break;
      case '\\':
      case '(':
      case ')':
        unescaped.push_back(static_cast<std::uint8_t>(next));
        break;
      default:
        unescaped.push_back(static_cast<std::uint8_t>(next));
        break;
    }
  }
  return unescaped;
}

std::string decode_engine_string(std::span<const std::uint8_t> bytes) {
  const auto unescaped = unescape_engine_bytes(bytes);
  if (unescaped.size() >= 2 && unescaped[0] == 0xFEU && unescaped[1] == 0xFFU) {
    std::string decoded;
    for (std::size_t index = 2; index + 1 < unescaped.size(); index += 2) {
      const auto codepoint = (static_cast<std::uint32_t>(unescaped[index]) << 8U) |
                             static_cast<std::uint32_t>(unescaped[index + 1]);
      append_utf8(decoded, codepoint);
    }
    return decoded;
  }
  if (unescaped.size() >= 2 && unescaped[0] == 0xFFU && unescaped[1] == 0xFEU) {
    std::string decoded;
    for (std::size_t index = 2; index + 1 < unescaped.size(); index += 2) {
      const auto codepoint = (static_cast<std::uint32_t>(unescaped[index + 1]) << 8U) |
                             static_cast<std::uint32_t>(unescaped[index]);
      append_utf8(decoded, codepoint);
    }
    return decoded;
  }
  return std::string(unescaped.begin(), unescaped.end());
}

std::string normalize_photoshop_text(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto ch = text[index];
    if (ch == '\r') {
      normalized.push_back('\n');
      if (index + 1 < text.size() && text[index + 1] == '\n') {
        ++index;
      }
      continue;
    }
    if (ch == '\n' || ch == '\x03') {
      normalized.push_back('\n');
      continue;
    }
    normalized.push_back(ch);
  }
  while (!normalized.empty() && normalized.back() == '\n') {
    normalized.pop_back();
  }
  return normalized;
}

}  // namespace

std::optional<std::string> extract_engine_data_text(std::span<const std::uint8_t> payload) {
  constexpr std::string_view marker = "/Text";
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  auto found = std::search(begin, end, marker.begin(), marker.end());
  while (found != end) {
    auto cursor = found + static_cast<std::ptrdiff_t>(marker.size());
    while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
      ++cursor;
    }
    if (cursor < end && *cursor == '(') {
      ++cursor;
      const auto text_begin = cursor;
      int depth = 1;
      bool escaped = false;
      while (cursor < end && depth > 0) {
        const auto ch = *cursor;
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '(') {
          ++depth;
        } else if (ch == ')') {
          --depth;
          if (depth == 0) {
            break;
          }
        }
        ++cursor;
      }
      if (cursor > text_begin) {
        auto text =
            decode_engine_string(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text_begin),
                                                               static_cast<std::size_t>(cursor - text_begin)));
        text = normalize_photoshop_text(text);
        if (!text.empty()) {
          return text;
        }
      }
    }
    found = std::search(cursor, end, marker.begin(), marker.end());
  }
  return std::nullopt;
}

std::optional<int> extract_engine_data_font_size(std::span<const std::uint8_t> payload) {
  constexpr std::string_view marker = "/FontSize";
  const auto begin = reinterpret_cast<const char*>(payload.data());
  const auto end = begin + payload.size();
  auto found = std::search(begin, end, marker.begin(), marker.end());
  while (found != end) {
    auto cursor = found + static_cast<std::ptrdiff_t>(marker.size());
    while (cursor < end &&
           (std::isspace(static_cast<unsigned char>(*cursor)) != 0 || *cursor == '[' || *cursor == '(')) {
      ++cursor;
    }
    if (cursor < end) {
      const auto remaining = static_cast<std::size_t>(end - cursor);
      const std::string number(cursor, cursor + std::min<std::size_t>(remaining, 48U));
      char* parsed_end = nullptr;
      const auto parsed = std::strtod(number.c_str(), &parsed_end);
      if (parsed_end != number.c_str() && std::isfinite(parsed) && parsed > 0.0) {
        return std::clamp(static_cast<int>(std::lround(parsed)), 1, kMaxTextSizePixels);
      }
    }
    found = std::search(cursor, end, marker.begin(), marker.end());
  }
  return std::nullopt;
}

namespace {

std::optional<double> first_engine_number_after(std::string_view text, std::string_view marker) {
  const auto found = text.find(marker);
  if (found == std::string_view::npos) {
    return std::nullopt;
  }

  auto cursor = found + marker.size();
  while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
  if (cursor >= text.size()) {
    return std::nullopt;
  }

  const std::string number(text.substr(cursor, std::min<std::size_t>(text.size() - cursor, 48U)));
  char* parsed_end = nullptr;
  const auto parsed = std::strtod(number.c_str(), &parsed_end);
  if (parsed_end == number.c_str() || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<double> parse_engine_number_array(std::string_view text) {
  std::vector<double> numbers;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == ',')) {
      ++cursor;
    }
    if (cursor >= text.size()) {
      break;
    }

    const std::string number(text.substr(cursor, std::min<std::size_t>(text.size() - cursor, 48U)));
    char* parsed_end = nullptr;
    const auto parsed = std::strtod(number.c_str(), &parsed_end);
    if (parsed_end == number.c_str()) {
      ++cursor;
      continue;
    }
    if (std::isfinite(parsed)) {
      numbers.push_back(parsed);
    }
    cursor += static_cast<std::size_t>(parsed_end - number.c_str());
  }
  return numbers;
}

std::uint8_t engine_color_component(double value, bool normalized) {
  const auto scaled = normalized ? value * 255.0 : value;
  return static_cast<std::uint8_t>(std::clamp(std::lround(scaled), 0L, 255L));
}

// Engine-data color /Type 1 is [alpha, red, green, blue]; /Type 2 (CMYK-mode documents)
// is [alpha, cyan, magenta, yellow, black] as 0-1 ink fractions.
std::optional<RgbColor> rgb_color_from_engine_values(int type, const std::vector<double>& values,
                                                     const CmykColorConverter& cmyk) {
  if (type == 2) {
    if (values.size() < 5U ||
        std::any_of(values.begin() + 1, values.begin() + 5, [](double value) { return !std::isfinite(value); })) {
      return std::nullopt;
    }
    return cmyk.rgb_from_ink(values[1], values[2], values[3], values[4]);
  }
  if (values.size() < 4U) {
    return std::nullopt;
  }

  const auto red = values[1];
  const auto green = values[2];
  const auto blue = values[3];
  if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
    return std::nullopt;
  }

  const auto normalized = red <= 1.0 && green <= 1.0 && blue <= 1.0;
  return RgbColor{engine_color_component(red, normalized), engine_color_component(green, normalized),
                  engine_color_component(blue, normalized)};
}

}  // namespace

std::optional<RgbColor> extract_engine_data_fill_color(std::span<const std::uint8_t> payload,
                                                       const CmykColorConverter& cmyk) {
  constexpr std::string_view marker = "/FillColor";
  constexpr std::string_view values_marker = "/Values";
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());

  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    const auto block_start = found + marker.size();
    const auto block_close = text.find(">>", block_start);
    const auto block = block_close == std::string_view::npos
                           ? text.substr(found)
                           : text.substr(found, block_close + 2U - found);
    const auto type = first_engine_number_after(block, "/Type");
    const auto type_value = type.has_value() ? static_cast<int>(std::lround(*type)) : 1;
    if (type_value != 1 && type_value != 2) {
      found = text.find(marker, block_start);
      continue;
    }

    const auto values = block.find(values_marker);
    if (values != std::string_view::npos) {
      const auto open = block.find('[', values + values_marker.size());
      const auto close = open == std::string_view::npos ? std::string_view::npos : block.find(']', open + 1U);
      if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        if (auto color = rgb_color_from_engine_values(
                type_value, parse_engine_number_array(block.substr(open + 1U, close - open - 1U)), cmyk);
            color.has_value()) {
          return color;
        }
      }
    }

    found = text.find(marker, block_start);
  }
  return std::nullopt;
}

std::optional<int> extract_engine_data_anti_alias(std::span<const std::uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  if (const auto value = first_engine_number_after(text, "/AntiAlias"); value.has_value()) {
    return std::clamp(static_cast<int>(std::lround(*value)), 0, 16);
  }
  return std::nullopt;
}

std::string rgb_hex_color(RgbColor color) {
  constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string result = "#000000";
  result[1] = digits[color.red >> 4U];
  result[2] = digits[color.red & 0x0FU];
  result[3] = digits[color.green >> 4U];
  result[4] = digits[color.green & 0x0FU];
  result[5] = digits[color.blue >> 4U];
  result[6] = digits[color.blue & 0x0FU];
  return result;
}

namespace {

int hex_digit_value(char ch) noexcept {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

}  // namespace

std::optional<RgbColor> rgb_color_from_hex(std::string_view text) {
  if (text.size() != 7U || text[0] != '#') {
    return std::nullopt;
  }
  const auto pair_value = [](char high, char low) -> std::optional<std::uint8_t> {
    const auto hi = hex_digit_value(high);
    const auto lo = hex_digit_value(low);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>((hi << 4) | lo);
  };
  const auto red = pair_value(text[1], text[2]);
  const auto green = pair_value(text[3], text[4]);
  const auto blue = pair_value(text[5], text[6]);
  if (!red.has_value() || !green.has_value() || !blue.has_value()) {
    return std::nullopt;
  }
  return RgbColor{*red, *green, *blue};
}

std::string percent_decode(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '%' && index + 2 < text.size()) {
      const auto hi = hex_digit_value(text[index + 1]);
      const auto lo = hex_digit_value(text[index + 2]);
      if (hi >= 0 && lo >= 0) {
        decoded.push_back(static_cast<char>((hi << 4) | lo));
        index += 2;
        continue;
      }
    }
    decoded.push_back(text[index]);
  }
  return decoded;
}

namespace {

std::string percent_encode(std::string_view text) {
  constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string encoded;
  encoded.reserve(text.size());
  for (const auto byte : text) {
    const auto value = static_cast<unsigned char>(byte);
    if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
        value == '-' || value == '_' || value == '.' || value == '~') {
      encoded.push_back(static_cast<char>(value));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(digits[value >> 4U]);
    encoded.push_back(digits[value & 0x0FU]);
  }
  return encoded;
}

std::optional<double> engine_number_after_key(std::string_view text, std::string_view marker) {
  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    auto cursor = found + marker.size();
    if (cursor < text.size() &&
        (std::isalnum(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '_' || text[cursor] == '-')) {
      found = text.find(marker, cursor);
      continue;
    }
    while (cursor < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '[' || text[cursor] == '(')) {
      ++cursor;
    }
    if (cursor < text.size()) {
      const std::string number(text.substr(cursor, std::min<std::size_t>(text.size() - cursor, 48U)));
      char* parsed_end = nullptr;
      const auto parsed = std::strtod(number.c_str(), &parsed_end);
      if (parsed_end != number.c_str() && std::isfinite(parsed)) {
        return parsed;
      }
    }
    found = text.find(marker, cursor);
  }
  return std::nullopt;
}

bool engine_bool_after_key(std::string_view text, std::string_view marker, bool fallback = false) {
  const auto found = text.find(marker);
  if (found == std::string_view::npos) {
    return fallback;
  }
  auto cursor = found + marker.size();
  while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
  if (text.substr(cursor, 4) == "true") {
    return true;
  }
  if (text.substr(cursor, 5) == "false") {
    return false;
  }
  return fallback;
}

std::optional<std::pair<std::size_t, std::size_t>> balanced_range_after(std::string_view text,
                                                                        std::string_view marker,
                                                                        char open, char close) {
  const auto marker_pos = text.find(marker);
  if (marker_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto open_pos = text.find(open, marker_pos + marker.size());
  if (open_pos == std::string_view::npos) {
    return std::nullopt;
  }
  int depth = 0;
  bool escaped = false;
  for (std::size_t index = open_pos; index < text.size(); ++index) {
    const auto ch = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == open) {
      ++depth;
    } else if (ch == close) {
      --depth;
      if (depth == 0) {
        return std::pair{open_pos + 1U, index};
      }
    }
  }
  return std::nullopt;
}

std::vector<std::string_view> engine_dictionary_ranges(std::string_view text) {
  std::vector<std::string_view> ranges;
  std::size_t cursor = 0;
  while (cursor + 1 < text.size()) {
    const auto start = text.find("<<", cursor);
    if (start == std::string_view::npos) {
      break;
    }
    int depth = 0;
    for (std::size_t index = start; index + 1 < text.size(); ++index) {
      if (text[index] == '<' && text[index + 1] == '<') {
        ++depth;
        ++index;
        continue;
      }
      if (text[index] == '>' && text[index + 1] == '>') {
        --depth;
        ++index;
        if (depth == 0) {
          ranges.push_back(text.substr(start, index + 1U - start));
          cursor = index + 1U;
          break;
        }
      }
    }
    if (cursor <= start) {
      break;
    }
  }
  return ranges;
}

std::optional<std::vector<std::uint8_t>> engine_parenthesized_bytes_after(std::string_view text,
                                                                          std::string_view marker) {
  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    auto cursor = found + marker.size();
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    if (cursor < text.size() && text[cursor] == '(') {
      ++cursor;
      const auto begin = cursor;
      int depth = 1;
      bool escaped = false;
      while (cursor < text.size() && depth > 0) {
        const auto ch = text[cursor];
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '(') {
          ++depth;
        } else if (ch == ')') {
          --depth;
          if (depth == 0) {
            break;
          }
        }
        ++cursor;
      }
      if (cursor > begin) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data() + begin);
        return std::vector<std::uint8_t>(data, data + (cursor - begin));
      }
    }
    found = text.find(marker, cursor);
  }
  return std::nullopt;
}

std::optional<RgbColor> extract_engine_fill_color_from_text(std::string_view text,
                                                            const CmykColorConverter& cmyk) {
  constexpr std::string_view marker = "/FillColor";
  constexpr std::string_view values_marker = "/Values";
  auto found = text.find(marker);
  while (found != std::string_view::npos) {
    const auto block_start = found + marker.size();
    const auto block_close = text.find(">>", block_start);
    const auto block = block_close == std::string_view::npos ? text.substr(found)
                                                             : text.substr(found, block_close + 2U - found);
    const auto type = engine_number_after_key(block, "/Type");
    const auto type_value = type.has_value() ? static_cast<int>(std::lround(*type)) : 1;
    if (type_value != 1 && type_value != 2) {
      found = text.find(marker, block_start);
      continue;
    }

    const auto values = block.find(values_marker);
    if (values != std::string_view::npos) {
      const auto open = block.find('[', values + values_marker.size());
      const auto close = open == std::string_view::npos ? std::string_view::npos : block.find(']', open + 1U);
      if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        if (auto color = rgb_color_from_engine_values(
                type_value, parse_engine_number_array(block.substr(open + 1U, close - open - 1U)), cmyk);
            color.has_value()) {
          return color;
        }
      }
    }
    found = text.find(marker, block_start);
  }
  return std::nullopt;
}

std::vector<std::string> extract_engine_font_names(std::span<const std::uint8_t> payload) {
  std::vector<std::string> fonts;
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto range = balanced_range_after(text, "/FontSet", '[', ']');
  if (!range.has_value()) {
    return fonts;
  }
  const auto block = text.substr(range->first, range->second - range->first);
  for (const auto dictionary : engine_dictionary_ranges(block)) {
    auto bytes = engine_parenthesized_bytes_after(dictionary, "/Name");
    if (!bytes.has_value()) {
      continue;
    }
    auto decoded = decode_engine_string(*bytes);
    if (!decoded.empty()) {
      fonts.push_back(std::move(decoded));
    }
  }
  return fonts;
}

struct ResolvedPhotoshopFont {
  std::string family{"Arial"};
  bool bold{false};
  bool italic{false};
};

std::string compact_font_key(std::string_view value) {
  std::string compact;
  compact.reserve(value.size());
  for (const auto ch : ascii_lower_copy(std::string(value))) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      compact.push_back(ch);
    }
  }
  return compact;
}

std::string compact_font_key_with_bt_suffix(std::string key) {
  const auto pos = key.find("bt");
  if (pos == std::string::npos || pos + 2U >= key.size()) {
    return key;
  }
  key.erase(pos, 2U);
  key += "bt";
  return key;
}

bool font_names_match(std::string_view lhs, std::string_view rhs) {
  const auto lhs_compact = compact_font_key(lhs);
  const auto rhs_compact = compact_font_key(rhs);
  return ascii_lower_copy(std::string(lhs)) == ascii_lower_copy(std::string(rhs)) ||
         lhs_compact == rhs_compact ||
         compact_font_key_with_bt_suffix(lhs_compact) == compact_font_key_with_bt_suffix(rhs_compact);
}

bool strip_ascii_ci_suffix(std::string& value, std::string_view suffix) {
  if (value.size() < suffix.size()) {
    return false;
  }
  const auto tail = std::string_view(value).substr(value.size() - suffix.size());
  if (ascii_lower_copy(std::string(tail)) != std::string(suffix)) {
    return false;
  }
  value.resize(value.size() - suffix.size());
  return true;
}

std::string humanized_postscript_family_name(std::string value) {
  if (value.empty() || value.find(' ') != std::string::npos) {
    return value;
  }

  std::string humanized;
  humanized.reserve(value.size() + 4U);
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto ch = value[index];
    if (index > 0U && std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      const auto previous = value[index - 1U];
      const auto next = index + 1U < value.size() ? value[index + 1U] : '\0';
      const bool lower_to_upper = std::islower(static_cast<unsigned char>(previous)) != 0 &&
                                  std::isupper(static_cast<unsigned char>(ch)) != 0;
      const bool acronym_to_word = std::isupper(static_cast<unsigned char>(previous)) != 0 &&
                                   std::isupper(static_cast<unsigned char>(ch)) != 0 &&
                                   std::islower(static_cast<unsigned char>(next)) != 0;
      const bool alpha_digit_boundary =
          (std::isalpha(static_cast<unsigned char>(previous)) != 0 && std::isdigit(static_cast<unsigned char>(ch)) != 0) ||
          (std::isdigit(static_cast<unsigned char>(previous)) != 0 && std::isalpha(static_cast<unsigned char>(ch)) != 0);
      if (lower_to_upper || acronym_to_word || alpha_digit_boundary) {
        humanized.push_back(' ');
      }
    }
    const auto output = (ch == '_' || ch == '-') ? ' ' : ch;
    if (output == ' ' && (humanized.empty() || humanized.back() == ' ')) {
      continue;
    }
    humanized.push_back(output);
  }
  return humanized;
}

ResolvedPhotoshopFont heuristic_resolved_photoshop_font(std::string_view font_name) {
  ResolvedPhotoshopFont resolved;
  resolved.family = font_name.empty() ? std::string("Arial") : std::string(font_name);
  struct StyleSuffix {
    std::string_view suffix;
    bool bold;
    bool italic;
  };
  static constexpr std::array<StyleSuffix, 25> kStyleSuffixes = {{
      {"-bolditalicmt", true, true},
      {"-boldobliquemt", true, true},
      {"-bolditalic", true, true},
      {"-boldoblique", true, true},
      {"-boldital", true, true},
      {"-boldit", true, true},
      {"-semibolditalic", true, true},
      {"-demibolditalic", true, true},
      {"-blackitalic", true, true},
      {"-heavyitalic", true, true},
      {"-extrabold", true, false},
      {"-ultrabold", true, false},
      {"-semibold", true, false},
      {"-demibold", true, false},
      {"-boldmt", true, false},
      {"-bold", true, false},
      {"-black", true, false},
      {"-heavy", true, false},
      {"-italicmt", false, true},
      {"-obliquemt", false, true},
      {"-italic", false, true},
      {"-oblique", false, true},
      {"-ital", false, true},
      {"-it", false, true},
      {"-regular", false, false},
  }};

  bool stripped = true;
  while (stripped) {
    stripped = false;
    for (const auto suffix : kStyleSuffixes) {
      if (strip_ascii_ci_suffix(resolved.family, suffix.suffix)) {
        resolved.bold = resolved.bold || suffix.bold;
        resolved.italic = resolved.italic || suffix.italic;
        stripped = true;
        break;
      }
    }
  }
  (void)strip_ascii_ci_suffix(resolved.family, "-roman");
  (void)strip_ascii_ci_suffix(resolved.family, "mt");
  (void)strip_ascii_ci_suffix(resolved.family, "ps");
  while (!resolved.family.empty() && (resolved.family.back() == '-' || resolved.family.back() == '_' ||
                                      std::isspace(static_cast<unsigned char>(resolved.family.back())) != 0)) {
    resolved.family.pop_back();
  }
  if (resolved.family.empty()) {
    resolved.family = font_name.empty() ? std::string("Arial") : std::string(font_name);
  } else {
    resolved.family = humanized_postscript_family_name(std::move(resolved.family));
  }
  return resolved;
}

}  // namespace

#ifdef _WIN32

std::optional<std::wstring> directwrite_localized_string(IDWriteLocalizedStrings* strings) {
  if (strings == nullptr || strings->GetCount() == 0) {
    return std::nullopt;
  }
  UINT32 index = 0;
  BOOL exists = FALSE;
  if (FAILED(strings->FindLocaleName(L"en-us", &index, &exists)) || !exists) {
    index = 0;
  }
  UINT32 length = 0;
  if (FAILED(strings->GetStringLength(index, &length))) {
    return std::nullopt;
  }
  std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
  if (FAILED(strings->GetString(index, value.data(), length + 1U))) {
    return std::nullopt;
  }
  value.resize(length);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> directwrite_font_info_string(IDWriteFont* font, DWRITE_INFORMATIONAL_STRING_ID id) {
  if (font == nullptr) {
    return std::nullopt;
  }
  Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> strings;
  BOOL exists = FALSE;
  if (FAILED(font->GetInformationalStrings(id, &strings, &exists)) || !exists || !strings) {
    return std::nullopt;
  }
  const auto value = directwrite_localized_string(strings.Get());
  if (!value.has_value()) {
    return std::nullopt;
  }
  auto utf8 = utf8_from_wide(*value);
  if (utf8.empty()) {
    return std::nullopt;
  }
  return utf8;
}

namespace {

std::optional<ResolvedPhotoshopFont> registry_resolved_photoshop_font(std::string_view font_name);

std::optional<ResolvedPhotoshopFont> directwrite_resolved_photoshop_font(std::string_view font_name) {
  const auto wide_name = wide_from_utf8(font_name);
  if (wide_name.empty()) {
    return std::nullopt;
  }

  Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
    return std::nullopt;
  }
  Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection)) || !collection) {
    return std::nullopt;
  }

  const auto family_count = collection->GetFontFamilyCount();
  for (UINT32 family_index = 0; family_index < family_count; ++family_index) {
    Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
    if (FAILED(collection->GetFontFamily(family_index, &font_family)) || !font_family) {
      continue;
    }
    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> family_names;
    if (FAILED(font_family->GetFamilyNames(&family_names)) || !family_names) {
      continue;
    }
    const auto localized_family = directwrite_localized_string(family_names.Get());
    if (!localized_family.has_value()) {
      continue;
    }
    auto family = utf8_from_wide(*localized_family);
    if (family.empty()) {
      continue;
    }

    const auto font_count = font_family->GetFontCount();
    for (UINT32 font_index = 0; font_index < font_count; ++font_index) {
      Microsoft::WRL::ComPtr<IDWriteFont> font;
      if (FAILED(font_family->GetFont(font_index, &font)) || !font) {
        continue;
      }
      std::vector<std::string> candidates;
      if (const auto postscript = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
          postscript.has_value()) {
        candidates.push_back(*postscript);
      }
      if (const auto full_name = directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_FULL_NAME);
          full_name.has_value()) {
        candidates.push_back(*full_name);
      }
      Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> face_names;
      if (SUCCEEDED(font->GetFaceNames(&face_names)) && face_names) {
        if (const auto face = directwrite_localized_string(face_names.Get()); face.has_value()) {
          auto face_utf8 = utf8_from_wide(*face);
          if (!face_utf8.empty()) {
            candidates.push_back(family + ' ' + face_utf8);
            candidates.push_back(family + '-' + face_utf8);
          }
        }
      }
      if (std::any_of(candidates.begin(), candidates.end(), [font_name](const std::string& candidate) {
            return font_names_match(candidate, font_name);
          })) {
        // Black/Heavy faces (weight >= 800) keep their full face name: the renderer's
        // family+style matcher then finds the real face ("Arial Black" -> family "Arial",
        // style "Black") instead of flattening it to the Bold face (~15% narrower glyphs on
        // the SNES box blurb). The bold flag stays set so an uninstalled face still falls
        // back to Bold exactly as before.
        if (font->GetWeight() >= DWRITE_FONT_WEIGHT_EXTRA_BOLD) {
          if (const auto full_name =
                  directwrite_font_info_string(font.Get(), DWRITE_INFORMATIONAL_STRING_FULL_NAME);
              full_name.has_value() && !full_name->empty() && *full_name != family) {
            return ResolvedPhotoshopFont{*full_name, true, font->GetStyle() != DWRITE_FONT_STYLE_NORMAL};
          }
        }
        return ResolvedPhotoshopFont{std::move(family), font->GetWeight() >= DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     font->GetStyle() != DWRITE_FONT_STYLE_NORMAL};
      }
    }
  }
  return std::nullopt;
}

std::string trim_ascii_whitespace(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  if (first > 0U) {
    value.erase(0, first);
  }
  return value;
}

std::string registry_font_family_from_value_name(std::wstring_view value_name) {
  auto family = trim_ascii_whitespace(utf8_from_wide(value_name));
  const auto suffix = family.find(" (");
  if (suffix != std::string::npos && !family.empty() && family.back() == ')') {
    family.resize(suffix);
    family = trim_ascii_whitespace(std::move(family));
  }
  return family;
}

void append_registry_font_families(HKEY root, const wchar_t* subkey, std::vector<std::string>& families) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS || key == nullptr) {
    return;
  }

  DWORD index = 0;
  std::wstring value_name(512, L'\0');
  while (true) {
    DWORD value_name_length = static_cast<DWORD>(value_name.size());
    const auto result =
        RegEnumValueW(key, index, value_name.data(), &value_name_length, nullptr, nullptr, nullptr, nullptr);
    if (result == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (result == ERROR_MORE_DATA) {
      value_name.resize(value_name.size() * 2U);
      continue;
    }
    if (result == ERROR_SUCCESS) {
      auto family = registry_font_family_from_value_name(std::wstring_view(value_name.data(), value_name_length));
      if (!family.empty()) {
        families.push_back(std::move(family));
      }
    }
    ++index;
  }
  RegCloseKey(key);
}

std::optional<ResolvedPhotoshopFont> registry_resolved_photoshop_font(std::string_view font_name) {
  if (font_name.empty()) {
    return std::nullopt;
  }

  const auto heuristic = heuristic_resolved_photoshop_font(font_name);
  const std::array<std::string_view, 2> targets{font_name, heuristic.family};
  std::vector<std::string> families;
  append_registry_font_families(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                families);
  append_registry_font_families(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                families);
  for (auto& family : families) {
    const bool matches = std::any_of(targets.begin(), targets.end(), [&family](std::string_view target) {
      return !target.empty() && font_names_match(family, target);
    });
    if (matches) {
      return ResolvedPhotoshopFont{std::move(family), heuristic.bold, heuristic.italic};
    }
  }
  return std::nullopt;
}

}  // namespace

#endif

namespace {

ResolvedPhotoshopFont resolve_photoshop_font_name(std::string_view font_name) {
#ifdef _WIN32
  if (const auto resolved = directwrite_resolved_photoshop_font(font_name); resolved.has_value()) {
    return *resolved;
  }
  if (const auto resolved = registry_resolved_photoshop_font(font_name); resolved.has_value()) {
    return *resolved;
  }
#endif
  return heuristic_resolved_photoshop_font(font_name);
}

// Auto-leading fraction from the normal paragraph sheet inside a ResourceDict (or the full
// engine text); Photoshop's default is 1.2 (auto leading = 1.2 x font size).
double engine_normal_paragraph_auto_leading_fraction(std::string_view resources) {
  const auto set_range = balanced_range_after(resources, "/ParagraphSheetSet", '[', ']');
  if (!set_range.has_value()) {
    return 1.2;
  }
  const auto sheets =
      engine_dictionary_ranges(resources.substr(set_range->first, set_range->second - set_range->first));
  auto index = 0;
  if (const auto value = engine_number_after_key(resources, "/TheNormalParagraphSheet");
      value.has_value() && std::isfinite(*value) && *value >= 0.0 && *value <= 255.0) {
    index = static_cast<int>(std::lround(*value));
  }
  if (sheets.empty() || static_cast<std::size_t>(index) >= sheets.size()) {
    return 1.2;
  }
  const auto fraction = engine_number_after_key(sheets[static_cast<std::size_t>(index)], "/AutoLeading");
  if (fraction.has_value() && std::isfinite(*fraction) && *fraction > 0.01 && *fraction < 10.0) {
    return *fraction;
  }
  return 1.2;
}

// Parse the ResourceDict's normal style/paragraph sheets (see PsdTextEngineDefaults). The
// fallbacks passed in are used when the engine data has no parsable ResourceDict (hand-built
// engine data in old files and tests).
PsdTextEngineDefaults extract_engine_text_defaults(std::span<const std::uint8_t> payload,
                                                   double fallback_size,
                                                   const CmykColorConverter& cmyk) {
  PsdTextEngineDefaults defaults;
  defaults.font_size = fallback_size;
  const std::string_view engine(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto resource_pos = engine.find("/ResourceDict");
  if (resource_pos == std::string_view::npos) {
    return defaults;
  }
  const auto resources = engine.substr(resource_pos);
  const auto sheet_index = [](std::optional<double> value) {
    if (!value.has_value() || !std::isfinite(*value) || *value < 0.0 || *value > 255.0) {
      return 0;
    }
    return static_cast<int>(std::lround(*value));
  };
  if (const auto set_range = balanced_range_after(resources, "/StyleSheetSet", '[', ']');
      set_range.has_value()) {
    const auto sheets =
        engine_dictionary_ranges(resources.substr(set_range->first, set_range->second - set_range->first));
    const auto index = sheet_index(engine_number_after_key(resources, "/TheNormalStyleSheet"));
    if (!sheets.empty() && static_cast<std::size_t>(index) < sheets.size()) {
      const auto sheet = sheets[static_cast<std::size_t>(index)];
      if (const auto size = engine_number_after_key(sheet, "/FontSize");
          size.has_value() && std::isfinite(*size) && *size > 0.0) {
        defaults.font_size = *size;
      }
      defaults.auto_leading = engine_bool_after_key(sheet, "/AutoLeading", true);
      if (const auto leading = engine_number_after_key(sheet, "/Leading");
          leading.has_value() && std::isfinite(*leading) && *leading > 0.0) {
        defaults.leading = *leading;
      }
      if (const auto tracking = engine_number_after_key(sheet, "/Tracking");
          tracking.has_value() && std::isfinite(*tracking)) {
        defaults.tracking = *tracking;
      }
      if (const auto scale = engine_number_after_key(sheet, "/HorizontalScale");
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        defaults.horizontal_scale = *scale;
      }
      if (const auto scale = engine_number_after_key(sheet, "/VerticalScale");
          scale.has_value() && std::isfinite(*scale) && *scale > 0.01 && *scale < 100.0) {
        defaults.vertical_scale = *scale;
      }
      if (const auto font_index = engine_number_after_key(sheet, "/Font"); font_index.has_value()) {
        defaults.font_index = static_cast<int>(std::lround(*font_index));
      }
      defaults.faux_bold = engine_bool_after_key(sheet, "/FauxBold");
      defaults.faux_italic = engine_bool_after_key(sheet, "/FauxItalic");
      defaults.fill_color = extract_engine_fill_color_from_text(sheet, cmyk);
    }
  }
  defaults.auto_leading_fraction = engine_normal_paragraph_auto_leading_fraction(resources);
  return defaults;
}

}  // namespace

std::optional<std::vector<PsdTextStyleRun>> extract_engine_text_runs(std::span<const std::uint8_t> payload,
                                                                     std::string_view text,
                                                                     int fallback_size,
                                                                     RgbColor fallback_color,
                                                                     const CmykColorConverter& cmyk) {
  const std::string_view engine(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto style_pos = engine.find("/StyleRun");
  if (style_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto style_block = engine.substr(style_pos);
  const auto run_lengths_range = balanced_range_after(style_block, "/RunLengthArray", '[', ']');
  const auto run_array_range = balanced_range_after(style_block, "/RunArray", '[', ']');
  if (!run_lengths_range.has_value() || !run_array_range.has_value()) {
    return std::nullopt;
  }

  const auto length_values = parse_engine_number_array(
      style_block.substr(run_lengths_range->first, run_lengths_range->second - run_lengths_range->first));
  auto dictionaries = engine_dictionary_ranges(style_block.substr(run_array_range->first,
                                                                  run_array_range->second - run_array_range->first));
  if (length_values.empty() || dictionaries.empty()) {
    return std::nullopt;
  }

  const auto font_names = extract_engine_font_names(payload);
  const auto defaults = extract_engine_text_defaults(payload, static_cast<double>(fallback_size), cmyk);
  const auto text_utf16_length = static_cast<int>(utf8_to_utf16(text).size());
  std::vector<PsdTextStyleRun> runs;
  runs.reserve(std::min(length_values.size(), dictionaries.size()));
  int start = 0;
  for (std::size_t index = 0; index < length_values.size() && index < dictionaries.size(); ++index) {
    const auto length = std::max(0, static_cast<int>(std::lround(length_values[index])));
    if (length <= 0) {
      continue;
    }
    if (start >= text_utf16_length) {
      break;
    }
    PsdTextStyleRun run;
    run.start = start;
    run.length = std::min(length, text_utf16_length - start);
    run.size = std::clamp(engine_number_after_key(dictionaries[index], "/FontSize").value_or(defaults.font_size),
                          1.0, static_cast<double>(kMaxTextSizePixels));
    run.color = extract_engine_fill_color_from_text(dictionaries[index], cmyk)
                    .value_or(defaults.fill_color.value_or(fallback_color));
    const auto faux_bold = engine_bool_after_key(dictionaries[index], "/FauxBold", defaults.faux_bold);
    const auto faux_italic = engine_bool_after_key(dictionaries[index], "/FauxItalic", defaults.faux_italic);
    run.bold = faux_bold;
    run.italic = faux_italic;
    run.auto_leading = engine_bool_after_key(dictionaries[index], "/AutoLeading", defaults.auto_leading);
    // Photoshop records a stale /Leading value even for auto-leading runs; only a fixed
    // (non-auto) run's leading participates in layout.
    if (!run.auto_leading) {
      if (const auto leading = engine_number_after_key(dictionaries[index], "/Leading").value_or(defaults.leading);
          std::isfinite(leading) && leading > 0.0) {
        run.leading = leading;
      }
    }
    if (const auto tracking = engine_number_after_key(dictionaries[index], "/Tracking").value_or(defaults.tracking);
        std::isfinite(tracking) && std::abs(tracking) < 10000.0) {
      run.tracking = tracking;
    }
    if (const auto scale =
            engine_number_after_key(dictionaries[index], "/HorizontalScale").value_or(defaults.horizontal_scale);
        std::isfinite(scale) && scale > 0.01 && scale < 100.0) {
      run.horizontal_scale = scale;
    }
    if (const auto scale =
            engine_number_after_key(dictionaries[index], "/VerticalScale").value_or(defaults.vertical_scale);
        std::isfinite(scale) && scale > 0.01 && scale < 100.0) {
      run.vertical_scale = scale;
    }
    const auto font_index = engine_number_after_key(dictionaries[index], "/Font");
    const auto font = font_index.has_value() ? static_cast<int>(std::lround(*font_index))
                                             : defaults.font_index.value_or(-1);
    if (font >= 0 && static_cast<std::size_t>(font) < font_names.size()) {
      const auto resolved = resolve_photoshop_font_name(font_names[static_cast<std::size_t>(font)]);
      run.family = resolved.family;
      run.bold = run.bold || resolved.bold;
      run.italic = run.italic || resolved.italic;
    }
    if (run.family.empty()) {
      run.family = "Arial";
    }
    runs.push_back(std::move(run));
    start += length;
  }

  if (runs.empty()) {
    return std::nullopt;
  }
  return runs;
}

std::optional<std::vector<PsdTextParagraphRun>> extract_engine_paragraph_runs(std::span<const std::uint8_t> payload,
                                                                              std::string_view text) {
  const std::string_view engine(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto paragraph_pos = engine.find("/ParagraphRun");
  if (paragraph_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto paragraph_block = engine.substr(paragraph_pos);
  const auto run_lengths_range = balanced_range_after(paragraph_block, "/RunLengthArray", '[', ']');
  const auto run_array_range = balanced_range_after(paragraph_block, "/RunArray", '[', ']');
  if (!run_lengths_range.has_value() || !run_array_range.has_value()) {
    return std::nullopt;
  }

  const auto length_values = parse_engine_number_array(
      paragraph_block.substr(run_lengths_range->first, run_lengths_range->second - run_lengths_range->first));
  auto dictionaries = engine_dictionary_ranges(paragraph_block.substr(run_array_range->first,
                                                                      run_array_range->second - run_array_range->first));
  if (length_values.empty() || dictionaries.empty()) {
    return std::nullopt;
  }

  const auto text_utf16_length = static_cast<int>(utf8_to_utf16(text).size());
  const auto default_auto_leading_fraction = [&engine] {
    const auto resource_pos = engine.find("/ResourceDict");
    return engine_normal_paragraph_auto_leading_fraction(
        resource_pos == std::string_view::npos ? std::string_view{} : engine.substr(resource_pos));
  }();
  std::vector<PsdTextParagraphRun> runs;
  int start = 0;
  for (std::size_t index = 0; index < length_values.size() && index < dictionaries.size(); ++index) {
    const auto length = std::max(0, static_cast<int>(std::lround(length_values[index])));
    if (length <= 0) {
      continue;
    }
    if (start >= text_utf16_length) {
      break;
    }
    PsdTextParagraphRun run;
    run.start = start;
    run.length = std::min(length, text_utf16_length - start);
    run.justification =
        std::clamp(static_cast<int>(std::lround(engine_number_after_key(dictionaries[index], "/Justification").value_or(0.0))),
                   0, 3);
    run.first_line_indent = engine_number_after_key(dictionaries[index], "/FirstLineIndent").value_or(0.0);
    run.start_indent = engine_number_after_key(dictionaries[index], "/StartIndent").value_or(0.0);
    run.end_indent = engine_number_after_key(dictionaries[index], "/EndIndent").value_or(0.0);
    run.space_before = engine_number_after_key(dictionaries[index], "/SpaceBefore").value_or(0.0);
    run.space_after = engine_number_after_key(dictionaries[index], "/SpaceAfter").value_or(0.0);
    run.auto_leading_fraction = default_auto_leading_fraction;
    if (const auto fraction = engine_number_after_key(dictionaries[index], "/AutoLeading");
        fraction.has_value() && std::isfinite(*fraction) && *fraction > 0.01 && *fraction < 10.0) {
      run.auto_leading_fraction = *fraction;
    }
    runs.push_back(run);
    start += length;
  }

  if (runs.empty()) {
    return std::nullopt;
  }
  return runs;
}

namespace {

bool text_run_size_is_integral(const PsdTextStyleRun& run) {
  return std::abs(run.size - std::round(run.size)) < 0.0001;
}

}  // namespace

// v1: start len size bold italic color family (int size, no leading)
// v2: v1 + fixed leading (double)
// v3: v2 with double size, leading may be the literal "auto" (auto leading: paragraph
//     auto-leading fraction x size), + tracking (Photoshop 1/1000-em units), + the character
//     panel's horizontal/vertical glyph scales (fractions, 1.0 = none).
std::string serialize_patchy_text_runs(std::span<const PsdTextStyleRun> runs) {
  const bool include_leading = std::any_of(runs.begin(), runs.end(), [](const PsdTextStyleRun& run) {
    return run.leading.has_value() && std::isfinite(*run.leading) && *run.leading > 0.0;
  });
  const bool photoshop_layout = std::any_of(runs.begin(), runs.end(), [](const PsdTextStyleRun& run) {
    return run.auto_leading || std::abs(run.tracking) > 0.0001 || !text_run_size_is_integral(run) ||
           std::abs(run.horizontal_scale - 1.0) > 0.0001 || std::abs(run.vertical_scale - 1.0) > 0.0001;
  });
  std::string serialized = photoshop_layout ? "v3" : (include_leading ? "v2" : "v1");
  for (const auto& run : runs) {
    serialized += '\n';
    serialized += std::to_string(run.start);
    serialized += '\t';
    serialized += std::to_string(run.length);
    serialized += '\t';
    if (photoshop_layout) {
      serialized += serialize_paragraph_metric(run.size);
    } else {
      serialized += std::to_string(static_cast<int>(std::lround(run.size)));
    }
    serialized += '\t';
    serialized += run.bold ? '1' : '0';
    serialized += '\t';
    serialized += run.italic ? '1' : '0';
    serialized += '\t';
    serialized += rgb_hex_color(run.color);
    serialized += '\t';
    serialized += percent_encode(run.family);
    if (photoshop_layout) {
      serialized += '\t';
      // A run with neither auto leading nor a usable fixed value renders auto (Photoshop
      // files always carry one or the other).
      if (run.auto_leading || !run.leading.has_value()) {
        serialized += "auto";
      } else {
        serialized += serialize_paragraph_metric(*run.leading);
      }
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.tracking);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.horizontal_scale);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.vertical_scale);
    } else if (include_leading) {
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.leading.value_or(0.0));
    }
  }
  return serialized;
}

namespace {

std::string patchy_alignment_name_from_justification(int justification) {
  switch (justification) {
    case 1:
      return "right";
    case 2:
      return "center";
    case 3:
      return "justify";
    default:
      return "left";
  }
}

bool paragraph_run_has_layout(const PsdTextParagraphRun& run) noexcept {
  constexpr double kEpsilon = 0.000001;
  return std::abs(run.first_line_indent) > kEpsilon || std::abs(run.start_indent) > kEpsilon ||
         std::abs(run.end_indent) > kEpsilon || std::abs(run.space_before) > kEpsilon ||
         std::abs(run.space_after) > kEpsilon;
}

}  // namespace

std::string serialize_paragraph_metric(double value) {
  if (!std::isfinite(value) || std::abs(value) < 0.000001) {
    return "0";
  }
  std::ostringstream stream;
  stream << std::setprecision(17) << value;
  return stream.str();
}

// v1: start len alignment; v2: + indent/space metrics; v3: + auto-leading fraction.
std::string serialize_patchy_paragraph_runs(std::span<const PsdTextParagraphRun> runs) {
  const bool include_fraction = std::any_of(runs.begin(), runs.end(), [](const PsdTextParagraphRun& run) {
    return std::abs(run.auto_leading_fraction - 1.2) > 0.0001;
  });
  const bool include_layout =
      include_fraction ||
      std::any_of(runs.begin(), runs.end(), [](const PsdTextParagraphRun& run) { return paragraph_run_has_layout(run); });
  std::string serialized = include_fraction ? "v3" : (include_layout ? "v2" : "v1");
  for (const auto& run : runs) {
    serialized += '\n';
    serialized += std::to_string(run.start);
    serialized += '\t';
    serialized += std::to_string(run.length);
    serialized += '\t';
    serialized += patchy_alignment_name_from_justification(run.justification);
    if (include_layout) {
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.first_line_indent);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.start_indent);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.end_indent);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.space_before);
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.space_after);
    }
    if (include_fraction) {
      serialized += '\t';
      serialized += serialize_paragraph_metric(run.auto_leading_fraction);
    }
  }
  return serialized;
}

namespace {

void append_html_escaped(std::string& output, std::uint32_t codepoint) {
  switch (codepoint) {
    case '&':
      output += "&amp;";
      return;
    case '<':
      output += "&lt;";
      return;
    case '>':
      output += "&gt;";
      return;
    case '"':
      output += "&quot;";
      return;
    case '\n':
      output += "<br />";
      return;
    default:
      append_utf8(output, codepoint);
      return;
  }
}

void append_utf8_range_as_html(std::string& output, std::string_view text, int start_units, int length_units) {
  const auto end_units = start_units + length_units;
  int utf16_position = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = 0x3FU;
    std::size_t consumed = 1;
    if (lead < 0x80U) {
      codepoint = lead;
    } else if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) {
      codepoint = ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
      consumed = 2;
    } else if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) {
      codepoint = ((lead & 0x0FU) << 12U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
      consumed = 3;
    } else if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) {
      codepoint = ((lead & 0x07U) << 18U) | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U) |
                  ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
      consumed = 4;
    }
    const auto units = codepoint > 0xFFFFU ? 2 : 1;
    if (utf16_position >= start_units && utf16_position < end_units) {
      append_html_escaped(output, codepoint);
    }
    utf16_position += units;
    index += consumed;
    if (utf16_position >= end_units) {
      break;
    }
  }
}

std::string css_escaped_family(std::string_view family) {
  std::string escaped;
  escaped.reserve(family.size());
  for (const auto ch : family) {
    if (ch == '\'' || ch == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

}  // namespace

std::string html_from_text_runs(std::string_view text, std::span<const PsdTextStyleRun> runs,
                                std::span<const PsdTextParagraphRun> paragraph_runs) {
  const auto alignment =
      paragraph_runs.empty() ? std::string("left") : patchy_alignment_name_from_justification(paragraph_runs.front().justification);
  std::string html =
      "<!DOCTYPE HTML><html><head><meta name=\"qrichtext\" content=\"1\" /></head>"
      "<body style=\"margin:0px;\"><p style=\"margin:0px; text-align:";
  html += alignment;
  html += ";\">";
  for (const auto& run : runs) {
    html += "<span style=\" font-family:'";
    html += css_escaped_family(run.family);
    html += "'; font-size:";
    html += std::to_string(std::max(1, static_cast<int>(std::lround(run.size))));
    html += "px;";
    if (run.bold) {
      html += " font-weight:700;";
    }
    if (run.italic) {
      html += " font-style:italic;";
    }
    html += " color:";
    html += rgb_hex_color(run.color);
    html += ";\">";
    append_utf8_range_as_html(html, text, run.start, run.length);
    html += "</span>";
  }
  html += "</p></body></html>";
  return html;
}

namespace {

const std::array<std::uint8_t, 7>* glyph_for(char ch) {
  static const std::map<char, std::array<std::uint8_t, 7>> glyphs = {
      {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
      {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
      {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
      {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
      {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
      {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
      {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
      {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
      {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
      {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
      {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
      {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
      {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
      {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
      {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
      {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
      {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
      {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
      {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
      {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
      {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
      {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
      {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
      {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
      {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
      {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
      {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
      {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
      {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
      {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
      {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
      {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
      {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
      {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
      {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
      {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
      {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
      {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
      {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
      {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
      {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
      {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
  };
  const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  const auto found = glyphs.find(upper);
  return found == glyphs.end() ? nullptr : &found->second;
}

}  // namespace

PixelBuffer render_placeholder_text(std::string_view text, std::int32_t width, std::int32_t height) {
  width = std::max<std::int32_t>(width, static_cast<std::int32_t>(std::min<std::size_t>(text.size(), 64) * 14U + 8U));
  height = std::max<std::int32_t>(height, 28);
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  pixels.clear(0);

  constexpr int scale = 2;
  constexpr int glyph_width = 5;
  constexpr int glyph_height = 7;
  const int advance = (glyph_width + 1) * scale;
  int cursor_x = 2;
  int cursor_y = 4;
  for (const auto ch : text) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      cursor_x = 2;
      cursor_y += (glyph_height + 2) * scale;
      continue;
    }
    if (ch == ' ') {
      cursor_x += advance;
      continue;
    }
    if (cursor_x + glyph_width * scale >= width) {
      cursor_x = 2;
      cursor_y += (glyph_height + 2) * scale;
    }
    if (cursor_y + glyph_height * scale >= height) {
      break;
    }

    const auto* glyph = glyph_for(ch);
    if (glyph != nullptr) {
      for (int gy = 0; gy < glyph_height; ++gy) {
        for (int gx = 0; gx < glyph_width; ++gx) {
          if (((*glyph)[static_cast<std::size_t>(gy)] & (1U << (glyph_width - 1 - gx))) == 0U) {
            continue;
          }
          for (int sy = 0; sy < scale; ++sy) {
            for (int sx = 0; sx < scale; ++sx) {
              const auto x = cursor_x + gx * scale + sx;
              const auto y = cursor_y + gy * scale + sy;
              if (x >= 0 && y >= 0 && x < width && y < height) {
                auto* px = pixels.pixel(x, y);
                px[0] = 0;
                px[1] = 0;
                px[2] = 0;
                px[3] = 255;
              }
            }
          }
        }
      }
    }
    cursor_x += advance;
  }
  return pixels;
}

bool has_visible_alpha(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (pixels.format().channels < 4) {
    for (const auto byte : pixels.data()) {
      if (byte != 0U) {
        return true;
      }
    }
    return false;
  }
  for (std::size_t offset = 3; offset < pixels.data().size(); offset += pixels.format().channels) {
    if (pixels.data()[offset] != 0U) {
      return true;
    }
  }
  return false;
}

std::optional<Rect> visible_pixel_local_bounds(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return std::nullopt;
  }
  if (pixels.format().channels < 4) {
    return Rect{0, 0, pixels.width(), pixels.height()};
  }

  std::int32_t min_x = pixels.width();
  std::int32_t min_y = pixels.height();
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] == 0U) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

std::optional<PsdTextBoundsD> visible_text_local_bounds_from_layer_pixels(const Layer& layer, const Rect& visible,
                                                                          const std::array<double, 6>& transform) {
  const auto determinant = transform[0] * transform[3] - transform[1] * transform[2];
  if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001) {
    return std::nullopt;
  }
  const auto map_doc_to_local = [&transform, determinant](double x, double y) {
    const auto dx = x - transform[4];
    const auto dy = y - transform[5];
    return std::array<double, 2>{(transform[3] * dx - transform[2] * dy) / determinant,
                                 (-transform[1] * dx + transform[0] * dy) / determinant};
  };

  const auto left = static_cast<double>(layer.bounds().x + visible.x);
  const auto top = static_cast<double>(layer.bounds().y + visible.y);
  const auto right = static_cast<double>(layer.bounds().x + visible.x + visible.width);
  const auto bottom = static_cast<double>(layer.bounds().y + visible.y + visible.height);
  const std::array<std::array<double, 2>, 4> points = {
      map_doc_to_local(left, top),
      map_doc_to_local(right, top),
      map_doc_to_local(right, bottom),
      map_doc_to_local(left, bottom),
  };

  auto min_x = points.front()[0];
  auto max_x = points.front()[0];
  auto min_y = points.front()[1];
  auto max_y = points.front()[1];
  for (const auto& point : points) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1])) {
      return std::nullopt;
    }
    min_x = std::min(min_x, point[0]);
    max_x = std::max(max_x, point[0]);
    min_y = std::min(min_y, point[1]);
    max_y = std::max(max_y, point[1]);
  }
  if (max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return PsdTextBoundsD{min_x, min_y, max_x, max_y};
}

int estimate_text_size_from_alpha(const PixelBuffer& pixels) {
  if (pixels.empty() || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 4) {
    return 48;
  }

  std::vector<int> visible_runs;
  bool in_run = false;
  int run_start = 0;
  int blank_rows = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    bool row_has_ink = false;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] >= 8) {
        row_has_ink = true;
        break;
      }
    }

    if (row_has_ink) {
      if (!in_run) {
        in_run = true;
        run_start = y;
      }
      blank_rows = 0;
      continue;
    }

    if (in_run) {
      ++blank_rows;
      if (blank_rows >= 3) {
        visible_runs.push_back(std::max(1, y - blank_rows + 1 - run_start));
        in_run = false;
        blank_rows = 0;
      }
    }
  }
  if (in_run) {
    visible_runs.push_back(std::max(1, pixels.height() - blank_rows - run_start));
  }

  if (visible_runs.empty()) {
    return 48;
  }

  std::sort(visible_runs.begin(), visible_runs.end());
  const auto median_ink_height = visible_runs[visible_runs.size() / 2U];
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(median_ink_height) * 1.35)), 8, 220);
}

namespace {

std::optional<Rect> descriptor_bounds_rect(const DescriptorObject& object, std::string_view key) {
  const auto* bounds = descriptor_object(object, key);
  if (bounds == nullptr) {
    return std::nullopt;
  }
  const auto left = descriptor_number(*bounds, "Left", 0.0);
  const auto top = descriptor_number(*bounds, "Top ", 0.0);
  const auto right = descriptor_number(*bounds, "Rght", left);
  const auto bottom = descriptor_number(*bounds, "Btom", top);
  const auto width = static_cast<std::int32_t>(std::max(0.0, std::round(right - left)));
  const auto height = static_cast<std::int32_t>(std::max(0.0, std::round(bottom - top)));
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return Rect{static_cast<std::int32_t>(std::round(left)), static_cast<std::int32_t>(std::round(top)), width, height};
}

std::optional<PsdTextBoundsD> descriptor_bounds(const DescriptorObject& object, std::string_view key) {
  const auto* bounds = descriptor_object(object, key);
  if (bounds == nullptr) {
    return std::nullopt;
  }
  const auto left = descriptor_number(*bounds, "Left", 0.0);
  const auto top = descriptor_number(*bounds, "Top ", 0.0);
  const auto right = descriptor_number(*bounds, "Rght", left);
  const auto bottom = descriptor_number(*bounds, "Btom", top);
  if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) {
    return std::nullopt;
  }
  return PsdTextBoundsD{left, top, right, bottom};
}

std::optional<PsdTextBoundsD> extract_engine_box_bounds(std::span<const std::uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  const auto range = balanced_range_after(text, "/BoxBounds", '[', ']');
  if (!range.has_value()) {
    return std::nullopt;
  }
  const auto values = parse_engine_number_array(text.substr(range->first, range->second - range->first));
  if (values.size() < 4U) {
    return std::nullopt;
  }
  return PsdTextBoundsD{values[0], values[1], values[2], values[3]};
}

bool engine_data_describes_box_text(std::span<const std::uint8_t> payload) {
  const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
  return text.find("/BoxBounds") != std::string_view::npos && text.find("/ShapeType 1") != std::string_view::npos;
}

}  // namespace

std::optional<PsdTextGeometry> extract_type_tool_geometry(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (reader.remaining() < 2U + 6U * 8U + 2U + 4U) {
      return std::nullopt;
    }
    PsdTextGeometry geometry;
    (void)reader.read_u16();
    for (double& value : geometry.transform) {
      value = read_f64(reader);
    }
    (void)reader.read_u16();
    (void)reader.read_u32();
    const auto descriptor = read_descriptor(reader);
    geometry.bounds = descriptor_bounds(descriptor, "bounds").value_or(geometry.bounds);
    geometry.bounding_box = descriptor_bounds(descriptor, "boundingBox").value_or(geometry.bounds);
    if (const auto* text_index = descriptor_value(descriptor, "TextIndex");
        text_index != nullptr && text_index->type == DescriptorValue::Type::Integer) {
      geometry.text_index = text_index->integer_value;
    }
    // The warp descriptor follows the text descriptor (Warp Text: style + bend +
    // distortions, acting over the 'bounds' box). A malformed warp degrades to "no
    // warp" without losing the text geometry.
    try {
      if (reader.remaining() >= 6U) {
        (void)reader.read_u16();  // warp version (1)
        (void)reader.read_u32();  // descriptor version (16)
        const auto warp_descriptor = read_descriptor(reader);
        TextWarp warp;
        if (const auto* style = descriptor_value(warp_descriptor, "warpStyle");
            style != nullptr && style->type == DescriptorValue::Type::Enum) {
          warp.style = style->enum_value;
        }
        warp.value = descriptor_number(warp_descriptor, "warpValue", 0.0);
        warp.perspective = descriptor_number(warp_descriptor, "warpPerspective", 0.0);
        warp.perspective_other = descriptor_number(warp_descriptor, "warpPerspectiveOther", 0.0);
        if (const auto* rotate = descriptor_value(warp_descriptor, "warpRotate");
            rotate != nullptr && rotate->type == DescriptorValue::Type::Enum) {
          warp.rotate = rotate->enum_value;
        }
        warp.bounds_left = geometry.bounds.left;
        warp.bounds_top = geometry.bounds.top;
        warp.bounds_right = geometry.bounds.right;
        warp.bounds_bottom = geometry.bounds.bottom;
        if (!text_warp_is_identity(warp)) {
          geometry.warp = std::move(warp);
        }
      }
    } catch (const std::exception&) {
    }
    geometry.box_bounds = extract_engine_box_bounds(payload).value_or(PsdTextBoundsD{
        0.0, 0.0, std::max(1.0, geometry.bounds.right - geometry.bounds.left), std::max(1.0, geometry.bounds.bottom)});
    if (payload.size() >= 16U) {
      const auto tail_offset = payload.size() - 16U;
      for (std::size_t index = 0; index < geometry.tail_bounds.size(); ++index) {
        geometry.tail_bounds[index] = static_cast<int>(static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(payload[tail_offset + index * 4U]) << 24U) |
            (static_cast<std::uint32_t>(payload[tail_offset + index * 4U + 1U]) << 16U) |
            (static_cast<std::uint32_t>(payload[tail_offset + index * 4U + 2U]) << 8U) |
            static_cast<std::uint32_t>(payload[tail_offset + index * 4U + 3U])));
      }
    }
    return geometry;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<Rect> extract_type_tool_text_box(std::span<const std::uint8_t> payload) {
  if (!engine_data_describes_box_text(payload)) {
    return std::nullopt;
  }
  if (const auto box_bounds = extract_engine_box_bounds(payload); box_bounds.has_value()) {
    const auto left = static_cast<std::int32_t>(std::round(box_bounds->left));
    const auto top = static_cast<std::int32_t>(std::round(box_bounds->top));
    const auto width = static_cast<std::int32_t>(std::max(0.0, std::round(box_bounds->right - box_bounds->left)));
    const auto height = static_cast<std::int32_t>(std::max(0.0, std::round(box_bounds->bottom - box_bounds->top)));
    if (width > 0 && height > 0) {
      return Rect{left, top, width, height};
    }
  }
  try {
    BigEndianReader reader(payload);
    if (reader.remaining() < 2U + 6U * 8U + 2U + 4U) {
      return std::nullopt;
    }
    (void)reader.read_u16();
    for (int i = 0; i < 6; ++i) {
      (void)read_f64(reader);
    }
    (void)reader.read_u16();
    (void)reader.read_u32();
    const auto descriptor = read_descriptor(reader);
    if (auto bounds = descriptor_bounds_rect(descriptor, "bounds"); bounds.has_value()) {
      return bounds;
    }
    return descriptor_bounds_rect(descriptor, "boundingBox");
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace patchy::psd
