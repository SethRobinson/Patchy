// Paint.NET .pdn import (docs/pdn-format.md) and the MS-NRBF reader under it. Every
// committed test builds its bytes in code (tests/pdn_test_builder.hpp); public sample
// files are optional local fixtures (local-test-fixtures/pdn) checked against pypdn dumps.

#include "core/blend_math.hpp"
#include "core/blend_math_deep.hpp"
#include "core/document_depth.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/image_open_diagnosis.hpp"
#include "formats/layered_blend_support.hpp"
#include "formats/metadata/metadata_containers.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/nrbf_reader.hpp"
#include "formats/pdf_crypt.hpp"
#include "formats/pdf_png_writer.hpp"
#include "formats/pdn_document_io.hpp"
#include "formats/stb/stb_image.h"
#include "psd/psd_document_io.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "pdn_test_builder.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using patchy::BlendMode;
using patchy::Document;
using patchy::Layer;
using patchy::test::pdn_build::bgra_surface;
using patchy::test::pdn_build::build_pdn;
using patchy::test::pdn_build::LayerSpec;
using patchy::test::pdn_build::NrbfWriter;
using patchy::test::pdn_build::PdnSpec;
namespace nrbf = patchy::nrbf;

using Rgba = std::array<std::uint8_t, 4>;

Rgba pixel_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& pixels = std::as_const(layer).pixels();
  const auto* p = pixels.pixel(x, y);
  if (pixels.format().channels == 3) {
    return {p[0], p[1], p[2], 255};
  }
  return {p[0], p[1], p[2], p[3]};
}

Rgba gradient_pixel(std::int32_t x, std::int32_t y) {
  return {static_cast<std::uint8_t>(x * 40), static_cast<std::uint8_t>(y * 60), 200, 255};
}

PdnSpec three_layer_spec() {
  PdnSpec spec;
  spec.width = 5;
  spec.height = 3;
  LayerSpec background;
  background.name = "Background";
  background.is_background = true;
  background.bgra = bgra_surface(5, 3, gradient_pixel);
  LayerSpec middle;
  middle.name = "caf\xC3\xA9 \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88";  // "cafe" with an accent + katakana
  middle.opacity = 128;
  middle.blend = 1;  // Multiply
  middle.blend_op = "Multiply";
  middle.bgra = bgra_surface(5, 3, [](std::int32_t x, std::int32_t) -> Rgba {
    return {10, 20, 30, static_cast<std::uint8_t>(x * 50)};
  });
  LayerSpec top;
  top.name = "Hidden";
  top.visible = false;
  top.blend = 12;  // Screen
  top.blend_op = "Screen";
  top.bgra = bgra_surface(5, 3, [](std::int32_t, std::int32_t y) -> Rgba { return {255, 0, 0, static_cast<std::uint8_t>(y * 100)}; });
  spec.layers = {background, middle, top};
  return spec;
}

void check_three_layer_document(const Document& document) {
  CHECK(document.width() == 5);
  CHECK(document.height() == 3);
  const auto& layers = document.layers();
  CHECK(layers.size() == 3);
  CHECK(layers[0].name() == "Background");
  CHECK(layers[1].name() == "caf\xC3\xA9 \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88");
  CHECK(layers[2].name() == "Hidden");
  // The opaque bottom Background becomes Patchy's Background layer.
  CHECK(patchy::background_layer(layers, 5, 3) == &layers[0]);
  CHECK(layers[0].pixels().format().channels == 3);
  CHECK(layers[1].opacity() > 0.50F && layers[1].opacity() < 0.51F);
  CHECK(layers[1].blend_mode() == BlendMode::Multiply);
  CHECK(layers[2].blend_mode() == BlendMode::Screen);
  CHECK(layers[0].visible() && layers[1].visible() && !layers[2].visible());
  for (std::int32_t y = 0; y < 3; ++y) {
    for (std::int32_t x = 0; x < 5; ++x) {
      CHECK(pixel_at(layers[0], x, y) == gradient_pixel(x, y));
      CHECK((pixel_at(layers[1], x, y) == Rgba{10, 20, 30, static_cast<std::uint8_t>(x * 50)}));
      CHECK((pixel_at(layers[2], x, y) == Rgba{255, 0, 0, static_cast<std::uint8_t>(y * 100)}));
    }
  }
  CHECK(document.print_settings().horizontal_ppi == 96.0);
}

// --- NRBF -------------------------------------------------------------------

void pdn_nrbf_reads_every_record_type() {
  NrbfWriter w;
  using M = NrbfWriter;
  w.header(1);
  w.library(2, "Lib, Version=1.0");
  // ClassWithMembersAndTypes: every primitive plus string, object and class members.
  w.class_with_types(1, "Root",
                     {M::prim("b", 1), M::prim("u8", 2), M::prim("ch", 3), M::prim("dec", 5), M::prim("d", 6),
                      M::prim("i16", 7), M::prim("i32", 8), M::prim("i64", 9), M::prim("s8", 10),
                      M::prim("f", 11), M::prim("ts", 12), M::prim("dt", 13), M::prim("u16", 14),
                      M::prim("u32", 15), M::prim("u64", 16), M::str("text"), M::obj("forward"),
                      M::cls("inline", "Child", 2), M::obj("nothing"), M::obj("boxed"), M::obj("ints"),
                      M::obj("objects"), M::obj("strings"), M::obj("grid"), M::obj("untyped")},
                     2);
  w.u8(1);
  w.u8(200);
  w.u8(0xC3);  // a two-byte UTF-8 char
  w.u8(0xA9);
  w.string("12.5");
  const double d = 2.25;
  std::uint64_t d_bits = 0;
  std::memcpy(&d_bits, &d, 8);
  w.i64(static_cast<std::int64_t>(d_bits));
  w.u8(0xFE);  // int16 -2
  w.u8(0xFF);
  w.i32(-123456);
  w.i64(-9000000000LL);
  w.u8(0x80);  // sbyte -128
  const float f = 1.5F;
  std::uint32_t f_bits = 0;
  std::memcpy(&f_bits, &f, 4);
  w.i32(static_cast<std::int32_t>(f_bits));
  w.i64(600000000LL);
  w.i64(0x4000000000000001LL);
  w.u8(0x34);
  w.u8(0x12);
  w.i32(static_cast<std::int32_t>(0xFFFFFFF0U));
  w.i64(-1);  // u64 max
  w.string_object(3, "hello");
  w.reference(50);  // forward: defined after the root
  w.class_with_types(-4, "Child", {M::prim("value", 8)}, 2);
  w.i32(77);
  w.null();
  w.u8(8);  // MemberPrimitiveTyped Int32
  w.u8(8);
  w.i32(31);
  // ArraySinglePrimitive Int32[3]
  w.u8(15);
  w.i32(5);
  w.i32(3);
  w.u8(8);
  w.i32(1);
  w.i32(2);
  w.i32(3);
  // ArraySingleObject[6]: an inline ClassWithId, a reference, a null run of 3, a string
  w.object_array(6, 6);
  w.class_with_id(-7, -4);
  w.i32(88);
  w.reference(3);
  w.null_run(3);
  w.string_object(8, "tail");
  // ArraySingleString[2] with an ObjectNullMultiple (the 32-bit count form)
  w.u8(17);
  w.i32(9);
  w.i32(4);
  w.string_object(10, "s0");
  w.u8(14);
  w.i32(3);
  // BinaryArray rectangular Int32[2,3] (array type 2, rank 2, primitive Int32)
  w.u8(7);
  w.i32(11);
  w.u8(2);
  w.i32(2);
  w.i32(2);
  w.i32(3);
  w.u8(0);
  w.u8(8);
  for (int i = 0; i < 6; ++i) {
    w.i32(i * 10);
  }
  // ClassWithMembers (no type info): every member is a record.
  w.u8(3);
  w.i32(12);
  w.string("Untyped");
  w.i32(2);
  w.string("a");
  w.string("b");
  w.i32(2);
  w.u8(8);
  w.u8(1);  // MemberPrimitiveTyped Boolean
  w.u8(1);
  w.string_object(13, "bee");
  // The forward reference's target, then a SystemClassWithMembers.
  w.u8(2);
  w.i32(50);
  w.string("System.Thing");
  w.i32(1);
  w.string("only");
  w.null();
  w.message_end();
  const auto bytes = w.bytes;
  const auto graph = nrbf::parse(bytes);
  CHECK(graph.end_offset() == bytes.size());
  CHECK(graph.dangling_references() == 0);
  const auto* root = graph.root();
  CHECK(root != nullptr && root->class_name() == "Root");
  CHECK(graph.member_bool(*root, "b") == true);
  CHECK(graph.member_integer(*root, "u8") == 200);
  CHECK(graph.member_string(*root, "ch") == std::string_view("\xC3\xA9"));
  CHECK(graph.member_string(*root, "dec") == std::string_view("12.5"));
  CHECK(graph.member(*root, "d")->real == 2.25);
  CHECK(graph.member_integer(*root, "i16") == -2);
  CHECK(graph.member_integer(*root, "i32") == -123456);
  CHECK(graph.member_integer(*root, "i64") == -9000000000LL);
  CHECK(graph.member_integer(*root, "s8") == -128);
  CHECK(graph.member(*root, "f")->real == 1.5);
  CHECK(graph.member_integer(*root, "ts") == 600000000LL);
  CHECK(graph.member(*root, "dt")->unsigned_integer == 0x4000000000000001ULL);
  CHECK(graph.member_integer(*root, "u16") == 0x1234);
  CHECK(graph.member_integer(*root, "u32") == 0xFFFFFFF0LL);
  CHECK(graph.member(*root, "u64")->unsigned_integer == UINT64_MAX);
  CHECK(!graph.member_integer(*root, "u64").has_value());
  CHECK(graph.member_string(*root, "text") == std::string_view("hello"));
  const auto* forward = graph.member_object(*root, "forward");
  CHECK(forward != nullptr && forward->class_name() == "System.Thing" && forward->class_info->library_id == 0);
  const auto* child = graph.member_object(*root, "inline");
  CHECK(child != nullptr && graph.member_integer(*child, "value") == 77);
  CHECK(graph.member(*root, "nothing")->kind == nrbf::Value::Kind::Null);
  CHECK(graph.member_integer(*root, "boxed") == 31);
  const auto* ints = graph.member_object(*root, "ints");
  CHECK(ints != nullptr && ints->kind == nrbf::Object::Kind::PrimitiveArray && ints->element_count == 3 &&
        ints->element_type == nrbf::PrimitiveType::Int32 && ints->data_size == 12 &&
        bytes[ints->data_offset + 4] == 2);
  const auto* objects = graph.member_object(*root, "objects");
  CHECK(objects != nullptr && objects->kind == nrbf::Object::Kind::ObjectArray);
  const auto elements = graph.values(*objects);
  CHECK(elements.size() == 6);
  const auto* reused = graph.object(&elements[0]);
  CHECK(reused != nullptr && reused->class_name() == "Child" && graph.member_integer(*reused, "value") == 88);
  CHECK(graph.string_value(&elements[1]) == std::string_view("hello"));
  CHECK(elements[2].kind == nrbf::Value::Kind::Null && elements[4].kind == nrbf::Value::Kind::Null);
  CHECK(graph.string_value(&elements[5]) == std::string_view("tail"));
  const auto* strings = graph.member_object(*root, "strings");
  CHECK(strings != nullptr && graph.values(*strings).size() == 4 &&
        graph.string_value(&graph.values(*strings)[0]) == std::string_view("s0") &&
        graph.values(*strings)[3].kind == nrbf::Value::Kind::Null);
  const auto* grid = graph.member_object(*root, "grid");
  CHECK(grid != nullptr && grid->kind == nrbf::Object::Kind::PrimitiveArray && grid->element_count == 6 &&
        grid->lengths == std::vector<std::int32_t>({2, 3}));
  const auto* untyped = graph.member_object(*root, "untyped");
  CHECK(untyped != nullptr && graph.member_bool(*untyped, "a") == true &&
        graph.member_string(*untyped, "b") == std::string_view("bee"));
  CHECK(graph.libraries().at(2) == "Lib, Version=1.0");
}

void expect_parse_error(const std::vector<std::uint8_t>& bytes, const char* what) {
  try {
    static_cast<void>(nrbf::parse(bytes));
  } catch (const nrbf::ParseError&) {
    return;
  }
  throw std::runtime_error(std::string("expected an NRBF parse error: ") + what);
}

void pdn_nrbf_rejects_malformed_input() {
  // Every truncation of a valid stream fails cleanly.
  const auto valid = patchy::test::pdn_build::build_graph(three_layer_spec());
  const auto end = nrbf::parse(valid).end_offset();
  for (std::size_t length = 0; length < end; ++length) {
    expect_parse_error(std::vector<std::uint8_t>(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length)),
                       "truncated");
  }
  const auto with_header = [](const std::function<void(NrbfWriter&)>& body) {
    NrbfWriter w;
    w.header(1);
    body(w);
    return w.bytes;
  };
  expect_parse_error({1, 2, 3}, "no header");
  expect_parse_error(with_header([](NrbfWriter& w) { w.u8(99); }), "unknown record");
  expect_parse_error(with_header([](NrbfWriter& w) { w.u8(21); }), "method call");
  expect_parse_error(with_header([](NrbfWriter& w) { w.null_run(2); }), "null run at top level");
  // A null run inside a class member.
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.class_with_types(1, "C", {NrbfWriter::obj("m")}, 0);
                       w.null_run(1);
                       w.message_end();
                     }),
                     "null run as member");
  // A null run longer than its array.
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.object_array(1, 2);
                       w.null_run(3);
                       w.message_end();
                     }),
                     "null run overrun");
  // Repeated object id.
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.string_object(5, "a");
                       w.string_object(5, "b");
                       w.message_end();
                     }),
                     "repeated id");
  // ClassWithId naming metadata that never appeared.
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.class_with_id(2, 40);
                       w.message_end();
                     }),
                     "unknown metadata");
  // Length bombs: a string, an object array and a primitive array claiming far more
  // than the stream holds; a null run covering two billion slots.
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.u8(6);
                       w.i32(1);
                       w.u8(0xFF);
                       w.u8(0xFF);
                       w.u8(0xFF);
                       w.u8(0xFF);
                       w.u8(0x07);
                     }),
                     "string bomb");
  expect_parse_error(with_header([](NrbfWriter& w) { w.object_array(1, 0x7FFFFFFF); }), "object array bomb");
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.object_array(1, 0x7FFFFFFF);
                       w.u8(14);
                       w.i32(0x7FFFFFFF);
                       w.message_end();
                     }),
                     "null run bomb");
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.u8(15);
                       w.i32(1);
                       w.i32(0x7FFFFFFF);
                       w.u8(9);
                     }),
                     "primitive array bomb");
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.u8(7);
                       w.i32(1);
                       w.u8(2);
                       w.i32(3);
                       w.i32(0x7FFFFFFF);
                       w.i32(0x7FFFFFFF);
                       w.i32(0x7FFFFFFF);
                       w.u8(0);
                       w.u8(8);
                     }),
                     "rank product bomb");
  // Nesting deeper than the limit: 200 inline classes, each the only member of the last.
  expect_parse_error(with_header([](NrbfWriter& w) {
                       w.class_with_types(1, "Deep", {NrbfWriter::obj("next")}, 0);
                       for (int i = 0; i < 200; ++i) {
                         w.class_with_id(-(i + 2), 1);
                       }
                     }),
                     "depth bomb");
  CHECK(!nrbf::looks_like_stream(std::vector<std::uint8_t>{0, 1, 0, 0, 0}));
  CHECK(nrbf::looks_like_stream(valid));
}

// --- the container and layers -----------------------------------------------

void pdn_reads_layers_properties_and_pixels() {
  const auto bytes = build_pdn(three_layer_spec());
  std::vector<std::string> notices;
  const auto document = patchy::pdn::DocumentIo::read(bytes, &notices);
  check_three_layer_document(document);
  CHECK(notices.empty());
  CHECK(document.active_layer_id() == document.layers().back().id());
  const auto header = patchy::pdn::read_header(bytes);
  CHECK(header.has_value() && header->width == 5 && header->height == 3 && header->layer_count == 3 &&
        header->saved_with_version == "4.21.6589.7045");
}

void pdn_reads_out_of_order_raw_and_small_chunks() {
  for (const std::uint8_t format : {std::uint8_t{0}, std::uint8_t{1}}) {
    auto spec = three_layer_spec();
    spec.chunk_format = format;
    spec.chunk_size = 16;  // 60-byte surfaces: four chunks each, the last one short
    spec.reverse_chunks = true;
    std::vector<std::string> notices;
    check_three_layer_document(patchy::pdn::DocumentIo::read(build_pdn(spec), &notices));
    CHECK(notices.empty());
  }
}

void pdn_reads_padded_strides_and_shared_blocks() {
  auto spec = three_layer_spec();
  for (auto& layer : spec.layers) {
    // Re-encode each surface with 4 bytes of row padding.
    std::vector<std::uint8_t> padded;
    for (std::int32_t y = 0; y < 3; ++y) {
      padded.insert(padded.end(), layer.bgra.begin() + y * 20, layer.bgra.begin() + (y + 1) * 20);
      padded.insert(padded.end(), {1, 2, 3, 4});
    }
    layer.bgra = padded;
    layer.stride = 24;
  }
  check_three_layer_document(patchy::pdn::DocumentIo::read(build_pdn(spec)));

  // Every surface pointing at one MemoryBlock: its data is written once and shared.
  auto shared = three_layer_spec();
  shared.share_first_block = true;
  const auto document = patchy::pdn::DocumentIo::read(build_pdn(shared));
  CHECK(document.layers().size() == 3);
  for (std::int32_t x = 0; x < 5; ++x) {
    CHECK(pixel_at(document.layers()[2], x, 1) == gradient_pixel(x, 1));
  }
}

void pdn_reads_legacy_blend_op_classes_and_unknown_modes() {
  auto spec = three_layer_spec();
  spec.write_blend_mode = false;  // Paint.NET 3.x: only the blend-op class says the mode
  spec.layers[1].blend_op = "Darken";
  spec.layers[2].blend_op = "Xor";
  const auto document = patchy::pdn::DocumentIo::read(build_pdn(spec));
  CHECK(document.layers()[0].blend_mode() == BlendMode::Normal);
  CHECK(document.layers()[1].blend_mode() == BlendMode::Darken);
  CHECK(document.layers()[2].blend_mode() == BlendMode::Xor);

  // The 4.x enum wins over the class when both are present; every enum value maps.
  const std::array<BlendMode, 14> known{BlendMode::Normal,     BlendMode::Multiply,   BlendMode::LinearDodge,
                                        BlendMode::ColorBurn,  BlendMode::ColorDodge, BlendMode::Reflect,
                                        BlendMode::Glow,       BlendMode::Overlay,    BlendMode::Difference,
                                        BlendMode::Negation,   BlendMode::Lighten,    BlendMode::Darken,
                                        BlendMode::Screen,     BlendMode::Xor};
  const std::array<int, 14> indices{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  for (std::size_t i = 0; i < known.size(); ++i) {
    auto mapped = three_layer_spec();
    mapped.layers[1].blend = indices[i];
    mapped.layers[1].blend_op = "Normal";
    std::vector<std::string> notices;
    CHECK(patchy::pdn::DocumentIo::read(build_pdn(mapped), &notices).layers()[1].blend_mode() == known[i]);
    CHECK(notices.empty());
  }
  auto unknown = three_layer_spec();
  unknown.layers[1].blend = 77;
  std::vector<std::string> notices;
  CHECK(patchy::pdn::DocumentIo::read(build_pdn(unknown), &notices).layers()[1].blend_mode() == BlendMode::Normal);
  CHECK(notices.size() == 1);
}

void pdn_reads_gzipped_graphs_and_headerless_legacy_files() {
  auto spec = three_layer_spec();
  spec.gzip_whole_graph = true;
  check_three_layer_document(patchy::pdn::DocumentIo::read(build_pdn(spec)));
  spec.gzip_whole_graph = false;
  spec.omit_header = true;
  const auto legacy = build_pdn(spec);
  CHECK(!patchy::pdn::sniff(legacy));
  check_three_layer_document(patchy::pdn::DocumentIo::read(legacy));
}

std::pair<std::string, std::string> exif_item(int index, int id, int type, const std::vector<std::uint8_t>& value) {
  return {"$exif.tag" + std::to_string(index),
          "<exif id=\"" + std::to_string(id) + "\" len=\"" + std::to_string(value.size()) + "\" type=\"" +
              std::to_string(type) + "\" value=\"" + patchy::test::pdn_build::base64(value) + "\" />"};
}

std::vector<std::uint8_t> rational(std::uint32_t numerator, std::uint32_t denominator) {
  std::vector<std::uint8_t> out;
  for (const auto value : {numerator, denominator}) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
    }
  }
  return out;
}

void pdn_reads_resolution_from_exif_metadata() {
  auto spec = three_layer_spec();
  spec.metadata = {exif_item(1, 296, 3, {3, 0}), exif_item(2, 282, 5, rational(118110, 1000)),
                   exif_item(3, 283, 5, rational(37795, 1000))};
  const auto document = patchy::pdn::DocumentIo::read(build_pdn(spec));
  CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 0.01);  // 118.11 px/cm
  CHECK(std::abs(document.print_settings().vertical_ppi - 96.0) < 0.01);
  CHECK(document.print_settings().horizontal_resolution_display_unit == 2);
  spec.metadata = {exif_item(1, 296, 3, {2, 0}), exif_item(2, 282, 5, rational(72, 1)),
                   exif_item(3, 283, 5, rational(72, 1))};
  const auto inch = patchy::pdn::DocumentIo::read(build_pdn(spec));
  CHECK(inch.print_settings().horizontal_ppi == 72.0 && inch.print_settings().vertical_ppi == 72.0);
  CHECK(inch.print_settings().horizontal_resolution_display_unit == 1);
}

std::vector<std::uint8_t> small_png() {
  std::vector<std::uint8_t> rgba(2 * 2 * 4, 0);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = 250;
    rgba[i + 3] = 255;
  }
  return patchy::formats::encode_png_rgba8(rgba, 2, 2);
}

void pdn_broken_graph_falls_back_to_the_thumbnail() {
  auto spec = three_layer_spec();
  spec.thumbnail_png = small_png();
  auto bytes = build_pdn(spec);
  const auto header = patchy::pdn::read_header(bytes);
  // Damage the root's class name: the graph still parses, but it is not a document.
  const auto at = std::search(bytes.begin(), bytes.end(), std::begin("PaintDotNet.Document") ,
                              std::end("PaintDotNet.Document") - 1);
  CHECK(at != bytes.end());
  *(at + 12) = 'X';
  std::vector<std::string> notices;
  auto document = patchy::pdn::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 2 && document.height() == 2 && document.layers().size() == 1);
  CHECK(pixel_at(document.layers()[0], 1, 1) == (Rgba{250, 0, 0, 255}));
  CHECK(notices.size() == 1 && notices[0].find("embedded preview") != std::string::npos);

  // Cut inside the graph: same fallback.
  bytes.resize(header->payload_offset + 40);
  notices.clear();
  document = patchy::pdn::DocumentIo::read(bytes, &notices);
  CHECK(document.width() == 2 && notices.size() == 1);

  // No thumbnail either: a clean error.
  spec.thumbnail_png.clear();
  auto bare = build_pdn(spec);
  bare.resize(patchy::pdn::read_header(bare)->payload_offset + 40);
  bool threw = false;
  try {
    static_cast<void>(patchy::pdn::DocumentIo::read(bare));
  } catch (const std::runtime_error& error) {
    threw = std::string(error.what()).find("damaged") != std::string::npos;
  }
  CHECK(threw);
}

void pdn_damaged_chunks_open_transparent_with_a_notice() {
  auto spec = three_layer_spec();
  spec.chunk_size = 16;
  auto bytes = build_pdn(spec);
  // The middle layer's first chunk: flip bytes inside its deflate body.
  const auto graph = patchy::test::pdn_build::build_graph(spec);
  const auto graph_start = bytes.size() - graph.size();
  const auto deferred = graph_start + nrbf::parse(graph).end_offset();
  // Skip the first block (5 header bytes + 4 chunks) to reach the second block's first chunk.
  std::size_t position = deferred + 5;
  for (int chunk = 0; chunk < 4; ++chunk) {
    const auto stored = (static_cast<std::size_t>(bytes[position + 4]) << 24U) |
                        (static_cast<std::size_t>(bytes[position + 5]) << 16U) |
                        (static_cast<std::size_t>(bytes[position + 6]) << 8U) | bytes[position + 7];
    position += 8 + stored;
  }
  position += 5 + 8;  // second block header, first chunk header
  for (std::size_t i = 10; i < 16; ++i) {
    bytes[position + i] ^= 0x5A;
  }
  std::vector<std::string> notices;
  const auto document = patchy::pdn::DocumentIo::read(bytes, &notices);
  CHECK(document.layers().size() == 3);
  CHECK(notices.size() == 1 && notices[0].find("damaged") != std::string::npos);
  CHECK(pixel_at(document.layers()[0], 4, 2) == gradient_pixel(4, 2));
  CHECK((pixel_at(document.layers()[2], 1, 2) == Rgba{255, 0, 0, 200}));
}

void pdn_mutation_sweep_never_crashes() {
  auto spec = three_layer_spec();
  spec.thumbnail_png = small_png();
  spec.chunk_size = 32;
  const auto valid = build_pdn(spec);
  std::size_t opened = 0;
  std::size_t refused = 0;
  const auto attempt = [&](const std::vector<std::uint8_t>& bytes) {
    try {
      const auto document = patchy::pdn::DocumentIo::read(bytes);
      CHECK(document.width() > 0 && document.height() > 0 && !document.layers().empty());
      ++opened;
    } catch (const std::exception&) {
      ++refused;
    }
  };
  for (std::size_t length = 0; length < valid.size(); length += 7) {
    attempt(std::vector<std::uint8_t>(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length)));
  }
  std::uint64_t state = 0x9E3779B97F4A7C15ULL;
  const auto next = [&] {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  };
  for (int round = 0; round < 600; ++round) {
    auto bytes = valid;
    const auto flips = 1 + next() % 4;
    for (std::uint64_t i = 0; i < flips; ++i) {
      const auto at = static_cast<std::size_t>(next() % bytes.size());
      // Mostly bit flips; sometimes a 0xFF run, which turns counts and lengths into bombs.
      if (round % 5 == 0 && at + 4 <= bytes.size()) {
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(at), 4, std::uint8_t{0xFF});
      } else {
        bytes[at] ^= static_cast<std::uint8_t>(1U << (next() % 8));
      }
    }
    attempt(bytes);
  }
  std::cout << "  mutation sweep: " << opened << " opened, " << refused << " refused\n";
  CHECK(opened > 0 && refused > 0);
}

void pdn_registry_sniff_and_open_by_content_detection() {
  const auto bytes = build_pdn(three_layer_spec());
  const auto* handler = patchy::builtin_format_registry().find_by_extension(".pdn");
  CHECK(handler != nullptr && handler->identifier == "patchy.formats.pdn" && !handler->can_write());
  CHECK(handler->sniff && handler->sniff(bytes));
  check_three_layer_document(handler->read(bytes).document);
  const auto sniffed = patchy::formats::sniff_openable_image_format(bytes);
  CHECK(sniffed.has_value() && sniffed->extension == "pdn");
  CHECK(!patchy::pdn::sniff(std::vector<std::uint8_t>{'P', 'D', 'N', '2', 0, 0, 0}));
}

void pdn_unicode_path_opens() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"pdn");
  const auto bytes = build_pdn(three_layer_spec());
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto path = dir / patchy::test::unicode_path_piece(std::u8string(stem) + u8".pdn");
    {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    check_three_layer_document(patchy::pdn::DocumentIo::read_file(path));
  }
}

// Found by the corpus (#379 follow-up): Paint.NET saves canvases past the 2^28-pixel cap
// the other importers share (25600 x 14400 files on GitHub). Up to 2^30 pixels per layer
// opens; past it the preview opens with a size notice rather than "could not be read", and
// a file that is only the "PDN3" magic (a signature sample) is reported as damaged.
void pdn_size_limits_and_stub_files_explain_themselves() {
  auto spec = three_layer_spec();
  spec.thumbnail_png = small_png();
  spec.width = 40000;  // 1.6 gigapixels: past the per-layer limit
  spec.height = 40000;
  std::vector<std::string> notices;
  const auto document = patchy::pdn::DocumentIo::read(build_pdn(spec), &notices);
  CHECK(document.width() == 2 && document.height() == 2);
  CHECK(notices.size() == 1 && notices[0].find("too large") != std::string::npos);

  for (const std::vector<std::uint8_t>& stub :
       {std::vector<std::uint8_t>{'P', 'D', 'N', '3'}, std::vector<std::uint8_t>{'P', 'D', 'N', '3', 0x10}}) {
    std::string message;
    try {
      static_cast<void>(patchy::pdn::DocumentIo::read(stub));
    } catch (const std::runtime_error& error) {
      message = error.what();
    }
    CHECK(message.find("damaged") != std::string::npos);
  }
}

// --- public samples -------------------------------------------------------

std::vector<std::uint8_t> rgba_bytes(const Layer& layer) {
  const auto& pixels = std::as_const(layer).pixels();
  const auto source = pixels.data();
  if (pixels.format().channels == 4) {
    return {source.begin(), source.end()};
  }
  // The Background imports without alpha: put the opaque alpha back for the comparison.
  const auto count = static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height());
  std::vector<std::uint8_t> out(count * 4U, 255);
  for (std::size_t i = 0; i < count; ++i) {
    out[i * 4U] = source[i * 3U];
    out[i * 4U + 1U] = source[i * 3U + 1U];
    out[i * 4U + 2U] = source[i * 3U + 2U];
  }
  return out;
}

std::optional<BlendMode> expected_blend(int index) {
  switch (index) {
    case 0: return BlendMode::Normal;
    case 1: return BlendMode::Multiply;
    case 2: return BlendMode::LinearDodge;
    case 3: return BlendMode::ColorBurn;
    case 4: return BlendMode::ColorDodge;
    case 5: return BlendMode::Reflect;
    case 6: return BlendMode::Glow;
    case 7: return BlendMode::Overlay;
    case 8: return BlendMode::Difference;
    case 9: return BlendMode::Negation;
    case 10: return BlendMode::Lighten;
    case 11: return BlendMode::Darken;
    case 12: return BlendMode::Screen;
    case 13: return BlendMode::Xor;
    default: return BlendMode::Normal;
  }
}

// Each local-test-fixtures/pdn/<name>.pdn beside a <name>.oracle.txt (pypdn's view of the
// same file: per-layer FNV-1a of the RGBA bytes and the layer properties).
void pdn_public_samples_match_pypdn_if_available() {
  const auto dir = patchy::test::local_format_fixture_path("pdn", "");
  patchy::test::skip_unless_exists(dir);
  std::size_t checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".txt" || !entry.path().stem().string().ends_with(".oracle")) {
      continue;
    }
    auto pdn_path = entry.path();
    pdn_path.replace_extension();  // name.oracle
    pdn_path.replace_extension(".pdn");
    if (!std::filesystem::exists(pdn_path)) {
      continue;
    }
    std::ifstream oracle(entry.path());
    std::string line;
    std::int32_t width = 0;
    std::int32_t height = 0;
    struct Expected {
      std::uint64_t hash;
      bool visible;
      bool background;
      int opacity;
      int blend;
      std::string name;
    };
    std::vector<Expected> expected;
    while (std::getline(oracle, line)) {
      std::istringstream fields(line);
      std::string kind;
      fields >> kind;
      if (kind == "size") {
        fields >> width >> height;
      } else if (kind == "layer") {
        Expected layer{};
        std::string hash;
        int visible = 0;
        int background = 0;
        fields >> hash >> visible >> background >> layer.opacity >> layer.blend;
        layer.hash = std::stoull(hash, nullptr, 16);
        layer.visible = visible != 0;
        layer.background = background != 0;
        std::getline(fields, layer.name);
        if (!layer.name.empty() && layer.name.front() == ' ') {
          layer.name.erase(0, 1);
        }
        expected.push_back(std::move(layer));
      }
    }
    std::vector<std::string> notices;
    const auto started = std::chrono::steady_clock::now();
    const auto document = patchy::pdn::DocumentIo::read_file(pdn_path, &notices);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::cout << "  " << pdn_path.filename().string() << ": " << document.width() << 'x' << document.height() << ", "
              << document.layers().size() << " layers, " << static_cast<int>(elapsed) << " ms";
    for (const auto& notice : notices) {
      std::cout << " | " << notice;
    }
    std::cout << '\n';
    CHECK(document.width() == width && document.height() == height);
    CHECK(document.layers().size() == expected.size());
    for (std::size_t i = 0; i < expected.size() && i < document.layers().size(); ++i) {
      const auto& layer = document.layers()[i];
      const auto& want = expected[i];
      CHECK(layer.name() == want.name);
      CHECK(layer.visible() == want.visible);
      CHECK(std::abs(layer.opacity() * 255.0F - static_cast<float>(want.opacity)) < 0.01F);
      CHECK(layer.blend_mode() == expected_blend(want.blend));
      if (patchy::test::fnv1a_hash_bytes(rgba_bytes(layer)) != want.hash) {
        throw std::runtime_error(pdn_path.filename().string() + " layer " + std::to_string(i) +
                                 " pixels differ from pypdn's");
      }
    }
    ++checked;
  }
  if (checked == 0) {
    patchy::test::skip("no .pdn samples with oracle dumps in local-test-fixtures/pdn");
  }
}


// EXIF that Paint.NET keeps as "$exif.tag<N>" user metadata (both the 4.x key form and
// 5.x's "$exif.tag<N>[0]") reaches File Info as one TIFF stream, IFD0, Exif and GPS
// tags in their own directories; other user metadata is shown, not written.
void pdn_exif_metadata_reaches_file_info() {
  auto spec = three_layer_spec();
  const std::string software = "Paint.NET 5.1.12";
  std::vector<std::uint8_t> software_bytes(software.begin(), software.end());
  software_bytes.push_back(0);
  spec.metadata = {
      {"$exif.tag0[0]", exif_item(0, 305, 2, software_bytes).second},
      exif_item(1, 296, 3, {2, 0}),
      exif_item(2, 282, 5, rational(300, 1)),
      exif_item(3, 283, 5, rational(300, 1)),
      exif_item(4, 33434, 5, rational(1, 250)),  // ExposureTime (Exif IFD)
      exif_item(5, 1, 2, {'N', 0}),              // GPSLatitudeRef
      {"Notes", "hello"},
      // Seen in the corpus: the XMP of an image Paint.NET opened, and its palette.
      {"$xmp.packet0", "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
                       "</x:xmpmeta><?xpacket end=\"w\"?>"},
      {"$user.Palette", "4278190335,4293787886,4293853423"},
  };
  const auto bytes = build_pdn(spec);
  const auto document = patchy::pdn::DocumentIo::read(bytes);
  CHECK(document.print_settings().horizontal_ppi == 300.0);
  const auto info = patchy::metadata::extract_file_info(bytes);
  CHECK(info.source_format == "PDN");
  CHECK(info.exif != nullptr);
  const auto stream = patchy::metadata::parse_tiff(*info.exif);
  CHECK(stream.has_value());
  const auto* ifd0 = stream->find(patchy::metadata::IfdKind::Ifd0);
  const auto* exif = stream->find(patchy::metadata::IfdKind::Exif);
  const auto* gps = stream->find(patchy::metadata::IfdKind::Gps);
  CHECK(ifd0 != nullptr && exif != nullptr && gps != nullptr);
  CHECK(stream->string_value(*ifd0->find(305)) == software);
  CHECK(stream->rational_value(*ifd0->find(282)) == (std::pair<std::int64_t, std::int64_t>{300, 1}));
  CHECK(stream->rational_value(*exif->find(33434)) == (std::pair<std::int64_t, std::int64_t>{1, 250}));
  CHECK(stream->string_value(*gps->find(1)) == "N");
  const auto notes = std::find_if(info.observed.begin(), info.observed.end(),
                                  [](const auto& item) { return item.label == "Notes"; });
  CHECK(notes != info.observed.end() && notes->summary == "hello");
  CHECK(info.xmp != nullptr && std::string(info.xmp->begin(), info.xmp->end()).find("x:xmpmeta") != std::string::npos);
  const auto palette = std::find_if(info.observed.begin(), info.observed.end(),
                                    [](const auto& item) { return item.label == "Palette"; });
  CHECK(palette != info.observed.end() && palette->summary == "3 colors");
}

// --- Paint.NET's blend modes ------------------------------------------------

struct BlendSample {
  std::uint8_t backdrop;
  std::uint8_t layer;
  std::uint8_t result;
};

// Opaque pixels of pypdn's FlattenBlendTest.pdn and the per-mode PNGs Paint.NET 4.21
// flattened from it (tests/data in pypdn): {backdrop, layer, Paint.NET's result}.
constexpr BlendSample kReflectSamples[] = {
    {35, 133, 10},   {35, 155, 12},   {35, 182, 16},   {35, 199, 21},   {35, 216, 31},   {35, 229, 47},
    {35, 236, 64},   {128, 3, 65},    {128, 33, 73},   {128, 68, 87},   {128, 98, 104},  {128, 117, 118},
    {128, 129, 130}, {128, 136, 137}, {128, 180, 218}, {128, 192, 255}, {128, 254, 255}, {255, 255, 255}};
constexpr BlendSample kGlowSamples[] = {
    {35, 136, 84},  {35, 157, 112}, {35, 184, 153}, {35, 197, 176},  {35, 228, 236},  {35, 238, 255},
    {128, 2, 0},    {128, 17, 2},   {128, 43, 14},  {128, 78, 47},   {128, 106, 88},  {128, 126, 125},
    {128, 143, 161}, {128, 162, 206}, {128, 170, 227}, {128, 199, 255}, {128, 239, 255}, {255, 255, 255}};
constexpr BlendSample kNegationSamples[] = {{35, 30, 65}, {128, 50, 178}, {255, 200, 55}};
constexpr BlendSample kXorSamples[] = {{35, 187, 152}, {128, 112, 240}, {255, 200, 55}};

void blend_math_paint_dot_net_modes_match_paint_net_renders() {
  const auto check_samples = [](BlendMode mode, std::span<const BlendSample> samples) {
    for (const auto& sample : samples) {
      const auto out = patchy::blend_rgb({sample.layer, sample.layer, sample.layer},
                                         {sample.backdrop, sample.backdrop, sample.backdrop}, mode);
      if (out[0] != sample.result) {
        throw std::runtime_error("blend mode " + std::to_string(static_cast<int>(mode)) + " gives " +
                                 std::to_string(out[0]) + " for backdrop " + std::to_string(sample.backdrop) +
                                 ", layer " + std::to_string(sample.layer) + "; Paint.NET gives " +
                                 std::to_string(sample.result));
      }
    }
  };
  check_samples(BlendMode::Reflect, kReflectSamples);
  check_samples(BlendMode::Glow, kGlowSamples);
  check_samples(BlendMode::Negation, kNegationSamples);
  check_samples(BlendMode::Xor, kXorSamples);
  // Whole-table properties: Glow is Reflect with the operands swapped, Negation and Xor
  // are symmetric, and the 16/32-bit kernels agree with the byte kernels within a level.
  for (int a = 0; a < 256; ++a) {
    for (int b = 0; b < 256; ++b) {
      const auto s = static_cast<std::uint8_t>(a);
      const auto d = static_cast<std::uint8_t>(b);
      CHECK(patchy::blend_rgb({s, s, s}, {d, d, d}, BlendMode::Glow)[0] ==
            patchy::blend_rgb({d, d, d}, {s, s, s}, BlendMode::Reflect)[0]);
      CHECK(patchy::blend_rgb({s, s, s}, {d, d, d}, BlendMode::Negation)[0] ==
            patchy::blend_rgb({d, d, d}, {s, s, s}, BlendMode::Negation)[0]);
      CHECK(patchy::blend_rgb({s, s, s}, {d, d, d}, BlendMode::Xor)[0] == (a ^ b));
      for (const auto mode : {BlendMode::Reflect, BlendMode::Glow, BlendMode::Negation, BlendMode::Xor}) {
        const auto byte = patchy::blend_rgb({s, s, s}, {d, d, d}, mode)[0];
        const auto deep = patchy::blend_rgb_deep({static_cast<float>(a), static_cast<float>(a), static_cast<float>(a)},
                                                 {static_cast<float>(b), static_cast<float>(b), static_cast<float>(b)},
                                                 mode, patchy::DeepDomain::Encoded)[0];
        CHECK(std::abs(deep - static_cast<float>(byte)) <= 1.0F);
      }
    }
  }
  CHECK(patchy::blend_mode_is_paint_dot_net_only(BlendMode::Xor));
  CHECK(!patchy::blend_mode_is_paint_dot_net_only(BlendMode::Exclusion));
  CHECK(!patchy::blend_mode_supported_at_depth(BlendMode::Reflect, patchy::BitDepth::Float32));
  CHECK(patchy::blend_mode_supported_at_depth(BlendMode::Reflect, patchy::BitDepth::UInt16));
}

void psd_saves_paint_dot_net_blend_modes_as_normal() {
  Document document(4, 2, patchy::PixelFormat::rgba8());
  const std::array modes{BlendMode::Reflect, BlendMode::Glow, BlendMode::Negation, BlendMode::Xor,
                         BlendMode::Multiply};
  for (const auto mode : modes) {
    auto& layer = document.add_pixel_layer("layer", patchy::test::solid_rgba(4, 2, 100, 150, 200, 255));
    layer.set_blend_mode(mode);
  }
  CHECK(patchy::layers_use_paint_dot_net_blend_modes(document.layers()));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  CHECK(reread.layers().size() == modes.size());
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(reread.layers()[i].blend_mode() == BlendMode::Normal);
  }
  CHECK(reread.layers()[4].blend_mode() == BlendMode::Multiply);
  CHECK(!patchy::layers_use_paint_dot_net_blend_modes(reread.layers()));
}

// Layered saves (docs/blend-modes.md, "Layered saves"): which modes each format stores.
void layered_blend_support_lists_what_each_format_stores() {
  using patchy::LayeredSaveFormat;
  using patchy::layered_format_stores_blend_mode;
  constexpr auto u8 = patchy::BitDepth::UInt8;
  CHECK(!layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::Xor, false, u8));
  CHECK(!layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::Reflect, true, u8));
  CHECK(layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::Multiply, false, u8));
  CHECK(layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::Dissolve, false, u8));
  CHECK(layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::PassThrough, true, u8));
  CHECK(!layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::PassThrough, false, u8));
  CHECK(!layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::Screen, false,
                                          patchy::BitDepth::Float32));
  CHECK(layered_format_stores_blend_mode(LayeredSaveFormat::Photoshop, BlendMode::Screen, false,
                                         patchy::BitDepth::UInt16));
  for (const auto mode : {BlendMode::LinearBurn, BlendMode::PinLight, BlendMode::VividLight, BlendMode::LinearLight,
                          BlendMode::HardMix, BlendMode::DarkerColor, BlendMode::LighterColor, BlendMode::Dissolve,
                          BlendMode::Xor, BlendMode::Glow, BlendMode::PassThrough}) {
    CHECK(!layered_format_stores_blend_mode(LayeredSaveFormat::Aseprite, mode, true, u8));
  }
  for (const auto mode : {BlendMode::Normal, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay,
                          BlendMode::Difference, BlendMode::Hue, BlendMode::Luminosity}) {
    CHECK(layered_format_stores_blend_mode(LayeredSaveFormat::Aseprite, mode, false, u8));
  }

  Document document(4, 4, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Base", patchy::test::solid_rgba(4, 4, 10, 20, 30, 255));
  auto& xor_layer = document.add_pixel_layer("Xor one", patchy::test::solid_rgba(4, 4, 200, 20, 30, 255));
  xor_layer.set_blend_mode(BlendMode::Xor);
  auto& hidden = document.add_pixel_layer("Hidden glow", patchy::test::solid_rgba(4, 4, 200, 20, 30, 255));
  hidden.set_blend_mode(BlendMode::Glow);
  hidden.set_visible(false);
  auto& burn = document.add_pixel_layer("Burn", patchy::test::solid_rgba(4, 4, 200, 20, 30, 255));
  burn.set_blend_mode(BlendMode::LinearBurn);
  Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.set_blend_mode(BlendMode::PassThrough);
  folder.children().emplace_back(document.allocate_layer_id(), "Plain", patchy::test::solid_rgba(4, 4, 1, 2, 3, 255));
  document.add_layer(folder);

  const auto psd = patchy::find_unsupported_blend_modes(document, LayeredSaveFormat::Photoshop);
  CHECK((psd.modes == std::vector<BlendMode>{BlendMode::Xor, BlendMode::Glow}));
  CHECK((psd.layer_names == std::vector<std::string>{"Xor one", "Hidden glow"}));
  // A Pass Through folder of Normal layers isolates to the same pixels: Aseprite keeps it.
  const auto ase = patchy::find_unsupported_blend_modes(document, LayeredSaveFormat::Aseprite);
  CHECK((ase.modes == std::vector<BlendMode>{BlendMode::Xor, BlendMode::Glow, BlendMode::LinearBurn}));
  CHECK(ase.layer_names.size() == 3);
  document.layers().back().children().front().set_blend_mode(BlendMode::Multiply);
  const auto ase_multiply = patchy::find_unsupported_blend_modes(document, LayeredSaveFormat::Aseprite);
  CHECK(ase_multiply.layer_names.size() == 4 && ase_multiply.layer_names.back() == "Folder");
  CHECK(patchy::find_unsupported_blend_modes(Document(2, 2, patchy::PixelFormat::rgba8()), LayeredSaveFormat::Photoshop)
            .empty());
}

patchy::PixelBuffer bake_test_pattern(std::int32_t width, std::int32_t height, int seed, bool partial_alpha) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  auto data = pixels.data();
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* p = data.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
      p[0] = static_cast<std::uint8_t>((x * 37 + y * 11 + seed * 53) & 255);
      p[1] = static_cast<std::uint8_t>((x * 5 + y * 29 + seed * 17) & 255);
      p[2] = static_cast<std::uint8_t>((x * y + seed * 91) & 255);
      p[3] = partial_alpha ? static_cast<std::uint8_t>((x * 13 + y * 7 + seed * 31) & 255) : 255;
    }
  }
  return pixels;
}

// Largest difference between two composites, in levels: alpha directly, colour weighted by
// the smaller cover (a colour under 2% cover is not a visible difference of 255 levels).
int composite_difference(const Document& a, const Document& b, bool report = false) {
  const patchy::Rect canvas{0, 0, a.width(), a.height()};
  const auto left = patchy::flatten_document_rgba8_rect(a, canvas);
  const auto right = patchy::flatten_document_rgba8_rect(b, canvas);
  if (left.width() != right.width() || left.height() != right.height()) {
    return 255;
  }
  const auto l = left.data();
  const auto r = right.data();
  int worst = 0;
  std::size_t worst_at = 0;
  for (std::size_t i = 0; i < l.size(); i += 4) {
    int here = std::abs(int{l[i + 3]} - int{r[i + 3]});
    const int cover = std::min(int{l[i + 3]}, int{r[i + 3]});
    for (std::size_t c = 0; c < 3; ++c) {
      here = std::max(here, (std::abs(int{l[i + c]} - int{r[i + c]}) * cover + 254) / 255);
    }
    if (here > worst) {
      worst = here;
      worst_at = i;
    }
  }
  if (report && worst > 1) {
    const auto pixel = worst_at / 4U;
    std::cout << "    worst at (" << pixel % static_cast<std::size_t>(a.width()) << ", "
              << pixel / static_cast<std::size_t>(a.width()) << "): " << int{l[worst_at]} << ' ' << int{l[worst_at + 1]}
              << ' ' << int{l[worst_at + 2]} << ' ' << int{l[worst_at + 3]} << " vs " << int{r[worst_at]} << ' '
              << int{r[worst_at + 1]} << ' ' << int{r[worst_at + 2]} << ' ' << int{r[worst_at + 3]} << '\n';
  }
  return worst;
}

// Bakes `document` for Photoshop, writes and re-reads a PSD, and returns how far the
// re-read composite is from the original's. With an artifact name, the baked PSD and the
// original's composite (BMP) land in test-artifacts for a Photoshop open check.
int psd_bake_round_trip_difference(const Document& document, const char* label, const char* artifact = nullptr) {
  Document baked = document;
  (void)patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop);
  CHECK(patchy::find_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop).empty());
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(baked);
  if (artifact != nullptr) {
    std::filesystem::create_directories("test-artifacts");
    std::ofstream out(std::filesystem::path("test-artifacts") / (std::string(artifact) + ".psd"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    patchy::test::write_bmp_artifact(std::string(artifact) + "_patchy", document);
  }
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const int difference = composite_difference(document, reread, true);
  if (difference > 1) {
    std::cout << "  " << label << ": baked PSD composite is " << difference << " levels off (before the PSD: "
              << composite_difference(document, baked, true) << ")\n";
  }
  return difference;
}

Layer bake_test_layer(Document& document, const char* name, int seed, bool partial, BlendMode mode,
                      patchy::Rect bounds) {
  Layer layer(document.allocate_layer_id(), name, bake_test_pattern(bounds.width, bounds.height, seed, partial));
  layer.set_bounds(bounds);
  layer.set_blend_mode(mode);
  return layer;
}

void layered_blend_bake_round_trips_through_psd() {
  constexpr std::int32_t w = 24;
  constexpr std::int32_t h = 16;
  const patchy::Rect full{0, 0, w, h};
  const patchy::Rect inset{3, 2, 15, 11};
  const auto fresh = [&] { return Document(w, h, patchy::PixelFormat::rgba8()); };
  int worst = 0;
  const auto check = [&](const Document& document, const char* label, int tolerance = 1) {
    // Scored against each case's own tolerance: `worst <= 1` means every case is within it.
    worst = std::max(worst, psd_bake_round_trip_difference(document, label) - tolerance + 1);
  };

  {  // Opaque backdrop, offset partial-alpha Xor, Glow at 60%, a Multiply layer above both.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 1, false, BlendMode::Normal, full));
    document.add_layer(bake_test_layer(document, "Xor", 2, true, BlendMode::Xor, inset));
    auto glow = bake_test_layer(document, "Glow", 3, true, BlendMode::Glow, full);
    glow.set_opacity(0.6F);
    document.add_layer(std::move(glow));
    document.add_layer(bake_test_layer(document, "Multiply", 4, true, BlendMode::Multiply, inset));
    Document normal = document;
    for (auto& layer : normal.layers()) {
      if (patchy::blend_mode_is_paint_dot_net_only(layer.blend_mode())) layer.set_blend_mode(BlendMode::Normal);
    }
    CHECK(composite_difference(document, normal) > 20);  // what Save as Normal loses
    check(document, "opaque backdrop");
    Document baked = document;
    const auto result = patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop);
    CHECK(result.baked == 2 && result.merged == 0 && result.cleared == 0);
    CHECK(baked.layers().size() == 4 && baked.layers()[1].name() == "Xor" &&
          baked.layers()[1].blend_mode() == BlendMode::Normal && baked.layers()[1].opacity() == 1.0F);
    CHECK(baked.layers()[3].blend_mode() == BlendMode::Multiply);
  }
  {  // No backdrop: partial cover everywhere.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Base", 5, true, BlendMode::Normal, full));
    document.add_layer(bake_test_layer(document, "Reflect", 6, true, BlendMode::Reflect, inset));
    document.add_layer(bake_test_layer(document, "Negation", 7, true, BlendMode::Negation, full));
    check(document, "transparent backdrop");
  }
  {  // A Pass Through folder: its children bake against the parent's composite.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 8, false, BlendMode::Normal, full));
    Layer folder(document.allocate_layer_id(), "Pass", patchy::LayerKind::Group);
    folder.set_blend_mode(BlendMode::PassThrough);
    folder.children().push_back(bake_test_layer(document, "Negation", 9, true, BlendMode::Negation, inset));
    folder.children().push_back(bake_test_layer(document, "Screen", 10, true, BlendMode::Screen, full));
    document.add_layer(std::move(folder));
    check(document, "pass through folder");
    Document baked = document;
    (void)patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop);
    CHECK(baked.layers().size() == 2 && baked.layers()[1].children().size() == 2);
  }
  {  // A Pass Through folder at half opacity bakes whole.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 11, false, BlendMode::Normal, full));
    Layer folder(document.allocate_layer_id(), "Faded", patchy::LayerKind::Group);
    folder.set_blend_mode(BlendMode::PassThrough);
    folder.set_opacity(0.5F);
    folder.children().push_back(bake_test_layer(document, "Xor", 12, true, BlendMode::Xor, inset));
    document.add_layer(std::move(folder));
    check(document, "faded pass through folder");
    Document baked = document;
    const auto result = patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop);
    CHECK(result.merged == 1 && baked.layers().size() == 2 && baked.layers()[1].kind() == patchy::LayerKind::Pixel);
  }
  {  // Isolated folders (Normal, Multiply): the bake rebuilds the folder's own content.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 13, false, BlendMode::Normal, full));
    for (const auto mode : {BlendMode::Normal, BlendMode::Multiply}) {
      Layer folder(document.allocate_layer_id(), "Isolated", patchy::LayerKind::Group);
      folder.set_blend_mode(mode);
      folder.set_opacity(0.8F);
      folder.children().push_back(bake_test_layer(document, "Base", 14, true, BlendMode::Normal, full));
      folder.children().push_back(bake_test_layer(document, "Xor", 15, true, BlendMode::Xor, inset));
      folder.children().push_back(bake_test_layer(document, "Overlay", 16, true, BlendMode::Overlay, full));
      document.add_layer(std::move(folder));
    }
    // Overlay above the baked layer in the same folder doubles the bake's one-level rounding
    // (Overlay's slope is 2), so this case is allowed two levels.
    check(document, "isolated folders", 2);
  }
  {  // Clipping: a Xor layer clipped to a Normal base, and a Xor base under a clipped layer.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 17, false, BlendMode::Normal, full));
    document.add_layer(bake_test_layer(document, "Base", 18, true, BlendMode::Normal, inset));
    auto clipped_xor = bake_test_layer(document, "Clipped xor", 19, true, BlendMode::Xor, full);
    clipped_xor.set_clipped(true);
    document.add_layer(std::move(clipped_xor));
    document.add_layer(bake_test_layer(document, "Glow base", 20, true, BlendMode::Glow, full));
    auto clipped = bake_test_layer(document, "Clipped", 21, true, BlendMode::HardLight, inset);
    clipped.set_clipped(true);
    document.add_layer(std::move(clipped));
    check(document, "clipping runs");
    Document baked = document;
    const auto result = patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop);
    CHECK(result.baked == 2 && result.merged == 2 && baked.layers().size() == 3);
  }
  {  // A masked Xor layer, and a hidden Glow layer that only switches to Normal.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 22, false, BlendMode::Normal, full));
    auto masked = bake_test_layer(document, "Masked", 23, false, BlendMode::Xor, full);
    patchy::LayerMask mask;
    mask.bounds = patchy::Rect{0, 0, w / 2, h};
    mask.pixels = patchy::PixelBuffer(w / 2, h, patchy::PixelFormat::gray8());
    mask.pixels.clear(255);
    mask.default_color = 0;
    masked.set_mask(std::move(mask));
    document.add_layer(std::move(masked));
    auto hidden = bake_test_layer(document, "Hidden", 24, true, BlendMode::Glow, full);
    hidden.set_visible(false);
    const auto hidden_pixels = hidden.pixels();
    document.add_layer(std::move(hidden));
    check(document, "masked and hidden");
    Document baked = document;
    const auto result = patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Photoshop);
    CHECK(result.baked == 1 && result.cleared == 1);
    const auto& kept = std::as_const(baked).layers()[2];
    CHECK(!kept.visible() && kept.blend_mode() == BlendMode::Normal && std::ranges::equal(kept.pixels().data(), hidden_pixels.data()));
  }
  {  // 16 bits: the bake goes through the 8-bit composite and back.
    auto document = fresh();
    document.add_layer(bake_test_layer(document, "Backdrop", 25, false, BlendMode::Normal, full));
    document.add_layer(bake_test_layer(document, "Xor", 26, true, BlendMode::Xor, inset));
    patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
    check(document, "16-bit document");
  }

  // Paint.NET's own files: every mode Paint.NET has, partial alpha rows, a hidden layer.
  for (const char* name : {"blend-modes.pdn", "demo.pdn", "opacity-stack.pdn"}) {
    const auto document = patchy::pdn::DocumentIo::read_file(patchy::test::committed_format_fixture_path("pdn", name));
    const std::string stem = std::filesystem::path(name).stem().string();
    const auto artifact = "layered_blend_bake_" + stem;
    worst = std::max(worst, psd_bake_round_trip_difference(document, name, artifact.c_str()));
  }
  CHECK(worst <= 1);
}

void layered_blend_bake_round_trips_through_aseprite() {
  constexpr std::int32_t w = 20;
  constexpr std::int32_t h = 12;
  const patchy::Rect full{0, 0, w, h};
  Document document(w, h, patchy::PixelFormat::rgba8());
  document.add_layer(bake_test_layer(document, "Backdrop", 30, false, BlendMode::Normal, full));
  document.add_layer(bake_test_layer(document, "Linear burn", 31, true, BlendMode::LinearBurn, {2, 2, 12, 8}));
  document.add_layer(bake_test_layer(document, "Pin light", 32, true, BlendMode::PinLight, full));
  Layer folder(document.allocate_layer_id(), "Pass", patchy::LayerKind::Group);
  folder.set_blend_mode(BlendMode::PassThrough);
  folder.children().push_back(bake_test_layer(document, "Multiply", 33, true, BlendMode::Multiply, full));
  document.add_layer(std::move(folder));
  document.add_layer(bake_test_layer(document, "Screen", 34, true, BlendMode::Screen, full));
  Document baked = document;
  (void)patchy::bake_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Aseprite);
  CHECK(patchy::find_unsupported_blend_modes(baked, patchy::LayeredSaveFormat::Aseprite).empty());
  CHECK(baked.layers().back().blend_mode() == BlendMode::Screen);
  const auto reread = patchy::aseprite::DocumentIo::read(patchy::aseprite::DocumentIo::write(baked));
  const int difference = composite_difference(document, reread);
  if (difference > 1) {
    std::cout << "  baked Aseprite composite is " << difference << " levels off\n";
  }
  CHECK(difference <= 1);
}

std::optional<std::vector<std::uint8_t>> load_png_rgba(const std::filesystem::path& path, int& width, int& height) {
  const auto bytes = patchy::test::read_binary_file(path);
  int components = 0;
  auto* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &components, 4);
  if (decoded == nullptr) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> out(decoded, decoded + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  stbi_image_free(decoded);
  return out;
}

// Patchy's composite of a sample against the PNG Paint.NET flattened from it, with only
// the listed layers visible. Returns {differing channel values, largest difference}.
std::pair<std::size_t, int> composite_difference(const std::filesystem::path& pdn, const std::vector<std::size_t>& visible,
                                                 const std::filesystem::path& png) {
  auto document = patchy::pdn::DocumentIo::read_file(pdn);
  for (std::size_t i = 0; i < document.layers().size(); ++i) {
    document.layers()[i].set_visible(std::find(visible.begin(), visible.end(), i) != visible.end());
  }
  const auto flat = patchy::flatten_document_rgba8(document);
  int width = 0;
  int height = 0;
  const auto reference = load_png_rgba(png, width, height);
  CHECK(reference.has_value() && width == document.width() && height == document.height());
  const auto ours = flat.data();
  std::size_t differing = 0;
  int largest = 0;
  for (std::size_t i = 0; i < reference->size(); ++i) {
    const auto difference = std::abs(static_cast<int>(ours[i]) - static_cast<int>((*reference)[i]));
    differing += difference != 0 ? 1U : 0U;
    largest = std::max(largest, difference);
  }
  return {differing, largest};
}

// The modes whose kernels are Paint.NET's own or agree with it exactly.
bool paint_net_exact_mode(int index) {
  return index == 0 || index == 2 || index == 5 || index == 6 || index == 8 || index == 9 || index == 10 ||
         index == 11 || index == 13;
}

struct ExportDifference {
  std::size_t differing{0};  // channel values that differ
  std::size_t over_one{0};   // channel values that differ by more than one level
  int largest{0};
};

// Patchy's flatten of `document` rows [first_row, last_row) against Paint.NET's PNG of the
// same file. Colour is compared premultiplied by its alpha (rounded to a level), so a
// pixel at alpha 3 does not turn a sub-level difference into a large straight-colour one.
ExportDifference export_difference(const Document& document, const std::vector<std::uint8_t>& reference,
                                   std::int32_t first_row, std::int32_t last_row) {
  const auto flat = patchy::flatten_document_rgba8(document);
  const auto ours = flat.data();
  ExportDifference result;
  const auto row_bytes = static_cast<std::size_t>(document.width()) * 4U;
  for (auto y = first_row; y < last_row; ++y) {
    for (std::size_t i = static_cast<std::size_t>(y) * row_bytes; i < static_cast<std::size_t>(y + 1) * row_bytes;
         i += 4) {
      for (std::size_t c = 0; c < 4; ++c) {
        const auto premultiplied = [&](const auto& pixels) {
          return c == 3 ? static_cast<int>(pixels[i + 3])
                        : (static_cast<int>(pixels[i + c]) * static_cast<int>(pixels[i + 3]) + 127) / 255;
        };
        const auto difference = std::abs(premultiplied(ours) - premultiplied(reference));
        result.differing += difference != 0 ? 1U : 0U;
        result.over_one += difference > 1 ? 1U : 0U;
        result.largest = std::max(result.largest, difference);
      }
    }
  }
  return result;
}

// Paint.NET's Color Burn returns 0 wherever the layer is 0 and its Color Dodge 255
// wherever the layer is 255; Photoshop's kernels (which Patchy keeps) give a white
// backdrop's 255 and a black backdrop's 0 there. Only those corners may differ by more
// than one level (docs/pdn-format.md, "Blend calibration").
bool export_within_residual(int mode_index, const ExportDifference& difference, std::size_t corner_budget) {
  if (paint_net_exact_mode(mode_index)) {
    return difference.largest == 0;
  }
  if (mode_index == 3 || mode_index == 4) {
    return difference.over_one <= corner_budget;
  }
  return difference.largest <= 1;
}

// Partial alpha adds one level of compositing rounding to every mode, and a second
// where a rounding mode's own residual meets it.
bool partial_within_residual(int mode_index, const ExportDifference& difference, std::size_t corner_budget) {
  if (paint_net_exact_mode(mode_index)) {
    return difference.largest <= 1;
  }
  if (mode_index == 3 || mode_index == 4) {
    return difference.largest <= 2 || difference.over_one <= corner_budget;
  }
  return difference.largest <= 2;
}

// Files we made and Paint.NET 5.1.12 re-saved, beside Paint.NET's flattened PNG export
// (docs/pdn-format.md, "Fixtures and tests"): the 5.x graph reads, and Patchy's composite
// matches Paint.NET's within the calibrated residuals.
void pdn_paint_net_5_files_match_their_exports() {
  const auto fixture = [](const char* name) { return patchy::test::committed_format_fixture_path("pdn", name); };
  int width = 0;
  int height = 0;
  {
    std::vector<std::string> notices;
    const auto document = patchy::pdn::DocumentIo::read_file(fixture("blend-modes.pdn"), &notices);
    CHECK(notices.empty());
    CHECK(document.layers().size() == 15);
    CHECK(patchy::background_layer(document.layers(), document.width(), document.height()) != nullptr);
    const std::array<BlendMode, 14> modes{BlendMode::Normal,     BlendMode::Multiply,   BlendMode::LinearDodge,
                                          BlendMode::ColorBurn,  BlendMode::ColorDodge, BlendMode::Reflect,
                                          BlendMode::Glow,       BlendMode::Overlay,    BlendMode::Difference,
                                          BlendMode::Negation,   BlendMode::Lighten,    BlendMode::Darken,
                                          BlendMode::Screen,     BlendMode::Xor};
    for (std::size_t k = 0; k < modes.size(); ++k) {
      CHECK(document.layers()[k + 1].blend_mode() == modes[k]);
    }
    const auto reference = load_png_rgba(fixture("blend-modes.png"), width, height);
    CHECK(reference.has_value() && width == document.width() && height == document.height());
    // 14 bands of 80 rows: a 66-row opaque value table, then 14 partial-alpha rows.
    const std::int32_t band = height / 14;
    const std::int32_t table = band - 14;
    std::vector<bool> results;
    for (int k = 0; k < 14; ++k) {
      const auto opaque = export_difference(document, *reference, k * band, k * band + table);
      const auto partial = export_difference(document, *reference, k * band + table, (k + 1) * band);
      std::cout << "  " << document.layers()[static_cast<std::size_t>(k) + 1].name() << ": opaque " << opaque.differing
                << " values differ (" << opaque.over_one << " by more than 1, largest " << opaque.largest
                << "), partial alpha " << partial.differing << " (" << partial.over_one << " by more than 1, largest "
                << partial.largest << ")\n";
      // A 66-value table holds one 0 and one 255 per channel and row or column.
      results.push_back(export_within_residual(k, opaque, 3U * 66U) &&
                        partial_within_residual(k, partial, 3U * 66U));
    }
    for (int k = 0; k < 14; ++k) {
      if (!results[static_cast<std::size_t>(k)]) {
        throw std::runtime_error("blend band " + std::to_string(k) + " differs from Paint.NET beyond its residual");
      }
    }
  }
  {
    const auto document = patchy::pdn::DocumentIo::read_file(fixture("opacity-stack.pdn"));
    CHECK(document.layers().size() == 5);
    CHECK(patchy::background_layer(document.layers(), document.width(), document.height()) == nullptr);
    CHECK(std::abs(document.layers()[1].opacity() * 255.0F - 102.0F) < 0.01F);
    CHECK(!document.layers()[3].visible());
    const auto reference = load_png_rgba(fixture("opacity-stack.png"), width, height);
    CHECK(reference.has_value() && width == document.width());
    // Several partial layers over transparency: each 8-bit intermediate composite rounds,
    // so the stack drifts up to 4 premultiplied levels from Paint.NET's.
    const auto difference = export_difference(document, *reference, 0, height);
    std::cout << "  opacity-stack: " << difference.differing << " values differ (largest " << difference.largest
              << ")\n";
    CHECK(difference.largest <= 4);
  }
  {
    // A landscape with Screen, Glow and Xor layers, a hidden layer and a non-ASCII name.
    const auto document = patchy::pdn::DocumentIo::read_file(fixture("demo.pdn"));
    CHECK(document.width() == 640 && document.height() == 400 && document.layers().size() == 6);
    CHECK(document.layers()[1].blend_mode() == BlendMode::Screen);
    CHECK(document.layers()[2].name() == "H\xC3\xBCgel");
    CHECK(document.layers()[3].blend_mode() == BlendMode::Glow);
    CHECK(document.layers()[4].blend_mode() == BlendMode::Xor);
    CHECK(!document.layers()[5].visible());
    CHECK(document.print_settings().horizontal_ppi == 150.0);
    const auto reference = load_png_rgba(fixture("demo.png"), width, height);
    CHECK(reference.has_value() && width == 640 && height == 400);
    const auto difference = export_difference(document, *reference, 0, height);
    std::cout << "  demo: " << difference.differing << " values differ (largest " << difference.largest << ")\n";
    CHECK(difference.largest <= 4);
  }
  {
    const auto document = patchy::pdn::DocumentIo::read_file(fixture("odd-size-unicode.pdn"));
    CHECK(document.width() == 33 && document.height() == 17);
    CHECK(document.layers()[1].name() == "caf\xC3\xA9 \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 \xF0\x9F\x8E\xA8");
    CHECK(std::abs(document.print_settings().horizontal_ppi - 300.0) < 0.01);
    CHECK(document.print_settings().horizontal_resolution_display_unit == 2);
    const auto reference = load_png_rgba(fixture("odd-size-unicode.png"), width, height);
    CHECK(reference.has_value());
    CHECK(export_difference(document, *reference, 0, height).largest <= 1);
  }
}

// The full 256 x 256 value tables (local-test-fixtures/pdn/pdn5: the same kind of files at
// full resolution, one mode per file): prints the residual per mode.
void pdn_paint_net_5_full_tables_if_available() {
  const auto dir = patchy::test::local_format_fixture_path("pdn", "pdn5");
  patchy::test::skip_unless_exists(dir / "blend-13-xor.pdn");
  std::size_t checked = 0;
  bool all_within = true;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".pdn") {
      continue;
    }
    auto png = entry.path();
    png.replace_extension(".png");
    if (!std::filesystem::exists(png)) {
      continue;
    }
    const auto document = patchy::pdn::DocumentIo::read_file(entry.path());
    int width = 0;
    int height = 0;
    const auto reference = load_png_rgba(png, width, height);
    CHECK(reference.has_value() && width == document.width() && height == document.height());
    const auto stem = entry.path().stem().string();
    const bool table = stem.starts_with("blend-");
    const auto opaque = export_difference(document, *reference, 0, table ? 256 : height);
    std::cout << "  " << stem << ": " << opaque.differing << " values differ (largest " << opaque.largest << ")";
    if (table) {
      const auto partial = export_difference(document, *reference, 256, height);
      std::cout << ", partial alpha " << partial.differing << " (" << partial.over_one << " by more than 1, largest "
                << partial.largest << ")";
      const auto mode = std::stoi(stem.substr(6, 2));
      std::cout << (export_within_residual(mode, opaque, 3U * 256U) && partial_within_residual(mode, partial, 3U * 256U)
                        ? ""
                        : " OUTSIDE THE RESIDUAL");
      all_within = all_within && export_within_residual(mode, opaque, 3U * 256U) &&
                   partial_within_residual(mode, partial, 3U * 256U);
    } else {
      CHECK(opaque.largest <= 4);
    }
    std::cout << '\n';
    ++checked;
  }
  CHECK(checked > 0);
  CHECK(all_within);
}

// Paint.NET's own flattened exports of the pypdn samples (local-test-fixtures/pdn). The
// modes Paint.NET computes exactly like Photoshop match exactly; Multiply, Screen and
// Overlay round where Photoshop's calibrated kernels floor, and Color Burn/Dodge floor
// where they round, so those stay within one level (docs/pdn-format.md).
void pdn_composites_match_paint_net_exports_if_available() {
  const auto dir = patchy::test::local_format_fixture_path("pdn", "");
  patchy::test::skip_unless_exists(dir / "FlattenBlendTest.pdn");
  struct Case {
    const char* pdn;
    std::vector<std::size_t> visible;
    const char* png;
    int tolerance;
  };
  const std::vector<Case> cases{
      {"FlattenBlendTest.pdn", {0, 1}, "FlattenMultiplyTest.png", 1},
      {"FlattenBlendTest.pdn", {0, 2}, "FlattenAdditiveTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 3}, "FlattenColorBurnTest.png", 1},
      {"FlattenBlendTest.pdn", {0, 4}, "FlattenColorDodgeTest.png", 1},
      {"FlattenBlendTest.pdn", {0, 5}, "FlattenReflectTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 6}, "FlattenGlowTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 7}, "FlattenOverlayTest.png", 1},
      {"FlattenBlendTest.pdn", {0, 8}, "FlattenDifferenceTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 9}, "FlattenNegationTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 10}, "FlattenLightenTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 11}, "FlattenDarkenTest.png", 0},
      {"FlattenBlendTest.pdn", {0, 12}, "FlattenScreenTest.png", 1},
      {"FlattenBlendTest.pdn", {0, 13}, "FlattenXORTest.png", 0},
      {"Untitled2.pdn", {0}, "flattenNormalTest2.png", 0},
      {"Untitled2.pdn", {0, 1}, "flattenNormalTest.png", 1},
      {"Untitled3.pdn", {0, 1}, "flattenOpacityTest.png", 1},
  };
  for (const auto& entry : cases) {
    const auto png = dir / entry.png;
    if (!std::filesystem::exists(png)) {
      patchy::test::skip(std::string("optional fixture missing: local-test-fixtures/pdn/") + entry.png);
    }
    const auto [differing, largest] = composite_difference(dir / entry.pdn, entry.visible, png);
    std::cout << "  " << entry.png << ": " << differing << " channel values differ, largest " << largest << '\n';
    CHECK(largest <= entry.tolerance);
  }
}

// What else the format carries, probed in Paint.NET 5.1.12 (docs/pdn-format.md, "What the
// format does not carry"). text-saved-while-editing.pdn was saved while the Text tool still
// had "Hello PDN text" open for editing: Paint.NET commits the text into the layer's pixels
// on save, and no string, font or text object reaches the file. A 1x1 image and a
// 300-layer image round-trip through Paint.NET and import like any other.
void pdn_paint_net_5_probe_files_import_as_pixel_layers() {
  const auto fixture = [](const char* name) { return patchy::test::committed_format_fixture_path("pdn", name); };
  {
    std::vector<std::string> notices;
    const auto bytes = patchy::test::read_binary_file(fixture("text-saved-while-editing.pdn"));
    const auto document = patchy::pdn::DocumentIo::read(bytes, &notices);
    CHECK(notices.empty());
    CHECK(document.width() == 400 && document.height() == 200 && document.layers().size() == 1);
    const auto& layer = document.layers()[0];
    CHECK(layer.kind() == patchy::LayerKind::Pixel && !patchy::layer_is_text(layer));
    CHECK(patchy::background_layer(document.layers(), 400, 200) == &layer);
    // The text is dark pixels around (28..125, 40..58); everything outside it stays white.
    std::size_t dark = 0;
    for (std::int32_t y = 0; y < 200; ++y) {
      for (std::int32_t x = 0; x < 400; ++x) {
        const auto p = pixel_at(layer, x, y);
        if (p[0] < 128) {
          ++dark;
          CHECK(x >= 20 && x < 140 && y >= 30 && y < 70);
        }
      }
    }
    CHECK(dark > 100);
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    CHECK(text.find("Hello") == std::string_view::npos);
  }
  int width = 0;
  int height = 0;
  {
    const auto document = patchy::pdn::DocumentIo::read_file(fixture("one-pixel.pdn"));
    CHECK(document.width() == 1 && document.height() == 1 && document.layers().size() == 3);
    CHECK(document.layers()[2].blend_mode() == BlendMode::Xor);
    const auto reference = load_png_rgba(fixture("one-pixel.png"), width, height);
    CHECK(reference.has_value() && width == 1 && height == 1);
    CHECK(export_difference(document, *reference, 0, 1).largest <= 1);
  }
  {
    const auto started = std::chrono::steady_clock::now();
    const auto document = patchy::pdn::DocumentIo::read_file(fixture("many-layers.pdn"));
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    CHECK(document.layers().size() == 300);
    CHECK(document.layers()[299].name() == "Layer 299");
    const auto reference = load_png_rgba(fixture("many-layers.png"), width, height);
    CHECK(reference.has_value() && width == 64 && height == 64);
    // Every layer's pixels and properties are exact (MD5s from pypdn) ...
    const std::array<std::pair<std::size_t, const char*>, 4> digests{{{0, "076b22eabe0376bf7395aa24e32cf5cb"},
                                                                      {1, "7e28d5c42d71e508e4078cf7e367cb88"},
                                                                      {150, "3f7975f67b10e06816ddb49aed156d34"},
                                                                      {299, "c279349b04f3b7ee79dd7634e5fe68ae"}}};
    for (const auto& [index, expected] : digests) {
      const auto digest = patchy::pdf::md5(rgba_bytes(document.layers()[index]));
      std::ostringstream hex;
      for (const auto byte : digest) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
      }
      CHECK(hex.str() == expected);
    }
    CHECK(document.layers()[1].blend_mode() == BlendMode::Multiply);
    CHECK(document.layers()[299].blend_mode() == BlendMode::Reflect);
    CHECK(std::abs(document.layers()[150].opacity() * 255.0F - 210.0F) < 0.01F);
    // ... while the composite of 300 mixed-mode, partial-opacity layers drifts: each layer's
    // rounding feeds the next, and Dodge, Burn and Reflect magnify it. A float model of
    // Paint.NET's own kernels drifts from its export by the same 38 levels.
    const auto difference = export_difference(document, *reference, 0, 64);
    std::cout << "  300 layers: " << static_cast<int>(elapsed) << " ms, " << difference.differing
              << " values differ from Paint.NET's export (largest " << difference.largest << ")\n";
    CHECK(difference.largest <= 40);
  }
}

// The real-world corpus (local-test-fixtures/pdn/corpus/<source>/*.pdn, untracked, with a
// manifest of source URLs; docs/pdn-format.md "Fixtures and tests"). Each file beside a
// <name>.oracle.txt from pypdn is opened and compared layer by layer: properties and the
// MD5 of the RGBA bytes. Writes test-artifacts/pdn-corpus-report.tsv (one row per file,
// flushed as it goes, so a crash names its file) and prints the totals by result. Files
// over 256 MB (the 25600 x 14400 ones take about 12 s each) run only with
// PATCHY_PDN_CORPUS_LARGE=1 and the test timeout off (PATCHY_TEST_TIMEOUT_SECONDS=0).
void pdn_corpus_matches_pypdn_if_available() {
  const bool include_large = patchy::test::environment_value("PATCHY_PDN_CORPUS_LARGE") == "1";
  const auto root = patchy::test::local_format_fixture_path("pdn", "corpus");
  patchy::test::skip_unless_exists(root);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream report("test-artifacts/pdn-corpus-report.tsv", std::ios::binary);
  report << "file\tresult\tversion\tlayers\twidth\theight\tms\tdetail\n";
  std::map<std::string, std::size_t> totals;
  std::map<std::string, std::map<std::string, std::size_t>> by_version;
  std::vector<std::string> failures;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".pdn") {
      continue;
    }
    const auto path = entry.path();
    const auto name = path.parent_path().filename().string() + "/" + path.filename().string();
    if (!include_large && entry.file_size() > (256U << 20U)) {
      report << name << "\tskipped (large)\t\t\t\t\t\t\n";
      ++totals["skipped (large)"];
      continue;
    }
    auto oracle_path = path;
    oracle_path.replace_extension(".oracle.txt");
    std::string version = "?";
    std::string oracle_error;
    std::int32_t width = 0;
    std::int32_t height = 0;
    struct Expected {
      std::string md5;
      bool visible;
      int opacity;
      int blend;
      std::string name;
    };
    std::vector<Expected> expected;
    bool have_oracle = false;
    if (std::ifstream oracle(oracle_path); oracle) {
      have_oracle = true;
      std::string line;
      while (std::getline(oracle, line)) {
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;
        if (kind == "size") {
          fields >> width >> height;
        } else if (kind == "version") {
          fields >> version;
        } else if (kind == "error") {
          oracle_error = line.substr(6);
        } else if (kind == "layer-md5") {
          Expected layer{};
          int visible = 0;
          int background = 0;
          fields >> layer.md5 >> visible >> background >> layer.opacity >> layer.blend;
          layer.visible = visible != 0;
          std::getline(fields, layer.name);
          if (!layer.name.empty() && layer.name.front() == ' ') {
            layer.name.erase(0, 1);
          }
          expected.push_back(std::move(layer));
        }
      }
    }
    std::string result;
    std::string detail;
    std::size_t layer_count = 0;
    double elapsed = 0.0;
    try {
      const auto bytes = patchy::test::read_binary_file(path);
      std::vector<std::string> notices;
      const auto started = std::chrono::steady_clock::now();
      const auto document = patchy::pdn::DocumentIo::read(bytes, &notices);
      elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      layer_count = document.layers().size();
      const bool preview = std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
        return notice.find("embedded preview") != std::string::npos;
      });
      for (const auto& notice : notices) {
        detail += (detail.empty() ? "" : " | ") + notice;
      }
      if (preview) {
        result = "fallback";
      } else if (!have_oracle || !oracle_error.empty()) {
        result = "opened (no oracle)";
      } else if (document.width() != width || document.height() != height || layer_count != expected.size()) {
        result = "mismatch";
        detail = "size or layer count differs";
      } else {
        result = "pass";
        for (std::size_t i = 0; i < expected.size(); ++i) {
          const auto& layer = document.layers()[i];
          const auto& want = expected[i];
          const auto digest = patchy::pdf::md5(rgba_bytes(layer));
          std::ostringstream hex;
          for (const auto byte : digest) {
            hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
          }
          std::string why;
          if (layer.name() != want.name) {
            why = "name";
          } else if (layer.visible() != want.visible) {
            why = "visibility";
          } else if (std::abs(layer.opacity() * 255.0F - static_cast<float>(want.opacity)) > 0.01F) {
            why = "opacity";
          } else if (layer.blend_mode() != expected_blend(want.blend)) {
            why = "blend " + std::to_string(want.blend);
          } else if (hex.str() != want.md5) {
            why = "pixels";
          }
          if (!why.empty()) {
            result = "mismatch";
            detail = "layer " + std::to_string(i) + ": " + why;
            break;
          }
        }
      }
    } catch (const std::exception& error) {
      const std::string what = error.what();
      result = what.find("Not a Paint.NET document") != std::string::npos ? "not a pdn" : "error";
      detail = what;
    }
    report << name << '\t' << result << '\t' << version << '\t' << layer_count << '\t' << width << '\t' << height
           << '\t' << static_cast<long long>(elapsed) << '\t' << detail << '\n';
    report.flush();
    ++totals[result];
    const auto dot = version.find('.');
    const auto major = dot == std::string::npos ? version : version.substr(0, dot);
    ++by_version[major][result];
    // pypdn read it and Patchy did not agree: a real failure. (Files pypdn cannot read
    // either are reported, not failed: some are truncated downloads.)
    if (have_oracle && oracle_error.empty() && result != "pass") {
      failures.push_back(name + ": " + result + " (" + detail + ")");
    }
  }
  for (const auto& [result, count] : totals) {
    std::cout << "  " << result << ": " << count << '\n';
  }
  for (const auto& [major, results] : by_version) {
    std::cout << "  Paint.NET " << major << ":";
    for (const auto& [result, count] : results) {
      std::cout << ' ' << result << ' ' << count;
    }
    std::cout << '\n';
  }
  for (const auto& failure : failures) {
    std::cout << "  FAILED " << failure << '\n';
  }
  CHECK(failures.empty());
}

// A 4000 x 4000 file with ten layers (local-test-fixtures/pdn/large-4000x4000x10.pdn,
// 250 MB, made by the fixture scripts): prints the open time for docs/pdn-format.md.
void pdn_large_file_timing_if_available() {
  const auto path = patchy::test::local_format_fixture_path("pdn", "large-4000x4000x10.pdn");
  patchy::test::skip_unless_exists(path);
  const auto bytes = patchy::test::read_binary_file(path);
  const auto started = std::chrono::steady_clock::now();
  std::vector<std::string> notices;
  const auto document = patchy::pdn::DocumentIo::read(bytes, &notices);
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  std::cout << "  4000x4000, 10 layers: " << static_cast<int>(elapsed) << " ms for " << bytes.size() / (1024 * 1024)
            << " MB\n";
  CHECK(notices.empty());
  CHECK(document.layers().size() == 10 && document.width() == 4000);
  CHECK(document.layers()[5].blend_mode() == BlendMode::Xor);
}

}  // namespace

std::vector<patchy::test::TestCase> pdn_tests() {
  return {
      {"pdn_nrbf_reads_every_record_type", pdn_nrbf_reads_every_record_type},
      {"pdn_nrbf_rejects_malformed_input", pdn_nrbf_rejects_malformed_input},
      {"pdn_reads_layers_properties_and_pixels", pdn_reads_layers_properties_and_pixels},
      {"pdn_reads_out_of_order_raw_and_small_chunks", pdn_reads_out_of_order_raw_and_small_chunks},
      {"pdn_reads_padded_strides_and_shared_blocks", pdn_reads_padded_strides_and_shared_blocks},
      {"pdn_reads_legacy_blend_op_classes_and_unknown_modes", pdn_reads_legacy_blend_op_classes_and_unknown_modes},
      {"pdn_reads_gzipped_graphs_and_headerless_legacy_files", pdn_reads_gzipped_graphs_and_headerless_legacy_files},
      {"pdn_reads_resolution_from_exif_metadata", pdn_reads_resolution_from_exif_metadata},
      {"pdn_exif_metadata_reaches_file_info", pdn_exif_metadata_reaches_file_info},
      {"pdn_broken_graph_falls_back_to_the_thumbnail", pdn_broken_graph_falls_back_to_the_thumbnail},
      {"pdn_damaged_chunks_open_transparent_with_a_notice", pdn_damaged_chunks_open_transparent_with_a_notice},
      {"pdn_mutation_sweep_never_crashes", pdn_mutation_sweep_never_crashes},
      {"pdn_size_limits_and_stub_files_explain_themselves", pdn_size_limits_and_stub_files_explain_themselves},
      {"pdn_registry_sniff_and_open_by_content_detection", pdn_registry_sniff_and_open_by_content_detection},
      {"pdn_unicode_path_opens", pdn_unicode_path_opens},
      {"pdn_public_samples_match_pypdn_if_available", pdn_public_samples_match_pypdn_if_available},
      {"pdn_composites_match_paint_net_exports_if_available", pdn_composites_match_paint_net_exports_if_available},
      {"pdn_paint_net_5_files_match_their_exports", pdn_paint_net_5_files_match_their_exports},
      {"pdn_paint_net_5_full_tables_if_available", pdn_paint_net_5_full_tables_if_available},
      {"pdn_large_file_timing_if_available", pdn_large_file_timing_if_available},
      {"pdn_corpus_matches_pypdn_if_available", pdn_corpus_matches_pypdn_if_available},
      {"pdn_paint_net_5_probe_files_import_as_pixel_layers", pdn_paint_net_5_probe_files_import_as_pixel_layers},
      {"blend_math_paint_dot_net_modes_match_paint_net_renders", blend_math_paint_dot_net_modes_match_paint_net_renders},
      {"psd_saves_paint_dot_net_blend_modes_as_normal", psd_saves_paint_dot_net_blend_modes_as_normal},
      {"layered_blend_support_lists_what_each_format_stores", layered_blend_support_lists_what_each_format_stores},
      {"layered_blend_bake_round_trips_through_psd", layered_blend_bake_round_trips_through_psd},
      {"layered_blend_bake_round_trips_through_aseprite", layered_blend_bake_round_trips_through_aseprite},
  };
}
