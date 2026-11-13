#include "core/text_path.hpp"

#include "core/text_area.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace patchy {

namespace {

constexpr double kMinimumRange = 1e-6;

void cubic_point(const double* px, const double* py, double t, double& x, double& y) {
  const double u = 1.0 - t;
  const double b0 = u * u * u;
  const double b1 = 3.0 * u * u * t;
  const double b2 = 3.0 * u * t * t;
  const double b3 = t * t * t;
  x = b0 * px[0] + b1 * px[1] + b2 * px[2] + b3 * px[3];
  y = b0 * py[0] + b1 * py[1] + b2 * py[2] + b3 * py[3];
}

void cubic_derivative(const double* px, const double* py, double t, double& dx, double& dy) {
  const double u = 1.0 - t;
  const double d0 = 3.0 * u * u;
  const double d1 = 6.0 * u * t;
  const double d2 = 3.0 * t * t;
  dx = d0 * (px[1] - px[0]) + d1 * (px[2] - px[1]) + d2 * (px[3] - px[2]);
  dy = d0 * (py[1] - py[0]) + d1 * (py[2] - py[1]) + d2 * (py[3] - py[2]);
}

}  // namespace

bool valid_text_path(const VectorPath& path) {
  if (path.subpaths.size() != 1) {
    return false;
  }
  const auto& anchors = path.subpaths.front().anchors;
  if (anchors.size() < 2 || anchors.size() > kTextPathMaxAnchors) {
    return false;
  }
  for (const auto& p : anchors) {
    for (const double v : {p.anchor_x, p.anchor_y, p.in_x, p.in_y, p.out_x, p.out_y}) {
      if (!std::isfinite(v) || std::abs(v) > 1000000.0) {
        return false;
      }
    }
  }
  return true;
}

int text_path_segment_count(const VectorPath& path) {
  if (path.subpaths.empty()) {
    return 0;
  }
  const auto& sub = path.subpaths.front();
  if (sub.anchors.size() < 2) {
    return 0;
  }
  return static_cast<int>(sub.closed ? sub.anchors.size() : sub.anchors.size() - 1U);
}

TextPathRange full_text_path_range(const VectorPath& path) {
  return {0.0, static_cast<double>(text_path_segment_count(path))};
}

TextPathRange normalized_text_path_range(const VectorPath& path, TextPathRange range) {
  const double count = static_cast<double>(text_path_segment_count(path));
  if (count <= 0.0) {
    return {};
  }
  if (!std::isfinite(range.start) || !std::isfinite(range.end) || !(range.end - range.start > kMinimumRange)) {
    return {0.0, count};
  }
  const bool closed = !path.subpaths.empty() && path.subpaths.front().closed;
  if (!closed) {
    range.start = std::clamp(range.start, 0.0, count);
    range.end = std::clamp(range.end, 0.0, count);
    if (!(range.end - range.start > kMinimumRange)) {
      return {0.0, count};
    }
    return range;
  }
  // Closed: bring the start onto the contour, keep the length, never more than one lap.
  const double length = std::min(range.end - range.start, count);
  double start = std::fmod(range.start, count);
  if (start < 0.0) {
    start += count;
  }
  if (start >= count) {
    start = 0.0;
  }
  return {start, start + length};
}

std::string serialize_text_path_range(TextPathRange range) {
  std::array<char, 80> buffer{};
  // %.17g round-trips every finite double exactly.
  std::snprintf(buffer.data(), buffer.size(), "%.17g %.17g", range.start, range.end);
  return std::string(buffer.data());
}

std::optional<TextPathRange> parse_text_path_range(std::string_view text) {
  const std::string copy(text);
  const char* cursor = copy.c_str();
  char* end = nullptr;
  const double start = std::strtod(cursor, &end);
  if (end == cursor) {
    return std::nullopt;
  }
  cursor = end;
  const double finish = std::strtod(cursor, &end);
  if (end == cursor || !std::isfinite(start) || !std::isfinite(finish)) {
    return std::nullopt;
  }
  return TextPathRange{start, finish};
}

std::optional<VectorPath> text_path_for_layer(const Layer& layer) {
  const auto it = layer.metadata().find(kLayerMetadataTextPath);
  if (it == layer.metadata().end()) {
    return std::nullopt;
  }
  auto path = parse_vector_path(it->second);
  if (!path || !valid_text_path(*path)) {
    return std::nullopt;
  }
  path->fill_rule_value = 0;
  path->initial_fill_value = 0;
  path->subpaths.front().op = PathCombineOp::Xor;
  path->subpaths.front().shape_group = 0;
  return path;
}

TextPathRange text_path_range_for_layer(const Layer& layer, const VectorPath& path) {
  const auto it = layer.metadata().find(kLayerMetadataTextPathRange);
  if (it != layer.metadata().end()) {
    if (const auto range = parse_text_path_range(it->second)) {
      return normalized_text_path_range(path, *range);
    }
  }
  return full_text_path_range(path);
}

std::optional<VectorPath> text_path_in_document(const Layer& layer) {
  auto path = text_path_for_layer(layer);
  if (path) {
    transform_vector_path(*path, text_area_transform(layer));
  }
  return path;
}

std::optional<TextPathFrame> text_path_frame_for_layer(const Layer& layer) {
  auto path = text_path_for_layer(layer);
  if (!path) {
    return std::nullopt;
  }
  const auto range = text_path_range_for_layer(layer, *path);
  return TextPathFrame{std::move(*path), range};
}

bool text_layer_has_contour(const Layer& layer) {
  return text_area_for_layer(layer).has_value() || text_path_for_layer(layer).has_value();
}

VectorPath reversed_text_path(const VectorPath& path) {
  VectorPath result = path;
  if (result.subpaths.empty()) {
    return result;
  }
  auto& sub = result.subpaths.front();
  if (sub.anchors.empty()) {
    return result;
  }
  // A closed contour keeps its first anchor, so the seam stays where it was.
  std::reverse(sub.closed ? sub.anchors.begin() + 1 : sub.anchors.begin(), sub.anchors.end());
  for (auto& anchor : sub.anchors) {
    std::swap(anchor.in_x, anchor.out_x);
    std::swap(anchor.in_y, anchor.out_y);
  }
  return result;
}

TextPathRange reversed_text_path_range(const VectorPath& path, TextPathRange range) {
  const double count = static_cast<double>(text_path_segment_count(path));
  range = normalized_text_path_range(path, range);
  // Segment k of the reversed contour is segment count - 1 - k walked backward.
  return normalized_text_path_range(path, {count - range.end, count - range.start});
}

TextPathCurve::TextPathCurve(const VectorPath& path, double scale) {
  if (!valid_text_path(path) || !std::isfinite(scale) || scale <= 0.0) {
    return;
  }
  const auto& sub = path.subpaths.front();
  closed_ = sub.closed;
  segment_count_ = text_path_segment_count(path);
  segments_.reserve(static_cast<std::size_t>(segment_count_));
  for (int i = 0; i < segment_count_; ++i) {
    const auto& a = sub.anchors[static_cast<std::size_t>(i)];
    const auto& b = sub.anchors[(static_cast<std::size_t>(i) + 1U) % sub.anchors.size()];
    Segment segment;
    segment.x[0] = a.anchor_x * scale;
    segment.y[0] = a.anchor_y * scale;
    segment.x[1] = a.out_x * scale;
    segment.y[1] = a.out_y * scale;
    segment.x[2] = b.in_x * scale;
    segment.y[2] = b.in_y * scale;
    segment.x[3] = b.anchor_x * scale;
    segment.y[3] = b.anchor_y * scale;
    segments_.push_back(segment);
  }
  const std::size_t stride = static_cast<std::size_t>(kChordsPerSegment) + 1U;
  cumulative_.assign(segments_.size() * stride, 0.0);
  double total = 0.0;
  for (std::size_t s = 0; s < segments_.size(); ++s) {
    const auto& segment = segments_[s];
    double previous_x = segment.x[0];
    double previous_y = segment.y[0];
    cumulative_[s * stride] = total;
    for (int c = 1; c <= kChordsPerSegment; ++c) {
      double x = 0.0;
      double y = 0.0;
      cubic_point(segment.x, segment.y, static_cast<double>(c) / kChordsPerSegment, x, y);
      total += std::hypot(x - previous_x, y - previous_y);
      cumulative_[s * stride + static_cast<std::size_t>(c)] = total;
      previous_x = x;
      previous_y = y;
    }
  }
  length_ = total;
  if (!(length_ > 0.0) || !std::isfinite(length_)) {
    segment_count_ = 0;
    length_ = 0.0;
    segments_.clear();
    cumulative_.clear();
  }
}

double TextPathCurve::distance_at_parameter(double parameter) const {
  if (!valid() || !std::isfinite(parameter)) {
    return 0.0;
  }
  const double count = static_cast<double>(segment_count_);
  double laps = 0.0;
  if (closed_) {
    laps = std::floor(parameter / count);
    parameter -= laps * count;
    // parameter == count after a subtraction that rounded: fold onto the next lap's start.
    if (parameter >= count) {
      parameter = 0.0;
      laps += 1.0;
    }
  }
  parameter = std::clamp(parameter, 0.0, count);
  const std::size_t stride = static_cast<std::size_t>(kChordsPerSegment) + 1U;
  auto segment = static_cast<std::size_t>(std::min(std::floor(parameter), count - 1.0));
  const double t = parameter - static_cast<double>(segment);
  const double chord_position = t * kChordsPerSegment;
  const auto chord = static_cast<std::size_t>(std::min<double>(std::floor(chord_position), kChordsPerSegment - 1));
  const double fraction = chord_position - static_cast<double>(chord);
  const double a = cumulative_[segment * stride + chord];
  const double b = cumulative_[segment * stride + chord + 1U];
  return laps * length_ + a + (b - a) * fraction;
}

double TextPathCurve::parameter_at_distance(double distance) const {
  if (!valid() || !std::isfinite(distance)) {
    return 0.0;
  }
  distance = std::clamp(distance, 0.0, length_);
  const std::size_t stride = static_cast<std::size_t>(kChordsPerSegment) + 1U;
  // The last table entry at or before the distance.
  const auto upper = std::upper_bound(cumulative_.begin(), cumulative_.end(), distance);
  std::size_t index = upper == cumulative_.begin() ? 0U : static_cast<std::size_t>(upper - cumulative_.begin()) - 1U;
  std::size_t segment = index / stride;
  std::size_t chord = index % stride;
  if (chord >= static_cast<std::size_t>(kChordsPerSegment)) {
    // The entry closing a segment: the same distance opens the next one.
    if (segment + 1U < segments_.size()) {
      ++segment;
      chord = 0U;
    } else {
      return static_cast<double>(segment_count_);
    }
  }
  const double a = cumulative_[segment * stride + chord];
  const double b = cumulative_[segment * stride + chord + 1U];
  const double fraction = b > a ? std::clamp((distance - a) / (b - a), 0.0, 1.0) : 0.0;
  return static_cast<double>(segment) + (static_cast<double>(chord) + fraction) / kChordsPerSegment;
}

TextPathCurve::Sample TextPathCurve::sample_segment(std::size_t segment, double t) const {
  const auto& s = segments_[segment];
  Sample result;
  cubic_point(s.x, s.y, t, result.x, result.y);
  double dx = 0.0;
  double dy = 0.0;
  cubic_derivative(s.x, s.y, t, dx, dy);
  double magnitude = std::hypot(dx, dy);
  if (!(magnitude > 1e-9)) {
    // Handles folded onto their anchor (a corner, a straight segment's ends) have no
    // derivative there: take the direction of the chord around the point.
    const double step = 1.0 / kChordsPerSegment;
    const double t0 = std::max(0.0, t - step);
    const double t1 = std::min(1.0, t + step);
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    cubic_point(s.x, s.y, t0, x0, y0);
    cubic_point(s.x, s.y, t1, x1, y1);
    dx = x1 - x0;
    dy = y1 - y0;
    magnitude = std::hypot(dx, dy);
  }
  if (magnitude > 1e-12) {
    result.tangent_x = dx / magnitude;
    result.tangent_y = dy / magnitude;
  }
  return result;
}

TextPathCurve::Sample TextPathCurve::sample(double distance) const {
  if (!valid() || !std::isfinite(distance)) {
    return {};
  }
  if (closed_) {
    distance = std::fmod(distance, length_);
    if (distance < 0.0) {
      distance += length_;
    }
  } else if (distance < 0.0 || distance > length_) {
    const bool before = distance < 0.0;
    auto end = before ? sample_segment(0U, 0.0) : sample_segment(segments_.size() - 1U, 1.0);
    const double overshoot = before ? distance : distance - length_;
    end.x += end.tangent_x * overshoot;
    end.y += end.tangent_y * overshoot;
    return end;
  }
  const double parameter = parameter_at_distance(distance);
  const double count = static_cast<double>(segment_count_);
  const auto segment = static_cast<std::size_t>(std::min(std::floor(parameter), count - 1.0));
  return sample_segment(segment, std::clamp(parameter - static_cast<double>(segment), 0.0, 1.0));
}

TextPathCurve::Nearest TextPathCurve::nearest(double x, double y) const {
  Nearest result;
  if (!valid()) {
    return result;
  }
  const std::size_t stride = static_cast<std::size_t>(kChordsPerSegment) + 1U;
  double best = -1.0;
  for (std::size_t s = 0; s < segments_.size(); ++s) {
    const auto& segment = segments_[s];
    double ax = segment.x[0];
    double ay = segment.y[0];
    for (int c = 0; c < kChordsPerSegment; ++c) {
      double bx = 0.0;
      double by = 0.0;
      cubic_point(segment.x, segment.y, static_cast<double>(c + 1) / kChordsPerSegment, bx, by);
      const double vx = bx - ax;
      const double vy = by - ay;
      const double squared = vx * vx + vy * vy;
      const double fraction = squared > 0.0 ? std::clamp(((x - ax) * vx + (y - ay) * vy) / squared, 0.0, 1.0) : 0.0;
      const double px = ax + vx * fraction;
      const double py = ay + vy * fraction;
      const double gap = (x - px) * (x - px) + (y - py) * (y - py);
      if (best < 0.0 || gap < best) {
        best = gap;
        result.parameter = static_cast<double>(s) + (static_cast<double>(c) + fraction) / kChordsPerSegment;
        const double a = cumulative_[s * stride + static_cast<std::size_t>(c)];
        const double b = cumulative_[s * stride + static_cast<std::size_t>(c) + 1U];
        result.distance = a + (b - a) * fraction;
        // Left of travel in y-down space: the cross product of travel and offset is negative.
        result.left = vx * (y - py) - vy * (x - px) < 0.0;
      }
      ax = bx;
      ay = by;
    }
  }
  result.gap = best > 0.0 ? std::sqrt(best) : 0.0;
  return result;
}

TextPathSpan text_path_span(const TextPathCurve& curve, TextPathRange range) {
  if (!curve.valid()) {
    return {};
  }
  const double start = curve.distance_at_parameter(range.start);
  const double end = curve.distance_at_parameter(range.end);
  return {start, std::max(0.0, end - start)};
}

}  // namespace patchy
