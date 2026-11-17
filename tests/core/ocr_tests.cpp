// Text recognition library (docs/ocr.md): the engine registry and its test hooks, the
// fixed rule that reads oversized images in bands, pixel preparation, block grouping, and
// the scripted fake engine the UI suites drive.

#include "ocr/ocr_bands.hpp"
#include "ocr/ocr_engine.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr/ocr_image.hpp"
#include "ocr/ocr_layout.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using patchy::ocr::Box;
using patchy::ocr::FakeEngineConfig;
using patchy::ocr::FakeLine;
using patchy::ocr::ImageView;
using patchy::ocr::PixelLayout;

// Restores the real engine list when a test ends, however it ends.
struct EngineListGuard {
  explicit EngineListGuard(std::vector<std::shared_ptr<patchy::ocr::Engine>> list) {
    patchy::ocr::set_engines_for_testing([list = std::move(list)] { return list; });
  }
  ~EngineListGuard() { patchy::ocr::set_engines_for_testing({}); }
  EngineListGuard(const EngineListGuard&) = delete;
  EngineListGuard& operator=(const EngineListGuard&) = delete;
};

struct GrayImage {
  std::vector<std::uint8_t> pixels;
  int width{0};
  int height{0};
  [[nodiscard]] ImageView view() const {
    return ImageView{pixels.data(), width, height, width, PixelLayout::Gray8};
  }
};

GrayImage white_image(int width, int height) {
  return GrayImage{std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 255),
                   width, height};
}

bool near(double a, double b, double tolerance = 1e-6) {
  return a > b - tolerance && a < b + tolerance;
}

patchy::ocr::Line line_at(double x, double y, double width, double height, bool vertical = false) {
  patchy::ocr::Line line;
  line.box = Box{x, y, width, height};
  line.quad = patchy::ocr::quad_from_box(line.box);
  line.vertical = vertical;
  line.text = "x";
  return line;
}

void ocr_registry_uses_installed_test_engines() {
  {
    // "No engine on this system" is an installed empty list, not the real one.
    const EngineListGuard none({});
    CHECK(patchy::ocr::engines().empty());
    CHECK(patchy::ocr::find_engine("fake") == nullptr);
  }
  FakeEngineConfig config;
  config.languages = {{"en-US", "English (United States)"}, {"ja", "Japanese"}};
  const auto fake = patchy::ocr::make_fake_engine(config);
  const EngineListGuard guard({fake});
  const auto listed = patchy::ocr::engines();
  CHECK(listed.size() == 1);
  CHECK(patchy::ocr::find_engine("fake") == fake);
  CHECK(patchy::ocr::find_engine("windows") == nullptr);
  CHECK(fake->unavailable() == patchy::ocr::Unavailable::None);
  CHECK(fake->unavailable_reason().empty());
  CHECK(fake->default_language() == "en-US");
  CHECK(fake->languages().size() == 2);
  CHECK(fake->open("ja") != nullptr);
  CHECK(fake->open_count() == 1);
  bool refused = false;
  try {
    (void)fake->open("xx");
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);

  // An engine without a language says so and cannot be opened.
  FakeEngineConfig bare;
  bare.languages.clear();
  const auto empty = patchy::ocr::make_fake_engine(bare);
  CHECK(empty->unavailable() == patchy::ocr::Unavailable::NoLanguages);
  CHECK(!empty->unavailable_reason().empty());
  CHECK(empty->default_language().empty());
  refused = false;
  try {
    (void)empty->open("en-US");
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);
}

void ocr_band_plan_is_pinned() {
  // No limit, or an image inside the limit: one band, no scaling.
  auto plan = patchy::ocr::plan_bands(4000, 9000, 0);
  CHECK(plan.scaled_width == 4000 && plan.scaled_height == 9000 && plan.bands.size() == 1 && plan.overlap == 0);
  plan = patchy::ocr::plan_bands(2600, 2600, 2600);
  CHECK(plan.scaled_width == 2600 && plan.scaled_height == 2600 && plan.bands.size() == 1);
  CHECK(plan.bands[0].start == 0 && plan.bands[0].end == 2600);

  // A Letter page at 300 ppi against the Windows engine's 2600: the width fits, so no
  // scaling, and two bands of 2600 rows that overlap by 325; the last ends at the bottom.
  plan = patchy::ocr::plan_bands(2550, 3300, 2600);
  CHECK(plan.scaled_width == 2550 && plan.scaled_height == 3300);
  CHECK(plan.overlap == 325);
  CHECK(plan.bands.size() == 2);
  CHECK(plan.bands[0].start == 0 && plan.bands[0].end == 2600);
  CHECK(plan.bands[1].start == 700 && plan.bands[1].end == 3300);

  // The same page at 600 ppi: scaled to the limit's width (height rounded to nearest),
  // then banded.
  plan = patchy::ocr::plan_bands(5100, 6600, 2600);
  CHECK(plan.scaled_width == 2600 && plan.scaled_height == 3365);
  CHECK(plan.bands.size() == 2);
  CHECK(plan.bands[1].start == 765 && plan.bands[1].end == 3365);

  // A long strip: every band but the last steps by M - V, and consecutive bands overlap
  // by at least V.
  plan = patchy::ocr::plan_bands(1000, 10000, 2600);
  CHECK(plan.bands.size() == 5);
  for (std::size_t index = 0; index + 1 < plan.bands.size(); ++index) {
    CHECK(plan.bands[index].start == static_cast<int>(index) * 2275);
    CHECK(plan.bands[index].end - plan.bands[index + 1].start >= plan.overlap);
    CHECK(plan.bands[index].end - plan.bands[index].start == 2600);
  }
  CHECK(plan.bands.back().end == 10000 && plan.bands.back().start == 7400);

  // A wide, short image only scales.
  plan = patchy::ocr::plan_bands(8000, 1000, 2600);
  CHECK(plan.scaled_width == 2600 && plan.scaled_height == 325 && plan.bands.size() == 1);

  // A tiny limit still makes progress.
  plan = patchy::ocr::plan_bands(10, 100, 8);
  CHECK(plan.scaled_width == 8 && plan.scaled_height == 80);
  CHECK(plan.overlap == 4);
  CHECK(!plan.bands.empty() && plan.bands.front().start == 0 && plan.bands.back().end == 80);
}

void ocr_bands_report_overlap_lines_once() {
  // 1000 x 6000 against a limit of 2600: bands [0,2600), [2275,4875), [3400,6000). Lines
  // sit in both overlaps and on both sides of each ownership cut (2437.5 and 4137.5).
  FakeEngineConfig config;
  config.max_dimension = 2600;
  const std::vector<double> tops = {100, 2300, 2420, 2440, 2500, 3000, 3500, 4100, 4130, 4200, 4800, 5900};
  for (std::size_t index = 0; index < tops.size(); ++index) {
    config.lines.push_back(FakeLine{Box{50, tops[index], 600, 20}, "line " + std::to_string(index), false});
  }
  const auto fake = patchy::ocr::make_fake_engine(config);
  const auto image = white_image(1000, 6000);
  const auto result = fake->open("en-US")->recognize(image.view());
  CHECK(result.lines.size() == tops.size());
  for (std::size_t index = 0; index < std::min(result.lines.size(), tops.size()); ++index) {
    // In reading order, each exactly once, in the original image's pixels.
    CHECK(result.lines[index].text == "line " + std::to_string(index));
    CHECK(near(result.lines[index].box.x, 50) && near(result.lines[index].box.y, tops[index]));
    CHECK(near(result.lines[index].box.width, 600) && near(result.lines[index].box.height, 20));
    CHECK(result.lines[index].words.size() == 2);
  }
  const auto calls = fake->calls();
  CHECK(calls.size() == 1);
  if (calls.size() == 1) {
    CHECK(calls[0].image_width == 1000 && calls[0].image_height == 6000);
    CHECK(calls[0].tiles.size() == 3);
    for (const auto& tile : calls[0].tiles) {
      CHECK(tile.first == 1000 && tile.second == 2600);
    }
  }
}

void ocr_wide_image_scales_to_max_dimension() {
  FakeEngineConfig config;
  config.max_dimension = 1000;
  config.lines = {FakeLine{Box{400, 200, 2000, 80}, "wide text here", false},
                  FakeLine{Box{3000, 1800, 900, 100}, "second", false}};
  const auto fake = patchy::ocr::make_fake_engine(config);
  const auto image = white_image(4000, 2000);
  const auto result = fake->open("en-US")->recognize(image.view());
  const auto calls = fake->calls();
  CHECK(calls.size() == 1 && calls[0].tiles.size() == 1);
  if (calls.size() == 1 && calls[0].tiles.size() == 1) {
    // The engine saw a quarter-size image...
    CHECK(calls[0].tiles[0].first == 1000 && calls[0].tiles[0].second == 500);
  }
  // ...and the lines came back in the original's pixels.
  CHECK(result.lines.size() == 2);
  if (result.lines.size() == 2) {
    CHECK(near(result.lines[0].box.x, 400) && near(result.lines[0].box.y, 200));
    CHECK(near(result.lines[0].box.width, 2000) && near(result.lines[0].box.height, 80));
    CHECK(near(result.lines[1].box.x, 3000) && near(result.lines[1].box.y, 1800));
    CHECK(result.lines[0].words.size() == 3);
    if (result.lines[0].words.size() == 3) {
      // Words keep their place inside the line.
      CHECK(near(result.lines[0].words[0].box.x, 400));
      CHECK(result.lines[0].words[2].text == "here");
      CHECK(near(result.lines[0].words[2].box.x + result.lines[0].words[2].box.width, 2400));
    }
  }
  CHECK(result.text() == "wide text here\nsecond");
}

void ocr_downscale_is_area_average() {
  // 4 x 2 gray, halved: each output pixel is the rounded mean of its 2 x 2 block.
  const std::vector<std::uint8_t> gray = {10, 20, 100, 101, 30, 41, 102, 104};
  const ImageView view{gray.data(), 4, 2, 4, PixelLayout::Gray8};
  const auto half = patchy::ocr::scaled_rows_bgra(view, 2, 1, 0, 1);
  CHECK(half.width == 2 && half.height == 1 && half.pixels.size() == 8);
  // (10+20+30+41)/4 = 25.25 -> 25; (100+101+102+104)/4 = 101.75 -> 102.
  CHECK(half.pixels[0] == 25 && half.pixels[1] == 25 && half.pixels[2] == 25 && half.pixels[3] == 255);
  CHECK(half.pixels[4] == 102 && half.pixels[7] == 255);

  // Same size: a straight conversion, and a row range reads just those rows.
  const auto second_row = patchy::ocr::scaled_rows_bgra(view, 4, 2, 1, 1);
  CHECK(second_row.width == 4 && second_row.height == 1);
  CHECK(second_row.pixels[0] == 30 && second_row.pixels[4] == 41 && second_row.pixels[12] == 104);

  // A stride wider than the row, and channel order: RGB in, BGRA out.
  const std::vector<std::uint8_t> rgb = {1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10, 11, 12, 0, 0};
  const ImageView rgb_view{rgb.data(), 2, 2, 8, PixelLayout::Rgb8};
  const auto converted = patchy::ocr::scaled_rows_bgra(rgb_view, 2, 2, 0, 2);
  CHECK(converted.pixels[0] == 3 && converted.pixels[1] == 2 && converted.pixels[2] == 1);
  CHECK(converted.pixels[8] == 9 && converted.pixels[9] == 8 && converted.pixels[10] == 7);

  bool refused = false;
  try {
    (void)patchy::ocr::scaled_rows_bgra(view, 8, 2, 0, 1);  // never upscales
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  CHECK(refused);
}

void ocr_alpha_flattens_over_white() {
  // Straight alpha: fully transparent is white whatever its colour, half-transparent black
  // is mid gray, opaque keeps its colour. RGBA and BGRA agree.
  const std::vector<std::uint8_t> rgba = {200, 10, 10, 0, 0, 0, 0, 128, 10, 20, 30, 255};
  const ImageView view{rgba.data(), 3, 1, 12, PixelLayout::Rgba8};
  const auto flat = patchy::ocr::scaled_rows_bgra(view, 3, 1, 0, 1);
  CHECK(flat.pixels[0] == 255 && flat.pixels[1] == 255 && flat.pixels[2] == 255 && flat.pixels[3] == 255);
  CHECK(flat.pixels[4] == 127 && flat.pixels[5] == 127 && flat.pixels[6] == 127);
  CHECK(flat.pixels[8] == 30 && flat.pixels[9] == 20 && flat.pixels[10] == 10 && flat.pixels[11] == 255);

  const std::vector<std::uint8_t> bgra = {10, 10, 200, 0, 0, 0, 0, 128, 30, 20, 10, 255};
  const ImageView bgra_view{bgra.data(), 3, 1, 12, PixelLayout::Bgra8};
  const auto same = patchy::ocr::scaled_rows_bgra(bgra_view, 3, 1, 0, 1);
  CHECK(same.pixels == flat.pixels);
}

void ocr_cancel_stops_between_bands() {
  FakeEngineConfig config;
  config.max_dimension = 100;
  config.delay_ms = 20;
  config.lines = {FakeLine{Box{5, 5, 50, 10}, "a", false}};
  const auto fake = patchy::ocr::make_fake_engine(config);
  const auto image = white_image(100, 1000);
  const auto recognizer = fake->open("en-US");

  // Requested before the call: nothing is read.
  patchy::ocr::Cancel before;
  before.request();
  bool cancelled = false;
  try {
    (void)recognizer->recognize(image.view(), &before);
  } catch (const patchy::ocr::RecognitionCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  CHECK(fake->calls().empty());

  // Requested while a band is being read: the call ends well before the remaining bands
  // (about a dozen at 20 ms each) would have.
  patchy::ocr::Cancel during;
  std::thread canceller([&during] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    during.request();
  });
  cancelled = false;
  try {
    (void)recognizer->recognize(image.view(), &during);
  } catch (const patchy::ocr::RecognitionCancelled&) {
    cancelled = true;
  }
  canceller.join();
  CHECK(cancelled);

  // An engine failure passes through as a runtime_error with its text.
  FakeEngineConfig failing;
  failing.fail_with = "the engine fell over";
  const auto broken = patchy::ocr::make_fake_engine(failing);
  std::string message;
  try {
    (void)broken->open("en-US")->recognize(image.view());
  } catch (const std::runtime_error& error) {
    message = error.what();
  }
  CHECK(message == "the engine fell over");
}

void ocr_env_fake_engine_parses_spec() {
  const auto lines = patchy::ocr::parse_fake_lines("10,20,300,40=Hello world;v:5,6,7,80=\\u65e5\\u672c;bad;1,2,3=x;"
                                                   "1,2,0,4=zero width;9,9,9,9=a=b");
  CHECK(lines.size() == 3);
  if (lines.size() == 3) {
    CHECK(lines[0].text == "Hello world" && !lines[0].vertical);
    CHECK(near(lines[0].box.x, 10) && near(lines[0].box.y, 20) && near(lines[0].box.width, 300) &&
          near(lines[0].box.height, 40));
    // "日本" is the two characters of "Japan" as UTF-8.
    CHECK(lines[1].vertical && lines[1].text == "\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK(near(lines[1].box.height, 80));
    // Only the first '=' separates the box from the text.
    CHECK(lines[2].text == "a=b");
  }
  CHECK(patchy::ocr::parse_fake_lines("").empty());

  const auto languages = patchy::ocr::parse_fake_languages("en-US=English (United States);ja=Japanese;=nameless;x");
  CHECK(languages.size() == 2);
  if (languages.size() == 2) {
    CHECK(languages[0].tag == "en-US" && languages[0].display_name == "English (United States)");
    CHECK(languages[1].tag == "ja");
  }
  CHECK(patchy::ocr::parse_fake_languages("none").empty());
}

void ocr_blocks_group_lines_by_spacing() {
  patchy::ocr::Result result;
  // A paragraph of three left-aligned lines (20 tall, 6 apart), listed out of order.
  result.lines.push_back(line_at(100, 152, 380, 20));  // 0: third
  result.lines.push_back(line_at(100, 100, 400, 20));  // 1: first
  result.lines.push_back(line_at(100, 126, 250, 20));  // 2: second
  // A heading far above, a much larger line right under the paragraph, and a column beside
  // it at the same height: none joins.
  result.lines.push_back(line_at(100, 20, 300, 20));    // 3
  result.lines.push_back(line_at(100, 180, 400, 60));   // 4: three times as thick
  result.lines.push_back(line_at(600, 100, 300, 20));   // 5: no overlap along the line
  // A rotated line overlapping the heading stays alone.
  auto rotated = line_at(100, 44, 300, 20);
  rotated.angle_degrees = 10.0;
  result.lines.push_back(rotated);  // 6

  const auto blocks = patchy::ocr::group_lines_into_blocks(result);
  CHECK(blocks.size() == 5);
  // Reading order: top to bottom, then left to right.
  if (blocks.size() == 5) {
    CHECK(blocks[0].lines == std::vector<std::size_t>{3});
    CHECK(blocks[1].lines == std::vector<std::size_t>{6});
    CHECK((blocks[2].lines == std::vector<std::size_t>{1, 2, 0}));
    CHECK(blocks[3].lines == std::vector<std::size_t>{5});
    CHECK(blocks[4].lines == std::vector<std::size_t>{4});
    const auto& paragraph = blocks[2];
    CHECK(!paragraph.vertical);
    CHECK(near(paragraph.box.x, 100) && near(paragraph.box.y, 100) && near(paragraph.box.width, 400) &&
          near(paragraph.box.height, 72));
    CHECK(near(paragraph.line_size, 20) && near(paragraph.line_pitch, 26));
    CHECK(paragraph.alignment == patchy::ocr::Alignment::Start);
    CHECK(near(blocks[0].line_pitch, 0));
  }

  // The same lines one block each, same order rule.
  const auto singles = patchy::ocr::lines_as_blocks(result);
  CHECK(singles.size() == result.lines.size());
  if (singles.size() == result.lines.size()) {
    CHECK(singles.front().lines == std::vector<std::size_t>{3});
    CHECK(singles[2].lines == std::vector<std::size_t>{1});
    CHECK(singles[3].lines == std::vector<std::size_t>{5});
  }
  CHECK(patchy::ocr::group_lines_into_blocks(patchy::ocr::Result{}).empty());
}

void ocr_blocks_report_alignment_and_vertical_order() {
  // Centred and right-aligned paragraphs.
  patchy::ocr::Result centered;
  centered.lines = {line_at(100, 0, 400, 20), line_at(150, 26, 300, 20), line_at(50, 52, 500, 20)};
  auto blocks = patchy::ocr::group_lines_into_blocks(centered);
  CHECK(blocks.size() == 1 && blocks[0].alignment == patchy::ocr::Alignment::Center);

  patchy::ocr::Result right;
  right.lines = {line_at(100, 0, 400, 20), line_at(220, 26, 280, 20), line_at(50, 52, 450, 20)};
  blocks = patchy::ocr::group_lines_into_blocks(right);
  CHECK(blocks.size() == 1 && blocks[0].alignment == patchy::ocr::Alignment::End);

  // Vertical Japanese: three columns 30 wide, 10 apart, read right to left; a horizontal
  // caption under them never joins a vertical column and reads first (horizontal blocks
  // come before vertical ones).
  patchy::ocr::Result vertical;
  vertical.lines.push_back(line_at(100, 50, 30, 600, true));  // 0: leftmost, last
  vertical.lines.push_back(line_at(180, 50, 30, 580, true));  // 1: rightmost, first
  vertical.lines.push_back(line_at(140, 50, 30, 600, true));  // 2: middle
  vertical.lines.push_back(line_at(100, 700, 110, 30, false));  // 3
  blocks = patchy::ocr::group_lines_into_blocks(vertical);
  CHECK(blocks.size() == 2);
  if (blocks.size() == 2) {
    CHECK(blocks[0].lines == std::vector<std::size_t>{3});
    CHECK(blocks[1].vertical);
    CHECK((blocks[1].lines == std::vector<std::size_t>{1, 2, 0}));
    CHECK(near(blocks[1].line_size, 30) && near(blocks[1].line_pitch, 40));
    // Columns share their top edge: Start.
    CHECK(blocks[1].alignment == patchy::ocr::Alignment::Start);
  }
}

void ocr_blocks_judge_size_by_length_per_character() {
  const auto text_line = [](const char* text, double x, double y, double width, double height) {
    auto line = line_at(x, y, width, height);
    line.text = text;
    return line;
  };
  patchy::ocr::Result result;
  // The same size throughout, 17 px a character and 46 px apart, but the ink boxes differ:
  // the first line has ascenders and descenders (34 tall), the second only x-height letters
  // (18 tall, barely half), the third ascenders alone (27 tall). One paragraph.
  result.lines.push_back(text_line("Typography with every kind of glyph", 100, 100, 595, 34));  // 0
  result.lines.push_back(text_line("we saw a car on our run", 100, 154, 391, 18));              // 1
  result.lines.push_back(text_line("and the third line ends it", 100, 195, 442, 27));           // 2
  // A heading just above, about as thick as the first line's ink but set far wider (30 px
  // a character): another size, so another block, however close it sits.
  result.lines.push_back(text_line("A Bigger Heading", 100, 52, 480, 30));  // 3
  // A page number under the paragraph: too few characters to judge by length, so its
  // thickness decides (14 against 27: no).
  result.lines.push_back(text_line("7", 100, 238, 9, 14));  // 4
  const auto blocks = patchy::ocr::group_lines_into_blocks(result);
  CHECK(blocks.size() == 3);
  if (blocks.size() == 3) {
    CHECK(blocks[0].lines == std::vector<std::size_t>{3});
    CHECK((blocks[1].lines == std::vector<std::size_t>{0, 1, 2}));
    CHECK(blocks[2].lines == std::vector<std::size_t>{4});
  }
  // Characters are counted, not bytes: a Japanese line of nine characters is 27 bytes, and
  // it is the same size as the one under it.
  patchy::ocr::Result japanese;
  japanese.lines.push_back(
      text_line("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE6\x96\x87\xE5\xAD\x97\xE3\x82\x92\xE8\xAA\xAD\xE3\x82\x80",
                50, 50, 432, 44));
  japanese.lines.push_back(text_line("\xE7\xB8\xA6\xE6\x9B\xB8\xE3\x81\x8D\xE3\x81\xA8\xE6\xA8\xAA\xE6\x9B\xB8\xE3\x81\x8D", 50, 120, 336, 44));
  CHECK(patchy::ocr::group_lines_into_blocks(japanese).size() == 1);
}

// Whether two lines sit side by side: their boxes overlap across the lines by at least half
// the thinner one (the rule's own measure, restated here so the test does not lean on it).
bool on_one_row(const patchy::ocr::Line& a, const patchy::ocr::Line& b) {
  const double overlap = std::min(a.box.y + a.box.height, b.box.y + b.box.height) - std::max(a.box.y, b.box.y);
  return overlap >= 0.5 * std::min(a.box.height, b.box.height);
}

const patchy::ocr::Block* block_holding(const std::vector<patchy::ocr::Block>& blocks, std::size_t line) {
  for (const auto& block : blocks) {
    if (std::find(block.lines.begin(), block.lines.end(), line) != block.lines.end()) {
      return &block;
    }
  }
  return nullptr;
}

void ocr_blocks_hold_one_line_per_row() {
  // Two columns under a line that spans both, everything one size and 26 apart. Both column
  // heads would join the spanning line, so neither does: three blocks, not one in which the
  // columns' lines alternate.
  patchy::ocr::Result columns;
  columns.lines.push_back(line_at(100, 100, 800, 20));  // 0: spans both columns
  columns.lines.push_back(line_at(100, 126, 380, 20));  // 1: left column
  columns.lines.push_back(line_at(520, 126, 380, 20));  // 2: right column
  columns.lines.push_back(line_at(100, 152, 380, 20));  // 3
  columns.lines.push_back(line_at(520, 152, 380, 20));  // 4
  auto blocks = patchy::ocr::group_lines_into_blocks(columns);
  CHECK(blocks.size() == 3);
  if (blocks.size() == 3) {
    CHECK(blocks[0].lines == std::vector<std::size_t>{0});
    CHECK((blocks[1].lines == std::vector<std::size_t>{1, 3}));
    CHECK((blocks[2].lines == std::vector<std::size_t>{2, 4}));
    CHECK(near(blocks[1].line_pitch, 26) && near(blocks[2].line_pitch, 26));
  }

  // A row in two pieces inside a paragraph: the lines above and below it cannot tell which
  // piece is theirs, so the pieces stand alone and keep their places.
  patchy::ocr::Result pieces;
  pieces.lines = {line_at(100, 100, 800, 20), line_at(100, 126, 300, 20), line_at(600, 126, 300, 20),
                  line_at(100, 152, 800, 20)};
  CHECK(patchy::ocr::group_lines_into_blocks(pieces).size() == 4);

  // Row neighbours that meet only through a longer chain (each heads its own staggered
  // column, and one wide line under both joins the columns): the two stand alone and the
  // rest stays a block.
  patchy::ocr::Result chain;
  chain.lines.push_back(line_at(100, 100, 300, 20));  // 0: row neighbour of 1
  chain.lines.push_back(line_at(500, 100, 300, 20));  // 1
  chain.lines.push_back(line_at(100, 126, 300, 20));  // 2: under 0
  chain.lines.push_back(line_at(500, 139, 300, 20));  // 3: under 1, half a line lower than 2
  chain.lines.push_back(line_at(100, 165, 700, 20));  // 4: under both
  blocks = patchy::ocr::group_lines_into_blocks(chain);
  CHECK(blocks.size() == 3);
  if (blocks.size() == 3) {
    CHECK(blocks[0].lines == std::vector<std::size_t>{0});
    CHECK(blocks[1].lines == std::vector<std::size_t>{1});
    CHECK((blocks[2].lines == std::vector<std::size_t>{2, 3, 4}));
  }

  // The boxes, character counts and angles a line detector reported for a screenshot of a
  // list in a dark interface: three entries, each a title with labels beside it on one row,
  // a paragraph, and a row of buttons. Every title, label and button joined the paragraph
  // next to it, so two of the entries became one block of 26 "lines" a pixel apart, which
  // was drawn as one pile.
  struct Detected {
    double x;
    double y;
    double width;
    double height;
    int characters;
    double angle;
  };
  static const Detected kDetected[] = {
      {41.5, 28.5, 112.0, 27.0, 9, 0.00},  // 0
      {41.1, 66.1, 1280.9, 23.9, 165, 0.00},  // 1
      {47.8, 109.8, 94.5, 22.5, 8, 0.00},  // 2
      {149.5, 107.3, 1141.0, 27.0, 120, 0.06},  // 3
      {53.4, 148.4, 186.2, 23.2, 23, 0.00},  // 4
      {271.0, 148.0, 75.1, 22.1, 7, 0.00},  // 5
      {362.6, 148.6, 115.8, 22.8, 11, 0.00},  // 6
      {43.1, 188.1, 1380.9, 23.9, 162, 0.00},  // 7
      {41.1, 217.1, 1314.9, 23.9, 154, 0.00},  // 8
      {42.1, 247.1, 1333.9, 23.9, 157, 0.00},  // 9
      {42.1, 276.1, 1389.9, 23.9, 159, 0.00},  // 10
      {45.1, 305.1, 1386.9, 23.9, 166, 0.00},  // 11
      {40.6, 332.9, 1380.8, 26.5, 166, -0.05},  // 12
      {41.4, 363.4, 1391.3, 26.3, 167, 0.00},  // 13
      {40.4, 393.4, 196.3, 23.3, 21, 0.00},  // 14
      {666.4, 439.4, 81.3, 24.3, 7, 0.00},  // 15
      {251.4, 440.4, 171.2, 23.2, 18, 0.00},  // 16
      {61.4, 438.4, 149.1, 27.7, 17, 0.48},  // 17
      {462.9, 439.2, 162.3, 26.2, 19, 0.50},  // 18
      {784.0, 442.0, 159.0, 21.9, 17, 0.44},  // 19
      {1363.1, 438.1, 55.9, 26.9, 4, 0.00},  // 20
      {1322.8, 457.8, 21.3, 19.3, 1, 0.00},  // 21
      {149.7, 502.0, 476.6, 26.3, 50, 0.13},  // 22
      {47.8, 505.8, 94.5, 22.5, 8, 0.00},  // 23
      {643.4, 505.3, 197.3, 24.2, 24, -0.34},  // 24
      {873.5, 503.5, 74.0, 24.0, 7, 0.00},  // 25
      {965.1, 507.1, 143.8, 20.8, 14, 0.00},  // 26
      {41.4, 542.5, 1372.2, 27.1, 165, -0.04},  // 27
      {42.4, 572.4, 1390.3, 26.3, 163, 0.00},  // 28
      {42.8, 603.8, 718.4, 21.4, 85, 0.00},  // 29
      {58.3, 646.3, 57.5, 30.5, 6, 0.00},  // 30
      {150.7, 648.3, 82.5, 27.1, 7, 1.17},  // 31
      {271.4, 649.4, 81.3, 24.3, 7, 0.00},  // 32
      {388.4, 651.4, 159.1, 23.1, 17, 0.00},  // 33
      {1364.5, 649.5, 54.0, 25.0, 4, 0.00},  // 34
      {1318.5, 663.3, 28.7, 27.7, 1, -26.57},  // 35
      {150.2, 714.2, 314.5, 23.5, 31, 0.00},  // 36
      {47.8, 715.8, 94.5, 22.5, 8, 0.00},  // 37
      {480.4, 715.4, 198.3, 23.3, 24, 0.00},  // 38
      {710.4, 713.4, 76.1, 24.1, 7, 0.00},  // 39
      {802.5, 715.5, 145.0, 23.0, 14, 0.00},  // 40
      {40.6, 752.6, 1360.8, 26.8, 160, -0.06},  // 41
      {43.1, 783.1, 1309.9, 23.9, 157, 0.00},  // 42
      {42.1, 813.1, 1357.9, 23.9, 160, 0.00},  // 43
      {41.6, 841.6, 959.9, 26.6, 113, 0.06},  // 44
      {59.5, 886.5, 75.0, 28.0, 8, 0.00},  // 45
      {498.4, 888.4, 81.3, 24.3, 7, 0.00},  // 46
      {376.8, 887.5, 82.4, 26.8, 7, 1.17},  // 47
      {616.1, 891.1, 157.9, 20.9, 17, 0.00},  // 48
      {171.4, 890.4, 166.1, 23.1, 18, 0.00},  // 49
      {1364.1, 887.1, 54.8, 26.8, 4, 0.00},  // 50
      {1321.8, 906.8, 21.3, 19.3, 1, 0.00},  // 51
  };
  patchy::ocr::Result screenshot;
  for (const auto& detected : kDetected) {
    auto line = line_at(detected.x, detected.y, detected.width, detected.height);
    line.text.assign(static_cast<std::size_t>(detected.characters), 'x');
    line.angle_degrees = detected.angle;
    screenshot.lines.push_back(line);
  }
  blocks = patchy::ocr::group_lines_into_blocks(screenshot);
  std::size_t placed = 0;
  for (const auto& block : blocks) {
    placed += block.lines.size();
    for (std::size_t first = 0; first < block.lines.size(); ++first) {
      for (std::size_t second = first + 1; second < block.lines.size(); ++second) {
        CHECK(!on_one_row(screenshot.lines[block.lines[first]], screenshot.lines[block.lines[second]]));
      }
    }
    // Lines of a block are a line apart, never a pixel.
    CHECK(block.lines.size() == 1 || block.line_pitch > 20.0);
  }
  CHECK(placed == screenshot.lines.size());
  // The three paragraphs are blocks at their own 29 to 30 px pitch.
  const auto holds = [&](std::size_t line, std::size_t from, std::size_t to) {
    const auto* block = block_holding(blocks, line);
    if (block == nullptr) {
      return false;
    }
    for (std::size_t member = from; member <= to; ++member) {
      if (std::find(block->lines.begin(), block->lines.end(), member) == block->lines.end()) {
        return false;
      }
    }
    return block->line_pitch > 28.0 && block->line_pitch < 31.0;
  };
  CHECK(holds(7, 7, 14));
  CHECK(holds(27, 27, 29));
  CHECK(holds(41, 41, 44));
  const auto* second = block_holding(blocks, 27);
  const auto* third = block_holding(blocks, 41);
  CHECK(second != nullptr && second->lines.size() == 3);
  CHECK(third != nullptr && third->lines.size() == 4);
  // A title and the labels on its row each keep their own place.
  for (const std::size_t line : {22U, 24U, 25U, 26U, 36U, 38U, 39U, 40U}) {
    const auto* block = block_holding(blocks, line);
    CHECK(block != nullptr && block->lines.size() == 1);
  }
}

// The operating system's engine as this machine has it (the core suite installs no test
// list): listed on Windows with its installed languages, absent everywhere else.
void ocr_windows_engine_lists_languages_if_available() {
  const auto engine = patchy::ocr::find_engine("windows");
#if defined(_WIN32)
  CHECK(engine != nullptr);
  if (engine == nullptr) {
    return;
  }
  CHECK(engine->display_name() == "Windows");
  if (engine->unavailable() != patchy::ocr::Unavailable::None) {
    CHECK(!engine->unavailable_reason().empty());
    patchy::test::skip(patchy::test::SkipKind::feature,
                       "the Windows text recognition engine is unavailable: " + engine->unavailable_reason());
  }
  CHECK(engine->unavailable_reason().empty());
  const auto languages = engine->languages();
  CHECK(!languages.empty());
  bool default_listed = false;
  const auto preferred = engine->default_language();
  for (const auto& language : languages) {
    CHECK(!language.tag.empty() && !language.display_name.empty());
    default_listed = default_listed || language.tag == preferred;
  }
  CHECK(default_listed);
  // Opening and dropping a recognizer starts and ends its own thread cleanly.
  CHECK(engine->open(preferred) != nullptr);
#else
  CHECK(engine == nullptr);
  CHECK(patchy::ocr::engines().empty());
#endif
}

}  // namespace

// The model engine's steps (ocr_model_tests.cpp), gathered here so the suite's group list
// needs no new entry.
std::vector<patchy::test::TestCase> ocr_model_tests();

std::vector<patchy::test::TestCase> ocr_tests() {
  auto model_tests = ocr_model_tests();
  std::vector<patchy::test::TestCase> tests{
      {"ocr_windows_engine_lists_languages_if_available", ocr_windows_engine_lists_languages_if_available},
      {"ocr_registry_uses_installed_test_engines", ocr_registry_uses_installed_test_engines},
      {"ocr_band_plan_is_pinned", ocr_band_plan_is_pinned},
      {"ocr_bands_report_overlap_lines_once", ocr_bands_report_overlap_lines_once},
      {"ocr_wide_image_scales_to_max_dimension", ocr_wide_image_scales_to_max_dimension},
      {"ocr_downscale_is_area_average", ocr_downscale_is_area_average},
      {"ocr_alpha_flattens_over_white", ocr_alpha_flattens_over_white},
      {"ocr_cancel_stops_between_bands", ocr_cancel_stops_between_bands},
      {"ocr_env_fake_engine_parses_spec", ocr_env_fake_engine_parses_spec},
      {"ocr_blocks_group_lines_by_spacing", ocr_blocks_group_lines_by_spacing},
      {"ocr_blocks_report_alignment_and_vertical_order", ocr_blocks_report_alignment_and_vertical_order},
      {"ocr_blocks_judge_size_by_length_per_character", ocr_blocks_judge_size_by_length_per_character},
      {"ocr_blocks_hold_one_line_per_row", ocr_blocks_hold_one_line_per_row},
  };
  tests.insert(tests.end(), std::make_move_iterator(model_tests.begin()), std::make_move_iterator(model_tests.end()));
  return tests;
}
