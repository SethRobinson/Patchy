// Contour editing model, Photoshop .shc contour files, and the PSD round trip of
// edited contours (GitHub issue 726).
#include "core/contour_editing.hpp"
#include "core/contour_presets.hpp"
#include "core/document.hpp"
#include "core/style_contour.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/shc_io.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::test::read_binary_file;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

patchy::StyleContourPoint contour_point(float x, float y, bool corner = false) {
  patchy::StyleContourPoint point;
  point.x = x;
  point.y = y;
  point.corner = corner;
  return point;
}

// The curve the editor tests and the PSD round trip share: corner and smooth
// points mixed, endpoints off the graph corners.
patchy::StyleContour mixed_contour(std::string name) {
  patchy::StyleContour contour;
  contour.name = std::move(name);
  contour.points = {contour_point(0.0F, 12.0F, true), contour_point(40.0F, 200.0F, true),
                    contour_point(100.0F, 30.0F, false), contour_point(180.0F, 240.0F, true),
                    contour_point(250.0F, 128.0F, false)};
  return contour;
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  append_u16(bytes, static_cast<std::uint16_t>(value >> 16U));
  append_u16(bytes, static_cast<std::uint16_t>(value & 0xFFFFU));
}

void contour_editing_model_adds_moves_removes_and_toggles_corners() {
  // Linear opens as the explicit two-point ramp.
  auto contour = patchy::editable_style_contour(patchy::StyleContour{});
  CHECK(contour.points.size() == 2U);
  CHECK(contour.points.front() == contour_point(0.0F, 0.0F));
  CHECK(contour.points.back() == contour_point(255.0F, 255.0F));

  // Adding rounds to whole levels, keeps input order and reports the index.
  const auto middle = patchy::add_contour_point(contour, 127.6F, 30.2F);
  CHECK(middle.has_value() && *middle == 1U);
  CHECK(contour.points[1] == contour_point(128.0F, 30.0F));
  const auto early = patchy::add_contour_point(contour, 40.0F, 200.0F);
  CHECK(early.has_value() && *early == 1U);
  CHECK(contour.points.size() == 4U && contour.points[2].x == 128.0F);
  // A second point on an occupied input level is refused.
  CHECK(!patchy::add_contour_point(contour, 40.3F, 10.0F).has_value());
  CHECK(!patchy::add_contour_point(contour, 0.0F, 90.0F).has_value());
  CHECK(contour.points.size() == 4U);

  // Moving keeps a point one level clear of its neighbours and inside the graph.
  CHECK(patchy::move_contour_point(contour, 1U, 300.0F, -20.0F));
  CHECK(contour.points[1] == contour_point(127.0F, 0.0F));
  CHECK(patchy::move_contour_point(contour, 1U, -50.0F, 400.0F));
  CHECK(contour.points[1] == contour_point(1.0F, 255.0F));
  CHECK(!patchy::move_contour_point(contour, 1U, 1.2F, 255.0F));  // nothing changed
  // The end points move too; the curve clamps outside them.
  CHECK(patchy::move_contour_point(contour, 3U, 200.0F, 100.0F));
  CHECK(contour.points[3] == contour_point(200.0F, 100.0F));
  CHECK(!patchy::move_contour_point(contour, 9U, 1.0F, 1.0F));

  // Corner flags.
  CHECK(patchy::set_contour_point_corner(contour, 2U, true));
  CHECK(!patchy::set_contour_point_corner(contour, 2U, true));
  CHECK(contour.points[2].corner);
  CHECK(!patchy::set_contour_point_corner(contour, 9U, true));

  // The renderer's own table goes through every point (the editor draws from
  // the same table, so there is one evaluator).
  const auto lut = patchy::build_style_contour_lut(contour);
  for (const auto& point : contour.points) {
    CHECK(lut[static_cast<std::size_t>(point.x)] == static_cast<std::uint8_t>(point.y));
  }
  CHECK(lut[0] == static_cast<std::uint8_t>(contour.points.front().y));
  CHECK(lut[255] == 100U);  // clamped past the moved end point

  // Removing stops at two points.
  CHECK(patchy::remove_contour_point(contour, 1U));
  CHECK(patchy::remove_contour_point(contour, 1U));
  CHECK(contour.points.size() == 2U);
  CHECK(!patchy::remove_contour_point(contour, 0U));
  CHECK(!patchy::remove_contour_point(contour, 5U));

  // The point cap.
  auto full = patchy::editable_style_contour(patchy::StyleContour{});
  for (int index = 1; full.points.size() < patchy::kMaxContourPoints; ++index) {
    CHECK(patchy::add_contour_point(full, static_cast<float>(index * 10), 50.0F).has_value());
  }
  CHECK(!patchy::add_contour_point(full, 201.0F, 50.0F).has_value());

  // Opening a file's curve for editing: clamped, sorted, equal inputs collapse
  // to the last one given, untouched fractional values kept.
  patchy::StyleContour messy;
  messy.name = "Messy";
  messy.points = {contour_point(200.0F, 10.0F), contour_point(-5.0F, 300.0F), contour_point(64.5F, 20.25F, true),
                  contour_point(200.0F, 99.0F, true)};
  const auto opened = patchy::editable_style_contour(messy);
  CHECK(opened.name == "Messy");
  CHECK(opened.points.size() == 3U);
  CHECK(opened.points[0] == contour_point(0.0F, 255.0F));
  CHECK(opened.points[1] == contour_point(64.5F, 20.25F, true));
  CHECK(opened.points[2] == contour_point(200.0F, 99.0F, true));
  CHECK(patchy::build_style_contour_lut(opened) == patchy::build_style_contour_lut(messy));

  // Shape comparison ignores names and treats every Linear form alike.
  CHECK(patchy::style_contours_same_shape(patchy::StyleContour{},
                                          patchy::editable_style_contour(patchy::StyleContour{})));
  auto renamed = mixed_contour("A");
  CHECK(patchy::style_contours_same_shape(renamed, mixed_contour("B")));
  renamed.points[2].corner = true;
  CHECK(!patchy::style_contours_same_shape(renamed, mixed_contour("B")));
}

void shc_write_then_read_round_trips_names_points_and_corners() {
  std::vector<patchy::StyleContour> contours;
  contours.push_back(mixed_contour("Ridge \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xA8"));  // BMP and astral characters
  contours.push_back(patchy::StyleContour{});                                           // Linear, no points
  patchy::StyleContour unnamed = mixed_contour("");
  unnamed.points.resize(2);
  contours.push_back(unnamed);

  const auto bytes = patchy::psd::write_shc(contours);
  CHECK(patchy::psd::write_shc(contours) == bytes);  // deterministic
  std::string error;
  const auto parsed = patchy::psd::read_shc(bytes, error);
  CHECK(parsed.has_value());
  if (!parsed.has_value()) {
    return;
  }
  CHECK(error.empty());
  CHECK(parsed->warnings.empty());
  CHECK(parsed->contours.size() == 3U);
  CHECK(parsed->contours[0] == contours[0]);
  CHECK(parsed->contours[1].name == "Linear");
  CHECK(patchy::style_contour_is_linear(parsed->contours[1]));
  CHECK(parsed->contours[1].points.size() == 2U);
  CHECK(parsed->contours[2].name == "Custom");
  CHECK(patchy::style_contours_same_shape(parsed->contours[2], unnamed));
  // A second trip is byte-stable.
  CHECK(patchy::psd::write_shc(parsed->contours) == patchy::psd::write_shc(parsed->contours));

  // The layout, byte for byte, for one small contour (docs/style-presets-contours.md).
  patchy::StyleContour small;
  small.name = "Ab";
  small.points = {contour_point(0.0F, 255.0F, true), contour_point(129.0F, 7.0F, false), contour_point(255.0F, 64.0F, true)};
  std::vector<std::uint8_t> expected{'8', 'B', 'F', 'S'};
  append_u16(expected, 1U);   // file version
  append_u32(expected, 1U);   // contour count
  append_u32(expected, 2U);   // record version
  append_u32(expected, 3U);   // name units, terminator included
  append_u16(expected, 'A');
  append_u16(expected, 'b');
  append_u16(expected, 0U);
  append_u16(expected, 2U);   // curve version
  append_u16(expected, 3U);   // point count
  for (const auto& pair : std::initializer_list<std::pair<int, int>>{{255, 0}, {7, 129}, {64, 255}}) {  // output, then input
    append_u16(expected, static_cast<std::uint16_t>(pair.first));
    append_u16(expected, static_cast<std::uint16_t>(pair.second));
  }
  expected.insert(expected.end(), {0U, 1U, 0U});  // 1 = smooth, 0 = corner
  expected.insert(expected.end(), 8U, std::uint8_t{0});
  CHECK(patchy::psd::write_shc(std::span<const patchy::StyleContour>(&small, 1)) == expected);

  // Photoshop's own presets name themselves with a localization key.
  const std::string key = "$$$/Contours/Defaults/Ab=A b";
  std::vector<std::uint8_t> keyed_file{'8', 'B', 'F', 'S'};
  append_u16(keyed_file, 1U);
  append_u32(keyed_file, 1U);
  append_u32(keyed_file, 2U);
  append_u32(keyed_file, static_cast<std::uint32_t>(key.size() + 1U));
  for (const auto character : key) {
    append_u16(keyed_file, static_cast<std::uint16_t>(character));
  }
  append_u16(keyed_file, 0U);
  keyed_file.insert(keyed_file.end(), expected.begin() + 24, expected.end());
  const auto keyed_parsed = patchy::psd::read_shc(keyed_file, error);
  CHECK(keyed_parsed.has_value() && keyed_parsed->contours.size() == 1U);
  CHECK(keyed_parsed.has_value() && keyed_parsed->contours[0].name == "A b");
  CHECK(keyed_parsed.has_value() && patchy::style_contours_same_shape(keyed_parsed->contours[0], small));

  // A file for hand testing Load Contours (the desk's test-files folder): three
  // contours this writer made.
  std::vector<patchy::StyleContour> samples(3);
  samples[0].name = "Ridge";
  samples[0].points = {contour_point(0.0F, 0.0F), contour_point(70.0F, 235.0F, true), contour_point(140.0F, 60.0F),
                       contour_point(255.0F, 255.0F)};
  samples[1].name = "Double Peak";
  samples[1].points = {contour_point(0.0F, 0.0F, true), contour_point(64.0F, 255.0F, true),
                       contour_point(128.0F, 40.0F, true), contour_point(192.0F, 255.0F, true),
                       contour_point(255.0F, 0.0F, true)};
  samples[2].name = "Hard Steps";
  samples[2].points = {contour_point(0.0F, 0.0F, true),     contour_point(84.0F, 0.0F, true),
                       contour_point(85.0F, 128.0F, true),  contour_point(169.0F, 128.0F, true),
                       contour_point(170.0F, 255.0F, true), contour_point(255.0F, 255.0F, true)};
  const auto sample_bytes = patchy::psd::write_shc(samples);
  const auto sample_parsed = patchy::psd::read_shc(sample_bytes, error);
  CHECK(sample_parsed.has_value() && sample_parsed->contours == samples);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream sample_file(std::filesystem::path("test-artifacts") / "patchy-contours.shc",
                            std::ios::binary | std::ios::trunc);
  sample_file.write(reinterpret_cast<const char*>(sample_bytes.data()),
                    static_cast<std::streamsize>(sample_bytes.size()));
  CHECK(sample_file.good());

  // Fractional points round to whole levels, and two that land on one input
  // level collapse instead of writing a doubled input.
  patchy::StyleContour fractional;
  fractional.name = "Fractional";
  fractional.points = {contour_point(0.0F, 0.0F), contour_point(10.2F, 99.6F), contour_point(10.4F, 20.0F),
                       contour_point(255.0F, 255.0F)};
  const auto fractional_parsed =
      patchy::psd::read_shc(patchy::psd::write_shc(std::span<const patchy::StyleContour>(&fractional, 1)), error);
  CHECK(fractional_parsed.has_value() && fractional_parsed->contours[0].points.size() == 3U);
  CHECK(fractional_parsed.has_value() && fractional_parsed->contours[0].points[1] == contour_point(10.0F, 20.0F));
}

void shc_reader_rejects_damaged_files() {
  std::string error;
  for (const auto& bytes :
       {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{'8', 'B', 'F', 'S'},
        std::vector<std::uint8_t>{'8', 'B', 'P', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
        std::vector<std::uint8_t>{'8', 'B', 'F', 'S', 0, 1, 0, 0, 0, 0}}) {  // a valid header with no contours
    error.clear();
    CHECK(!patchy::psd::read_shc(bytes, error).has_value());
    CHECK(!error.empty());
  }
  error.clear();
  CHECK(!patchy::psd::read_shc(std::vector<std::uint8_t>{'8', 'B', 'F', 'S', 0, 9, 0, 0, 0, 1, 0, 0}, error).has_value());
  CHECK(error == "Unsupported contour file version");

  const std::vector<patchy::StyleContour> contours{mixed_contour("First"), mixed_contour("Second")};
  const auto good = patchy::psd::write_shc(contours);
  const auto first_only = patchy::psd::write_shc(std::span<const patchy::StyleContour>(contours.data(), 1));
  // Every truncation either fails cleanly or keeps the contour before the cut.
  for (std::size_t size = 0; size < good.size(); ++size) {
    const std::span<const std::uint8_t> cut(good.data(), size);
    error.clear();
    const auto parsed = patchy::psd::read_shc(cut, error);
    if (parsed.has_value()) {
      CHECK(size >= first_only.size());
      CHECK(parsed->contours.size() == 1U && parsed->contours[0] == contours[0]);
      CHECK(!parsed->warnings.empty());
    } else {
      CHECK(size < first_only.size());
      CHECK(!error.empty());
    }
  }
  // A huge count with no data, an oversized name, an oversized point count.
  auto huge = std::vector<std::uint8_t>{'8', 'B', 'F', 'S', 0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
  error.clear();
  CHECK(!patchy::psd::read_shc(huge, error).has_value());
  auto long_name = good;
  long_name[14] = 0x7F;  // name units, high byte
  error.clear();
  CHECK(!patchy::psd::read_shc(long_name, error).has_value());
  auto many_points = first_only;
  const auto point_count_at = 10U + 8U + 6U * 2U + 2U;  // header, record head, "First" + terminator, curve version
  many_points[point_count_at] = 0xFF;
  error.clear();
  CHECK(!patchy::psd::read_shc(many_points, error).has_value());
  // Random damage must not crash or hang.
  std::uint64_t seed = 0x726U;
  for (int round = 0; round < 256; ++round) {
    auto noisy = good;
    for (int flip = 0; flip < 6; ++flip) {
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      const auto at = 6U + static_cast<std::size_t>((seed >> 33U) % (noisy.size() - 6U));
      noisy[at] = static_cast<std::uint8_t>(seed >> 56U);
    }
    error.clear();
    const auto parsed = patchy::psd::read_shc(noisy, error);
    if (parsed.has_value()) {
      for (const auto& contour : parsed->contours) {
        CHECK(contour.points.size() >= 2U);
        for (const auto& point : contour.points) {
          CHECK(point.x >= 0.0F && point.x <= 255.0F && point.y >= 0.0F && point.y <= 255.0F);
        }
      }
    }
  }
  // A contour with one point is skipped; the next still loads.
  std::vector<std::uint8_t> one_point{'8', 'B', 'F', 'S'};
  append_u16(one_point, 1U);
  append_u32(one_point, 2U);
  append_u32(one_point, 2U);
  append_u32(one_point, 1U);
  append_u16(one_point, 0U);
  append_u16(one_point, 2U);
  append_u16(one_point, 1U);
  append_u16(one_point, 9U);
  append_u16(one_point, 9U);
  one_point.push_back(1U);
  one_point.insert(one_point.end(), 8U, std::uint8_t{0});
  one_point.insert(one_point.end(), first_only.begin() + 10, first_only.end());
  error.clear();
  const auto skipped = patchy::psd::read_shc(one_point, error);
  CHECK(skipped.has_value() && skipped->contours.size() == 1U && skipped->contours[0].name == "First");
  CHECK(skipped.has_value() && skipped->warnings.size() == 1U);
  // Values past 255 clamp.
  auto wide = first_only;
  wide[point_count_at + 2U] = 0x7F;  // first point's output, high byte
  error.clear();
  const auto clamped = patchy::psd::read_shc(wide, error);
  CHECK(clamped.has_value() && clamped->contours[0].points.front().y == 255.0F);
}

void shc_reader_reads_photoshop_contours_if_available() {
  // Photoshop 2027's contour set saved over COM after loading three throwaway
  // contours written in this layout (local-test-fixtures/shc/README.txt). The
  // first twelve are Photoshop's own presets, so the file is never committed.
  const auto path = patchy::test::local_format_fixture_path("shc", "photoshop-2027-contours-with-probes.shc");
  patchy::test::skip_unless_exists(path);
  const auto bytes = read_binary_file(path);
  std::string error;
  const auto parsed = patchy::psd::read_shc(bytes, error);
  CHECK(parsed.has_value());
  if (!parsed.has_value()) {
    return;
  }
  CHECK(parsed->warnings.empty());
  CHECK(parsed->contours.size() == 15U);
  CHECK(parsed->contours[0].name == "Linear" && patchy::style_contour_is_linear(parsed->contours[0]));
  CHECK(parsed->contours[1].name == "Cone" && parsed->contours[1].points.size() == 3U);
  CHECK(parsed->contours[1].points[1] == contour_point(129.0F, 255.0F, false));
  // Rounded Steps: corners at the step edges, smooth points between.
  const auto& steps = parsed->contours[10];
  CHECK(steps.name == "Rounded Steps" && steps.points.size() == 9U);
  CHECK(!steps.points[1].corner && steps.points[2].corner && !steps.points[3].corner && steps.points[4].corner);
  CHECK(parsed->contours[11].name == "Sawtooth 1");
  CHECK(parsed->contours[12].name == "Patchy Probe A");
  CHECK(parsed->contours[12].points.size() == 5U);
  CHECK(parsed->contours[12].points[1] == contour_point(40.0F, 200.0F, true));
  CHECK(parsed->contours[12].points[2] == contour_point(100.0F, 30.0F, false));
  CHECK(parsed->contours[13].points.size() == 16U);
  CHECK(parsed->contours[14].name == "Patchy Probe \xE6\x97\xA5\xE6\x9C\xAC");
  // Photoshop saved the three probe contours byte for byte as this layout
  // writes them, so the writer reproduces the tail of Photoshop's file.
  const auto rewritten =
      patchy::psd::write_shc(std::span<const patchy::StyleContour>(parsed->contours.data() + 12, 3));
  CHECK(rewritten.size() > 10U && bytes.size() > rewritten.size());
  CHECK(std::equal(rewritten.begin() + 10, rewritten.end(), bytes.end() - (static_cast<std::ptrdiff_t>(rewritten.size()) - 10)));
}

void psd_edited_contours_with_corner_and_smooth_points_round_trip() {
  // An edited curve on each contour Patchy models: Bevel's Gloss Contour, its
  // Contour sub-option and Drop Shadow's Contour.
  patchy::Document document(96, 72, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(96, 72, 255, 255, 255));
  patchy::Layer styled_layer(document.allocate_layer_id(), "Edited contours", solid_rgba(48, 36, 60, 110, 190, 255));
  auto& layer = document.add_layer(std::move(styled_layer));
  layer.set_bounds(patchy::Rect{24, 18, 48, 36});

  auto gloss = patchy::editable_style_contour(patchy::StyleContour{});
  CHECK(patchy::add_contour_point(gloss, 64.0F, 220.0F).has_value());
  CHECK(patchy::add_contour_point(gloss, 150.0F, 40.0F).has_value());
  CHECK(patchy::set_contour_point_corner(gloss, 1U, true));
  CHECK(patchy::move_contour_point(gloss, 3U, 240.0F, 128.0F));
  gloss.name = patchy::kCustomContourName;

  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.size = 8.0F;
  bevel.gloss_contour = gloss;
  bevel.gloss_anti_aliased = true;
  bevel.contour.enabled = true;
  bevel.contour.contour = mixed_contour("Custom");
  bevel.contour.range = 0.8F;
  layer.layer_style().bevels.push_back(bevel);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 6.0F;
  shadow.size = 6.0F;
  shadow.contour = mixed_contour("My Ridge");
  shadow.contour.points.erase(shadow.contour.points.begin() + 2);
  shadow.anti_aliased = true;
  layer.layer_style().drop_shadows.push_back(shadow);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  CHECK(patchy::psd::DocumentIo::write_layered_rgb8(document) == bytes);
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.layers().size() == 2U);
  const auto found = std::find_if(reread.layers().begin(), reread.layers().end(),
                                  [](const patchy::Layer& candidate) { return candidate.name() == "Edited contours"; });
  CHECK(found != reread.layers().end());
  if (found == reread.layers().end()) {
    return;
  }
  const auto& style = found->layer_style();
  CHECK(style.bevels.size() == 1U && style.drop_shadows.size() == 1U);
  if (style.bevels.size() != 1U || style.drop_shadows.size() != 1U) {
    return;
  }
  CHECK(style.bevels[0].gloss_contour == bevel.gloss_contour);
  CHECK(style.bevels[0].gloss_anti_aliased);
  CHECK(style.bevels[0].contour.enabled);
  CHECK(style.bevels[0].contour.contour == bevel.contour.contour);
  CHECK(style.drop_shadows[0].contour == shadow.contour);
  CHECK(style.drop_shadows[0].anti_aliased);
  // A second save of the reopened document keeps the curves too.
  const auto again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(reread));
  const auto found_again = std::find_if(again.layers().begin(), again.layers().end(),
                                        [](const patchy::Layer& candidate) { return candidate.name() == "Edited contours"; });
  CHECK(found_again != again.layers().end());
  if (found_again != again.layers().end()) {
    CHECK(found_again->layer_style().bevels.size() == 1U && found_again->layer_style().drop_shadows.size() == 1U);
    CHECK(found_again->layer_style().bevels[0].gloss_contour == bevel.gloss_contour);
    CHECK(found_again->layer_style().bevels[0].contour.contour == bevel.contour.contour);
    CHECK(found_again->layer_style().drop_shadows[0].contour == shadow.contour);
  }

  // Kept for the Photoshop open check (scripts\dev\photoshop-open-check.ps1).
  std::filesystem::create_directories("test-artifacts");
  std::ofstream output(std::filesystem::path("test-artifacts") / "psd_edited_contours_round_trip.psd",
                       std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  CHECK(output.good());
}

}  // namespace

std::vector<patchy::test::TestCase> contour_tests() {
  return {
      {"contour_editing_model_adds_moves_removes_and_toggles_corners",
       contour_editing_model_adds_moves_removes_and_toggles_corners},
      {"shc_write_then_read_round_trips_names_points_and_corners",
       shc_write_then_read_round_trips_names_points_and_corners},
      {"shc_reader_rejects_damaged_files", shc_reader_rejects_damaged_files},
      {"shc_reader_reads_photoshop_contours_if_available", shc_reader_reads_photoshop_contours_if_available},
      {"psd_edited_contours_with_corner_and_smooth_points_round_trip",
       psd_edited_contours_with_corner_and_smooth_points_round_trip},
  };
}
