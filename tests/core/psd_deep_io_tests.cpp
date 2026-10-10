// 16/32-bit PSD/PSB reading and writing at the file's depth (docs/high-bit-depth.md,
// Phase 2): the reader keeps the samples, the writer emits them back, and the 8-bit
// reading of the same file is exactly the deep reading narrowed.

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_depth.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "psd_test_support.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace patchy;

bool same_bytes(const PixelBuffer& a, const PixelBuffer& b) {
  return a.format() == b.format() && a.width() == b.width() && a.height() == b.height() &&
         std::memcmp(a.data().data(), b.data().data(), a.byte_size()) == 0;
}

std::vector<const Layer*> flat_layers(const Document& document) {
  std::vector<const Layer*> out;
  const std::function<void(const std::vector<Layer>&)> walk = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      out.push_back(&layer);
      walk(layer.children());
    }
  };
  walk(document.layers());
  return out;
}

// A 16 or 32-bit document whose samples are not representable at 8 bits: a fine
// gradient layer with a gradient mask over a background, plus a saved channel.
Document make_deep_document(BitDepth depth) {
  Document document(64, 32, PixelFormat::rgb8());
  PixelBuffer background(64, 32, PixelFormat::rgba8());
  PixelBuffer top(64, 32, PixelFormat::rgba8());
  document.add_pixel_layer("Background", background);
  document.add_pixel_layer("Top", top);
  convert_document_depth(document, depth);
  const auto domain = deep_domain_for(depth);
  std::vector<float> row(64 * 4);
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      auto* p = &row[static_cast<std::size_t>(x) * 4U];
      p[0] = 3.3F + x * 3.9F;
      p[1] = 250.0F - y * 7.1F;
      p[2] = 17.25F + (x + y) * 1.3F;
      p[3] = 255.0F;
    }
    store_rgba_row(document.layers()[0].pixels(), y, 0, 64, domain, row);
    for (int x = 0; x < 64; ++x) {
      row[static_cast<std::size_t>(x) * 4U + 3U] = 40.5F + x * 2.7F;
    }
    store_rgba_row(document.layers()[1].pixels(), y, 0, 64, domain, row);
  }
  LayerMask mask;
  mask.bounds = Rect::from_size(64, 32);
  mask.pixels = PixelBuffer(64, 32, with_bit_depth(PixelFormat::gray8(), depth));
  std::vector<float> coverage(64);
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      coverage[static_cast<std::size_t>(x)] = x * 3.99F + 0.37F;
    }
    store_coverage_row(mask.pixels, y, 0, 64, coverage);
  }
  document.layers()[1].set_mask(mask);
  PixelBuffer saved(64, 32, with_bit_depth(PixelFormat::gray8(), depth));
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      coverage[static_cast<std::size_t>(x)] = 255.0F - x * 1.11F - y;
    }
    store_coverage_row(saved, y, 0, 64, coverage);
  }
  document.add_channel(DocumentChannel(document.allocate_channel_id(), "Alpha 1", DocumentChannelKind::Alpha, saved));
  return document;
}

std::uint16_t header_depth(const std::vector<std::uint8_t>& bytes) {
  return static_cast<std::uint16_t>((bytes[22] << 8) | bytes[23]);
}

void deep_round_trip(BitDepth depth, bool large_document) {
  const auto original = make_deep_document(depth);
  CHECK(document_depth_problems(original).empty());
  const auto bytes = psd::DocumentIo::write_layered_rgb8(original, psd::WriteOptions{large_document});
  CHECK(header_depth(bytes) == (depth == BitDepth::Float32 ? 32 : 16));
  psd::ReadOptions options;
  options.keep_bit_depth = true;
  const auto read = psd::DocumentIo::read(bytes, options);
  CHECK(document_bit_depth(read) == depth);
  CHECK(document_depth_problems(read).empty());
  CHECK(read.layers().size() == 2);
  for (std::size_t i = 0; i < 2; ++i) {
    CHECK(same_bytes(read.layers()[i].pixels(), original.layers()[i].pixels()));
  }
  CHECK(read.layers()[1].mask().has_value());
  CHECK(same_bytes(read.layers()[1].mask()->pixels, original.layers()[1].mask()->pixels));
  CHECK(read.channels().size() == 1);
  CHECK(same_bytes(read.channels()[0].pixels(), original.channels()[0].pixels()));

  // Read the same bytes the old way: 8 bits, and exactly the deep pixels narrowed.
  psd::ReadOptions shallow_options;
  shallow_options.keep_bit_depth = false;
  const auto shallow = psd::DocumentIo::read(bytes, shallow_options);
  CHECK(document_bit_depth(shallow) == BitDepth::UInt8);
  for (std::size_t i = 0; i < 2; ++i) {
    CHECK(same_bytes(shallow.layers()[i].pixels(),
                     convert_pixel_buffer_depth(read.layers()[i].pixels(), BitDepth::UInt8, SampleKind::Color)));
  }
}

// The stored composite is the deep compositor's flatten at the document's depth, not
// an 8-bit flatten widened: other readers (and Patchy's flat reading) see every bit.
void deep_composite_keeps_depth(BitDepth depth) {
  const auto original = make_deep_document(depth);
  const auto expected = Compositor{}.flatten_rgba_deep(original);
  const auto domain = deep_domain_for(depth);
  for (const bool flat : {false, true}) {
    const auto bytes = flat ? psd::DocumentIo::write_flat_rgb8(original) : psd::DocumentIo::write_layered_rgb8(original);
    psd::ReadOptions options;
    options.keep_bit_depth = true;
    options.prefer_flat_composite = true;
    const auto read = psd::DocumentIo::read(bytes, options);
    CHECK(document_bit_depth(read) == depth);
    CHECK(!read.layers().empty());
    const auto& pixels = read.layers()[0].pixels();
    CHECK(pixels.width() == expected.width() && pixels.height() == expected.height());
    std::vector<float> mine(static_cast<std::size_t>(expected.width()) * 4U);
    std::vector<float> theirs(mine.size());
    double worst = 0.0;
    for (std::int32_t y = 0; y < expected.height(); ++y) {
      load_rgba_row(pixels, y, 0, expected.width(), domain, mine);
      load_rgba_row(expected, y, 0, expected.width(), domain, theirs);
      for (std::size_t i = 0; i < mine.size(); ++i) {
        if (i % 4U != 3U) {
          worst = std::max(worst, static_cast<double>(std::abs(mine[i] - theirs[i])));
        }
      }
    }
    // Exact for floats; within rounding for 16 bits (one 16-bit step is 1/257).
    CHECK(worst <= (depth == BitDepth::Float32 ? 1e-4 : 0.5 / 257.0 + 1e-4));
  }
}

void psd_deep_composite_is_written_at_depth() {
  deep_composite_keeps_depth(BitDepth::UInt16);
  deep_composite_keeps_depth(BitDepth::Float32);
}

void psd_deep_16_bit_document_round_trips_exactly() {
  deep_round_trip(BitDepth::UInt16, false);
  deep_round_trip(BitDepth::UInt16, true);
}

void psd_deep_32_bit_document_round_trips_exactly() {
  deep_round_trip(BitDepth::Float32, false);
  deep_round_trip(BitDepth::Float32, true);
}

void psd_deep_gate_off_reads_8_bit_as_before() {
  const auto bytes = psd::DocumentIo::write_layered_rgb8(make_deep_document(BitDepth::UInt16));
  set_deep_editing_override(false);
  const auto document = psd::DocumentIo::read(bytes);
  set_deep_editing_override(std::nullopt);
  CHECK(document_bit_depth(document) == BitDepth::UInt8);
  CHECK(document.layers()[0].pixels().format().bit_depth == BitDepth::UInt8);
  set_deep_editing_override(true);
  const auto deep = psd::DocumentIo::read(bytes);
  set_deep_editing_override(std::nullopt);
  CHECK(document_bit_depth(deep) == BitDepth::UInt16);
}

// Raw native planes keep this independent of Patchy's RGB writer. A layered file
// and its composite carry the same fine ramp, alpha and source profile.
std::vector<std::uint8_t> color_mode_ramp16(std::uint16_t mode, std::span<const std::uint8_t> profile) {
  using namespace patchy::test;
  const std::uint16_t colors = mode == 4 ? 4 : mode == 1 ? 1 : 3;
  psd::BigEndianWriter resources;
  if (!profile.empty()) {
    write_ascii4(resources, "8BIM");
    resources.write_u16(1039);
    resources.write_u16(0);  // empty Pascal name, padded to even
    resources.write_u32(static_cast<std::uint32_t>(profile.size()));
    resources.write_bytes(profile);
    if (profile.size() % 2U != 0U) resources.write_u8(0);
  }
  std::vector<std::vector<std::uint8_t>> planes;
  for (std::uint16_t c = 0; c <= colors; ++c) {
    psd::BigEndianWriter plane;
    for (std::uint16_t x = 0; x < 64; ++x) {
      plane.write_u16(c == colors ? static_cast<std::uint16_t>(40000U + x)
                     : c == 0 ? static_cast<std::uint16_t>(30000U + x)
                     : c == 3 ? 65535 : 32896);
    }
    planes.push_back(plane.bytes());
  }
  psd::BigEndianWriter info;
  info.write_u16(0xFFFF);  // one layer, merged transparency present
  info.write_u32(0); info.write_u32(0); info.write_u32(1); info.write_u32(64);
  info.write_u16(static_cast<std::uint16_t>(colors + 1U));
  for (std::uint16_t c = 0; c <= colors; ++c) {
    info.write_u16(c == colors ? 0xFFFF : c);
    info.write_u32(130);  // compression marker + 64 full-range u16 samples
  }
  write_ascii4(info, "8BIM"); write_ascii4(info, "norm");
  info.write_u8(255); info.write_u8(0); info.write_u8(8); info.write_u8(0);
  info.write_u32(12);  // empty mask, blending ranges and name
  info.write_u32(0); info.write_u32(0); info.write_u32(0);
  for (const auto& plane : planes) {
    info.write_u16(0);
    info.write_bytes(plane);
  }
  psd::BigEndianWriter layer_mask;
  layer_mask.write_u32(0); layer_mask.write_u32(0);
  write_ascii4(layer_mask, "8BIM"); write_ascii4(layer_mask, "Lr16");
  layer_mask.write_u32(static_cast<std::uint32_t>(info.bytes().size()));
  layer_mask.write_bytes(info.bytes());
  while (layer_mask.bytes().size() % 4U != 0U) layer_mask.write_u8(0);
  psd::BigEndianWriter file;
  psd::write_header(file, psd::Header{false, static_cast<std::uint16_t>(colors + 1U), 1, 64, 16, mode});
  file.write_u32(0);
  file.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  file.write_bytes(resources.bytes());
  file.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  file.write_bytes(layer_mask.bytes());
  file.write_u16(0);
  for (const auto& plane : planes) file.write_bytes(plane);
  return file.bytes();
}

void psd_deep_color_conversions_keep_sub_byte_samples_and_alpha() {
  const auto cmyk_source = psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-cmyk-style-colors.psd"));
  const auto cmyk_profile = patchy::test::test_image_resource_payload(
      cmyk_source.metadata().raw_psd_image_resources, 1039);
  CHECK(cmyk_profile.has_value());
  const auto gray_profile = patchy::test::test_linear_gray_icc_profile();
  for (const auto mode : std::array<std::uint16_t, 4>{1, 4, 7, 9}) {
    for (const bool with_profile : {false, true}) {
      if (with_profile && mode != 1 && mode != 4) continue;
      const auto profile = !with_profile ? std::span<const std::uint8_t>{}
                           : mode == 1 ? std::span<const std::uint8_t>(gray_profile)
                                       : std::span<const std::uint8_t>(*cmyk_profile);
      const auto bytes = color_mode_ramp16(mode, profile);
      for (const bool flat : {false, true}) {
        psd::ReadOptions options;
        options.keep_bit_depth = true;
        options.prefer_flat_composite = flat;
        const auto document = psd::DocumentIo::read(bytes, options);
        CHECK(document_bit_depth(document) == BitDepth::UInt16);
        CHECK(document.layers().size() == 1);
        const auto& layer = document.layers()[0];
        const auto& pixels = layer.pixels();
        CHECK(pixels.format().bit_depth == BitDepth::UInt16);
        std::set<int> values;
        for (int x = 0; x < 64; ++x) {
          const auto pixel = load_pixel(pixels.format(), pixels.pixel(x, 0));
          const auto red = static_cast<int>(std::lround(pixel[0] * 257.0F));
          values.insert(red);
          if ((mode == 1 || mode == 4 || mode == 7) && !with_profile) CHECK(red == 30000 + x);
          if (mode == 1 && with_profile) {
            const auto expected = static_cast<int>(std::lround(srgb_encode((30000.0 + x) / 65535.0) * 65535.0));
            CHECK(std::abs(red - expected) <= 8);
          }
          const auto alpha = flat ? coverage_at(layer.mask()->pixels, x, 0) * 65535.0F : pixel[3] * 257.0F;
          CHECK(std::abs(alpha - (40000.0F + x)) < 0.1F);
        }
        // All 64 input samples round to the SAME byte. A deep conversion must
        // preserve a gradient instead of producing one repeated output value.
        CHECK(values.size() > 24U);
        const auto saved = psd::DocumentIo::write_layered_rgb8(document);
        options.prefer_flat_composite = false;
        const auto reopened = psd::DocumentIo::read(saved, options);
        const auto& saved_pixels = reopened.layers()[0].pixels();
        for (int x = 0; x < 64; ++x) {
          CHECK(load_pixel(saved_pixels.format(), saved_pixels.pixel(x, 0)) ==
                load_pixel(pixels.format(), pixels.pixel(x, 0)));
        }
      }
    }
  }
}

// Photoshop-made files from scripts/dev/deep/make_deep_fixtures.py: every scene and
// depth the corpus has. The deep reading narrows to exactly the 8-bit reading (which
// the existing suites pin against Photoshop), keeps samples 8 bits cannot hold, and
// survives Patchy's own deep write.
void psd_deep_photoshop_corpus_reads_and_round_trips_if_available() {
  const auto root = patchy::test::source_root_path() / "local-test-fixtures" / "deep";
  if (!std::filesystem::exists(root / "manifest.json")) {
    std::cout << "[SKIP] deep fixture corpus missing: " << root.string() << '\n';
    return;
  }
  int checked = 0;
  for (const auto& scene : std::filesystem::directory_iterator(root)) {
    if (!scene.is_directory()) {
      continue;
    }
    for (const auto* depth_name : {"16", "32"}) {
      const auto path = scene.path() / depth_name / (scene.path().filename().string() + "-" + depth_name + ".psd");
      if (!std::filesystem::exists(path)) {
        continue;
      }
      const auto depth = std::string(depth_name) == "32" ? BitDepth::Float32 : BitDepth::UInt16;
      psd::ReadOptions deep_options;
      deep_options.keep_bit_depth = true;
      const auto deep = psd::DocumentIo::read_file(path, deep_options);
      psd::ReadOptions shallow_options;
      shallow_options.keep_bit_depth = false;
      const auto shallow = psd::DocumentIo::read_file(path, shallow_options);
      CHECK(document_bit_depth(deep) == depth);
      if (!document_depth_problems(deep).empty()) {
        std::cerr << path.string() << ": " << document_depth_problems(deep).front() << '\n';
        CHECK(false);
      }
      const auto deep_layers = flat_layers(deep);
      const auto shallow_layers = flat_layers(shallow);
      CHECK(deep_layers.size() == shallow_layers.size());
      for (std::size_t i = 0; i < deep_layers.size(); ++i) {
        const auto& deep_pixels = deep_layers[i]->pixels();
        const auto& shallow_pixels = shallow_layers[i]->pixels();
        if (deep_pixels.empty() || shallow_pixels.format().bit_depth != BitDepth::UInt8) {
          continue;
        }
        const auto narrowed = convert_pixel_buffer_depth(deep_pixels, BitDepth::UInt8, SampleKind::Color);
        if (!same_bytes(narrowed, shallow_pixels)) {
          std::cerr << path.string() << ": layer '" << deep_layers[i]->name() << "' narrows differently\n";
          CHECK(false);
        }
      }
      const auto bytes = psd::DocumentIo::write_layered_rgb8(deep);
      CHECK(header_depth(bytes) == (depth == BitDepth::Float32 ? 32 : 16));
      const auto reread = psd::DocumentIo::read(bytes, deep_options);
      const auto reread_layers = flat_layers(reread);
      CHECK(reread_layers.size() == deep_layers.size());
      for (std::size_t i = 0; i < deep_layers.size() && i < reread_layers.size(); ++i) {
        if (deep_layers[i]->kind() == LayerKind::Pixel && !deep_layers[i]->pixels().empty() &&
            deep_layers[i]->vector_shape() == nullptr &&
            !same_bytes(reread_layers[i]->pixels(), deep_layers[i]->pixels())) {
          std::cerr << path.string() << ": layer '" << deep_layers[i]->name() << "' changed on resave\n";
          CHECK(false);
        }
      }
      ++checked;
    }
  }
  std::cout << "  deep corpus documents checked: " << checked << '\n';
  CHECK(checked > 0);
}

}  // namespace

// Photoshop 2026 refuses to open a 32-bit file with a layer in Color Burn, Linear Burn,
// Screen, Color Dodge, Overlay, the Lights, Hard Mix or Exclusion, and its conversion
// to 32 bits sets them to Normal (probed over COM, October 2026). Patchy converts the
// same way, and the writer never writes them at 32 bits.
void psd_deep_32_bit_blend_modes_follow_photoshop() {
  for (int index = static_cast<int>(BlendMode::Normal); index <= static_cast<int>(BlendMode::Dissolve); ++index) {
    const auto mode = static_cast<BlendMode>(index);
    CHECK(blend_mode_supported_at_depth(mode, BitDepth::UInt8));
    CHECK(blend_mode_supported_at_depth(mode, BitDepth::UInt16));
  }
  for (const auto mode : {BlendMode::Screen, BlendMode::Overlay, BlendMode::ColorBurn, BlendMode::LinearBurn,
                          BlendMode::ColorDodge, BlendMode::SoftLight, BlendMode::HardLight, BlendMode::VividLight,
                          BlendMode::LinearLight, BlendMode::PinLight, BlendMode::HardMix, BlendMode::Exclusion}) {
    CHECK(!blend_mode_supported_at_depth(mode, BitDepth::Float32));
  }
  for (const auto mode : {BlendMode::Normal, BlendMode::Dissolve, BlendMode::Darken, BlendMode::Multiply,
                          BlendMode::DarkerColor, BlendMode::Lighten, BlendMode::LinearDodge, BlendMode::LighterColor,
                          BlendMode::Difference, BlendMode::Subtract, BlendMode::Divide, BlendMode::Hue,
                          BlendMode::Saturation, BlendMode::Color, BlendMode::Luminosity, BlendMode::PassThrough}) {
    CHECK(blend_mode_supported_at_depth(mode, BitDepth::Float32));
  }

  auto document = make_deep_document(BitDepth::UInt16);
  document.layers()[0].set_blend_mode(BlendMode::Multiply);
  document.layers()[1].set_blend_mode(BlendMode::Overlay);
  auto sixteen = document;
  convert_document_depth(sixteen, BitDepth::UInt8);
  CHECK(sixteen.layers()[1].blend_mode() == BlendMode::Overlay);
  convert_document_depth(document, BitDepth::Float32);
  CHECK(document.layers()[0].blend_mode() == BlendMode::Multiply);
  CHECK(document.layers()[1].blend_mode() == BlendMode::Normal);

  // A mode set after the conversion (an older layer pasted in, a hand edit) is written as
  // Normal so Photoshop still opens the file; 16 bits keep it.
  document.layers()[1].set_blend_mode(BlendMode::Screen);
  psd::ReadOptions options;
  options.keep_bit_depth = true;
  const auto read = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(document), options);
  CHECK(read.layers()[0].blend_mode() == BlendMode::Multiply);
  CHECK(read.layers()[1].blend_mode() == BlendMode::Normal);
  auto deep16 = make_deep_document(BitDepth::UInt16);
  deep16.layers()[1].set_blend_mode(BlendMode::Screen);
  const auto read16 = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(deep16), options);
  CHECK(read16.layers()[1].blend_mode() == BlendMode::Screen);
}

std::vector<patchy::test::TestCase> psd_deep_io_tests() {
  return {
      {"psd_deep_composite_is_written_at_depth", psd_deep_composite_is_written_at_depth},
      {"psd_deep_16_bit_document_round_trips_exactly", psd_deep_16_bit_document_round_trips_exactly},
      {"psd_deep_32_bit_document_round_trips_exactly", psd_deep_32_bit_document_round_trips_exactly},
      {"psd_deep_gate_off_reads_8_bit_as_before", psd_deep_gate_off_reads_8_bit_as_before},
      {"psd_deep_color_conversions_keep_sub_byte_samples_and_alpha",
       psd_deep_color_conversions_keep_sub_byte_samples_and_alpha},
      {"psd_deep_32_bit_blend_modes_follow_photoshop", psd_deep_32_bit_blend_modes_follow_photoshop},
      {"psd_deep_photoshop_corpus_reads_and_round_trips_if_available",
       psd_deep_photoshop_corpus_reads_and_round_trips_if_available},
  };
}
