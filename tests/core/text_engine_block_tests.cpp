// Photoshop's document-level text engine block ('Txt2'): the EngineData syntax module and the
// block writer (docs/txt2.md). Photoshop acceptance of the authored blocks was verified over COM
// (scripts/dev/txt2/ps-probe.ps1); these tests pin the byte-exact round trip of Photoshop's own
// blocks and the shape of what Patchy authors.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_area.hpp"
#include "psd/engine_data.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_text_engine_block.hpp"

#include "core_test_support.hpp"
#include "psd_test_support.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_extra_data;
using patchy::test::solid_rgba;

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The Txt2 payload of a written PSD: the global tagged block after the layer info.
std::optional<std::vector<std::uint8_t>> text_engine_payload(const std::vector<std::uint8_t>& bytes) {
  static constexpr std::string_view kMarker = "8BIMTxt2";
  const auto it = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
  if (it == bytes.end() || std::distance(it, bytes.end()) < static_cast<std::ptrdiff_t>(kMarker.size() + 4)) {
    return std::nullopt;
  }
  const auto* p = &*(it + static_cast<std::ptrdiff_t>(kMarker.size()));
  const auto length = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                      (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
  const auto start = it + static_cast<std::ptrdiff_t>(kMarker.size() + 4);
  if (std::distance(start, bytes.end()) < static_cast<std::ptrdiff_t>(length)) {
    return std::nullopt;
  }
  return std::vector<std::uint8_t>(start, start + static_cast<std::ptrdiff_t>(length));
}

std::optional<std::int32_t> text_index_of(const std::vector<std::uint8_t>& bytes, int layer_index) {
  const auto payload = psd_layer_block_payload(psd_layer_extra_data(bytes, static_cast<std::int16_t>(layer_index)), "TySh");
  if (!payload.has_value()) {
    return std::nullopt;
  }
  static constexpr std::string_view kKey = "TextIndexlong";
  const auto it = std::search(payload->begin(), payload->end(), kKey.begin(), kKey.end());
  if (it == payload->end() || std::distance(it, payload->end()) < static_cast<std::ptrdiff_t>(kKey.size() + 4)) {
    return std::nullopt;
  }
  const auto* p = &*(it + static_cast<std::ptrdiff_t>(kKey.size()));
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                                   (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]));
}

patchy::Layer patchy_text_layer(patchy::Document& document, std::string_view name, std::string_view text, bool boxed) {
  patchy::Layer layer(document.allocate_layer_id(), std::string(name), solid_rgba(200, 60, 0, 0, 0, 0));
  layer.metadata()[patchy::kLayerMetadataText] = std::string(text);
  layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  layer.metadata()[patchy::kLayerMetadataTextSize] = "24";
  layer.metadata()[patchy::kLayerMetadataTextColor] = "#202020";
  layer.metadata()[patchy::kLayerMetadataTextBold] = "false";
  layer.metadata()[patchy::kLayerMetadataTextItalic] = "false";
  layer.metadata()[patchy::kLayerMetadataTextAntiAlias] = "3";
  layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  if (boxed) {
    layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
    layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "400";
    layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "180";
    layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v2\n0\t16\tleft\t24\t12\t0\t0\t6\n16\t17\tcenter\t0\t0\t0\t0\t0";
  }
  return layer;
}

}  // namespace

// Every Photoshop-authored Txt2 block in the committed fixtures parses and re-serializes byte for byte.
void text_engine_photoshop_blocks_round_trip_byte_exact() {
  int checked = 0;
  for (const auto* name : {"photoshop-text-tracking.psd", "photoshop-text-vertical-box.psd", "photoshop-text-rtl-hebrew.psd",
                           "photoshop-text-box-auto-leading.psd", "photoshop-warp-text.psd", "qual_rca_pinout.psd",
                           "photoshop-cmyk-style-colors.psd"}) {
    const auto bytes = read_all(patchy::test::committed_psd_fixture_path(name));
    const auto payload = text_engine_payload(bytes);
    CHECK(payload.has_value());
    if (!payload.has_value()) {
      continue;
    }
    const auto root = patchy::psd::parse_engine_data(*payload, /*bare_root=*/true);
    CHECK(root.has_value());
    if (!root.has_value()) {
      continue;
    }
    CHECK(patchy::psd::serialize_engine_data_bytes(*root, /*bare_root=*/true) == *payload);
    const auto block = patchy::psd::TextEngineBlock::parse(*payload);
    CHECK(block.has_value() && block->object_count() >= 1);
    ++checked;
  }
  CHECK(checked == 7);
}

// The syntax module: strings, numbers, names, nested containers and Photoshop's number spelling.
void text_engine_syntax_parses_and_authors_tokens() {
  using namespace patchy::psd;
  // Built in pieces: the UTF-16 string carries NUL bytes a bare literal would stop at.
  const std::string source = std::string(" /98 << /0 14 >> /0 << /1 [ << /99 /CoolTypeFont /0 (") +
                             std::string("\xfe\xff\x00H\x00i", 6) +
                             std::string(") >> ] /2 -20.5957 /3 .583 /4 true /5 /nil >>");
  const auto root = parse_engine_data(source, /*bare_root=*/true);
  CHECK(root.has_value());
  if (!root.has_value()) {
    return;
  }
  CHECK(serialize_engine_data(*root, /*bare_root=*/true) == source);
  CHECK(root->at_path({"98", "0"})->integer() == 14);
  CHECK(root->at_path({"0", "1", "0", "99"})->name() == std::string("CoolTypeFont"));
  CHECK(root->at_path({"0", "1", "0", "0"})->string_utf8() == std::string("Hi"));
  CHECK(std::abs(*root->at_path({"0", "2"})->number() + 20.5957) < 1e-9);
  CHECK(std::abs(*root->at_path({"0", "3"})->number() - 0.583) < 1e-9);
  CHECK(root->at_path({"0", "4"})->boolean() == true);
  CHECK(root->at_path({"0", "5"})->is_nil());
  CHECK(root->at_path({"0", "9"}) == nullptr);
  CHECK(engine_number_text(24.0) == "24.0");
  CHECK(engine_number_text(-20.0) == "-20.0");
  CHECK(engine_number_text(0.0) == "0.0");
  CHECK(engine_number_text(0.583) == ".583");
  CHECK(engine_number_text(-0.5) == "-.5");
  CHECK(engine_number_text(216.52031) == "216.52031");
  CHECK(engine_number_text(1.2) == "1.2");
  const auto escaped = engine_string("a(b)c\\");
  CHECK(escaped.raw == std::string("(\xfe\xff\x00" "a\x00\\(\x00" "b\x00\\)\x00" "c\x00\\\\)", 19));
  CHECK(escaped.string_utf8() == std::string("a(b)c\\"));
  auto dict = engine_dict_of({{"0", engine_integer(1)}, {"1", engine_list_of({engine_number(0.8), engine_number(1.0)})}});
  dict.set("0", engine_integer(2));
  CHECK(serialize_engine_data(dict, false) == " << /0 2 /1 [ .8 1.0 ] >>");
  CHECK(!parse_engine_data("<< /0 (unterminated", false).has_value());
  CHECK(!parse_engine_data("<< /0 1", false).has_value());
}

// A Patchy-born document gets a block from the Photoshop 2026 template: one object per type
// layer, indexed like the TySh, with the text, runs, paragraph metrics, box frame and fonts.
void text_engine_patchy_born_document_writes_a_block() {
  patchy::Document document(600, 400, patchy::PixelFormat::rgb8());
  document.add_layer(patchy_text_layer(document, "Point", "HHHHHHHHHH", false));
  document.add_layer(patchy_text_layer(document, "Box", "First paragraph\nSecond paragraph", true));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto payload = text_engine_payload(bytes);
  CHECK(payload.has_value());
  if (!payload.has_value()) {
    return;
  }
  const auto block = patchy::psd::TextEngineBlock::parse(*payload);
  CHECK(block.has_value());
  if (!block.has_value()) {
    return;
  }
  CHECK(block->object_count() == 2);
  CHECK(block->object_text(0) == std::string("HHHHHHHHHH\r"));
  CHECK(block->object_text(1) == std::string("First paragraph\rSecond paragraph\r"));
  CHECK(text_index_of(bytes, 0) == 0);
  CHECK(text_index_of(bytes, 1) == 1);
  const auto& root = block->root();
  // Version and engine version as Photoshop 2026 writes them.
  CHECK(root.at_path({"98", "0"})->integer() == 14);
  CHECK(root.at_path({"1", "4", "3"})->string_utf8() == std::string("Photoshop"));
  // Fonts: the template's two plus Arial for the runs.
  const auto* fonts = root.at_path({"0", "1", "0"});
  CHECK(fonts != nullptr && fonts->list.size() == 3);
  if (fonts != nullptr && fonts->list.size() == 3) {
    CHECK(fonts->list[0].at_path({"0", "0", "0"})->string_utf8() == std::string("AdobeInvisFont"));
    // The PostScript name comes from DirectWrite on Windows; other platforms keep the family.
    const auto arial = fonts->list[2].at_path({"0", "0", "0"})->string_utf8();
    CHECK(arial == std::string("ArialMT") || arial == std::string("Arial"));
  }
  // The box object: two paragraphs, the first carrying the indents (document px), Arial 24, the
  // run color, and a frame with the 400 x 180 path. No layout cache.
  const auto* box = root.at_path({"1", "1", "1"});
  CHECK(box != nullptr);
  if (box == nullptr) {
    return;
  }
  const auto* paragraphs = box->at_path({"0", "5", "0"});
  CHECK(paragraphs != nullptr && paragraphs->list.size() == 2);
  if (paragraphs != nullptr && paragraphs->list.size() == 2) {
    const auto* sheet = paragraphs->list[0].at_path({"0", "0", "5"});
    CHECK(sheet != nullptr);
    if (sheet != nullptr) {
      CHECK(sheet->find("1")->raw == "24.0");
      CHECK(sheet->find("2")->raw == "12.0");
      CHECK(sheet->find("5")->raw == "6.0");
      CHECK(sheet->find("0")->raw == "0");
      CHECK(sheet->find("36")->is_nil());
    }
    CHECK(paragraphs->list[0].find("1")->integer() == 16);
    CHECK(paragraphs->list[1].at_path({"0", "0", "5", "0"})->integer() == 2);
    CHECK(paragraphs->list[1].find("1")->integer() == 17);
  }
  // The run's unit flag is 0 (document pixels): 1 makes Photoshop read the sizes as points.
  CHECK(box->at_path({"0", "6", "0", "0", "0", "0", "5"})->integer() == 0);
  CHECK(box->at_path({"0", "5", "0", "0", "0", "0", "6"})->integer() == 0);
  const auto* style = box->at_path({"0", "6", "0", "0", "0", "0", "6"});
  CHECK(style != nullptr);
  if (style != nullptr) {
    CHECK(style->find("0")->integer() == 2);
    CHECK(style->find("1")->raw == "24.0");
    CHECK(style->find("4")->boolean() == true);
    CHECK(style->find("5")->raw == "28.8");
    CHECK(style->find("8")->raw == "0");
    CHECK(style->at_path({"53", "0", "1", "1"})->raw == ".12549");
  }
  CHECK(box->at_path({"1", "2"}) == nullptr);
  CHECK(block->object_frame_index(1) == 1);
  const auto* frame = root.at_path({"0", "8", "0", "1", "0"});
  CHECK(frame != nullptr);
  if (frame != nullptr) {
    const auto* path = frame->at_path({"1", "0"});
    CHECK(path != nullptr && path->list.size() == 32);
    if (path != nullptr && path->list.size() == 32) {
      // Corners (0,0), (w,0), (w,h), (0,h), each point written four times as x y pairs.
      CHECK(path->list[8].raw == "400.0");
      CHECK(path->list[9].raw == "0.0");
      CHECK(path->list[16].raw == "400.0");
      CHECK(path->list[17].raw == "180.0");
      CHECK(path->list[24].raw == "0.0");
      CHECK(path->list[25].raw == "180.0");
    }
    CHECK(frame->at_path({"2", "0"})->integer() == 1);
    CHECK(frame->at_path({"2", "11", "4"})->integer() == -2);
  }
  const auto* point_frame = root.at_path({"0", "8", "0", "0", "0"});
  CHECK(point_frame != nullptr && point_frame->find("1") == nullptr);
  CHECK(point_frame != nullptr && point_frame->at_path({"2", "11", "4"})->integer() == -1);
  // Reading the file back keeps the block (preserved) and the indices, and a second save of the
  // untouched document keeps the block byte for byte.
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const auto again = patchy::psd::DocumentIo::write_layered_rgb8(reread);
  CHECK(text_engine_payload(again) == payload);
  CHECK(text_index_of(again, 0) == 0);
  CHECK(text_index_of(again, 1) == 1);
}

// A Photoshop document with one retyped layer: that layer's object is replaced at its own index
// (caches stripped everywhere), the untouched layer keeps its object and index, and nothing is
// appended. A layer whose imported index is stale gets appended instead.
void text_engine_retyped_layer_replaces_its_object_in_place() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-tracking.psd");
  auto document = patchy::psd::DocumentIo::read_file(path);
  std::vector<std::size_t> text_layers;
  for (std::size_t i = 0; i < document.layers().size(); ++i) {
    if (patchy::layer_is_text(document.layers()[i])) {
      text_layers.push_back(i);
    }
  }
  CHECK(text_layers.size() == 2);
  if (text_layers.size() != 2) {
    return;
  }
  const auto original = text_engine_payload(read_all(path));
  CHECK(original.has_value());
  const auto original_block = original.has_value() ? patchy::psd::TextEngineBlock::parse(*original) : std::nullopt;
  CHECK(original_block.has_value() && original_block->object_count() == 2);

  auto& retyped = document.layers()[text_layers[0]];
  const auto stored_index = retyped.metadata().at(patchy::kLayerMetadataPsdTextIndex);
  retyped.metadata()[patchy::kLayerMetadataText] = "Retyped";
  retyped.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto payload = text_engine_payload(bytes);
  CHECK(payload.has_value());
  const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  CHECK(block.has_value());
  if (!block.has_value() || !original_block.has_value()) {
    return;
  }
  CHECK(block->object_count() == 2);
  const auto index = static_cast<std::size_t>(std::stoi(stored_index));
  CHECK(block->object_text(index) == std::string("Retyped\r"));
  CHECK(block->object_text(1 - index) == original_block->object_text(1 - index));
  CHECK(text_index_of(bytes, static_cast<int>(text_layers[0])) == static_cast<std::int32_t>(index));
  CHECK(text_index_of(bytes, static_cast<int>(text_layers[1])) == static_cast<std::int32_t>(1 - index));
  // Every object lost its layout cache, the untouched one included.
  CHECK(block->root().at_path({"1", "1", "0", "1", "2"}) == nullptr);
  CHECK(block->root().at_path({"1", "1", "1", "1", "2"}) == nullptr);
  // The untouched layer's model is byte-identical to Photoshop's.
  CHECK(patchy::psd::serialize_engine_data(*block->root().at_path({"1", "1", std::to_string(1 - index), "0"}), false) ==
        patchy::psd::serialize_engine_data(*original_block->root().at_path({"1", "1", std::to_string(1 - index), "0"}), false));

  // A stale index (an older Patchy save's 100000+n) appends instead of replacing.
  retyped.metadata()[patchy::kLayerMetadataPsdTextIndex] = "100000";
  const auto appended_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto appended = text_engine_payload(appended_bytes);
  const auto appended_block = appended.has_value() ? patchy::psd::TextEngineBlock::parse(*appended) : std::nullopt;
  CHECK(appended_block.has_value() && appended_block->object_count() == 3);
  CHECK(appended_block.has_value() && appended_block->object_text(2) == std::string("Retyped\r"));
  CHECK(text_index_of(appended_bytes, static_cast<int>(text_layers[0])) == 2);
}

void text_engine_area_frames_preserve_native_contours() {
  using namespace patchy;
  using namespace patchy::psd;
  for (const auto* name : {"photoshop-area-triangle.psd", "photoshop-area-concave.psd", "photoshop-area-ellipse.psd"}) {
    const auto bytes = read_all(test::committed_psd_fixture_path(name));
    const auto payload = text_engine_payload(bytes);
    CHECK(payload.has_value());
    if (!payload) continue;
    const auto engine = TextEngineBlock::parse(*payload);
    CHECK(engine.has_value());
    if (!engine) continue;
    const auto geometry = engine->object_geometry(0);
    CHECK(geometry.kind == TextFrameGeometry::Kind::Area);
    CHECK(geometry.area.has_value());
    if (!geometry.area) continue;
    const auto& anchors = geometry.area->subpaths.front().anchors;
    if (std::string_view(name) == "photoshop-area-triangle.psd") {
      CHECK(anchors.size() == 3);
      CHECK(anchors.front().anchor_x == 250.0);
      CHECK(anchors.front().anchor_y == 0.0);
      CHECK(anchors[1].anchor_x == 500.0 && anchors[1].anchor_y == 400.0);
    } else if (std::string_view(name) == "photoshop-area-concave.psd") {
      CHECK(anchors.size() == 8);
      CHECK(anchors[4].anchor_x == 320.0 && anchors[4].anchor_y == 150.0);
    } else {
      CHECK(anchors.size() == 4);
      CHECK(std::abs(anchors.front().out_x - 388.0712) < 0.001);
    }
    auto document = DocumentIo::read(bytes);
    auto text = std::find_if(document.layers().begin(), document.layers().end(), [](const Layer& l) {
      return layer_is_text(l);
    });
    CHECK(text != document.layers().end());
    if (text == document.layers().end()) continue;
    CHECK(text_area_for_layer(*text) == geometry.area);
    CHECK(!text_geometry_is_protected(*text));
    // Untouched geometry retains its native frame. Editing causes both the layer
    // and document text object to be authored from the same local boundary.
    for (bool edited : {false, true}) {
      if (edited) {
        text->metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
        text->metadata()[kLayerMetadataText] = "Edited area";
      }
      const auto saved = DocumentIo::write_layered_rgb8(document);
      const auto saved_payload = text_engine_payload(saved);
      CHECK(saved_payload.has_value());
      if (!saved_payload) continue;
      const auto saved_engine = TextEngineBlock::parse(*saved_payload);
      CHECK(saved_engine.has_value());
      if (!saved_engine) continue;
      const auto saved_area = saved_engine->object_geometry(0).area;
      CHECK(saved_area.has_value());
      CHECK(saved_area == geometry.area);
    }
  }
}

void text_engine_area_frames_reject_unrecognized_geometry() {
  using namespace patchy::psd;
  auto engine = TextEngineBlock::from_template();
  TextEngineInputs input;
  input.text = "Area\r";
  input.boxed = true;
  engine.append_object(input);
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Box);
  auto* marker = engine.root().at_path({"0", "8", "0", "0", "0", "2", "6"});
  CHECK(marker != nullptr);
  if (!marker) return;
  *marker = engine_list_of({engine_number(-3), engine_number(-3)});
  // The box writer's legacy repeated-corner format isn't a closed cubic stream.
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
  *marker = engine_list_of({engine_number(-4), engine_number(-4)});
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
}

void text_engine_named_frames_keep_point_and_box_editable() {
  using namespace patchy::psd;
  const std::string source = R"ED(
    /DocumentResources << /TextFrameSet << /Resources [
      << /Resource << /Data << /TextOnPathTRange [-1 -1] >> >> >>
      << /Resource << /Data << /TextOnPathTRange [-2 -2] >> >> >>
      << /Resource << /Data << /TextOnPathTRange [0 100] >> >> >>
    ] >> >>
    /DocumentObjects << /TextObjects [
      << /View << /Frames [ << /Resource 1 >> ] >> >>
      << /View << /Frames [ << /Resource 0 >> ] >> >>
      << /View << /Frames [ << /Resource 2 >> ] >> >>
    ] >>
  )ED";
  const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(source.data()), source.size());
  const auto frames = read_text_frame_geometries(bytes);
  CHECK(frames.size() == 3);
  CHECK(frames[0].kind == TextFrameGeometry::Kind::Box);
  CHECK(frames[1].kind == TextFrameGeometry::Kind::Point);
  CHECK(frames[2].kind == TextFrameGeometry::Kind::Unsupported);
}

void text_engine_area_overflow_and_protected_rasters() {
  using namespace patchy;
  using namespace patchy::psd;
  auto document=DocumentIo::read_file(test::committed_psd_fixture_path("photoshop-area-empty.psd"));
  auto text=std::find_if(document.layers().begin(),document.layers().end(),[](const Layer& l){return layer_is_text(l);});
  CHECK(text!=document.layers().end());if(text==document.layers().end())return;
  const auto area=text_area_for_layer(*text);CHECK(area.has_value());
  const auto blank=[](const Layer& layer) {
    const auto& pixels=layer.pixels();
    for(int y=0;y<pixels.height();++y) for(int x=0;x<pixels.width();++x)
      if(pixels.pixel(x,y)[3]!=0)return false;
    return true;
  };
  CHECK(blank(*text));
  text->metadata()[kLayerMetadataText]="";
  text->metadata()[kLayerMetadataTextRasterStatus]="patchy_raster";
  const auto saved=DocumentIo::write_layered_rgb8(document);
  auto reopened=DocumentIo::read(saved);
  auto layer=std::find_if(reopened.layers().begin(),reopened.layers().end(),[](const Layer& l){return layer_is_text(l);});
  CHECK(layer!=reopened.layers().end());if(layer==reopened.layers().end())return;
  CHECK(text_area_for_layer(*layer)==area);
  CHECK(layer->metadata().at(kLayerMetadataText).empty());
  CHECK(blank(*layer));

  // Unknown native geometry is kept and protected, including a blank saved
  // raster. It must never become a rectangular placeholder on import.
  for(auto& resource:reopened.metadata().unknown_psd_resources) if(resource.key=="Txt2") {
    auto engine=TextEngineBlock::parse(resource.payload);CHECK(engine.has_value());if(!engine)continue;
    *engine->root().at_path({"0","8","0","0","0","2","6"})=engine_list_of({engine_number(-4),engine_number(-4)});
    resource.payload=engine->serialize();
  }
  layer->metadata()[kLayerMetadataTextRasterStatus]="psd_raster_preview";
  const auto protected_doc=DocumentIo::read(DocumentIo::write_layered_rgb8(reopened));
  for(const auto& candidate:protected_doc.layers()) if(layer_is_text(candidate)) {
    CHECK(text_geometry_is_protected(candidate));
    CHECK(blank(candidate));
  }
  CHECK(!parse_vector_path("v1 0 0 999999999"));
  CHECK(!parse_vector_path("v1 0 0 1 S 1 1 0 999999999"));

  // An engine we cannot author must not silently discard a newly added area
  // boundary. The writer fails before producing replacement file bytes.
  auto legacy = DocumentIo::read_file(test::committed_psd_fixture_path("photoshop-area-triangle.psd"));
  for (auto& block : legacy.metadata().unknown_psd_resources) if (block.key == "Txt2") {
    const std::string named = "/DocumentResources << >> /DocumentObjects << >>";
    block.payload.assign(named.begin(), named.end());
  }
  bool refused = false;
  try { (void)DocumentIo::write_layered_rgb8(legacy); }
  catch (const std::runtime_error&) { refused = true; }
  CHECK(refused);
}

std::vector<patchy::test::TestCase> text_engine_block_tests() {
  return {
      {"text_engine_area_frames_preserve_native_contours", text_engine_area_frames_preserve_native_contours},
      {"text_engine_named_frames_keep_point_and_box_editable", text_engine_named_frames_keep_point_and_box_editable},
      {"text_engine_area_frames_reject_unrecognized_geometry", text_engine_area_frames_reject_unrecognized_geometry},
      {"text_engine_area_overflow_and_protected_rasters", text_engine_area_overflow_and_protected_rasters},
      {"text_engine_photoshop_blocks_round_trip_byte_exact", text_engine_photoshop_blocks_round_trip_byte_exact},
      {"text_engine_syntax_parses_and_authors_tokens", text_engine_syntax_parses_and_authors_tokens},
      {"text_engine_patchy_born_document_writes_a_block", text_engine_patchy_born_document_writes_a_block},
      {"text_engine_retyped_layer_replaces_its_object_in_place", text_engine_retyped_layer_replaces_its_object_in_place},
  };
}
