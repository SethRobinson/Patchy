#include "core/text_warp.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace patchy {

namespace {

std::optional<double> parse_double_token(const char*& cursor) {
  char* end = nullptr;
  const auto value = std::strtod(cursor, &end);
  if (end == cursor) {
    return std::nullopt;
  }
  cursor = end;
  return value;
}

std::string format_double(double value) {
  std::array<char, 32> buffer{};
  // %.17g round-trips every finite double exactly.
  std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
  return std::string(buffer.data());
}

}  // namespace

bool text_warp_is_identity(const TextWarp& warp) {
  if (warp.style == "warpNone" || warp.style.empty()) {
    return true;
  }
  return warp.value == 0.0 && warp.perspective == 0.0 && warp.perspective_other == 0.0;
}

std::string serialize_text_warp(const TextWarp& warp) {
  std::string serialized = warp.style + " " + warp.rotate;
  const auto append_number = [&serialized](double value) {
    serialized.push_back(' ');
    serialized += format_double(value);
  };
  append_number(warp.value);
  append_number(warp.perspective);
  append_number(warp.perspective_other);
  append_number(warp.bounds_left);
  append_number(warp.bounds_top);
  append_number(warp.bounds_right);
  append_number(warp.bounds_bottom);
  append_number(warp.baseline);
  return serialized;
}

std::optional<TextWarp> parse_text_warp(std::string_view text) {
  const std::string copy(text);
  const char* cursor = copy.c_str();
  const auto read_token = [&cursor]() -> std::optional<std::string> {
    while (*cursor == ' ') {
      ++cursor;
    }
    const char* start = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
      ++cursor;
    }
    if (cursor == start) {
      return std::nullopt;
    }
    return std::string(start, cursor);
  };
  TextWarp warp;
  const auto style = read_token();
  const auto rotate = read_token();
  if (!style.has_value() || !rotate.has_value()) {
    return std::nullopt;
  }
  warp.style = *style;
  warp.rotate = *rotate;
  const auto value = parse_double_token(cursor);
  const auto perspective = parse_double_token(cursor);
  const auto perspective_other = parse_double_token(cursor);
  const auto left = parse_double_token(cursor);
  const auto top = parse_double_token(cursor);
  const auto right = parse_double_token(cursor);
  const auto bottom = parse_double_token(cursor);
  // Optional trailing baseline (older strings simply end after the box).
  const auto baseline = parse_double_token(cursor);
  warp.value = value.value_or(0.0);
  warp.perspective = perspective.value_or(0.0);
  warp.perspective_other = perspective_other.value_or(0.0);
  warp.bounds_left = left.value_or(0.0);
  warp.bounds_top = top.value_or(0.0);
  warp.bounds_right = right.value_or(0.0);
  warp.bounds_bottom = bottom.value_or(0.0);
  warp.baseline = baseline.value_or(0.0);
  return warp;
}

std::optional<TextWarp> text_warp_from_layer(const Layer& layer) {
  const auto& metadata = std::as_const(layer).metadata();
  const auto found = metadata.find(kLayerMetadataTextWarp);
  if (found == metadata.end()) {
    return std::nullopt;
  }
  return parse_text_warp(found->second);
}

std::optional<WarpMeshGrid> generate_text_warp_mesh(const TextWarp& warp) {
  if (text_warp_is_identity(warp)) {
    return std::nullopt;
  }
  const double width = warp.bounds_right - warp.bounds_left;
  const double height = warp.bounds_bottom - warp.bounds_top;
  auto mesh = generate_style_warp_mesh(warp.style, warp.value, warp.rotate == "Vrtc", width, height);
  if (!mesh.has_value()) {
    return std::nullopt;
  }
  apply_warp_distortion(*mesh, warp.perspective, warp.perspective_other);
  for (auto& x : mesh->xs) {
    x += warp.bounds_left;
  }
  for (auto& y : mesh->ys) {
    y += warp.bounds_top;
  }
  return mesh;
}

}  // namespace patchy
