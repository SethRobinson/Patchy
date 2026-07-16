#include "core/vector_live_shapes.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

PathAnchor corner_anchor(double x, double y) {
  PathAnchor anchor;
  anchor.anchor_x = anchor.in_x = anchor.out_x = x;
  anchor.anchor_y = anchor.in_y = anchor.out_y = y;
  anchor.smooth = false;
  return anchor;
}

// A smooth knot whose handles default to the anchor; callers move in/out.
PathAnchor smooth_anchor(double x, double y) {
  auto anchor = corner_anchor(x, y);
  anchor.smooth = true;
  return anchor;
}

std::vector<PathSubpath> rectangle_subpaths(const LiveShapeParams& params) {
  PathSubpath subpath;
  subpath.closed = true;
  subpath.op = PathCombineOp::Add;
  subpath.shape_group = params.index;
  subpath.anchors = {
      corner_anchor(params.left, params.top),
      corner_anchor(params.right, params.top),
      corner_anchor(params.right, params.bottom),
      corner_anchor(params.left, params.bottom),
  };
  return {std::move(subpath)};
}

std::vector<PathSubpath> rounded_rectangle_subpaths(const LiveShapeParams& params) {
  const double width = params.right - params.left;
  const double height = params.bottom - params.top;
  // Proportional clamp so opposing radii never overlap (per side).
  auto radii = params.corner_radii;  // TL, TR, BR, BL
  for (auto& radius : radii) {
    radius = std::max(0.0, radius);
  }
  double scale = 1.0;
  const auto limit = [&scale](double a, double b, double extent) {
    if (a + b > extent && a + b > 0.0) {
      scale = std::min(scale, extent / (a + b));
    }
  };
  limit(radii[0], radii[1], width);
  limit(radii[3], radii[2], width);
  limit(radii[0], radii[3], height);
  limit(radii[1], radii[2], height);
  for (auto& radius : radii) {
    radius *= scale;
  }
  const double rtl = radii[0];
  const double rtr = radii[1];
  const double rbr = radii[2];
  const double rbl = radii[3];
  if (rtl <= 0.0 && rtr <= 0.0 && rbr <= 0.0 && rbl <= 0.0) {
    return rectangle_subpaths(params);
  }

  PathSubpath subpath;
  subpath.closed = true;
  subpath.op = PathCombineOp::Add;
  subpath.shape_group = params.index;
  auto& anchors = subpath.anchors;

  // Photoshop's construction: knots run clockwise starting at the top-left
  // arc's end on the top edge; each rounded corner contributes its arc start
  // and end knots (smooth, one kappa handle each); the top-left arc's start
  // knot closes the loop. Zero-radius corners collapse to one corner knot.
  const double left = params.left;
  const double top = params.top;
  const double right = params.right;
  const double bottom = params.bottom;

  bool tl_pending_start = false;
  if (rtl > 0.0) {
    // Arc end on the top edge: in-handle reaches back toward the arc.
    auto arc_end = smooth_anchor(left + rtl, top);
    arc_end.in_x = left + rtl - kLiveShapeKappa * rtl;
    arc_end.in_y = top;
    anchors.push_back(arc_end);
    tl_pending_start = true;
  } else {
    anchors.push_back(corner_anchor(left, top));
  }
  if (rtr > 0.0) {
    auto arc_start = smooth_anchor(right - rtr, top);
    arc_start.out_x = right - rtr + kLiveShapeKappa * rtr;
    arc_start.out_y = top;
    anchors.push_back(arc_start);
    auto arc_end = smooth_anchor(right, top + rtr);
    arc_end.in_x = right;
    arc_end.in_y = top + rtr - kLiveShapeKappa * rtr;
    anchors.push_back(arc_end);
  } else {
    anchors.push_back(corner_anchor(right, top));
  }
  if (rbr > 0.0) {
    auto arc_start = smooth_anchor(right, bottom - rbr);
    arc_start.out_x = right;
    arc_start.out_y = bottom - rbr + kLiveShapeKappa * rbr;
    anchors.push_back(arc_start);
    auto arc_end = smooth_anchor(right - rbr, bottom);
    arc_end.in_x = right - rbr + kLiveShapeKappa * rbr;
    arc_end.in_y = bottom;
    anchors.push_back(arc_end);
  } else {
    anchors.push_back(corner_anchor(right, bottom));
  }
  if (rbl > 0.0) {
    auto arc_start = smooth_anchor(left + rbl, bottom);
    arc_start.out_x = left + rbl - kLiveShapeKappa * rbl;
    arc_start.out_y = bottom;
    anchors.push_back(arc_start);
    auto arc_end = smooth_anchor(left, bottom - rbl);
    arc_end.in_x = left;
    arc_end.in_y = bottom - rbl + kLiveShapeKappa * rbl;
    anchors.push_back(arc_end);
  } else {
    anchors.push_back(corner_anchor(left, bottom));
  }
  if (tl_pending_start) {
    auto arc_start = smooth_anchor(left, top + rtl);
    arc_start.out_x = left;
    arc_start.out_y = top + rtl - kLiveShapeKappa * rtl;
    anchors.push_back(arc_start);
  }
  return {std::move(subpath)};
}

std::vector<PathSubpath> ellipse_subpaths(const LiveShapeParams& params) {
  const double cx = (params.left + params.right) / 2.0;
  const double cy = (params.top + params.bottom) / 2.0;
  const double rx = (params.right - params.left) / 2.0;
  const double ry = (params.bottom - params.top) / 2.0;
  const double kx = kLiveShapeKappa * rx;
  const double ky = kLiveShapeKappa * ry;

  PathSubpath subpath;
  subpath.closed = true;
  subpath.op = PathCombineOp::Add;
  subpath.shape_group = params.index;

  auto top = smooth_anchor(cx, params.top);
  top.in_x = cx - kx;
  top.out_x = cx + kx;
  auto right = smooth_anchor(params.right, cy);
  right.in_y = cy - ky;
  right.out_y = cy + ky;
  auto bottom = smooth_anchor(cx, params.bottom);
  bottom.in_x = cx + kx;
  bottom.out_x = cx - kx;
  auto left = smooth_anchor(params.left, cy);
  left.in_y = cy + ky;
  left.out_y = cy - ky;
  subpath.anchors = {top, right, bottom, left};
  return {std::move(subpath)};
}

std::vector<PathSubpath> line_subpaths(const LiveShapeParams& params) {
  const double dx = params.line_end_x - params.line_start_x;
  const double dy = params.line_end_y - params.line_start_y;
  const double length = std::sqrt(dx * dx + dy * dy);
  if (!(length > 0.0)) {
    return {};
  }
  const double dir_x = dx / length;
  const double dir_y = dy / length;
  const double half = std::max(params.line_weight, 0.0) / 2.0;
  // Photoshop's quad: [start+n, start-n, end-n, end+n], n = half * (-dy, dx).
  const double nx = -dir_y * half;
  const double ny = dir_x * half;

  std::vector<PathSubpath> subpaths;

  // Arrowheads shorten the shaft so heads sit tip-at-endpoint.
  double start_x = params.line_start_x;
  double start_y = params.line_start_y;
  double end_x = params.line_end_x;
  double end_y = params.line_end_y;
  const double head_length = params.arrow_length > 0.0 ? params.arrow_length : 0.0;
  if (params.arrow_start && head_length > 0.0) {
    start_x += dir_x * head_length;
    start_y += dir_y * head_length;
  }
  if (params.arrow_end && head_length > 0.0) {
    end_x -= dir_x * head_length;
    end_y -= dir_y * head_length;
  }

  PathSubpath shaft;
  shaft.closed = true;
  shaft.op = PathCombineOp::Add;
  shaft.shape_group = params.index;
  shaft.anchors = {
      corner_anchor(start_x + nx, start_y + ny),
      corner_anchor(start_x - nx, start_y - ny),
      corner_anchor(end_x - nx, end_y - ny),
      corner_anchor(end_x + nx, end_y + ny),
  };
  subpaths.push_back(std::move(shaft));

  const auto append_head = [&](double tip_x, double tip_y, double toward_x, double toward_y) {
    // toward = unit vector from tip back along the shaft.
    const double base_x = tip_x + toward_x * head_length;
    const double base_y = tip_y + toward_y * head_length;
    const double half_width = std::max(params.arrow_width, 0.0) / 2.0;
    const double bx = -toward_y * half_width;
    const double by = toward_x * half_width;
    // Concavity pulls the base midpoint toward the tip (positive) or away
    // (negative); Photoshop stores percent.
    const double concavity = static_cast<double>(params.arrow_concavity) / 100.0;
    const double mid_x = base_x - toward_x * head_length * concavity;
    const double mid_y = base_y - toward_y * head_length * concavity;
    PathSubpath head;
    head.closed = true;
    head.op = PathCombineOp::Add;
    head.shape_group = params.index;
    head.anchors = {
        corner_anchor(tip_x, tip_y),
        corner_anchor(base_x + bx, base_y + by),
        corner_anchor(mid_x, mid_y),
        corner_anchor(base_x - bx, base_y - by),
    };
    subpaths.push_back(std::move(head));
  };
  if (params.arrow_start && head_length > 0.0) {
    append_head(params.line_start_x, params.line_start_y, dir_x, dir_y);
  }
  if (params.arrow_end && head_length > 0.0) {
    append_head(params.line_end_x, params.line_end_y, -dir_x, -dir_y);
  }
  return subpaths;
}

}  // namespace

std::vector<PathSubpath> generate_live_shape_subpaths(const LiveShapeParams& params) {
  switch (params.kind) {
    case LiveShapeKind::Rectangle:
      return rectangle_subpaths(params);
    case LiveShapeKind::RoundedRectangle:
      return rounded_rectangle_subpaths(params);
    case LiveShapeKind::Ellipse:
      return ellipse_subpaths(params);
    case LiveShapeKind::Line:
      return line_subpaths(params);
    case LiveShapeKind::None:
    case LiveShapeKind::Custom:
      break;
  }
  return {};
}

void populate_live_shape_box_corners(LiveShapeParams& params) noexcept {
  params.box_corners = {params.left,  params.top,    params.right, params.top,
                        params.right, params.bottom, params.left,  params.bottom};
}

}  // namespace patchy
