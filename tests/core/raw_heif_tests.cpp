#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::read_binary_file;

// ---- Camera raw (vendored LibRaw behind formats/raw_document_io) ----
//
// The synthetic fixture is a minimal uncompressed 16-bit Bayer DNG built byte-by-byte
// (tests/synthetic_dng.hpp; the house adversarial-file pattern), so the raw pipeline is
// exercised on every platform with no committed camera files. Assertions are statistical
// (dimensions, channel means, monotonic responses) — LibRaw's float pipeline is NOT
// byte-stable across toolchains, so exact-hash pinning is deliberately avoided (AGENTS.md
// universal invariants).

using patchy::test::synthetic_bayer_dng;

using patchy::test::SyntheticDngOptions;

struct RawChannelMeans {
  double red{0.0};
  double green{0.0};
  double blue{0.0};
};

RawChannelMeans raw_channel_means(const patchy::Document& document) {
  const auto& pixels = document.layers().front().pixels();
  RawChannelMeans means;
  const auto pixel_count =
      static_cast<double>(pixels.width()) * static_cast<double>(pixels.height());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      means.red += px[0];
      means.green += px[1];
      means.blue += px[2];
    }
  }
  means.red /= pixel_count;
  means.green /= pixel_count;
  means.blue /= pixel_count;
  return means;
}

void raw_white_balance_round_trips_temperature_and_tint() {
  // The inverse (multipliers -> kelvin/tint) must recover what the forward direction
  // produced; run a spread of plausible photographic values through the sRGB fallback
  // matrix and a Canon-ish matrix.
  const patchy::raw::CameraMatrix canonish = {{
      {{0.6844, -0.0996, -0.0856}},
      {{-0.3876, 1.1761, 0.2396}},
      {{-0.0593, 0.1772, 0.6198}},
      {{0.0, 0.0, 0.0}},
  }};
  const std::array<patchy::raw::CameraMatrix, 2> matrices = {patchy::raw::CameraMatrix{}, canonish};
  const std::array<patchy::raw::WhiteBalance, 5> cases = {{
      {2850.0, 0.0},
      {3800.0, 21.0},
      {5500.0, 10.0},
      {6500.0, -20.0},
      {12000.0, 0.0},
  }};
  for (const auto& matrix : matrices) {
    for (const auto& expected : cases) {
      const auto multipliers = patchy::raw::multipliers_for_white_balance(expected, matrix);
      CHECK(multipliers[0] > 0.0);
      CHECK(multipliers[1] == 1.0);
      CHECK(multipliers[2] > 0.0);
      const auto recovered = patchy::raw::white_balance_for_multipliers(multipliers, matrix);
      CHECK(recovered.has_value());
      CHECK(std::abs(recovered->temperature_k - expected.temperature_k) <
            expected.temperature_k * 0.02);
      CHECK(std::abs(recovered->tint - expected.tint) < 3.0);
    }
  }
}

void raw_develop_reads_synthetic_dng() {
  // Camera-raw defaults are deliberately neutral: no auto histogram stretch.
  CHECK(!patchy::raw::DevelopParams{}.auto_brighten);

  const auto dng = synthetic_bayer_dng(128, 96);
  patchy::raw::DevelopParams params;
  params.auto_brighten = false;  // a uniform field would auto-stretch to white
  auto result = patchy::raw::read_camera_raw(dng, params);
  CHECK(result.document.width() == 128);
  CHECK(result.document.height() == 96);
  CHECK(result.document.layers().size() == 1);
  CHECK(result.document.layers().front().name() == "Background");
  CHECK(result.document.format().bit_depth == patchy::BitDepth::UInt8);
  // A gray card under the as-shot illuminant develops to a near-neutral mid tone
  // (borders excluded by averaging the whole frame; demosaic edges are a tiny fraction).
  const auto means = raw_channel_means(result.document);
  CHECK(means.green > 40.0);
  CHECK(means.green < 220.0);
  CHECK(std::abs(means.red - means.green) < 14.0);
  CHECK(std::abs(means.blue - means.green) < 14.0);

  // Registry dispatch: .dng routes to the read-only camera raw handler.
  const auto* handler = patchy::builtin_format_registry().find_by_extension(".dng");
  CHECK(handler != nullptr);
  CHECK(!handler->can_write());
  CHECK(patchy::raw::is_camera_raw_extension("cr3"));
  CHECK(!patchy::raw::is_camera_raw_extension("raw"));
  const auto via_registry = handler->read(dng);
  CHECK(via_registry.document.width() == 128);
  CHECK(via_registry.document.height() == 96);
}

void raw_develop_exposure_and_white_balance_shift_output() {
  const auto dng = synthetic_bayer_dng(128, 96);
  patchy::raw::DevelopParams params;
  params.auto_brighten = false;
  const auto base = raw_channel_means(patchy::raw::read_camera_raw(dng, params).document);

  auto brighter_params = params;
  brighter_params.exposure_ev = 1.5;
  const auto brighter = raw_channel_means(patchy::raw::read_camera_raw(dng, brighter_params).document);
  CHECK(brighter.green > base.green + 8.0);

  auto darker_params = params;
  darker_params.exposure_ev = -1.5;
  const auto darker = raw_channel_means(patchy::raw::read_camera_raw(dng, darker_params).document);
  CHECK(darker.green + 8.0 < base.green);

  // Raising the temperature renders warmer (more red, less blue) than lowering it.
  auto warm_params = params;
  warm_params.white_balance = patchy::raw::WhiteBalanceMode::Custom;
  warm_params.custom_white_balance = {8000.0, 0.0};
  auto cool_params = warm_params;
  cool_params.custom_white_balance = {3000.0, 0.0};
  const auto warm = raw_channel_means(patchy::raw::read_camera_raw(dng, warm_params).document);
  const auto cool = raw_channel_means(patchy::raw::read_camera_raw(dng, cool_params).document);
  CHECK(warm.red - warm.blue > cool.red - cool.blue + 10.0);
}

void raw_tone_lut_and_color_math() {
  // Neutral parameters must be an exact identity table.
  const auto identity = patchy::raw::build_tone_lut({});
  for (const int index : {0, 1, 255, 12345, 32768, 54321, 65534, 65535}) {
    CHECK(identity[static_cast<std::size_t>(index)] == index);
  }

  // Contrast: S-curve around the midpoint, endpoints pinned.
  patchy::raw::ToneParams contrast;
  contrast.contrast = 100.0;
  const auto contrasty = patchy::raw::build_tone_lut(contrast);
  CHECK(contrasty[16384] < 16384);
  CHECK(contrasty[49151] > 49151);
  CHECK(contrasty[0] == 0);
  CHECK(contrasty[65535] == 65535);
  contrast.contrast = -100.0;
  const auto flat = patchy::raw::build_tone_lut(contrast);
  CHECK(flat[16384] > 16384);
  CHECK(flat[49151] < 49151);

  // Shadows: lifts the dark band, pins pure black, leaves highlights alone.
  patchy::raw::ToneParams shadows;
  shadows.shadows = 100.0;
  const auto lifted = patchy::raw::build_tone_lut(shadows);
  CHECK(lifted[0] == 0);
  CHECK(lifted[9830] > 9830 + 3000);   // 0.15: band center clearly lifted
  CHECK(lifted[58982] == 58982);       // 0.9: untouched

  // Highlights: negative values dim the bright end (including full white) and leave
  // shadows alone.
  patchy::raw::ToneParams highlights;
  highlights.highlights = -100.0;
  const auto recovered = patchy::raw::build_tone_lut(highlights);
  CHECK(recovered[65535] < 65535 - 10000);
  CHECK(recovered[6553] == 6553);  // 0.1: untouched

  // Saturation converges channels to luma at -100 and widens the spread at +100;
  // vibrance moves an already-saturated pixel LESS than plain saturation does.
  const std::array<std::uint16_t, 3> source = {40000, 20000, 10000};
  auto desaturated = source;
  patchy::raw::apply_color(std::span<std::uint16_t>(desaturated), -100.0, 0.0);
  CHECK(std::abs(int(desaturated[0]) - int(desaturated[1])) <= 1);
  CHECK(std::abs(int(desaturated[2]) - int(desaturated[1])) <= 1);
  auto saturated = source;
  patchy::raw::apply_color(std::span<std::uint16_t>(saturated), 80.0, 0.0);
  auto vibrant = source;
  patchy::raw::apply_color(std::span<std::uint16_t>(vibrant), 0.0, 80.0);
  const auto source_spread = int(source[0]) - int(source[2]);
  const auto saturated_spread = int(saturated[0]) - int(saturated[2]);
  const auto vibrant_spread = int(vibrant[0]) - int(vibrant[2]);
  CHECK(saturated_spread > source_spread);
  CHECK(vibrant_spread > source_spread);
  CHECK(vibrant_spread < saturated_spread);
}

void raw_develop_tone_and_color_controls_shift_output() {
  // Bright ramp (0 -> 90% full scale): distinct shadow and highlight regions.
  SyntheticDngOptions ramp;
  ramp.red_value = 58982;
  ramp.green_value = 58982;
  ramp.blue_value = 58982;
  ramp.horizontal_ramp = true;
  const auto ramp_dng = synthetic_bayer_dng(128, 96, ramp);

  const auto quarter_green_mean = [](const patchy::Document& document, bool right_quarter) {
    const auto& pixels = document.layers().front().pixels();
    const auto begin_x = right_quarter ? pixels.width() * 3 / 4 : 0;
    const auto end_x = right_quarter ? pixels.width() : pixels.width() / 4;
    double total = 0.0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = begin_x; x < end_x; ++x) {
        total += pixels.pixel(x, y)[1];
      }
    }
    return total / (static_cast<double>(end_x - begin_x) * pixels.height());
  };

  patchy::raw::DevelopParams neutral;
  const auto base = patchy::raw::read_camera_raw(ramp_dng, neutral).document;
  const auto base_dark = quarter_green_mean(base, false);
  const auto base_bright = quarter_green_mean(base, true);
  CHECK(base_dark > 5.0);
  CHECK(base_bright > base_dark + 60.0);

  auto contrast_params = neutral;
  contrast_params.contrast = 80.0;
  const auto contrasty = patchy::raw::read_camera_raw(ramp_dng, contrast_params).document;
  CHECK(quarter_green_mean(contrasty, false) < base_dark - 4.0);
  CHECK(quarter_green_mean(contrasty, true) > base_bright + 1.0);

  auto shadow_params = neutral;
  shadow_params.shadows = 80.0;
  const auto shadow_lifted = patchy::raw::read_camera_raw(ramp_dng, shadow_params).document;
  const auto lifted_dark_delta = quarter_green_mean(shadow_lifted, false) - base_dark;
  const auto lifted_bright_delta = std::abs(quarter_green_mean(shadow_lifted, true) - base_bright);
  CHECK(lifted_dark_delta > 8.0);
  CHECK(lifted_bright_delta < 2.0);

  auto highlight_params = neutral;
  highlight_params.highlights = -80.0;
  const auto highlight_recovered = patchy::raw::read_camera_raw(ramp_dng, highlight_params).document;
  const auto recovered_bright_delta = base_bright - quarter_green_mean(highlight_recovered, true);
  const auto recovered_dark_delta = std::abs(base_dark - quarter_green_mean(highlight_recovered, false));
  CHECK(recovered_bright_delta > 20.0);
  CHECK(recovered_dark_delta * 4.0 < recovered_bright_delta);

  // Colored scene (red-heavy) for the color controls.
  SyntheticDngOptions colored;
  colored.red_value = 23592;
  colored.green_value = 11796;
  colored.blue_value = 5898;
  const auto colored_dng = synthetic_bayer_dng(128, 96, colored);
  const auto colored_base = raw_channel_means(patchy::raw::read_camera_raw(colored_dng, neutral).document);
  const auto base_spread = colored_base.red - colored_base.blue;
  CHECK(base_spread > 20.0);

  auto desaturate_params = neutral;
  desaturate_params.saturation = -100.0;
  const auto gray = raw_channel_means(patchy::raw::read_camera_raw(colored_dng, desaturate_params).document);
  CHECK(std::abs(gray.red - gray.green) < 3.0);
  CHECK(std::abs(gray.blue - gray.green) < 3.0);

  auto saturate_params = neutral;
  saturate_params.saturation = 80.0;
  const auto vivid = raw_channel_means(patchy::raw::read_camera_raw(colored_dng, saturate_params).document);
  auto vibrance_params = neutral;
  vibrance_params.vibrance = 80.0;
  const auto vibrant = raw_channel_means(patchy::raw::read_camera_raw(colored_dng, vibrance_params).document);
  const auto vivid_spread = vivid.red - vivid.blue;
  const auto vibrant_spread = vibrant.red - vibrant.blue;
  CHECK(vivid_spread > base_spread + 8.0);
  CHECK(vibrant_spread > base_spread + 2.0);
  // Vibrance holds back on an already-saturated subject.
  CHECK(vibrant_spread < vivid_spread);
}

void raw_develop_half_size_and_denoise_run() {
  const auto dng = synthetic_bayer_dng(128, 96);
  patchy::raw::DevelopParams params;
  params.auto_brighten = false;
  params.half_size = true;
  const auto half = patchy::raw::read_camera_raw(dng, params).document;
  CHECK(half.width() == 64);
  CHECK(half.height() == 48);

  // The remaining pipeline stages must at least run cleanly end to end.
  patchy::raw::DevelopParams heavy;
  heavy.auto_brighten = true;
  heavy.exposure_ev = 0.5;
  heavy.highlight_recovery = patchy::raw::HighlightMode::Rebuild;
  heavy.demosaic = patchy::raw::DemosaicAlgorithm::Dht;
  heavy.wavelet_denoise_threshold = 300;
  heavy.fbdd = patchy::raw::FbddNoiseReduction::Full;
  const auto processed = patchy::raw::read_camera_raw(dng, heavy).document;
  CHECK(processed.width() == 128);
  CHECK(processed.height() == 96);
}

void raw_develop_session_reports_info_and_orientation() {
  SyntheticDngOptions options;
  options.orientation = 6;  // rotate 90 CW: output swaps to portrait
  const auto dng = synthetic_bayer_dng(128, 96, options);
  patchy::raw::DevelopSession session(std::vector<std::uint8_t>(dng.begin(), dng.end()));
  const auto& info = session.info();
  CHECK(info.camera_model.find("Synthetic") != std::string::npos);
  CHECK(info.output_width == 96);
  CHECK(info.output_height == 128);
  CHECK(info.orientation_flip == 6);
  CHECK(!info.is_xtrans);
  // AsShotNeutral encodes D65 through the sRGB matrix; the derived display value must
  // land near daylight with a small tint.
  CHECK(info.as_shot_white_balance.has_value());
  CHECK(info.as_shot_white_balance->temperature_k > 5000.0);
  CHECK(info.as_shot_white_balance->temperature_k < 8000.0);
  CHECK(std::abs(info.as_shot_white_balance->tint) < 15.0);

  patchy::raw::DevelopParams params;
  params.auto_brighten = false;
  const auto developed = session.develop(params);
  CHECK(developed.width == 96);
  CHECK(developed.height == 128);
  // Re-develop with new parameters on the same session (the preview loop's contract).
  auto brighter = params;
  brighter.exposure_ev = 1.0;
  const auto second = session.develop(brighter);
  CHECK(second.width == 96);
  CHECK(second.height == 128);
}

void raw_develop_rejects_non_raw_bytes() {
  bool rejected = false;
  try {
    const std::vector<std::uint8_t> garbage(4096, 0x5A);
    (void)patchy::raw::read_camera_raw(garbage, {});
  } catch (const std::exception& error) {
    rejected = true;
    CHECK(std::string(error.what()).find("not a supported camera raw file") != std::string::npos);
  }
  CHECK(rejected);

  // A structurally valid DNG whose pixel strip is cut short must error, not crash.
  auto truncated = synthetic_bayer_dng(128, 96);
  truncated.resize(truncated.size() - 128 * 96);  // drop half the samples
  bool truncated_rejected = false;
  try {
    (void)patchy::raw::read_camera_raw(truncated, {});
  } catch (const std::exception&) {
    truncated_rejected = true;
  }
  CHECK(truncated_rejected);
}

void raw_decodes_real_camera_samples_if_available() {
  const auto directory = patchy::test::source_root_path() / "local-test-fixtures" / "raw";
  if (!std::filesystem::exists(directory)) {
    std::cout << "[SKIP] local raw fixtures missing: " << directory.string() << '\n';
    return;
  }
  std::size_t decoded = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto extension = entry.path().extension().string();
    if (!extension.empty() && extension.front() == '.') {
      extension.erase(extension.begin());
    }
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!patchy::raw::is_camera_raw_extension(extension)) {
      continue;
    }
    std::ifstream stream(entry.path(), std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
    patchy::raw::DevelopSession session(std::move(bytes));
    CHECK(session.info().output_width > 0);
    CHECK(session.info().output_height > 0);
    patchy::raw::DevelopParams params;
    params.half_size = true;  // keep the suite fast on 40+ MP samples
    const auto developed = session.develop(params);
    CHECK(developed.width > 0);
    CHECK(developed.height > 0);
    ++decoded;
    std::cout << "[INFO] developed " << entry.path().filename().string() << " ("
              << session.info().camera_make << ' ' << session.info().camera_model << ", "
              << developed.width << 'x' << developed.height << " half size)\n";
  }
  CHECK(decoded > 0);
}

void heif_extensions_sniff_and_registry_routing() {
  CHECK(patchy::heif::is_heif_extension("heic"));
  CHECK(patchy::heif::is_heif_extension(".HEIF"));
  CHECK(patchy::heif::is_heif_extension("hif"));
  CHECK(!patchy::heif::is_heif_extension("jpg"));

  // Registry dispatch is read-only (no writer), which is what routes Save to Save As.
  const auto* handler = patchy::builtin_format_registry().find_by_extension(".heic");
  CHECK(handler != nullptr);
  CHECK(!handler->can_write());
  CHECK(patchy::builtin_format_registry().find_by_extension(".hif") == handler);

  // The committed fixture (authored from a Patchy PNG via macOS sips) is brand "heic".
  const auto fixture =
      read_binary_file(patchy::test::committed_format_fixture_path("heif", "quadrants.heic"));
  CHECK(fixture.size() > 16);
  CHECK(patchy::heif::sniff(fixture));

  const auto synthetic_ftyp = [](std::string_view brand) {
    std::vector<std::uint8_t> bytes = {0, 0, 0, 16, 'f', 't', 'y', 'p'};
    bytes.insert(bytes.end(), brand.begin(), brand.end());
    bytes.insert(bytes.end(), {0, 0, 0, 0});
    return bytes;
  };
  CHECK(patchy::heif::sniff(synthetic_ftyp("heix")));  // Sony/Fuji .hif
  CHECK(patchy::heif::sniff(synthetic_ftyp("msf1")));
  // AVIF shares the container but is deliberately not routed to the HEIF reader.
  CHECK(!patchy::heif::sniff(synthetic_ftyp("avif")));
  const std::vector<std::uint8_t> png_magic = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
                                               0,    0,   0,   13,  'I',  'H',  'D',  'R'};
  CHECK(!patchy::heif::sniff(png_magic));
}

void heif_orientation_mapping_matches_exif_semantics() {
  // 2x2 source (red channel carries the pixel id):  1 2 / 3 4.
  std::vector<std::uint8_t> source;
  for (std::uint8_t id = 1; id <= 4; ++id) {
    source.insert(source.end(), {id, 0, 0, 255});
  }
  const auto red_at = [](const patchy::heif::OrientedImage& image, std::int32_t x, std::int32_t y) {
    return image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                       static_cast<std::size_t>(x)) *
                      4U];
  };
  // Expected row-major ids per EXIF orientation, derived from "value describes where the
  // stored 0th row/column appear visually".
  const std::array<std::array<std::uint8_t, 4>, 9> expected = {{
      {1, 2, 3, 4},  // [0] unused
      {1, 2, 3, 4},  // 1 identity
      {2, 1, 4, 3},  // 2 mirrored horizontally
      {4, 3, 2, 1},  // 3 rotated 180
      {3, 4, 1, 2},  // 4 mirrored vertically
      {1, 3, 2, 4},  // 5 transposed
      {3, 1, 4, 2},  // 6 rotate 90 CW to display
      {4, 2, 3, 1},  // 7 transverse
      {2, 4, 1, 3},  // 8 rotate 90 CCW to display
  }};
  for (int orientation = 1; orientation <= 8; ++orientation) {
    const auto oriented = patchy::heif::apply_exif_orientation(source, 2, 2, orientation);
    CHECK(oriented.width == 2);
    CHECK(oriented.height == 2);
    for (std::int32_t index = 0; index < 4; ++index) {
      CHECK(red_at(oriented, index % 2, index / 2) ==
            expected[static_cast<std::size_t>(orientation)][static_cast<std::size_t>(index)]);
    }
  }

  // Orientations 5-8 swap the dimensions: a 2x3 source rotated 90 CW displays as 3x2.
  std::vector<std::uint8_t> tall;
  for (std::uint8_t id = 1; id <= 6; ++id) {
    tall.insert(tall.end(), {id, 0, 0, 255});
  }
  const auto rotated = patchy::heif::apply_exif_orientation(tall, 2, 3, 6);
  CHECK(rotated.width == 3);
  CHECK(rotated.height == 2);
  const std::array<std::uint8_t, 6> rotated_expected = {5, 3, 1, 6, 4, 2};
  for (std::int32_t index = 0; index < 6; ++index) {
    CHECK(red_at(rotated, index % 3, index / 3) == rotated_expected[static_cast<std::size_t>(index)]);
  }

  // Out-of-range values are treated as "no orientation", matching the reader's fallback.
  const auto passthrough = patchy::heif::apply_exif_orientation(source, 2, 2, 0);
  CHECK(passthrough.rgba == std::vector<std::uint8_t>(source.begin(), source.end()));
}

void heif_reads_quadrant_fixture_if_available() {
  const auto bytes =
      read_binary_file(patchy::test::committed_format_fixture_path("heif", "quadrants.heic"));
  CHECK(!bytes.empty());
  patchy::FormatReadResult result;
  try {
    result = patchy::heif::read_heif(bytes);
  } catch (const std::exception& error) {
    // Expected wherever no platform decoder exists: the non-Windows read always throws
    // (macOS/Linux decode through Qt plugins the core suite does not link), and Windows
    // throws marker-prefixed errors when the Store codec packages are absent. Anything
    // else is a real failure.
    const std::string message = error.what();
    const auto starts_with = [&message](std::string_view prefix) {
      return message.rfind(std::string(prefix), 0) == 0;
    };
    if (starts_with(patchy::heif::kHeifPackageMissingMarker) ||
        starts_with(patchy::heif::kHevcPackageMissingMarker) ||
        message.find("system codec") != std::string::npos ||
        message.find("Flatpak codec extension") != std::string::npos) {
      std::cout << "[SKIP] HEIC platform decoder unavailable: " << message << '\n';
      return;
    }
    throw;
  }

  CHECK(result.document.width() == 64);
  CHECK(result.document.height() == 48);
  CHECK(result.document.layers().size() == 1);
  CHECK(result.document.layers().front().name() == "Background");

  // Statistics only, never byte pins: HEVC decode is lossy-coded (4:2:0) and the color
  // conversion runs through the platform CMS. Sample quadrant interiors (6 px inset keeps
  // chroma-subsampling edge bleed out of the means).
  const auto& pixels = std::as_const(result.document.layers().front()).pixels();
  const auto channels = pixels.format().channels;
  CHECK(channels >= 3);
  const auto quadrant_mean = [&](std::int32_t x0, std::int32_t y0) {
    double sums[3] = {0.0, 0.0, 0.0};
    int count = 0;
    for (std::int32_t y = y0 + 6; y < y0 + 24 - 6; ++y) {
      for (std::int32_t x = x0 + 6; x < x0 + 32 - 6; ++x) {
        const auto* px = pixels.pixel(x, y);
        for (int channel = 0; channel < 3; ++channel) {
          sums[channel] += px[channel];
        }
        ++count;
      }
    }
    return std::array<double, 3>{sums[0] / count, sums[1] / count, sums[2] / count};
  };
  const auto top_left = quadrant_mean(0, 0);       // red
  const auto top_right = quadrant_mean(32, 0);     // green
  const auto bottom_left = quadrant_mean(0, 24);   // blue
  const auto bottom_right = quadrant_mean(32, 24); // white
  CHECK(top_left[0] > 200.0);
  CHECK(top_left[1] < 80.0);
  CHECK(top_left[2] < 80.0);
  CHECK(top_right[1] > 200.0);
  CHECK(top_right[0] < 100.0);
  CHECK(top_right[2] < 100.0);
  CHECK(bottom_left[2] > 200.0);
  CHECK(bottom_left[0] < 80.0);
  CHECK(bottom_left[1] < 80.0);
  CHECK(bottom_right[0] > 200.0);
  CHECK(bottom_right[1] > 200.0);
  CHECK(bottom_right[2] > 200.0);
}

void heif_decodes_real_photos_if_available() {
  // Real HEICs (e.g. iPhone captures: Display P3, camera orientation) live in the
  // untracked local fixtures; remotes and codec-less machines [SKIP].
  const auto directory = patchy::test::source_root_path() / "local-test-fixtures" / "heif";
  if (!std::filesystem::exists(directory)) {
    std::cout << "[SKIP] local heif fixtures missing: " << directory.string() << '\n';
    return;
  }
  std::size_t decoded = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto extension = entry.path().extension().string();
    if (!extension.empty() && extension.front() == '.') {
      extension.erase(extension.begin());
    }
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!patchy::heif::is_heif_extension(extension)) {
      continue;
    }
    const auto bytes = read_binary_file(entry.path());
    CHECK(patchy::heif::sniff(bytes));
    try {
      const auto result = patchy::heif::read_heif(bytes);
      CHECK(result.document.width() > 0);
      CHECK(result.document.height() > 0);
      // quadrants-p3.heic is the committed quadrant art re-encoded through the Display P3
      // profile (macOS sips --matchTo). Its top-left quadrant stores sRGB-pure red as P3
      // coordinates (~234, 51, 35); only a decoder that APPLIES the embedded profile
      // returns ~ (255, 0, 0), so these thresholds pin the ICC -> sRGB conversion (an
      // unmanaged decode reads green ~51 and fails).
      if (entry.path().filename().string().rfind("quadrants-p3", 0) == 0) {
        const auto& pixels = std::as_const(result.document.layers().front()).pixels();
        double red_sum = 0.0;
        double green_sum = 0.0;
        int count = 0;
        for (std::int32_t y = 6; y < 18; ++y) {
          for (std::int32_t x = 6; x < 26; ++x) {
            const auto* px = pixels.pixel(x, y);
            red_sum += px[0];
            green_sum += px[1];
            ++count;
          }
        }
        CHECK(red_sum / count > 245.0);
        CHECK(green_sum / count < 30.0);
      }
      ++decoded;
      std::cout << "[INFO] decoded " << entry.path().filename().string() << " ("
                << result.document.width() << 'x' << result.document.height() << ")\n";
    } catch (const std::exception& error) {
      std::cout << "[SKIP] HEIC platform decoder unavailable: " << error.what() << '\n';
      return;
    }
  }
  CHECK(decoded > 0);
}

}  // namespace

std::vector<patchy::test::TestCase> raw_heif_tests() {
  return {
      {"raw_white_balance_round_trips_temperature_and_tint", raw_white_balance_round_trips_temperature_and_tint},
      {"raw_develop_reads_synthetic_dng", raw_develop_reads_synthetic_dng},
      {"raw_develop_exposure_and_white_balance_shift_output",
       raw_develop_exposure_and_white_balance_shift_output},
      {"raw_tone_lut_and_color_math", raw_tone_lut_and_color_math},
      {"raw_develop_tone_and_color_controls_shift_output",
       raw_develop_tone_and_color_controls_shift_output},
      {"raw_develop_half_size_and_denoise_run", raw_develop_half_size_and_denoise_run},
      {"raw_develop_session_reports_info_and_orientation", raw_develop_session_reports_info_and_orientation},
      {"raw_develop_rejects_non_raw_bytes", raw_develop_rejects_non_raw_bytes},
      {"raw_decodes_real_camera_samples_if_available", raw_decodes_real_camera_samples_if_available},
      {"heif_extensions_sniff_and_registry_routing", heif_extensions_sniff_and_registry_routing},
      {"heif_orientation_mapping_matches_exif_semantics", heif_orientation_mapping_matches_exif_semantics},
      {"heif_reads_quadrant_fixture_if_available", heif_reads_quadrant_fixture_if_available},
      {"heif_decodes_real_photos_if_available", heif_decodes_real_photos_if_available},
  };
}
