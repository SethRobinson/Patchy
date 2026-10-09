// 16/32-bit primitives (docs/high-bit-depth.md, Phase 1): exact sample conversions,
// typed rows, buffer and whole-document depth conversion, the depth invariant and
// the gate.

#include "core/document.hpp"
#include "core/environment.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "core/smart_filter.hpp"
#include "support/srgb_transfer.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
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

// A document with an opaque Background, a translucent layer with a gradient mask, and a
// saved channel: every buffer the geometry operations move.
Document geometry_document() {
  Document document(24, 16, PixelFormat::rgb8());
  PixelBuffer background(24, 16, PixelFormat::rgb8());
  PixelBuffer top(20, 12, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 24; ++x) {
      auto* b = background.pixel(x, y);
      b[0] = static_cast<std::uint8_t>(x * 10);
      b[1] = static_cast<std::uint8_t>(y * 15);
      b[2] = static_cast<std::uint8_t>((x + y) * 5);
      if (x < 20 && y < 12) {
        auto* t = top.pixel(x, y);
        t[0] = static_cast<std::uint8_t>(250 - x * 7);
        t[1] = static_cast<std::uint8_t>(40 + y * 9);
        t[2] = 90;
        t[3] = static_cast<std::uint8_t>(60 + x * 9);
      }
    }
  }
  document.add_pixel_layer("Background", background);
  auto& layer = document.add_pixel_layer("Top", PixelBuffer(24, 16, PixelFormat::rgba8()));
  layer.pixels() = top;
  layer.set_bounds(Rect{3, 2, 20, 12});
  LayerMask mask;
  mask.bounds = Rect{1, 1, 18, 13};
  mask.default_color = 255;
  mask.pixels = PixelBuffer(18, 13, PixelFormat::gray8());
  for (std::int32_t y = 0; y < 13; ++y) {
    for (std::int32_t x = 0; x < 18; ++x) {
      mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>(x * 14 + y);
    }
  }
  layer.set_mask(mask);
  PixelBuffer channel(24, 16, PixelFormat::gray8());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 24; ++x) {
      channel.pixel(x, y)[0] = static_cast<std::uint8_t>(255 - x * 9 - y);
    }
  }
  document.add_channel(DocumentChannel(document.allocate_channel_id(), "Alpha 1", DocumentChannelKind::Alpha, channel));
  return document;
}

// Largest sample difference between an 8-bit buffer and a deep one narrowed to 8 bits;
// -1 when their shapes differ.
bool same_rect(Rect a, Rect b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

int narrowed_difference(const PixelBuffer& eight, const PixelBuffer& deep, SampleKind kind) {
  if (eight.width() != deep.width() || eight.height() != deep.height() ||
      eight.format().channels != deep.format().channels) {
    return -1;
  }
  const auto narrowed = convert_pixel_buffer_depth(deep, BitDepth::UInt8, kind);
  int worst = 0;
  for (std::size_t i = 0; i < eight.byte_size(); ++i) {
    worst = std::max(worst, std::abs(static_cast<int>(eight.data()[i]) - static_cast<int>(narrowed.data()[i])));
  }
  return worst;
}

void geometry_operations_keep_deep_buffers_at_depth() {
  using Operation = std::function<void(Document&)>;
  const std::vector<std::pair<const char*, Operation>> operations{
      {"rotate clockwise", [](Document& d) { rotate_document_clockwise(d); }},
      {"rotate counterclockwise", [](Document& d) { rotate_document_counterclockwise(d); }},
      {"crop", [](Document& d) { CHECK(crop_document(d, Rect{2, 3, 15, 10})); }},
      {"crop past the canvas", [](Document& d) { CHECK(crop_document(d, Rect{-3, -2, 30, 20}, EditColor{200, 30, 30, 255})); }},
      {"rotated crop", [](Document& d) { CHECK(crop_document(d, Rect{2, 2, 18, 12}, 17.0, EditColor{255, 255, 255, 255})); }},
      {"rotate arbitrary", [](Document& d) { CHECK(rotate_document_arbitrary(d, 23.0, EditColor{10, 20, 30, 255})); }},
      {"canvas size", [](Document& d) { resize_canvas_and_layers(d, 30, 21, CanvasAnchor::Center, EditColor{40, 50, 60, 255}, true); }},
      {"canvas size keeping layers", [](Document& d) { resize_canvas_and_layers(d, 30, 21, CanvasAnchor::BottomRight); }},
      {"shift seams", [](Document& d) { wrap_offset_document(d, 7, 5); }},
      {"flip horizontal", [](Document& d) { (void)flip_layer_horizontal(d, d.layers()[1].id()); }},
      {"flip vertical", [](Document& d) { (void)flip_layer_vertical(d, d.layers()[1].id()); }},
  };
  for (const auto depth : {BitDepth::UInt16, BitDepth::Float32}) {
    for (const auto& [name, operation] : operations) {
      auto eight = geometry_document();
      auto deep = geometry_document();
      convert_document_depth(deep, depth);
      operation(eight);
      operation(deep);
      const auto problems = document_depth_problems(deep);
      // Rotated resampling interpolates at different precisions; everything else
      // moves whole pixels and must agree exactly.
      const bool resampled = std::string(name) == "rotated crop" || std::string(name) == "rotate arbitrary";
      const int allowed = resampled ? 1 : 0;
      bool ok = problems.empty() && eight.width() == deep.width() && eight.height() == deep.height();
      for (std::size_t i = 0; ok && i < eight.layers().size(); ++i) {
        const auto& a = std::as_const(eight).layers()[i];
        const auto& b = std::as_const(deep).layers()[i];
        ok = same_rect(a.bounds(), b.bounds());
        const auto color = narrowed_difference(a.pixels(), b.pixels(), SampleKind::Color);
        ok = ok && color >= 0 && color <= allowed;
        if (a.mask().has_value()) {
          ok = ok && b.mask().has_value() && same_rect(a.mask()->bounds, b.mask()->bounds) &&
               b.mask()->pixels.format().bit_depth == (b.mask()->pixels.empty() ? b.mask()->pixels.format().bit_depth : depth);
          const auto mask = narrowed_difference(a.mask()->pixels, b.mask()->pixels, SampleKind::Coverage);
          ok = ok && mask >= 0 && mask <= allowed;
        }
      }
      const auto channel = narrowed_difference(eight.channels()[0].pixels(), deep.channels()[0].pixels(),
                                               SampleKind::Coverage);
      ok = ok && channel >= 0 && channel <= allowed;
      if (!ok) {
        std::cerr << name << " at " << (depth == BitDepth::UInt16 ? 16 : 32) << " bits: deep result differs ("
                  << problems.size() << " depth problems)\n";
      }
      CHECK(ok);
    }
  }
}

void painting_writes_deep_layers_at_depth() {
  // Each painting write on an 8-bit layer and on its 16-bit conversion: the deep result,
  // narrowed, matches within a level (it rounds once instead of per write), the layer
  // stays at its depth, and 32 bits paints too.
  using Operation = std::function<Rect(Document&, LayerId)>;
  EditOptions options;
  options.primary = EditColor{200, 60, 30, 180};
  options.secondary = EditColor{20, 40, 220, 255};
  options.brush_size = 9;
  options.brush_softness = 70;
  const std::vector<std::pair<const char*, Operation>> operations{
      {"dab", [&](Document& d, LayerId id) { return paint_brush_dab(d, id, 12.3, 8.7, options, false); }},
      {"segment", [&](Document& d, LayerId id) { return paint_brush_segment(d, id, 2.0, 3.0, 21.0, 13.5, options, false); }},
      {"erase", [&](Document& d, LayerId id) { return paint_brush_segment(d, id, 3.0, 12.0, 20.0, 2.0, options, true); }},
      {"fill", [&](Document& d, LayerId id) {
         auto fill = options;
         fill.selection = Rect{4, 3, 11, 7};
         return fill_rect(d, id, Rect{0, 0, 24, 16}, fill);
       }},
      {"flood", [&](Document& d, LayerId id) {
         auto flood = options;
         flood.flood_tolerance = 40;
         return flood_fill(d, id, 5, 5, flood);
       }},
      {"gradient", [&](Document& d, LayerId id) {
         GradientOptions gradient;
         gradient.stops = {GradientStop{0.0F, EditColor{10, 200, 30, 255}}, GradientStop{1.0F, EditColor{240, 20, 90, 120}}};
         return draw_gradient(d, id, 1, 2, 22, 14, options, gradient);
       }},
      {"ellipse", [&](Document& d, LayerId id) {
         auto shape = options;
         shape.fill_shapes = true;
         return draw_ellipse(d, id, Rect{3, 2, 15, 11}, shape, false);
       }},
  };
  for (const auto& [name, operation] : operations) {
    auto eight = geometry_document();
    auto deep = geometry_document();
    convert_document_depth(deep, BitDepth::UInt16);
    auto linear = geometry_document();
    convert_document_depth(linear, BitDepth::Float32);
    const auto id = eight.layers()[1].id();
    const auto rect8 = operation(eight, id);
    const auto rect16 = operation(deep, deep.layers()[1].id());
    const auto rect32 = operation(linear, linear.layers()[1].id());
    bool ok = !rect8.empty() && !rect16.empty() && !rect32.empty() && document_depth_problems(deep).empty() &&
              document_depth_problems(linear).empty();
    const auto& a = std::as_const(eight).layers()[1];
    const auto& b = std::as_const(deep).layers()[1];
    ok = ok && same_rect(a.bounds(), b.bounds());
    const auto difference = narrowed_difference(a.pixels(), b.pixels(), SampleKind::Color);
    ok = ok && difference >= 0 && difference <= 1;
    if (!ok) {
      std::cerr << name << ": 16-bit painting differs (worst " << difference << ")\n";
    }
    CHECK(ok);
  }
}

void deep_editing_gate_can_be_overridden() {
  set_deep_editing_override(true);
  CHECK(deep_editing_enabled());
  set_deep_editing_override(false);
  CHECK(!deep_editing_enabled());
  set_deep_editing_override(std::nullopt);
  // Without PATCHY_DEEP_EDITING the default decides: on in the app, off in this harness.
  if (!environment_variable("PATCHY_DEEP_EDITING").has_value()) {
    CHECK(!deep_editing_enabled());
    set_deep_editing_default(true);
    CHECK(deep_editing_enabled());
    set_deep_editing_override(false);
    CHECK(!deep_editing_enabled());
    set_deep_editing_override(std::nullopt);
    set_deep_editing_default(false);
  }
}

// Exposure and Gamma toning before a 32-bit document leaves 32 bits:
// (v * 2^exposure)^(1/gamma) on pixel layers' color, alpha and masks untouched, and
// no change at exposure 0, gamma 1 or on other depths.
void hdr_toning_maps_linear_color_only() {
  Document document(2, 1, PixelFormat::rgba8());
  PixelBuffer pixels(2, 1, PixelFormat::rgba8());
  pixels.pixel(0, 0)[0] = 255;
  pixels.pixel(0, 0)[1] = 128;
  pixels.pixel(0, 0)[2] = 0;
  pixels.pixel(0, 0)[3] = 200;
  pixels.pixel(1, 0)[0] = 64;
  pixels.pixel(1, 0)[1] = 64;
  pixels.pixel(1, 0)[2] = 64;
  pixels.pixel(1, 0)[3] = 255;
  auto& layer = document.add_pixel_layer("Toned", pixels);
  PixelBuffer mask(2, 1, PixelFormat::gray8());
  mask.clear(90);
  layer.set_mask(LayerMask{Rect::from_size(2, 1), mask, 255, false});
  convert_document_depth(document, BitDepth::Float32);
  const auto read = [&document](std::int32_t x) {
    const auto& buffer = document.layers().front().pixels();
    return load_pixel(buffer.format(), buffer.pixel(x, 0));
  };
  const auto before0 = read(0);
  const auto before1 = read(1);
  tone_map_linear_document(document, 0.0, 1.0);
  CHECK(read(0) == before0 && read(1) == before1);
  tone_map_linear_document(document, 1.0, 2.0);
  const auto after0 = read(0);
  const auto after1 = read(1);
  for (std::size_t c = 0; c < 3U; ++c) {
    const auto expect0 = std::sqrt(static_cast<double>(before0[c]) / 255.0 * 2.0) * 255.0;
    const auto expect1 = std::sqrt(static_cast<double>(before1[c]) / 255.0 * 2.0) * 255.0;
    CHECK(std::abs(after0[c] - expect0) < 0.01);
    CHECK(std::abs(after1[c] - expect1) < 0.01);
  }
  CHECK(after0[3] == before0[3]);
  CHECK(std::abs(coverage_at(document.layers().front().mask()->pixels, 0, 0) - 90.0F / 255.0F) < 1e-6F);
  auto eight = document;
  convert_document_depth(eight, BitDepth::UInt8);
  const auto& eight_pixels = eight.layers().front().pixels();
  const std::vector<std::uint8_t> eight_before(eight_pixels.data().begin(), eight_pixels.data().end());
  tone_map_linear_document(eight, 2.0, 1.5);
  CHECK(std::equal(eight_before.begin(), eight_before.end(), eight.layers().front().pixels().data().begin()));
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
      {"pixel_depth_geometry_operations_keep_deep_buffers_at_depth",
       geometry_operations_keep_deep_buffers_at_depth},
      {"pixel_depth_painting_writes_deep_layers_at_depth", painting_writes_deep_layers_at_depth},
      {"pixel_depth_hdr_toning_maps_linear_color_only", hdr_toning_maps_linear_color_only},
      {"pixel_depth_gate_can_be_overridden", deep_editing_gate_can_be_overridden},
  };
}
