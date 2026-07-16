#include "core/document_path.hpp"
#include "core/layer.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_shape.hpp"

#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

using patchy::DocumentPath;
using patchy::DocumentPathKind;
using patchy::kLiveShapeKappa;
using patchy::Layer;
using patchy::LiveShapeKind;
using patchy::LiveShapeParams;
using patchy::PathAnchor;
using patchy::PathCombineOp;
using patchy::PathSubpath;
using patchy::VectorPath;

bool nearly(double a, double b, double tolerance = 1e-12) {
  return std::fabs(a - b) <= tolerance;
}

VectorPath make_sample_path() {
  VectorPath path;
  path.fill_rule_value = 0;
  path.initial_fill_value = 1;

  PathSubpath closed;
  closed.closed = true;
  closed.op = PathCombineOp::Subtract;
  closed.shape_group = 0;
  PathAnchor smooth;
  smooth.anchor_x = 12.25;
  smooth.anchor_y = -3.5;
  smooth.in_x = 10.0;
  smooth.in_y = -8.125;
  smooth.out_x = 14.75;
  smooth.out_y = 1.0625;
  smooth.smooth = true;
  PathAnchor corner;
  corner.anchor_x = 47.001953125;
  corner.anchor_y = 33.0;
  corner.in_x = corner.anchor_x;
  corner.in_y = corner.anchor_y;
  corner.out_x = corner.anchor_x;
  corner.out_y = corner.anchor_y;
  corner.smooth = false;
  closed.anchors = {smooth, corner};

  PathSubpath open;
  open.closed = false;
  open.op = PathCombineOp::Xor;
  open.shape_group = 1;
  PathAnchor lone;
  lone.anchor_x = 0.1;
  lone.anchor_y = 1e-9;
  lone.in_x = -0.25;
  lone.in_y = 100000.5;
  lone.out_x = 0.3333333333333333;
  lone.out_y = -1e6;
  lone.smooth = true;
  open.anchors = {lone};

  path.subpaths = {closed, open};
  return path;
}

void vector_path_text_codec_round_trips() {
  const auto path = make_sample_path();
  const auto text = patchy::serialize_vector_path(path);
  const auto parsed = patchy::parse_vector_path(text);
  CHECK(parsed.has_value());
  CHECK(*parsed == path);

  CHECK(!patchy::parse_vector_path("").has_value());
  CHECK(!patchy::parse_vector_path("v2 0 0 0").has_value());
  CHECK(!patchy::parse_vector_path("v1 0 0").has_value());
  CHECK(!patchy::parse_vector_path("v1 0 0 1\nS 1 9 0 0").has_value());
  CHECK(!patchy::parse_vector_path(text + " trailing").has_value());
  // Cut mid-record: the final anchor tag survives but its numbers are gone.
  const auto truncated = text.substr(0, text.rfind('A') + 1);
  CHECK(!patchy::parse_vector_path(truncated).has_value());
}

void path_fixed_point_conversion_round_trips() {
  for (const std::int32_t extent : {64, 977, 1920, 30000}) {
    for (const std::int32_t fixed : {0, 1, -1, 0x00a00000, -0x00280000, 0x7fffffff}) {
      const double pixels = patchy::path_coordinate_from_fixed(fixed, extent);
      CHECK(patchy::path_coordinate_to_fixed(pixels, extent) == fixed);
    }
  }
  // The observed encoding: 40 px on a 64 px canvas = 0.625 * 2^24.
  CHECK(patchy::path_coordinate_to_fixed(40.0, 64) == 0x00a00000);
  CHECK(nearly(patchy::path_coordinate_from_fixed(0x00a00000, 64), 40.0));
  // Degenerate extents produce 0 rather than dividing by zero.
  CHECK(patchy::path_coordinate_to_fixed(10.0, 0) == 0);
}

void live_rounded_rect_matches_photoshop_construction() {
  // The probe-live-rect capture: box (8,12)-(52,44), radii TL 4, TR 8,
  // BL 12, BR 16 (docs/vector-tools.md).
  LiveShapeParams params;
  params.kind = LiveShapeKind::RoundedRectangle;
  params.left = 8.0;
  params.top = 12.0;
  params.right = 52.0;
  params.bottom = 44.0;
  params.corner_radii = {4.0, 8.0, 16.0, 12.0};  // TL, TR, BR, BL
  const auto subpaths = patchy::generate_live_shape_subpaths(params);
  CHECK(subpaths.size() == 1);
  const auto& anchors = subpaths[0].anchors;
  CHECK(subpaths[0].closed);
  CHECK(anchors.size() == 8);
  // Knot order and handles exactly as Photoshop wrote them.
  CHECK(nearly(anchors[0].anchor_x, 12.0) && nearly(anchors[0].anchor_y, 12.0));
  CHECK(nearly(anchors[0].in_x, 12.0 - 4.0 * kLiveShapeKappa));
  CHECK(anchors[0].smooth);
  CHECK(nearly(anchors[1].anchor_x, 44.0) && nearly(anchors[1].anchor_y, 12.0));
  CHECK(nearly(anchors[1].out_x, 44.0 + 8.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[2].anchor_x, 52.0) && nearly(anchors[2].anchor_y, 20.0));
  CHECK(nearly(anchors[2].in_y, 20.0 - 8.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[3].anchor_x, 52.0) && nearly(anchors[3].anchor_y, 28.0));
  CHECK(nearly(anchors[3].out_y, 28.0 + 16.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[4].anchor_x, 36.0) && nearly(anchors[4].anchor_y, 44.0));
  CHECK(nearly(anchors[4].in_x, 36.0 + 16.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[5].anchor_x, 20.0) && nearly(anchors[5].anchor_y, 44.0));
  CHECK(nearly(anchors[5].out_x, 20.0 - 12.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[6].anchor_x, 8.0) && nearly(anchors[6].anchor_y, 32.0));
  CHECK(nearly(anchors[6].in_y, 32.0 + 12.0 * kLiveShapeKappa));
  CHECK(nearly(anchors[7].anchor_x, 8.0) && nearly(anchors[7].anchor_y, 16.0));
  CHECK(nearly(anchors[7].out_y, 16.0 - 4.0 * kLiveShapeKappa));
}

void live_ellipse_and_line_match_photoshop_construction() {
  LiveShapeParams ellipse;
  ellipse.kind = LiveShapeKind::Ellipse;
  ellipse.left = 8.0;
  ellipse.top = 12.0;
  ellipse.right = 52.0;
  ellipse.bottom = 44.0;
  const auto ellipse_subpaths = patchy::generate_live_shape_subpaths(ellipse);
  CHECK(ellipse_subpaths.size() == 1);
  const auto& knots = ellipse_subpaths[0].anchors;
  CHECK(knots.size() == 4);
  CHECK(nearly(knots[0].anchor_x, 30.0) && nearly(knots[0].anchor_y, 12.0));
  CHECK(nearly(knots[0].in_x, 30.0 - 22.0 * kLiveShapeKappa));
  CHECK(nearly(knots[0].out_x, 30.0 + 22.0 * kLiveShapeKappa));
  CHECK(nearly(knots[1].anchor_x, 52.0) && nearly(knots[1].anchor_y, 28.0));
  CHECK(nearly(knots[1].in_y, 28.0 - 16.0 * kLiveShapeKappa));
  CHECK(nearly(knots[2].anchor_x, 30.0) && nearly(knots[2].anchor_y, 44.0));
  CHECK(nearly(knots[3].anchor_x, 8.0) && nearly(knots[3].anchor_y, 28.0));

  // The probe-live-line capture: (8,50)->(56,14), width 5 => the quad
  // (9.5,52), (6.5,48), (54.5,12), (57.5,16).
  LiveShapeParams line;
  line.kind = LiveShapeKind::Line;
  line.line_start_x = 8.0;
  line.line_start_y = 50.0;
  line.line_end_x = 56.0;
  line.line_end_y = 14.0;
  line.line_weight = 5.0;
  const auto line_subpaths = patchy::generate_live_shape_subpaths(line);
  CHECK(line_subpaths.size() == 1);
  const auto& quad = line_subpaths[0].anchors;
  CHECK(quad.size() == 4);
  CHECK(nearly(quad[0].anchor_x, 9.5) && nearly(quad[0].anchor_y, 52.0));
  CHECK(nearly(quad[1].anchor_x, 6.5) && nearly(quad[1].anchor_y, 48.0));
  CHECK(nearly(quad[2].anchor_x, 54.5) && nearly(quad[2].anchor_y, 12.0));
  CHECK(nearly(quad[3].anchor_x, 57.5) && nearly(quad[3].anchor_y, 16.0));
  CHECK(!quad[0].smooth);
}

void live_line_arrowheads_generate_heads() {
  LiveShapeParams line;
  line.kind = LiveShapeKind::Line;
  line.index = 3;
  line.line_start_x = 0.0;
  line.line_start_y = 0.0;
  line.line_end_x = 100.0;
  line.line_end_y = 0.0;
  line.line_weight = 4.0;
  line.arrow_end = true;
  line.arrow_width = 12.0;
  line.arrow_length = 16.0;
  line.arrow_concavity = 25;
  const auto subpaths = patchy::generate_live_shape_subpaths(line);
  CHECK(subpaths.size() == 2);
  CHECK(subpaths[0].shape_group == 3);
  CHECK(subpaths[1].shape_group == 3);
  // Shaft shortens so the head has room; the head's tip sits at the endpoint.
  CHECK(nearly(subpaths[0].anchors[2].anchor_x, 84.0));
  const auto& head = subpaths[1].anchors;
  CHECK(head.size() == 4);
  CHECK(nearly(head[0].anchor_x, 100.0) && nearly(head[0].anchor_y, 0.0));
  CHECK(nearly(head[1].anchor_x, 84.0) && nearly(head[1].anchor_y, -6.0));
  CHECK(nearly(head[2].anchor_x, 88.0) && nearly(head[2].anchor_y, 0.0));
  CHECK(nearly(head[3].anchor_x, 84.0) && nearly(head[3].anchor_y, 6.0));

  LiveShapeParams degenerate = line;
  degenerate.line_end_x = 0.0;
  CHECK(patchy::generate_live_shape_subpaths(degenerate).empty());
}

void rounded_rect_zero_and_clamped_radii() {
  LiveShapeParams sharp;
  sharp.kind = LiveShapeKind::RoundedRectangle;
  sharp.left = 1.0;
  sharp.top = 2.0;
  sharp.right = 11.0;
  sharp.bottom = 8.0;
  sharp.corner_radii = {0.0, 0.0, 0.0, 0.0};
  const auto sharp_subpaths = patchy::generate_live_shape_subpaths(sharp);
  CHECK(sharp_subpaths.size() == 1);
  CHECK(sharp_subpaths[0].anchors.size() == 4);
  CHECK(!sharp_subpaths[0].anchors[0].smooth);

  LiveShapeParams clamped = sharp;
  clamped.left = 0.0;
  clamped.top = 0.0;
  clamped.right = 40.0;
  clamped.bottom = 30.0;
  clamped.corner_radii = {50.0, 50.0, 50.0, 50.0};
  const auto clamped_subpaths = patchy::generate_live_shape_subpaths(clamped);
  CHECK(clamped_subpaths.size() == 1);
  // Scale = min(40/100, 30/100) = 0.3 => radii 15 everywhere.
  const auto& anchors = clamped_subpaths[0].anchors;
  CHECK(anchors.size() == 8);
  CHECK(nearly(anchors[0].anchor_x, 15.0) && nearly(anchors[0].anchor_y, 0.0));
  CHECK(nearly(anchors[1].anchor_x, 25.0) && nearly(anchors[1].anchor_y, 0.0));
}

void vector_path_bounds_groups_and_transforms() {
  auto path = make_sample_path();
  CHECK(!path.empty());
  CHECK(path.next_shape_group() == 2);
  const auto bounds = path.bounds();
  CHECK(bounds.has_value());
  // Control points extend the hull beyond anchors.
  CHECK(nearly(bounds->left, -0.25));
  CHECK(nearly(bounds->right, 47.001953125));
  CHECK(nearly(bounds->top, -1e6));
  CHECK(nearly(bounds->bottom, 100000.5));

  VectorPath empty;
  CHECK(empty.empty());
  CHECK(!empty.bounds().has_value());
  CHECK(empty.next_shape_group() == 0);

  VectorPath square;
  PathSubpath subpath;
  subpath.anchors = {PathAnchor{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false},
                     PathAnchor{10.0, 0.0, 10.0, 0.0, 10.0, 0.0, false},
                     PathAnchor{10.0, 10.0, 10.0, 10.0, 10.0, 10.0, false}};
  square.subpaths = {subpath};
  patchy::transform_vector_path(square, {2.0, 0.0, 0.0, 0.5, 3.0, -1.0});
  CHECK(nearly(square.subpaths[0].anchors[1].anchor_x, 23.0));
  CHECK(nearly(square.subpaths[0].anchors[1].anchor_y, -1.0));
  CHECK(nearly(square.subpaths[0].anchors[2].anchor_y, 4.0));
  patchy::translate_vector_path(square, -3.0, 1.0);
  CHECK(nearly(square.subpaths[0].anchors[0].anchor_x, 0.0));
  CHECK(nearly(square.subpaths[0].anchors[0].anchor_y, 0.0));
}

void vector_metadata_flags_round_trip() {
  Layer layer(1, "shape", patchy::PixelBuffer());
  CHECK(!patchy::layer_has_vector_shape_marker(layer));
  CHECK(!patchy::layer_vector_block_dirty(layer));
  CHECK(patchy::vector_lock_reason(layer).empty());

  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  CHECK(patchy::layer_has_vector_shape_marker(layer));
  patchy::mark_layer_vector_block_dirty(layer);
  CHECK(patchy::layer_vector_block_dirty(layer));
  layer.metadata()[patchy::kLayerMetadataVectorLock] = "unparsed";
  CHECK(patchy::vector_lock_reason(layer) == "unparsed");
}

void document_path_revision_and_dirty_semantics() {
  DocumentPath path(7, "Alpha Path", DocumentPathKind::Saved, make_sample_path());
  CHECK(path.id() == 7);
  CHECK(!path.dirty());
  const auto initial_revision = path.content_revision();

  auto payload = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3});
  path.set_resource_source(2000, payload);
  CHECK(!path.dirty());
  CHECK(path.resource_id().has_value() && *path.resource_id() == 2000);
  CHECK(path.content_revision() == initial_revision);

  path.set_name("Alpha Path");  // no-op
  CHECK(!path.dirty());
  path.set_name("Renamed");
  CHECK(path.dirty());
  CHECK(path.content_revision() > initial_revision);

  DocumentPath work(8, "Work Path", DocumentPathKind::Work, VectorPath{});
  const auto before = work.content_revision();
  work.set_clipping_path(true);
  CHECK(work.is_clipping_path());
  CHECK(work.dirty());
  CHECK(work.content_revision() > before);
}

}  // namespace

std::vector<patchy::test::TestCase> vector_shape_tests() {
  return {
      {"vector_path_text_codec_round_trips", vector_path_text_codec_round_trips},
      {"path_fixed_point_conversion_round_trips", path_fixed_point_conversion_round_trips},
      {"live_rounded_rect_matches_photoshop_construction", live_rounded_rect_matches_photoshop_construction},
      {"live_ellipse_and_line_match_photoshop_construction",
       live_ellipse_and_line_match_photoshop_construction},
      {"live_line_arrowheads_generate_heads", live_line_arrowheads_generate_heads},
      {"rounded_rect_zero_and_clamped_radii", rounded_rect_zero_and_clamped_radii},
      {"vector_path_bounds_groups_and_transforms", vector_path_bounds_groups_and_transforms},
      {"vector_metadata_flags_round_trip", vector_metadata_flags_round_trip},
      {"document_path_revision_and_dirty_semantics", document_path_revision_and_dirty_semantics},
  };
}
