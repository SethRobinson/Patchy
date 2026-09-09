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
  // Keep the existing adjustment calibration independent of the base profile.
  neutral.profile = patchy::raw::RenderingProfile::Neutral;
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
  heavy.noise_reduction = patchy::raw::NoiseReductionMode::Manual;
  heavy.wavelet_denoise_threshold = 300;
  heavy.fbdd = patchy::raw::FbddNoiseReduction::Full;
  const auto processed = patchy::raw::read_camera_raw(dng, heavy).document;
  CHECK(processed.width() == 128);
  CHECK(processed.height() == 96);
}

void raw_auto_noise_policy_and_modes() {
  using namespace patchy::raw;
  RawFileInfo info;
  info.is_three_color_bayer = true;
  DevelopParams params;
  const std::array<double, 8> isos{100, 400, 800, 1600, 3200, 5000, 6400, 25600};
  const std::array<int, 8> thresholds{0, 0, 100, 200, 300, 364, 400, 500};
  const std::array<int, 8> original_thresholds{0, 0, 50, 100, 150, 182, 200, 250};
  for (std::size_t i = 0; i < isos.size(); ++i) {
    info.iso = isos[i];
    const auto result = effective_noise_reduction(params, info);
    CHECK(result.auto_available);
    CHECK(result.wavelet_threshold == thresholds[i]);
    CHECK(result.fbdd == (info.iso >= 1600 ? FbddNoiseReduction::Full :
                         info.iso >= 800 ? FbddNoiseReduction::Light : FbddNoiseReduction::Off));
    CHECK(result.color_passes == (info.iso >= 1600 ? 2 : info.iso >= 800 ? 1 : 0));
    params.processing_version = 1;
    const auto original = effective_noise_reduction(params, info);
    CHECK(original.wavelet_threshold == original_thresholds[i]);
    CHECK(original.color_passes == 0);
    params.processing_version = kProcessingVersion;
  }
  for (const double iso : {799.999, 800.0, 1599.999, 1600.0}) {
    info.iso = iso;
    CHECK(effective_noise_reduction(params, info).color_passes == (iso >= 1600 ? 2 : iso >= 800 ? 1 : 0));
  }
  params.processing_version = 1;
  CHECK(effective_noise_reduction(params, info).color_passes == 0);
  params.processing_version = kProcessingVersion;
  info.iso = 0;
  CHECK(!effective_noise_reduction(params, info).auto_available);
  CHECK(effective_noise_reduction(params, info).color_passes == 0);
  info.iso = std::numeric_limits<double>::quiet_NaN();
  CHECK(effective_noise_reduction(params, info).color_passes == 0);
  info.iso = 5000;
  info.is_three_color_bayer = false;
  CHECK(effective_noise_reduction(params, info).wavelet_threshold == 0);
  params.noise_reduction = NoiseReductionMode::Manual;
  params.wavelet_denoise_threshold = 333;
  params.fbdd = FbddNoiseReduction::Light;
  params.color_denoise_passes = 3;
  CHECK(effective_noise_reduction(params, info).color_passes == 0);
  info.is_three_color_bayer = true;
  CHECK(effective_noise_reduction(params, info).color_passes == 3);
  CHECK(effective_noise_reduction(params, info).wavelet_threshold == 333);
  CHECK(effective_noise_reduction(params, info).fbdd == FbddNoiseReduction::Light);
  params.noise_reduction = NoiseReductionMode::Off;
  CHECK(effective_noise_reduction(params, info).wavelet_threshold == 0);
  CHECK(effective_noise_reduction(params, info).fbdd == FbddNoiseReduction::Off);
  CHECK(effective_noise_reduction(params, info).color_passes == 0);
  params.exposure_ev = std::numeric_limits<double>::quiet_NaN();
  params.brightness = std::numeric_limits<double>::infinity();
  const auto normalized = normalize_develop_params(params);
  CHECK(normalized.exposure_ev == 0);
  CHECK(normalized.brightness == 1);
}

void raw_final_half_size_preserves_processing_and_cancellation_recovers() {
  using namespace patchy::raw;
  SyntheticDngOptions fixture;
  fixture.horizontal_ramp = true;
  fixture.noise_amplitude = 700;
  fixture.iso = 5000;
  // Odd dimensions exercise partial 2x2 blocks after LibRaw's own active-area handling.
  DevelopSession session(synthetic_bayer_dng(257, 193, fixture));
  CHECK(session.info().iso == 5000);
  DevelopParams params;
  const auto full = session.develop(params);
  CHECK(full.noise.wavelet_threshold == 364);
  CHECK(full.noise.fbdd == FbddNoiseReduction::Full);
  CHECK(full.noise.color_passes == 2);
  const auto draft = session.develop(params, {DevelopQuality::Draft, {}});
  CHECK(draft.output_width == full.width);
  CHECK(draft.output_height == full.height);
  CHECK(draft.width < draft.output_width);
  CHECK(draft.fast_half_size && !draft.demosaic);
  CHECK(draft.noise.fbdd == FbddNoiseReduction::Off);
  CHECK(draft.noise.color_passes == 0);
  CHECK(full.demosaic == DemosaicAlgorithm::Ahd);
  params.half_size = true;
  const auto half = session.develop(params);
  CHECK(half.width == (full.width + 1) / 2);
  CHECK(half.height == (full.height + 1) / 2);
  for (int y = 0; y < half.height; ++y) {
    for (int x = 0; x < half.width; ++x) {
      for (std::size_t c = 0; c < 3; ++c) {
        int sum = 0, count = 0;
        for (int sy = 2 * y; sy < std::min(2 * y + 2, full.height); ++sy)
          for (int sx = 2 * x; sx < std::min(2 * x + 2, full.width); ++sx) {
            sum += full.rgb[(static_cast<std::size_t>(sy) * full.width + sx) * 3 + c];
            ++count;
          }
        const int actual = half.rgb[(static_cast<std::size_t>(y) * half.width + x) * 3 + c];
        CHECK(std::abs(actual - (sum + count / 2) / count) <= 1);
      }
    }
  }
  bool cancelled = false;
  int checkpoints = 0;
  try { (void)session.develop(params, {DevelopQuality::Final, [&] { return ++checkpoints >= 2; }}); }
  catch (const DevelopCancelled&) { cancelled = true; }
  CHECK(cancelled);
  const auto recovered = session.develop(params);
  CHECK(recovered.width == half.width);
  CHECK(recovered.rgb == half.rgb); // same process, not a cross-toolchain byte pin
}

void raw_auto_denoise_reduces_chroma_without_color_shift() {
  using namespace patchy::raw;
  SyntheticDngOptions fixture;
  fixture.iso = 5000;
  fixture.noise_amplitude = 1800;
  DevelopSession session(synthetic_bayer_dng(256, 256, fixture));
  DevelopParams params;
  const auto clean = session.develop(params);
  params.noise_reduction = NoiseReductionMode::Off;
  const auto noisy = session.develop(params);
  const auto stats = [](const DevelopSession::DevelopedImage& image) {
    std::array<double, 2> result{};
    for (int y = 16; y < image.height - 16; ++y)
      for (int x = 16; x < image.width - 16; ++x) {
        const auto* pixel = image.rgb.data() + (static_cast<std::size_t>(y) * image.width + x) * 3;
        const auto rg = static_cast<double>(pixel[0]) - pixel[1];
        const auto bg = static_cast<double>(pixel[2]) - pixel[1];
        result[0] += rg * rg + bg * bg;
        result[1] += (pixel[0] + pixel[1] + pixel[2]) / 3.0;
      }
    const auto count = static_cast<double>((image.width - 32) * (image.height - 32));
    result[0] /= count;
    result[1] /= count;
    return result;
  };
  const auto before = stats(noisy), after = stats(clean);
  CHECK(after[0] < before[0] * 0.7);
  CHECK(std::abs(after[1] - before[1]) < 4.0);
  fixture.red_value = 16000;
  fixture.blue_value = 8000;
  DevelopSession colored(synthetic_bayer_dng(256, 256, fixture));
  params.white_balance = WhiteBalanceMode::Auto;
  const auto automatic = colored.develop(params);
  CHECK(automatic.white_balance_multipliers.has_value());
  CHECK(automatic.effective_white_balance.has_value());
  CHECK(std::abs((*automatic.white_balance_multipliers)[0] - 1.0) > 0.1);
  CHECK(std::abs((*automatic.white_balance_multipliers)[2] - 1.0) > 0.1);
}

void raw_natural_profile_preserves_hues_and_tonal_detail() {
  using namespace patchy::raw;
  const auto lut = build_natural_profile_lut();
  const auto previous = build_natural_profile_lut(2);
  CHECK(lut[32768] > previous[32768] + 4000);
  CHECK(lut.front() == 0 && lut.back() == 65535);
  CHECK(lut[6553] < 6553); // deeper shadows
  CHECK(lut[32768] > 39000); // brighter midtones
  CHECK(lut[62000] < 65000); // highlight detail survives
  for (std::size_t i = 1; i < lut.size(); ++i) CHECK(lut[i] >= lut[i - 1]);
  for (int value = 0; value <= 65535; value += 257) {
    std::array<std::uint16_t, 3> gray{static_cast<std::uint16_t>(value), static_cast<std::uint16_t>(value), static_cast<std::uint16_t>(value)};
    apply_natural_profile(gray, lut);
    CHECK(gray[0] == gray[1] && gray[1] == gray[2]);
    CHECK(std::abs(int(gray[0]) - int(lut[static_cast<std::size_t>(value)])) <= 1);
  }
  // Hue is the relative position of the middle channel between the extremes.
  // Test the gamut boundary as well as muted colors and different channel orders.
  for (const auto source : {std::array<std::uint16_t, 3>{60000, 15000, 4000},
                           std::array<std::uint16_t, 3>{8000, 32000, 18000},
                           std::array<std::uint16_t, 3>{24000, 20000, 45000},
                           std::array<std::uint16_t, 3>{65535, 42000, 0}}) {
    auto mapped = source;
    apply_natural_profile(mapped, lut);
    std::array<std::size_t, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return source[a] < source[b]; });
    CHECK(mapped[order[0]] < mapped[order[1]] && mapped[order[1]] < mapped[order[2]]);
    const double before = double(source[order[1]] - source[order[0]]) / (source[order[2]] - source[order[0]]);
    const double after = double(mapped[order[1]] - mapped[order[0]]) / (mapped[order[2]] - mapped[order[0]]);
    CHECK(std::abs(before - after) < 0.001);
  }
}

void raw_small_drafts_cache_color_processing_and_report_progress() {
  using namespace patchy::raw;
  SyntheticDngOptions fixture;
  fixture.iso = 5000;
  fixture.horizontal_ramp = true;
  fixture.orientation = 6;
  const auto bytes = synthetic_bayer_dng(2601, 1703, fixture);
  DevelopSession session(bytes);
  DevelopParams params;
  params.noise_reduction = NoiseReductionMode::Off;
  std::vector<int> progress;
  DevelopOptions options;
  options.progress = [&](int percent) { progress.push_back(percent); };
  const auto full = session.develop(params, options);
  CHECK(progress.front() == 0 && progress.back() == 100 && progress.size() > 5);
  CHECK(std::is_sorted(progress.begin(), progress.end()));
  options.quality = DevelopQuality::Draft;
  progress.clear();
  const auto draft = session.develop(params, options);
  CHECK(std::max(draft.width, draft.height) <= 1280);
  CHECK(draft.output_width == full.width && draft.output_height == full.height);
  CHECK(draft.width < draft.height); // orientation survives the sensor proxy
  CHECK(!draft.reused_draft_decode && draft.noise.wavelet_threshold == 0);
  CHECK(progress.front() == 0 && progress.back() == 100 && std::is_sorted(progress.begin(), progress.end()));
  CHECK(draft.white_balance_multipliers == full.white_balance_multipliers);
  params.contrast = 30;
  params.saturation = 15;
  params.noise_reduction = NoiseReductionMode::Auto;
  const auto edited = session.develop(params, options);
  CHECK(edited.reused_draft_decode && edited.rgb != draft.rgb);
  CHECK(edited.noise.wavelet_threshold == 0 && edited.noise.color_passes == 0);
  DevelopSession fresh(bytes);
  CHECK(fresh.develop(params, options).rgb == edited.rgb);
  params.exposure_ev = 0.5;
  CHECK(!session.develop(params, options).reused_draft_decode);
  params = {};
  params.noise_reduction = NoiseReductionMode::Off;
  CHECK(session.develop(params).rgb == full.rgb); // proxy never changes final geometry/pixels
}

void raw_profiles_and_color_noise_preserve_legacy_processing() {
  using namespace patchy::raw;
  SyntheticDngOptions fixture;
  fixture.iso = 4000;
  fixture.noise_amplitude = 1700;
  DevelopSession session(synthetic_bayer_dng(256, 192, fixture));
  DevelopParams legacy;
  legacy.processing_version = 1;
  legacy.contrast = 12;
  const auto old = session.develop(legacy);
  CHECK(old.profile == RenderingProfile::Neutral && old.processing_version == 1);
  CHECK(old.noise.color_passes == 0);
  auto current = normalize_develop_params(legacy);
  current.processing_version = kProcessingVersion;
  current.noise_reduction = NoiseReductionMode::Manual;
  current.wavelet_denoise_threshold = old.noise.wavelet_threshold;
  current.fbdd = old.noise.fbdd;
  const auto equivalent = session.develop(current);
  CHECK(equivalent.rgb == old.rgb); // same-process compatibility, not a byte canary
  current.color_denoise_passes = 2;
  const auto cleaned = session.develop(current);
  const auto chroma = [](const DevelopSession::DevelopedImage& image) {
    double total = 0;
    for (int y = 16; y < image.height - 16; ++y)
      for (int x = 16; x < image.width - 16; ++x) {
        const auto* p = image.rgb.data() + (static_cast<std::size_t>(y) * image.width + x) * 3;
        total += std::pow(double(p[0]) - p[1], 2) + std::pow(double(p[2]) - p[1], 2);
      }
    return total;
  };
  CHECK(cleaned.noise.color_passes == 2);
  CHECK(chroma(cleaned) < chroma(old) * 0.65);
  const auto natural = session.develop({});
  CHECK(natural.profile == RenderingProfile::Natural && natural.processing_version == kProcessingVersion);
  CHECK(natural.rgb != old.rgb);
}

void raw_color_noise_preserves_thin_lines_and_color_edges() {
  using namespace patchy::raw;
  constexpr int width = 256, height = 128;
  auto bytes = synthetic_bayer_dng(width, height);
  const auto offset = bytes.size() - width * height * 2;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int channel = (y % 2 == 0 && x % 2 == 0) ? 0 : (y % 2 != 0 && x % 2 != 0) ? 2 : 1;
      const std::array<int, 3> rgb = x < width / 2 ? std::array{30000, 9000, 6000} : std::array{6000, 9000, 30000};
      const int value = x >= 80 && x < 86 ? 45000 : rgb[static_cast<std::size_t>(channel)];
      const auto index = offset + static_cast<std::size_t>(y * width + x) * 2;
      bytes[index] = static_cast<std::uint8_t>(value & 255);
      bytes[index + 1] = static_cast<std::uint8_t>(value >> 8);
    }
  }
  DevelopSession session(std::move(bytes));
  DevelopParams params;
  params.profile = RenderingProfile::Neutral;
  params.noise_reduction = NoiseReductionMode::Manual;
  const auto original = session.develop(params);
  params.color_denoise_passes = 2;
  const auto clean = session.develop(params);
  const auto sample = [](const auto& image, int x, int c) {
    return int(image.rgb[(static_cast<std::size_t>(image.height / 2) * image.width + x) * 3 + c]);
  };
  for (int x : {40, 160, 200})
    for (int c = 0; c < 3; ++c) CHECK(std::abs(sample(clean, x, c) - sample(original, x, c)) <= 1);
  // Keep the thin bright line and the two distinct colors on either side of the edge.
  CHECK(sample(clean, 82, 1) - sample(clean, 70, 1) > 0.9 * (sample(original, 82, 1) - sample(original, 70, 1)));
  CHECK(sample(clean, 122, 0) - sample(clean, 122, 2) > 50);
  CHECK(sample(clean, 134, 2) - sample(clean, 134, 0) > 50);
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
    // Expected wherever no platform decoder exists: macOS/Linux decode through Qt
    // plugins the core suite does not link, wasm-core runs under Node without browser
    // WebCodecs, and Windows reports missing Store codec packages. Anything else is a
    // real failure.
    const std::string message = error.what();
    const auto starts_with = [&message](std::string_view prefix) {
      return message.rfind(std::string(prefix), 0) == 0;
    };
    if (starts_with(patchy::heif::kHeifPackageMissingMarker) ||
        starts_with(patchy::heif::kHevcPackageMissingMarker) ||
        message.find("system codec") != std::string::npos ||
        message.find("Flatpak codec extension") != std::string::npos ||
        message.find("outside a browser") != std::string::npos) {
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
      const std::string message = error.what();
      if (message.starts_with(patchy::heif::kHeifPackageMissingMarker) ||
          message.starts_with(patchy::heif::kHevcPackageMissingMarker) ||
          message.find("system codec") != std::string::npos ||
          message.find("Flatpak codec extension") != std::string::npos ||
          message.find("outside a browser") != std::string::npos) {
        std::cout << "[SKIP] HEIC platform decoder unavailable: " << message << '\n';
        return;
      }
      throw;
    }
  }
  CHECK(decoded > 0);
}

}  // namespace

std::vector<patchy::test::TestCase> raw_heif_tests() {
  return {
      {"raw_small_drafts_cache_color_processing_and_report_progress", raw_small_drafts_cache_color_processing_and_report_progress},
      {"raw_natural_profile_preserves_hues_and_tonal_detail", raw_natural_profile_preserves_hues_and_tonal_detail},
      {"raw_profiles_and_color_noise_preserve_legacy_processing", raw_profiles_and_color_noise_preserve_legacy_processing},
      {"raw_color_noise_preserves_thin_lines_and_color_edges", raw_color_noise_preserves_thin_lines_and_color_edges},
      {"raw_auto_noise_policy_and_modes", raw_auto_noise_policy_and_modes},
      {"raw_final_half_size_preserves_processing_and_cancellation_recovers", raw_final_half_size_preserves_processing_and_cancellation_recovers},
      {"raw_auto_denoise_reduces_chroma_without_color_shift", raw_auto_denoise_reduces_chroma_without_color_shift},
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
