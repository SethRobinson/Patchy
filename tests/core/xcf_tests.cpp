// GIMP .xcf import (docs/xcf.md). Committed tests build their bytes in code
// (tests/xcf_test_builder.hpp) or read the small GIMP-made files in test-fixtures/xcf;
// GIMP's fixture set and the public corpus are optional local fixtures
// (local-test-fixtures/xcf, local-test-fixtures/xcf-corpus).

#include "color/color_management.hpp"
#include "color/rgb_profiles.hpp"
#include "core/blend_math.hpp"
#include "core/blend_math_deep.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/image_open_diagnosis.hpp"
#include "formats/layered_blend_support.hpp"
#include "formats/metadata/exif_tags.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"
#include "formats/ora_document_io.hpp"
#include "formats/stb/stb_image.h"
#include "formats/xcf_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include "xcf_test_builder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using patchy::BlendMode;
using patchy::Document;
using patchy::RgbColor;
namespace builder = patchy::test::xcf;

// --- helpers --------------------------------------------------------------------------------

[[nodiscard]] const patchy::Layer* find_named(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* child = find_named(layer.children(), name)) {
      return child;
    }
  }
  return nullptr;
}

[[nodiscard]] std::array<std::uint8_t, 4> rgba_at(const patchy::Layer& layer, std::int32_t x, std::int32_t y) {
  return patchy::display_rgba8_at(std::as_const(layer).pixels(), x, y);
}

[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// The document's composite as 8-bit straight RGBA, whatever its depth, after the sRGB
// conversion the open path makes for an embedded profile (docs/rgb-profiles.md).
[[nodiscard]] patchy::PixelBuffer composite_rgba8(const Document& source) {
  Document document = source;
  const auto profile = std::as_const(document).color_state().embedded_icc_profile;
  if (!profile.empty() && patchy::rgb_profile_needs_conversion(profile)) {
    (void)patchy::convert_document_rgb_profile(document, {}, patchy::RenderingIntent::RelativeColorimetric, true);
  }
  if (document.color_state().bit_depth == patchy::BitDepth::UInt8) {
    return patchy::flatten_document_rgba8(document);
  }
  const auto deep = patchy::Compositor().flatten_rgba_deep(document);
  patchy::PixelBuffer out(deep.width(), deep.height(), patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < deep.height(); ++y) {
    for (std::int32_t x = 0; x < deep.width(); ++x) {
      const auto px = patchy::display_rgba8_at(deep, x, y);
      std::memcpy(out.pixel(x, y), px.data(), 4);
    }
  }
  return out;
}

// Writes `pixels` (8-bit RGBA) as test-artifacts/<folder>/<name>.png for a look beside
// GIMP's render.
void write_png_artifact(const std::string& folder, const std::string& name, const patchy::PixelBuffer& pixels) {
  const auto dir = std::filesystem::path("test-artifacts") / folder;
  std::filesystem::create_directories(dir);
  const auto png = patchy::ora::detail::encode_png(pixels);
  std::ofstream file(dir / (name + ".png"), std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

struct Difference {
  double mean{0.0};  // per channel, 8-bit units, on premultiplied RGBA
  int worst{0};
  double bad_fraction{0.0};  // pixels with any channel off by more than 8
};

// Compares a composite with a PNG (8 or 16-bit, any channel count) on premultiplied RGBA.
[[nodiscard]] std::optional<Difference> compare_with_png(const patchy::PixelBuffer& composite,
                                                         const std::filesystem::path& png) {
  const auto bytes = read_bytes(png);
  int width = 0;
  int height = 0;
  int components = 0;
  auto* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &components, 4);
  if (decoded == nullptr) {
    return std::nullopt;
  }
  if (width != composite.width() || height != composite.height()) {
    stbi_image_free(decoded);
    return std::nullopt;
  }
  Difference difference;
  double total = 0.0;
  std::size_t bad = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto* a = decoded + (static_cast<std::size_t>(y) * width + x) * 4;
      const auto* b = composite.pixel(x, y);
      int pixel_worst = 0;
      for (int c = 0; c < 4; ++c) {
        const double pa = c == 3 ? a[3] : a[c] * a[3] / 255.0;
        const double pb = c == 3 ? b[3] : b[c] * b[3] / 255.0;
        const int delta = static_cast<int>(std::lround(std::abs(pa - pb)));
        total += std::abs(pa - pb);
        pixel_worst = std::max(pixel_worst, delta);
      }
      difference.worst = std::max(difference.worst, pixel_worst);
      bad += pixel_worst > 8 ? 1U : 0U;
    }
  }
  stbi_image_free(decoded);
  difference.mean = total / (static_cast<double>(width) * height * 4.0);
  difference.bad_fraction = static_cast<double>(bad) / (static_cast<double>(width) * height);
  return difference;
}

// A gradient RGBA drawable (8-bit), so tiles and channels differ from pixel to pixel.
[[nodiscard]] builder::Drawable gradient(std::uint32_t width, std::uint32_t height, bool alpha = true) {
  builder::Drawable d;
  d.width = width;
  d.height = height;
  d.bpp = alpha ? 4 : 3;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      d.pixels.push_back(static_cast<std::uint8_t>(x * 3));
      d.pixels.push_back(static_cast<std::uint8_t>(y * 5));
      d.pixels.push_back(static_cast<std::uint8_t>((x + y) * 2));
      if (alpha) {
        d.pixels.push_back(static_cast<std::uint8_t>(255 - ((x * 7 + y) % 200)));
      }
    }
  }
  return d;
}

[[nodiscard]] builder::Layer pixel_layer(const std::string& name, builder::Drawable drawable, std::int32_t x = 0,
                                         std::int32_t y = 0) {
  builder::Layer layer;
  layer.name = name;
  layer.type = drawable.bpp == 4 ? 1U : 0U;
  layer.drawable = std::move(drawable);
  layer.properties.push_back(builder::prop_offsets(x, y));
  return layer;
}

[[nodiscard]] builder::Image structure_image(int version, builder::Compression compression) {
  builder::Image image;
  image.version = version;
  image.width = 100;
  image.height = 80;
  image.compression = compression;
  image.properties.push_back([] {
    builder::Bytes b;
    b.f32(150.0F);
    b.f32(150.0F);
    return builder::Property{builder::kResolution, b.data, std::nullopt};
  }());
  // Top first: a text layer, a group with two children (one masked), a background.
  auto top = pixel_layer("Top", gradient(70, 66), 5, 7);
  top.properties.push_back(builder::prop_u32(builder::kMode, 30));  // Multiply
  top.properties.push_back(builder::prop_u32(builder::kOpacity, 128));
  top.properties.push_back(builder::prop_f32(builder::kFloatOpacity, 0.5F));
  top.properties.push_back(builder::prop_u32(builder::kColorTag, 6));
  top.properties.push_back(builder::prop_u32(builder::kLockAlpha, 1));
  top.properties.push_back(builder::prop_empty(builder::kActiveLayer));
  image.layers.push_back(top);
  builder::Layer group;
  group.name = "Group";
  group.drawable = builder::solid(30, 20, {0, 0, 0, 0});
  group.properties = {builder::prop_empty(builder::kGroupItem), builder::prop_offsets(10, 10),
                      builder::prop_u32(builder::kMode, 61), builder::prop_u32(builder::kGroupItemFlags, 0),
                      builder::prop_u32(builder::kVisible, 1)};
  image.layers.push_back(group);
  auto child = pixel_layer("Child", builder::solid(20, 10, {200, 100, 50, 255}), 10, 10);
  child.properties.push_back(builder::prop_item_path({1, 0}));
  child.properties.push_back(builder::prop_u32(builder::kMode, 46));  // Grain Extract
  builder::Channel mask;
  mask.name = "Child mask";
  mask.drawable = builder::solid(20, 10, {64});
  child.mask = mask;
  child.properties.push_back(builder::prop_u32(builder::kApplyMask, 0));
  image.layers.push_back(child);
  auto hidden = pixel_layer("Hidden", builder::solid(5, 5, {1, 2, 3, 255}), 20, 15);
  hidden.properties.push_back(builder::prop_item_path({1, 1}));
  hidden.properties.push_back(builder::prop_u32(builder::kVisible, 0));
  hidden.properties.push_back(builder::prop_u32(builder::kLockContent, 1));
  image.layers.push_back(hidden);
  auto background = pixel_layer("Background", gradient(100, 80, false));
  background.type = 0;
  image.layers.push_back(background);
  return image;
}

void check_structure_document(const Document& document) {
  CHECK(document.width() == 100 && document.height() == 80);
  CHECK(std::abs(document.print_settings().horizontal_ppi - 150.0) < 1e-6);
  const auto& layers = document.layers();
  CHECK(layers.size() == 3);
  CHECK(layers[0].name() == "Background");
  CHECK(layers[0].pixels().format().channels == 3);  // GIMP's alpha-less bottom layer
  CHECK(patchy::layer_locks_position(layers[0]));
  CHECK(layers[1].name() == "Group" && layers[1].kind() == patchy::LayerKind::Group);
  CHECK(layers[1].blend_mode() == BlendMode::PassThrough);
  CHECK(!patchy::layer_group_expanded(layers[1]));
  CHECK(layers[1].children().size() == 2);
  CHECK(layers[1].children()[0].name() == "Hidden");  // bottom first
  CHECK(!layers[1].children()[0].visible());
  CHECK(patchy::layer_locks_image_pixels(layers[1].children()[0]));
  const auto& child = layers[1].children()[1];
  CHECK(child.name() == "Child" && child.blend_mode() == BlendMode::GrainExtract);
  CHECK(child.mask().has_value() && child.mask()->disabled);
  CHECK(child.mask()->bounds == (patchy::Rect{10, 10, 20, 10}));
  CHECK(std::as_const(child).mask()->pixels.pixel(3, 3)[0] == 64);
  const auto& top = layers[2];
  CHECK(top.name() == "Top" && top.blend_mode() == BlendMode::LinearMultiply);  // GIMP 2.10+ Multiply
  CHECK(std::abs(top.opacity() - 0.5F) < 1e-6F);
  CHECK(top.bounds() == (patchy::Rect{5, 7, 70, 66}));
  CHECK(top.color_label() == patchy::kLayerColorRed);
  CHECK(patchy::layer_locks_transparent_pixels(top));
  CHECK(document.active_layer_id() == top.id());
  // A pixel in each of the tiles of the 70 x 66 layer (it spans four).
  for (const auto [x, y] : {std::pair{0, 0}, std::pair{65, 3}, std::pair{2, 65}, std::pair{69, 65}}) {
    const auto px = rgba_at(top, x, y);
    if (px[0] != static_cast<std::uint8_t>(x * 3) || px[1] != static_cast<std::uint8_t>(y * 5)) {
      std::cout << "  Top at " << x << "," << y << ": " << int(px[0]) << " " << int(px[1]) << " " << int(px[2]) << " "
                << int(px[3]) << "\n";
    }
    CHECK(px[0] == static_cast<std::uint8_t>(x * 3) && px[1] == static_cast<std::uint8_t>(y * 5));
    CHECK(px[2] == static_cast<std::uint8_t>((x + y) * 2) && px[3] == static_cast<std::uint8_t>(255 - ((x * 7 + y) % 200)));
  }
}

// --- tests --------------------------------------------------------------------------------

// GIMP 3.2's renders of its legacy grain modes over 8-bit samples (modes-legacy.xcf in
// local-test-fixtures/xcf matched these kernels on every opaque pixel).
void blend_math_gimp_grain_modes_match_gimp_renders() {
  struct Sample {
    std::uint8_t s;
    std::uint8_t d;
    std::uint8_t extract;
    std::uint8_t merge;
  };
  constexpr std::array kSamples{
      Sample{0, 0, 128, 0},      Sample{255, 255, 128, 255}, Sample{200, 100, 28, 172},
      Sample{100, 200, 228, 172}, Sample{0, 255, 255, 127},   Sample{255, 0, 0, 127},
      Sample{128, 128, 128, 128}, Sample{10, 240, 255, 122},
  };
  for (const auto& sample : kSamples) {
    CHECK(patchy::blend_rgb({sample.s, sample.s, sample.s}, {sample.d, sample.d, sample.d},
                            BlendMode::GrainExtract)[0] == sample.extract);
    CHECK(patchy::blend_rgb({sample.s, sample.s, sample.s}, {sample.d, sample.d, sample.d},
                            BlendMode::GrainMerge)[0] == sample.merge);
  }
  // The 16/32-bit kernels agree with the byte kernels within a level.
  for (int a = 0; a < 256; a += 3) {
    for (int b = 0; b < 256; b += 5) {
      const auto s = static_cast<std::uint8_t>(a);
      const auto d = static_cast<std::uint8_t>(b);
      for (const auto mode : {BlendMode::GrainExtract, BlendMode::GrainMerge}) {
        const auto byte = patchy::blend_rgb({s, s, s}, {d, d, d}, mode)[0];
        const auto deep = patchy::blend_rgb_deep({static_cast<float>(a), static_cast<float>(a), static_cast<float>(a)},
                                                 {static_cast<float>(b), static_cast<float>(b), static_cast<float>(b)},
                                                 mode, patchy::DeepDomain::Encoded)[0];
        CHECK(std::abs(deep - static_cast<float>(byte)) <= 1.0F);
      }
    }
  }
  CHECK(patchy::blend_mode_is_gimp_only(BlendMode::GrainMerge));
  CHECK(!patchy::blend_mode_is_gimp_only(BlendMode::Xor));
  CHECK(patchy::blend_mode_is_non_photoshop(BlendMode::GrainExtract));
  CHECK(patchy::blend_mode_is_non_photoshop(BlendMode::Reflect));
  CHECK(!patchy::blend_mode_is_non_photoshop(BlendMode::LinearLight));
  CHECK(!patchy::blend_mode_supported_at_depth(BlendMode::GrainExtract, patchy::BitDepth::Float32));
  CHECK(patchy::blend_mode_supported_at_depth(BlendMode::GrainMerge, patchy::BitDepth::UInt16));
}

// GIMP's color-model and linear-light modes (core/gimp_blend_modes.hpp): sample points of
// the formulas that matched GIMP 3.2's renders of modes.xcf and modes-legacy.xcf within a
// level on every opaque pixel.
void blend_math_gimp_color_modes_match_gimp_renders() {
  struct Sample {
    BlendMode mode;
    std::array<std::uint8_t, 3> s;
    std::array<std::uint8_t, 3> d;
    std::array<std::uint8_t, 3> expected;
  };
  const std::vector<Sample> samples{
      {BlendMode::HsvHue, {200, 40, 90}, {30, 160, 220}, {220, 30, 89}},
      {BlendMode::HsvHue, {12, 250, 128}, {240, 200, 10}, {10, 240, 122}},
      {BlendMode::HsvHue, {128, 128, 128}, {90, 60, 200}, {90, 60, 200}},
      {BlendMode::HsvSaturation, {200, 40, 90}, {30, 160, 220}, {44, 164, 220}},
      {BlendMode::HsvSaturation, {12, 250, 128}, {240, 200, 10}, {240, 200, 12}},
      {BlendMode::HsvSaturation, {128, 128, 128}, {90, 60, 200}, {200, 200, 200}},
      {BlendMode::HslColor, {200, 40, 90}, {30, 160, 220}, {208, 42, 94}},
      {BlendMode::HslColor, {12, 250, 128}, {240, 200, 10}, {5, 245, 122}},
      {BlendMode::HslColor, {128, 128, 128}, {90, 60, 200}, {130, 130, 130}},
      {BlendMode::HsvValue, {200, 40, 90}, {30, 160, 220}, {27, 145, 200}},
      {BlendMode::HsvValue, {12, 250, 128}, {240, 200, 10}, {250, 208, 10}},
      {BlendMode::HsvValue, {128, 128, 128}, {90, 60, 200}, {58, 38, 128}},
      {BlendMode::LchHue, {200, 40, 90}, {30, 160, 220}, {221, 115, 135}},
      {BlendMode::LchHue, {12, 250, 128}, {240, 200, 10}, {0, 236, 117}},
      {BlendMode::LchHue, {128, 128, 128}, {90, 60, 200}, {90, 60, 200}},
      {BlendMode::LchChroma, {200, 40, 90}, {30, 160, 220}, {0, 165, 253}},
      {BlendMode::LchChroma, {12, 250, 128}, {240, 200, 10}, {240, 200, 0}},
      {BlendMode::LchChroma, {128, 128, 128}, {90, 60, 200}, {85, 85, 85}},
      {BlendMode::LchColor, {200, 40, 90}, {30, 160, 220}, {249, 91, 129}},
      {BlendMode::LchColor, {12, 250, 128}, {240, 200, 10}, {0, 236, 116}},
      {BlendMode::LchColor, {128, 128, 128}, {90, 60, 200}, {85, 85, 85}},
      {BlendMode::LchLightness, {200, 40, 90}, {30, 160, 220}, {0, 119, 176}},
      {BlendMode::LchLightness, {12, 250, 128}, {240, 200, 10}, {254, 213, 37}},
      {BlendMode::LchLightness, {128, 128, 128}, {90, 60, 200}, {141, 103, 251}},
      {BlendMode::Luminance, {200, 40, 90}, {30, 160, 220}, {19, 117, 162}},
      {BlendMode::Luminance, {12, 250, 128}, {240, 200, 10}, {255, 213, 11}},
      {BlendMode::Luminance, {128, 128, 128}, {90, 60, 200}, {136, 92, 255}},
      {BlendMode::LumaDarkenOnly, {200, 40, 90}, {30, 160, 220}, {200, 40, 90}},
      {BlendMode::LumaDarkenOnly, {12, 250, 128}, {240, 200, 10}, {240, 200, 10}},
      {BlendMode::LumaDarkenOnly, {128, 128, 128}, {90, 60, 200}, {90, 60, 200}},
      {BlendMode::LumaLightenOnly, {200, 40, 90}, {30, 160, 220}, {30, 160, 220}},
      {BlendMode::LumaLightenOnly, {12, 250, 128}, {240, 200, 10}, {12, 250, 128}},
      {BlendMode::LumaLightenOnly, {128, 128, 128}, {90, 60, 200}, {128, 128, 128}},
      {BlendMode::LinearMultiply, {200, 40, 90}, {30, 160, 220}, {21, 21, 76}},
      {BlendMode::LinearMultiply, {12, 250, 128}, {240, 200, 10}, {11, 196, 2}},
      {BlendMode::LinearMultiply, {128, 128, 128}, {90, 60, 200}, {41, 25, 99}},
      {BlendMode::LinearAddition, {200, 40, 90}, {30, 160, 220}, {202, 164, 233}},
      {BlendMode::LinearAddition, {12, 250, 128}, {240, 200, 10}, {240, 255, 129}},
      {BlendMode::LinearAddition, {128, 128, 128}, {90, 60, 200}, {153, 140, 230}},
      {BlendMode::LinearSubtract, {200, 40, 90}, {30, 160, 220}, {0, 156, 205}},
      {BlendMode::LinearSubtract, {12, 250, 128}, {240, 200, 10}, {240, 0, 0}},
      {BlendMode::LinearSubtract, {128, 128, 128}, {90, 60, 200}, {0, 0, 162}},
      {BlendMode::LinearDivide, {200, 40, 90}, {30, 160, 220}, {41, 255, 255}},
      {BlendMode::LinearDivide, {12, 250, 128}, {240, 200, 10}, {255, 204, 31}},
      {BlendMode::LinearDivide, {128, 128, 128}, {90, 60, 200}, {183, 126, 255}},
  };
  for (const auto& sample : samples) {
    const auto out = patchy::blend_rgb(sample.s, sample.d, sample.mode);
    for (int c = 0; c < 3; ++c) {
      const int delta = std::abs(static_cast<int>(out[static_cast<std::size_t>(c)]) -
                                 static_cast<int>(sample.expected[static_cast<std::size_t>(c)]));
      if (delta > 1) {
        std::cout << "  mode " << static_cast<int>(sample.mode) << " channel " << c << ": " << int(out[c]) << " vs "
                  << int(sample.expected[c]) << "\n";
      }
      CHECK(delta <= 1);
    }
    // The deep kernel agrees within a level.
    const auto deep = patchy::blend_rgb_deep(
        {static_cast<float>(sample.s[0]), static_cast<float>(sample.s[1]), static_cast<float>(sample.s[2])},
        {static_cast<float>(sample.d[0]), static_cast<float>(sample.d[1]), static_cast<float>(sample.d[2])},
        sample.mode, patchy::DeepDomain::Encoded);
    for (int c = 0; c < 3; ++c) {
      CHECK(std::abs(deep[static_cast<std::size_t>(c)] - static_cast<float>(out[static_cast<std::size_t>(c)])) <= 1.0F);
    }
    CHECK(patchy::blend_mode_is_gimp_only(sample.mode) && patchy::blend_mode_is_non_photoshop(sample.mode));
    CHECK(!patchy::blend_mode_supported_at_depth(sample.mode, patchy::BitDepth::Float32));
  }
}

void psd_saves_gimp_blend_modes_as_normal() {
  Document document(4, 2, patchy::PixelFormat::rgba8());
  const std::array modes{BlendMode::GrainExtract, BlendMode::GrainMerge, BlendMode::Multiply};
  for (const auto mode : modes) {
    auto& layer = document.add_pixel_layer("layer", patchy::test::solid_rgba(4, 2, 100, 150, 200, 255));
    layer.set_blend_mode(mode);
  }
  CHECK(patchy::layers_use_gimp_blend_modes(document.layers()));
  const auto unsupported = patchy::find_unsupported_blend_modes(document, patchy::LayeredSaveFormat::Photoshop);
  CHECK((unsupported.modes == std::vector<BlendMode>{BlendMode::GrainExtract, BlendMode::GrainMerge}));
  CHECK(!patchy::layered_format_stores_blend_mode(patchy::LayeredSaveFormat::Aseprite, BlendMode::GrainMerge, false,
                                                  patchy::BitDepth::UInt8));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.layers().size() == modes.size());
  CHECK(reread.layers()[0].blend_mode() == BlendMode::Normal);
  CHECK(reread.layers()[1].blend_mode() == BlendMode::Normal);
  CHECK(reread.layers()[2].blend_mode() == BlendMode::Multiply);
}

void xcf_reads_layers_groups_masks_and_properties() {
  std::vector<std::string> notices;
  const auto document =
      patchy::xcf::DocumentIo::read(builder::write(structure_image(11, builder::Compression::Rle)), &notices);
  check_structure_document(document);
  CHECK(notices.empty());
}

// Raw, RLE and zlib tiles, and 32-bit pointers (versions up to 10) against 64-bit ones.
void xcf_reads_every_tile_compression_and_pointer_size() {
  for (const int version : {3, 7, 10, 11, 14}) {
    for (const auto compression :
         {builder::Compression::None, builder::Compression::Rle, builder::Compression::Zlib}) {
      if (version < 8 && compression == builder::Compression::Zlib) {
        continue;
      }
      check_structure_document(patchy::xcf::DocumentIo::read(builder::write(structure_image(version, compression))));
    }
  }
  // Version 0 ("gimp xcf file"): no precision field.
  auto image = structure_image(0, builder::Compression::Rle);
  check_structure_document(patchy::xcf::DocumentIo::read(builder::write(image)));
}

// Big-endian samples of one layer at a precision, for xcf_reads_every_precision.
[[nodiscard]] builder::Drawable precision_drawable(std::uint32_t component_bytes,
                                                   const std::function<void(builder::Bytes&, double)>& put,
                                                   const std::array<double, 4>& rgba) {
  builder::Drawable d;
  d.width = 3;
  d.height = 2;
  d.bpp = component_bytes * 4;
  builder::Bytes bytes;
  for (int i = 0; i < 6; ++i) {
    for (const auto value : rgba) {
      put(bytes, value);
    }
  }
  d.pixels = bytes.data;
  return d;
}

void xcf_reads_every_precision() {
  patchy::set_deep_editing_override(true);
  struct Case {
    std::uint32_t precision;
    std::uint32_t bytes;
    patchy::BitDepth depth;
    bool linear;
  };
  const auto put_u8 = [](builder::Bytes& b, double v) { b.u8(static_cast<std::uint8_t>(std::lround(v * 255.0))); };
  const auto put_u16 = [](builder::Bytes& b, double v) {
    const auto value = static_cast<std::uint16_t>(std::lround(v * 65535.0));
    b.u8(static_cast<std::uint8_t>(value >> 8U));
    b.u8(static_cast<std::uint8_t>(value));
  };
  const auto put_u32 = [](builder::Bytes& b, double v) {
    b.u32(static_cast<std::uint32_t>(std::llround(v * 4294967295.0)));
  };
  const auto put_half = [](builder::Bytes& b, double v) {
    // Exact halves for the test values (0, 0.25, 0.5, 1).
    std::uint16_t half = 0;
    if (v == 1.0) half = 0x3C00;
    else if (v == 0.5) half = 0x3800;
    else if (v == 0.25) half = 0x3400;
    b.u8(static_cast<std::uint8_t>(half >> 8U));
    b.u8(static_cast<std::uint8_t>(half));
  };
  const auto put_float = [](builder::Bytes& b, double v) { b.f32(static_cast<float>(v)); };
  const auto put_double = [](builder::Bytes& b, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    b.u64(bits);
  };
  const std::array<double, 4> rgba{0.25, 0.5, 1.0, 0.5};
  const std::vector<std::tuple<Case, std::function<void(builder::Bytes&, double)>>> cases{
      {{150, 1, patchy::BitDepth::UInt8, false}, put_u8},     {{175, 1, patchy::BitDepth::UInt8, false}, put_u8},
      {{100, 1, patchy::BitDepth::UInt16, true}, put_u8},     {{250, 2, patchy::BitDepth::UInt16, false}, put_u16},
      {{200, 2, patchy::BitDepth::UInt16, true}, put_u16},    {{350, 4, patchy::BitDepth::UInt16, false}, put_u32},
      {{300, 4, patchy::BitDepth::UInt16, true}, put_u32},    {{550, 2, patchy::BitDepth::Float32, false}, put_half},
      {{500, 2, patchy::BitDepth::Float32, true}, put_half},  {{650, 4, patchy::BitDepth::Float32, false}, put_float},
      {{600, 4, patchy::BitDepth::Float32, true}, put_float}, {{700, 8, patchy::BitDepth::Float32, true}, put_double},
  };
  for (const auto& [spec, put] : cases) {
    builder::Image image;
    image.version = 12;
    image.width = 3;
    image.height = 2;
    image.precision = spec.precision;
    builder::Layer layer;
    layer.name = "Samples";
    layer.drawable = precision_drawable(spec.bytes, put, rgba);
    image.layers.push_back(layer);
    const auto document = patchy::xcf::DocumentIo::read(builder::write(image));
    // The web build opens 32-bit files at 16 bits (depth_supported_on_platform).
    const auto opened =
        patchy::depth_supported_on_platform(spec.depth) ? spec.depth : patchy::BitDepth::UInt16;
    CHECK(document.color_state().bit_depth == opened);
    const auto& layer_pixels = std::as_const(document.layers()[0]).pixels();
    CHECK(layer_pixels.format().bit_depth == opened);
    const auto loaded = patchy::load_pixel(layer_pixels.format(), layer_pixels.pixel(1, 1));
    // Expected on the deep scale: encoded for 8/16 bits, linear for 32.
    for (int c = 0; c < 3; ++c) {
      double expected = rgba[static_cast<std::size_t>(c)];
      if (opened == patchy::BitDepth::Float32) {
        expected = spec.linear ? expected : patchy::srgb_decode(expected);
      } else {
        expected = spec.linear ? patchy::srgb_encode(expected) : expected;
      }
      const double tolerance = spec.precision == 100 ? 2.0 : 0.6;  // 8-bit linear is coarse
      CHECK(std::abs(loaded[static_cast<std::size_t>(c)] - expected * 255.0) <= tolerance);
    }
    CHECK(std::abs(loaded[3] - 127.5F) <= 0.6F);
  }
  // With deep editing off, every precision opens at 8 bits and says so.
  patchy::set_deep_editing_override(false);
  builder::Image image;
  image.version = 12;
  image.width = 3;
  image.height = 2;
  image.precision = 250;
  builder::Layer layer;
  layer.drawable = precision_drawable(2, put_u16, rgba);
  image.layers.push_back(layer);
  std::vector<std::string> notices;
  const auto narrowed = patchy::xcf::DocumentIo::read(builder::write(image), &notices);
  CHECK(narrowed.color_state().bit_depth == patchy::BitDepth::UInt8);
  CHECK(notices.size() == 1 && notices[0].find("8 bits") != std::string::npos);
  patchy::set_deep_editing_override(std::nullopt);
}

void xcf_reads_gray_and_indexed_images() {
  {
    builder::Image image;
    image.base_type = 1;
    image.width = 4;
    image.height = 4;
    builder::Layer layer;
    layer.type = 3;
    layer.drawable = builder::solid(4, 4, {90, 200});
    image.layers.push_back(layer);
    const auto document = patchy::xcf::DocumentIo::read(builder::write(image));
    const auto px = rgba_at(document.layers()[0], 1, 1);
    CHECK(px[0] == 90 && px[1] == 90 && px[2] == 90 && px[3] == 200);
  }
  {
    builder::Image image;
    image.base_type = 2;
    image.width = 4;
    image.height = 4;
    builder::Bytes colormap;
    colormap.u32(3);
    for (const std::uint8_t v : std::array<std::uint8_t, 9>{10, 20, 30, 200, 100, 50, 0, 255, 0}) {
      colormap.u8(v);
    }
    // Old GIMP releases wrote n + 4 as the length: the reader measures the payload.
    image.properties.push_back(builder::Property{builder::kColormap, colormap.data, 3U + 4U});
    builder::Layer layer;
    layer.type = 5;
    layer.drawable = builder::solid(4, 4, {1, 200});
    layer.drawable.pixels[2] = 2;  // the second pixel: index 2, alpha 100 (under half: off)
    layer.drawable.pixels[3] = 100;
    image.layers.push_back(layer);
    const auto document = patchy::xcf::DocumentIo::read(builder::write(image));
    const auto first = rgba_at(document.layers()[0], 0, 0);
    CHECK(first[0] == 200 && first[1] == 100 && first[2] == 50 && first[3] == 255);
    const auto second = rgba_at(document.layers()[0], 1, 0);
    CHECK(second[3] == 0);
    // The colormap rides along in file order for the open path's Palette mode offer (#452).
    const auto& palette = document.indexed_palette();
    CHECK(palette.has_value() && palette->colors.size() == 3 && palette->source_bit_depth == 2);
    CHECK(palette.has_value() && palette->colors[0] == (RgbColor{10, 20, 30}) &&
          palette->colors[2] == (RgbColor{0, 255, 0}));
  }
}

// philipzae's indexed GIMP 3.2 samples from issue 452 (local-test-fixtures/xcf-indexed,
// README.txt there): the colormap attaches whole and in order, every pixel is one of its
// colors, and the composite matches GIMP's render.
void xcf_indexed_samples_attach_their_colormap() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-indexed";
  patchy::test::skip_unless_exists(dir / "README.txt");
  struct Sample {
    const char* name;
    std::size_t count;
    std::uint16_t bits;
    RgbColor first;
    RgbColor last;
    std::uint64_t checksum;  // sum of (7r + 13g + 17b) * (index + 1) over the colormap
  };
  for (const auto& sample : {Sample{"palette-8bit", 228, 8, {0, 0, 0}, {255, 255, 255}, 161554654},
                             Sample{"palette-4bit", 7, 4, {24, 20, 12}, {196, 68, 72}, 114824},
                             Sample{"palette-1bit", 2, 2, {0, 0, 0}, {255, 255, 255}, 18870}}) {
    const auto document = patchy::xcf::DocumentIo::read_file(dir / (std::string(sample.name) + ".xcf"));
    const auto& palette = document.indexed_palette();
    CHECK(palette.has_value());
    if (!palette.has_value()) {
      continue;
    }
    const auto& colors = palette->colors;
    CHECK(colors.size() == sample.count && palette->source_bit_depth == sample.bits);
    CHECK(!colors.empty() && colors.front() == sample.first && colors.back() == sample.last);
    std::uint64_t checksum = 0;
    for (std::size_t i = 0; i < colors.size(); ++i) {
      checksum += (7ULL * colors[i].red + 13ULL * colors[i].green + 17ULL * colors[i].blue) * (i + 1);
    }
    CHECK(checksum == sample.checksum);
    CHECK(!document.palette_editing().has_value());
    CHECK(document.color_state().bit_depth == patchy::BitDepth::UInt8);
    const auto composite = composite_rgba8(document);
    std::size_t off_palette = 0;
    for (std::int32_t y = 0; y < composite.height(); ++y) {
      for (std::int32_t x = 0; x < composite.width(); ++x) {
        const auto* px = composite.pixel(x, y);
        const RgbColor color{px[0], px[1], px[2]};
        if (std::find(colors.begin(), colors.end(), color) == colors.end()) {
          ++off_palette;
        }
      }
    }
    CHECK(off_palette == 0);
    const auto reference = dir / "gimp-renders" / (std::string(sample.name) + ".xcf.png");
    if (std::filesystem::exists(reference)) {
      const auto difference = compare_with_png(composite, reference);
      CHECK(difference.has_value() && difference->worst == 0);
    }
  }
}

// PROP_MODE to Patchy's mode on a pixel layer (docs/xcf.md, "Blend modes").
[[nodiscard]] const std::map<std::uint32_t, BlendMode>& gimp_mode_map() {
  static const std::map<std::uint32_t, BlendMode> map{
      {0, BlendMode::Normal},        {1, BlendMode::Dissolve},     {3, BlendMode::Multiply},
      {4, BlendMode::Screen},        {6, BlendMode::Difference},   {7, BlendMode::LinearDodge},
      {8, BlendMode::Subtract},      {9, BlendMode::Darken},       {10, BlendMode::Lighten},
      {11, BlendMode::HsvHue},       {12, BlendMode::HsvSaturation}, {13, BlendMode::HslColor},
      {14, BlendMode::HsvValue},   {15, BlendMode::Divide},      {16, BlendMode::ColorDodge},
      {17, BlendMode::ColorBurn},    {18, BlendMode::HardLight},   {19, BlendMode::SoftLight},
      {20, BlendMode::GrainExtract}, {21, BlendMode::GrainMerge},  {22, BlendMode::Normal},
      {23, BlendMode::Overlay},      {24, BlendMode::LchHue},      {25, BlendMode::LchChroma},
      {26, BlendMode::LchColor},     {27, BlendMode::LchLightness}, {28, BlendMode::Normal},
      {29, BlendMode::Normal},       {30, BlendMode::LinearMultiply}, {31, BlendMode::Screen},
      {32, BlendMode::Difference},   {33, BlendMode::LinearAddition}, {34, BlendMode::LinearSubtract},
      {35, BlendMode::Darken},       {36, BlendMode::Lighten},     {37, BlendMode::HsvHue},
      {38, BlendMode::HsvSaturation}, {39, BlendMode::HslColor},   {40, BlendMode::HsvValue},
      {41, BlendMode::LinearDivide},       {42, BlendMode::ColorDodge},  {43, BlendMode::ColorBurn},
      {44, BlendMode::HardLight},    {45, BlendMode::SoftLight},   {46, BlendMode::GrainExtract},
      {47, BlendMode::GrainMerge},   {48, BlendMode::VividLight},  {49, BlendMode::PinLight},
      {50, BlendMode::LinearLight},  {51, BlendMode::HardMix},     {52, BlendMode::Exclusion},
      {53, BlendMode::LinearBurn},   {54, BlendMode::LumaDarkenOnly}, {55, BlendMode::LumaLightenOnly},
      {56, BlendMode::Luminance},   {57, BlendMode::Normal},      {59, BlendMode::Normal},
      {60, BlendMode::Normal},       {61, BlendMode::Normal},      {999, BlendMode::Normal},
  };
  return map;
}

// Every PROP_MODE value maps (docs/xcf.md, "Blend modes"), with notices for the
// approximated and missing ones.
void xcf_maps_every_gimp_layer_mode() {
  const auto& expected = gimp_mode_map();
  builder::Image image;
  image.width = 2;
  image.height = 2;
  for (const auto& [mode, blend] : expected) {
    auto layer = pixel_layer("mode " + std::to_string(mode), builder::solid(2, 2, {10, 20, 30, 255}));
    layer.properties.push_back(builder::prop_u32(builder::kMode, mode));
    image.layers.push_back(layer);
  }
  std::vector<std::string> notices;
  const auto document = patchy::xcf::DocumentIo::read(builder::write(image), &notices);
  for (const auto& [mode, blend] : expected) {
    const auto* layer = find_named(document.layers(), "mode " + std::to_string(mode));
    CHECK(layer != nullptr);
    if (layer->blend_mode() != blend) {
      std::cout << "  mode " << mode << " opened as " << static_cast<int>(layer->blend_mode()) << "\n";
    }
    CHECK(layer->blend_mode() == blend);
  }
  const auto has = [&](std::string_view text) {
    return std::any_of(notices.begin(), notices.end(), [&](const std::string& n) { return n.find(text) != std::string::npos; });
  };
  CHECK(has("does not have"));
  CHECK(!has("another color space"));
}

void xcf_text_parasites_parse_gimp_2_and_3_forms() {
  const auto gimp3 = patchy::xcf::detail::parse_text_parasite(R"((text "Hello \"GIMP\"\nsecond")
(font "GimpFont"
    (fonthash "f5f5")
    (fullname "Noto Sans Bold")
    (family "Sans-serif")
    (style "Bold")
    (psname "NotoSans-Bold")
    (index 0))
(font-size 24)
(font-size-unit pixels)
(antialias yes)
(justify center)
(letter-spacing 2.5)
(line-spacing -1)
(box-mode fixed)
(box-width 150)
(box-height 90.5)
(color
    (color "R'G'B'A float" 16 "\0\0\200\77\0\0\0\0\0\0\0\77\0\0\200\77" 0))
)");
  CHECK(gimp3.text == "Hello \"GIMP\"\nsecond");
  CHECK(gimp3.font == "Noto Sans Bold" && gimp3.font_family == "Sans-serif" && gimp3.font_style == "Bold");
  CHECK(gimp3.font_size == 24.0 && gimp3.font_size_unit == "pixels" && gimp3.antialias);
  CHECK(gimp3.justify == "center" && gimp3.letter_spacing == 2.5 && gimp3.line_spacing == -1.0);
  CHECK(gimp3.box_mode == "fixed" && gimp3.box_width == 150.0 && gimp3.box_height == 90.5);
  CHECK(gimp3.have_color && gimp3.color[0] == 1.0 && gimp3.color[1] == 0.0 && std::abs(gimp3.color[2] - 0.5) < 1e-6);
  const auto gimp2 = patchy::xcf::detail::parse_text_parasite(
      "(text \"Old\")\n(font \"Serif Bold Italic\")\n(font-size 18.000000)\n(font-size-unit points)\n"
      "(color (color-rgb 0.000000 0.501961 1.000000))\n(justify left)\n(box-mode dynamic)\n");
  CHECK(gimp2.text == "Old" && gimp2.font == "Serif Bold Italic" && gimp2.font_size_unit == "points");
  CHECK(gimp2.have_color && std::abs(gimp2.color[1] - 0.501961) < 1e-6);
  // Hostile input throws, never crashes.
  for (const std::string bad : {"(text \"unterminated", "((((((((((((((((((((((((((((((((((((((((x", "(text \\", ")"}) {
    try {
      (void)patchy::xcf::detail::parse_text_parasite(bad);
    } catch (const std::exception&) {
    }
  }
}

[[nodiscard]] std::string metadata_of(const patchy::Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  return found == layer.metadata().end() ? std::string() : found->second;
}

void xcf_text_layers_open_as_editable_text_over_gimps_raster() {
  builder::Image image;
  image.width = 60;
  image.height = 40;
  image.properties.push_back([] {
    builder::Bytes b;
    b.f32(144.0F);
    b.f32(144.0F);
    return builder::Property{builder::kResolution, b.data, std::nullopt};
  }());
  auto text = pixel_layer("Hello", builder::solid(30, 12, {255, 0, 0, 255}), 3, 4);
  text.properties.push_back(builder::prop_parasites({builder::text_parasite(
      "(text \"Hello\")\n(font \"Serif Bold\")\n(font-size 10)\n(font-size-unit points)\n"
      "(color (color-rgb 1 0 0))\n(justify right)\n(box-mode dynamic)\n")}));
  image.layers.push_back(text);
  auto rich = pixel_layer("Rich", builder::solid(30, 12, {0, 0, 0, 255}), 3, 20);
  rich.properties.push_back(builder::prop_parasites({builder::text_parasite(
      "(markup \"<markup>A <b>bold</b> <span foreground=\\\"#00ff00\\\">green</span></markup>\")\n"
      "(font \"Sans-serif\")\n(font-size 16)\n(font-size-unit pixels)\n(color (color-rgb 0 0 0))\n")}));
  image.layers.push_back(rich);
  auto painted = pixel_layer("Painted", builder::solid(10, 10, {1, 1, 1, 255}), 40, 20);
  painted.properties.push_back(builder::prop_parasites({builder::text_parasite("(text \"Painted\")\n(font-size 10)\n")}));
  painted.properties.push_back(builder::prop_u32(builder::kTextLayerFlags, 3));
  image.layers.push_back(painted);
  std::vector<std::string> notices;
  const auto document = patchy::xcf::DocumentIo::read(builder::write(image), &notices);
  const auto* hello = find_named(document.layers(), "Hello");
  CHECK(hello != nullptr && patchy::layer_is_text(*hello));
  CHECK(metadata_of(*hello, patchy::kLayerMetadataText) == "Hello");
  CHECK(metadata_of(*hello, patchy::kLayerMetadataTextFont) == "Serif");
  CHECK(metadata_of(*hello, patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata_of(*hello, patchy::kLayerMetadataTextSize) == "20");  // 10 pt at 144 ppi
  CHECK(metadata_of(*hello, patchy::kLayerMetadataTextColor) == "#ff0000");
  CHECK(metadata_of(*hello, patchy::kLayerMetadataTextParagraphRuns).find("right") != std::string::npos);
  CHECK(rgba_at(*hello, 0, 0)[0] == 255);  // GIMP's raster stays until the text is edited
  const auto* rich_layer = find_named(document.layers(), "Rich");
  CHECK(rich_layer != nullptr && metadata_of(*rich_layer, patchy::kLayerMetadataText) == "A bold green");
  const auto runs = metadata_of(*rich_layer, patchy::kLayerMetadataTextRuns);
  CHECK(!runs.empty() && runs.find("00ff00") != std::string::npos);
  CHECK(metadata_of(*rich_layer, patchy::kLayerMetadataTextFont) == "Sans Serif");
  const auto* painted_layer = find_named(document.layers(), "Painted");
  CHECK(painted_layer != nullptr && !patchy::layer_is_text(*painted_layer));
  CHECK(std::any_of(notices.begin(), notices.end(),
                    [](const std::string& n) { return n.find("painted on") != std::string::npos; }));
}

void xcf_floating_selection_opens_above_its_layer() {
  builder::Image image;
  image.width = 20;
  image.height = 20;
  auto floating = pixel_layer("Pasted", builder::solid(4, 4, {0, 255, 0, 255}), 2, 2);
  floating.floating_target = 1;
  image.layers.push_back(floating);
  image.layers.push_back(pixel_layer("Target", builder::solid(10, 10, {255, 0, 0, 255})));
  image.layers.push_back(pixel_layer("Below", builder::solid(20, 20, {0, 0, 255, 255})));
  std::vector<std::string> notices;
  const auto document = patchy::xcf::DocumentIo::read(builder::write(image), &notices);
  CHECK(document.layers().size() == 3);
  CHECK(document.layers()[1].name() == "Target" && document.layers()[2].name() == "Pasted");
  CHECK(notices.size() == 1 && notices[0].find("floating selection") != std::string::npos);
}

void xcf_reads_channels_selection_paths_guides_and_metadata() {
  builder::Image image;
  image.version = 15;
  image.width = 32;
  image.height = 24;
  builder::Bytes guides;
  guides.i32(10);
  guides.u8(1);
  guides.i32(-5);  // off the canvas: kept from version 15
  guides.u8(2);
  image.properties.push_back(builder::Property{builder::kGuides, guides.data, std::nullopt});
  image.properties.push_back(builder::prop_u32(builder::kUnit, 2));
  // PROP_PATHS (format 3) with GIMP 3's anchor 1 / control 2 numbering: an open curve.
  builder::Bytes paths;
  paths.u32(0);
  paths.u32(1);
  paths.string("Curve");
  paths.u32(0);
  paths.u8(2);
  paths.u32(0);   // open
  paths.u32(5);   // points
  paths.u32(3);   // version
  paths.u32(1);
  paths.u32(7);   // tattoo
  for (const auto [type, x, y] : {std::tuple{1, 1.0F, 20.0F}, std::tuple{2, 6.0F, 10.0F}, std::tuple{2, 12.0F, 16.0F},
                                  std::tuple{1, 19.0F, 12.0F}, std::tuple{2, 19.0F, 12.0F}}) {
    paths.i32(type);
    paths.f32(x);
    paths.f32(y);
  }
  image.properties.push_back(builder::Property{builder::kPaths, paths.data, std::nullopt});
  builder::Bytes comment;
  for (const char c : std::string("A GIMP comment")) comment.u8(static_cast<std::uint8_t>(c));
  comment.u8(0);
  image.properties.push_back(builder::prop_parasites({{"gimp-comment", comment.data}}));
  image.layers.push_back(pixel_layer("Layer", builder::solid(32, 24, {9, 9, 9, 255})));
  builder::Channel saved;
  saved.name = "Saved ellipse";
  saved.drawable = builder::solid(32, 24, {200});
  saved.properties.push_back(builder::prop_u32(builder::kOpacity, 127));
  image.channels.push_back(saved);
  builder::Channel selection;
  selection.name = "Selection Mask";
  selection.drawable = builder::solid(32, 24, {255});
  selection.properties.push_back(builder::prop_empty(builder::kSelection));
  image.channels.push_back(selection);
  const auto document = patchy::xcf::DocumentIo::read(builder::write(image));
  CHECK(document.guides().size() == 2);
  CHECK(document.guides()[0].orientation == patchy::GuideOrientation::Horizontal);
  CHECK(document.guides()[0].position_32 == 320);
  CHECK(document.guides()[1].position_32 == -160);
  CHECK(document.print_settings().width_display_unit == 2);
  CHECK(document.channels().size() == 2);
  CHECK(document.channels()[0].name() == "Saved ellipse");
  CHECK(std::as_const(document.channels()[0]).pixels().pixel(4, 4)[0] == 200);
  CHECK(document.channels()[1].name() == "Selection Mask");
  CHECK(document.paths().size() == 1);
  const auto& path = document.paths()[0];
  CHECK(path.name() == "Curve" && path.path().subpaths.size() == 1);
  const auto& subpath = path.path().subpaths[0];
  CHECK(!subpath.closed && subpath.anchors.size() == 2);
  CHECK(subpath.anchors[0].anchor_x == 1.0 && subpath.anchors[0].out_x == 6.0 && subpath.anchors[0].out_y == 10.0);
  CHECK(subpath.anchors[1].in_x == 12.0 && subpath.anchors[1].in_y == 16.0 && subpath.anchors[1].anchor_x == 19.0);
  CHECK(document.file_info().source_format == "XCF");
  CHECK(document.file_info().text.size() == 1 && document.file_info().text[0].value == "A GIMP comment");
}

[[nodiscard]] builder::Property filter_argument(const std::string& name, std::uint32_t type,
                                               const std::vector<std::uint8_t>& data) {
  builder::Bytes b;
  b.string(name);
  b.u32(type);
  b.append(data);
  return builder::Property{builder::kFilterArgument, b.data, std::nullopt};
}

[[nodiscard]] std::vector<std::uint8_t> u32_bytes(std::uint32_t value) {
  builder::Bytes b;
  b.u32(value);
  return b.data;
}

[[nodiscard]] std::vector<std::uint8_t> f32_bytes(float value) {
  builder::Bytes b;
  b.f32(value);
  return b.data;
}

[[nodiscard]] std::vector<std::uint8_t> config_bytes(const std::string& text) {
  builder::Bytes b;
  b.u32(static_cast<std::uint32_t>(text.size()));
  b.append(std::vector<std::uint8_t>(text.begin(), text.end()));
  return b.data;
}

// GIMP 3 layer effects: color filters bake into the pixels with GIMP's formulas, a drop
// shadow becomes a layer style, the rest is left out with a notice.
void xcf_layer_effects_bake_or_become_styles() {
  builder::Image image;
  image.version = 22;
  image.width = 4;
  image.height = 2;
  const auto layer_with = [&](const std::string& name, builder::Effect effect) {
    auto layer = pixel_layer(name, builder::solid(4, 2, {200, 100, 50, 255}));
    layer.effects.push_back(std::move(effect));
    return layer;
  };
  builder::Effect desaturate{"Average", "gimp:desaturate", {filter_argument("mode", 5, u32_bytes(2))}};
  builder::Effect invert{"Invert", "gegl:invert-gamma", {}};
  builder::Effect half_invert{"Half invert", "gegl:invert-gamma", {builder::prop_f32(builder::kFloatOpacity, 0.5F)}};
  builder::Effect levels{"Levels", "gimp:levels",
                         {filter_argument("config", 6,
                                          config_bytes("(trc perceptual)\n(channel value)\n(low-input 0.2)\n"
                                                       "(high-input 0.8)\n(gamma 1)\n(low-output 0)\n(high-output 1)\n"))}};
  builder::Effect posterize{"Posterize", "gimp:posterize", {filter_argument("levels", 1, u32_bytes(2))}};
  builder::Effect shadow{"Shadow", "gegl:dropshadow",
                         {filter_argument("x", 3, f32_bytes(3.0F)), filter_argument("y", 3, f32_bytes(4.0F)),
                          filter_argument("radius", 3, f32_bytes(2.0F))}};
  builder::Effect bloom{"Bloom", "gegl:bloom", {}};
  image.layers = {layer_with("desaturate", desaturate), layer_with("invert", invert),
                  layer_with("half invert", half_invert), layer_with("levels", levels),
                  layer_with("posterize", posterize), layer_with("shadow", shadow), layer_with("bloom", bloom)};
  std::vector<std::string> notices;
  const auto document = patchy::xcf::DocumentIo::read(builder::write(image), &notices);
  const auto px = [&](const char* name) { return rgba_at(*find_named(document.layers(), name), 1, 1); };
  const auto near = [](std::uint8_t value, int expected) { return std::abs(static_cast<int>(value) - expected) <= 1; };
  CHECK(near(px("desaturate")[0], 117) && near(px("desaturate")[1], 117) && near(px("desaturate")[2], 117));
  CHECK(near(px("invert")[0], 55) && near(px("invert")[1], 155) && near(px("invert")[2], 205));
  // Half opacity mixes in linear light: brighter than the display-value midpoint.
  const auto half = px("half invert");
  CHECK(half[0] > 140 && half[0] < 160);
  CHECK(near(px("levels")[0], 248) && px("levels")[2] == 0);
  CHECK(px("posterize")[0] == 255 && px("posterize")[1] == 0 && px("posterize")[2] == 0);
  const auto* shadowed = find_named(document.layers(), "shadow");
  CHECK(shadowed->layer_style().drop_shadows.size() == 1);
  CHECK(std::abs(shadowed->layer_style().drop_shadows[0].distance - 5.0F) < 1e-4F);
  CHECK(std::abs(shadowed->layer_style().drop_shadows[0].size - 6.0F) < 1e-4F);
  CHECK(px("bloom")[0] == 200);
  const auto has = [&](std::string_view text) {
    return std::any_of(notices.begin(), notices.end(), [&](const std::string& n) { return n.find(text) != std::string::npos; });
  };
  CHECK(has("were applied"));
  CHECK(has("no Patchy equivalent"));
}

// GIMP's renders of each color filter on the 4096-color sweep
// (scripts/dev/xcf/make_effect_fixtures.py) against Patchy's baked result.
void xcf_effect_fixtures_match_gimp_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "xcf" / "effects";
  patchy::test::skip_unless_exists(dir / "posterize.xcf");
  std::vector<std::string> failures;
  for (const auto& item : std::filesystem::directory_iterator(dir)) {
    if (item.path().extension() != ".xcf") {
      continue;
    }
    const auto stem = item.path().stem().string();
    std::vector<std::string> notices;
    const auto document = patchy::xcf::DocumentIo::read_file(item.path(), &notices);
    const auto difference = compare_with_png(composite_rgba8(document), dir / (stem + ".png"));
    CHECK(difference.has_value());
    const bool applied = std::any_of(notices.begin(), notices.end(),
                                     [](const std::string& n) { return n.find("were applied") != std::string::npos; });
    std::cout << "  " << std::left << std::setw(26) << stem << " mean " << std::fixed << std::setprecision(3)
              << difference->mean << "  worst " << difference->worst << (applied ? "" : "  (not applied)") << "\n";
    // Colorize with lightness and Color Balance are left out or uncalibrated.
    if (applied && stem != "colorize-2" && (difference->mean > 0.6 || difference->worst > 4)) {
      failures.push_back(stem);
    }
  }
  for (const auto& failure : failures) {
    std::cout << "  FAIL " << failure << "\n";
  }
  CHECK(failures.empty());
}

// GIMP's own .xcf.gz, .xcf.bz2 and .xcf.xz of one small image (test-fixtures/xcf).
void xcf_unwraps_gzip_bzip2_and_xz() {
  const auto plain = read_bytes(patchy::test::committed_format_fixture_path("xcf", "compressed.xcf"));
  CHECK(!plain.empty());
  const auto reference = patchy::flatten_document_rgba8(patchy::xcf::DocumentIo::read(plain));
  for (const char* name : {"compressed.xcf.gz", "compressed.xcf.bz2", "compressed.xcf.xz"}) {
    const auto bytes = read_bytes(patchy::test::committed_format_fixture_path("xcf", name));
    CHECK(patchy::xcf::sniff(bytes));
    // GIMP saved each file on its own, so only the gzip one holds exactly the plain bytes
    // (the others differ in a tattoo); all of them render the same.
    const auto unwrapped = patchy::xcf::unwrap(bytes);
    CHECK(unwrapped.size() == plain.size() && std::memcmp(unwrapped.data(), "gimp xcf ", 9) == 0);
    if (std::string(name).ends_with(".gz")) {
      CHECK(unwrapped == plain);
    }
    const auto composite = patchy::flatten_document_rgba8(patchy::xcf::DocumentIo::read(bytes));
    CHECK(composite.width() == reference.width());
    CHECK(std::equal(composite.data().begin(), composite.data().end(), reference.data().begin()));
    // A cut-off wrapper is damaged, not a crash.
    try {
      (void)patchy::xcf::DocumentIo::read(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + bytes.size() / 2));
      CHECK(false);
    } catch (const std::runtime_error&) {
    }
  }
  CHECK(patchy::xcf::is_compressed_xcf_file_name("Picture.XCF.gz"));
  CHECK(patchy::xcf::is_compressed_xcf_file_name("a.xcf.bz2") && patchy::xcf::is_compressed_xcf_file_name("a.xcf.xz"));
  CHECK(!patchy::xcf::is_compressed_xcf_file_name("a.gz") && !patchy::xcf::is_compressed_xcf_file_name("xcf.gz"));
}

void xcf_hostile_input_never_crashes() {
  const auto valid = builder::write(structure_image(11, builder::Compression::Rle));
  std::size_t opened = 0;
  std::size_t refused = 0;
  const auto attempt = [&](const std::vector<std::uint8_t>& bytes) {
    try {
      const auto document = patchy::xcf::DocumentIo::read(bytes);
      CHECK(document.width() > 0 && document.height() > 0 && !document.layers().empty());
      ++opened;
    } catch (const std::runtime_error&) {
      ++refused;
    }
  };
  for (std::size_t length = 0; length < valid.size(); length += 11) {
    attempt(std::vector<std::uint8_t>(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length)));
  }
  std::uint64_t state = 0x9E3779B97F4A7C15ULL;
  const auto next = [&] {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  };
  for (int round = 0; round < 800; ++round) {
    auto bytes = valid;
    const auto flips = 1 + next() % 4;
    for (std::uint64_t i = 0; i < flips; ++i) {
      const auto at = static_cast<std::size_t>(next() % bytes.size());
      if (round % 5 == 0 && at + 4 <= bytes.size()) {
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(at), 4, std::uint8_t{0xFF});
      } else {
        bytes[at] ^= static_cast<std::uint8_t>(1U << (next() % 8));
      }
    }
    attempt(bytes);
  }
  std::cout << "  mutation sweep: " << opened << " opened, " << refused << " refused\n";
  // A canvas past the limits is "too large", not "damaged".
  builder::Image huge;
  huge.width = 400000;
  huge.height = 10;
  try {
    (void)patchy::xcf::DocumentIo::read(builder::write(huge));
    CHECK(false);
  } catch (const std::runtime_error& error) {
    CHECK(std::string(error.what()).find("too large") != std::string::npos);
  }
  // A layer claiming 2^31 pixels with no data behind it.
  builder::Image bomb;
  bomb.width = 16;
  bomb.height = 16;
  auto layer = pixel_layer("Bomb", builder::solid(1, 1, {0, 0, 0, 0}));
  layer.drawable.width = 65536;
  layer.drawable.height = 32768;
  layer.drawable.no_tiles = true;
  bomb.layers.push_back(layer);
  try {
    (void)patchy::xcf::DocumentIo::read(builder::write(bomb));
  } catch (const std::runtime_error&) {
  }
  // Not XCF at all, and CinePaint's XCF.
  for (const std::string& text : {std::string("hello world, not an image"), std::string("gimp xcf v100\0", 14)}) {
    try {
      (void)patchy::xcf::DocumentIo::read(std::vector<std::uint8_t>(text.begin(), text.end()));
      CHECK(false);
    } catch (const std::runtime_error&) {
    }
  }
  // Deep nesting: 200 groups, each in the previous one.
  builder::Image nested;
  nested.width = 4;
  nested.height = 4;
  std::vector<std::uint32_t> path;
  for (int i = 0; i < 200; ++i) {
    builder::Layer group;
    group.name = "G" + std::to_string(i);
    group.drawable = builder::solid(1, 1, {0, 0, 0, 0});
    group.properties.push_back(builder::prop_empty(builder::kGroupItem));
    path.push_back(0);
    if (i > 0) group.properties.push_back(builder::prop_item_path(path));
    nested.layers.push_back(group);
  }
  (void)patchy::xcf::DocumentIo::read(builder::write(nested));
}

void xcf_registry_sniff_and_open_by_content() {
  const auto bytes = builder::write(structure_image(11, builder::Compression::Rle));
  for (const char* extension : {".xcf", ".xcfgz", ".xcfbz2", ".xcfxz"}) {
    const auto* handler = patchy::builtin_format_registry().find_by_extension(extension);
    CHECK(handler != nullptr && handler->identifier == "patchy.formats.xcf" && !handler->can_write());
  }
  const auto* handler = patchy::builtin_format_registry().find_by_extension(".xcf");
  CHECK(handler->sniff && handler->sniff(bytes));
  check_structure_document(handler->read(bytes).document);
  const auto sniffed = patchy::formats::sniff_openable_image_format(bytes);
  CHECK(sniffed.has_value() && sniffed->extension == "xcf");
  CHECK(patchy::formats::extensions_share_format("xcf", "XCF"));
}

void xcf_unicode_path_opens() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"xcf");
  const auto bytes = builder::write(structure_image(11, builder::Compression::Zlib));
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto path = dir / patchy::test::unicode_path_piece(std::u8string(stem) + u8".xcf");
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    check_structure_document(patchy::xcf::DocumentIo::read_file(path));
  }
}

// The committed GIMP-made files against GIMP's own renders of them (test-fixtures/xcf).
void xcf_committed_gimp_files_match_gimp_renders() {
  struct Entry {
    const char* name;
    double max_mean;
    int max_worst;
  };
  // Ceilings a little over the measured differences (docs/xcf.md, "Rendering against GIMP"):
  // what remains is GIMP compositing partly covered pixels in linear light.
  for (const auto& entry : {Entry{"compressed", 0.01, 1}, Entry{"plain-rgba", 0.01, 1}, Entry{"structure", 1.3, 70},
                            Entry{"modes", 4.2, 255}, Entry{"modes-legacy", 0.6, 60}, Entry{"groups", 2.9, 50},
                            Entry{"text", 2.0, 80}, Entry{"indexed", 0.01, 1}}) {
    const auto xcf_path = patchy::test::committed_format_fixture_path("xcf", std::string(entry.name) + ".xcf");
    const auto png_path = patchy::test::committed_format_fixture_path("xcf", std::string(entry.name) + ".png");
    const auto document = patchy::xcf::DocumentIo::read_file(xcf_path);
    const auto difference = compare_with_png(composite_rgba8(document), png_path);
    CHECK(difference.has_value());
    std::cout << "  " << entry.name << ": mean " << difference->mean << ", worst " << difference->worst << "\n";
    CHECK(difference->mean <= entry.max_mean && difference->worst <= entry.max_worst);
  }
}

// GIMP's fixture set (scripts/dev/xcf/make_xcf_fixtures.py): every file opens, and its
// composite is compared with GIMP's merge of the visible layers. The tolerances are the
// known differences in docs/xcf.md ("Rendering against GIMP").
void xcf_gimp_fixtures_match_gimp_renders_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "xcf";
  patchy::test::skip_unless_exists(dir / "manifest.txt");
  patchy::set_deep_editing_override(true);
  std::size_t checked = 0;
  std::vector<std::string> failures;
  for (const auto& item : std::filesystem::directory_iterator(dir)) {
    const auto name = item.path().filename().string();
    if (name.find(".xcf") == std::string::npos) {
      continue;
    }
    const auto stem = name.substr(0, name.find(".xcf"));
    const auto png = dir / (stem + ".png");
    std::vector<std::string> notices;
    Document document;
    try {
      document = patchy::xcf::DocumentIo::read_file(item.path(), &notices);
    } catch (const std::exception& error) {
      failures.push_back(name + ": " + error.what());
      continue;
    }
    ++checked;
    if (!std::filesystem::exists(png)) {
      continue;
    }
    const auto composite = composite_rgba8(document);
    write_png_artifact("xcf-renders", stem, composite);
    const auto difference = compare_with_png(composite, png);
    if (!difference.has_value()) {
      failures.push_back(name + ": the render could not be compared");
      continue;
    }
    std::cout << "  " << std::left << std::setw(34) << name << " mean " << std::fixed << std::setprecision(2)
              << difference->mean << "  worst " << std::setw(3) << difference->worst << "  bad "
              << std::setprecision(4) << difference->bad_fraction << "  notices " << notices.size() << "\n";
    // The per-file ceilings: linear-light compositing and GIMP's own color models move
    // edges and the HSV/LCh tiles; anything past these is a real regression.
    // The "-alpha" mode files blend over a half-transparent backdrop, where GIMP's clip to
    // backdrop keeps the backdrop's coverage and Patchy's masks only approach it.
    const double max_mean = name.find("-alpha") != std::string::npos ? 16.0
                            : name.rfind("modes", 0) == 0          ? 5.0
                                                                   : 3.0;
    if (difference->mean > max_mean) {
      failures.push_back(name + ": mean difference " + std::to_string(difference->mean));
    }
  }
  patchy::set_deep_editing_override(std::nullopt);
  for (const auto& failure : failures) {
    std::cout << "  FAIL " << failure << "\n";
  }
  CHECK(checked > 0);
  CHECK(failures.empty());
}

// The public corpus (local-test-fixtures/xcf-corpus, sources in its README.txt): every file
// opens or is refused with a plain message, quickly; a PNG beside a file is its reference.
void xcf_corpus_opens_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-corpus";
  patchy::test::skip_unless_exists(dir / "README.txt");
  patchy::set_deep_editing_override(true);
  patchy::xcf::detail::set_trace_override(1);
  std::size_t opened = 0;
  std::size_t refused = 0;
  std::size_t compared_count = 0;
  std::size_t far_count = 0;
  for (const auto& item : std::filesystem::recursive_directory_iterator(dir)) {
    if (!item.is_regular_file()) {
      continue;
    }
    const auto name = item.path().filename().string();
    std::string lower;
    for (const char c : name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const bool xcf_name = lower.ends_with(".xcf") || lower.ends_with(".xcf.gz") || lower.ends_with(".xcf.bz2") ||
                          lower.ends_with(".xcf.xz") || lower.ends_with(".xcfgz") || lower.ends_with(".xcfbz2") ||
                          lower.ends_with(".xcfxz");
    if (!xcf_name) {
      continue;
    }
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::string> notices;
    std::string outcome;
    // lexically_relative: relative() goes through weakly_canonical, which fails under wasm NODERAWFS.
    auto relative = item.path().lexically_relative(dir).generic_string();
    std::string render_name;
    for (std::size_t i = 0; i < relative.size(); ++i) {
      render_name += relative[i] == '/' ? std::string("__") : std::string(1, relative[i]);
    }
    double read_ms = 0.0;
    try {
      const auto document = patchy::xcf::DocumentIo::read_file(item.path(), &notices);
      read_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      ++opened;
      const auto reference = dir / "gimp-renders" / (render_name + ".png");
      std::string compared;
      if (std::filesystem::exists(reference)) {
        const auto composite = composite_rgba8(document);
        write_png_artifact("xcf-corpus-renders", render_name, composite);
        if (const auto difference = compare_with_png(composite, reference)) {
          std::ostringstream text;
          text << std::fixed << std::setprecision(2) << " | vs GIMP mean " << difference->mean << " worst "
               << difference->worst << " bad " << std::setprecision(4) << difference->bad_fraction;
          compared = text.str();
          ++compared_count;
          if (difference->mean > 6.0) {
            ++far_count;
          }
        } else {
          compared = " | GIMP render size differs";
        }
      }
      std::size_t count = 0;
      std::function<void(const std::vector<patchy::Layer>&)> walk = [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          ++count;
          walk(layer.children());
        }
      };
      walk(document.layers());
      std::ostringstream text;
      text << document.width() << "x" << document.height() << " " << static_cast<int>(document.color_state().bit_depth)
           << "-bit, " << count << " layers, " << notices.size() << " notices";
      outcome = text.str() + compared;
    } catch (const std::exception& error) {
      ++refused;
      outcome = std::string("refused: ") + error.what();
    }
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "  " << std::left << std::setw(48) << name << " " << outcome << " (read " << std::fixed
              << std::setprecision(0) << read_ms << " ms, all " << ms << " ms)\n";
    for (const auto& notice : notices) {
      std::cout << "      - " << notice << "\n";
    }
  }
  patchy::set_deep_editing_override(std::nullopt);
  patchy::xcf::detail::set_trace_override(-1);
  std::cout << "  corpus: " << opened << " opened, " << refused << " refused, " << compared_count
            << " compared with GIMP, " << far_count << " with a mean difference over 6\n";
  CHECK(opened > 0);
}

// --- issue 487: Erase and Split layers --------------------------------------------------------

[[nodiscard]] std::vector<std::string> top_level_names(const Document& document) {
  std::vector<std::string> names;
  for (auto it = document.layers().rbegin(); it != document.layers().rend(); ++it) {
    names.push_back(it->name());
  }
  return names;
}

// The active layer GIMP saved can be one the import replaces (an Erase or Split layer):
// the file still opens, with a layer that exists active (issue 487, "test 4").
void xcf_active_erase_layer_still_opens() {
  for (const std::uint32_t mode : {58U, 60U}) {
    builder::Image image;
    image.width = 4;
    image.height = 4;
    auto eraser = pixel_layer("Eraser", builder::solid(4, 4, {0, 0, 0, 255}));
    eraser.properties.push_back(builder::prop_u32(builder::kMode, mode));
    eraser.properties.push_back(builder::prop_empty(builder::kActiveLayer));
    image.layers.push_back(eraser);
    image.layers.push_back(pixel_layer("Middle", builder::solid(4, 4, {10, 200, 30, 255})));
    image.layers.push_back(pixel_layer("Bottom", builder::solid(4, 4, {200, 10, 30, 255})));
    const auto document = patchy::xcf::DocumentIo::read(builder::write(image));
    const auto active = document.active_layer_id();
    CHECK(active.has_value() && document.find_layer(*active) != nullptr);
  }
}

// GIMP draws the bottom visible layer of a stack as Normal whatever its mode, Erase and
// Split included (measured on GIMP 3.2): such a layer stays, as Normal.
void xcf_bottom_erase_layer_draws_as_normal() {
  builder::Image image;
  image.width = 4;
  image.height = 4;
  auto eraser = pixel_layer("Eraser", builder::solid(4, 4, {20, 220, 120, 128}));
  eraser.properties.push_back(builder::prop_u32(builder::kMode, 58));
  image.layers.push_back(eraser);
  auto hidden = pixel_layer("Hidden", builder::solid(4, 4, {200, 10, 30, 255}));
  hidden.properties.push_back(builder::prop_u32(builder::kVisible, 0));
  image.layers.push_back(hidden);
  const auto document = patchy::xcf::DocumentIo::read(builder::write(image));
  CHECK((top_level_names(document) == std::vector<std::string>{"Eraser", "Hidden"}));
  const auto* layer = find_named(document.layers(), "Eraser");
  CHECK(layer != nullptr && layer->blend_mode() == BlendMode::Normal && layer->visible());
  const auto composite = patchy::flatten_document_rgba8(document);
  const auto* px = composite.pixel(1, 1);
  CHECK(px[0] == 20 && px[1] == 220 && px[2] == 120 && std::abs(px[3] - 128) <= 1);
}

// philipzae's files from issue 487 (local-test-fixtures/xcf-issue-487, README.txt there):
// both open, the active layer exists, and the composite matches GIMP's render.
void xcf_issue_487_samples_open_like_gimp() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-issue-487";
  patchy::test::skip_unless_exists(dir / "README.txt");
  for (const std::string stem : {"0203 - layer blend modes - test 3", "0203 - layer blend modes - test 4"}) {
    std::vector<std::string> notices;
    Document document;
    try {
      document = patchy::xcf::DocumentIo::read_file(dir / (stem + ".xcf"), &notices);
    } catch (const std::exception& error) {
      std::cout << "  " << stem << ": " << error.what() << "\n";
      CHECK(false);
      continue;
    }
    const auto active = document.active_layer_id();
    CHECK(active.has_value() && document.find_layer(*active) != nullptr);
    const auto composite = composite_rgba8(document);
    write_png_artifact("xcf-issue-487", stem, composite);
    const auto difference = compare_with_png(composite, dir / "gimp-renders" / (stem + ".xcf.png"));
    CHECK(difference.has_value());
    if (difference.has_value()) {
      std::cout << "  " << stem << ": mean " << difference->mean << " worst " << difference->worst << "\n";
      // Dissolve's noise is Patchy's own, and Color Erase and Merge open as Normal.
      CHECK(difference->mean < 12.0);
    }
  }
}

// --- layer trees against GIMP ---------------------------------------------------------------

struct TreeItem {
  int depth{0};
  std::string kind;  // group, text or layer
  std::uint32_t mode{0};
  bool visible{true};
  int opacity{100};
  patchy::Rect bounds;
  bool mask{false};
  std::string name;
};

struct GimpTree {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<TreeItem> items;
};

// A gimp-trees/*.tree.txt file (scripts/dev/xcf/dump_gimp_trees.py).
[[nodiscard]] std::optional<GimpTree> read_gimp_tree(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::string line;
  if (!std::getline(file, line) || !line.starts_with("image ")) {
    return std::nullopt;
  }
  GimpTree tree;
  std::istringstream header(line.substr(6));
  header >> tree.width >> tree.height;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (int i = 0; i < 10; ++i) {
      const auto tab = line.find('\t', start);
      if (tab == std::string::npos) {
        return std::nullopt;
      }
      fields.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
    TreeItem item;
    item.depth = std::stoi(fields[0]);
    item.kind = fields[1];
    item.mode = static_cast<std::uint32_t>(std::stoul(fields[2]));
    item.visible = fields[3] == "1";
    item.opacity = std::stoi(fields[4]);
    item.bounds = patchy::Rect{std::stoi(fields[5]), std::stoi(fields[6]), std::stoi(fields[7]), std::stoi(fields[8])};
    item.mask = fields[9] == "1";
    item.name = line.substr(start);
    tree.items.push_back(item);
  }
  return tree;
}

// Patchy's tree in GIMP's order: top first, depth first.
void flatten_patchy_tree(const std::vector<patchy::Layer>& layers, int depth,
                         std::vector<std::pair<int, const patchy::Layer*>>& out) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    out.emplace_back(depth, &*it);
    if (it->kind() == patchy::LayerKind::Group) {
      flatten_patchy_tree(it->children(), depth + 1, out);
    }
  }
}

// Whether Patchy's mode is what PROP_MODE `mode` maps to: the linear-light modes open in
// their plain form when the layer blends in another space (docs/xcf.md).
[[nodiscard]] bool mode_matches(std::uint32_t mode, bool group, BlendMode actual) {
  if (group && mode == 61) {
    return actual == BlendMode::PassThrough;
  }
  const auto& map = gimp_mode_map();
  const auto found = map.find(mode);
  const auto expected = found == map.end() ? BlendMode::Normal : found->second;
  if (actual == expected) {
    return true;
  }
  const std::map<BlendMode, BlendMode> plain{{BlendMode::LinearMultiply, BlendMode::Multiply},
                                             {BlendMode::LinearAddition, BlendMode::LinearDodge},
                                             {BlendMode::LinearSubtract, BlendMode::Subtract},
                                             {BlendMode::LinearDivide, BlendMode::Divide},
                                             {BlendMode::Multiply, BlendMode::LinearMultiply},
                                             {BlendMode::LinearDodge, BlendMode::LinearAddition},
                                             {BlendMode::Subtract, BlendMode::LinearSubtract},
                                             {BlendMode::Divide, BlendMode::LinearDivide}};
  const auto other = plain.find(expected);
  return other != plain.end() && other->second == actual;
}

// Compares one document with GIMP's tree; returns the differences (empty when they match).
[[nodiscard]] std::vector<std::string> compare_tree(const Document& document, const GimpTree& tree,
                                                    const std::vector<std::string>& notices) {
  std::vector<std::string> problems;
  if (document.width() != tree.width || document.height() != tree.height) {
    problems.push_back("canvas " + std::to_string(document.width()) + "x" + std::to_string(document.height()));
  }
  std::vector<std::pair<int, const patchy::Layer*>> mine;
  flatten_patchy_tree(document.layers(), 0, mine);
  const bool floating = std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("floating selection opened") != std::string::npos;
  });
  const bool effects_applied = std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("layer effects Patchy can reproduce") != std::string::npos;
  });
  std::size_t at = 0;
  bool skipped_floating = false;
  for (const auto& item : tree.items) {
    if (at < mine.size() && floating && !skipped_floating && mine[at].second->name() != item.name) {
      ++at;  // the floating selection, which GIMP's list leaves out
      skipped_floating = true;
    }
    if (at >= mine.size()) {
      problems.push_back("missing '" + item.name + "'");
      break;
    }
    const auto [depth, layer] = mine[at++];
    const std::string where = "'" + item.name + "'";
    if (layer->name() != item.name) {
      problems.push_back(where + " opened as '" + layer->name() + "'");
      break;  // the rest would only repeat the shift
    }
    const bool group = item.kind == "group";
    if (depth != item.depth || group != (layer->kind() == patchy::LayerKind::Group)) {
      problems.push_back(where + " depth or group differs");
      break;
    }
    if (layer->visible() != item.visible) {
      problems.push_back(where + " visibility");
    }
    if (std::abs(static_cast<int>(std::lround(layer->opacity() * 100.0F)) - item.opacity) > 1) {
      problems.push_back(where + " opacity " + std::to_string(layer->opacity()));
    }
    if (item.mode != 58 && item.mode != 60 && !mode_matches(item.mode, group, layer->blend_mode())) {
      problems.push_back(where + " mode " + std::to_string(item.mode) + " opened as " +
                         std::to_string(static_cast<int>(layer->blend_mode())));
    }
    if (item.mask && !layer->mask().has_value()) {
      problems.push_back(where + " lost its mask");
    }
    // A GIMP effect that grows the layer (Gaussian Blur with clip-extent off) grows Patchy's
    // pixels too (docs/xcf.md, "Layer effects").
    const auto& lb = layer->bounds();
    const bool grown = effects_applied && lb.x <= item.bounds.x && lb.y <= item.bounds.y &&
                       lb.x + lb.width >= item.bounds.x + item.bounds.width &&
                       lb.y + lb.height >= item.bounds.y + item.bounds.height;
    if (!group && layer->bounds() != item.bounds && !grown) {
      const auto& b = layer->bounds();
      problems.push_back(where + " bounds " + std::to_string(b.x) + "," + std::to_string(b.y) + " " +
                         std::to_string(b.width) + "x" + std::to_string(b.height));
    }
  }
  const std::size_t expected_count = at + (floating && !skipped_floating ? 1U : 0U);
  if (problems.empty() && expected_count != mine.size()) {
    problems.push_back(std::to_string(mine.size() - at) + " extra layers");
  }
  return problems;
}

// Every local .xcf with a GIMP tree beside it (gimp-trees/, from dump_gimp_trees.py) opens with
// GIMP's layer tree: the same layers, names, nesting, order, visibility, opacity, modes, masks
// and bounds. Files with a visible Erase or Split layer are listed and skipped: the import
// emulates those modes with groups (docs/xcf.md, "Blend modes").
void xcf_layer_trees_match_gimp_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures";
  std::vector<std::filesystem::path> dirs;
  for (const char* name : {"xcf", "xcf-corpus", "xcf-indexed", "xcf-metadata", "xcf-issue-487"}) {
    if (std::filesystem::exists(root / name / "gimp-trees")) {
      dirs.push_back(root / name);
    }
  }
  patchy::test::skip_unless_exists(dirs.empty() ? root / "xcf" / "gimp-trees" : dirs.front() / "gimp-trees");
  patchy::set_deep_editing_override(true);
  std::size_t compared = 0;
  std::size_t skipped = 0;
  std::vector<std::string> failures;
  for (const auto& dir : dirs) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::string lower = entry.path().filename().string();
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (!lower.ends_with(".xcf") && !lower.ends_with(".xcf.gz") && !lower.ends_with(".xcf.bz2") &&
          !lower.ends_with(".xcf.xz") && !lower.ends_with(".xcfgz") && !lower.ends_with(".xcfbz2") &&
          !lower.ends_with(".xcfxz")) {
        continue;
      }
      // The tree's name is the path under `dir` with "/" as "__" (dump_gimp_trees.py).
      const auto relative = entry.path().lexically_relative(dir).generic_string();
      std::string tree_name;
      for (const char c : relative) {
        tree_name += c == '/' ? std::string("__") : std::string(1, c);
      }
      const auto tree_path = dir / "gimp-trees" / (tree_name + ".tree.txt");
      if (!std::filesystem::exists(tree_path)) {
        continue;  // GIMP refuses it, or it was added after the dump
      }
      const auto& source = entry.path();
      const auto tree = read_gimp_tree(tree_path);
      const auto label = dir.filename().string() + "/" + relative;
      if (!tree.has_value()) {
        failures.push_back(label + ": unreadable tree");
        continue;
      }
      if (std::any_of(tree->items.begin(), tree->items.end(), [](const TreeItem& item) {
            return item.visible && (item.mode == 58 || item.mode == 60);
          })) {
        ++skipped;
        std::cout << "  skipped (Erase or Split groups): " << label << "\n";
        continue;
      }
      std::vector<std::string> notices;
      try {
        const auto document = patchy::xcf::DocumentIo::read_file(source, &notices);
        ++compared;
        for (const auto& problem : compare_tree(document, *tree, notices)) {
          failures.push_back(label + ": " + problem);
        }
      } catch (const std::exception& error) {
        failures.push_back(label + ": " + error.what());
      }
    }
  }
  patchy::set_deep_editing_override(std::nullopt);
  for (const auto& failure : failures) {
    std::cout << "  FAIL " << failure << "\n";
  }
  std::cout << "  trees: " << compared << " compared, " << skipped << " skipped, " << failures.size()
            << " differences\n";
  CHECK(compared > 0);
  CHECK(failures.empty());
}

// --- GIMP's metadata tag list (gimp-image-metadata, issue 450) ------------------------------

namespace md = patchy::metadata;

[[nodiscard]] std::vector<std::uint8_t> text_bytes(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] Document read_with_parasites(const std::vector<builder::Parasite>& parasites) {
  builder::Image image;
  image.version = 11;
  image.width = 8;
  image.height = 8;
  image.properties.push_back(builder::prop_parasites(parasites));
  image.layers.push_back(pixel_layer("Layer", builder::solid(8, 8, {9, 9, 9, 255})));
  return patchy::xcf::DocumentIo::read(builder::write(image));
}

[[nodiscard]] md::XmpPacket document_xmp(const Document& document) {
  const auto& xmp = document.file_info().xmp;
  if (!xmp) {
    return {};
  }
  return md::parse_xmp(*xmp).value_or(md::XmpPacket{});
}

[[nodiscard]] const md::XmpValue* xmp_field(const md::XmpValue& value, std::string_view ns, std::string_view name) {
  for (const auto& field : value.fields) {
    if (field.ns == ns && field.name == name) {
      return &field.value;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string alt_text(const md::XmpValue* value) {
  return value != nullptr && value->kind == md::XmpValue::Kind::Alt && !value->items.empty() ? value->items[0].text
                                                                                              : std::string("?");
}

// GIMP's exiv2 text forms: lang-alt, repeated tags as array items, type="Bag" containers
// (empty ones dropped), struct and struct-array paths, base64 values, the stray top-level
// CreatorContactInfo fields, Exif rationals and GPS, IIM dates and times.
void xcf_gimp_image_metadata_tag_list_fills_file_info() {
  const std::string tags = R"(<?xml version='1.0' encoding='UTF-8'?>
<metadata>
  <tag name="Exif.Image.Artist">Ann</tag>
  <tag name="Exif.Image.ImageWidth">50</tag>
  <tag name="Exif.Image.XResolution">300/1</tag>
  <tag name="Exif.Photo.ExifVersion">48 50 51 48</tag>
  <tag name="Exif.Photo.UserComment">charset=Ascii hello</tag>
  <tag name="Exif.GPSInfo.GPSLatitudeRef">S</tag>
  <tag name="Exif.GPSInfo.GPSLatitude">79/1 23/1 11322/1000</tag>
  <tag name="Exif.Thumbnail.Compression">6</tag>
  <namespace prefix="dc" url="http://purl.org/dc/elements/1.1/"></namespace>
  <tag name="Xmp.dc.title">lang=&quot;x-default&quot; Title</tag>
  <tag name="Xmp.dc.subject">one</tag>
  <tag name="Xmp.dc.subject">two</tag>
  <tag name="Xmp.dc.creator">Ann</tag>
  <tag name="Xmp.photoshop.Headline" encoding="base64">SGVhZA==</tag>
  <tag name="Xmp.iptc.CiAdrCity">Stray</tag>
  <tag name="Xmp.iptc.CreatorContactInfo/Iptc4xmpCore:CiAdrCity">Brampton</tag>
  <tag name="Xmp.iptcExt.LocationShown">type=&quot;Bag&quot;</tag>
  <tag name="Xmp.iptcExt.ArtworkOrObject">type=&quot;Bag&quot;</tag>
  <tag name="Xmp.iptcExt.ArtworkOrObject[1]/iptcExt:AOTitle">lang=&quot;x-default&quot; Art</tag>
  <tag name="Xmp.xmpMM.History">type=&quot;Seq&quot;</tag>
  <tag name="Xmp.xmpMM.History[1]/stEvt:action">saved</tag>
  <tag name="Xmp.DICOM.PatientName">patient</tag>
  <tag name="Xmp.nowhere.Foo">dropped</tag>
  <tag name="Xmp.dc.rights[99999]">dropped</tag>
  <namespace prefix="DICOM" url="http://ns.adobe.com/DICOM/"/>
  <tag name="Iptc.Application2.City">city</tag>
  <tag name="Iptc.Application2.Keywords">k1</tag>
  <tag name="Iptc.Application2.Keywords">k2</tag>
  <tag name="Iptc.Application2.DateCreated">2026-10-26</tag>
  <tag name="Iptc.Application2.TimeCreated">12:56:05+03:00</tag>
</metadata>
)";
  auto with_nul = text_bytes(tags);
  with_nul.push_back(0);
  const auto document = read_with_parasites({{"gimp-image-metadata", with_nul}});
  const auto& info = document.file_info();
  CHECK(info.xmp && info.exif && info.iptc);

  const auto xmp = document_xmp(document);
  CHECK(xmp.text(md::xmp_ns::kDc, "title") == "Title");
  CHECK(xmp.items(md::xmp_ns::kDc, "subject") == std::vector<std::string>({"one", "two"}));
  const auto* creator = xmp.find(md::xmp_ns::kDc, "creator");
  CHECK(creator != nullptr && creator->value.kind == md::XmpValue::Kind::Seq && creator->value.items.size() == 1);
  CHECK(xmp.text(md::xmp_ns::kPhotoshop, "Headline") == "Head");
  CHECK(xmp.find(md::xmp_ns::kIptcCore, "CiAdrCity") == nullptr);
  CHECK(xmp.struct_field(md::xmp_ns::kIptcCore, "CreatorContactInfo", md::xmp_ns::kIptcCore, "CiAdrCity") ==
        "Brampton");
  CHECK(xmp.find(md::xmp_ns::kIptcExt, "LocationShown") == nullptr);
  const auto* artwork = xmp.find(md::xmp_ns::kIptcExt, "ArtworkOrObject");
  CHECK(artwork != nullptr && artwork->value.kind == md::XmpValue::Kind::Bag && artwork->value.items.size() == 1);
  if (artwork != nullptr && artwork->value.items.size() == 1) {
    CHECK(alt_text(xmp_field(artwork->value.items[0], md::xmp_ns::kIptcExt, "AOTitle")) == "Art");
  }
  const auto* history = xmp.find(md::xmp_ns::kXmpMM, "History");
  CHECK(history != nullptr && history->value.kind == md::XmpValue::Kind::Seq && history->value.items.size() == 1);
  CHECK(xmp.text("http://ns.adobe.com/DICOM/", "PatientName") == "patient");
  CHECK(xmp.find(md::xmp_ns::kDc, "rights") == nullptr);
  const std::string packet(info.xmp->begin(), info.xmp->end());
  CHECK(packet.find("DICOM:PatientName") != std::string::npos);
  CHECK(packet.find("dropped") == std::string::npos);

  const auto fields = md::read_fields(info);
  CHECK(fields.title == "Title" && fields.authors == std::vector<std::string>({"Ann"}));
  CHECK(fields.keywords == std::vector<std::string>({"one", "two"}));
  CHECK(fields.contact_city == "Brampton");
  CHECK(fields.city == "city");  // from the IIM: the XMP has no photoshop:City

  const auto exif = md::parse_tiff(*info.exif);
  CHECK(exif.has_value());
  if (exif) {
    CHECK(exif->big_endian);
    const auto* ifd0 = exif->find(md::IfdKind::Ifd0);
    CHECK(ifd0 != nullptr && ifd0->find(256) == nullptr);
    if (ifd0 != nullptr) {
      const auto* artist = ifd0->find(md::exif_tag::kArtist);
      CHECK(artist != nullptr && exif->string_value(*artist) == "Ann");
      const auto* resolution = ifd0->find(md::exif_tag::kXResolution);
      const std::pair<std::int64_t, std::int64_t> per_inch{300, 1};
      CHECK(resolution != nullptr && exif->rational_value(*resolution) == per_inch);
    }
    const auto* photo = exif->find(md::IfdKind::Exif);
    CHECK(photo != nullptr);
    if (photo != nullptr) {
      const auto* version = photo->find(md::exif_tag::kExifVersion);
      CHECK(version != nullptr && version->data == text_bytes("0230"));
      const auto* comment = photo->find(md::exif_tag::kUserComment);
      CHECK(comment != nullptr && comment->data == text_bytes(std::string("ASCII\0\0\0hello", 13)));
    }
    const auto latitude = md::gps_latitude(*exif);
    CHECK(latitude.has_value() && std::abs(*latitude + (79.0 + 23.0 / 60.0 + 11.322 / 3600.0)) < 1e-9);
    CHECK(exif->find(md::IfdKind::Ifd1) == nullptr);
  }

  const auto iim = md::parse_iim(*info.iptc);
  CHECK(iim.has_value());
  if (iim) {
    CHECK(iim->value(2, md::iim::kCity) == "city");
    CHECK(iim->values(2, md::iim::kKeywords) == std::vector<std::string>({"k1", "k2"}));
    CHECK(iim->value(2, md::iim::kDateCreated) == "20261026");
    CHECK(iim->value(2, md::iim::kTimeCreated) == "125605+0300");
  }
}

// The Created date keeps the Exif offset GIMP 3 writes beside DateTimeOriginal (issue 450).
void xcf_gimp_image_metadata_created_date_keeps_time_offset() {
  const std::string tags = R"(<metadata>
  <tag name="Exif.Image.DateTime">2026:10:26 13:12:12</tag>
  <tag name="Exif.Photo.DateTimeOriginal">2026:10:26 13:12:12</tag>
  <tag name="Exif.Photo.OffsetTime">+03:00</tag>
  <tag name="Exif.Photo.OffsetTimeOriginal">+03:00</tag>
  <tag name="Xmp.xmp.CreateDate">2026-10-26T13:12:12+03:00</tag>
  <tag name="Xmp.xmp.ModifyDate">2026-10-26T13:12:12+03:00</tag>
</metadata>)";
  const auto document = read_with_parasites({{"gimp-image-metadata", text_bytes(tags)}});
  const auto fields = md::read_fields(document.file_info());
  CHECK(fields.created == "2026-10-26T13:12:12+03:00");
  CHECK(fields.modified == "2026-10-26T13:12:12+03:00");
}

// gimp-metadata (XMP) and exif-data keep precedence; the tag list only fills what they
// lack. A damaged tag list is ignored and the comment still arrives.
void xcf_gimp_image_metadata_fills_only_gaps_and_survives_damage() {
  md::XmpPacket packet;
  packet.set_lang_alt(md::xmp_ns::kDc, "title", "From packet");
  auto xmp_parasite = text_bytes("GIMP_XMP_1" + md::serialize_xmp(packet, 0));
  md::TiffStream stream;
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kArtist, "Exif block");
  auto exif_parasite = text_bytes(std::string("Exif\0\0", 6));
  const auto tiff = md::write_tiff(stream);
  exif_parasite.insert(exif_parasite.end(), tiff.begin(), tiff.end());
  const std::string tags = R"(<metadata>
  <tag name="Exif.Image.Artist">Tag list</tag>
  <tag name="Xmp.dc.title">lang=&quot;x-default&quot; Tag list</tag>
  <tag name="Xmp.photoshop.City">Filled</tag>
</metadata>)";
  const auto document = read_with_parasites(
      {{"gimp-metadata", xmp_parasite}, {"exif-data", exif_parasite}, {"gimp-image-metadata", text_bytes(tags)}});
  const auto fields = md::read_fields(document.file_info());
  CHECK(fields.title == "From packet");
  CHECK(fields.city == "Filled");
  CHECK(fields.authors == std::vector<std::string>({"Exif block"}));

  for (const auto& damaged :
       {std::string("<metadata><tag name=\"Xmp.dc.title\">lang="), std::string("not xml at all"), std::string(),
        std::string("<other><tag name=\"Xmp.dc.title\">x</tag></other>"),
        std::string("<metadata><tag name=\"Xmp.dc.title[1]/dc:x[2]\">y</tag><tag>no name</tag>"
                    "<tag name=\"Exif.Photo.ExposureTime\">1/0 x</tag><tag name=\"Exif.GPSInfo.GPSVersionID\">2 999"
                    "</tag><tag name=\"Iptc.Envelope.CharacterSet\">x</tag></metadata>")}) {
    const auto opened = read_with_parasites(
        {{"gimp-comment", text_bytes(std::string("A comment\0", 10))}, {"gimp-image-metadata", text_bytes(damaged)}});
    const auto& info = opened.file_info();
    CHECK(info.text.size() == 1 && info.text[0].value == "A comment");
    CHECK(!info.exif && !info.iptc);
  }
}

// File Info's Application (#450): a file that names no creator gets the first XMP history
// softwareAgent, else "GIMP"; an xmp:CreatorTool or EXIF Software the file has is kept, and
// the value survives a PSD save as xmp:CreatorTool.
void xcf_metadata_application_names_gimp() {
  const std::string history = R"(<metadata>
  <tag name="Xmp.xmpMM.History">type=&quot;Seq&quot;</tag>
  <tag name="Xmp.xmpMM.History[1]/stEvt:action">saved</tag>
  <tag name="Xmp.xmpMM.History[1]/stEvt:softwareAgent">GIMP 3.2.6 (Linux)</tag>
  <tag name="Xmp.xmpMM.History[2]/stEvt:action">saved</tag>
  <tag name="Xmp.xmpMM.History[2]/stEvt:softwareAgent">GIMP 3.4.0 (Windows)</tag>
</metadata>)";
  const auto from_history = read_with_parasites({{"gimp-image-metadata", text_bytes(history)}});
  CHECK(document_xmp(from_history).text(md::xmp_ns::kXmp, "CreatorTool") == "GIMP 3.2.6 (Linux)");
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(from_history));
  CHECK(document_xmp(reread).text(md::xmp_ns::kXmp, "CreatorTool") == "GIMP 3.2.6 (Linux)");

  const auto bare = read_with_parasites({{"gimp-comment", text_bytes(std::string("Created with GIMP\0", 17))}});
  CHECK(md::read_fields(bare.file_info()).creator_tool == "GIMP");
  CHECK(md::read_fields(read_with_parasites({}).file_info()).creator_tool == "GIMP");

  md::XmpPacket packet;
  packet.set_simple(md::xmp_ns::kXmp, "CreatorTool", "Other Editor 2.0");
  const auto own = read_with_parasites({{"gimp-metadata", text_bytes("GIMP_XMP_1" + md::serialize_xmp(packet, 0))},
                                        {"gimp-image-metadata", text_bytes(history)}});
  CHECK(document_xmp(own).text(md::xmp_ns::kXmp, "CreatorTool") == "Other Editor 2.0");

  md::TiffStream stream;
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kSoftware, "GIMP 2.10.38");
  auto exif_parasite = text_bytes(std::string("Exif\0\0", 6));
  const auto tiff = md::write_tiff(stream);
  exif_parasite.insert(exif_parasite.end(), tiff.begin(), tiff.end());
  const auto exif = read_with_parasites({{"exif-data", exif_parasite}});
  CHECK(md::read_fields(exif.file_info()).creator_tool == "GIMP 2.10.38");
  CHECK(!exif.file_info().xmp);
}

// philipzae's GIMP 3.2 samples from issue 450 (local-test-fixtures/xcf-metadata, README.txt
// there). Each also goes through a PSD save and read: File Info must show the same fields,
// and the PSD lands in test-artifacts/xcf-metadata for scripts/dev/photoshop-open-check.ps1.
[[nodiscard]] Document open_metadata_sample(const std::string& name) {
  const auto path = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-metadata" / (name + ".xcf");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::xcf::DocumentIo::read_file(path);
  CHECK(document.file_info().source_format == "XCF");
  const auto psd = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto out = std::filesystem::path("test-artifacts") / "xcf-metadata";
  std::filesystem::create_directories(out);
  std::ofstream(out / (name + ".psd"), std::ios::binary)
      .write(reinterpret_cast<const char*>(psd.data()), static_cast<std::streamsize>(psd.size()));
  const auto reread = patchy::psd::DocumentIo::read(psd);
  CHECK(md::read_fields(reread.file_info()) == md::read_fields(document.file_info()));
  CHECK(static_cast<bool>(reread.file_info().exif) == static_cast<bool>(document.file_info().exif));
  CHECK(static_cast<bool>(reread.file_info().iptc) == static_cast<bool>(document.file_info().iptc));
  CHECK(reread.file_info().text.size() == document.file_info().text.size());
  return document;
}

void xcf_metadata_sample_comment() {
  const auto document = open_metadata_sample("comment");
  const auto& info = document.file_info();
  CHECK(info.text.size() == 1 && info.text[0].kind == patchy::FileInfoTextKind::Comment &&
        info.text[0].value == "Created by Yousuf");
  const auto fields = md::read_fields(info);
  CHECK(fields.created.starts_with("2026-10-26T13:12:12"));  // EXIF DateTimeOriginal wins (MWG)
  CHECK(fields.date_created == "2026-10-26T13:12:12+03:00");
  CHECK(fields.creator_tool == "GIMP");  // no XMP, no EXIF Software
  CHECK(info.exif && info.iptc);
}

void xcf_metadata_sample_description() {
  const auto document = open_metadata_sample("description");
  const auto fields = md::read_fields(document.file_info());
  CHECK(fields.title == "Meta data test");
  CHECK(fields.description == "Description of what this document is");
  CHECK(fields.creator_tool == "GIMP 3.2.6 (Linux)");  // the XMP history's softwareAgent
  CHECK(fields.authors.size() == 1 && fields.authors[0].starts_with("Author's name"));
  CHECK(fields.author_title == "Author's title");
  CHECK(fields.description_writer == "Description writer, Yousuf");
  CHECK(fields.keywords == std::vector<std::string>({"keywords, 1, 2, 3, 4 5"}));
  CHECK(fields.rating == 3);
  CHECK(fields.copyright_status == md::CopyrightStatus::Copyrighted);
  CHECK(fields.copyright_notice == "(c) 2025");
  CHECK(fields.copyright_url == "https://www.github.com/");
  const auto exif = md::parse_tiff(*document.file_info().exif);
  const auto* ifd0 = exif ? exif->find(md::IfdKind::Ifd0) : nullptr;
  const auto* description = ifd0 != nullptr ? ifd0->find(md::exif_tag::kImageDescription) : nullptr;
  CHECK(description != nullptr && exif->string_value(*description) == "Description of what this document is");
  const auto iim = md::parse_iim(*document.file_info().iptc);
  CHECK(iim && iim->value(2, md::iim::kCaption) == "Description of what this document is" &&
        iim->value(2, md::iim::kObjectName) == "Meta data test");
}

void xcf_metadata_sample_iptc() {
  const auto document = open_metadata_sample("iptc");
  const auto fields = md::read_fields(document.file_info());
  CHECK(fields.headline == "headline");
  CHECK(fields.city == "city" && fields.state == "state/province" && fields.country == "country");
  CHECK(fields.country_code == "country iso-code" && fields.sublocation == "sublocation");
  CHECK(fields.credit == "credit line" && fields.source == "source" && fields.instructions == "instructions");
  CHECK(fields.transmission_reference == "job identifier" && fields.usage_terms == "usage terms");
  CHECK(fields.intellectual_genre == "intellectual genre");
  CHECK(fields.scene == std::vector<std::string>({"iptc scene code"}));
  CHECK(fields.subject_code == std::vector<std::string>({"iptc subject code"}));
  CHECK(fields.contact_address == "5 Don Mills Road" && fields.contact_city == "Brampton");
  CHECK(fields.contact_region == "Ontario" && fields.contact_postal_code == "M2J 2D5");
  CHECK(fields.contact_country == "Canada" && fields.contact_phone == "+1(416)521-4521");
  CHECK(fields.contact_email == "microsoft@github.com" && fields.contact_url == "www.github.com");
  const auto xmp = document_xmp(document);
  CHECK(xmp.text(md::xmp_ns::kPhotoshop, "Urgency") == "5");
  CHECK(xmp.find(md::xmp_ns::kIptcCore, "CiAdrCity") == nullptr);
}

void xcf_metadata_sample_iptc_extension() {
  const auto document = open_metadata_sample("iptc-extension");
  const auto xmp = document_xmp(document);
  CHECK(xmp.items(md::xmp_ns::kIptcExt, "PersonInImage") == std::vector<std::string>({"person shown"}));
  CHECK(xmp.text(md::xmp_ns::kIptcExt, "Event") == "event");
  CHECK(xmp.text(md::xmp_ns::kIptcExt, "DigitalSourceType") ==
        "http://cv.iptc.org/newscodes/digitalsourcetype/digitalCapture");
  const auto* shown = xmp.find(md::xmp_ns::kIptcExt, "LocationShown");
  CHECK(shown != nullptr && shown->value.kind == md::XmpValue::Kind::Bag && shown->value.items.size() == 2);
  if (shown != nullptr && shown->value.items.size() == 2) {
    const auto* city = xmp_field(shown->value.items[1], md::xmp_ns::kIptcExt, "City");
    CHECK(city != nullptr && city->text == "city 2");
  }
  const auto* artwork = xmp.find(md::xmp_ns::kIptcExt, "ArtworkOrObject");
  CHECK(artwork != nullptr && artwork->value.items.size() == 1);
  if (artwork != nullptr && artwork->value.items.size() == 1) {
    CHECK(alt_text(xmp_field(artwork->value.items[0], md::xmp_ns::kIptcExt, "AOTitle")) == "title 1");
  }
  const auto* licensor = xmp.find(md::xmp_ns::kPlus, "Licensor");
  CHECK(licensor != nullptr && licensor->value.items.size() == 1);
  if (licensor != nullptr && licensor->value.items.size() == 1) {
    const auto* name = xmp_field(licensor->value.items[0], md::xmp_ns::kPlus, "LicensorName");
    CHECK(name != nullptr && name->text == "licensor name");
  }
  const auto iim = md::parse_iim(*document.file_info().iptc);
  CHECK(iim && iim->value(2, md::iim::kSublocation) == "sublocation");
}

void xcf_metadata_sample_categories() {
  const auto document = open_metadata_sample("categories");
  const auto xmp = document_xmp(document);
  CHECK(xmp.text(md::xmp_ns::kPhotoshop, "Category") == "category: animal");
  CHECK(xmp.items(md::xmp_ns::kPhotoshop, "SupplementalCategories") ==
        std::vector<std::string>({"supplemental category: dog"}));
  const auto iim = md::parse_iim(*document.file_info().iptc);
  CHECK(iim && iim->value(2, md::iim::kCategory) == "category: animal" &&
        iim->value(2, 20) == "supplemental category: dog");
}

void xcf_metadata_sample_gps() {
  const auto document = open_metadata_sample("gps");
  const auto exif = md::parse_tiff(*document.file_info().exif);
  CHECK(exif.has_value());
  if (exif) {
    const auto latitude = md::gps_latitude(*exif);
    const auto longitude = md::gps_longitude(*exif);
    const auto altitude = md::gps_altitude(*exif);
    CHECK(latitude && std::abs(*latitude - (79.0 + 23.0 / 60.0 + 11.322 / 3600.0)) < 1e-9);
    CHECK(longitude && std::abs(*longitude - (43.0 + 44.0 / 60.0 + 25.091 / 3600.0)) < 1e-9);
    CHECK(altitude && std::abs(*altitude - 100.0) < 1e-9);
  }
}

void xcf_metadata_sample_dicom() {
  const auto document = open_metadata_sample("dicom");
  const auto xmp = document_xmp(document);
  constexpr std::string_view kDicom = "http://ns.adobe.com/DICOM/";
  CHECK(xmp.text(kDicom, "PatientName") == "patient");
  CHECK(xmp.text(kDicom, "StudyDescription") == "study description");
  CHECK(xmp.text(kDicom, "EquipmentManufacturer") == "equipment manufacturer");
  const std::string packet(document.file_info().xmp->begin(), document.file_info().xmp->end());
  CHECK(packet.find("DICOM:PatientName") != std::string::npos);
}

}  // namespace

std::vector<patchy::test::TestCase> xcf_tests() {
  return {
      {"blend_math_gimp_grain_modes_match_gimp_renders", blend_math_gimp_grain_modes_match_gimp_renders},
      {"blend_math_gimp_color_modes_match_gimp_renders", blend_math_gimp_color_modes_match_gimp_renders},
      {"psd_saves_gimp_blend_modes_as_normal", psd_saves_gimp_blend_modes_as_normal},
      {"xcf_reads_layers_groups_masks_and_properties", xcf_reads_layers_groups_masks_and_properties},
      {"xcf_reads_every_tile_compression_and_pointer_size", xcf_reads_every_tile_compression_and_pointer_size},
      {"xcf_reads_every_precision", xcf_reads_every_precision},
      {"xcf_reads_gray_and_indexed_images", xcf_reads_gray_and_indexed_images},
      {"xcf_indexed_samples_attach_their_colormap", xcf_indexed_samples_attach_their_colormap},
      {"xcf_maps_every_gimp_layer_mode", xcf_maps_every_gimp_layer_mode},
      {"xcf_text_parasites_parse_gimp_2_and_3_forms", xcf_text_parasites_parse_gimp_2_and_3_forms},
      {"xcf_text_layers_open_as_editable_text_over_gimps_raster", xcf_text_layers_open_as_editable_text_over_gimps_raster},
      {"xcf_floating_selection_opens_above_its_layer", xcf_floating_selection_opens_above_its_layer},
      {"xcf_reads_channels_selection_paths_guides_and_metadata", xcf_reads_channels_selection_paths_guides_and_metadata},
      {"xcf_gimp_image_metadata_tag_list_fills_file_info", xcf_gimp_image_metadata_tag_list_fills_file_info},
      {"xcf_gimp_image_metadata_created_date_keeps_time_offset", xcf_gimp_image_metadata_created_date_keeps_time_offset},
      {"xcf_gimp_image_metadata_fills_only_gaps_and_survives_damage", xcf_gimp_image_metadata_fills_only_gaps_and_survives_damage},
      {"xcf_metadata_application_names_gimp", xcf_metadata_application_names_gimp},
      {"xcf_metadata_sample_comment", xcf_metadata_sample_comment},
      {"xcf_metadata_sample_description", xcf_metadata_sample_description},
      {"xcf_metadata_sample_iptc", xcf_metadata_sample_iptc},
      {"xcf_metadata_sample_iptc_extension", xcf_metadata_sample_iptc_extension},
      {"xcf_metadata_sample_categories", xcf_metadata_sample_categories},
      {"xcf_metadata_sample_gps", xcf_metadata_sample_gps},
      {"xcf_metadata_sample_dicom", xcf_metadata_sample_dicom},
      {"xcf_layer_effects_bake_or_become_styles", xcf_layer_effects_bake_or_become_styles},
      {"xcf_effect_fixtures_match_gimp_if_available", xcf_effect_fixtures_match_gimp_if_available},
      {"xcf_unwraps_gzip_bzip2_and_xz", xcf_unwraps_gzip_bzip2_and_xz},
      {"xcf_hostile_input_never_crashes", xcf_hostile_input_never_crashes},
      {"xcf_registry_sniff_and_open_by_content", xcf_registry_sniff_and_open_by_content},
      {"xcf_unicode_path_opens", xcf_unicode_path_opens},
      {"xcf_committed_gimp_files_match_gimp_renders", xcf_committed_gimp_files_match_gimp_renders},
      {"xcf_gimp_fixtures_match_gimp_renders_if_available", xcf_gimp_fixtures_match_gimp_renders_if_available},
      {"xcf_corpus_opens_if_available", xcf_corpus_opens_if_available},
      {"xcf_active_erase_layer_still_opens", xcf_active_erase_layer_still_opens},
      {"xcf_bottom_erase_layer_draws_as_normal", xcf_bottom_erase_layer_draws_as_normal},
      {"xcf_issue_487_samples_open_like_gimp", xcf_issue_487_samples_open_like_gimp},
      {"xcf_layer_trees_match_gimp_if_available", xcf_layer_trees_match_gimp_if_available},
  };
}
