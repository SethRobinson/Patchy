// Align / Distribute geometry (src/core/layer_alignment.hpp): the deltas the
// Layer > Arrange commands, the Move tool's options-bar buttons, and the
// scripting API all apply. See docs/alignment.md.

#include "core/layer_alignment.hpp"

#include "test_groups.hpp"
#include "test_harness.hpp"

#include <vector>

namespace {

using patchy::AlignEdge;
using patchy::AlignmentOffset;
using patchy::DistributeMode;
using patchy::Rect;

bool offsets_equal(const std::vector<AlignmentOffset>& actual, const std::vector<AlignmentOffset>& expected) {
  if (actual.size() != expected.size()) {
    return false;
  }
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (actual[i].dx != expected[i].dx || actual[i].dy != expected[i].dy) {
      return false;
    }
  }
  return true;
}

void layer_alignment_align_edges_and_centers_round_with_lround() {
  // Three units of different sizes; the reference is their union.
  const std::vector<Rect> units{Rect{10, 10, 20, 20}, Rect{50, 40, 30, 10}, Rect{100, 90, 21, 31}};
  const Rect reference{10, 10, 111, 111};

  CHECK(offsets_equal(patchy::compute_align_deltas(units, reference, AlignEdge::Left),
                      {AlignmentOffset{0, 0}, AlignmentOffset{-40, 0}, AlignmentOffset{-90, 0}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(units, reference, AlignEdge::Right),
                      {AlignmentOffset{91, 0}, AlignmentOffset{41, 0}, AlignmentOffset{0, 0}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(units, reference, AlignEdge::Top),
                      {AlignmentOffset{0, 0}, AlignmentOffset{0, -30}, AlignmentOffset{0, -80}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(units, reference, AlignEdge::Bottom),
                      {AlignmentOffset{0, 91}, AlignmentOffset{0, 71}, AlignmentOffset{0, 0}}));
  // Centers: reference center x = 65.5; unit centers 20, 65, 110.5 -> deltas
  // 45.5, 0.5, -45 rounded away from zero by std::lround (46, 1, -45).
  CHECK(offsets_equal(patchy::compute_align_deltas(units, reference, AlignEdge::HorizontalCenter),
                      {AlignmentOffset{46, 0}, AlignmentOffset{1, 0}, AlignmentOffset{-45, 0}}));
  // reference center y = 65.5; unit centers 20, 45, 105.5 -> 46, 21 (20.5), -40.
  CHECK(offsets_equal(patchy::compute_align_deltas(units, reference, AlignEdge::VerticalCenter),
                      {AlignmentOffset{0, 46}, AlignmentOffset{0, 21}, AlignmentOffset{0, -40}}));

  // Empty units and an empty reference produce no movement.
  CHECK(offsets_equal(patchy::compute_align_deltas({Rect{}, Rect{5, 5, 1, 1}}, reference, AlignEdge::Left),
                      {AlignmentOffset{0, 0}, AlignmentOffset{5, 0}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(units, Rect{}, AlignEdge::Left),
                      {AlignmentOffset{0, 0}, AlignmentOffset{0, 0}, AlignmentOffset{0, 0}}));
  CHECK(patchy::align_edge_is_horizontal(AlignEdge::Left));
  CHECK(patchy::align_edge_is_horizontal(AlignEdge::HorizontalCenter));
  CHECK(!patchy::align_edge_is_horizontal(AlignEdge::Top));
  CHECK(!patchy::align_edge_is_horizontal(AlignEdge::Bottom));
}

void layer_alignment_distribute_keeps_ends_and_spaces_middles() {
  // Input order is not sorted order: the middle unit by position is the first.
  const std::vector<Rect> units{Rect{60, 0, 10, 10}, Rect{10, 0, 10, 10}, Rect{100, 0, 10, 10},
                                Rect{20, 0, 10, 10}};
  // Left edges 10, 20, 60, 100 -> targets 10, 40, 70, 100.
  CHECK(offsets_equal(patchy::compute_distribute_deltas(units, DistributeMode::Left),
                      {AlignmentOffset{10, 0}, AlignmentOffset{0, 0}, AlignmentOffset{0, 0}, AlignmentOffset{20, 0}}));
  // Horizontal centers move the same amount for same-width units.
  CHECK(offsets_equal(patchy::compute_distribute_deltas(units, DistributeMode::HorizontalCenter),
                      {AlignmentOffset{10, 0}, AlignmentOffset{0, 0}, AlignmentOffset{0, 0}, AlignmentOffset{20, 0}}));
  // Vertical features: tops 0, 0, 0, 0 -> nothing moves.
  CHECK(offsets_equal(patchy::compute_distribute_deltas(units, DistributeMode::Top),
                      {AlignmentOffset{}, AlignmentOffset{}, AlignmentOffset{}, AlignmentOffset{}}));
  // Fewer than three units: no movement.
  CHECK(offsets_equal(patchy::compute_distribute_deltas({Rect{0, 0, 5, 5}, Rect{50, 0, 5, 5}}, DistributeMode::Left),
                      {AlignmentOffset{}, AlignmentOffset{}}));
  // Uneven span rounds per unit: lefts 0, 1, 2, 10 -> 0, 3.33 (3), 6.67 (7), 10.
  const std::vector<Rect> uneven{Rect{0, 0, 1, 1}, Rect{1, 0, 1, 1}, Rect{2, 0, 1, 1}, Rect{10, 0, 1, 1}};
  CHECK(offsets_equal(patchy::compute_distribute_deltas(uneven, DistributeMode::Left),
                      {AlignmentOffset{0, 0}, AlignmentOffset{2, 0}, AlignmentOffset{5, 0}, AlignmentOffset{0, 0}}));
  // Vertical bottoms: 5, 15, 40 -> middle bottom to 22.5 -> 23 (delta 8).
  const std::vector<Rect> vertical{Rect{0, 0, 4, 5}, Rect{0, 10, 4, 5}, Rect{0, 30, 4, 10}};
  CHECK(offsets_equal(patchy::compute_distribute_deltas(vertical, DistributeMode::Bottom),
                      {AlignmentOffset{}, AlignmentOffset{0, 8}, AlignmentOffset{}}));
}

void layer_alignment_distribute_spacing_overlaps_when_span_too_small() {
  // Widths 20, 30, 20 between left 10 and right 120: span 110, total 70,
  // gap 20 -> the middle unit lands at 50.
  const std::vector<Rect> units{Rect{10, 0, 20, 10}, Rect{60, 0, 30, 10}, Rect{100, 0, 20, 10}};
  CHECK(offsets_equal(patchy::compute_distribute_deltas(units, DistributeMode::HorizontalSpacing),
                      {AlignmentOffset{}, AlignmentOffset{-10, 0}, AlignmentOffset{}}));
  // Wider than the span: gap (60 - 90) / 2 = -15, so the middle overlaps evenly
  // (target left 0 + 40 - 15 = 25).
  const std::vector<Rect> crowded{Rect{0, 0, 40, 10}, Rect{5, 0, 30, 10}, Rect{40, 0, 20, 10}};
  CHECK(offsets_equal(patchy::compute_distribute_deltas(crowded, DistributeMode::HorizontalSpacing),
                      {AlignmentOffset{}, AlignmentOffset{20, 0}, AlignmentOffset{}}));
  // Vertical spacing: heights 10, 10, 10 between top 0 and bottom 100 -> gap 35,
  // middle top at 45 (from 20: delta 25). Four units share two equal gaps.
  const std::vector<Rect> vertical{Rect{0, 0, 5, 10}, Rect{0, 20, 5, 10}, Rect{0, 90, 5, 10}, Rect{0, 50, 5, 10}};
  // Heights 40 total in a span of 100: gap 20 -> tops 0, 30, 60, 90.
  CHECK(offsets_equal(patchy::compute_distribute_deltas(vertical, DistributeMode::VerticalSpacing),
                      {AlignmentOffset{}, AlignmentOffset{0, 10}, AlignmentOffset{}, AlignmentOffset{0, 10}}));
  CHECK(patchy::distribute_mode_is_horizontal(DistributeMode::HorizontalSpacing));
  CHECK(!patchy::distribute_mode_is_horizontal(DistributeMode::VerticalSpacing));
}

void layer_alignment_single_unit_aligns_to_reference() {
  // One unit against a canvas reference: every edge and center lands exactly.
  const std::vector<Rect> unit{Rect{37, 91, 20, 10}};
  const Rect canvas = Rect::from_size(200, 150);
  CHECK(offsets_equal(patchy::compute_align_deltas(unit, canvas, AlignEdge::Left), {AlignmentOffset{-37, 0}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(unit, canvas, AlignEdge::HorizontalCenter),
                      {AlignmentOffset{53, 0}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(unit, canvas, AlignEdge::Right), {AlignmentOffset{143, 0}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(unit, canvas, AlignEdge::Top), {AlignmentOffset{0, -91}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(unit, canvas, AlignEdge::VerticalCenter),
                      {AlignmentOffset{0, -21}}));
  CHECK(offsets_equal(patchy::compute_align_deltas(unit, canvas, AlignEdge::Bottom), {AlignmentOffset{0, 49}}));
  // Negative (pasteboard) coordinates are legal.
  CHECK(offsets_equal(patchy::compute_align_deltas({Rect{-30, -40, 10, 10}}, canvas, AlignEdge::Left),
                      {AlignmentOffset{30, 0}}));

  // The permanent script identifiers round-trip.
  for (const auto edge : {AlignEdge::Left, AlignEdge::HorizontalCenter, AlignEdge::Right, AlignEdge::Top,
                          AlignEdge::VerticalCenter, AlignEdge::Bottom}) {
    const auto parsed = patchy::align_edge_from_id(patchy::align_edge_id(edge));
    CHECK(parsed.has_value() && *parsed == edge);
  }
  for (const auto mode : {DistributeMode::Left, DistributeMode::HorizontalCenter, DistributeMode::Right,
                          DistributeMode::Top, DistributeMode::VerticalCenter, DistributeMode::Bottom,
                          DistributeMode::HorizontalSpacing, DistributeMode::VerticalSpacing}) {
    const auto parsed = patchy::distribute_mode_from_id(patchy::distribute_mode_id(mode));
    CHECK(parsed.has_value() && *parsed == mode);
  }
  CHECK(patchy::align_edge_id(AlignEdge::HorizontalCenter) == "hcenter");
  CHECK(patchy::distribute_mode_id(DistributeMode::VerticalSpacing) == "vspacing");
  CHECK(!patchy::align_edge_from_id("middle").has_value());
  CHECK(!patchy::distribute_mode_from_id("").has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> layer_alignment_tests() {
  return {
      {"layer_alignment_align_edges_and_centers_round_with_lround",
       layer_alignment_align_edges_and_centers_round_with_lround},
      {"layer_alignment_distribute_keeps_ends_and_spaces_middles",
       layer_alignment_distribute_keeps_ends_and_spaces_middles},
      {"layer_alignment_distribute_spacing_overlaps_when_span_too_small",
       layer_alignment_distribute_spacing_overlaps_when_span_too_small},
      {"layer_alignment_single_unit_aligns_to_reference", layer_alignment_single_unit_aligns_to_reference},
  };
}
