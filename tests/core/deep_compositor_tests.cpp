// The 16/32-bit compositor (docs/high-bit-depth.md, Phase 3): every feature composited
// at 16 bits narrows to what the 8-bit compositor draws (the differential oracle), and
// the deep flatten matches Photoshop's own 16-bit and 32-bit renders of the deep
// fixture corpus (scripts/dev/deep/make_deep_fixtures.py) when it is present.

#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "formats/miniz/miniz.h"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace patchy;

constexpr std::int32_t kWidth = 64;
constexpr std::int32_t kHeight = 32;

PixelBuffer ramp(bool vertical, bool alpha_ramp) {
  PixelBuffer pixels(kWidth, kHeight, PixelFormat::rgba8());
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      auto* p = pixels.pixel(x, y);
      const auto t = vertical ? y * 255 / (kHeight - 1) : x * 255 / (kWidth - 1);
      p[0] = static_cast<std::uint8_t>(vertical ? 255 - t : (t * 3) % 256);
      p[1] = static_cast<std::uint8_t>(vertical ? (t + 60) % 256 : 200 - t / 2);
      p[2] = static_cast<std::uint8_t>(vertical ? t / 2 + 30 : 255 - t);
      p[3] = static_cast<std::uint8_t>(alpha_ramp ? 40 + x * 215 / (kWidth - 1) : 255);
    }
  }
  return pixels;
}

Document two_layer_document(BlendMode mode, float opacity, float fill) {
  Document document(kWidth, kHeight, PixelFormat::rgb8());
  document.add_pixel_layer("Base", ramp(false, false));
  auto& top = document.add_pixel_layer("Top", ramp(true, true));
  top.set_blend_mode(mode);
  top.set_opacity(opacity);
  top.set_fill_opacity(fill);
  return document;
}

// Largest channel difference between the 8-bit flatten and the 16-bit flatten narrowed
// to 8 bits, over pixels both cover, and the fraction of pixels above `tolerance`.
struct Agreement {
  int worst{0};
  double over{0.0};
};

Agreement compare_deep_with_8_bit(const Document& document, int tolerance) {
  std::vector<std::uint8_t> alpha8;
  const auto flat8 = Compositor{}.flatten_rgb8(document, &alpha8);
  auto deep_document = document;
  convert_document_depth(deep_document, BitDepth::UInt16);
  const auto deep = Compositor{}.flatten_rgba_deep(deep_document);
  const auto narrowed = convert_pixel_buffer_depth(deep, BitDepth::UInt8, SampleKind::Color);
  Agreement result;
  int over = 0;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto* a = flat8.pixel(x, y);
      const auto* b = narrowed.pixel(x, y);
      const auto alpha_a = alpha8.empty() ? 255 : alpha8[static_cast<std::size_t>(y * kWidth + x)];
      if (alpha_a == 0 && b[3] == 0) {
        continue;
      }
      int pixel_worst = std::abs(static_cast<int>(alpha_a) - b[3]);
      for (int c = 0; c < 3; ++c) {
        pixel_worst = std::max(pixel_worst, std::abs(static_cast<int>(a[c]) - b[c]));
      }
      result.worst = std::max(result.worst, pixel_worst);
      over += pixel_worst > tolerance ? 1 : 0;
    }
  }
  result.over = static_cast<double>(over) / (kWidth * kHeight);
  return result;
}

void deep_compositor_agrees_with_8_bit_on_every_blend_mode() {
  for (int mode_index = static_cast<int>(BlendMode::Normal); mode_index <= static_cast<int>(BlendMode::Dissolve);
       ++mode_index) {
    const auto mode = static_cast<BlendMode>(mode_index);
    for (const auto& [opacity, fill] : std::vector<std::pair<float, float>>{{1.0F, 1.0F}, {0.6F, 1.0F}, {1.0F, 0.45F}}) {
      const auto agreement = compare_deep_with_8_bit(two_layer_document(mode, opacity, fill), 2);
      // Selection modes: Hard Mix thresholds and Darker/Lighter Color pick one whole
      // color by luma, so a value a hair either side of the decision flips a pixel;
      // Dissolve's paint decision compares the same coverage at two precisions.
      const bool selects = mode == BlendMode::HardMix || mode == BlendMode::Dissolve ||
                           mode == BlendMode::DarkerColor || mode == BlendMode::LighterColor;
      const auto allowed_over = selects ? 0.02 : 0.0;
      if (agreement.over > allowed_over) {
        std::cerr << "blend mode " << mode_index << " opacity " << opacity << " fill " << fill << ": worst "
                  << agreement.worst << ", " << agreement.over * 100.0 << "% over 2\n";
        CHECK(false);
      }
    }
  }
}

void deep_compositor_agrees_with_8_bit_on_masks_groups_clips_and_adjustments() {
  // A gradient layer mask, a clipped member, a pass-through group with opacity and an
  // isolated group, and an adjustment layer of each kind on top.
  std::vector<AdjustmentSettings> adjustments;
  {
    AdjustmentSettings levels;
    levels.kind = AdjustmentKind::Levels;
    levels.levels.black_input = 20;
    levels.levels.white_input = 235;
    levels.levels.gamma_percent = 160;
    adjustments.push_back(levels);
    AdjustmentSettings curves;
    curves.kind = AdjustmentKind::Curves;
    curves.curves.rgb = {{0, 0}, {96, 150}, {255, 255}};
    adjustments.push_back(curves);
    AdjustmentSettings hue;
    hue.kind = AdjustmentKind::HueSaturation;
    hue.hue_saturation.hue_shift = 30;
    hue.hue_saturation.saturation_delta = 20;
    hue.hue_saturation.lightness_delta = -10;
    adjustments.push_back(hue);
    AdjustmentSettings brightness;
    brightness.kind = AdjustmentKind::BrightnessContrast;
    brightness.brightness_contrast.brightness = 25;
    brightness.brightness_contrast.contrast = 30;
    adjustments.push_back(brightness);
    AdjustmentSettings exposure;
    exposure.kind = AdjustmentKind::Exposure;
    exposure.exposure.exposure_hundredths = 60;
    exposure.exposure.gamma_hundredths = 120;
    adjustments.push_back(exposure);
    AdjustmentSettings invert;
    invert.kind = AdjustmentKind::Invert;
    adjustments.push_back(invert);
    AdjustmentSettings balance;
    balance.kind = AdjustmentKind::ColorBalance;
    balance.color_balance.cyan_red = 20;
    balance.color_balance.yellow_blue = -15;
    adjustments.push_back(balance);
  }
  for (std::size_t variant = 0; variant < adjustments.size() + 1U; ++variant) {
    Document document(kWidth, kHeight, PixelFormat::rgb8());
    document.add_pixel_layer("Base", ramp(false, false));
    auto& masked = document.add_pixel_layer("Masked", ramp(true, true));
    LayerMask mask;
    mask.bounds = Rect::from_size(kWidth, kHeight);
    mask.pixels = PixelBuffer(kWidth, kHeight, PixelFormat::gray8());
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>(x * 4);
      }
    }
    masked.set_mask(mask);
    masked.set_blend_mode(BlendMode::Multiply);
    auto& clipped = document.add_pixel_layer("Clipped", ramp(false, true));
    clipped.set_clipped(true);
    clipped.set_blend_mode(BlendMode::Screen);
    Layer group(document.allocate_layer_id(), "Group", LayerKind::Group);
    group.set_blend_mode(BlendMode::PassThrough);
    group.set_opacity(0.7F);
    Layer inner(document.allocate_layer_id(), "Inner", ramp(true, false));
    inner.set_blend_mode(BlendMode::Overlay);
    inner.set_opacity(0.5F);
    group.add_child(std::move(inner));
    document.add_layer(std::move(group));
    if (variant < adjustments.size()) {
      Layer adjustment(document.allocate_layer_id(), "Adjustment", LayerKind::Adjustment);
      configure_adjustment_layer(adjustment, adjustments[variant]);
      adjustment.set_bounds(Rect::from_size(kWidth, kHeight));
      document.add_layer(std::move(adjustment));
    }
    // Byte kernels and LUTs round at each stage; the deep path rounds once, at the end.
    // A steep transfer (Levels' black point under gamma 1.6) magnifies the 8-bit path's
    // input rounding there, so a few pixels may differ by more; the deep side is the
    // accurate one.
    const auto agreement = compare_deep_with_8_bit(document, 3);
    if (agreement.over > 0.02 || agreement.worst > 16) {
      std::cerr << "variant " << variant << ": worst " << agreement.worst << ", " << agreement.over * 100.0
                << "% over 3\n";
      CHECK(false);
    }
  }
}

void deep_compositor_keeps_precision_8_bits_cannot_hold() {
  // A 16-bit ramp under a 50% Normal layer: the deep flatten keeps every level the 8-bit
  // one would collapse.
  Document document(256, 1, PixelFormat::rgb8());
  document.add_pixel_layer("Base", PixelBuffer(256, 1, PixelFormat::rgba8()));
  convert_document_depth(document, BitDepth::UInt16);
  std::vector<float> row(256 * 4);
  for (int x = 0; x < 256; ++x) {
    row[static_cast<std::size_t>(x) * 4U + 0U] = 100.0F + x / 256.0F;  // 256 values inside one 8-bit step
    row[static_cast<std::size_t>(x) * 4U + 1U] = 0.0F;
    row[static_cast<std::size_t>(x) * 4U + 2U] = 0.0F;
    row[static_cast<std::size_t>(x) * 4U + 3U] = 255.0F;
  }
  store_rgba_row(document.layers()[0].pixels(), 0, 0, 256, DeepDomain::Encoded, row);
  const auto deep = Compositor{}.flatten_rgba_deep(document);
  CHECK(deep.format().bit_depth == BitDepth::UInt16);
  std::vector<std::uint16_t> seen;
  for (int x = 0; x < 256; ++x) {
    std::uint16_t value = 0;
    std::memcpy(&value, deep.pixel(x, 0), sizeof(value));
    seen.push_back(value);
  }
  std::sort(seen.begin(), seen.end());
  seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
  CHECK(seen.size() > 200U);
}

// ---- Photoshop corpus -------------------------------------------------------------

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t be32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
         (static_cast<std::uint32_t>(p[2]) << 8U) | p[3];
}

// Photoshop's 16-bit RGB PNG (non-interlaced, any filter) as straight 16-bit RGB.
std::optional<std::vector<std::uint16_t>> read_png16_rgb(const std::filesystem::path& path, int& width,
                                                         int& height) {
  const auto data = file_bytes(path);
  if (data.size() < 33U || data[12] != 'I' || data[24] != 16) {
    return std::nullopt;
  }
  width = static_cast<int>(be32(&data[16]));
  height = static_cast<int>(be32(&data[20]));
  const int color_type = data[25];
  const int channels = color_type == 2 ? 3 : color_type == 6 ? 4 : 0;
  if (channels == 0 || data[28] != 0) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> compressed;
  for (std::size_t offset = 8; offset + 8 <= data.size();) {
    const auto length = be32(&data[offset]);
    const std::string kind(reinterpret_cast<const char*>(&data[offset + 4]), 4);
    if (kind == "IDAT") {
      compressed.insert(compressed.end(), data.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                        data.begin() + static_cast<std::ptrdiff_t>(offset + 8 + length));
    }
    offset += 12U + length;
  }
  const auto bpp = static_cast<std::size_t>(channels) * 2U;
  const auto stride = static_cast<std::size_t>(width) * bpp;
  std::vector<std::uint8_t> raw((stride + 1U) * static_cast<std::size_t>(height));
  mz_ulong raw_length = static_cast<mz_ulong>(raw.size());
  if (mz_uncompress(raw.data(), &raw_length, compressed.data(), static_cast<mz_ulong>(compressed.size())) != MZ_OK) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> previous(stride, 0);
  std::vector<std::uint8_t> current(stride, 0);
  std::vector<std::uint16_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
  for (int y = 0; y < height; ++y) {
    const auto* line = raw.data() + static_cast<std::size_t>(y) * (stride + 1U);
    const int filter = line[0];
    for (std::size_t i = 0; i < stride; ++i) {
      const int left = i >= bpp ? current[i - bpp] : 0;
      const int up = previous[i];
      const int upper_left = i >= bpp ? previous[i - bpp] : 0;
      int predictor = 0;
      if (filter == 1) {
        predictor = left;
      } else if (filter == 2) {
        predictor = up;
      } else if (filter == 3) {
        predictor = (left + up) / 2;
      } else if (filter == 4) {
        const int estimate = left + up - upper_left;
        const int pa = std::abs(estimate - left);
        const int pb = std::abs(estimate - up);
        const int pc = std::abs(estimate - upper_left);
        predictor = pa <= pb && pa <= pc ? left : pb <= pc ? up : upper_left;
      }
      current[i] = static_cast<std::uint8_t>(line[1 + i] + predictor);
    }
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c) {
        const auto* sample = current.data() + static_cast<std::size_t>(x) * bpp + static_cast<std::size_t>(c) * 2U;
        rgb[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3U +
            static_cast<std::size_t>(c)] = static_cast<std::uint16_t>((sample[0] << 8U) | sample[1]);
      }
    }
    std::swap(previous, current);
  }
  return rgb;
}

// Photoshop's uncompressed little-endian 32-bit float RGB TIFF (make_deep_fixtures.jsx).
std::optional<std::vector<float>> read_float_tiff_rgb(const std::filesystem::path& path, int& width, int& height) {
  const auto data = file_bytes(path);
  if (data.size() < 8U || data[0] != 'I') {
    return std::nullopt;
  }
  const auto le32 = [&](std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[at + 2]) << 16U) | (static_cast<std::uint32_t>(data[at + 3]) << 24U);
  };
  const auto le16 = [&](std::size_t at) { return static_cast<std::uint32_t>(data[at] | (data[at + 1] << 8U)); };
  const auto ifd = le32(4);
  const auto count = le16(ifd);
  std::uint32_t strip_offsets = 0;
  std::uint32_t strip_count = 0;
  std::uint32_t samples = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto entry = ifd + 2U + 12U * i;
    const auto tag = le16(entry);
    const auto type = le16(entry + 2);
    const auto n = le32(entry + 4);
    const auto value = type == 3 && n == 1 ? le16(entry + 8) : le32(entry + 8);
    if (tag == 256) {
      width = static_cast<int>(value);
    } else if (tag == 257) {
      height = static_cast<int>(value);
    } else if (tag == 277) {
      samples = value;
    } else if (tag == 273) {
      strip_offsets = value;
      strip_count = n;
    }
  }
  if (samples != 3U || strip_count == 0U) {
    return std::nullopt;
  }
  std::vector<float> rgb;
  rgb.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
  for (std::uint32_t strip = 0; strip < strip_count; ++strip) {
    const auto offset = strip_count == 1U ? strip_offsets : le32(strip_offsets + 4U * strip);
    const auto rows_left = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U - rgb.size();
    const auto floats = std::min(rows_left, (data.size() - offset) / 4U);
    for (std::size_t i = 0; i < floats && rgb.size() < static_cast<std::size_t>(width) * height * 3U; ++i) {
      float value = 0.0F;
      std::memcpy(&value, data.data() + offset + i * 4U, sizeof(value));
      rgb.push_back(value);
    }
  }
  return rgb;
}

void deep_hue_saturation_matches_photoshop_16_bit_samples() {
  // Photoshop 2026's destructive Hue/Saturation on a 16-bit ramp, one slider at a time:
  // {input, output} in 16-bit levels. Its deep rules differ from the 8-bit ones (exact
  // lightness percent, unrounded hue rotation, its own saturation multipliers).
  struct Sample {
    int hue;
    int saturation;
    int lightness;
    std::array<int, 3> input;
    std::array<int, 3> expected;
  };
  const std::vector<Sample> samples{
      {0, 20, 0, {45299, 16378, 16378}, {48801, 12876, 12876}},
      {0, 20, 0, {34263, 27416, 27416}, {35091, 26586, 26586}},
      {0, -10, 0, {45299, 16378, 16378}, {43887, 17790, 17790}},
      {0, 75, 0, {45299, 16378, 16378}, {61677, 0, 0}},
      {0, 75, 0, {34263, 27416, 27416}, {44267, 17410, 17410}},
      {0, 0, -10, {41091, 20588, 20588}, {36983, 18530, 18530}},
      {0, 0, 25, {34263, 27416, 27416}, {42081, 36945, 36945}},
      {7, 0, 0, {45299, 16378, 16378}, {45299, 19746, 16378}},
      {-45, 0, 0, {41091, 20588, 20588}, {41091, 20588, 35965}},
  };
  for (const auto& sample : samples) {
    HueSaturationAdjustment settings;
    settings.hue_shift = sample.hue;
    settings.saturation_delta = sample.saturation;
    settings.lightness_delta = sample.lightness;
    const auto mapped = hue_saturation_transfer({sample.input[0] / 257.0, sample.input[1] / 257.0,
                                                 sample.input[2] / 257.0},
                                                settings, false);
    CHECK(mapped.has_value());
    for (std::size_t c = 0; c < 3U; ++c) {
      CHECK(std::abs((*mapped)[c] * 257.0 - sample.expected[c]) <= 64.0);
    }
  }
}

void deep_compositor_matches_photoshop_corpus_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "deep";
  if (!std::filesystem::exists(root / "manifest.json")) {
    std::cout << "[SKIP] deep fixture corpus missing: " << root.string() << '\n';
    return;
  }
  // Scenes whose Photoshop math Patchy models only within its 8-bit calibration (the
  // gradient fill's interpolation, Exposure's 16-bit pipeline, the layer effects'
  // 8-bit parameters): held to the 8-bit tolerance instead of 16-bit precision.
  // Everything else must match Photoshop within a quarter 8-bit step.
  const std::vector<std::string> calibrated_8_bit{"adj-exposure", "effects", "fill-gradient"};
  int checked = 0;
  for (const auto& scene_dir : std::filesystem::directory_iterator(root)) {
    if (!scene_dir.is_directory()) {
      continue;
    }
    const auto scene = scene_dir.path().filename().string();
    for (const auto* depth_name : {"16", "32"}) {
      const auto psd = scene_dir.path() / depth_name / (scene + "-" + depth_name + ".psd");
      if (!std::filesystem::exists(psd)) {
        continue;
      }
      psd::ReadOptions options;
      options.keep_bit_depth = true;
      const auto document = psd::DocumentIo::read_file(psd, options);
      const auto flat = Compositor{}.flatten_rgba_deep(document);
      int width = 0;
      int height = 0;
      int over = 0;
      double worst = 0.0;
      const bool sixteen = std::string(depth_name) == "16";
      if (sixteen) {
        const auto truth = read_png16_rgb(scene_dir.path() / depth_name / "render16.png", width, height);
        CHECK(truth.has_value() && width == flat.width() && height == flat.height());
        std::vector<float> row(static_cast<std::size_t>(width) * 4U);
        const bool strict = std::find(calibrated_8_bit.begin(), calibrated_8_bit.end(), scene) == calibrated_8_bit.end();
        const double tolerance = strict ? 64.0 : 6.0 * 257.0;
        for (int y = 0; y < height; ++y) {
          load_rgba_row(flat, y, 0, width, DeepDomain::Encoded, row);
          for (int x = 0; x < width; ++x) {
            double pixel_worst = 0.0;
            for (int c = 0; c < 3; ++c) {
              const auto mine = static_cast<double>(row[static_cast<std::size_t>(x) * 4U + c]) * 257.0;
              const auto theirs =
                  (*truth)[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 3U + c];
              pixel_worst = std::max(pixel_worst, std::abs(mine - theirs));
            }
            worst = std::max(worst, pixel_worst);
            over += pixel_worst > tolerance ? 1 : 0;
          }
        }
      } else {
        const bool strict = std::find(calibrated_8_bit.begin(), calibrated_8_bit.end(), scene) == calibrated_8_bit.end();
        const double tolerance = strict ? 2.0 : 6.0;
        const auto truth = read_float_tiff_rgb(scene_dir.path() / depth_name / "render32.tif", width, height);
        CHECK(truth.has_value() && width == flat.width() && height == flat.height());
        std::vector<float> row(static_cast<std::size_t>(width) * 4U);
        for (int y = 0; y < height; ++y) {
          load_rgba_row(flat, y, 0, width, DeepDomain::Linear, row);
          for (int x = 0; x < width; ++x) {
            double pixel_worst = 0.0;
            for (int c = 0; c < 3; ++c) {
              // Compared as display values, where one 8-bit step means the same everywhere.
              const auto mine = srgb_encode(row[static_cast<std::size_t>(x) * 4U + c] / 255.0) * 255.0;
              const auto theirs =
                  srgb_encode((*truth)[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 3U + c]) *
                  255.0;
              pixel_worst = std::max(pixel_worst, std::abs(mine - theirs));
            }
            worst = std::max(worst, pixel_worst);
            over += pixel_worst > tolerance ? 1 : 0;
          }
        }
      }
      const auto fraction = static_cast<double>(over) / (static_cast<double>(width) * height);
      // The corpus's documented gaps: the 32-bit gradient fill and effects sit within
      // 1% of Photoshop at 2/255. The 16-bit effects scene misses where the 8-bit
      // document's render misses Photoshop too (98.2% byte-exact at both depths: the
      // same 148 pixels: the outside stroke's corners and the shadow beside it), a
      // layer-effects calibration gap rather than a deep one.
      const double allowed = scene == "effects" && sixteen ? 0.025 : 0.01;
      if (fraction > allowed) {
        std::cerr << scene << " " << depth_name << "-bit: " << fraction * 100.0 << "% of pixels off (worst "
                  << worst << ")\n";
        CHECK(false);
      }
      ++checked;
    }
  }
  std::cout << "  deep corpus documents compared with Photoshop: " << checked << '\n';
  CHECK(checked > 0);
}

}  // namespace

std::vector<patchy::test::TestCase> deep_compositor_tests() {
  return {
      {"deep_compositor_agrees_with_8_bit_on_every_blend_mode", deep_compositor_agrees_with_8_bit_on_every_blend_mode},
      {"deep_compositor_agrees_with_8_bit_on_masks_groups_clips_and_adjustments",
       deep_compositor_agrees_with_8_bit_on_masks_groups_clips_and_adjustments},
      {"deep_compositor_keeps_precision_8_bits_cannot_hold", deep_compositor_keeps_precision_8_bits_cannot_hold},
      {"deep_hue_saturation_matches_photoshop_16_bit_samples", deep_hue_saturation_matches_photoshop_16_bit_samples},
      {"deep_compositor_matches_photoshop_corpus_if_available", deep_compositor_matches_photoshop_corpus_if_available},
  };
}
