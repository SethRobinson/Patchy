#include "core/path_fit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace patchy {

namespace {

struct Vec {
  double x{0.0};
  double y{0.0};
};

Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(Vec a, double s) { return {a.x * s, a.y * s}; }

double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
double length(Vec a) { return std::sqrt(dot(a, a)); }

Vec normalized(Vec a) {
  const auto len = length(a);
  if (len <= 1e-12) {
    return {0.0, 0.0};
  }
  return {a.x / len, a.y / len};
}

Vec point_vec(const FitPoint& p) { return {p.x, p.y}; }

// One fitted cubic segment: p0 -> (c1, c2) -> p3.
struct CubicSegment {
  Vec p0;
  Vec c1;
  Vec c2;
  Vec p3;
};

double bernstein(int index, double t) {
  const double u = 1.0 - t;
  switch (index) {
    case 0:
      return u * u * u;
    case 1:
      return 3.0 * t * u * u;
    case 2:
      return 3.0 * t * t * u;
    default:
      return t * t * t;
  }
}

Vec evaluate_cubic(const CubicSegment& segment, double t) {
  return segment.p0 * bernstein(0, t) + segment.c1 * bernstein(1, t) +
         segment.c2 * bernstein(2, t) + segment.p3 * bernstein(3, t);
}

// Perpendicular distance of `point` from the chord a..b (falls back to the
// distance to `a` for a degenerate chord).
double chord_distance(Vec point, Vec a, Vec b) {
  const auto chord = b - a;
  const auto chord_length = length(chord);
  if (chord_length <= 1e-12) {
    return length(point - a);
  }
  const auto cross = chord.x * (point.y - a.y) - chord.y * (point.x - a.x);
  return std::abs(cross) / chord_length;
}

// --- Douglas-Peucker over the closed loop ---------------------------------

// Marks kept vertices of the arc points[first..last] (indices into the cyclic
// loop, walked forward with wraparound; first/last themselves already kept).
void douglas_peucker_arc(const std::vector<FitPoint>& points, std::size_t first, std::size_t last,
                         double epsilon, std::vector<bool>& keep) {
  const auto count = points.size();
  const auto arc_length = (last + count - first) % count;
  if (arc_length < 2) {
    return;
  }
  const auto a = point_vec(points[first]);
  const auto b = point_vec(points[last]);
  double max_distance = -1.0;
  std::size_t max_index = first;
  for (std::size_t step = 1; step < arc_length; ++step) {
    const auto index = (first + step) % count;
    const auto distance = chord_distance(point_vec(points[index]), a, b);
    if (distance > max_distance) {  // strict >: first index wins ties
      max_distance = distance;
      max_index = index;
    }
  }
  if (max_distance > epsilon) {
    keep[max_index] = true;
    douglas_peucker_arc(points, first, max_index, epsilon, keep);
    douglas_peucker_arc(points, max_index, last, epsilon, keep);
  }
}

// Kept-vertex indices of the closed loop, ascending. Seeds with vertex 0 and
// the vertex farthest from it (the standard closed-loop split).
std::vector<std::size_t> douglas_peucker_loop(const std::vector<FitPoint>& points, double epsilon) {
  const auto count = points.size();
  std::vector<bool> keep(count, false);
  keep[0] = true;
  double max_distance = -1.0;
  std::size_t far_index = 0;
  const auto origin = point_vec(points[0]);
  for (std::size_t index = 1; index < count; ++index) {
    const auto distance = length(point_vec(points[index]) - origin);
    if (distance > max_distance) {
      max_distance = distance;
      far_index = index;
    }
  }
  if (far_index != 0) {
    keep[far_index] = true;
    douglas_peucker_arc(points, 0, far_index, epsilon, keep);
    douglas_peucker_arc(points, far_index, 0, epsilon, keep);
  }
  std::vector<std::size_t> kept;
  for (std::size_t index = 0; index < count; ++index) {
    if (keep[index]) {
      kept.push_back(index);
    }
  }
  return kept;
}

// --- Schneider least-squares cubic fitting --------------------------------

// Chord-length parametrization of run[first..last].
std::vector<double> chord_parameters(const std::vector<Vec>& run) {
  std::vector<double> u(run.size(), 0.0);
  for (std::size_t i = 1; i < run.size(); ++i) {
    u[i] = u[i - 1] + length(run[i] - run[i - 1]);
  }
  const auto total = u.back();
  if (total > 1e-12) {
    for (auto& value : u) {
      value /= total;
    }
  }
  return u;
}

// Least-squares placement of the two inner control points for fixed end
// tangents (Graphics Gems GenerateBezier).
CubicSegment generate_bezier(const std::vector<Vec>& run, const std::vector<double>& u,
                             Vec tangent_start, Vec tangent_end) {
  const auto first = run.front();
  const auto last = run.back();
  double c00 = 0.0;
  double c01 = 0.0;
  double c11 = 0.0;
  double x0 = 0.0;
  double x1 = 0.0;
  for (std::size_t i = 0; i < run.size(); ++i) {
    const auto b0 = bernstein(0, u[i]);
    const auto b1 = bernstein(1, u[i]);
    const auto b2 = bernstein(2, u[i]);
    const auto b3 = bernstein(3, u[i]);
    const auto a0 = tangent_start * b1;
    const auto a1 = tangent_end * b2;
    c00 += dot(a0, a0);
    c01 += dot(a0, a1);
    c11 += dot(a1, a1);
    const auto target = run[i] - (first * (b0 + b1) + last * (b2 + b3));
    x0 += dot(a0, target);
    x1 += dot(a1, target);
  }
  const auto det_c = c00 * c11 - c01 * c01;
  double alpha_left = 0.0;
  double alpha_right = 0.0;
  if (std::abs(det_c) > 1e-12) {
    alpha_left = (x0 * c11 - x1 * c01) / det_c;
    alpha_right = (c00 * x1 - c01 * x0) / det_c;
  }
  const auto segment_length = length(last - first);
  const auto fallback = segment_length / 3.0;
  if (alpha_left <= 1e-6 || alpha_right <= 1e-6 || !std::isfinite(alpha_left) ||
      !std::isfinite(alpha_right)) {
    alpha_left = fallback;
    alpha_right = fallback;
  }
  return {first, first + tangent_start * alpha_left, last + tangent_end * alpha_right, last};
}

// Max squared deviation of the run from the segment; the split index reports
// where (first index wins ties).
double max_fit_error(const std::vector<Vec>& run, const std::vector<double>& u,
                     const CubicSegment& segment, std::size_t& split_index) {
  double max_error = 0.0;
  split_index = run.size() / 2;
  for (std::size_t i = 1; i + 1 < run.size(); ++i) {
    const auto offset = evaluate_cubic(segment, u[i]) - run[i];
    const auto error = dot(offset, offset);
    if (error > max_error) {
      max_error = error;
      split_index = i;
    }
  }
  return max_error;
}

// One Newton-Raphson step of the parameter for a closer projection.
double refine_parameter(const CubicSegment& segment, Vec point, double u) {
  // Derivatives of the cubic at u.
  const std::array<Vec, 3> d1{(segment.c1 - segment.p0) * 3.0, (segment.c2 - segment.c1) * 3.0,
                              (segment.p3 - segment.c2) * 3.0};
  const std::array<Vec, 2> d2{(d1[1] - d1[0]) * 2.0, (d1[2] - d1[1]) * 2.0};
  const auto q_u = evaluate_cubic(segment, u);
  const auto v = 1.0 - u;
  const auto q1 = d1[0] * (v * v) + d1[1] * (2.0 * v * u) + d1[2] * (u * u);
  const auto q2 = d2[0] * v + d2[1] * u;
  const auto numerator = dot(q_u - point, q1);
  const auto denominator = dot(q1, q1) + dot(q_u - point, q2);
  if (std::abs(denominator) <= 1e-12) {
    return u;
  }
  return std::clamp(u - numerator / denominator, 0.0, 1.0);
}

// Recursive Schneider fit of run into segments (appended in order).
void fit_cubic_run(const std::vector<Vec>& run, Vec tangent_start, Vec tangent_end,
                   double error_squared, std::vector<CubicSegment>& segments, int depth) {
  if (run.size() == 2) {
    // A straight corner-to-corner segment keeps collapsed handles (the clean
    // corner-knot form); only tangents that leave the chord need handles.
    const auto chord = normalized(run[1] - run[0]);
    if (dot(tangent_start, chord) > 0.999 && dot(tangent_end, chord * -1.0) > 0.999) {
      segments.push_back({run[0], run[0], run[1], run[1]});
      return;
    }
    const auto handle = length(run[1] - run[0]) / 3.0;
    segments.push_back({run[0], run[0] + tangent_start * handle, run[1] + tangent_end * handle,
                        run[1]});
    return;
  }
  auto u = chord_parameters(run);
  auto segment = generate_bezier(run, u, tangent_start, tangent_end);
  std::size_t split_index = 0;
  auto max_error = max_fit_error(run, u, segment, split_index);
  if (max_error <= error_squared) {
    segments.push_back(segment);
    return;
  }
  // A modest overshoot is often fixable by reparametrizing (Graphics Gems
  // uses error^2 as the attempt gate, but that collapses below the
  // acceptance threshold for sub-pixel tolerances, so keep the gate at
  // least a few times the threshold).
  if (max_error <= std::max(error_squared * error_squared, error_squared * 4.0)) {
    for (int iteration = 0; iteration < 4; ++iteration) {
      for (std::size_t i = 0; i < run.size(); ++i) {
        u[i] = refine_parameter(segment, run[i], u[i]);
      }
      segment = generate_bezier(run, u, tangent_start, tangent_end);
      max_error = max_fit_error(run, u, segment, split_index);
      if (max_error <= error_squared) {
        segments.push_back(segment);
        return;
      }
    }
  }
  if (depth > 32) {  // hard stop: emit what we have rather than recurse forever
    segments.push_back(segment);
    return;
  }
  // Split at the worst point with a smooth center tangent.
  split_index = std::clamp<std::size_t>(split_index, 1, run.size() - 2);
  auto center_tangent = normalized(run[split_index - 1] - run[split_index + 1]);
  if (length(center_tangent) <= 1e-12) {
    center_tangent = normalized(run[split_index - 1] - run[split_index]);
    if (length(center_tangent) <= 1e-12) {
      center_tangent = {1.0, 0.0};
    }
  }
  const std::vector<Vec> left(run.begin(), run.begin() + static_cast<std::ptrdiff_t>(split_index) + 1);
  const std::vector<Vec> right(run.begin() + static_cast<std::ptrdiff_t>(split_index), run.end());
  fit_cubic_run(left, tangent_start, center_tangent, error_squared, segments, depth + 1);
  fit_cubic_run(right, center_tangent * -1.0, tangent_end, error_squared, segments, depth + 1);
}

}  // namespace

double loop_signed_area(const std::vector<FitPoint>& points) {
  double doubled = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    doubled += a.x * b.y - b.x * a.y;
  }
  return doubled * 0.5;
}

PathSubpath fit_closed_loop(const std::vector<FitPoint>& points, double tolerance) {
  PathSubpath subpath;
  subpath.closed = true;
  if (points.size() < 3) {
    return subpath;
  }
  const auto safe_tolerance = std::max(0.1, tolerance);

  // Significant vertices, then corner classification: a kept vertex whose
  // direction change exceeds 60 degrees breaks the curve there.
  const auto kept = douglas_peucker_loop(points, safe_tolerance);
  std::vector<std::size_t> corners;
  constexpr double kCornerCosine = 0.5;  // cos(60 deg)
  for (std::size_t k = 0; k < kept.size(); ++k) {
    const auto previous = kept[(k + kept.size() - 1) % kept.size()];
    const auto current = kept[k];
    const auto next = kept[(k + 1) % kept.size()];
    const auto incoming = normalized(point_vec(points[current]) - point_vec(points[previous]));
    const auto outgoing = normalized(point_vec(points[next]) - point_vec(points[current]));
    if (dot(incoming, outgoing) < kCornerCosine) {
      corners.push_back(current);
    }
  }
  // A fully smooth loop (a circle) still needs seams to fit runs between; use
  // the two Douglas-Peucker seeds and mark the seam anchors smooth.
  bool seams_are_smooth = false;
  if (corners.empty()) {
    seams_are_smooth = true;
    corners.push_back(kept.front());
    if (kept.size() > 1) {
      corners.push_back(kept[kept.size() / 2]);
    }
  }

  const auto count = points.size();
  const auto error_squared = safe_tolerance * safe_tolerance;
  struct FittedRun {
    std::vector<CubicSegment> segments;
  };
  std::vector<FittedRun> runs(corners.size());
  for (std::size_t c = 0; c < corners.size(); ++c) {
    const auto start = corners[c];
    const auto end = corners[(c + 1) % corners.size()];
    // Walk at least one step so a single-corner loop traverses the whole
    // outline back to its start instead of stopping immediately.
    std::vector<Vec> run;
    run.push_back(point_vec(points[start]));
    for (std::size_t index = (start + 1) % count;; index = (index + 1) % count) {
      run.push_back(point_vec(points[index]));
      if (index == end) {
        break;
      }
    }
    Vec tangent_start;
    Vec tangent_end;
    if (seams_are_smooth) {
      // Central-difference tangents at the seams keep them smooth: both
      // adjacent runs receive the same direction (one negated).
      const auto start_prev = (start + count - 1) % count;
      const auto start_next = (start + 1) % count;
      const auto end_prev = (end + count - 1) % count;
      const auto end_next = (end + 1) % count;
      tangent_start = normalized(point_vec(points[start_next]) - point_vec(points[start_prev]));
      tangent_end = normalized(point_vec(points[end_prev]) - point_vec(points[end_next]));
    } else {
      tangent_start = normalized(run[1] - run[0]);
      tangent_end = normalized(run[run.size() - 2] - run[run.size() - 1]);
    }
    if (length(tangent_start) <= 1e-12) {
      tangent_start = {1.0, 0.0};
    }
    if (length(tangent_end) <= 1e-12) {
      tangent_end = {-1.0, 0.0};
    }
    fit_cubic_run(run, tangent_start, tangent_end, error_squared, runs[c].segments, 0);
  }

  // Assemble anchors: each run contributes its start anchor plus the interior
  // split anchors; the incoming run's last segment supplies every start
  // anchor's `in` handle (cyclically).
  for (std::size_t c = 0; c < corners.size(); ++c) {
    const auto& segments = runs[c].segments;
    const auto& incoming = runs[(c + corners.size() - 1) % corners.size()].segments;
    PathAnchor anchor;
    anchor.anchor_x = segments.front().p0.x;
    anchor.anchor_y = segments.front().p0.y;
    anchor.in_x = incoming.back().c2.x;
    anchor.in_y = incoming.back().c2.y;
    anchor.out_x = segments.front().c1.x;
    anchor.out_y = segments.front().c1.y;
    anchor.smooth = seams_are_smooth;
    subpath.anchors.push_back(anchor);
    for (std::size_t s = 0; s + 1 < segments.size(); ++s) {
      PathAnchor split;
      split.anchor_x = segments[s].p3.x;
      split.anchor_y = segments[s].p3.y;
      split.in_x = segments[s].c2.x;
      split.in_y = segments[s].c2.y;
      split.out_x = segments[s + 1].c1.x;
      split.out_y = segments[s + 1].c1.y;
      split.smooth = true;
      subpath.anchors.push_back(split);
    }
  }
  return subpath;
}

}  // namespace patchy
