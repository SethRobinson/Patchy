#include "core/vector_shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace patchy {

namespace {

std::string format_double(double value) {
  std::array<char, 32> buffer{};
  // %.17g round-trips every finite double exactly.
  std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
  return std::string(buffer.data());
}

std::optional<double> parse_double_token(const char*& cursor) {
  char* end = nullptr;
  const auto value = std::strtod(cursor, &end);
  if (end == cursor) {
    return std::nullopt;
  }
  cursor = end;
  return value;
}

std::optional<long> parse_long_token(const char*& cursor) {
  char* end = nullptr;
  const auto value = std::strtol(cursor, &end, 10);
  if (end == cursor) {
    return std::nullopt;
  }
  cursor = end;
  return value;
}

void skip_spaces(const char*& cursor) {
  while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') {
    ++cursor;
  }
}

bool consume_tag(const char*& cursor, char tag) {
  skip_spaces(cursor);
  if (*cursor != tag) {
    return false;
  }
  ++cursor;
  return true;
}

void extend_bounds(VectorPathBounds& bounds, bool& any, double x, double y) noexcept {
  if (!any) {
    bounds.left = bounds.right = x;
    bounds.top = bounds.bottom = y;
    any = true;
    return;
  }
  bounds.left = std::min(bounds.left, x);
  bounds.right = std::max(bounds.right, x);
  bounds.top = std::min(bounds.top, y);
  bounds.bottom = std::max(bounds.bottom, y);
}

}  // namespace

bool VectorPath::empty() const noexcept {
  for (const auto& subpath : subpaths) {
    if (!subpath.anchors.empty()) {
      return false;
    }
  }
  return true;
}

std::optional<VectorPathBounds> VectorPath::bounds() const noexcept {
  VectorPathBounds result{};
  bool any = false;
  for (const auto& subpath : subpaths) {
    for (const auto& anchor : subpath.anchors) {
      extend_bounds(result, any, anchor.anchor_x, anchor.anchor_y);
      extend_bounds(result, any, anchor.in_x, anchor.in_y);
      extend_bounds(result, any, anchor.out_x, anchor.out_y);
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return result;
}

std::int32_t VectorPath::next_shape_group() const noexcept {
  std::int32_t next = 0;
  for (const auto& subpath : subpaths) {
    next = std::max(next, subpath.shape_group + 1);
  }
  return next;
}

bool layer_has_vector_shape_marker(const Layer& layer) {
  const auto& metadata = layer.metadata();
  const auto it = metadata.find(kLayerMetadataVectorShape);
  return it != metadata.end() && !it->second.empty();
}

std::string vector_lock_reason(const Layer& layer) {
  const auto& metadata = layer.metadata();
  const auto it = metadata.find(kLayerMetadataVectorLock);
  return it != metadata.end() ? it->second : std::string{};
}

bool layer_vector_block_dirty(const Layer& layer) {
  return layer.metadata().contains(kLayerMetadataVectorBlockDirty);
}

void mark_layer_vector_block_dirty(Layer& layer) {
  layer.metadata()[kLayerMetadataVectorBlockDirty] = "1";
}

std::string serialize_vector_path(const VectorPath& path) {
  std::string out = "v1 ";
  out += std::to_string(path.fill_rule_value);
  out += ' ';
  out += std::to_string(path.initial_fill_value);
  out += ' ';
  out += std::to_string(path.subpaths.size());
  for (const auto& subpath : path.subpaths) {
    out += "\nS ";
    out += subpath.closed ? '1' : '0';
    out += ' ';
    out += std::to_string(static_cast<int>(subpath.op));
    out += ' ';
    out += std::to_string(subpath.shape_group);
    out += ' ';
    out += std::to_string(subpath.anchors.size());
    for (const auto& anchor : subpath.anchors) {
      out += "\nA ";
      out += anchor.smooth ? '1' : '0';
      out += ' ';
      out += format_double(anchor.anchor_x);
      out += ' ';
      out += format_double(anchor.anchor_y);
      out += ' ';
      out += format_double(anchor.in_x);
      out += ' ';
      out += format_double(anchor.in_y);
      out += ' ';
      out += format_double(anchor.out_x);
      out += ' ';
      out += format_double(anchor.out_y);
    }
  }
  return out;
}

std::optional<VectorPath> parse_vector_path(std::string_view text) {
  const std::string copy(text);
  const char* cursor = copy.c_str();
  skip_spaces(cursor);
  if (*cursor != 'v' || *(cursor + 1) != '1') {
    return std::nullopt;
  }
  cursor += 2;
  VectorPath path;
  const auto fill_rule = parse_long_token(cursor);
  const auto initial_fill = parse_long_token(cursor);
  const auto subpath_count = parse_long_token(cursor);
  if (!fill_rule.has_value() || !initial_fill.has_value() || !subpath_count.has_value() ||
      *subpath_count < 0) {
    return std::nullopt;
  }
  path.fill_rule_value = static_cast<std::uint16_t>(*fill_rule);
  path.initial_fill_value = static_cast<std::uint16_t>(*initial_fill);
  path.subpaths.reserve(static_cast<std::size_t>(*subpath_count));
  for (long subpath_index = 0; subpath_index < *subpath_count; ++subpath_index) {
    if (!consume_tag(cursor, 'S')) {
      return std::nullopt;
    }
    PathSubpath subpath;
    const auto closed = parse_long_token(cursor);
    const auto op = parse_long_token(cursor);
    const auto group = parse_long_token(cursor);
    const auto anchor_count = parse_long_token(cursor);
    if (!closed.has_value() || !op.has_value() || !group.has_value() || !anchor_count.has_value() ||
        *op < 0 || *op > 3 || *anchor_count < 0) {
      return std::nullopt;
    }
    subpath.closed = *closed != 0;
    subpath.op = static_cast<PathCombineOp>(*op);
    subpath.shape_group = static_cast<std::int32_t>(*group);
    subpath.anchors.reserve(static_cast<std::size_t>(*anchor_count));
    for (long anchor_index = 0; anchor_index < *anchor_count; ++anchor_index) {
      if (!consume_tag(cursor, 'A')) {
        return std::nullopt;
      }
      PathAnchor anchor;
      const auto smooth = parse_long_token(cursor);
      const auto ax = parse_double_token(cursor);
      const auto ay = parse_double_token(cursor);
      const auto in_x = parse_double_token(cursor);
      const auto in_y = parse_double_token(cursor);
      const auto out_x = parse_double_token(cursor);
      const auto out_y = parse_double_token(cursor);
      if (!smooth.has_value() || !ax.has_value() || !ay.has_value() || !in_x.has_value() ||
          !in_y.has_value() || !out_x.has_value() || !out_y.has_value()) {
        return std::nullopt;
      }
      anchor.smooth = *smooth != 0;
      anchor.anchor_x = *ax;
      anchor.anchor_y = *ay;
      anchor.in_x = *in_x;
      anchor.in_y = *in_y;
      anchor.out_x = *out_x;
      anchor.out_y = *out_y;
      subpath.anchors.push_back(anchor);
    }
    path.subpaths.push_back(std::move(subpath));
  }
  skip_spaces(cursor);
  if (*cursor != '\0') {
    return std::nullopt;
  }
  return path;
}

std::int32_t path_coordinate_to_fixed(double pixels, std::int32_t extent) noexcept {
  if (extent <= 0) {
    return 0;
  }
  constexpr double kPathFixedScale = 16777216.0;
  const auto scaled = std::llround((pixels * kPathFixedScale) / static_cast<double>(extent));
  return static_cast<std::int32_t>(
      std::clamp(scaled, static_cast<long long>(std::numeric_limits<std::int32_t>::min()),
                 static_cast<long long>(std::numeric_limits<std::int32_t>::max())));
}

double path_coordinate_from_fixed(std::int32_t fixed, std::int32_t extent) noexcept {
  constexpr double kPathFixedScale = 16777216.0;
  return (static_cast<double>(fixed) / kPathFixedScale) * static_cast<double>(extent);
}

void transform_vector_path(VectorPath& path, const std::array<double, 6>& matrix) {
  const auto apply = [&matrix](double& x, double& y) {
    const double source_x = x;
    const double source_y = y;
    x = matrix[0] * source_x + matrix[2] * source_y + matrix[4];
    y = matrix[1] * source_x + matrix[3] * source_y + matrix[5];
  };
  for (auto& subpath : path.subpaths) {
    for (auto& anchor : subpath.anchors) {
      apply(anchor.anchor_x, anchor.anchor_y);
      apply(anchor.in_x, anchor.in_y);
      apply(anchor.out_x, anchor.out_y);
    }
  }
}

void translate_vector_path(VectorPath& path, double dx, double dy) {
  transform_vector_path(path, {1.0, 0.0, 0.0, 1.0, dx, dy});
}

}  // namespace patchy
