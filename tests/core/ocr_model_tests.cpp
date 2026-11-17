// The model engine's own steps around its networks (docs/ocr-engines.md, "PP-OCR"), on
// synthetic data: the detector's input, rectangles read off a probability map, a line cut
// out for the recognizer, the character list, and CTC decoding. No network runs here.

#include "ocr/ocr_engine.hpp"
#include "ocr/ocr_text_decode.hpp"
#include "ocr/ocr_text_detect.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using patchy::ocr::ImageView;
using patchy::ocr::PixelLayout;
using patchy::ocr::Quad;

bool near(double a, double b, double tolerance) {
  return std::abs(a - b) <= tolerance;
}

struct Gray {
  std::vector<std::uint8_t> pixels;
  int width{0};
  int height{0};
  Gray(int w, int h, std::uint8_t value) : pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), value), width(w), height(h) {}
  void fill(int x0, int y0, int x1, int y1, std::uint8_t value) {
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = value;
      }
    }
  }
  [[nodiscard]] ImageView view() const { return ImageView{pixels.data(), width, height, width, PixelLayout::Gray8}; }
};

struct Map {
  std::vector<float> values;
  int width{0};
  int height{0};
  Map(int w, int h) : values(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0F), width(w), height(h) {}
  void set(int x, int y, float value) {
    if (x >= 0 && y >= 0 && x < width && y < height) {
      values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = value;
    }
  }
  void fill(int x0, int y0, int x1, int y1, float value) {
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        set(x, y, value);
      }
    }
  }
};

double side(const patchy::ocr::Point& a, const patchy::ocr::Point& b) {
  return std::hypot(b.x - a.x, b.y - a.y);
}

void ocr_detector_input_scales_and_pads_with_white() {
  // Small: not scaled, padded to multiples of 32 with white paper.
  Gray small(100, 50, 0);
  const auto input = patchy::ocr::detector_input(small.view());
  CHECK(input.scaled_width == 100 && input.scaled_height == 50);
  CHECK(input.tensor_width == 128 && input.tensor_height == 64);
  CHECK(input.values.size() == 3U * 128U * 64U);
  const std::size_t plane = 128U * 64U;
  // Blue, green, red planes: black inside, white in the padding, each with its own statistics.
  CHECK(near(input.values[0], (0.0 - 0.485) / 0.229, 1e-4));
  CHECK(near(input.values[plane], (0.0 - 0.456) / 0.224, 1e-4));
  CHECK(near(input.values[2 * plane], (0.0 - 0.406) / 0.225, 1e-4));
  CHECK(near(input.values[110], (1.0 - 0.485) / 0.229, 1e-4));             // right of the image
  CHECK(near(input.values[plane - 1], (1.0 - 0.485) / 0.229, 1e-4));       // below it
  CHECK(near(input.values[49U * 128U + 99U], (0.0 - 0.485) / 0.229, 1e-4));  // its last pixel

  // Large: the long side comes down to the cap, the other in proportion; never up.
  Gray large(4000, 2000, 255);
  const auto capped = patchy::ocr::detector_input(large.view());
  CHECK(capped.scaled_width == 1536 && capped.scaled_height == 768);
  CHECK(capped.tensor_width == 1536 && capped.tensor_height == 768);
  Gray tall(700, 2100, 255);
  const auto tall_input = patchy::ocr::detector_input(tall.view(), 1050);
  CHECK(tall_input.scaled_width == 350 && tall_input.scaled_height == 1050);
  CHECK(tall_input.tensor_width == 352 && tall_input.tensor_height == 1056);

  bool threw = false;
  try {
    (void)patchy::ocr::detector_input(ImageView{}, 1536);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void ocr_detected_text_reads_rectangles_off_the_map() {
  Map map(240, 160);
  map.fill(20, 30, 120, 50, 0.9F);    // a level line, 100 x 20
  map.fill(180, 20, 200, 120, 0.8F);  // a column, 20 x 100
  map.fill(30, 100, 60, 110, 0.3F);   // above the pixel threshold, below the box threshold
  map.fill(100, 140, 102, 142, 0.9F); // too small
  const auto found = patchy::ocr::detected_text(map.values, map.width, map.height, 2.0, 3.0);
  CHECK(found.size() == 2);
  if (found.size() != 2) {
    return;
  }
  // In the order their first pixel is met: the column starts higher up.
  const auto& column = found[0];
  const auto& line = found[1];
  // Each side grows by area * 1.4 / perimeter = 100 * 20 * 1.4 / 240.
  const double grow = 100.0 * 20.0 * 1.4 / 240.0;
  CHECK(near(line.angle_degrees, 0.0, 1e-6));
  CHECK(near(line.score, 0.9, 1e-5));
  CHECK(near(line.quad[0].x, (20.0 - grow) * 2.0, 1e-6));
  CHECK(near(line.quad[0].y, (30.0 - grow) * 3.0, 1e-6));
  CHECK(near(line.quad[2].x, (120.0 + grow) * 2.0, 1e-6));
  CHECK(near(line.quad[2].y, (50.0 + grow) * 3.0, 1e-6));
  // The column keeps the upright frame: top left first, taller than wide.
  CHECK(near(column.angle_degrees, 0.0, 1e-6));
  CHECK(near(column.quad[0].x, (180.0 - grow) * 2.0, 1e-6));
  CHECK(near(column.quad[0].y, (20.0 - grow) * 3.0, 1e-6));
  CHECK(near(column.quad[1].x, (200.0 + grow) * 2.0, 1e-6));
  CHECK(near(column.quad[3].y, (120.0 + grow) * 3.0, 1e-6));
  CHECK(side(column.quad[0], column.quad[3]) > 2.0 * side(column.quad[0], column.quad[1]));
  // The reported line is the region itself grown by a quarter of its thickness (its shorter
  // side) on each side, same frame: 5 map pixels here, for the line and for the column.
  CHECK(near(line.line[0].x, (20.0 - 5.0) * 2.0, 1e-6) && near(line.line[0].y, (30.0 - 5.0) * 3.0, 1e-6));
  CHECK(near(line.line[2].x, (120.0 + 5.0) * 2.0, 1e-6) && near(line.line[2].y, (50.0 + 5.0) * 3.0, 1e-6));
  CHECK(near(column.line[0].x, (180.0 - 5.0) * 2.0, 1e-6) && near(column.line[0].y, (20.0 - 5.0) * 3.0, 1e-6));
  CHECK(near(column.line[2].x, (200.0 + 5.0) * 2.0, 1e-6) && near(column.line[2].y, (120.0 + 5.0) * 3.0, 1e-6));

  // A line turned 20 degrees clockwise comes back as one rectangle at that angle, its long
  // side the reading direction.
  Map turned(300, 220);
  const double angle = 20.0 * 3.14159265358979323846 / 180.0;
  const double ux = std::cos(angle);
  const double uy = std::sin(angle);
  for (int y = 0; y < turned.height; ++y) {
    for (int x = 0; x < turned.width; ++x) {
      const double px = x + 0.5 - 150.0;
      const double py = y + 0.5 - 110.0;
      if (std::abs(px * ux + py * uy) <= 90.0 && std::abs(-px * uy + py * ux) <= 12.0) {
        turned.set(x, y, 0.95F);
      }
    }
  }
  const auto rotated = patchy::ocr::detected_text(turned.values, turned.width, turned.height, 1.0, 1.0);
  CHECK(rotated.size() == 1);
  if (rotated.size() == 1) {
    CHECK(near(rotated[0].angle_degrees, 20.0, 1.0));
    const double turned_grow = 180.0 * 24.0 * 1.4 / (2.0 * 204.0);
    CHECK(near(side(rotated[0].quad[0], rotated[0].quad[1]), 180.0 + 2.0 * turned_grow, 3.0));
    CHECK(near(side(rotated[0].quad[0], rotated[0].quad[3]), 24.0 + 2.0 * turned_grow, 3.0));
    const double centre_x = (rotated[0].quad[0].x + rotated[0].quad[2].x) / 2.0;
    const double centre_y = (rotated[0].quad[0].y + rotated[0].quad[2].y) / 2.0;
    CHECK(near(centre_x, 150.0, 1.0) && near(centre_y, 110.0, 1.0));
    // The reported line turns with it: the region (about 180 x 24) a quarter of 24 larger on
    // each side, on the same centre.
    CHECK(near(side(rotated[0].line[0], rotated[0].line[1]), 180.0 + 2.0 * 0.25 * 24.0, 3.0));
    CHECK(near(side(rotated[0].line[0], rotated[0].line[3]), 24.0 + 2.0 * 0.25 * 24.0, 3.0));
    CHECK(near((rotated[0].line[0].x + rotated[0].line[2].x) / 2.0, centre_x, 1e-6));
    CHECK(near((rotated[0].line[0].y + rotated[0].line[2].y) / 2.0, centre_y, 1e-6));
  }

  // Regions that touch only at a corner are one region; the map itself is never widened, so
  // regions a pixel apart stay two.
  Map corners(80, 60);
  corners.fill(10, 10, 30, 20, 0.9F);
  corners.fill(30, 20, 50, 30, 0.9F);
  CHECK(patchy::ocr::detected_text(corners.values, corners.width, corners.height, 1.0, 1.0).size() == 1);
  Map apart(80, 60);
  apart.fill(10, 10, 30, 20, 0.9F);
  apart.fill(10, 21, 30, 31, 0.9F);
  CHECK(patchy::ocr::detected_text(apart.values, apart.width, apart.height, 1.0, 1.0).size() == 2);

  bool threw = false;
  try {
    (void)patchy::ocr::detected_text(map.values, 10, 10, 1.0, 1.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void ocr_line_input_cuts_rectangles_and_turns_columns() {
  // A level line: left half black, right half white, cut at the recognizer's own height.
  Gray level(200, 100, 255);
  level.fill(0, 0, 100, 100, 0);
  const Quad level_quad{{{0.0, 26.0}, {200.0, 26.0}, {200.0, 74.0}, {0.0, 74.0}}};
  const auto line = patchy::ocr::line_input(level.view(), level_quad);
  CHECK(!line.vertical);
  CHECK(line.text_width == 200 && line.tensor_width == 200);
  CHECK(line.values.size() == 3U * 48U * 200U);
  const auto at = [](const patchy::ocr::LineInput& input, int channel, int row, int column) {
    const auto plane = static_cast<std::size_t>(48) * static_cast<std::size_t>(input.tensor_width);
    return input.values[static_cast<std::size_t>(channel) * plane + static_cast<std::size_t>(row) * static_cast<std::size_t>(input.tensor_width) +
                        static_cast<std::size_t>(column)];
  };
  for (int channel = 0; channel < 3; ++channel) {
    CHECK(near(at(line, channel, 24, 10), -1.0, 1e-4));
    CHECK(near(at(line, channel, 24, 190), 1.0, 1e-4));
  }

  // A short line is padded to the minimum width with zeros; a line twice as tall as the
  // input is averaged down, not point sampled.
  Gray checker(80, 96, 255);
  for (int y = 0; y < 96; y += 2) {
    checker.fill(0, y, 80, y + 1, 0);
  }
  const Quad checker_quad{{{0.0, 0.0}, {80.0, 0.0}, {80.0, 96.0}, {0.0, 96.0}}};
  const auto small = patchy::ocr::line_input(checker.view(), checker_quad);
  CHECK(small.text_width == 40 && small.tensor_width == 160);
  CHECK(near(at(small, 0, 20, 20), 0.0, 0.02));   // black and white rows average to mid gray
  CHECK(near(at(small, 0, 20, 100), 0.0, 1e-6));  // padding

  // A column: turned a quarter turn counterclockwise, so its top is at the left of the
  // input and its right edge at the top.
  Gray column(60, 300, 255);
  column.fill(0, 0, 60, 100, 0);       // the top third is black
  column.fill(50, 200, 60, 300, 0);    // a stripe down the right edge of the bottom third
  const Quad column_quad{{{0.0, 0.0}, {60.0, 0.0}, {60.0, 300.0}, {0.0, 300.0}}};
  const auto turned = patchy::ocr::line_input(column.view(), column_quad);
  CHECK(turned.vertical);
  CHECK(turned.text_width == 240 && turned.tensor_width == 240);
  CHECK(near(at(turned, 0, 24, 10), -1.0, 1e-4));   // the column's top
  CHECK(near(at(turned, 0, 24, 120), 1.0, 1e-4));   // its middle
  CHECK(near(at(turned, 0, 2, 230), -1.0, 1e-4));   // the stripe: right edge, now the top rows
  CHECK(near(at(turned, 0, 45, 230), 1.0, 1e-4));

  // RGBA is read over white: a transparent page is white paper.
  std::vector<std::uint8_t> clear(static_cast<std::size_t>(200) * 48U * 4U, 0);
  const ImageView transparent{clear.data(), 200, 48, 200 * 4, PixelLayout::Rgba8};
  const auto over_white =
      patchy::ocr::line_input(transparent, Quad{{{0.0, 0.0}, {200.0, 0.0}, {200.0, 48.0}, {0.0, 48.0}}});
  CHECK(near(at(over_white, 1, 10, 50), 1.0, 1e-4));
}

void ocr_character_list_reads_the_yaml_list_only() {
  const std::string yaml =
      "Global:\n  model_name: test\nPostProcess:\n  name: CTCLabelDecode\n  character_dict:\n"
      "  - '!'\n  - a\n  - ''''\n  - \"\\\\\"\n  - '\"'\n  - \xE6\x97\xA5\n  - ':'\n  -\n"
      "  other_key: 1\n  - not part of the list\n";
  const auto characters = patchy::ocr::character_list_from_yaml(yaml);
  CHECK(characters.size() == 8);
  if (characters.size() == 8) {
    CHECK(characters[0] == "!");
    CHECK(characters[1] == "a");
    CHECK(characters[2] == "'");
    CHECK(characters[3] == "\\");
    CHECK(characters[4] == "\"");
    CHECK(characters[5] == "\xE6\x97\xA5");
    CHECK(characters[6] == ":");
    CHECK(characters[7].empty());
  }
  // Windows line ends read the same.
  CHECK(patchy::ocr::character_list_from_yaml("  character_dict:\r\n  - a\r\n  - b\r\n").size() == 2);
  bool threw = false;
  try {
    (void)patchy::ocr::character_list_from_yaml("Global:\n  model_name: none\n");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

void ocr_ctc_decoding_collapses_repeats_and_places_words() {
  const std::vector<std::string> characters{"a", "b", "c"};
  // Classes: 0 blank, 1 a, 2 b, 3 c, 4 space.
  const std::vector<int> best{1, 1, 0, 1, 2, 0, 4, 4, 3, 3, 0, 0};
  std::vector<float> values(best.size() * 5U, 0.0F);
  for (std::size_t step = 0; step < best.size(); ++step) {
    values[step * 5U + static_cast<std::size_t>(best[step])] = 0.8F;
  }
  const auto line = patchy::ocr::decode_line(values, static_cast<std::int32_t>(best.size()), 5, characters);
  CHECK(line.text == "aab c");
  CHECK(line.characters.size() == 5);
  CHECK(near(line.confidence, 0.8, 1e-6));
  if (line.characters.size() == 5) {
    CHECK(line.characters[0].step == 0 && line.characters[1].step == 3 && line.characters[2].step == 4);
    CHECK(line.characters[3].text == " " && line.characters[3].step == 6);
    CHECK(line.characters[4].step == 8);
  }
  // Two words along a level line of 120 px: the first from the line's start to the middle of
  // the gap before the space, the second from the middle of the gap after it to the end.
  const Quad quad{{{0.0, 0.0}, {120.0, 0.0}, {120.0, 20.0}, {0.0, 20.0}}};
  const auto words = patchy::ocr::words_from_decoded_line(line, 12, 1.0, quad);
  CHECK(words.size() == 2);
  if (words.size() == 2) {
    CHECK(words[0].text == "aab" && words[1].text == "c");
    CHECK(near(words[0].box.x, 0.0, 1e-9));
    CHECK(near(words[0].box.x + words[0].box.width, 120.0 * 5.5 / 12.0, 1e-9));
    CHECK(near(words[1].box.x, 120.0 * 7.5 / 12.0, 1e-9));
    CHECK(near(words[1].box.x + words[1].box.width, 120.0, 1e-9));
    CHECK(near(words[1].box.height, 20.0, 1e-9));
  }
  // The same words moved onto the line's reported rectangle, which starts 10 px in and ends
  // 10 px early, 4 px in from top and bottom: each keeps its place along the line, clamped to
  // the line's ends, and takes the line's edges.
  const Quad inner{{{10.0, 4.0}, {110.0, 4.0}, {110.0, 16.0}, {10.0, 16.0}}};
  const auto moved = patchy::ocr::words_on_line(words, inner);
  CHECK(moved.size() == 2);
  if (moved.size() == 2) {
    CHECK(moved[0].text == "aab" && near(moved[0].box.x, 10.0, 1e-9));
    CHECK(near(moved[0].box.x + moved[0].box.width, 120.0 * 5.5 / 12.0, 1e-9));
    CHECK(near(moved[1].box.x, 120.0 * 7.5 / 12.0, 1e-9) && near(moved[1].box.x + moved[1].box.width, 110.0, 1e-9));
    CHECK(near(moved[0].box.y, 4.0, 1e-9) && near(moved[0].box.height, 12.0, 1e-9));
  }
  // A word wholly past the line's end has no length left and is dropped.
  const Quad short_line{{{0.0, 0.0}, {50.0, 0.0}, {50.0, 20.0}, {0.0, 20.0}}};
  CHECK(patchy::ocr::words_on_line(words, short_line).size() == 1);
  // Half of the input was padding: the steps cover twice the line.
  const auto padded = patchy::ocr::words_from_decoded_line(line, 12, 0.5, quad);
  CHECK(padded.size() == 2 && near(padded[0].box.x + padded[0].box.width, 120.0 * 5.5 / 6.0, 1e-9));

  // Nothing but blanks is an empty line; a class count that does not fit the list is refused.
  const std::vector<float> blanks(4U * 5U, 0.0F);
  CHECK(patchy::ocr::decode_line(blanks, 4, 5, characters).text.empty());
  bool threw = false;
  try {
    (void)patchy::ocr::decode_line(values, static_cast<std::int32_t>(best.size()), 5, {"a", "b"});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

std::vector<patchy::test::TestCase> ocr_model_tests() {
  return {
      {"ocr_detector_input_scales_and_pads_with_white", ocr_detector_input_scales_and_pads_with_white},
      {"ocr_detected_text_reads_rectangles_off_the_map", ocr_detected_text_reads_rectangles_off_the_map},
      {"ocr_line_input_cuts_rectangles_and_turns_columns", ocr_line_input_cuts_rectangles_and_turns_columns},
      {"ocr_character_list_reads_the_yaml_list_only", ocr_character_list_reads_the_yaml_list_only},
      {"ocr_ctc_decoding_collapses_repeats_and_places_words", ocr_ctc_decoding_collapses_repeats_and_places_words},
  };
}
