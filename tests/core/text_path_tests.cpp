// Text along a path (docs/path-text.md): the arc-length table and range rules of
// core/text_path.hpp, and the Txt2 path frame read from Photoshop's own files and authored by
// Patchy. The fixtures are Photoshop 2026 captures (scripts/dev/capture-path-text.ps1).
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_area.hpp"
#include "core/text_path.hpp"
#include "psd/engine_data.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_text_engine_block.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using patchy::PathAnchor;
using patchy::TextPathCurve;
using patchy::TextPathRange;
using patchy::VectorPath;

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The Txt2 payload of a PSD: the global tagged block after the layer info.
std::optional<std::vector<std::uint8_t>> text_engine_payload(const std::vector<std::uint8_t>& bytes) {
  static constexpr std::string_view kMarker = "8BIMTxt2";
  const auto it = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
  if (it == bytes.end() || std::distance(it, bytes.end()) < static_cast<std::ptrdiff_t>(kMarker.size() + 4)) {
    return std::nullopt;
  }
  const auto* p = &*(it + static_cast<std::ptrdiff_t>(kMarker.size()));
  const auto length = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                      (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
  const auto start = it + static_cast<std::ptrdiff_t>(kMarker.size() + 4);
  if (std::distance(start, bytes.end()) < static_cast<std::ptrdiff_t>(length)) {
    return std::nullopt;
  }
  return std::vector<std::uint8_t>(start, start + static_cast<std::ptrdiff_t>(length));
}

// anchor, backward handle, forward handle, as the capture script lists them.
PathAnchor knot(double x, double y, double in_x, double in_y, double out_x, double out_y) {
  return PathAnchor{x, y, in_x, in_y, out_x, out_y, false};
}

VectorPath contour(std::vector<PathAnchor> anchors, bool closed) {
  VectorPath path;
  auto& sub = path.subpaths.emplace_back();
  sub.anchors = std::move(anchors);
  sub.closed = closed;
  sub.op = patchy::PathCombineOp::Xor;
  return path;
}

// The capture matrix's contours, in text-local coordinates.
VectorPath arc_path() {
  return contour({knot(0, 300, 0, 300, 120, 0), knot(500, 300, 380, 0, 500, 300)}, false);
}

VectorPath wave_path() {
  return contour({knot(0, 250, 0, 250, 40, 120), knot(150, 150, 110, 150, 260, 150), knot(500, 250, 400, 380, 500, 250)},
                 false);
}

VectorPath ellipse_path() {
  return contour({knot(250, 0, 111.9288, 0, 388.0712, 0), knot(500, 200, 500, 89.543, 500, 310.457),
                  knot(250, 400, 388.0712, 400, 111.9288, 400), knot(0, 200, 0, 310.457, 0, 89.543)},
                 true);
}

bool close_to(double a, double b, double tolerance) {
  return std::abs(a - b) <= tolerance;
}

// Arc lengths measured independently (4000 chords per segment) for the capture contours.
void text_path_curve_measures_arc_length() {
  const TextPathCurve arc(arc_path());
  CHECK(arc.valid());
  CHECK(!arc.closed());
  CHECK(arc.segment_count() == 1);
  CHECK(close_to(arc.length(), 713.548, 0.02));
  CHECK(close_to(arc.distance_at_parameter(0.3), 237.604, 0.02));
  CHECK(close_to(arc.distance_at_parameter(0.9), 623.145, 0.02));
  CHECK(close_to(arc.parameter_at_distance(arc.distance_at_parameter(0.3)), 0.3, 1e-6));

  const TextPathCurve wave(wave_path());
  CHECK(wave.segment_count() == 2);
  CHECK(close_to(wave.length(), 605.915, 0.02));
  CHECK(close_to(wave.distance_at_parameter(0.5), 128.978, 0.02));
  CHECK(close_to(wave.distance_at_parameter(1.5), 411.504, 0.02));

  const TextPathCurve ellipse(ellipse_path());
  CHECK(ellipse.closed());
  CHECK(ellipse.segment_count() == 4);
  CHECK(close_to(ellipse.length(), 1418.283, 0.03));
  CHECK(close_to(ellipse.distance_at_parameter(3.5), 1228.530, 0.03));
  // Past the seam a closed contour adds whole laps, so a range's length is end minus start.
  CHECK(close_to(ellipse.distance_at_parameter(4.0), ellipse.length(), 1e-9));
  CHECK(close_to(ellipse.distance_at_parameter(5.0) - ellipse.distance_at_parameter(3.5),
             ellipse.length() - 1228.530 + ellipse.distance_at_parameter(1.0), 0.03));

  // The top of the arc: its middle, travelling right.
  const auto top = arc.sample(arc.length() / 2.0);
  CHECK(close_to(top.x, 250.0, 0.01));
  CHECK(close_to(top.y, 75.0, 0.01));
  CHECK(close_to(top.tangent_x, 1.0, 1e-6));
  CHECK(close_to(top.tangent_y, 0.0, 1e-6));
  // An open contour runs on straight past its ends; a closed one wraps.
  const auto before = arc.sample(-10.0);
  const auto start = arc.sample(0.0);
  CHECK(close_to(std::hypot(before.x - start.x, before.y - start.y), 10.0, 1e-6));
  CHECK(close_to(before.tangent_x, start.tangent_x, 1e-9));
  const auto wrapped = ellipse.sample(ellipse.length() + 25.0);
  const auto direct = ellipse.sample(25.0);
  CHECK(close_to(wrapped.x, direct.x, 1e-6) && close_to(wrapped.y, direct.y, 1e-6));

  // A straight segment whose handles sit on its anchors has no derivative at its ends; the
  // tangent still points along it.
  const TextPathCurve line(contour({knot(10, 20, 10, 20, 10, 20), knot(110, 20, 110, 20, 110, 20)}, false));
  CHECK(close_to(line.length(), 100.0, 1e-6));
  const auto line_start = line.sample(0.0);
  CHECK(close_to(line_start.tangent_x, 1.0, 1e-9) && close_to(line_start.tangent_y, 0.0, 1e-9));
  const auto quarter = line.sample(25.0);
  // Arc length, not the Bezier parameter (which eases in and out on such a segment), places it.
  CHECK(close_to(quarter.x, 35.0, 0.01) && close_to(quarter.y, 20.0, 1e-9));

  // A point above the arc's top is on the text's side; one below it is not.
  const auto above = arc.nearest(250.0, 55.0);
  CHECK(close_to(above.gap, 20.0, 0.01));
  CHECK(above.left);
  CHECK(close_to(above.distance, arc.length() / 2.0, 0.05));
  CHECK(close_to(above.parameter, 0.5, 1e-3));
  CHECK(!arc.nearest(250.0, 95.0).left);

  // The fold scale multiplies every coordinate.
  CHECK(close_to(TextPathCurve(arc_path(), 2.0).length(), 2.0 * arc.length(), 1e-6));
  CHECK(!TextPathCurve(VectorPath{}).valid());
}

void text_path_ranges_clamp_wrap_and_reverse() {
  using namespace patchy;
  const auto arc = arc_path();
  const auto wave = wave_path();
  const auto ellipse = ellipse_path();
  CHECK(valid_text_path(arc) && valid_text_path(ellipse));
  CHECK(!valid_text_path(VectorPath{}));
  CHECK(text_path_segment_count(arc) == 1);
  CHECK(text_path_segment_count(wave) == 2);
  CHECK(text_path_segment_count(ellipse) == 4);
  CHECK((full_text_path_range(wave) == TextPathRange{0.0, 2.0}));

  // Open: clamped onto the contour; empty, reversed or not finite ranges become all of it.
  CHECK((normalized_text_path_range(wave, {0.5, 1.5}) == TextPathRange{0.5, 1.5}));
  CHECK((normalized_text_path_range(wave, {-0.5, 3.0}) == TextPathRange{0.0, 2.0}));
  CHECK((normalized_text_path_range(wave, {1.5, 0.5}) == TextPathRange{0.0, 2.0}));
  CHECK((normalized_text_path_range(wave, {0.0, std::nan("")}) == TextPathRange{0.0, 2.0}));
  // Closed: the start comes onto the contour and the range may pass the seam, once around at most.
  CHECK((normalized_text_path_range(ellipse, {3.5, 5.0}) == TextPathRange{3.5, 5.0}));
  CHECK((normalized_text_path_range(ellipse, {4.5, 6.0}) == TextPathRange{0.5, 2.0}));
  CHECK((normalized_text_path_range(ellipse, {1.0, 9.0}) == TextPathRange{1.0, 5.0}));
  CHECK((normalized_text_path_range(ellipse, {-1.0, 0.5}) == TextPathRange{3.0, 4.5}));

  const auto text = serialize_text_path_range({0.30000001192093, 0.89999997615814});
  const auto parsed = parse_text_path_range(text);
  CHECK(parsed.has_value());
  CHECK(parsed && parsed->start == 0.30000001192093 && parsed->end == 0.89999997615814);
  CHECK(!parse_text_path_range("nonsense"));
  CHECK(!parse_text_path_range("1"));

  // A flip is the contour walked the other way: the same points at mirrored arc lengths, the
  // range covering the same stretch, and a second flip gives back the original.
  for (const auto* path : {&arc, &wave, &ellipse}) {
    const auto reversed = reversed_text_path(*path);
    CHECK(valid_text_path(reversed));
    CHECK(reversed_text_path(reversed) == *path);
    const TextPathCurve forward(*path);
    const TextPathCurve backward(reversed);
    CHECK(close_to(forward.length(), backward.length(), 1e-6));
    for (const double fraction : {0.1, 0.37, 0.5, 0.82}) {
      const auto a = forward.sample(forward.length() * fraction);
      const auto b = backward.sample(backward.length() * (1.0 - fraction));
      CHECK(close_to(a.x, b.x, 0.02) && close_to(a.y, b.y, 0.02));
      CHECK(close_to(a.tangent_x, -b.tangent_x, 1e-3) && close_to(a.tangent_y, -b.tangent_y, 1e-3));
    }
  }
  const TextPathRange range{0.5, 1.5};
  const auto flipped = reversed_text_path_range(wave, range);
  CHECK((flipped == TextPathRange{0.5, 1.5}));
  CHECK(close_to(reversed_text_path_range(arc, {0.3, 0.9}).start, 0.1, 1e-9));
  CHECK(close_to(reversed_text_path_range(arc, {0.3, 0.9}).end, 0.7, 1e-9));
  const auto wrapped = reversed_text_path_range(ellipse, {3.5, 5.0});
  CHECK(close_to(wrapped.start, 3.0, 1e-9) && close_to(wrapped.end, 4.5, 1e-9));
  const TextPathCurve forward(ellipse);
  const TextPathCurve backward(reversed_text_path(ellipse));
  // The flipped range starts where the original ended.
  const auto original_end = forward.sample(forward.distance_at_parameter(5.0));
  const auto flipped_start = backward.sample(backward.distance_at_parameter(wrapped.start));
  CHECK(close_to(original_end.x, flipped_start.x, 0.02) && close_to(original_end.y, flipped_start.y, 0.02));
}

struct PathFixture {
  const char* name;
  std::size_t anchors;
  bool closed;
  TextPathRange range;
};

// Photoshop's own path-text files open as editable path text and keep their frame through a
// save, untouched and edited.
void text_engine_path_frames_read_photoshop_files() {
  using namespace patchy;
  using namespace patchy::psd;
  const PathFixture fixtures[] = {
      {"photoshop-path-open-curve.psd", 2, false, {0.0, 1.0}},
      {"photoshop-path-wave-range.psd", 3, false, {0.5, 1.5}},
      {"photoshop-path-ellipse-range.psd", 4, true, {1.0, 3.0}},
      {"photoshop-path-cal-arc-range.psd", 2, false, {0.3, 0.9}},
  };
  for (const auto& fixture : fixtures) {
    const auto bytes = read_all(test::committed_psd_fixture_path(fixture.name));
    const auto payload = text_engine_payload(bytes);
    CHECK(payload.has_value());
    if (!payload) continue;
    const auto engine = TextEngineBlock::parse(*payload);
    CHECK(engine.has_value());
    if (!engine) continue;
    const auto geometry = engine->object_geometry(0);
    CHECK(geometry.kind == TextFrameGeometry::Kind::Path);
    CHECK(geometry.path.has_value());
    if (!geometry.path) continue;
    const auto& sub = geometry.path->subpaths.front();
    CHECK(sub.anchors.size() == fixture.anchors);
    CHECK(sub.closed == fixture.closed);
    CHECK(close_to(geometry.path_range.start, fixture.range.start, 1e-5));
    CHECK(close_to(geometry.path_range.end, fixture.range.end, 1e-5));
    if (std::string_view(fixture.name) == "photoshop-path-open-curve.psd") {
      CHECK(*geometry.path == arc_path());
    } else if (std::string_view(fixture.name) == "photoshop-path-wave-range.psd") {
      CHECK(*geometry.path == wave_path());
    } else if (std::string_view(fixture.name) == "photoshop-path-ellipse-range.psd") {
      CHECK(*geometry.path == ellipse_path());
    }

    auto document = DocumentIo::read(bytes);
    auto text = std::find_if(document.layers().begin(), document.layers().end(), [](const Layer& l) {
      return layer_is_text(l);
    });
    CHECK(text != document.layers().end());
    if (text == document.layers().end()) continue;
    CHECK(text_path_for_layer(*text) == geometry.path);
    CHECK(close_to(text_path_range_for_layer(*text, *geometry.path).start, fixture.range.start, 1e-5));
    CHECK(close_to(text_path_range_for_layer(*text, *geometry.path).end, fixture.range.end, 1e-5));
    CHECK(!text_area_for_layer(*text).has_value());
    CHECK(text_layer_has_contour(*text));
    CHECK(!text_geometry_is_protected(*text));
    CHECK(text->metadata().at(kLayerMetadataTextFlow) == "box");
    // The capture puts the text origin at (80, 60); the contour maps there.
    const auto in_document = text_path_in_document(*text);
    CHECK(in_document.has_value());
    if (in_document && std::string_view(fixture.name) == "photoshop-path-open-curve.psd") {
      CHECK(close_to(in_document->subpaths.front().anchors.front().anchor_x, 80.0, 1e-6));
      CHECK(close_to(in_document->subpaths.front().anchors.front().anchor_y, 360.0, 1e-6));
    }

    for (bool edited : {false, true}) {
      if (edited) {
        text->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
        text->metadata()[kLayerMetadataText] = "Edited path text";
      }
      const auto saved = DocumentIo::write_layered_rgb8(document);
      const auto saved_payload = text_engine_payload(saved);
      CHECK(saved_payload.has_value());
      if (!saved_payload) continue;
      const auto saved_engine = TextEngineBlock::parse(*saved_payload);
      CHECK(saved_engine.has_value());
      if (!saved_engine) continue;
      const auto saved_geometry = saved_engine->object_geometry(0);
      CHECK(saved_geometry.kind == TextFrameGeometry::Kind::Path);
      CHECK(saved_geometry.path == geometry.path);
      CHECK(close_to(saved_geometry.path_range.start, fixture.range.start, 1e-5));
      CHECK(close_to(saved_geometry.path_range.end, fixture.range.end, 1e-5));
      const auto frame_index = saved_engine->object_frame_index(0);
      CHECK(frame_index.has_value());
      if (!frame_index) continue;
      // Photoshop's frame, key for key: the type, the range, nothing else.
      const auto index_text = std::to_string(*frame_index);
      const auto* settings = saved_engine->root().at_path({"0", "8", "0", index_text, "0", "2"});
      CHECK(settings != nullptr && settings->is_dict());
      if (settings == nullptr) continue;
      CHECK(settings->dict.size() == 2);
      const auto* type = settings->find("0");
      CHECK(type != nullptr && type->integer() == 2);
      CHECK(settings->find("6") != nullptr);
      CHECK(settings->find("11") == nullptr);
      // And it reads back as the same layer.
      const auto reopened = DocumentIo::read(saved);
      const auto again = std::find_if(reopened.layers().begin(), reopened.layers().end(), [](const Layer& l) {
        return layer_is_text(l);
      });
      CHECK(again != reopened.layers().end());
      if (again == reopened.layers().end()) continue;
      CHECK(text_path_for_layer(*again) == geometry.path);
      CHECK(!text_geometry_is_protected(*again));
      if (edited) {
        CHECK(again->metadata().at(kLayerMetadataText) == "Edited path text");
      }
    }
  }
}

// Photoshop 2026 (27.11) after its own flip gesture: the text's start point dragged across the
// path with the Path Selection tool. That sets a flip flag in Photoshop's live layer data only;
// the text stays on its side and the saved file is ordinary path text with no flip record
// (docs/path-text.md, "Flip"). The text was made in Photoshop's window (Type tool clicked on a
// saved path), so the frame keeps the path in document coordinates behind an affine.
void text_engine_path_frame_after_photoshop_flip_drag_is_plain_path_text() {
  using namespace patchy;
  using namespace patchy::psd;
  const auto bytes = read_all(test::committed_psd_fixture_path("photoshop-path-flip-drag.psd"));
  const auto payload = text_engine_payload(bytes);
  CHECK(payload.has_value());
  if (!payload) return;
  const auto engine = TextEngineBlock::parse(*payload);
  CHECK(engine.has_value());
  if (!engine) return;
  // The frame holds the type, the affine and the range: no key for a flip.
  const auto frame_index = engine->object_frame_index(0);
  CHECK(frame_index.has_value());
  if (!frame_index) return;
  const auto index_text = std::to_string(*frame_index);
  const auto* settings = engine->root().at_path({"0", "8", "0", index_text, "0", "2"});
  CHECK(settings != nullptr && settings->is_dict());
  if (settings == nullptr) return;
  CHECK(settings->dict.size() == 3);
  CHECK(settings->find("0") != nullptr && settings->find("0")->integer() == 2);
  CHECK(settings->find("2") != nullptr && settings->find("2")->is_list() && settings->find("2")->list.size() == 6);
  CHECK(settings->find("6") != nullptr);
  const std::string file(bytes.begin(), bytes.end());
  CHECK(file.find("/Flip") == std::string::npos);

  const auto geometry = engine->object_geometry(0);
  CHECK(geometry.kind == TextFrameGeometry::Kind::Path);
  CHECK(geometry.path.has_value());
  if (!geometry.path) return;
  // The stored arc (80,360) to (580,360), brought into text-local space by the affine (-80,-135).
  const auto expected = contour({knot(0, 225, 0, 225, 120, -75), knot(500, 225, 380, -75, 500, 225)}, false);
  CHECK(*geometry.path == expected);
  CHECK(close_to(geometry.path_range.start, 0.20932, 1e-5));
  CHECK(geometry.path_range.end == 1.0);

  auto document = DocumentIo::read(bytes);
  auto text = std::find_if(document.layers().begin(), document.layers().end(), [](const Layer& l) {
    return layer_is_text(l);
  });
  CHECK(text != document.layers().end());
  if (text == document.layers().end()) return;
  CHECK(text->metadata().at(kLayerMetadataText) == "Lorem Ipsum");
  CHECK(text_path_for_layer(*text) == geometry.path);
  CHECK(!text_geometry_is_protected(*text));
  // In the document the contour is the path Photoshop's Type tool was clicked on, walked the
  // way it was drawn: the flip gesture did not reverse it.
  const auto in_document = text_path_in_document(*text);
  CHECK(in_document.has_value());
  if (in_document) {
    const auto& anchors = in_document->subpaths.front().anchors;
    CHECK(anchors.size() == 2);
    CHECK(close_to(anchors[0].anchor_x, 80.0, 1e-6) && close_to(anchors[0].anchor_y, 360.0, 1e-6));
    CHECK(close_to(anchors[0].out_x, 200.0, 1e-6) && close_to(anchors[0].out_y, 60.0, 1e-6));
    CHECK(close_to(anchors[1].anchor_x, 580.0, 1e-6) && close_to(anchors[1].anchor_y, 360.0, 1e-6));
  }

  // Untouched, the save keeps Photoshop's frame (affine included); edited, Patchy authors the
  // contour in text-local coordinates with no affine. Both read back as the same path text.
  for (bool edited : {false, true}) {
    if (edited) {
      text->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
      text->metadata()[kLayerMetadataText] = "Lorem Ipsum edited";
    }
    const auto saved = DocumentIo::write_layered_rgb8(document);
    const auto saved_payload = text_engine_payload(saved);
    CHECK(saved_payload.has_value());
    if (!saved_payload) continue;
    const auto saved_engine = TextEngineBlock::parse(*saved_payload);
    CHECK(saved_engine.has_value());
    if (!saved_engine) continue;
    const auto saved_geometry = saved_engine->object_geometry(0);
    CHECK(saved_geometry.kind == TextFrameGeometry::Kind::Path);
    CHECK(saved_geometry.path == geometry.path);
    CHECK(close_to(saved_geometry.path_range.start, 0.20932, 1e-5) && saved_geometry.path_range.end == 1.0);
    const auto saved_frame = saved_engine->object_frame_index(0);
    CHECK(saved_frame.has_value());
    if (saved_frame) {
      const auto saved_index = std::to_string(*saved_frame);
      const auto* saved_settings = saved_engine->root().at_path({"0", "8", "0", saved_index, "0", "2"});
      CHECK(saved_settings != nullptr);
      if (saved_settings != nullptr) {
        CHECK((saved_settings->find("2") != nullptr) == !edited);
      }
    }
    const auto reopened = DocumentIo::read(saved);
    const auto again = std::find_if(reopened.layers().begin(), reopened.layers().end(), [](const Layer& l) {
      return layer_is_text(l);
    });
    CHECK(again != reopened.layers().end());
    if (again == reopened.layers().end()) continue;
    CHECK(text_path_for_layer(*again) == geometry.path);
    CHECK(!text_geometry_is_protected(*again));
    const auto again_in_document = text_path_in_document(*again);
    CHECK(again_in_document.has_value() && in_document.has_value());
    if (again_in_document && in_document) {
      CHECK(close_to(again_in_document->subpaths.front().anchors[0].anchor_x, 80.0, 1e-6));
      CHECK(close_to(again_in_document->subpaths.front().anchors[0].anchor_y, 360.0, 1e-6));
    }
  }
}

// Photoshop 2025 (26.9.0) draws a flip and stores it as /0/2/11 << /0 true /22 .025 >> in the
// path frame, with the contour and the range as they were: the text then runs back from the
// range's end on the other side of the line. Patchy reads that as the contour reversed, which
// is what Photoshop 2026 turns the frame into when it opens the file (docs/path-text.md).
void text_engine_path_frames_read_photoshop_2025_flip() {
  using namespace patchy;
  using namespace patchy::psd;
  struct Flipped {
    const char* name;
    VectorPath contour;
    TextPathRange stored;
    TextPathRange read;
  };
  const Flipped fixtures[] = {
      {"photoshop-path-2025-flip-arc.psd", arc_path(), {0.2, 0.9}, {0.1, 0.8}},
      {"photoshop-path-2025-flip-center.psd", arc_path(), {0.2, 0.9}, {0.1, 0.8}},
      {"photoshop-path-2025-flip-cal.psd", arc_path(), {0.3, 0.9}, {0.1, 0.7}},
      {"photoshop-path-2025-flip-ellipse.psd", ellipse_path(), {0.5, 2.5}, {1.5, 3.5}},
  };
  for (const auto& fixture : fixtures) {
    const auto bytes = read_all(test::committed_psd_fixture_path(fixture.name));
    const auto payload = text_engine_payload(bytes);
    CHECK(payload.has_value());
    if (!payload) continue;
    const auto engine = TextEngineBlock::parse(*payload);
    CHECK(engine.has_value());
    if (!engine) continue;
    const auto frame_index = engine->object_frame_index(0);
    CHECK(frame_index.has_value());
    if (!frame_index) continue;
    const auto index_text = std::to_string(*frame_index);
    // As stored: the flip key beside the unreversed contour and range.
    const auto* flip = engine->root().at_path({"0", "8", "0", index_text, "0", "2", "11", "0"});
    CHECK(flip != nullptr && flip->boolean() == true);
    const auto* stored_range = engine->root().at_path({"0", "8", "0", index_text, "0", "2", "6"});
    CHECK(stored_range != nullptr && stored_range->is_list() && stored_range->list.size() == 2);
    if (stored_range != nullptr && stored_range->list.size() == 2) {
      CHECK(close_to(stored_range->list[0].number().value_or(-1.0), fixture.stored.start, 1e-5));
      CHECK(close_to(stored_range->list[1].number().value_or(-1.0), fixture.stored.end, 1e-5));
    }
    // As read: the contour walked the other way and the range that covers the same stretch.
    const auto geometry = engine->object_geometry(0);
    CHECK(geometry.kind == TextFrameGeometry::Kind::Path);
    CHECK(geometry.path.has_value());
    if (!geometry.path) continue;
    CHECK(*geometry.path == reversed_text_path(fixture.contour));
    CHECK(close_to(geometry.path_range.start, fixture.read.start, 1e-5));
    CHECK(close_to(geometry.path_range.end, fixture.read.end, 1e-5));

    auto document = DocumentIo::read(bytes);
    auto text = std::find_if(document.layers().begin(), document.layers().end(), [](const Layer& l) {
      return layer_is_text(l);
    });
    CHECK(text != document.layers().end());
    if (text == document.layers().end()) continue;
    CHECK(text_path_for_layer(*text) == geometry.path);
    CHECK(!text_geometry_is_protected(*text));

    // Untouched, the save keeps Photoshop's frame with its flip key; edited, Patchy writes the
    // reversed contour and no key. Both read back as the same flipped text.
    for (bool edited : {false, true}) {
      if (edited) {
        text->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
        text->metadata()[kLayerMetadataText] = "Edited flipped text";
      }
      const auto saved = DocumentIo::write_layered_rgb8(document);
      const auto saved_payload = text_engine_payload(saved);
      CHECK(saved_payload.has_value());
      if (!saved_payload) continue;
      const auto saved_engine = TextEngineBlock::parse(*saved_payload);
      CHECK(saved_engine.has_value());
      if (!saved_engine) continue;
      const auto saved_frame = saved_engine->object_frame_index(0);
      CHECK(saved_frame.has_value());
      if (!saved_frame) continue;
      const auto saved_index = std::to_string(*saved_frame);
      const auto* saved_options = saved_engine->root().at_path({"0", "8", "0", saved_index, "0", "2", "11"});
      CHECK((saved_options != nullptr) == !edited);
      const auto saved_geometry = saved_engine->object_geometry(0);
      CHECK(saved_geometry.kind == TextFrameGeometry::Kind::Path);
      CHECK(saved_geometry.path == geometry.path);
      CHECK(close_to(saved_geometry.path_range.start, fixture.read.start, 1e-5));
      CHECK(close_to(saved_geometry.path_range.end, fixture.read.end, 1e-5));
      const auto reopened = DocumentIo::read(saved);
      const auto again = std::find_if(reopened.layers().begin(), reopened.layers().end(), [](const Layer& l) {
        return layer_is_text(l);
      });
      CHECK(again != reopened.layers().end());
      if (again == reopened.layers().end()) continue;
      CHECK(text_path_for_layer(*again) == geometry.path);
      CHECK(!text_geometry_is_protected(*again));
    }
  }

  // The other path-type options Photoshop 2025 draws are not modelled: a Skew effect (/11/1 1)
  // leaves the layer protected, with its pixels and native frame kept.
  const auto skew = read_all(test::committed_psd_fixture_path("photoshop-path-2025-skew.psd"));
  const auto skew_payload = text_engine_payload(skew);
  CHECK(skew_payload.has_value());
  if (!skew_payload) return;
  const auto skew_engine = TextEngineBlock::parse(*skew_payload);
  CHECK(skew_engine.has_value());
  if (!skew_engine) return;
  CHECK(skew_engine->object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
  const auto skew_document = DocumentIo::read(skew);
  for (const auto& layer : skew_document.layers()) {
    if (layer_is_text(layer)) {
      CHECK(text_geometry_is_protected(layer));
      CHECK(!text_path_for_layer(layer).has_value());
    }
  }

  // The option dictionary key by key.
  auto engine = TextEngineBlock::from_template();
  TextEngineInputs input;
  input.text = "On a path\r";
  input.boxed = true;
  input.path = arc_path();
  input.path_range = {0.2, 0.9};
  engine.append_object(input);
  auto* settings = engine.root().at_path({"0", "8", "0", "0", "0", "2"});
  CHECK(settings != nullptr);
  if (settings == nullptr) return;
  const auto plain = *settings;
  const auto with_options = [&](EngineNode options) {
    *settings = plain;
    settings->set("11", std::move(options));
    return engine.object_geometry(0);
  };
  const auto unflipped = with_options(engine_dict_of({{"0", engine_bool(false)}, {"22", engine_number(0.025)}}));
  CHECK(unflipped.kind == TextFrameGeometry::Kind::Path && unflipped.path == arc_path());
  CHECK((unflipped.path_range == TextPathRange{0.2, 0.9}));
  const auto flipped = with_options(engine_dict_of({{"0", engine_bool(true)}, {"22", engine_number(0.025)}}));
  CHECK(flipped.kind == TextFrameGeometry::Kind::Path && flipped.path == reversed_text_path(arc_path()));
  CHECK(close_to(flipped.path_range.start, 0.1, 1e-9) && close_to(flipped.path_range.end, 0.8, 1e-9));
  const auto rainbow = with_options(engine_dict_of({{"1", engine_integer(0)}, {"2", engine_integer(0)}}));
  CHECK(rainbow.kind == TextFrameGeometry::Kind::Path);
  CHECK(with_options(engine_dict_of({{"1", engine_integer(1)}})).kind == TextFrameGeometry::Kind::Unsupported);
  CHECK(with_options(engine_dict_of({{"2", engine_integer(2)}})).kind == TextFrameGeometry::Kind::Unsupported);
  CHECK(with_options(engine_dict_of({{"0", engine_integer(1)}})).kind == TextFrameGeometry::Kind::Unsupported);
  CHECK(with_options(engine_dict_of({{"7", engine_integer(0)}})).kind == TextFrameGeometry::Kind::Unsupported);
  CHECK(with_options(engine_list()).kind == TextFrameGeometry::Kind::Unsupported);
}

bool paths_close(const VectorPath& a, const VectorPath& b, double tolerance) {
  if (a.subpaths.size() != 1 || b.subpaths.size() != 1) return false;
  const auto& left = a.subpaths.front();
  const auto& right = b.subpaths.front();
  if (left.closed != right.closed || left.anchors.size() != right.anchors.size()) return false;
  for (std::size_t i = 0; i < left.anchors.size(); ++i) {
    const auto& p = left.anchors[i];
    const auto& q = right.anchors[i];
    if (!close_to(p.anchor_x, q.anchor_x, tolerance) || !close_to(p.anchor_y, q.anchor_y, tolerance) ||
        !close_to(p.in_x, q.in_x, tolerance) || !close_to(p.in_y, q.in_y, tolerance) ||
        !close_to(p.out_x, q.out_x, tolerance) || !close_to(p.out_y, q.out_y, tolerance)) {
      return false;
    }
  }
  return true;
}

// Older Photoshop CC versions write every path frame with /11 << /3 0 /5 0.0 /7 false >> and
// /12 1 (GitHub issue 717). Photoshop 2025 and 2026 lay such a frame out exactly as one without
// the keys, so those values read as plain path text; any other value keeps the layer protected
// (docs/path-text.md, "Older Photoshop CC frames").
void text_engine_path_frames_read_older_photoshop_cc_options() {
  using namespace patchy;
  using namespace patchy::psd;
  auto engine = TextEngineBlock::from_template();
  TextEngineInputs input;
  input.text = "On a path\r";
  input.boxed = true;
  input.path = arc_path();
  input.path_range = {0.2, 0.9};
  engine.append_object(input);
  auto* settings = engine.root().at_path({"0", "8", "0", "0", "0", "2"});
  CHECK(settings != nullptr);
  if (settings == nullptr) return;
  const auto plain = *settings;
  const auto old_options = [](long long align_to, double number, bool flag) {
    return engine_dict_of({{"3", engine_integer(align_to)}, {"5", engine_number(number)}, {"7", engine_bool(flag)}});
  };
  const auto read = [&](std::optional<EngineNode> options, std::optional<EngineNode> key12) {
    *settings = plain;
    if (options) settings->set("11", std::move(*options));
    if (key12) settings->set("12", std::move(*key12));
    return engine.object_geometry(0);
  };
  const auto is_plain_path = [&](const TextFrameGeometry& geometry) {
    return geometry.kind == TextFrameGeometry::Kind::Path && geometry.path == arc_path() &&
           geometry.path_range == TextPathRange{0.2, 0.9};
  };
  const auto unsupported = [](const TextFrameGeometry& geometry) {
    return geometry.kind == TextFrameGeometry::Kind::Unsupported;
  };
  // As the older versions write it, and as Photoshop 2025 (/7 dropped) and Photoshop 2026 (the
  // whole /11 dropped) save the same frame again.
  CHECK(is_plain_path(read(old_options(0, 0.0, false), engine_integer(1))));
  CHECK(is_plain_path(read(engine_dict_of({{"3", engine_integer(0)}, {"5", engine_number(0.0)}}), engine_integer(1))));
  CHECK(is_plain_path(read(std::nullopt, engine_integer(1))));
  CHECK(is_plain_path(read(old_options(0, 0.0, false), std::nullopt)));
  // The flip of those versions is the key Photoshop 2025 writes, beside the older ones.
  auto flipped_options = old_options(0, 0.0, false);
  flipped_options.set("0", engine_bool(true));
  const auto flipped = read(std::move(flipped_options), engine_integer(1));
  CHECK(flipped.kind == TextFrameGeometry::Kind::Path && flipped.path == reversed_text_path(arc_path()));
  CHECK(close_to(flipped.path_range.start, 0.1, 1e-9) && close_to(flipped.path_range.end, 0.8, 1e-9));
  // Any other value stays protected: Patchy has no setting to keep it in.
  CHECK(unsupported(read(old_options(1, 0.0, false), engine_integer(1))));
  CHECK(unsupported(read(old_options(2, 0.0, false), engine_integer(1))));
  CHECK(unsupported(read(old_options(0, 20.0, false), engine_integer(1))));
  CHECK(unsupported(read(old_options(0, -20.0, false), engine_integer(1))));
  CHECK(unsupported(read(old_options(0, 0.0, true), engine_integer(1))));
  CHECK(unsupported(read(old_options(0, 0.0, false), engine_integer(0))));
  CHECK(unsupported(read(old_options(0, 0.0, false), engine_integer(2))));
  CHECK(unsupported(read(old_options(0, 0.0, false), engine_bool(true))));
  CHECK(unsupported(read(engine_dict_of({{"3", engine_bool(false)}}), std::nullopt)));
  CHECK(unsupported(read(engine_dict_of({{"7", engine_integer(0)}}), std::nullopt)));
  CHECK(unsupported(read(std::nullopt, engine_dict())));

  // Through a document: Photoshop's own path text with the older keys added opens editable and an
  // untouched save keeps them; a value Patchy does not model protects the layer.
  auto document = DocumentIo::read_file(test::committed_psd_fixture_path("photoshop-path-flip-drag.psd"));
  const auto text = std::find_if(document.layers().begin(), document.layers().end(), [](const Layer& l) {
    return layer_is_text(l);
  });
  CHECK(text != document.layers().end());
  if (text == document.layers().end()) return;
  const auto expected_path = text_path_for_layer(*text);
  CHECK(expected_path.has_value());
  const auto rewrite_frame = [&](long long align_to) {
    for (auto& block : document.metadata().unknown_psd_resources) {
      if (block.key != "Txt2") continue;
      auto parsed = TextEngineBlock::parse(block.payload);
      CHECK(parsed.has_value());
      if (!parsed) return;
      const auto frame_index = parsed->object_frame_index(0);
      CHECK(frame_index.has_value());
      if (!frame_index) return;
      auto* frame_settings = parsed->root().at_path({"0", "8", "0", std::to_string(*frame_index), "0", "2"});
      CHECK(frame_settings != nullptr);
      if (frame_settings == nullptr) return;
      frame_settings->set("11", old_options(align_to, 0.0, false));
      frame_settings->set("12", engine_integer(1));
      block.payload = parsed->serialize();
    }
    import_text_frame_geometry(document);
  };
  rewrite_frame(1);
  CHECK(text_geometry_is_protected(*text));
  rewrite_frame(0);
  CHECK(!text_geometry_is_protected(*text));
  CHECK(text_path_for_layer(*text) == expected_path);
  const auto saved_payload = text_engine_payload(DocumentIo::write_layered_rgb8(document));
  CHECK(saved_payload.has_value());
  if (!saved_payload) return;
  const auto saved_engine = TextEngineBlock::parse(*saved_payload);
  CHECK(saved_engine.has_value());
  if (!saved_engine) return;
  const auto saved_frame = saved_engine->object_frame_index(0);
  CHECK(saved_frame.has_value());
  if (!saved_frame) return;
  const auto saved_index = std::to_string(*saved_frame);
  const auto* kept_align_to = saved_engine->root().at_path({"0", "8", "0", saved_index, "0", "2", "11", "3"});
  CHECK(kept_align_to != nullptr && kept_align_to->integer() == 0);
  const auto* kept_key12 = saved_engine->root().at_path({"0", "8", "0", saved_index, "0", "2", "12"});
  CHECK(kept_key12 != nullptr && kept_key12->integer() == 1);
  CHECK(saved_engine->object_geometry(0).kind == TextFrameGeometry::Kind::Path);
}

// A reporter's file from an older Photoshop CC (GitHub issue 717): two texts on curves, both
// frames with the older keys and the path in document coordinates behind an affine.
void text_engine_path_frames_read_older_photoshop_cc_file_if_available() {
  using namespace patchy;
  using namespace patchy::psd;
  const auto path = test::local_psd_fixture_path("issue-717/photoshop-cc-text-on-a-path.psd");
  patchy::test::skip_unless_exists(path);
  const auto bytes = read_all(path);
  const auto payload = text_engine_payload(bytes);
  CHECK(payload.has_value());
  if (!payload) return;
  const auto engine = TextEngineBlock::parse(*payload);
  CHECK(engine.has_value());
  if (!engine) return;
  CHECK(engine->object_count() == 2);
  struct Expected {
    const char* text;
    VectorPath contour;  // text-local: the stored contour moved by the frame's affine
    TextPathRange range;
  };
  const Expected expected[] = {
      {"This is a test",
       contour({knot(0, 35.73166, 0, 35.73166, 0, 35.73166), knot(347, 39.73166, 194, -47.26834, 347, 39.73166)}, false),
       {0.0, 0.75545}},
      {"Text on a Path", contour({knot(0, 165, 0, 165, 0, 165), knot(321, 0, 321, 0, 321, 0)}, false), {0.0, 0.56873}},
  };
  // The frames as that Photoshop wrote them.
  for (std::size_t object = 0; object < engine->object_count(); ++object) {
    const auto frame_index = engine->object_frame_index(object);
    CHECK(frame_index.has_value());
    if (!frame_index) continue;
    const auto index_text = std::to_string(*frame_index);
    const auto* options = engine->root().at_path({"0", "8", "0", index_text, "0", "2", "11"});
    CHECK(options != nullptr && options->is_dict() && options->dict.size() == 3);
    const auto* key12 = engine->root().at_path({"0", "8", "0", index_text, "0", "2", "12"});
    CHECK(key12 != nullptr && key12->integer() == 1);
    CHECK(engine->object_geometry(object).kind == TextFrameGeometry::Kind::Path);
  }

  auto document = DocumentIo::read(bytes);
  const auto find_text = [](Document& in, std::string_view text) {
    return std::find_if(in.layers().begin(), in.layers().end(), [&](const Layer& l) {
      const auto found = l.metadata().find(kLayerMetadataText);
      return layer_is_text(l) && found != l.metadata().end() && found->second == text;
    });
  };
  for (const auto& item : expected) {
    const auto layer = find_text(document, item.text);
    CHECK(layer != document.layers().end());
    if (layer == document.layers().end()) continue;
    CHECK(!text_geometry_is_protected(*layer));
    const auto read_path = text_path_for_layer(*layer);
    CHECK(read_path.has_value());
    if (!read_path) continue;
    // Not flipped: the contour runs the way it is stored.
    CHECK(paths_close(*read_path, item.contour, 1e-4));
    const auto range = text_path_range_for_layer(*layer, *read_path);
    CHECK(close_to(range.start, item.range.start, 1e-5) && close_to(range.end, item.range.end, 1e-5));
  }

  // Untouched, the save keeps both of Photoshop's frames with their older keys. With the first
  // layer edited, Patchy authors that one as its own plain path frame and keeps the other.
  for (bool edited : {false, true}) {
    if (edited) {
      const auto layer = find_text(document, expected[0].text);
      if (layer == document.layers().end()) return;
      layer->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
    }
    const auto saved = DocumentIo::write_layered_rgb8(document);
    const auto saved_payload = text_engine_payload(saved);
    CHECK(saved_payload.has_value());
    if (!saved_payload) continue;
    const auto saved_engine = TextEngineBlock::parse(*saved_payload);
    CHECK(saved_engine.has_value());
    if (!saved_engine) continue;
    CHECK(saved_engine->object_count() == 2);
    for (std::size_t object = 0; object < saved_engine->object_count(); ++object) {
      const auto frame_index = saved_engine->object_frame_index(object);
      CHECK(frame_index.has_value());
      if (!frame_index) continue;
      const auto index_text = std::to_string(*frame_index);
      const bool authored = edited && saved_engine->object_text(object) == std::string(expected[0].text) + "\r";
      const auto* key12 = saved_engine->root().at_path({"0", "8", "0", index_text, "0", "2", "12"});
      const auto* options = saved_engine->root().at_path({"0", "8", "0", index_text, "0", "2", "11"});
      CHECK((key12 == nullptr) == authored);
      CHECK((options == nullptr) == authored);
      CHECK(saved_engine->object_geometry(object).kind == TextFrameGeometry::Kind::Path);
    }
    auto reopened = DocumentIo::read(saved);
    for (const auto& item : expected) {
      const auto layer = find_text(reopened, item.text);
      CHECK(layer != reopened.layers().end());
      if (layer == reopened.layers().end()) continue;
      CHECK(!text_geometry_is_protected(*layer));
      const auto read_path = text_path_for_layer(*layer);
      CHECK(read_path.has_value());
      if (!read_path) continue;
      CHECK(paths_close(*read_path, item.contour, 1e-4));
      const auto range = text_path_range_for_layer(*layer, *read_path);
      CHECK(close_to(range.start, item.range.start, 1e-5) && close_to(range.end, item.range.end, 1e-5));
    }
  }
}

void text_engine_path_frames_are_authored_like_photoshops() {
  using namespace patchy;
  using namespace patchy::psd;
  auto engine = TextEngineBlock::from_template();
  TextEngineInputs input;
  input.text = "On a path\r";
  input.boxed = true;  // path text keeps the box flow, like area text
  input.box_width = 500.0;
  input.box_height = 300.0;
  input.path = wave_path();
  input.path_range = {0.5, 1.5};
  engine.append_object(input);
  const auto geometry = engine.object_geometry(0);
  CHECK(geometry.kind == TextFrameGeometry::Kind::Path);
  CHECK(geometry.path == input.path);
  CHECK(geometry.path_range == input.path_range);
  // Eight numbers per cubic segment: start, outgoing handle, incoming handle, end.
  const auto* coords = engine.root().at_path({"0", "8", "0", "0", "0", "1", "0"});
  CHECK(coords != nullptr && coords->is_list() && coords->list.size() == 16);
  if (coords != nullptr && coords->list.size() == 16) {
    const double expected[16] = {0, 250, 40, 120, 110, 150, 150, 150, 150, 150, 260, 150, 400, 380, 500, 250};
    for (std::size_t i = 0; i < 16; ++i) {
      CHECK(coords->list[i].number() == expected[i]);
    }
  }
  const auto serialized = engine.serialize();
  const std::string text(serialized.begin(), serialized.end());
  CHECK(text.find("/2 << /0 2 /6 [ .5 1.5 ] >>") != std::string::npos);

  // A closed contour ends where it starts; the frame has no closed flag.
  input.path = ellipse_path();
  input.path_range = {3.5, 5.0};
  engine.set_object(0, input);
  const auto closed = engine.object_geometry(0);
  CHECK(closed.kind == TextFrameGeometry::Kind::Path);
  CHECK(closed.path == input.path);
  CHECK(closed.path_range == input.path_range);

  // Anything Patchy has not seen in a path frame leaves the layer protected: vertical type's
  // key, a reversed range, a contour that is not whole segments or does not join up.
  auto* settings = engine.root().at_path({"0", "8", "0", "0", "0", "2"});
  CHECK(settings != nullptr);
  if (settings == nullptr) return;
  const auto original = *settings;
  settings->set("1", engine_integer(2));
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
  *settings = original;
  settings->set("6", engine_list_of({engine_number(2.0), engine_number(1.0)}));
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
  *settings = original;
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Path);
  auto* contour_list = engine.root().at_path({"0", "8", "0", "0", "0", "1", "0"});
  CHECK(contour_list != nullptr);
  if (contour_list == nullptr) return;
  const auto original_contour = *contour_list;
  contour_list->list.pop_back();
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
  *contour_list = original_contour;
  contour_list->list[8] = engine_number(999.0);  // the second segment no longer starts where the first ends
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
}

void text_engine_path_text_round_trips_and_refuses_legacy_engines() {
  using namespace patchy;
  using namespace patchy::psd;
  // A Patchy-born path-text layer: written as a path frame, read back as the same layer.
  Document document(660, 520, PixelFormat::rgb8());
  Layer layer(document.allocate_layer_id(), "Path text", PixelBuffer(200, 60, PixelFormat::rgba8()));
  layer.set_bounds(Rect{80, 60, 200, 60});
  layer.metadata()[kLayerMetadataText] = "Along the wave";
  layer.metadata()[kLayerMetadataTextFont] = "Arial";
  layer.metadata()[kLayerMetadataTextSize] = "24";
  layer.metadata()[kLayerMetadataTextColor] = "#202020";
  layer.metadata()[kLayerMetadataTextBold] = "false";
  layer.metadata()[kLayerMetadataTextItalic] = "false";
  layer.metadata()[kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
  layer.metadata()[kLayerMetadataTextFlow] = "box";
  layer.metadata()[kLayerMetadataTextBoxWidth] = "500";
  layer.metadata()[kLayerMetadataTextBoxHeight] = "250";
  layer.metadata()[kLayerMetadataTextTransform] = serialize_layer_affine_transform({1.0, 0.0, 0.0, 1.0, 80.0, 60.0});
  layer.metadata()[kLayerMetadataTextPath] = serialize_vector_path(wave_path());
  layer.metadata()[kLayerMetadataTextPathRange] = serialize_text_path_range({0.25, 1.75});
  document.add_layer(std::move(layer));

  const auto saved = DocumentIo::write_layered_rgb8(document);
  const auto payload = text_engine_payload(saved);
  CHECK(payload.has_value());
  if (!payload) return;
  const auto engine = TextEngineBlock::parse(*payload);
  CHECK(engine.has_value());
  if (!engine) return;
  CHECK(engine->object_count() == 1);
  const auto geometry = engine->object_geometry(0);
  CHECK(geometry.kind == TextFrameGeometry::Kind::Path);
  CHECK(geometry.path == wave_path());
  CHECK((geometry.path_range == TextPathRange{0.25, 1.75}));
  // The TySh records a box holding the contour's bounds, as Photoshop's does.
  const std::string file(saved.begin(), saved.end());
  CHECK(file.find("/ShapeType 1") != std::string::npos);

  auto reopened = DocumentIo::read(saved);
  auto text = std::find_if(reopened.layers().begin(), reopened.layers().end(), [](const Layer& l) {
    return layer_is_text(l);
  });
  CHECK(text != reopened.layers().end());
  if (text == reopened.layers().end()) return;
  CHECK(text_path_for_layer(*text) == wave_path());
  CHECK((text_path_range_for_layer(*text, wave_path()) == TextPathRange{0.25, 1.75}));
  CHECK(!text_geometry_is_protected(*text));
  CHECK(text->metadata().at(kLayerMetadataText) == "Along the wave");
  // A second save of the reopened file writes the same frame.
  const auto again = TextEngineBlock::parse(*text_engine_payload(DocumentIo::write_layered_rgb8(reopened)));
  CHECK(again.has_value());
  CHECK(again && again->object_geometry(0).path == wave_path());

  // Clearing the text metadata drops the path with it.
  clear_layer_text_metadata(*text);
  CHECK(!text->metadata().contains(kLayerMetadataTextPath));
  CHECK(!text->metadata().contains(kLayerMetadataTextPathRange));

  // An engine Patchy cannot author into must not silently lose a path: the writer refuses
  // before producing file bytes, as it does for area text.
  auto legacy = DocumentIo::read_file(test::committed_psd_fixture_path("photoshop-path-open-curve.psd"));
  for (auto& block : legacy.metadata().unknown_psd_resources) {
    if (block.key == "Txt2") {
      const std::string named = "/DocumentResources << >> /DocumentObjects << >>";
      block.payload.assign(named.begin(), named.end());
    }
  }
  bool refused = false;
  try {
    (void)DocumentIo::write_layered_rgb8(legacy);
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);
}

}  // namespace

std::vector<patchy::test::TestCase> text_path_tests() {
  return {
      {"text_path_curve_measures_arc_length", text_path_curve_measures_arc_length},
      {"text_path_ranges_clamp_wrap_and_reverse", text_path_ranges_clamp_wrap_and_reverse},
      {"text_engine_path_frames_read_photoshop_files", text_engine_path_frames_read_photoshop_files},
      {"text_engine_path_frame_after_photoshop_flip_drag_is_plain_path_text",
       text_engine_path_frame_after_photoshop_flip_drag_is_plain_path_text},
      {"text_engine_path_frames_read_photoshop_2025_flip", text_engine_path_frames_read_photoshop_2025_flip},
      {"text_engine_path_frames_read_older_photoshop_cc_options",
       text_engine_path_frames_read_older_photoshop_cc_options},
      {"text_engine_path_frames_read_older_photoshop_cc_file_if_available",
       text_engine_path_frames_read_older_photoshop_cc_file_if_available},
      {"text_engine_path_frames_are_authored_like_photoshops", text_engine_path_frames_are_authored_like_photoshops},
      {"text_engine_path_text_round_trips_and_refuses_legacy_engines",
       text_engine_path_text_round_trips_and_refuses_legacy_engines},
  };
}
