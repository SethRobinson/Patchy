// 16/32-bit primitives (docs/high-bit-depth.md, Phase 1): exact sample conversions,
// typed rows, buffer and whole-document depth conversion, the depth invariant and
// the gate.

#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/smart_filter.hpp"
#include "support/srgb_transfer.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using namespace patchy;

std::uint16_t u16_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y, int channel) {
  std::uint16_t value = 0;
  std::memcpy(&value, buffer.pixel(x, y) + channel * 2, sizeof(value));
  return value;
}

float f32_at(const PixelBuffer& buffer, std::int32_t x, std::int32_t y, int channel) {
  float value = 0.0F;
  std::memcpy(&value, buffer.pixel(x, y) + channel * 4, sizeof(value));
  return value;
}

void set_u16(PixelBuffer& buffer, std::int32_t x, std::int32_t y, int channel, std::uint16_t value) {
  std::memcpy(buffer.pixel(x, y) + channel * 2, &value, sizeof(value));
}

// 256 x 1 RGBA: every byte value in every channel (each channel offset so they differ).
PixelBuffer every_byte_rgba() {
  PixelBuffer pixels(256, 1, PixelFormat::rgba8());
  for (int x = 0; x < 256; ++x) {
    auto* p = pixels.pixel(x, 0);
    p[0] = static_cast<std::uint8_t>(x);
    p[1] = static_cast<std::uint8_t>((x + 85) % 256);
    p[2] = static_cast<std::uint8_t>((x + 170) % 256);
    p[3] = static_cast<std::uint8_t>(255 - x);
  }
  return pixels;
}

void u8_u16_sample_conversions_are_exact() {
  for (int v = 0; v < 256; ++v) {
    CHECK(widen_u8_to_u16(static_cast<std::uint8_t>(v)) == v * 257);
    CHECK(narrow_u16_to_u8(widen_u8_to_u16(static_cast<std::uint8_t>(v))) == v);
  }
  // Round to nearest over the whole range (the PSD reader's rule).
  for (int v = 0; v < 65536; ++v) {
    const auto narrowed = narrow_u16_to_u8(static_cast<std::uint16_t>(v));
    CHECK(std::abs(static_cast<double>(narrowed) * 257.0 - v) <= 128.5);
  }
}

void buffer_depth_round_trips_keep_8_bit_content() {
  const auto source = every_byte_rgba();
  const auto deep16 = convert_pixel_buffer_depth(source, BitDepth::UInt16, SampleKind::Color);
  CHECK(deep16.format().bit_depth == BitDepth::UInt16 && deep16.format().channels == 4);
  for (int x = 0; x < 256; ++x) {
    for (int c = 0; c < 4; ++c) {
      CHECK(u16_at(deep16, x, 0, c) == source.pixel(x, 0)[c] * 257);
    }
  }
  const auto back16 = convert_pixel_buffer_depth(deep16, BitDepth::UInt8, SampleKind::Color);
  CHECK(std::memcmp(back16.data().data(), source.data().data(), source.byte_size()) == 0);

  const auto deep32 = convert_pixel_buffer_depth(source, BitDepth::Float32, SampleKind::Color);
  for (int x = 0; x < 256; ++x) {
    // Color decodes to linear light; alpha scales linearly.
    const double expected = srgb_decode(source.pixel(x, 0)[0] / 255.0);
    CHECK(std::abs(f32_at(deep32, x, 0, 0) - expected) < 1e-6);
    CHECK(f32_at(deep32, x, 0, 3) == static_cast<float>(source.pixel(x, 0)[3]) / 255.0F);
  }
  const auto back32 = convert_pixel_buffer_depth(deep32, BitDepth::UInt8, SampleKind::Color);
  CHECK(std::memcmp(back32.data().data(), source.data().data(), source.byte_size()) == 0);
  // A 32-bit value converts to 8 bits exactly as the PSD reader decodes it.
  CHECK(back32.pixel(200, 0)[0] == linear_to_srgb8(f32_at(deep32, 200, 0, 0)));
}

void sixteen_bit_survives_a_trip_through_32_bits() {
  PixelBuffer source(256, 256, PixelFormat::rgba8());
  source = convert_pixel_buffer_depth(source, BitDepth::UInt16, SampleKind::Color);
  for (int i = 0; i < 65536; ++i) {
    const auto x = i % 256;
    const auto y = i / 256;
    set_u16(source, x, y, 0, static_cast<std::uint16_t>(i));
    set_u16(source, x, y, 1, static_cast<std::uint16_t>(65535 - i));
    set_u16(source, x, y, 2, static_cast<std::uint16_t>((i * 7) % 65536));
    set_u16(source, x, y, 3, static_cast<std::uint16_t>(i));
  }
  const auto deep = convert_pixel_buffer_depth(source, BitDepth::Float32, SampleKind::Color);
  const auto back = convert_pixel_buffer_depth(deep, BitDepth::UInt16, SampleKind::Color);
  int worst = 0;
  for (int y = 0; y < 256; ++y) {
    for (int x = 0; x < 256; ++x) {
      for (int c = 0; c < 4; ++c) {
        worst = std::max(worst, std::abs(static_cast<int>(u16_at(back, x, y, c)) - u16_at(source, x, y, c)));
      }
    }
  }
  CHECK(worst <= 1);
}

void coverage_buffers_scale_linearly_at_every_depth() {
  PixelBuffer mask(256, 1, PixelFormat::gray8());
  for (int x = 0; x < 256; ++x) {
    mask.pixel(x, 0)[0] = static_cast<std::uint8_t>(x);
  }
  const auto deep16 = convert_pixel_buffer_depth(mask, BitDepth::UInt16, SampleKind::Coverage);
  const auto deep32 = convert_pixel_buffer_depth(mask, BitDepth::Float32, SampleKind::Coverage);
  CHECK(deep16.format() == with_bit_depth(PixelFormat::gray8(), BitDepth::UInt16));
  for (int x = 0; x < 256; ++x) {
    CHECK(u16_at(deep16, x, 0, 0) == x * 257);
    CHECK(f32_at(deep32, x, 0, 0) == static_cast<float>(x) / 255.0F);
    CHECK(std::abs(coverage_at(deep16, x, 0) - x / 255.0F) < 1e-6F);
    CHECK(std::abs(coverage_at(deep32, x, 0) - x / 255.0F) < 1e-6F);
  }
  const auto back = convert_pixel_buffer_depth(deep32, BitDepth::UInt8, SampleKind::Coverage);
  CHECK(std::memcmp(back.data().data(), mask.data().data(), mask.byte_size()) == 0);
  std::vector<float> row(256);
  load_coverage_row(deep16, 0, 0, 256, row);
  CHECK(row[51] == 51.0F);
  PixelBuffer stored(256, 1, with_bit_depth(PixelFormat::gray8(), BitDepth::UInt16));
  store_coverage_row(stored, 0, 0, 256, row);
  CHECK(std::memcmp(stored.data().data(), deep16.data().data(), deep16.byte_size()) == 0);
}

void typed_rows_round_trip_in_their_domain() {
  const auto source = every_byte_rgba();
  std::vector<float> row(256 * 4);
  // 8-bit, Encoded: the values themselves.
  load_rgba_row(source, 0, 0, 256, DeepDomain::Encoded, row);
  CHECK(row[4 * 10 + 0] == 10.0F && row[4 * 10 + 3] == 245.0F);
  PixelBuffer out8(256, 1, PixelFormat::rgba8());
  store_rgba_row(out8, 0, 0, 256, DeepDomain::Encoded, row);
  CHECK(std::memcmp(out8.data().data(), source.data().data(), source.byte_size()) == 0);
  // 8-bit read into the Linear domain and stored back encodes again exactly.
  load_rgba_row(source, 0, 0, 256, DeepDomain::Linear, row);
  CHECK(std::abs(row[4 * 128] - srgb_decode(128.0 / 255.0) * 255.0) < 1e-3);
  CHECK(row[4 * 10 + 3] == 245.0F);  // alpha is never transferred
  store_rgba_row(out8, 0, 0, 256, DeepDomain::Linear, row);
  CHECK(std::memcmp(out8.data().data(), source.data().data(), source.byte_size()) == 0);
  // 16-bit, Encoded: v / 257 on the deep scale, stored back exactly.
  const auto deep16 = convert_pixel_buffer_depth(source, BitDepth::UInt16, SampleKind::Color);
  load_rgba_row(deep16, 0, 0, 256, DeepDomain::Encoded, row);
  CHECK(row[4 * 10 + 0] == 10.0F);
  PixelBuffer out16(256, 1, with_bit_depth(PixelFormat::rgba8(), BitDepth::UInt16));
  store_rgba_row(out16, 0, 0, 256, DeepDomain::Encoded, row);
  CHECK(std::memcmp(out16.data().data(), deep16.data().data(), deep16.byte_size()) == 0);
  // 32-bit, Linear: color keeps values above full scale; alpha clamps.
  PixelBuffer out32(2, 1, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
  const float hdr[8] = {510.0F, 0.0F, 25.5F, 255.0F, 1.0F, 2.0F, 3.0F, 400.0F};
  store_rgba_row(out32, 0, 0, 2, DeepDomain::Linear, hdr);
  CHECK(f32_at(out32, 0, 0, 0) == 2.0F && f32_at(out32, 0, 0, 2) == 0.1F);
  CHECK(f32_at(out32, 1, 0, 3) == 1.0F);
  std::vector<float> back(8);
  load_rgba_row(out32, 0, 0, 2, DeepDomain::Linear, back);
  CHECK(back[0] == 510.0F && back[7] == 255.0F);
  // A 3-channel buffer reads opaque.
  PixelBuffer rgb(1, 1, PixelFormat::rgb8());
  load_rgba_row(rgb, 0, 0, 1, DeepDomain::Encoded, back);
  CHECK(back[3] == 255.0F);
}

Document sample_document() {
  Document document(4, 3, PixelFormat::rgb8());
  PixelBuffer pixels(4, 3, PixelFormat::rgba8());
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 4; ++x) {
      auto* p = pixels.pixel(x, y);
      p[0] = static_cast<std::uint8_t>(x * 60 + y);
      p[1] = static_cast<std::uint8_t>(200 - y * 50);
      p[2] = static_cast<std::uint8_t>(x * 17);
      p[3] = static_cast<std::uint8_t>(255 - x * 40);
    }
  }
  auto& base = document.add_pixel_layer("Base", pixels);
  LayerMask mask;
  mask.bounds = Rect::from_size(4, 3);
  mask.pixels = PixelBuffer(4, 3, PixelFormat::gray8());
  mask.pixels.pixel(1, 1)[0] = 77;
  base.set_mask(mask);
  SmartFilterStack stack;
  stack.mask.bounds = Rect::from_size(4, 3);
  stack.mask.pixels = PixelBuffer(4, 3, PixelFormat::gray8());
  stack.mask.pixels.pixel(2, 2)[0] = 99;
  base.set_smart_filter_stack(stack);

  Layer text(document.allocate_layer_id(), "Title", LayerKind::Text);
  text.pixels() = PixelBuffer(2, 1, PixelFormat::rgba8());
  text.pixels().pixel(1, 0)[0] = 33;
  text.set_bounds(Rect{2, 1, 2, 1});
  Layer group(document.allocate_layer_id(), "Group", LayerKind::Group);
  group.add_child(std::move(text));
  document.add_layer(std::move(group));
  document.add_channel(DocumentChannel(document.allocate_channel_id(), "Alpha 1", DocumentChannelKind::Alpha,
                                       PixelBuffer(4, 3, PixelFormat::gray8())));
  document.metadata().psd_flat_composite = PixelBuffer(4, 3, PixelFormat::rgb8());
  return document;
}

void document_depth_conversion_covers_every_buffer_and_round_trips() {
  const auto original = sample_document();
  CHECK(document_depth_problems(original).empty());
  for (const auto depth : {BitDepth::UInt16, BitDepth::Float32}) {
    auto document = original;
    convert_document_depth(document, depth);
    CHECK(document_bit_depth(document) == depth);
    CHECK(document.format().bit_depth == depth);
    CHECK(document_depth_problems(document).empty());
    const auto& base = document.layers()[0];
    CHECK(base.pixels().format().bit_depth == depth);
    CHECK(base.mask()->pixels.format().bit_depth == depth);
    CHECK(base.smart_filter_stack()->mask.pixels.format().bit_depth == depth);
    CHECK(document.channels()[0].pixels().format().bit_depth == depth);
    CHECK(document.metadata().psd_flat_composite->format().bit_depth == depth);
    // The text layer stays a text layer where it was.
    const auto& text = document.layers()[1].children()[0];
    CHECK(text.kind() == LayerKind::Text);
    CHECK(text.bounds().x == 2 && text.bounds().y == 1);
    CHECK(text.pixels().format().bit_depth == depth);

    convert_document_depth(document, BitDepth::UInt8);
    CHECK(document_bit_depth(document) == BitDepth::UInt8);
    CHECK(document_depth_problems(document).empty());
    const auto& before = original.layers()[0].pixels();
    const auto& after = document.layers()[0].pixels();
    CHECK(std::memcmp(before.data().data(), after.data().data(), before.byte_size()) == 0);
    CHECK(document.layers()[0].mask()->pixels.pixel(1, 1)[0] == 77);
    CHECK(document.layers()[0].smart_filter_stack()->mask.pixels.pixel(2, 2)[0] == 99);
    CHECK(document.layers()[1].children()[0].pixels().pixel(1, 0)[0] == 33);
  }
}

void depth_problems_name_the_buffers_that_break_the_invariant() {
  auto document = sample_document();
  convert_document_depth(document, BitDepth::UInt16);
  // An 8-bit pixel layer arriving in a 16-bit document is reported; a derived
  // raster (the text layer) may be any depth.
  document.add_pixel_layer("Pasted", PixelBuffer(4, 3, PixelFormat::rgba8()));
  document.layers()[1].children()[0].pixels() = PixelBuffer(2, 1, PixelFormat::rgba8());
  const auto problems = document_depth_problems(document);
  CHECK(problems.size() == 1);
  CHECK(problems[0] == "layer 'Pasted' is 8-bit");
}

void display_bytes_match_the_8_bit_reading_at_every_depth() {
  // Previews (layer thumbnails) read any depth as display bytes; for content that came
  // from 8 bits that is the original byte at every depth.
  const auto source = every_byte_rgba();
  for (const auto depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Float32}) {
    const auto deep = convert_pixel_buffer_depth(source, depth, SampleKind::Color);
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto bytes = display_rgba8_at(deep, x, 0);
      for (int c = 0; c < 4; ++c) {
        CHECK(bytes[static_cast<std::size_t>(c)] == source.pixel(x, 0)[c]);
      }
    }
  }
}

void deep_editing_gate_can_be_overridden() {
  set_deep_editing_override(true);
  CHECK(deep_editing_enabled());
  set_deep_editing_override(false);
  CHECK(!deep_editing_enabled());
  set_deep_editing_override(std::nullopt);
}

}  // namespace

std::vector<patchy::test::TestCase> pixel_depth_tests() {
  return {
      {"pixel_depth_u8_u16_sample_conversions_are_exact", u8_u16_sample_conversions_are_exact},
      {"pixel_depth_buffer_round_trips_keep_8_bit_content", buffer_depth_round_trips_keep_8_bit_content},
      {"pixel_depth_sixteen_bit_survives_a_trip_through_32_bits", sixteen_bit_survives_a_trip_through_32_bits},
      {"pixel_depth_coverage_buffers_scale_linearly_at_every_depth",
       coverage_buffers_scale_linearly_at_every_depth},
      {"pixel_depth_typed_rows_round_trip_in_their_domain", typed_rows_round_trip_in_their_domain},
      {"pixel_depth_document_conversion_covers_every_buffer_and_round_trips",
       document_depth_conversion_covers_every_buffer_and_round_trips},
      {"pixel_depth_problems_name_the_buffers_that_break_the_invariant",
       depth_problems_name_the_buffers_that_break_the_invariant},
      {"pixel_depth_display_bytes_match_the_8_bit_reading_at_every_depth",
       display_bytes_match_the_8_bit_reading_at_every_depth},
      {"pixel_depth_gate_can_be_overridden", deep_editing_gate_can_be_overridden},
  };
}
