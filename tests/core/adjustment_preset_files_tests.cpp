// Photoshop Hue/Saturation (.ahu), Levels (.alv) and Black & White (.blw) preset files
// (formats/photoshop_adjustment_presets; #588, #589, #528).

#include "core/adjustment_layer.hpp"
#include "formats/photoshop_adjustment_presets.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

bool same_hue_saturation(const patchy::HueSaturationAdjustment& left, const patchy::HueSaturationAdjustment& right) {
  return left.hue_shift == right.hue_shift && left.saturation_delta == right.saturation_delta &&
         left.lightness_delta == right.lightness_delta && left.colorize == right.colorize &&
         left.colorize_hue == right.colorize_hue && left.colorize_saturation == right.colorize_saturation &&
         left.colorize_lightness == right.colorize_lightness && left.bands == right.bands;
}

bool throws(const std::function<void()>& action) {
  try {
    action();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

patchy::HueSaturationAdjustment sample_hue_saturation() {
  patchy::HueSaturationAdjustment settings;
  settings.hue_shift = -12;
  settings.saturation_delta = 30;
  settings.lightness_delta = -4;
  settings.colorize_hue = 205;
  settings.colorize_saturation = 33;
  settings.colorize_lightness = 7;
  settings.bands[0].hue_shift = -40;
  settings.bands[0].saturation_delta = 25;
  settings.bands[4].lightness_delta = -15;
  return settings;
}

void adjustment_preset_files_round_trip_and_reject_malformed() {
  const auto scratch = std::filesystem::path("test-artifacts") / "adjustment-preset-files";
  std::filesystem::create_directories(scratch);

  // Hue/Saturation: the hue2 body, colorize off and on, bands included.
  auto hue = sample_hue_saturation();
  const auto hue_bytes = patchy::ahu::write(hue);
  CHECK(hue_bytes.size() == 136U);
  CHECK(hue_bytes[0] == 0x00 && hue_bytes[1] == 0x02);
  CHECK(same_hue_saturation(patchy::ahu::read(hue_bytes), hue));
  hue.colorize = true;
  CHECK(same_hue_saturation(patchy::ahu::read(patchy::ahu::write(hue)), hue));
  // Hue 270 is stored as -90 in the file and comes back as 270.
  hue.colorize_hue = 270;
  const auto hue_270 = patchy::ahu::write(hue);
  CHECK(hue_270[4] == 0xFF && hue_270[5] == 0xA6);
  CHECK(patchy::ahu::read(hue_270).colorize_hue == 270);
  // The short legacy shape (header only) keeps the default ranges.
  const std::vector<std::uint8_t> short_shape(hue_270.begin(), hue_270.begin() + 16);
  const auto short_read = patchy::ahu::read(short_shape);
  CHECK(short_read.colorize && short_read.colorize_hue == 270);
  CHECK(short_read.bands == patchy::default_hue_saturation_bands());
  const auto hue_path = scratch / "round-trip.ahu";
  patchy::ahu::write_file(hue_path, hue);
  CHECK(same_hue_saturation(patchy::ahu::read_file(hue_path), hue));

  // Levels: composite and per-channel records with output levels.
  patchy::LevelsAdjustment levels;
  levels.black_input = 18;
  levels.white_input = 232;
  levels.gamma_percent = 115;
  levels.black_output = 12;
  levels.white_output = 244;
  levels.red = patchy::LevelsRecord{5, 250, 90, 0, 255};
  levels.blue = patchy::LevelsRecord{0, 240, 100, 10, 255};
  const auto level_bytes = patchy::alv::write(levels);
  CHECK(level_bytes.size() == 292U);
  CHECK(level_bytes[0] == 0x00 && level_bytes[1] == 0x02);
  CHECK(patchy::alv::read(level_bytes) == levels);
  // The channel tab is dialog state, not file content.
  auto on_red = levels;
  on_red.channel = patchy::LevelsChannel::Red;
  CHECK(patchy::alv::read(patchy::alv::write(on_red)) == levels);
  // Bytes past the 29 records (Photoshop's extension for more channels) are ignored.
  auto extended = level_bytes;
  extended.insert(extended.end(), {'L', 'v', 'l', 's', 0x00, 0x03, 0x00, 0x3E});
  CHECK(patchy::alv::read(extended) == levels);
  const auto levels_path = scratch / "round-trip.alv";
  patchy::alv::write_file(levels_path, levels);
  CHECK(patchy::alv::read_file(levels_path) == levels);

  // Black & White: weights, tint and tint color.
  patchy::BlackWhiteAdjustment black_white;
  black_white.reds = -20;
  black_white.yellows = 140;
  black_white.greens = 55;
  black_white.cyans = 300;
  black_white.blues = -200;
  black_white.magentas = 12;
  black_white.tint = true;
  black_white.tint_color = patchy::RgbColor{190, 160, 120};
  const auto bw_bytes = patchy::blw::write(black_white);
  CHECK(bw_bytes.size() > 16U);
  CHECK(bw_bytes[0] == 0x00 && bw_bytes[3] == 0x10);
  CHECK(patchy::blw::read(bw_bytes) == black_white);
  const auto bw_path = scratch / "round-trip.blw";
  patchy::blw::write_file(bw_path, black_white);
  CHECK(patchy::blw::read_file(bw_path) == black_white);

  // What Patchy renders through each file, for the Photoshop acceptance run that applies the
  // same files (docs/adjustment-presets.md): "<file> r,g,b -> r,g,b" per probe color.
  {
    std::ofstream expected(scratch / "expected.txt", std::ios::trunc);
    const auto render = [&expected](const char* file, const patchy::AdjustmentSettings& settings) {
      for (const auto& color : {patchy::RgbColor{220, 30, 30}, patchy::RgbColor{30, 200, 30},
                                patchy::RgbColor{40, 60, 220}, patchy::RgbColor{128, 128, 128},
                                patchy::RgbColor{200, 170, 60}}) {
        const auto out = patchy::apply_adjustment_to_color(color, settings);
        expected << file << ' ' << int{color.red} << ',' << int{color.green} << ',' << int{color.blue} << " -> "
                 << int{out.red} << ',' << int{out.green} << ',' << int{out.blue} << '\n';
      }
    };
    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::HueSaturation;
    settings.hue_saturation = patchy::ahu::read_file(hue_path);
    render("round-trip.ahu", settings);
    const auto bands_path = scratch / "bands.ahu";
    patchy::ahu::write_file(bands_path, sample_hue_saturation());
    settings.hue_saturation = patchy::ahu::read_file(bands_path);
    render("bands.ahu", settings);
    settings = {};
    settings.kind = patchy::AdjustmentKind::Levels;
    settings.levels = patchy::alv::read_file(levels_path);
    render("round-trip.alv", settings);
    settings = {};
    settings.kind = patchy::AdjustmentKind::BlackWhite;
    settings.black_white = patchy::blw::read_file(bw_path);
    render("round-trip.blw", settings);
  }

  // Malformed data and the wrong kind of file are refused.
  const std::vector<std::uint8_t> empty;
  CHECK(throws([&] { (void)patchy::ahu::read(empty); }));
  CHECK(throws([&] { (void)patchy::alv::read(empty); }));
  CHECK(throws([&] { (void)patchy::blw::read(empty); }));
  CHECK(throws([&] { (void)patchy::alv::read(hue_bytes); }));
  CHECK(throws([&] { (void)patchy::blw::read(level_bytes); }));
  auto wrong_version = level_bytes;
  wrong_version[1] = 0x07;
  CHECK(throws([&] { (void)patchy::alv::read(wrong_version); }));
  CHECK(throws([&] { (void)patchy::ahu::read(std::vector<std::uint8_t>(hue_bytes.begin(), hue_bytes.begin() + 10)); }));
  CHECK(throws([&] { (void)patchy::ahu::read_file(scratch / "missing.ahu"); }));
  const auto oversized = scratch / "oversized.alv";
  auto huge = level_bytes;
  huge.resize(200 * 1024, 0);
  write_bytes(oversized, huge);
  CHECK(throws([&] { (void)patchy::alv::read_file(oversized); }));
}

// Payloads Photoshop 2026 wrote (hue2, levl and blwh blocks of a PSD saved over COM; the
// preset files carry the same bytes, and Photoshop applied each exported file through
// its Usng load). See local-test-fixtures/adjustment-presets/README.txt.
void adjustment_preset_files_read_photoshop_payloads_if_available() {
  const auto folder = patchy::test::local_fixtures_root() / "adjustment-presets";
  patchy::test::skip_unless_exists(folder / "README.txt");

  const auto hue = patchy::ahu::read_file(folder / "photoshop-hue-reds.ahu");
  CHECK(!hue.colorize);
  CHECK(hue.hue_shift == 10 && hue.saturation_delta == -20 && hue.lightness_delta == 5);
  CHECK(hue.bands[0].hue_shift == -30 && hue.bands[0].saturation_delta == 25 && hue.bands[0].lightness_delta == 0);
  CHECK(!hue.bands[1].has_effect() && !hue.bands[5].has_effect());

  const auto colorize = patchy::ahu::read_file(folder / "photoshop-hue-colorize.ahu");
  CHECK(colorize.colorize);
  CHECK(colorize.colorize_hue == 200 && colorize.colorize_saturation == 40 && colorize.colorize_lightness == -10);

  const auto levels = patchy::alv::read_file(folder / "photoshop-levels.alv");
  CHECK(levels.black_input == 20 && levels.white_input == 230 && levels.gamma_percent == 120);
  CHECK(levels.black_output == 5 && levels.white_output == 250);
  CHECK((levels.red == patchy::LevelsRecord{10, 245, 100, 0, 255}));

  const auto black_white = patchy::blw::read_file(folder / "photoshop-bw-tint.blw");
  CHECK(black_white.reds == 50 && black_white.yellows == 70 && black_white.greens == 30);
  CHECK(black_white.cyans == 60 && black_white.blues == 10 && black_white.magentas == 90);
  CHECK(black_white.tint);
}

// The Levels eyedroppers (#589) share the Curves samples: per-channel black and white
// inputs, and a gamma that takes the gray sample to mid-gray.
void levels_eyedroppers_set_channel_records() {
  patchy::LevelsAdjustment start;
  start.black_input = 7;
  start.black_output = 9;
  patchy::CurvesEyedropperSamples samples;
  samples.black = patchy::RgbColor{10, 20, 30};
  samples.white = patchy::RgbColor{240, 230, 220};
  auto levels = patchy::levels_adjustment_from_eyedropper_samples(start, samples);
  CHECK(levels.black_input == 7 && levels.black_output == 9);
  CHECK((levels.red == patchy::LevelsRecord{10, 240, 100, 0, 255}));
  CHECK((levels.green == patchy::LevelsRecord{20, 230, 100, 0, 255}));
  CHECK((levels.blue == patchy::LevelsRecord{30, 220, 100, 0, 255}));

  samples.gray = patchy::RgbColor{128, 100, 160};
  levels = patchy::levels_adjustment_from_eyedropper_samples(start, samples);
  const auto expected_gamma = [](int value, int black, int white) {
    return static_cast<int>(std::lround(100.0 * std::log(static_cast<double>(value - black) / (white - black)) /
                                        std::log(0.5)));
  };
  CHECK(levels.red.gamma_percent == expected_gamma(128, 10, 240));
  CHECK(levels.green.gamma_percent == expected_gamma(100, 20, 230));
  CHECK(levels.blue.gamma_percent == expected_gamma(160, 30, 220));
  // The gray sample lands on mid-gray in every channel.
  for (const auto& [record, value] : {std::pair{levels.red, 128}, std::pair{levels.green, 100},
                                      std::pair{levels.blue, 160}}) {
    const auto x = static_cast<double>(value - record.black_input) / (record.white_input - record.black_input);
    CHECK(std::abs(std::pow(x, 100.0 / record.gamma_percent) - 0.5) < 0.01);
  }

  // A white sample alone keeps black at 0; a degenerate pair still leaves a usable range.
  patchy::CurvesEyedropperSamples white_only;
  white_only.white = patchy::RgbColor{200, 0, 255};
  levels = patchy::levels_adjustment_from_eyedropper_samples({}, white_only);
  CHECK(levels.red.black_input == 0 && levels.red.white_input == 200);
  CHECK(levels.green.black_input == 0 && levels.green.white_input == 1);
  CHECK(levels.blue.white_input == 255);
}

}  // namespace

std::vector<patchy::test::TestCase> adjustment_preset_files_tests() {
  return {
      {"levels_eyedroppers_set_channel_records", levels_eyedroppers_set_channel_records},
      {"adjustment_preset_files_round_trip_and_reject_malformed",
       adjustment_preset_files_round_trip_and_reject_malformed},
      {"adjustment_preset_files_read_photoshop_payloads_if_available",
       adjustment_preset_files_read_photoshop_payloads_if_available},
  };
}
