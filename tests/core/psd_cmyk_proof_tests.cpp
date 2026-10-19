// CMYK Proof PSD and PSB saves (docs/cmyk.md, "CMYK Proof mode"): an RGB-born or edited
// document writes a CMYK file with its layers. Raster layers separate through the CMYK
// profile, unchanged layers of an imported CMYK file keep their own planes, and every
// layer kind the CMYK form cannot carry yet makes the whole file RGB, with
// layered_save_color_mode agreeing with the writer and naming the reason.
#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "core/psd_source_colors.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"
#include "psd_test_support.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy;

using Planes = std::array<std::vector<std::uint8_t>, 4>;

int mode_of(std::span<const std::uint8_t> bytes) {
  return (bytes[24] << 8U) | bytes[25];
}

int depth_of(std::span<const std::uint8_t> bytes) {
  return (bytes[22] << 8U) | bytes[23];
}

std::vector<std::uint8_t> bundled_profile() {
  std::ifstream in(patchy::test::source_root_path() / "third_party/icc/basiccolor/ISOcoated_v2_bas.ICC",
                   std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::shared_ptr<const CmykWorkingSpace> proof_space(RenderingIntent intent = RenderingIntent::RelativeColorimetric,
                                                    bool black_point_compensation = true) {
  auto space = CmykWorkingSpace::from_icc_profile(bundled_profile(), intent, black_point_compensation);
  CHECK(space != nullptr);
  return space;
}

psd::WriteOptions proof_options(std::shared_ptr<const CmykWorkingSpace> space, bool large) {
  psd::WriteOptions options;
  options.large_document = large;
  options.cmyk_proof = std::move(space);
  return options;
}

// What the writer must store for `pixels`: the RGB channels at `depth` separated through
// `profile`, as big-endian PSD planes (255 = no ink).
Planes expected_planes(const PixelBuffer& pixels, BitDepth depth, std::span<const std::uint8_t> profile,
                       RenderingIntent intent = RenderingIntent::RelativeColorimetric, bool bpc = true) {
  const auto at_depth = convert_pixel_buffer_depth(pixels, depth, SampleKind::Color);
  PixelBuffer rgb(at_depth.width(), at_depth.height(), with_bit_depth(PixelFormat::rgb8(), depth));
  const auto bytes = depth == BitDepth::UInt8 ? 1U : 2U;
  for (int y = 0; y < rgb.height(); ++y)
    for (int x = 0; x < rgb.width(); ++x) std::memcpy(rgb.pixel(x, y), at_depth.pixel(x, y), 3U * bytes);
  const auto inks = rgb_to_native_color_space(rgb, ColorMode::CMYK, profile, intent, bpc);
  CHECK(inks.has_value());
  Planes planes;
  for (std::size_t c = 0; c < 4; ++c) planes[c] = psd::big_endian_plane(*inks, c);
  return planes;
}

// The merged image's first four planes, decoded (raw or PackBits rows at the file's depth).
Planes composite_planes(std::span<const std::uint8_t> bytes) {
  psd::BigEndianReader reader(bytes);
  reader.skip(4);
  const auto version = reader.read_u16();
  reader.skip(6);
  const auto channels = reader.read_u16();
  const auto height = reader.read_u32();
  const auto width = reader.read_u32();
  const auto depth = reader.read_u16();
  reader.skip(2);
  reader.skip(reader.read_u32());
  reader.skip(reader.read_u32());
  reader.skip(static_cast<std::size_t>(version == 2 ? reader.read_u64() : reader.read_u32()));
  const auto compression = reader.read_u16();
  const std::size_t row_bytes = static_cast<std::size_t>(width) * (depth / 8U);
  Planes planes;
  if (compression == 0) {
    for (std::size_t c = 0; c < 4; ++c) planes[c] = reader.read_bytes(row_bytes * height);
    return planes;
  }
  CHECK(compression == 1);
  std::vector<std::size_t> counts;
  for (std::size_t row = 0; row < static_cast<std::size_t>(channels) * height; ++row)
    counts.push_back(version == 2 ? reader.read_u32() : reader.read_u16());
  for (std::size_t c = 0; c < 4; ++c) {
    for (std::size_t y = 0; y < height; ++y) {
      const auto packed = reader.read_bytes(counts[c * height + y]);
      std::vector<std::uint8_t> row;
      for (std::size_t i = 0; i < packed.size();) {
        const auto header = static_cast<std::int8_t>(packed[i++]);
        if (header >= 0) {
          row.insert(row.end(), packed.begin() + static_cast<std::ptrdiff_t>(i),
                     packed.begin() + static_cast<std::ptrdiff_t>(i + header + 1U));
          i += static_cast<std::size_t>(header) + 1U;
        } else if (header != -128) {
          row.insert(row.end(), static_cast<std::size_t>(1 - header), packed[i++]);
        }
      }
      CHECK(row.size() == row_bytes);
      planes[c].insert(planes[c].end(), row.begin(), row.end());
    }
  }
  return planes;
}

PixelBuffer gradient_pixels(int width, int height, int channels, int seed) {
  PixelBuffer pixels(width, height, channels == 4 ? PixelFormat::rgba8() : PixelFormat::rgb8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 5 + seed * 37) & 0xFF);
      pixel[1] = static_cast<std::uint8_t>((y * 7 + seed * 11) & 0xFF);
      pixel[2] = static_cast<std::uint8_t>((200 - x * 2 + seed * 53) & 0xFF);
      if (channels == 4) pixel[3] = static_cast<std::uint8_t>(40 + (x * 9 + y * 3) % 216);
    }
  }
  return pixels;
}

// An RGB-born stack: an opaque background, a translucent masked layer, and a group with a
// child, at `depth`.
Document rgb_born_document(BitDepth depth) {
  Document document(48, 32, PixelFormat::rgba8());
  Layer background(document.allocate_layer_id(), "Background", gradient_pixels(48, 32, 3, 0));
  background.set_bounds(Rect{0, 0, 48, 32});
  document.add_layer(std::move(background));
  Layer paint(document.allocate_layer_id(), "Paint", gradient_pixels(20, 12, 4, 1));
  paint.set_bounds(Rect{6, 5, 20, 12});
  LayerMask mask;
  mask.bounds = Rect{8, 6, 10, 8};
  mask.pixels = PixelBuffer(10, 8, PixelFormat::gray8());
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 10; ++x) mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>(x * 25);
  paint.set_mask(std::move(mask));
  paint.set_opacity(0.7F);
  document.add_layer(std::move(paint));
  Layer group(document.allocate_layer_id(), "Group", LayerKind::Group);
  Layer child(document.allocate_layer_id(), "Child", gradient_pixels(16, 16, 4, 2));
  child.set_bounds(Rect{28, 10, 16, 16});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  if (depth != BitDepth::UInt8) convert_document_depth(document, depth);
  return document;
}

// rgb_born_document plus a Patchy-authored text layer (no Txt2 block from any file).
Document authored_text_document(BitDepth depth) {
  auto document = rgb_born_document(BitDepth::UInt8);
  Layer text(document.allocate_layer_id(), "Words", gradient_pixels(20, 10, 4, 4));
  text.set_bounds(Rect{2, 2, 20, 10});
  text.metadata()[kLayerMetadataText] = "Words";
  text.metadata()[kLayerMetadataTextFont] = "Arial";
  text.metadata()[kLayerMetadataTextSize] = "12";
  text.metadata()[kLayerMetadataTextColor] = "#102030";
  text.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
  document.add_layer(std::move(text));
  if (depth != BitDepth::UInt8) convert_document_depth(document, depth);
  return document;
}

// A committed Photoshop file at `depth`; `regenerate_text` makes Patchy write its own type
// blocks for every text layer instead of Photoshop's.
Document fixture_document(const char* name, BitDepth depth, bool regenerate_text = false) {
  auto document = psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path(name));
  if (regenerate_text) {
    const auto regenerate = [](auto& self, std::vector<Layer>& layers) -> void {
      for (auto& layer : layers) {
        if (layer_is_text(layer)) layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
        self(self, layer.children());
      }
    };
    regenerate(regenerate, document.layers());
  }
  if (depth != BitDepth::UInt8) convert_document_depth(document, depth);
  return document;
}

std::vector<const Layer*> flat_layer_list(const std::vector<Layer>& layers) {
  std::vector<const Layer*> result;
  for (const auto& layer : layers) {
    result.push_back(&layer);
    const auto children = flat_layer_list(layer.children());
    result.insert(result.end(), children.begin(), children.end());
  }
  return result;
}

void psd_cmyk_proof_saves_rgb_documents_as_cmyk_with_layers() {
  const auto profile = bundled_profile();
  const auto space = proof_space();
  CHECK(!profile.empty());
  psd::ReadOptions read;
  read.keep_bit_depth = true;
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    const auto document = rgb_born_document(depth);
    // Outside CMYK Proof nothing changes: an RGB document saves RGB.
    CHECK(mode_of(psd::DocumentIo::write_layered_rgb8(document)) == 3);
    CHECK(psd::DocumentIo::layered_save_color_mode(document).mode == 3);
    CHECK(psd::DocumentIo::layered_save_color_mode(document).rgb_reason == nullptr);
    for (const bool large : {false, true}) {
      const auto options = proof_options(space, large);
      const auto decision = psd::DocumentIo::layered_save_color_mode(document, options);
      CHECK(decision.mode == 4);
      CHECK(decision.rgb_reason == nullptr);
      CHECK(psd::DocumentIo::layered_save_keeps_source_color_mode(document, options));
      const auto bytes = psd::DocumentIo::write_layered_rgb8(document, options);
      CHECK(mode_of(bytes) == 4);
      CHECK(depth_of(bytes) == (depth == BitDepth::UInt8 ? 8 : 16));
      CHECK(bytes[4] == 0 && bytes[5] == (large ? 2 : 1));
      // The merged image is the separation of the document's own composite.
      const auto composite_rgb = depth == BitDepth::UInt8
                                     ? psd::merged_flatten_composite(document).rgb
                                     : psd::deep_merged_flatten_composite(document, depth).rgb;
      CHECK(composite_planes(bytes) == expected_planes(composite_rgb, depth, profile));

      const auto reopened = psd::DocumentIo::read(bytes, read);
      CHECK(psd::DocumentIo::source_icc_profile(reopened) == profile);
      CHECK(reopened.metadata().values.at("psd.color_mode") == "CMYK");
      const auto before = flat_layer_list(std::as_const(document).layers());
      const auto after = flat_layer_list(std::as_const(reopened).layers());
      CHECK(before.size() == after.size());
      for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
        CHECK(before[i]->name() == after[i]->name());
        CHECK(before[i]->kind() == after[i]->kind());
        CHECK(std::abs(before[i]->opacity() - after[i]->opacity()) <= 1.0F / 255.0F);
        if (before[i]->kind() != LayerKind::Pixel) continue;
        CHECK(after[i]->bounds() == before[i]->bounds());
        const auto& colors = after[i]->psd_native_colors();
        CHECK(colors != nullptr);
        CHECK(colors->planes == expected_planes(before[i]->pixels(), depth, profile));
        // Alpha and masks pass through untouched.
        const auto& old_pixels = before[i]->pixels();
        const auto& new_pixels = after[i]->pixels();
        if (old_pixels.format().channels == 4) {
          for (int y = 0; y < old_pixels.height(); ++y)
            for (int x = 0; x < old_pixels.width(); ++x)
              CHECK(load_pixel(old_pixels.format(), old_pixels.pixel(x, y))[3] ==
                    load_pixel(new_pixels.format(), new_pixels.pixel(x, y))[3]);
        }
        CHECK(before[i]->mask().has_value() == after[i]->mask().has_value());
        if (before[i]->mask().has_value()) {
          CHECK(after[i]->mask()->bounds == before[i]->mask()->bounds);
          const auto old_mask = convert_pixel_buffer_depth(before[i]->mask()->pixels, depth, SampleKind::Coverage);
          const auto new_mask = convert_pixel_buffer_depth(after[i]->mask()->pixels, depth, SampleKind::Coverage);
          CHECK(std::ranges::equal(old_mask.data(), new_mask.data()));
        }
      }
      // A second CMYK Proof save of the reopened, unchanged file keeps its planes exactly.
      const auto again = psd::DocumentIo::write_layered_rgb8(reopened, options);
      CHECK(mode_of(again) == 4);
      const auto twice = psd::DocumentIo::read(again, read);
      const auto twice_layers = flat_layer_list(std::as_const(twice).layers());
      for (std::size_t i = 0; i < after.size() && i < twice_layers.size(); ++i) {
        if (after[i]->kind() != LayerKind::Pixel) continue;
        CHECK(twice_layers[i]->psd_native_colors()->planes == after[i]->psd_native_colors()->planes);
      }
      // An explicit RGB request wins over CMYK Proof.
      auto rgb = options;
      rgb.preserve_source_color_mode = false;
      CHECK(psd::DocumentIo::layered_save_color_mode(document, rgb).mode == 3);
      CHECK(mode_of(psd::DocumentIo::write_layered_rgb8(document, rgb)) == 3);
    }
  }
  // The working space's intent and black point compensation choose the separation.
  const auto document = rgb_born_document(BitDepth::UInt8);
  const auto perceptual = proof_space(RenderingIntent::Perceptual, false);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(document, proof_options(perceptual, false));
  const auto reopened = psd::DocumentIo::read(bytes, read);
  CHECK(reopened.layers()[0].psd_native_colors()->planes ==
        expected_planes(std::as_const(document).layers()[0].pixels(), BitDepth::UInt8, profile,
                        RenderingIntent::Perceptual, false));
  CHECK(reopened.layers()[0].psd_native_colors()->planes !=
        expected_planes(std::as_const(document).layers()[0].pixels(), BitDepth::UInt8, profile));
  // Without a profile (the formula) CMYK Proof cannot apply and the file stays RGB.
  const auto formula = proof_options(CmykWorkingSpace::formula(), false);
  CHECK(psd::DocumentIo::layered_save_color_mode(document, formula).mode == 3);
  CHECK(mode_of(psd::DocumentIo::write_layered_rgb8(document, formula)) == 3);
}

// One-layer CMYK file with a fine ink ramp and merged transparency (the
// psd_deep_io_tests fixture shape), 8 or 16 bits, tagged with `profile` when given.
std::vector<std::uint8_t> cmyk_ramp_psd(std::span<const std::uint8_t> profile, bool eight_bit) {
  using namespace patchy::test;
  psd::BigEndianWriter resources;
  if (!profile.empty()) {
    write_ascii4(resources, "8BIM");
    resources.write_u16(1039);
    resources.write_u16(0);
    resources.write_u32(static_cast<std::uint32_t>(profile.size()));
    resources.write_bytes(profile);
    if (profile.size() % 2U != 0U) resources.write_u8(0);
  }
  std::vector<std::vector<std::uint8_t>> planes;
  for (std::uint16_t c = 0; c <= 4; ++c) {
    psd::BigEndianWriter plane;
    for (std::uint16_t x = 0; x < 64; ++x) {
      const auto value = c == 4 ? 180U + x : c == 0 ? 80U + x * 2U : c == 1 ? 200U - x : c == 2 ? 128U : 240U;
      if (eight_bit) plane.write_u8(static_cast<std::uint8_t>(value));
      else plane.write_u16(static_cast<std::uint16_t>(value * 257U + x));
    }
    planes.push_back(plane.bytes());
  }
  psd::BigEndianWriter info;
  info.write_u16(0xFFFF);  // one layer, merged transparency present
  info.write_u32(0); info.write_u32(0); info.write_u32(1); info.write_u32(64);
  info.write_u16(5);
  for (std::uint16_t c = 0; c <= 4; ++c) {
    info.write_u16(c == 4 ? 0xFFFF : c);
    info.write_u32(eight_bit ? 66 : 130);
  }
  write_ascii4(info, "8BIM"); write_ascii4(info, "norm");
  info.write_u8(255); info.write_u8(0); info.write_u8(8); info.write_u8(0);
  info.write_u32(16);  // empty mask and blending ranges, name "Ink"
  info.write_u32(0); info.write_u32(0);
  info.write_u8(3); info.write_u8('I'); info.write_u8('n'); info.write_u8('k');
  info.write_u32(0);
  for (const auto& plane : planes) {
    info.write_u16(0);
    info.write_bytes(plane);
  }
  psd::BigEndianWriter layer_mask;
  if (eight_bit) {
    layer_mask.write_u32(static_cast<std::uint32_t>(info.bytes().size()));
    layer_mask.write_bytes(info.bytes());
    layer_mask.write_u32(0);
  } else {
    layer_mask.write_u32(0); layer_mask.write_u32(0);
    write_ascii4(layer_mask, "8BIM"); write_ascii4(layer_mask, "Lr16");
    layer_mask.write_u32(static_cast<std::uint32_t>(info.bytes().size()));
    layer_mask.write_bytes(info.bytes());
    while (layer_mask.bytes().size() % 4U != 0U) layer_mask.write_u8(0);
  }
  psd::BigEndianWriter file;
  psd::write_header(file, psd::Header{false, 5, 1, 64, static_cast<std::uint16_t>(eight_bit ? 8 : 16), 4});
  file.write_u32(0);
  file.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  file.write_bytes(resources.bytes());
  file.write_u32(static_cast<std::uint32_t>(layer_mask.bytes().size()));
  file.write_bytes(layer_mask.bytes());
  file.write_u16(0);
  for (const auto& plane : planes) file.write_bytes(plane);
  return file.bytes();
}

void psd_cmyk_proof_keeps_unchanged_cmyk_layers_and_separates_edits() {
  const auto bundled = bundled_profile();
  const auto space = proof_space();
  // The committed Photoshop CMYK file's profile (U.S. Web Coated), unlike the working one.
  const auto cmyk_source = psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-cmyk-style-colors.psd"));
  const auto file_profile =
      patchy::test::test_image_resource_payload(cmyk_source.metadata().raw_psd_image_resources, 1039);
  CHECK(file_profile.has_value());
  CHECK(*file_profile != bundled);
  psd::ReadOptions read;
  read.keep_bit_depth = true;
  for (const bool eight_bit : {true, false}) {
    const auto depth = eight_bit ? BitDepth::UInt8 : BitDepth::UInt16;
    for (const bool tagged : {true, false}) {
      const auto source_profile = tagged ? std::span<const std::uint8_t>(*file_profile)
                                         : std::span<const std::uint8_t>{};
      // Untagged inks separate through the same assumed profile their preview used.
      const auto separation_profile = tagged ? std::vector<std::uint8_t>(file_profile->begin(), file_profile->end())
                                             : default_cmyk_profile();
      const auto original = psd::DocumentIo::read(cmyk_ramp_psd(source_profile, eight_bit), read);
      CHECK(original.layers().size() == 1);
      const auto& imported_planes = original.layers()[0].psd_native_colors()->planes;
      for (const bool large : {false, true}) {
        const auto options = proof_options(space, large);
        auto document = original;
        // A new RGB layer: outside CMYK Proof the file goes RGB, as before.
        Layer added(document.allocate_layer_id(), "Added", gradient_pixels(24, 1, 4, 3));
        added.set_bounds(Rect{20, 0, 24, 1});
        if (!eight_bit) added.pixels() = convert_pixel_buffer_depth(added.pixels(), depth, SampleKind::Color);
        document.add_layer(std::move(added));
        CHECK(mode_of(psd::DocumentIo::write_layered_rgb8(document, psd::WriteOptions{large})) == 3);
        CHECK(psd::DocumentIo::layered_save_color_mode(document, psd::WriteOptions{large}).mode == 3);
        // In CMYK Proof: CMYK, the untouched layer byte-identical, the new one separated
        // through the file's own profile, which the file keeps (or stays untagged).
        CHECK(psd::DocumentIo::layered_save_color_mode(document, options).mode == 4);
        const auto bytes = psd::DocumentIo::write_layered_rgb8(document, options);
        CHECK(mode_of(bytes) == 4);
        CHECK(depth_of(bytes) == (eight_bit ? 8 : 16));
        auto reopened = psd::DocumentIo::read(bytes, read);
        CHECK(psd::DocumentIo::source_icc_profile(reopened) ==
              std::vector<std::uint8_t>(source_profile.begin(), source_profile.end()));
        CHECK(reopened.layers().size() == 2);
        CHECK(reopened.layers()[0].name() == "Ink");
        CHECK(reopened.layers()[0].psd_native_colors()->planes == imported_planes);
        CHECK(reopened.layers()[1].psd_native_colors()->planes ==
              expected_planes(std::as_const(document).layers()[1].pixels(), depth, separation_profile));
        // Editing the imported layer separates its new pixels too.
        document.layers()[0].pixels().pixel(5, 0)[0] ^= 0x40;
        CHECK(psd::DocumentIo::layered_save_color_mode(document, options).mode == 4);
        const auto edited = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(document, options), read);
        CHECK(edited.layers()[0].psd_native_colors()->planes ==
              expected_planes(std::as_const(document).layers()[0].pixels(), depth, separation_profile));
        CHECK(edited.layers()[0].psd_native_colors()->planes != imported_planes);
      }
    }
  }
  // A depth change keeps the file's profile, but no stored plane is still valid.
  {
    const auto original = psd::DocumentIo::read(cmyk_ramp_psd(*file_profile, true), read);
    auto deep = original;
    convert_document_depth(deep, BitDepth::UInt16);
    const auto options = proof_options(space, false);
    CHECK(psd::DocumentIo::layered_save_color_mode(deep, options).mode == 4);
    const auto bytes = psd::DocumentIo::write_layered_rgb8(deep, options);
    CHECK(depth_of(bytes) == 16);
    const auto reopened = psd::DocumentIo::read(bytes, read);
    CHECK(psd::DocumentIo::source_icc_profile(reopened) == *file_profile);
    CHECK(reopened.layers()[0].psd_native_colors()->planes ==
          expected_planes(std::as_const(deep).layers()[0].pixels(), BitDepth::UInt16, *file_profile));
  }
}

// Every kind the CMYK form cannot carry yet: the predicate names it, the writer agrees.
void psd_cmyk_proof_falls_back_to_rgb_for_unsupported_layers() {
  const auto space = proof_space();
  const auto check_rgb = [&space](const Document& document, std::string_view expected_reason) {
    for (const bool large : {false, true}) {
      const auto options = proof_options(space, large);
      const auto decision = psd::DocumentIo::layered_save_color_mode(document, options);
      CHECK(decision.mode == 3);
      CHECK(decision.rgb_reason != nullptr && std::string_view(decision.rgb_reason) == expected_reason);
      CHECK(!psd::DocumentIo::layered_save_keeps_source_color_mode(document, options));
      CHECK(mode_of(psd::DocumentIo::write_layered_rgb8(document, options)) == 3);
    }
  };
  const auto with_layer = [](const Layer& prototype, bool in_group) {
    auto document = rgb_born_document(BitDepth::UInt8);
    auto layer = prototype.clone_with_id(document.allocate_layer_id());
    if (in_group) {
      document.layers()[2].add_child(std::move(layer));
    } else {
      document.add_layer(std::move(layer));
    }
    return document;
  };
  for (const auto mode : {BlendMode::Multiply, BlendMode::Screen, BlendMode::Difference, BlendMode::Dissolve}) {
    Layer blended(0, "Blended", gradient_pixels(12, 12, 4, 4));
    blended.set_bounds(Rect{2, 2, 12, 12});
    blended.set_blend_mode(mode);
    for (const bool in_group : {false, true}) {
      check_rgb(with_layer(blended, in_group),
                "In a CMYK file its layer blend modes would mix the inks and change the colors.");
    }
  }
  {
    Layer adjustment(0, "Levels", LayerKind::Adjustment);
    AdjustmentSettings settings;
    settings.kind = AdjustmentKind::Invert;
    configure_adjustment_layer(adjustment, settings);
    check_rgb(with_layer(adjustment, false),
              "In a CMYK file its adjustment layers would act on the inks and change the colors.");
  }
  {
    LiveShapeParams geometry;
    geometry.kind = LiveShapeKind::Rectangle;
    geometry.left = 4; geometry.top = 4; geometry.right = 30; geometry.bottom = 20;
    VectorShapeContent shape;
    shape.path.subpaths = generate_live_shape_subpaths(geometry);
    shape.fill.color = RgbColor{200, 40, 20};
    Layer layer(0, "Shape", LayerKind::Pixel);
    layer.metadata()[kLayerMetadataVectorShape] = "1";
    layer.set_vector_shape(std::move(shape));
    update_vector_shape_raster(layer, Rect::from_size(48, 32), nullptr);
    check_rgb(with_layer(layer, false), "Patchy can save shape and fill layers only in an RGB PSD.");
  }
  {
    Layer styled(0, "Styled", gradient_pixels(12, 12, 4, 5));
    styled.set_bounds(Rect{1, 1, 12, 12});
    LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.color = RgbColor{0, 0, 0};
    shadow.opacity = 0.5F;
    styled.layer_style().drop_shadows.push_back(shadow);
    check_rgb(with_layer(styled, true), "Patchy can save layer styles only in an RGB PSD.");
  }
  {
    Layer blend(0, "Blend If", gradient_pixels(12, 12, 4, 6));
    blend.set_bounds(Rect{1, 1, 12, 12});
    LayerBlendIf ranges;
    ranges.channels[1].this_layer.black_low = 40;
    ranges.channels[1].this_layer.black_high = 40;
    CHECK(blend.set_blend_if(ranges));
    check_rgb(with_layer(blend, false), "Its Blend If or channel blending settings refer to RGB channels.");
    Layer restricted(0, "Channels", gradient_pixels(12, 12, 4, 7));
    restricted.set_bounds(Rect{1, 1, 12, 12});
    restricted.set_restricted_channels(kRestrictGreen);
    check_rgb(with_layer(restricted, false), "Its Blend If or channel blending settings refer to RGB channels.");
  }
  {
    // Committed Photoshop files: an RGB Blend If layer, a pattern overlay style.
    check_rgb(psd::DocumentIo::read_file(
                  patchy::test::committed_psd_fixture_path("photoshop-blend-if-4b-roundtrip.psd")),
              "Its Blend If or channel blending settings refer to RGB channels.");
    check_rgb(psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-pattern-overlay.psd")),
              "Patchy can save layer styles only in an RGB PSD.");
  }
  // An RGB file's Smart Filter cache converts to CMYK only in an 8-bit document.
  check_rgb(fixture_document("photoshop-smart-filter-median.psd", BitDepth::UInt16),
            "Its Smart Filter previews cannot be converted to CMYK.");
  {
    auto deep = rgb_born_document(BitDepth::UInt8);
    convert_document_depth(deep, BitDepth::Float32);
    check_rgb(deep, "32-bit documents are saved in RGB.");
  }
  {
    // 52 saved channels fit RGB's limit (with merged transparency) but not CMYK's.
    auto crowded = rgb_born_document(BitDepth::UInt8);
    for (int i = 0; i < 52; ++i) {
      crowded.add_channel(DocumentChannel(crowded.allocate_channel_id(), "Alpha " + std::to_string(i),
                                          DocumentChannelKind::Alpha, PixelBuffer(48, 32, PixelFormat::gray8())));
    }
    check_rgb(crowded, "It has too many saved channels for a CMYK file.");
  }
}

// Text and Smart Object layers write their separated pixels; Photoshop shows them as they
// are (the stored preview, RGB EngineData colors accepted). The kinds stay what they were.
void psd_cmyk_proof_writes_text_and_smart_objects_in_cmyk() {
  const auto profile = bundled_profile();
  const auto space = proof_space();
  psd::ReadOptions read;
  read.keep_bit_depth = true;
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    struct Case {
      Document document;
      bool (*kind)(const Layer&);
    };
    std::vector<Case> cases;
    cases.push_back({authored_text_document(depth), layer_is_text});
    cases.push_back({fixture_document("photoshop-text-tracking.psd", depth), layer_is_text});
    cases.push_back({fixture_document("photoshop-text-tracking.psd", depth, true), layer_is_text});
    cases.push_back({fixture_document("photoshop-place-embedded-png.psd", depth), layer_is_smart_object});
    if (depth == BitDepth::UInt8) {
      cases.push_back({fixture_document("photoshop-smart-filter-median.psd", depth), layer_is_smart_object});
    }
    for (const auto& [document, kind] : cases) {
      for (const bool large : {false, true}) {
        const auto options = proof_options(space, large);
        CHECK(psd::DocumentIo::layered_save_color_mode(document, options).mode == 4);
        const auto bytes = psd::DocumentIo::write_layered_rgb8(document, options);
        CHECK(mode_of(bytes) == 4);
        const auto reopened = psd::DocumentIo::read(bytes, read);
        const auto before = flat_layer_list(document.layers());
        const auto after = flat_layer_list(reopened.layers());
        CHECK(before.size() == after.size());
        int matched = 0;
        for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
          if (!kind(*before[i])) continue;
          ++matched;
          CHECK(kind(*after[i]));
          CHECK(after[i]->psd_native_colors() != nullptr);
          CHECK(after[i]->psd_native_colors()->planes == expected_planes(before[i]->pixels(), depth, profile));
        }
        CHECK(matched > 0);
      }
    }
  }
}

// The files the Photoshop acceptance run opens (docs/cmyk.md, "CMYK Proof PSD saves"):
// written under test-artifacts/cmyk-proof, one per supported kind, PSD and PSB.
void psd_cmyk_proof_writes_photoshop_acceptance_files() {
  const auto space = proof_space();
  const auto directory = std::filesystem::path("test-artifacts") / "cmyk-proof";
  std::filesystem::create_directories(directory);
  const auto save = [&](const Document& document, const std::string& name) {
    for (const bool large : {false, true}) {
      const auto bytes = psd::DocumentIo::write_layered_rgb8(document, proof_options(space, large));
      CHECK(mode_of(bytes) == 4);
      std::ofstream out(directory / (name + (large ? ".psb" : ".psd")), std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      CHECK(out.good());
    }
  };
  save(rgb_born_document(BitDepth::UInt8), "rgb-born-8");
  save(rgb_born_document(BitDepth::UInt16), "rgb-born-16");
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16}) {
    const std::string bits = depth == BitDepth::UInt8 ? "-8" : "-16";
    save(authored_text_document(depth), "text-authored" + bits);
    save(fixture_document("photoshop-text-tracking.psd", depth), "text-photoshop" + bits);
    save(fixture_document("photoshop-text-tracking.psd", depth, true), "text-regenerated" + bits);
    save(fixture_document("photoshop-place-embedded-png.psd", depth), "smart-object" + bits);
  }
  save(fixture_document("photoshop-smart-filter-median.psd", BitDepth::UInt8), "smart-filter-8");
  psd::ReadOptions read;
  read.keep_bit_depth = true;
  const auto cmyk_source = psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-cmyk-style-colors.psd"));
  const auto file_profile =
      patchy::test::test_image_resource_payload(cmyk_source.metadata().raw_psd_image_resources, 1039);
  CHECK(file_profile.has_value());
  for (const bool eight_bit : {true, false}) {
    auto document = psd::DocumentIo::read(cmyk_ramp_psd(*file_profile, eight_bit), read);
    Layer added(document.allocate_layer_id(), "Added", gradient_pixels(24, 1, 4, 3));
    added.set_bounds(Rect{20, 0, 24, 1});
    if (!eight_bit) added.pixels() = convert_pixel_buffer_depth(added.pixels(), BitDepth::UInt16, SampleKind::Color);
    document.add_layer(std::move(added));
    save(document, eight_bit ? "cmyk-edited-8" : "cmyk-edited-16");
  }
}


}  // namespace

std::vector<patchy::test::TestCase> psd_cmyk_proof_tests() {
  return {
      {"psd_cmyk_proof_saves_rgb_documents_as_cmyk_with_layers",
       psd_cmyk_proof_saves_rgb_documents_as_cmyk_with_layers},
      {"psd_cmyk_proof_keeps_unchanged_cmyk_layers_and_separates_edits",
       psd_cmyk_proof_keeps_unchanged_cmyk_layers_and_separates_edits},
      {"psd_cmyk_proof_writes_text_and_smart_objects_in_cmyk", psd_cmyk_proof_writes_text_and_smart_objects_in_cmyk},
      {"psd_cmyk_proof_falls_back_to_rgb_for_unsupported_layers",
       psd_cmyk_proof_falls_back_to_rgb_for_unsupported_layers},
      {"psd_cmyk_proof_writes_photoshop_acceptance_files", psd_cmyk_proof_writes_photoshop_acceptance_files},
  };
}
