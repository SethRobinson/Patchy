// Automatic selection (docs/ai-selection.md): the pre/post-processing math everywhere, and
// real MobileSAM / EfficientViT-SAM / BiRefNet runs when the build includes ONNX Runtime.
// Model outputs are CPU-dependent float math, so model checks compare masks by IoU, never
// by bytes.

#include "ai/ai_selection_math.hpp"
#include "ai/segmentation_engine.hpp"
#include "core_test_support.hpp"
#include "formats/stb/stb_image.h"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;
using patchy::ai::AiMask;
using patchy::ai::LogitMapping;
using patchy::test::solid_rgba;

std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}


// A self-authored scene: a mottled green-grey background (deterministic noise) with a
// shaded red disc, the one clear object. `disc` receives the disc's exact mask.
PixelBuffer disc_scene(std::int32_t width, std::int32_t height, AiMask& disc) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  const double cx = width * 0.45;
  const double cy = height * 0.55;
  const double radius = std::min(width, height) * 0.22;
  disc.bounds = Rect{static_cast<std::int32_t>(cx - radius) - 1, static_cast<std::int32_t>(cy - radius) - 1,
                     static_cast<std::int32_t>(radius * 2) + 3, static_cast<std::int32_t>(radius * 2) + 3};
  disc.alpha = PixelBuffer(disc.bounds.width, disc.bounds.height, PixelFormat::gray8());
  std::uint64_t state = 7;
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = row.data() + static_cast<std::size_t>(x) * 4U;
      const auto noise = static_cast<int>(splitmix64(state) % 25U);
      const auto blotch = static_cast<int>(18.0 * std::sin(x * 0.05) * std::cos(y * 0.07));
      const double dx = x + 0.5 - cx;
      const double dy = y + 0.5 - cy;
      const bool inside = dx * dx + dy * dy <= radius * radius;
      if (inside) {
        const double shade = 0.65 + 0.35 * std::max(0.0, -(dx + dy) / (radius * 1.5));
        px[0] = static_cast<std::uint8_t>(std::clamp(200.0 * shade + noise * 0.3, 0.0, 255.0));
        px[1] = static_cast<std::uint8_t>(std::clamp(40.0 * shade, 0.0, 255.0));
        px[2] = static_cast<std::uint8_t>(std::clamp(35.0 * shade, 0.0, 255.0));
      } else {
        px[0] = static_cast<std::uint8_t>(std::clamp(95 + noise + blotch, 0, 255));
        px[1] = static_cast<std::uint8_t>(std::clamp(120 + noise + blotch, 0, 255));
        px[2] = static_cast<std::uint8_t>(std::clamp(90 + noise, 0, 255));
      }
      px[3] = 255;
      if (disc.bounds.contains(x, y)) {
        disc.alpha.pixel(x - disc.bounds.x, y - disc.bounds.y)[0] = inside ? 255U : 0U;
      }
    }
  }
  return pixels;
}

// The build's staged models (patchy_copy_ai_runtime puts them beside the test binary,
// which runs from the build directory). A stub build has no models to point at, and
// under Node (the wasm suite) current_path() throws because getcwd fails.
bool point_engine_at_build_models() {
#if defined(PATCHY_HAVE_AI_SELECTION)
  patchy::ai::set_model_directory(std::filesystem::current_path() / "ai-models");
  // A build with ONNX Runtime stages the models; their absence is a real failure.
  if (!patchy::ai::segmentation_available()) {
    std::printf("automatic selection unavailable: %s\n", patchy::ai::segmentation_unavailable_reason().c_str());
  }
  CHECK(patchy::ai::segmentation_available());
  return true;
#else
  CHECK(!patchy::ai::segmentation_available());
  return false;
#endif
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ai_sam_transform_scales_longest_side() {
  const auto wide = patchy::ai::sam_transform(4000, 3000);
  CHECK(wide.scaled_width == 1024);
  CHECK(wide.scaled_height == 768);
  CHECK(std::abs(wide.scale - 0.256) < 1e-9);
  const auto tall = patchy::ai::sam_transform(300, 600);
  CHECK(tall.scaled_width == 512);
  CHECK(tall.scaled_height == 1024);
  const auto point = patchy::ai::to_sam_point(wide, 1000.0, 500.0);
  CHECK(std::abs(point.x - 256.0F) < 1e-3F);
  CHECK(std::abs(point.y - 128.0F) < 1e-3F);
}

void ai_sam_region_transform_offsets_points() {
  const auto region = patchy::ai::sam_region_transform(Rect{1000, 500, 2000, 1000});
  CHECK(region.source_width == 2000 && region.source_height == 1000);
  CHECK(region.scaled_width == 1024 && region.scaled_height == 512);
  CHECK(region.origin_x == 1000 && region.origin_y == 500);
  const auto point = patchy::ai::to_sam_point(region, 1500.0, 700.0);
  CHECK(std::abs(point.x - 256.0F) < 1e-3F);
  CHECK(std::abs(point.y - 102.4F) < 1e-3F);
}

// Zoom-to-box is fixed geometry from the user's box alone (patent boundary in
// segmentation_engine.hpp): margin, minimum size, clamping, and the whole-image fallback.
void ai_sam_box_region_is_fixed_geometry() {
  // A 1000x800 box grows by 15% of its longer side on every side.
  const auto grown = patchy::ai::sam_box_region(Rect{2000, 1000, 1000, 800}, 6000, 4000);
  CHECK(grown.has_value());
  CHECK(grown->x == 1850 && grown->y == 850 && grown->width == 1300 && grown->height == 1100);
  // A tiny box gets at least the minimum margin, widens to the minimum region, and clamps.
  const auto tiny = patchy::ai::sam_box_region(Rect{100, 2000, 20, 20}, 6000, 4000);
  CHECK(tiny.has_value());
  CHECK(tiny->x == 0 && tiny->width == 302);
  CHECK(tiny->height == patchy::ai::kSamBoxMinimumRegion);
  CHECK(tiny->y <= 2000 - patchy::ai::kSamBoxMinimumMargin && tiny->y + tiny->height >= 2020 + 64);
  // A box covering most of the image keeps the whole-image embedding.
  CHECK(!patchy::ai::sam_box_region(Rect{50, 50, 900, 700}, 1000, 800).has_value());
  CHECK(!patchy::ai::sam_box_region(Rect{10, 10, 0, 5}, 1000, 800).has_value());
  CHECK(!patchy::ai::sam_box_region(Rect{2000, 2000, 50, 50}, 1000, 800).has_value());
}

void ai_crop_rgba_copies_the_region() {
  PixelBuffer pixels(8, 6, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 6; ++y) {
    for (std::int32_t x = 0; x < 8; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x);
      px[1] = static_cast<std::uint8_t>(y);
      px[2] = 0;
      px[3] = 255;
    }
  }
  const auto crop = patchy::ai::crop_rgba(pixels, Rect{5, 2, 10, 3});  // clamped to 3x3
  CHECK(crop.width() == 3 && crop.height() == 3);
  CHECK(crop.pixel(0, 0)[0] == 5 && crop.pixel(0, 0)[1] == 2);
  CHECK(crop.pixel(2, 2)[0] == 7 && crop.pixel(2, 2)[1] == 4);
}

void ai_padded_encoder_input_is_normalized_and_padded() {
  const auto white = solid_rgba(200, 100, 255, 255, 255, 255);
  const auto transform = patchy::ai::sam_transform(200, 100);
  const auto input = patchy::ai::padded_sam_encoder_input(white, transform);
  const std::size_t plane = 1024U * 1024U;
  CHECK(input.size() == plane * 3U);
  CHECK(std::abs(input[0] - (1.0F - 0.485F) / 0.229F) < 1e-4F);
  CHECK(std::abs(input[2 * plane + 511U * 1024U + 1023U] - (1.0F - 0.406F) / 0.225F) < 1e-4F);
  // Below the scaled image (512 rows) is padding: zero, the normalized mean color.
  CHECK(input[plane + 600U * 1024U + 10U] == 0.0F);
}

void ai_low_res_logits_map_through_the_padded_frame() {
  // A 2000x1000 region at (100, 50): the scaled frame is 1024x512, so the decoder's 256-cell
  // grid holds the image in its top 128 rows. Cells 64..127 x 32..63 are frame pixels
  // 256..511 x 128..255, region pixels 500..999 x 250..499.
  const auto transform = patchy::ai::sam_region_transform(Rect{100, 50, 2000, 1000});
  std::vector<float> logits(256U * 256U, -20.0F);
  for (std::size_t y = 32; y < 64; ++y) {
    for (std::size_t x = 64; x < 128; ++x) {
      logits[y * 256U + x] = 20.0F;
    }
  }
  // Hot cells in the padding below the image must never select anything.
  for (std::size_t y = 200; y < 256; ++y) {
    for (std::size_t x = 0; x < 256; ++x) {
      logits[y * 256U + x] = 20.0F;
    }
  }
  const auto local = patchy::ai::document_mask_from_low_res_logits(logits.data(), transform);
  CHECK(!local.empty());
  CHECK(std::abs(local.bounds.x - 500) <= 8 && std::abs(local.bounds.y - 250) <= 8);
  CHECK(std::abs(local.bounds.x + local.bounds.width - 1000) <= 8);
  CHECK(std::abs(local.bounds.y + local.bounds.height - 500) <= 8);
  const auto placed = patchy::ai::translated_mask(local, transform.origin_x, transform.origin_y);
  CHECK(placed.bounds.x == local.bounds.x + 100 && placed.bounds.y == local.bounds.y + 50);
  CHECK(placed.alpha.pixel(750 - local.bounds.x, 375 - local.bounds.y)[0] == 255U);
}

void ai_encoder_input_composites_over_white_and_resizes() {
  const auto transparent = solid_rgba(200, 100, 10, 20, 30, 0);
  const auto transform = patchy::ai::sam_transform(200, 100);
  const auto input = patchy::ai::sam_encoder_input(transparent, transform);
  CHECK(input.size() == static_cast<std::size_t>(1024 * 512 * 3));
  CHECK(std::all_of(input.begin(), input.end(), [](float v) { return std::abs(v - 255.0F) < 0.01F; }));

  const auto opaque = solid_rgba(2048, 1024, 10, 20, 30, 255);
  const auto reduced = patchy::ai::sam_encoder_input(opaque, patchy::ai::sam_transform(2048, 1024));
  CHECK(reduced.size() == static_cast<std::size_t>(1024 * 512 * 3));
  CHECK(std::abs(reduced[0] - 10.0F) < 0.01F && std::abs(reduced[1] - 20.0F) < 0.01F &&
        std::abs(reduced[2] - 30.0F) < 0.01F);
}

void ai_subject_input_is_imagenet_normalized_nchw() {
  const auto white = solid_rgba(64, 32, 255, 255, 255, 255);
  const auto input = patchy::ai::subject_model_input(white);
  const std::size_t plane = 1024U * 1024U;
  CHECK(input.size() == plane * 3U);
  CHECK(std::abs(input[0] - (1.0F - 0.485F) / 0.229F) < 1e-4F);
  CHECK(std::abs(input[plane + 5] - (1.0F - 0.456F) / 0.224F) < 1e-4F);
  CHECK(std::abs(input[2 * plane + plane - 1] - (1.0F - 0.406F) / 0.225F) < 1e-4F);
}

void ai_logit_coverage_mappings() {
  CHECK(patchy::ai::logit_to_coverage(0.0F, LogitMapping::SamEdgeRamp) == 128U);
  CHECK(patchy::ai::logit_to_coverage(5.0F, LogitMapping::SamEdgeRamp) == 255U);
  CHECK(patchy::ai::logit_to_coverage(-5.0F, LogitMapping::SamEdgeRamp) == 0U);
  CHECK(patchy::ai::logit_to_coverage(0.0F, LogitMapping::Sigmoid) == 128U);
  CHECK(patchy::ai::logit_to_coverage(10.0F, LogitMapping::Sigmoid) == 255U);
  CHECK(patchy::ai::logit_to_coverage(-10.0F, LogitMapping::Sigmoid) == 0U);
  const auto soft = patchy::ai::logit_to_coverage(1.0F, LogitMapping::Sigmoid);
  CHECK(soft > 180U && soft < 200U);
}

void ai_document_mask_from_logits_upsamples_and_crops() {
  // A 4x4 grid with one hot 2x2 block in the middle maps onto a 400x400 document.
  std::vector<float> logits(16, -20.0F);
  logits[5] = logits[6] = logits[9] = logits[10] = 20.0F;
  const auto mask =
      patchy::ai::document_mask_from_logits(logits.data(), 4, 4, 400, 400, LogitMapping::SamEdgeRamp);
  CHECK(!mask.empty());
  CHECK(mask.alpha.width() == mask.bounds.width && mask.alpha.height() == mask.bounds.height);
  // The hot block covers document 100..300; the ramp keeps the edge within a few pixels.
  CHECK(std::abs(mask.bounds.x - 100) <= 25 && std::abs(mask.bounds.y - 100) <= 25);
  CHECK(std::abs(mask.bounds.x + mask.bounds.width - 300) <= 25);
  CHECK(mask.alpha.pixel(200 - mask.bounds.x, 200 - mask.bounds.y)[0] == 255U);

  std::vector<float> cold(16, -20.0F);
  CHECK(patchy::ai::document_mask_from_logits(cold.data(), 4, 4, 400, 400, LogitMapping::Sigmoid).empty());
}

void ai_clip_mask_to_rect_crops_and_clears() {
  AiMask mask;
  mask.bounds = Rect{10, 10, 20, 20};
  mask.alpha = PixelBuffer(20, 20, PixelFormat::gray8());
  mask.alpha.clear(200);
  const auto clipped = patchy::ai::clip_mask_to_rect(mask, Rect{0, 15, 25, 100});
  CHECK(clipped.bounds.x == 10 && clipped.bounds.y == 15 && clipped.bounds.width == 15 && clipped.bounds.height == 15);
  CHECK(clipped.alpha.width() == 15 && clipped.alpha.pixel(14, 14)[0] == 200U);
  CHECK(patchy::ai::clip_mask_to_rect(mask, Rect{100, 100, 5, 5}).empty());
}

void ai_mask_iou_counts_threshold_overlap() {
  AiMask a;
  a.bounds = Rect{0, 0, 10, 10};
  a.alpha = PixelBuffer(10, 10, PixelFormat::gray8());
  a.alpha.clear(255);
  AiMask b;
  b.bounds = Rect{5, 0, 10, 10};
  b.alpha = PixelBuffer(10, 10, PixelFormat::gray8());
  b.alpha.clear(200);
  CHECK(std::abs(patchy::ai::mask_iou(a, b) - 50.0 / 150.0) < 1e-9);
  CHECK(patchy::ai::mask_iou(a, a) == 1.0);
  CHECK(patchy::ai::mask_iou(AiMask{}, AiMask{}) == 1.0);
  CHECK(patchy::ai::mask_iou(a, AiMask{}) == 0.0);
}

void ai_object_selection_box_and_point_find_the_disc() {
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  AiMask disc;
  const auto scene = disc_scene(1200, 900, disc);
  auto start = std::chrono::steady_clock::now();
  const auto embedding = patchy::ai::sam_encode(scene);
  std::printf("  MobileSAM encode %.0f ms\n", milliseconds_since(start));
  CHECK(embedding != nullptr && embedding->values.size() == patchy::ai::kSamEmbeddingValues);

  const Rect box{disc.bounds.x - 20, disc.bounds.y - 20, disc.bounds.width + 40, disc.bounds.height + 40};
  start = std::chrono::steady_clock::now();
  const auto boxed = patchy::ai::sam_select_box(*embedding, box);
  std::printf("  MobileSAM box decode %.0f ms\n", milliseconds_since(start));
  const auto box_iou = patchy::ai::mask_iou(boxed, disc);
  std::printf("  box IoU %.3f\n", box_iou);
  CHECK(box_iou >= 0.9);

  start = std::chrono::steady_clock::now();
  const auto pointed = patchy::ai::sam_select_point(*embedding, disc.bounds.x + disc.bounds.width * 0.5,
                                                    disc.bounds.y + disc.bounds.height * 0.5);
  std::printf("  MobileSAM point decode %.0f ms\n", milliseconds_since(start));
  const auto point_iou = patchy::ai::mask_iou(pointed, disc);
  std::printf("  point IoU %.3f\n", point_iou);
  CHECK(point_iou >= 0.9);
}

// The High Quality model (EfficientViT-SAM XL0) from the build's stand-in for the download
// folder (<build>/ai-models-download, staged when setup-ai-deps provisioned it). Restores
// the Standard-only state afterwards so the other tests keep running MobileSAM.
struct HighQualityModelScope {
  bool available{false};
  HighQualityModelScope() {
    const auto folder = std::filesystem::current_path() / "ai-models-download";
    patchy::ai::set_downloaded_model_directory(folder);
    patchy::ai::set_high_quality_model_enabled(true);
    available = patchy::ai::high_quality_model_available();
  }
  ~HighQualityModelScope() {
    patchy::ai::set_downloaded_model_directory({});
    patchy::ai::set_high_quality_model_enabled(true);
  }
  HighQualityModelScope(const HighQualityModelScope&) = delete;
  HighQualityModelScope& operator=(const HighQualityModelScope&) = delete;
};

// Zoom-to-box: the box's own region, encoded at full resolution, finds the same object.
void check_zoomed_box_finds_the_disc(const char* label) {
  AiMask disc;
  const auto scene = disc_scene(2400, 1800, disc);
  const Rect box{disc.bounds.x - 30, disc.bounds.y - 30, disc.bounds.width + 60, disc.bounds.height + 60};
  const auto region = patchy::ai::sam_box_region(box, scene.width(), scene.height());
  CHECK(region.has_value());
  if (!region.has_value()) {
    return;
  }
  const auto embedding = patchy::ai::sam_encode_region(scene, *region);
  CHECK(embedding->transform.origin_x == region->x && embedding->transform.origin_y == region->y);
  const auto iou = patchy::ai::mask_iou(patchy::ai::sam_select_box(*embedding, box), disc);
  std::printf("  %s zoomed box IoU %.3f\n", label, iou);
  CHECK(iou >= 0.9);
}

void ai_object_selection_zoomed_box_finds_the_disc() {
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  CHECK(patchy::ai::active_sam_model() == patchy::ai::SamModel::Standard);
  check_zoomed_box_finds_the_disc("MobileSAM");
}

void ai_high_quality_object_selection_finds_the_disc() {
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  const HighQualityModelScope scope;
  if (!scope.available) {
    patchy::test::skip("High Quality model not staged");
  }
  CHECK(patchy::ai::active_sam_model() == patchy::ai::SamModel::HighQuality);
  AiMask disc;
  const auto scene = disc_scene(1200, 900, disc);
  const auto start = std::chrono::steady_clock::now();
  const auto embedding = patchy::ai::sam_encode(scene);
  std::printf("  EfficientViT-SAM XL0 encode %.0f ms (load included)\n", milliseconds_since(start));
  CHECK(embedding->model == patchy::ai::SamModel::HighQuality);
  const Rect box{disc.bounds.x - 20, disc.bounds.y - 20, disc.bounds.width + 40, disc.bounds.height + 40};
  const auto box_iou = patchy::ai::mask_iou(patchy::ai::sam_select_box(*embedding, box), disc);
  const auto point_iou = patchy::ai::mask_iou(
      patchy::ai::sam_select_point(*embedding, disc.bounds.x + disc.bounds.width * 0.5,
                                   disc.bounds.y + disc.bounds.height * 0.5),
      disc);
  std::printf("  box IoU %.3f, point IoU %.3f\n", box_iou, point_iou);
  CHECK(box_iou >= 0.9);
  CHECK(point_iou >= 0.9);
  check_zoomed_box_finds_the_disc("EfficientViT-SAM XL0");

  // The Model setting switches back to Standard; the High Quality embedding still decodes
  // with its own decoder.
  patchy::ai::set_high_quality_model_enabled(false);
  CHECK(patchy::ai::active_sam_model() == patchy::ai::SamModel::Standard);
  CHECK(patchy::ai::sam_encode(scene)->model == patchy::ai::SamModel::Standard);
  CHECK(patchy::ai::mask_iou(patchy::ai::sam_select_box(*embedding, box), disc) >= 0.9);
}

// A loose box around a boy in front of a fridge (local photo, never committed): MobileSAM's
// single-mask output took the fridge too, so boxes pick the decoder output with the highest
// predicted IoU. The box and check points are in the photo's 2816x1880 pixels.
void ai_object_selection_loose_box_skips_background_if_available() {
  const auto path = patchy::test::local_format_fixture_path("photos", "P1010437.JPG");
  std::error_code missing;
  if (!std::filesystem::is_regular_file(path, missing)) {
    patchy::test::skip("optional fixture missing: " + patchy::test::fixture_display_path(path));
  }
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  std::ifstream file(path, std::ios::binary);
  const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* decoded =
      stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
  CHECK(decoded != nullptr && width == 2816 && height == 1880);
  PixelBuffer photo(width, height, PixelFormat::rgba8());
  std::memcpy(photo.data().data(), decoded, photo.byte_size());
  stbi_image_free(decoded);

  const auto embedding = patchy::ai::sam_encode(photo);
  const auto mask = patchy::ai::sam_select_box(*embedding, Rect{760, 120, 1290, 1760});
  const auto covered = [&mask](std::int32_t x, std::int32_t y) {
    return mask.bounds.contains(x, y) && mask.alpha.pixel(x - mask.bounds.x, y - mask.bounds.y)[0] >= 128U;
  };
  CHECK(covered(1450, 1150));  // the boy's chest
  CHECK(covered(1400, 560));   // his face
  CHECK(!covered(1900, 800));  // the fridge door behind him
}

void ai_select_subject_finds_the_disc() {
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  if (!patchy::ai::subject_selection_available()) {
    // Held back pending patent review (PATCHY_ENABLE_SELECT_SUBJECT off): the call refuses.
    bool refused = false;
    try {
      (void)patchy::ai::subject_matte(solid_rgba(64, 64, 200, 40, 35, 255));
    } catch (const std::runtime_error& error) {
      refused = std::string(error.what()).find("Select Subject") != std::string::npos;
    }
    CHECK(refused);
    std::printf("  note: build without the Select Subject model, only the refusal was checked\n");
    return;
  }
  AiMask disc;
  const auto scene = disc_scene(900, 700, disc);
  const auto start = std::chrono::steady_clock::now();
  const auto matte = patchy::ai::subject_matte(scene);
  std::printf("  BiRefNet subject matte %.0f ms (load included)\n", milliseconds_since(start));
  const auto iou = patchy::ai::mask_iou(matte, disc);
  std::printf("  subject IoU %.3f\n", iou);
  CHECK(iou >= 0.85);
}

// ONNX Runtime reports a std::bad_alloc thrown inside a kernel only as status text; those
// failures must reach users as the out-of-memory message, not the raw diagnostic.
void ai_out_of_memory_failures_are_recognized() {
  // Captured from a real Select Subject run on Windows with 6 GB free.
  CHECK(patchy::ai::is_out_of_memory_message(
      "Non-zero status code returned while running Sum node. "
      "Name:'/decoder/decoder_block1/dec_att/aspp_deforms.2/atrous_conv/Sum' Status Message: bad allocation"));
  CHECK(patchy::ai::is_out_of_memory_message("Status Message: std::bad_alloc"));
  CHECK(patchy::ai::is_out_of_memory_message("Failed to allocate memory for requested buffer of size 1073741824"));
  CHECK(patchy::ai::is_out_of_memory_message("CPU allocator: Out of memory"));
  CHECK(!patchy::ai::is_out_of_memory_message(""));
  CHECK(!patchy::ai::is_out_of_memory_message("automatic selection was cancelled"));
  CHECK(!patchy::ai::is_out_of_memory_message("Got invalid dimensions for input: input_image"));
  CHECK(!patchy::ai::is_out_of_memory_message("Load model from x.onnx failed: Protobuf parsing failed."));

  CHECK(patchy::ai::is_out_of_memory(patchy::ai::InferenceOutOfMemory()));
  CHECK(patchy::ai::is_out_of_memory(std::bad_alloc()));
  CHECK(!patchy::ai::is_out_of_memory(patchy::ai::InferenceCancelled()));
  CHECK(!patchy::ai::is_out_of_memory(std::runtime_error("automatic selection failed: bad allocation")));
}

void ai_cancel_stops_inference_without_a_result() {
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  AiMask disc;
  const auto scene = disc_scene(640, 480, disc);
  patchy::ai::InferenceCancel early;
  early.request();
  bool cancelled = false;
  try {
    (void)patchy::ai::sam_encode(scene, &early);
  } catch (const patchy::ai::InferenceCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);

  // A request mid-run terminates the run well before it ends: the subject model's multi-second
  // run when the build includes it, otherwise the encoder (200 ms or more; a request that lands
  // before ONNX Runtime attaches still terminates at once).
  const bool subject = patchy::ai::subject_selection_available();
  patchy::ai::InferenceCancel midway;
  std::thread canceller([&midway, subject] {
    std::this_thread::sleep_for(std::chrono::milliseconds(subject ? 1500 : 50));
    midway.request();
  });
  cancelled = false;
  const auto start = std::chrono::steady_clock::now();
  try {
    if (subject) {
      (void)patchy::ai::subject_matte(scene, &midway);
    } else {
      (void)patchy::ai::sam_encode(scene, &midway);
    }
  } catch (const patchy::ai::InferenceCancelled&) {
    cancelled = true;
  }
  canceller.join();
  std::printf("  cancelled after %.0f ms\n", milliseconds_since(start));
  CHECK(cancelled);
}

void ai_models_load_from_unicode_directory() {
  if (!point_engine_at_build_models()) {
    patchy::test::skip("build without automatic selection");
  }
  const auto staged = patchy::ai::model_directory();
  const auto dir = patchy::test::unicode_artifact_dir(patchy::test::kUnicodeCombinedStem);
  for (const char* file : {"mobile_sam_image_encoder_int8.onnx", "sam_mask_decoder_multi_int8.onnx"}) {
    std::filesystem::copy_file(staged / file, dir / file);
  }
  patchy::ai::release_models();
  patchy::ai::set_model_directory(dir);
  AiMask disc;
  const auto scene = disc_scene(320, 240, disc);
  bool loaded = false;
  try {
    const auto embedding = patchy::ai::sam_encode(scene);
    loaded = !patchy::ai::sam_select_point(*embedding, 144.0, 132.0).empty();
  } catch (const std::exception& error) {
    std::printf("  unicode model load failed: %s\n", error.what());
  }
  patchy::ai::release_models();
  patchy::ai::set_model_directory(staged);
  patchy::test::remove_test_scratch_tree(dir);
  CHECK(loaded);
}

}  // namespace

std::vector<patchy::test::TestCase> ai_selection_tests() {
  return {
      {"ai_sam_transform_scales_longest_side", ai_sam_transform_scales_longest_side},
      {"ai_sam_region_transform_offsets_points", ai_sam_region_transform_offsets_points},
      {"ai_sam_box_region_is_fixed_geometry", ai_sam_box_region_is_fixed_geometry},
      {"ai_crop_rgba_copies_the_region", ai_crop_rgba_copies_the_region},
      {"ai_padded_encoder_input_is_normalized_and_padded", ai_padded_encoder_input_is_normalized_and_padded},
      {"ai_low_res_logits_map_through_the_padded_frame", ai_low_res_logits_map_through_the_padded_frame},
      {"ai_encoder_input_composites_over_white_and_resizes", ai_encoder_input_composites_over_white_and_resizes},
      {"ai_subject_input_is_imagenet_normalized_nchw", ai_subject_input_is_imagenet_normalized_nchw},
      {"ai_logit_coverage_mappings", ai_logit_coverage_mappings},
      {"ai_document_mask_from_logits_upsamples_and_crops", ai_document_mask_from_logits_upsamples_and_crops},
      {"ai_clip_mask_to_rect_crops_and_clears", ai_clip_mask_to_rect_crops_and_clears},
      {"ai_mask_iou_counts_threshold_overlap", ai_mask_iou_counts_threshold_overlap},
      {"ai_object_selection_box_and_point_find_the_disc", ai_object_selection_box_and_point_find_the_disc},
      {"ai_object_selection_zoomed_box_finds_the_disc", ai_object_selection_zoomed_box_finds_the_disc},
      {"ai_high_quality_object_selection_finds_the_disc", ai_high_quality_object_selection_finds_the_disc},
      {"ai_object_selection_loose_box_skips_background_if_available",
       ai_object_selection_loose_box_skips_background_if_available},
      {"ai_select_subject_finds_the_disc", ai_select_subject_finds_the_disc},
      {"ai_out_of_memory_failures_are_recognized", ai_out_of_memory_failures_are_recognized},
      {"ai_cancel_stops_inference_without_a_result", ai_cancel_stops_inference_without_a_result},
      {"ai_models_load_from_unicode_directory", ai_models_load_from_unicode_directory},
  };
}
