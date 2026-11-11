#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>
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
#include <cstdlib>
#include <cstdio>
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

// Each style run's Txt2 /38 for the object a TySh points at.
std::vector<long long> txt2_run_languages(const std::vector<std::uint8_t>& bytes, int layer_index) {
  std::vector<long long> languages;
  const auto payload = text_engine_payload(bytes);
  const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  const auto index = text_index_of(bytes, layer_index);
  if (!block.has_value() || !index.has_value()) {
    return languages;
  }
  const auto* runs = block->root().at_path({"1", "1", std::to_string(*index), "0", "6", "0"});
  if (runs == nullptr || !runs->is_list()) {
    return languages;
  }
  for (const auto& run : runs->list) {
    const auto* language = run.at_path({"0", "0", "6", "38"});
    languages.push_back(language != nullptr ? language->integer().value_or(-1) : -1);
  }
  return languages;
}

// Each /Language a layer's TySh EngineData names, in order.
std::vector<int> tysh_languages(const std::vector<std::uint8_t>& bytes, int layer_index) {
  std::vector<int> languages;
  const auto payload = psd_layer_block_payload(psd_layer_extra_data(bytes, static_cast<std::int16_t>(layer_index)), "TySh");
  if (!payload.has_value()) {
    return languages;
  }
  const std::string_view text(reinterpret_cast<const char*>(payload->data()), payload->size());
  static constexpr std::string_view kKey = "/Language ";
  for (auto at = text.find(kKey); at != std::string_view::npos; at = text.find(kKey, at + kKey.size())) {
    languages.push_back(std::atoi(std::string(text.substr(at + kKey.size(), 4)).c_str()));
  }
  return languages;
}

// The language of every run in a layer's runs metadata (-1 unset), from column 16.
std::vector<int> metadata_run_languages(const patchy::Layer& layer) {
  std::vector<int> languages;
  const auto found = layer.metadata().find(patchy::kLayerMetadataTextRuns);
  if (found == layer.metadata().end()) {
    return languages;
  }
  std::string_view rest = found->second;
  while (!rest.empty()) {
    const auto end = rest.find('\n');
    const auto line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1U);
    if (line.empty() || line.front() == 'v') {
      continue;
    }
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
      const auto tab = line.find('\t', start);
      fields.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
      if (tab == std::string_view::npos) break;
      start = tab + 1U;
    }
    patchy::psd::PsdTextStyleRun run;
    if (fields.size() >= 17U) {
      patchy::psd::parse_text_run_character_styles(fields[16], run);
    }
    languages.push_back(run.language.value_or(-1));
  }
  return languages;
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
  // CS5-era blocks end in NUL padding counted in the block length; only a NUL-only tail is accepted.
  const auto padded = source + std::string("\0\0", 2);
  const auto padded_root = parse_engine_data(padded, /*bare_root=*/true);
  CHECK(padded_root.has_value());
  if (padded_root.has_value()) {
    CHECK(serialize_engine_data(*padded_root, /*bare_root=*/true) == padded);
    CHECK(padded_root->at_path({"98", "0"})->integer() == 14);
  }
  CHECK(!parse_engine_data(std::string("/0 1 \0 /1 2", 11), /*bare_root=*/true).has_value());
}

// A Patchy-born document gets a block from the Photoshop 2026 template: one object per type
// layer, indexed like the TySh, with the text, runs, paragraph metrics, box frame and fonts.
void text_engine_patchy_born_document_writes_a_block() {
  patchy::Document document(600, 400, patchy::PixelFormat::rgb8());
  document.add_layer(patchy_text_layer(document, "Point", "HHHHHHHHHH", false));
  document.add_layer(patchy_text_layer(document, "Box", "First paragraph\nSecond paragraph", true));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream("test-artifacts/text-engine-patchy-born.psd", std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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
  // run color, and a frame with the 400 x 180 path. An empty layout cache.
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
  CHECK(box->at_path({"1", "2"}) != nullptr && box->at_path({"1", "2"})->is_list() &&
        box->at_path({"1", "2"})->list.empty());
  CHECK(block->object_frame_index(1) == 1);
  const auto* frame = root.at_path({"0", "8", "0", "1", "0"});
  CHECK(frame != nullptr);
  if (frame != nullptr) {
    const auto* path = frame->at_path({"1", "0"});
    CHECK(path != nullptr && path->list.size() == 32);
    if (path != nullptr && path->list.size() == 32) {
      // One straight segment per edge (start, start, end, end), Photoshop's own layout
      // (wt_arch_p50_para.psd): its corner-repeated predecessor made a warped box layer
      // prompt to update in Photoshop 2025 and 2026.
      const char* expected[32] = {"0.0",   "0.0",   "0.0",   "0.0",   "400.0", "0.0",   "400.0", "0.0",
                                  "400.0", "0.0",   "400.0", "0.0",   "400.0", "180.0", "400.0", "180.0",
                                  "400.0", "180.0", "400.0", "180.0", "0.0",   "180.0", "0.0",   "180.0",
                                  "0.0",   "180.0", "0.0",   "180.0", "0.0",   "0.0",   "0.0",   "0.0"};
      for (std::size_t i = 0; i < 32; ++i) {
        CHECK(path->list[i].raw == expected[i]);
      }
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
// (caches emptied everywhere), the untouched layer keeps its object and index, and nothing is
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
  // Every object's layout cache is emptied, the untouched one included, but the key stays:
  // Photoshop 2025 crashes opening a block whose objects all lack it (GitHub issue 341).
  for (const auto* object : {"0", "1"}) {
    const auto* cache = block->root().at_path({"1", "1", object, "1", "2"});
    CHECK(cache != nullptr && cache->is_list() && cache->list.empty());
  }
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
  // The box writer's legacy repeated-corner format (each corner four times, older Patchy saves)
  // isn't a closed cubic stream; today's writer emits Photoshop's edge segments instead.
  auto* contour = engine.root().at_path({"0", "8", "0", "0", "0", "1", "0"});
  CHECK(contour != nullptr);
  if (!contour) return;
  contour->list.clear();
  for (const auto& corner : {std::pair{0.0, 0.0}, {100.0, 0.0}, {100.0, 50.0}, {0.0, 50.0}}) {
    for (int repeat = 0; repeat < 4; ++repeat) {
      contour->list.push_back(engine_number(corner.first));
      contour->list.push_back(engine_number(corner.second));
    }
  }
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
  *marker = engine_list_of({engine_number(-4), engine_number(-4)});
  CHECK(engine.object_geometry(0).kind == TextFrameGeometry::Kind::Unsupported);
}

// GitHub issue #86: a NUL-padded CS5 block failed to parse, so every type layer came in protected.
void text_engine_nul_padded_block_keeps_frames_editable() {
  using namespace patchy::psd;
  auto engine = TextEngineBlock::from_template();
  TextEngineInputs input;
  input.text = "Box\r";
  input.boxed = true;
  engine.append_object(input);
  input.text = "Point\r";
  input.boxed = false;
  engine.append_object(input);
  auto bytes = engine.serialize();
  bytes.push_back(0);
  bytes.push_back(0);
  CHECK(TextEngineBlock::parse(bytes).has_value());
  const auto frames = read_text_frame_geometries(bytes);
  CHECK(frames.size() == 2);
  if (frames.size() != 2) return;
  CHECK(frames[0].kind == TextFrameGeometry::Kind::Box);
  CHECK(frames[1].kind == TextFrameGeometry::Kind::Point);
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

// The issue 341 reporter's grayscale document: Photoshop 22.4, 45 type layers. Photoshop 2025
// (26.9) crashed opening Patchy's save of it because no text object kept its /1/2 cache key;
// every object, the retyped one and the untouched ones, now carries an empty one.
void text_engine_issue341_every_object_keeps_a_cache_key_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-341/grayscale photoshop.psd");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::Layer* retyped = nullptr;
  for (auto& layer : document.layers()) {
    if (layer.name() == "esprit") {
      retyped = &layer;
    }
  }
  CHECK(retyped != nullptr);
  if (retyped == nullptr) {
    return;
  }
  retyped->metadata()[patchy::kLayerMetadataText] = "esprits";
  retyped->metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream("test-artifacts/issue341-retyped-save.psd", std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  const auto payload = text_engine_payload(bytes);
  CHECK(payload.has_value());
  const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  CHECK(block.has_value() && block->object_count() == 45);
  if (!block.has_value()) {
    return;
  }
  int retyped_objects = 0;
  for (std::size_t i = 0; i < block->object_count(); ++i) {
    const auto* cache = block->root().at_path({"1", "1", std::to_string(i), "1", "2"});
    CHECK(cache != nullptr && cache->is_list() && cache->list.empty());
    if (block->object_text(i) == std::string("esprits\r")) {
      ++retyped_objects;
    }
  }
  CHECK(retyped_objects == 1);
}

// Photoshop's paragraph composer lives only in the Txt2 (paragraph key /35, never the TySh):
// World-Ready (1) aligns a paragraph's last line with its last glyph kerned against a space,
// Latin and East Asian (0) without (docs/text-render-calibration.md, "Tracking and alignment").
// A World-Ready import keeps the shorter paragraph runs; a Latin one survives the import as the
// v5 column and a regenerating save as its /35.
void text_engine_paragraph_composer_round_trips() {
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-track-box-right-m80.psd");
  auto bytes = read_all(path);
  const auto text_layer = [](const patchy::Document& document) -> const patchy::Layer* {
    for (const auto& layer : document.layers()) {
      if (patchy::layer_is_text(layer)) {
        return &layer;
      }
    }
    return nullptr;
  };
  const auto paragraph_runs = [](const patchy::Layer& layer) {
    const auto found = layer.metadata().find(patchy::kLayerMetadataTextParagraphRuns);
    return found != layer.metadata().end() ? found->second : std::string();
  };
  const auto saved_composers = [](patchy::Document& document) {
    for (auto& layer : document.layers()) {
      if (patchy::layer_is_text(layer)) {
        layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
      }
    }
    const auto payload = text_engine_payload(patchy::psd::DocumentIo::write_layered_rgb8(document));
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    std::vector<int> composers;
    if (block.has_value() && block->object_count() > 0) {
      for (const auto& entry : block->object_paragraph_composers(0)) {
        composers.push_back(entry.composer.value_or(-1));
      }
    }
    return composers;
  };

  auto world_ready = patchy::psd::DocumentIo::read(bytes);
  const auto* layer = text_layer(world_ready);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(paragraph_runs(*layer).rfind("v5", 0) != 0);
  CHECK(saved_composers(world_ready) == std::vector<int>{1});

  // The same file with its paragraph run switched to the Latin composer (one byte: "1" -> "0").
  const auto payload = text_engine_payload(bytes);
  auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  CHECK(block.has_value() && block->object_count() == 1);
  if (!block.has_value() || block->object_count() != 1) {
    return;
  }
  CHECK(block->object_paragraph_composers(0).size() == 1 && block->object_paragraph_composers(0)[0].composer == 1);
  auto* sheet = block->root().at_path({"1", "1", "0", "0", "5", "0", "0", "0", "0", "5"});
  auto* composer = sheet != nullptr ? sheet->find("35") : nullptr;
  CHECK(composer != nullptr && composer->raw == "1");
  if (composer == nullptr) {
    return;
  }
  composer->raw = "0";
  const auto edited = block->serialize();
  CHECK(edited.size() == payload->size());
  if (edited.size() != payload->size()) {
    return;
  }
  static constexpr std::string_view kMarker = "8BIMTxt2";
  const auto at = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
  std::copy(edited.begin(), edited.end(), at + static_cast<std::ptrdiff_t>(kMarker.size() + 4));

  auto latin = patchy::psd::DocumentIo::read(bytes);
  layer = text_layer(latin);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto runs = paragraph_runs(*layer);
  CHECK(runs.rfind("v5\n", 0) == 0);
  CHECK(runs.size() >= 2 && runs.substr(runs.size() - 2) == "\t0");
  CHECK(saved_composers(latin) == std::vector<int>{0});
}

// Photoshop's justify modes (#672, PS 2026 DOM readback): EngineData /Justification 4 is
// justify last right, 5 last centered, 6 justify all. Each paragraph-runs name writes its value
// into the TySh and the Txt2 paragraph sheet (/0) and reads back as the same name.
void text_engine_justify_modes_round_trip() {
  patchy::Document document(420, 240, patchy::PixelFormat::rgb8());
  auto layer = patchy_text_layer(document, "Justify", "a\nb\nc\nd\ne\nf\ng", true);
  const std::string runs =
      "v1\n0\t2\tleft\n2\t2\tcenter\n4\t2\tright\n6\t2\tjustify\n8\t2\tjustifyCenter\n10\t2\tjustifyRight\n"
      "12\t1\tjustifyAll";
  layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = runs;
  document.add_layer(std::move(layer));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);

  const auto tysh = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "TySh");
  CHECK(tysh.has_value());
  if (tysh.has_value()) {
    const std::string text(tysh->begin(), tysh->end());
    for (const auto* value : {"/Justification 1", "/Justification 2", "/Justification 3", "/Justification 4",
                              "/Justification 5", "/Justification 6"}) {
      CHECK(text.find(value) != std::string::npos);
    }
  }

  const auto payload = text_engine_payload(bytes);
  const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  const auto index = text_index_of(bytes, 0);
  CHECK(block.has_value() && index.has_value());
  if (block.has_value() && index.has_value()) {
    const auto* paragraphs = block->root().at_path({"1", "1", std::to_string(*index), "0", "5", "0"});
    CHECK(paragraphs != nullptr);
    std::vector<long long> justifications;
    if (paragraphs != nullptr) {
      for (const auto& entry : paragraphs->list) {
        const auto* value = entry.at_path({"0", "0", "5", "0"});
        justifications.push_back(value != nullptr ? value->integer().value_or(-1) : -1);
      }
    }
    CHECK((justifications == std::vector<long long>{0, 2, 1, 3, 5, 4, 6}));
  }

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 1U);
  if (read.layers().size() == 1U) {
    const auto& read_runs = read.layers().front().metadata().at(patchy::kLayerMetadataTextParagraphRuns);
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start < read_runs.size()) {
      auto end = read_runs.find('\n', start);
      if (end == std::string::npos) end = read_runs.size();
      const auto line = read_runs.substr(start, end - start);
      const auto first_tab = line.find('\t');
      const auto second_tab = first_tab == std::string::npos ? first_tab : line.find('\t', first_tab + 1);
      if (second_tab != std::string::npos) {
        const auto third_tab = line.find('\t', second_tab + 1);
        names.push_back(line.substr(second_tab + 1, third_tab == std::string::npos ? std::string::npos
                                                                                  : third_tab - second_tab - 1));
      }
      start = end + 1;
    }
    CHECK((names == std::vector<std::string>{"left", "center", "right", "justify", "justifyCenter", "justifyRight",
                                             "justifyAll"}));
  }
}

// GitHub issue 682: each run's hyphenation and spelling language survives a save and a reopen,
// in the Txt2 /38 numbering with the TySh /Language twin in its older table (German 42 is TySh 4,
// Arabic 39 is TySh 37). A run without one writes Photoshop's default 14 to the Txt2 and nothing
// to the TySh, exactly as before.
void text_engine_run_languages_round_trip() {
  patchy::Document document(600, 200, patchy::PixelFormat::rgb8());
  auto tagged = patchy_text_layer(document, "Tagged", "English Deutsch Arabic", false);
  const std::string columns = "\t30\t0\t0\t#202020\tArial\tauto\t0\t1\t1\t0\t\t0\t0\t0\t";
  tagged.metadata()[patchy::kLayerMetadataTextRuns] =
      "v9\n0\t8" + columns + "-\n8\t8" + columns + "language:42\n16\t6" + columns + "underline,language:39";
  document.add_layer(std::move(tagged));
  document.add_layer(patchy_text_layer(document, "Plain", "Untagged text", false));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream("test-artifacts/text-engine-run-languages.psd", std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  CHECK((txt2_run_languages(bytes, 0) == std::vector<long long>{14, 42, 39}));
  CHECK((tysh_languages(bytes, 0) == std::vector<int>{4, 37}));
  CHECK((txt2_run_languages(bytes, 1) == std::vector<long long>{14}));
  CHECK(tysh_languages(bytes, 1).empty());

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2U);
  if (read.layers().size() != 2U) {
    return;
  }
  CHECK((metadata_run_languages(read.layers()[0]) == std::vector<int>{-1, 42, 39}));
  CHECK((metadata_run_languages(read.layers()[1]) == std::vector<int>{-1}));
  CHECK(read.layers()[1].metadata().at(patchy::kLayerMetadataTextRuns).find("language") == std::string::npos);
  // An edit regenerates both blocks from the runs: the tags stay on the same runs.
  for (auto& layer : read.layers()) {
    layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  }
  const auto again = patchy::psd::DocumentIo::write_layered_rgb8(read);
  CHECK((txt2_run_languages(again, 0) == std::vector<long long>{14, 42, 39}));
  CHECK((tysh_languages(again, 0) == std::vector<int>{4, 37}));
  CHECK(tysh_languages(again, 1).empty());
  const auto reread = patchy::psd::DocumentIo::read(again);
  CHECK(reread.layers().size() == 2U);
  if (reread.layers().size() == 2U) {
    CHECK((metadata_run_languages(reread.layers()[0]) == std::vector<int>{-1, 42, 39}));
  }
}

// The two language tables, pinned by Photoshop 2026 COM captures (docs/txt2.md, "Languages"), and
// the runs token.
void text_engine_language_tables_match_photoshop() {
  using patchy::psd::photoshop_tysh_language_from_txt2;
  using patchy::psd::photoshop_txt2_language_from_tysh;
  const std::vector<std::pair<int, int>> txt2_to_tysh{
      {0, 0},   {1, 1},   {2, 2},   {3, 3},   {5, 5},   {7, 7},   {8, 8},   {9, 9},   {10, 10}, {11, 11},
      {12, 12}, {13, 13}, {14, 14}, {16, 16}, {17, 17}, {18, 18}, {20, 20}, {21, 21}, {22, 22}, {23, 23},
      {24, 24}, {25, 25}, {26, 26}, {27, 27}, {28, 28}, {42, 4},  {44, 6},  {45, 14}, {43, 15}, {31, 29},
      {32, 30}, {33, 31}, {36, 34}, {37, 35}, {38, 36}, {39, 37}, {41, 37}, {29, 0},  {30, 0},  {40, 0},
      {46, 0},  {47, 0},  {49, 0},  {51, 0},  {62, 0},  {63, 0}};
  for (const auto& [txt2, tysh] : txt2_to_tysh) {
    if (photoshop_tysh_language_from_txt2(txt2) != tysh) {
      std::printf("  Txt2 %d wrote TySh %d, expected %d\n", txt2, photoshop_tysh_language_from_txt2(txt2), tysh);
    }
    CHECK(photoshop_tysh_language_from_txt2(txt2) == tysh);
  }
  CHECK(photoshop_txt2_language_from_tysh(4) == 42);
  CHECK(photoshop_txt2_language_from_tysh(37) == 39);
  CHECK(photoshop_txt2_language_from_tysh(14) == 14);
  CHECK(photoshop_txt2_language_from_tysh(0) == 0);
  CHECK(photoshop_txt2_language_from_tysh(2) == 2);
  CHECK(photoshop_txt2_language_from_tysh(29) == 31);

  patchy::psd::PsdTextStyleRun run;
  patchy::psd::parse_text_run_character_styles("underline,language:42", run);
  CHECK(run.underline && run.language == 42);
  CHECK(patchy::psd::serialize_text_run_character_styles(run) == "underline,language:42");
  patchy::psd::parse_text_run_character_styles("language:x,language:", run);
  CHECK(!run.language.has_value() && !run.underline);
  run.language = 0;
  CHECK(patchy::psd::text_run_has_character_styles(run));
  CHECK(patchy::psd::serialize_text_run_character_styles(run) == "language:0");
}

// A Photoshop 2026 file tagged English UK, German and Arabic per run keeps its tags through an
// edit and a Patchy save (local fixture; README.txt beside it says how it was made).
void text_engine_photoshop_languages_survive_an_edit() {
  const auto path = patchy::test::local_psd_fixture_path("issue-682/photoshop-text-languages.psd");
  patchy::test::skip_unless_exists(path);
  const auto original = read_all(path);
  auto document = patchy::psd::DocumentIo::read(original);
  patchy::Layer* layer = nullptr;
  for (auto& candidate : document.layers()) {
    if (candidate.metadata().contains(patchy::kLayerMetadataText)) {
      layer = &candidate;
    }
  }
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK((metadata_run_languages(*layer) == std::vector<int>{-1, 42, 39}));
  layer->metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  std::ofstream("test-artifacts/text-engine-photoshop-languages-edited.psd", std::ios::binary)
      .write(reinterpret_cast<const char*>(written.data()), static_cast<std::streamsize>(written.size()));
  int text_layer_index = -1;
  for (int index = 0; index < static_cast<int>(document.layers().size()); ++index) {
    if (text_index_of(written, index).has_value()) {
      text_layer_index = index;
    }
  }
  CHECK(text_layer_index >= 0);
  CHECK((txt2_run_languages(written, text_layer_index) == std::vector<long long>{14, 42, 39}));
  CHECK((tysh_languages(written, text_layer_index) == std::vector<int>{4, 37}));
  const auto reread = patchy::psd::DocumentIo::read(written);
  bool found = false;
  for (const auto& candidate : reread.layers()) {
    if (candidate.metadata().contains(patchy::kLayerMetadataText)) {
      found = true;
      CHECK((metadata_run_languages(candidate) == std::vector<int>{-1, 42, 39}));
    }
  }
  CHECK(found);
}

namespace {

// The sparse paragraph (key "5") or style (key "6") sheet of each run of the Txt2 object a
// layer's TySh points at.
std::vector<const patchy::psd::EngineNode*> txt2_run_sheets(const patchy::psd::TextEngineBlock& block,
                                                            std::int32_t object, const char* runs_key) {
  std::vector<const patchy::psd::EngineNode*> sheets;
  const auto* runs = block.root().at_path({"1", "1", std::to_string(object), "0", runs_key, "0"});
  if (runs == nullptr || !runs->is_list()) {
    return sheets;
  }
  for (const auto& run : runs->list) {
    sheets.push_back(run.at_path({"0", "0", runs_key}));
  }
  return sheets;
}

long long sheet_integer(const patchy::psd::EngineNode* sheet, const char* key) {
  const auto* node = sheet != nullptr ? sheet->find(key) : nullptr;
  return node != nullptr ? node->integer().value_or(-1) : -1;
}

double sheet_number(const patchy::psd::EngineNode* sheet, const char* key) {
  const auto* node = sheet != nullptr ? sheet->find(key) : nullptr;
  return node != nullptr ? node->number().value_or(-9999.0) : -9999.0;
}

// 1 true, 0 false, -1 absent.
int sheet_flag(const patchy::psd::EngineNode* sheet, const char* key) {
  const auto* node = sheet != nullptr ? sheet->find(key) : nullptr;
  const auto value = node != nullptr ? node->boolean() : std::nullopt;
  return value.has_value() ? (*value ? 1 : 0) : -1;
}

std::string tysh_text(const std::vector<std::uint8_t>& bytes, int layer_index) {
  const auto payload = psd_layer_block_payload(psd_layer_extra_data(bytes, static_cast<std::int16_t>(layer_index)), "TySh");
  return payload.has_value() ? std::string(reinterpret_cast<const char*>(payload->data()), payload->size())
                             : std::string();
}

// The paragraph runs of a TySh ("/ParagraphRun" up to "/StyleRun"), where every run names its
// own hyphenation; the normal paragraph sheet in the ResourceDict comes later.
std::string tysh_paragraph_runs(const std::vector<std::uint8_t>& bytes, int layer_index) {
  const auto text = tysh_text(bytes, layer_index);
  const auto start = text.find("/ParagraphRun");
  const auto end = text.find("/StyleRun");
  return start != std::string::npos && end != std::string::npos && end > start ? text.substr(start, end - start)
                                                                               : std::string();
}

std::size_t count_of(std::string_view haystack, std::string_view needle) {
  std::size_t count = 0;
  for (auto at = haystack.find(needle); at != std::string_view::npos; at = haystack.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

// One column of every run line of a runs or paragraph-runs metadata value ("" when absent).
std::vector<std::string> metadata_run_column(const patchy::Layer& layer, const char* key, std::size_t column) {
  std::vector<std::string> values;
  const auto found = layer.metadata().find(key);
  if (found == layer.metadata().end()) {
    return values;
  }
  std::string_view rest = found->second;
  while (!rest.empty()) {
    const auto end = rest.find('\n');
    const auto line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1U);
    if (line.empty() || line.front() == 'v') {
      continue;
    }
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
      const auto tab = line.find('\t', start);
      fields.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
      if (tab == std::string_view::npos) break;
      start = tab + 1U;
    }
    values.emplace_back(column < fields.size() ? fields[column] : std::string_view{});
  }
  return values;
}

void write_artifact(const char* name, const std::vector<std::uint8_t>& bytes) {
  std::filesystem::create_directories("test-artifacts");
  std::ofstream(std::string("test-artifacts/") + name, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void mark_text_layers_edited(patchy::Document& document) {
  for (auto& layer : document.layers()) {
    if (layer.metadata().contains(patchy::kLayerMetadataText)) {
      layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    }
  }
}

}  // namespace

// GitHub issue 683: a Patchy save wrote "/AutoHyphenate true" into every paragraph it rewrote, so
// Photoshop showed Hyphenate on. A new layer now writes it off (as Photoshop 2026 writes one),
// and a paragraph that has Hyphenate on, its own hyphenation settings or a Kashida width keeps
// them in the TySh and the Txt2 alike, through a reopen and another save.
void text_engine_hyphenation_round_trips() {
  patchy::Document document(600, 300, patchy::PixelFormat::rgb8());
  document.add_layer(patchy_text_layer(document, "New", "A new Patchy layer", false));
  auto kept = patchy_text_layer(document, "Kept", "Hyphenated paragraph\nPlain paragraph", false);
  kept.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v6\n0\t21\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\thyphenate,hyphenation:7:3:4:5:30:0,kashida:0\n"
      "21\t15\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-";
  document.add_layer(std::move(kept));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  write_artifact("text-engine-hyphenation.psd", bytes);

  const auto check_written = [](const std::vector<std::uint8_t>& written) {
    // The new layer: Hyphenate off in its paragraph run, Photoshop's default numbers.
    const auto plain = tysh_paragraph_runs(written, 0);
    CHECK(count_of(plain, "/AutoHyphenate false /HyphenatedWordSize 6 /PreHyphen 2 /PostHyphen 2 "
                          "/ConsecutiveHyphens 8 /Zone 36.0 ") == 1U);
    CHECK(count_of(plain, "/AutoHyphenate true") == 0U);
    // The normal paragraph sheet (in the ResourceDict and its DocumentResources twin) keeps
    // Photoshop's "true".
    CHECK(count_of(tysh_text(written, 0), "/AutoHyphenate true") == 2U);
    const auto kept_runs = tysh_paragraph_runs(written, 1);
    CHECK(count_of(kept_runs, "/AutoHyphenate true /HyphenatedWordSize 7 /PreHyphen 3 /PostHyphen 4 "
                              "/ConsecutiveHyphens 5 /Zone 30.0 ") == 1U);
    CHECK(count_of(kept_runs, "/AutoHyphenate false /HyphenatedWordSize 6 ") == 1U);

    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    const auto plain_index = text_index_of(written, 0);
    const auto kept_index = text_index_of(written, 1);
    CHECK(block.has_value() && plain_index.has_value() && kept_index.has_value());
    if (!block.has_value() || !plain_index.has_value() || !kept_index.has_value()) {
      return;
    }
    const auto plain_sheets = txt2_run_sheets(*block, *plain_index, "5");
    CHECK(plain_sheets.size() == 1U);
    if (plain_sheets.size() == 1U) {
      CHECK(sheet_flag(plain_sheets[0], "9") == 0);
      CHECK(sheet_integer(plain_sheets[0], "10") == 6 && sheet_integer(plain_sheets[0], "13") == 8);
      CHECK(sheet_flag(plain_sheets[0], "15") == 1);
      CHECK(sheet_integer(plain_sheets[0], "40") == 2);
    }
    const auto kept_sheets = txt2_run_sheets(*block, *kept_index, "5");
    CHECK(kept_sheets.size() == 2U);
    if (kept_sheets.size() == 2U) {
      CHECK(sheet_flag(kept_sheets[0], "9") == 1);
      CHECK(sheet_integer(kept_sheets[0], "10") == 7 && sheet_integer(kept_sheets[0], "11") == 3);
      CHECK(sheet_integer(kept_sheets[0], "12") == 4 && sheet_integer(kept_sheets[0], "13") == 5);
      CHECK(sheet_number(kept_sheets[0], "14") == 30.0);
      CHECK(sheet_flag(kept_sheets[0], "15") == 0);
      CHECK(sheet_integer(kept_sheets[0], "40") == 0);
      CHECK(sheet_flag(kept_sheets[1], "9") == 0);
      CHECK(sheet_integer(kept_sheets[1], "40") == 2);
    }
  };
  check_written(bytes);

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2U);
  if (read.layers().size() != 2U) {
    return;
  }
  // The new layer's paragraph keeps its short form: no settings column.
  CHECK(read.layers()[0].metadata().at(patchy::kLayerMetadataTextParagraphRuns).rfind("v6", 0) != 0);
  CHECK((metadata_run_column(read.layers()[1], patchy::kLayerMetadataTextParagraphRuns, 11) ==
         std::vector<std::string>{"hyphenate,hyphenation:7:3:4:5:30:0,kashida:0", "-"}));
  mark_text_layers_edited(read);
  check_written(patchy::psd::DocumentIo::write_layered_rgb8(read));

  // The column's tokens.
  patchy::psd::PsdTextParagraphRun run;
  CHECK(patchy::psd::serialize_paragraph_run_settings(run) == "-");
  patchy::psd::parse_paragraph_run_settings("kashida:4,hyphenate,future-token", run);
  CHECK(run.auto_hyphenate && run.kashida_width == 4 && run.hyphenated_word_size == 6);
  CHECK(patchy::psd::serialize_paragraph_run_settings(run) == "hyphenate,kashida:4");
  patchy::psd::parse_paragraph_run_settings("hyphenation:7:3:4:5:30", run);  // five values: ignored whole
  CHECK(!run.auto_hyphenate && run.kashida_width == 2 && run.hyphenated_word_size == 6 && run.pre_hyphen == 2);
  patchy::psd::parse_paragraph_run_settings("hyphenation:9:1:1:0:12.5:1,kashida:9", run);
  CHECK(run.hyphenated_word_size == 9 && run.consecutive_hyphens == 0 && run.hyphenation_zone == 12.5);
  CHECK(run.hyphenate_capitalized && run.kashida_width == 2);
  CHECK(patchy::psd::serialize_paragraph_run_settings(run) == "hyphenation:9:1:1:0:12.5:1");
}

// GitHub issue 683: Photoshop's Middle Eastern character settings (digit set, diacritic offsets,
// mark distance, Justification Alternates) are kept per run: Txt2 style keys /70, /72, /73, /87
// and /75, with the older TySh /HindiNumbers for the digit set. Patchy draws nothing from them.
void text_engine_middle_eastern_settings_round_trip() {
  patchy::Document document(600, 300, patchy::PixelFormat::rgb8());
  auto tagged = patchy_text_layer(document, "Tagged", "Hindi Farsi Arabic plain", false);
  const std::string columns = "\t30\t0\t0\t#202020\tArial\tauto\t0\t1\t1\t0\t\t0\t0\t0\t";
  tagged.metadata()[patchy::kLayerMetadataTextRuns] =
      "v9\n0\t6" + columns + "digits:2,diacx:-100,marky:0,justalt\n6\t6" + columns + "digits:3,diacy:25.5\n12\t7" +
      columns + "underline,digits:1\n19\t5" + columns + "-";
  document.add_layer(std::move(tagged));
  document.add_layer(patchy_text_layer(document, "Plain", "Untagged text", false));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  write_artifact("text-engine-middle-eastern.psd", bytes);

  const auto check_written = [](const std::vector<std::uint8_t>& written) {
    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    const auto tagged_index = text_index_of(written, 0);
    const auto plain_index = text_index_of(written, 1);
    CHECK(block.has_value() && tagged_index.has_value() && plain_index.has_value());
    if (!block.has_value() || !tagged_index.has_value() || !plain_index.has_value()) {
      return;
    }
    const auto sheets = txt2_run_sheets(*block, *tagged_index, "6");
    CHECK(sheets.size() == 4U);
    if (sheets.size() == 4U) {
      CHECK(sheet_integer(sheets[0], "70") == 2 && sheet_number(sheets[0], "72") == -100.0);
      CHECK(sheet_number(sheets[0], "87") == 0.0 && sheet_flag(sheets[0], "75") == 1);
      CHECK(sheet_flag(sheets[0], "88") == 0);
      CHECK(sheet_integer(sheets[1], "70") == 3 && sheet_number(sheets[1], "73") == 25.5);
      CHECK(sheet_number(sheets[1], "87") == 100.0 && sheet_flag(sheets[1], "75") == 0);
      CHECK(sheet_integer(sheets[2], "70") == 1 && sheet_integer(sheets[2], "16") == 2);
      CHECK(sheet_integer(sheets[3], "70") == 0 && sheet_number(sheets[3], "72") == 0.0);
    }
    const auto plain = txt2_run_sheets(*block, *plain_index, "6");
    CHECK(plain.size() == 1U);
    if (plain.size() == 1U) {
      CHECK(sheet_integer(plain[0], "70") == 0 && sheet_number(plain[0], "87") == 100.0);
      CHECK(sheet_flag(plain[0], "75") == 0 && sheet_flag(plain[0], "88") == 0);
    }
    // The TySh twin: Hindi and Farsi digits true, Arabic false, nothing for the default set.
    const auto tysh = tysh_text(written, 0);
    CHECK(count_of(tysh, "/HindiNumbers true") == 2U);
    CHECK(count_of(tysh, "/HindiNumbers false") == 1U);
    CHECK(count_of(tysh_text(written, 1), "/HindiNumbers") == 0U);
  };
  check_written(bytes);

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2U);
  if (read.layers().size() != 2U) {
    return;
  }
  CHECK((metadata_run_column(read.layers()[0], patchy::kLayerMetadataTextRuns, 16) ==
         std::vector<std::string>{"digits:2,diacx:-100,marky:0,justalt", "digits:3,diacy:25.5", "underline,digits:1",
                                  "-"}));
  CHECK(read.layers()[1].metadata().at(patchy::kLayerMetadataTextRuns).find("digits") == std::string::npos);
  mark_text_layers_edited(read);
  check_written(patchy::psd::DocumentIo::write_layered_rgb8(read));

  patchy::psd::PsdTextStyleRun run;
  CHECK(!patchy::psd::text_run_has_middle_eastern_settings(run));
  patchy::psd::parse_text_run_character_styles("justalt,marky:60,digits:99,diacx:nope", run);
  CHECK(run.justification_alternates && run.mark_y_distance == 60.0 && run.digit_set == 0);
  CHECK(run.diacritic_x_offset == 0.0);
  CHECK(patchy::psd::text_run_has_character_styles(run));
  CHECK(patchy::psd::serialize_text_run_character_styles(run) == "marky:60,justalt");
}

namespace {

// The layers of a saved file that carry text, with what the save wrote for each paragraph's line
// composer: "E" (Every-line) or "S" (Single-line) per paragraph run, from the TySh
// (/EveryLineComposer) and from the Txt2 object the TySh points at (/29, "?" when absent).
struct SavedLineComposers {
  std::string layer;
  std::string tysh;
  std::string txt2;
};

std::vector<SavedLineComposers> saved_line_composers(const patchy::Document& document,
                                                     const std::vector<std::uint8_t>& bytes) {
  std::vector<SavedLineComposers> saved;
  const auto payload = text_engine_payload(bytes);
  const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  for (int index = 0; index < static_cast<int>(document.layers().size()); ++index) {
    const auto& layer = document.layers()[static_cast<std::size_t>(index)];
    if (!layer.metadata().contains(patchy::kLayerMetadataText)) {
      continue;
    }
    SavedLineComposers entry;
    entry.layer = layer.name();
    const auto runs = tysh_paragraph_runs(bytes, index);
    static constexpr std::string_view kKey = "/EveryLineComposer ";
    for (auto at = runs.find(kKey); at != std::string::npos; at = runs.find(kKey, at + kKey.size())) {
      entry.tysh += runs.compare(at + kKey.size(), 4, "true") == 0 ? 'E' : 'S';
    }
    const auto object = text_index_of(bytes, index);
    if (block.has_value() && object.has_value()) {
      for (const auto* sheet : txt2_run_sheets(*block, *object, "5")) {
        const auto flag = sheet_flag(sheet, "29");
        entry.txt2 += flag == 1 ? 'E' : flag == 0 ? 'S' : '?';
      }
    }
    saved.push_back(std::move(entry));
  }
  return saved;
}

// Column 11 of a layer's paragraph runs, joined with "|" ("" for a paragraph without the column).
std::string paragraph_settings_of(const patchy::Layer& layer) {
  std::string joined;
  bool first = true;
  for (const auto& value : metadata_run_column(layer, patchy::kLayerMetadataTextParagraphRuns, 11)) {
    if (!first) {
      joined += '|';
    }
    first = false;
    joined += value;
  }
  return joined;
}

const patchy::Layer* first_text_layer(const patchy::Document& document) {
  for (const auto& layer : document.layers()) {
    if (layer.metadata().contains(patchy::kLayerMetadataText)) {
      return &layer;
    }
  }
  return nullptr;
}

}  // namespace

// Photoshop's line composer (the Paragraph panel menu's Adobe Every-line Composer or Adobe
// Single-line Composer) is kept per paragraph: TySh /EveryLineComposer and Txt2 paragraph key
// /29. Patchy wrote "false" into every paragraph it rewrote, which switched an Every-line
// paragraph to the Single-line composer in Photoshop. A new layer still writes false, as
// Photoshop 2026 writes one, and the default sheets keep Photoshop's false.
void text_engine_line_composer_round_trips() {
  patchy::Document document(600, 300, patchy::PixelFormat::rgb8());
  document.add_layer(patchy_text_layer(document, "New", "A new Patchy layer", false));
  auto kept = patchy_text_layer(document, "Kept", "Every-line paragraph\nSingle-line paragraph", false);
  kept.metadata()[patchy::kLayerMetadataTextParagraphRuns] =
      "v6\n0\t21\tjustify\t0\t0\t0\t0\t0\t1.2\tauto\t1\teveryline\n"
      "21\t21\tjustify\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-";
  document.add_layer(std::move(kept));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  write_artifact("text-engine-line-composer.psd", bytes);

  const auto check_written = [](const patchy::Document& source, const std::vector<std::uint8_t>& written) {
    const auto saved = saved_line_composers(source, written);
    CHECK(saved.size() == 2U);
    if (saved.size() != 2U) {
      return;
    }
    CHECK(saved[0].layer == "New" && saved[0].tysh == "S" && saved[0].txt2 == "S");
    CHECK(saved[1].layer == "Kept" && saved[1].tysh == "ES" && saved[1].txt2 == "ES");
    // The normal paragraph sheet (in the ResourceDict and its DocumentResources twin) keeps
    // Photoshop's "false" on both layers.
    CHECK(count_of(tysh_text(written, 0), "/EveryLineComposer false") == 3U);
    CHECK(count_of(tysh_text(written, 0), "/EveryLineComposer true") == 0U);
    CHECK(count_of(tysh_text(written, 1), "/EveryLineComposer false") == 3U);
    CHECK(count_of(tysh_text(written, 1), "/EveryLineComposer true") == 1U);
    // So do the Txt2 default sheets: the template's ParagraphSheetSet entry ("Normal RGB", the
    // parent every authored run names) and /1/3.
    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    CHECK(block.has_value());
    if (block.has_value()) {
      CHECK(sheet_flag(block->root().at_path({"0", "6", "0", "0", "0", "5"}), "29") == 0);
      CHECK(sheet_flag(block->root().at_path({"1", "3"}), "29") == 0);
    }
  };
  check_written(document, bytes);

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2U);
  if (read.layers().size() != 2U) {
    return;
  }
  // The new layer's paragraph keeps its short form: no settings column.
  CHECK(read.layers()[0].metadata().at(patchy::kLayerMetadataTextParagraphRuns).rfind("v6", 0) != 0);
  CHECK(paragraph_settings_of(read.layers()[1]) == "everyline|-");
  mark_text_layers_edited(read);
  check_written(read, patchy::psd::DocumentIo::write_layered_rgb8(read));

  // The column's token.
  patchy::psd::PsdTextParagraphRun run;
  CHECK(!run.every_line_composer);
  patchy::psd::parse_paragraph_run_settings("everyline,future-token", run);
  CHECK(run.every_line_composer && !run.auto_hyphenate && run.kashida_width == 2);
  CHECK(patchy::psd::serialize_paragraph_run_settings(run) == "everyline");
  patchy::psd::parse_paragraph_run_settings("everyline,kashida:0,hyphenate", run);
  CHECK(patchy::psd::serialize_paragraph_run_settings(run) == "hyphenate,kashida:0,everyline");
  patchy::psd::PsdTextParagraphRun copy;
  patchy::psd::copy_paragraph_run_settings(run, copy);
  CHECK(copy.every_line_composer && copy.auto_hyphenate && copy.kashida_width == 0);
  patchy::psd::parse_paragraph_run_settings("-", run);
  CHECK(!run.every_line_composer);
  patchy::psd::parse_paragraph_run_settings("everylines,EveryLine", run);  // not the token
  CHECK(!run.every_line_composer);
}

// Where Photoshop takes a paragraph's line composer from (Photoshop 2026 render probes,
// docs/txt2-kept-settings.md): the Txt2 paragraph run's own /29, else its parent ParagraphSheetSet entry's,
// else the document's default sheet; the TySh /EveryLineComposer only for a layer with no Txt2
// object, where a run without the key takes the normal paragraph sheet's. Each case edits the
// committed Photoshop fixture in place (one paragraph, Single-line everywhere).
void text_engine_line_composer_reads_photoshop_sheets() {
  const auto original = read_all(patchy::test::committed_psd_fixture_path("photoshop-text-track-box-right-m80.psd"));
  static constexpr std::string_view kMarker = "8BIMTxt2";
  // The fixture with its Txt2 block edited: the block keeps its length (the edits here only
  // shorten it, and the difference goes into the whitespace before the root's end).
  const auto with_txt2 = [&original](const std::function<void(patchy::psd::EngineNode&)>& edit) {
    auto bytes = original;
    const auto payload = text_engine_payload(bytes);
    auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    CHECK(block.has_value());
    if (!block.has_value()) {
      return bytes;
    }
    edit(block->root());
    auto* sheet = block->root().at_path({"1", "1", "0", "0", "5", "0", "0", "0", "0", "5"});
    CHECK(sheet != nullptr);
    const auto shorter = block->serialize();
    CHECK(shorter.size() <= payload->size());
    if (sheet == nullptr || shorter.size() > payload->size()) {
      return bytes;
    }
    sheet->close_ws.append(payload->size() - shorter.size(), ' ');
    const auto edited = block->serialize();
    CHECK(edited.size() == payload->size());
    const auto at = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
    std::copy(edited.begin(), edited.end(), at + static_cast<std::ptrdiff_t>(kMarker.size() + 4));
    return bytes;
  };
  // The fixture with the Nth "/EveryLineComposer false" of its TySh EngineData (0 the paragraph
  // run, 1 the normal paragraph sheet) rewritten in place.
  const auto with_tysh = [](std::vector<std::uint8_t> bytes, int occurrence, std::string_view replacement) {
    static constexpr std::string_view kKey = "/EveryLineComposer false";
    CHECK(replacement.size() == kKey.size());
    auto at = bytes.begin();
    for (int index = 0; index <= occurrence; ++index) {
      at = std::search(index == 0 ? at : at + 1, bytes.end(), kKey.begin(), kKey.end());
      CHECK(at != bytes.end());
      if (at == bytes.end()) {
        return bytes;
      }
    }
    std::copy(replacement.begin(), replacement.end(), at);
    return bytes;
  };
  // The fixture with its Txt2 block renamed to a key no reader knows: the layer has no text
  // object, as in a file from an editor that writes only the TySh.
  const auto without_txt2 = [](std::vector<std::uint8_t> bytes) {
    const auto at = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
    CHECK(at != bytes.end());
    if (at != bytes.end()) {
      *(at + 7) = 'X';
    }
    return bytes;
  };
  const auto run_sheet = [](patchy::psd::EngineNode& root) {
    return root.at_path({"1", "1", "0", "0", "5", "0", "0", "0", "0", "5"});
  };
  const auto parent_sheet = [](patchy::psd::EngineNode& root) { return root.at_path({"0", "6", "0", "1", "0", "5"}); };
  const auto set_true = [](patchy::psd::EngineNode* sheet) {
    auto* node = sheet != nullptr ? sheet->find("29") : nullptr;
    CHECK(node != nullptr && node->raw == "false");
    if (node != nullptr) {
      node->raw = "true";
    }
  };
  const auto erase = [](patchy::psd::EngineNode* sheet) { CHECK(sheet != nullptr && sheet->erase("29")); };
  // What a read finds, and what an edit and a save then write for the paragraph.
  const auto read_and_saved = [](const std::vector<std::uint8_t>& bytes) {
    auto document = patchy::psd::DocumentIo::read(bytes);
    const auto* layer = first_text_layer(document);
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      return std::string("no text layer");
    }
    auto found = paragraph_settings_of(*layer);
    mark_text_layers_edited(document);
    for (const auto& saved : saved_line_composers(document, patchy::psd::DocumentIo::write_layered_rgb8(document))) {
      found += " tysh=" + saved.tysh + " txt2=" + saved.txt2;
    }
    return found;
  };
  static constexpr const char* kSingle = " tysh=S txt2=S";
  static constexpr const char* kEvery = "everyline tysh=E txt2=E";

  // As Photoshop wrote it: Single-line, no settings column.
  CHECK(read_and_saved(original) == kSingle);
  // The run's own /29.
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) { set_true(run_sheet(root)); })) == kEvery);
  // A run that leaves /29 out takes its parent sheet's ("Basic Paragraph").
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) {
          erase(run_sheet(root));
          set_true(parent_sheet(root));
        })) == kEvery);
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) { erase(run_sheet(root)); })) == kSingle);
  // Neither names it: the document's default paragraph sheet.
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) {
          erase(run_sheet(root));
          erase(parent_sheet(root));
          set_true(root.at_path({"1", "3"}));
        })) == kEvery);
  // The other ParagraphSheetSet entry ("Normal RGB") is not the run's parent: not read.
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) {
          erase(run_sheet(root));
          set_true(root.at_path({"0", "6", "0", "0", "0", "5"}));
        })) == kSingle);
  // The Txt2 object wins over the TySh run, both ways.
  CHECK(read_and_saved(with_tysh(original, 0, "/EveryLineComposer true ")) == kSingle);
  CHECK(read_and_saved(with_tysh(with_txt2([&](patchy::psd::EngineNode& root) { set_true(run_sheet(root)); }), 0,
                                 "/EveryLineComposer false")) == kEvery);
  // No Txt2 object: the TySh run, and for a run without the key the normal paragraph sheet.
  CHECK(read_and_saved(without_txt2(original)) == kSingle);
  CHECK(read_and_saved(without_txt2(with_tysh(original, 0, "/EveryLineComposer true "))) == kEvery);
  CHECK(read_and_saved(without_txt2(with_tysh(original, 1, "/EveryLineComposer true "))) == kSingle);
  CHECK(read_and_saved(without_txt2(with_tysh(with_tysh(original, 1, "/EveryLineComposer true "), 0,
                                              "/EveryLineCompose_ false"))) == kEvery);
}

// Photoshop 2026 files (local fixtures, README.txt beside them): the #672 justify-modes file with
// Adobe Every-line Composer set on its four layers, a layer whose first paragraph is Every-line
// and whose second is Single-line, and the #672 file with only its parent paragraph sheet saying
// Every-line. Opened, marked edited and saved by Patchy, every paragraph keeps its composer in
// both blocks, and the #672 file itself (Single-line) stays Single-line.
void text_engine_photoshop_line_composer_survives_an_edit_if_available() {
  struct Fixture {
    const char* file;
    const char* artifact;
    std::size_t layers;
    const char* settings;  // column 11 of every layer
    const char* written;   // each layer's paragraphs in both blocks
  };
  const Fixture fixtures[] = {
      {"every-line-composer/justify-modes-every-line-photoshop.psd", "line-composer-every-line-edited.psd", 4U,
       "everyline", "E"},
      {"every-line-composer/two-paragraphs-every-then-single-photoshop.psd", "line-composer-mixed-edited.psd", 1U,
       "everyline|-", "ES"},
      {"every-line-composer/justify-modes-parent-sheet-every-line.psd", "line-composer-parent-sheet-edited.psd", 4U,
       "everyline", "E"},
      {"issue-672-justify-modes.psd", "line-composer-single-line-edited.psd", 4U, "", "S"},
  };
  for (const auto& fixture : fixtures) {
    patchy::test::skip_unless_exists(patchy::test::local_psd_fixture_path(fixture.file));
  }
  for (const auto& fixture : fixtures) {
    auto document = patchy::psd::DocumentIo::read(read_all(patchy::test::local_psd_fixture_path(fixture.file)));
    std::size_t text_layers = 0;
    for (const auto& layer : document.layers()) {
      if (!layer.metadata().contains(patchy::kLayerMetadataText)) {
        continue;
      }
      ++text_layers;
      const auto settings = paragraph_settings_of(layer);
      if (settings != fixture.settings) {
        std::printf("  %s [%s]: read \"%s\"\n", fixture.file, layer.name().c_str(), settings.c_str());
      }
      CHECK(settings == fixture.settings);
    }
    CHECK(text_layers == fixture.layers);
    mark_text_layers_edited(document);
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    write_artifact(fixture.artifact, bytes);
    const auto saved = saved_line_composers(document, bytes);
    CHECK(saved.size() == fixture.layers);
    for (const auto& entry : saved) {
      if (entry.tysh != fixture.written || entry.txt2 != fixture.written) {
        std::printf("  %s [%s]: wrote tysh %s, txt2 %s\n", fixture.file, entry.layer.c_str(), entry.tysh.c_str(),
                    entry.txt2.c_str());
      }
      CHECK(entry.tysh == fixture.written && entry.txt2 == fixture.written);
    }
    // A reopen of the saved file reads the same composers back.
    const auto reread = patchy::psd::DocumentIo::read(bytes);
    std::size_t reread_layers = 0;
    for (const auto& layer : reread.layers()) {
      if (layer.metadata().contains(patchy::kLayerMetadataText)) {
        ++reread_layers;
        CHECK(paragraph_settings_of(layer) == fixture.settings);
      }
    }
    CHECK(reread_layers == fixture.layers);
  }
}

// The reporter's Photoshop files (GitHub issue 683, local fixtures): Hebrew boxes with Hyphenate
// off, and an Arabic file with one layer per Middle Eastern setting. Opened, marked edited and
// saved by Patchy, every layer keeps Hyphenate, the Kashida width, the digit set, Justification
// Alternates and the diacritic position in both blocks.
void text_engine_issue_683_reporter_settings_survive_an_edit_if_available() {
  const auto hebrew_path = patchy::test::local_psd_fixture_path("issue-683/hebrew-rtl-justified-photoshop.psd");
  const auto arabic_path = patchy::test::local_psd_fixture_path("issue-683/arabic-rtl-settings-photoshop.psd");
  patchy::test::skip_unless_exists(hebrew_path);
  patchy::test::skip_unless_exists(arabic_path);

  struct Saved {
    std::vector<std::uint8_t> bytes;
    patchy::Document document;
  };
  const auto edit_and_save = [](const std::filesystem::path& path, const char* artifact) {
    auto document = patchy::psd::DocumentIo::read(read_all(path));
    mark_text_layers_edited(document);
    auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    write_artifact(artifact, bytes);
    return Saved{std::move(bytes), std::move(document)};
  };
  const auto layer_index_named = [](const patchy::Document& document, std::string_view prefix) {
    for (int index = 0; index < static_cast<int>(document.layers().size()); ++index) {
      if (document.layers()[static_cast<std::size_t>(index)].name().rfind(prefix, 0) == 0) {
        return index;
      }
    }
    return -1;
  };

  // Hebrew: three justified right-to-left boxes, Hyphenate off in the file and after the save.
  const auto hebrew = edit_and_save(hebrew_path, "issue-683-hebrew-justified-edited.psd");
  const auto hebrew_payload = text_engine_payload(hebrew.bytes);
  const auto hebrew_block = hebrew_payload.has_value() ? patchy::psd::TextEngineBlock::parse(*hebrew_payload) : std::nullopt;
  CHECK(hebrew_block.has_value());
  int hebrew_layers = 0;
  for (int index = 0; index < static_cast<int>(hebrew.document.layers().size()); ++index) {
    const auto& layer = hebrew.document.layers()[static_cast<std::size_t>(index)];
    if (!layer.metadata().contains(patchy::kLayerMetadataText)) {
      continue;
    }
    ++hebrew_layers;
    const auto& runs = layer.metadata().at(patchy::kLayerMetadataTextParagraphRuns);
    CHECK(runs.find("hyphenate") == std::string::npos);
    CHECK((metadata_run_column(layer, patchy::kLayerMetadataTextParagraphRuns, 9) == std::vector<std::string>{"rtl"}));
    const auto paragraph_runs = tysh_paragraph_runs(hebrew.bytes, index);
    CHECK(count_of(paragraph_runs, "/AutoHyphenate false") == 1U);
    CHECK(count_of(paragraph_runs, "/AutoHyphenate true") == 0U);
    const auto object = text_index_of(hebrew.bytes, index);
    CHECK(object.has_value());
    if (hebrew_block.has_value() && object.has_value()) {
      const auto sheets = txt2_run_sheets(*hebrew_block, *object, "5");
      CHECK(sheets.size() == 1U);
      for (const auto* sheet : sheets) {
        CHECK(sheet_flag(sheet, "9") == 0);
        CHECK(sheet_integer(sheet, "33") == 1);
      }
    }
  }
  CHECK(hebrew_layers == 3);

  // Arabic: layer names say what each one sets.
  const auto arabic = edit_and_save(arabic_path, "issue-683-arabic-settings-edited.psd");
  const auto arabic_payload = text_engine_payload(arabic.bytes);
  const auto arabic_block = arabic_payload.has_value() ? patchy::psd::TextEngineBlock::parse(*arabic_payload) : std::nullopt;
  CHECK(arabic_block.has_value());
  if (!arabic_block.has_value()) {
    return;
  }
  struct Expected {
    std::string_view layer;
    int kashida;
    int digits;
    int alternates;
    double diacritic_x;
    double mark_y;
    bool hindi_numbers;
  };
  const Expected expected[] = {
      {"Right align, Kashidas medium", 2, 1, 0, 0.0, 100.0, false},
      {"Justify last right, Arabic digits", 2, 1, 0, 0.0, 100.0, false},
      {"Justify last center, Hindi digits", 2, 2, 0, 0.0, 100.0, true},
      {"Justify last left, Farsi digits", 2, 3, 0, 0.0, 100.0, true},
      {"Justify all, Kashidas none", 0, 1, 1, 0.0, 100.0, false},
      {"Standard Diacretics", 2, 2, 0, 0.0, 100.0, true},
      {"Horizontal Diacretics", 2, 2, 0, -100.0, 0.0, true},
      {"Vertical Diacretics", 2, 2, 0, 0.0, 100.0, true},
  };
  for (const auto& entry : expected) {
    const auto index = layer_index_named(arabic.document, entry.layer);
    CHECK(index >= 0);
    if (index < 0) {
      std::printf("  no layer named %.*s\n", static_cast<int>(entry.layer.size()), entry.layer.data());
      continue;
    }
    const auto object = text_index_of(arabic.bytes, index);
    CHECK(object.has_value());
    if (!object.has_value()) {
      continue;
    }
    const auto paragraphs = txt2_run_sheets(*arabic_block, *object, "5");
    const auto styles = txt2_run_sheets(*arabic_block, *object, "6");
    CHECK(!paragraphs.empty() && !styles.empty());
    bool matches = !paragraphs.empty() && !styles.empty();
    for (const auto* sheet : paragraphs) {
      matches = matches && sheet_integer(sheet, "40") == entry.kashida && sheet_flag(sheet, "9") == 0;
    }
    for (const auto* sheet : styles) {
      matches = matches && sheet_integer(sheet, "70") == entry.digits && sheet_flag(sheet, "75") == entry.alternates &&
                sheet_number(sheet, "72") == entry.diacritic_x && sheet_number(sheet, "87") == entry.mark_y;
    }
    const auto tysh = tysh_text(arabic.bytes, index);
    matches = matches && count_of(tysh_paragraph_runs(arabic.bytes, index), "/AutoHyphenate true") == 0U &&
              count_of(tysh, entry.hindi_numbers ? "/HindiNumbers true" : "/HindiNumbers false") >= 1U &&
              count_of(tysh, entry.hindi_numbers ? "/HindiNumbers false" : "/HindiNumbers true") == 0U;
    if (!matches) {
      std::printf("  %.*s: settings changed by the save\n", static_cast<int>(entry.layer.size()), entry.layer.data());
    }
    CHECK(matches);
  }
  // A reopen of the saved file reads the same settings back.
  const auto reread = patchy::psd::DocumentIo::read(arabic.bytes);
  for (const auto& layer : arabic.document.layers()) {
    if (!layer.metadata().contains(patchy::kLayerMetadataText)) {
      continue;
    }
    const auto* twin = [&]() -> const patchy::Layer* {
      for (const auto& candidate : reread.layers()) {
        if (candidate.name() == layer.name()) {
          return &candidate;
        }
      }
      return nullptr;
    }();
    CHECK(twin != nullptr);
    if (twin != nullptr) {
      CHECK(metadata_run_column(*twin, patchy::kLayerMetadataTextRuns, 16) ==
            metadata_run_column(layer, patchy::kLayerMetadataTextRuns, 16));
      CHECK(metadata_run_column(*twin, patchy::kLayerMetadataTextParagraphRuns, 11) ==
            metadata_run_column(layer, patchy::kLayerMetadataTextParagraphRuns, 11));
    }
  }
}

namespace {

int layer_index_named(const patchy::Document& document, std::string_view name) {
  for (int index = 0; index < static_cast<int>(document.layers().size()); ++index) {
    if (document.layers()[static_cast<std::size_t>(index)].name() == name) {
      return index;
    }
  }
  return -1;
}

// Column 16 of a layer's runs (the kept character settings), joined with "|".
std::string run_styles_of(const patchy::Layer& layer) {
  std::string joined;
  bool first = true;
  for (const auto& value : metadata_run_column(layer, patchy::kLayerMetadataTextRuns, 16)) {
    if (!first) {
      joined += '|';
    }
    first = false;
    joined += value;
  }
  return joined;
}

}  // namespace

// Kerning is the file's own (Testy's text-save leg, November 2026): Patchy wrote "/AutoKerning
// true /Kerning 0" and Txt2 /11 1 (Metrics) into every run it rewrote, which kerned a Photoshop
// run that was manual 0. A run keeps manual (with its number) and Optical in both blocks, through
// a reopen and another save; a new layer is Metrics, as Photoshop 2026 writes one.
void text_engine_kerning_round_trips() {
  patchy::Document document(600, 300, patchy::PixelFormat::rgb8());
  document.add_layer(patchy_text_layer(document, "New", "A new Patchy layer", false));
  auto kept = patchy_text_layer(document, "Kept", "Manual Number Optical plain", false);
  const std::string columns = "\t30\t0\t0\t#202020\tArial\tauto\t0\t1\t1\t0\t\t0\t0\t0\t";
  kept.metadata()[patchy::kLayerMetadataTextRuns] = "v9\n0\t7" + columns + "kern:0\n7\t7" + columns +
                                                    "underline,kern:-25\n14\t8" + columns + "kern:optical\n22\t5" +
                                                    columns + "-";
  document.add_layer(std::move(kept));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  write_artifact("text-engine-kerning.psd", bytes);

  const auto check_written = [](const std::vector<std::uint8_t>& written) {
    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    const auto new_index = text_index_of(written, 0);
    const auto kept_index = text_index_of(written, 1);
    CHECK(block.has_value() && new_index.has_value() && kept_index.has_value());
    if (!block.has_value() || !new_index.has_value() || !kept_index.has_value()) {
      return;
    }
    const auto plain = txt2_run_sheets(*block, *new_index, "6");
    CHECK(plain.size() == 1U);
    if (plain.size() == 1U) {
      CHECK(sheet_integer(plain[0], "11") == 1);
      CHECK(plain[0]->find("10") == nullptr);
    }
    const auto sheets = txt2_run_sheets(*block, *kept_index, "6");
    CHECK(sheets.size() == 4U);
    if (sheets.size() == 4U) {
      // Manual 0: the mode alone, as Photoshop 2026 writes it.
      CHECK(sheet_integer(sheets[0], "11") == 0 && sheets[0]->find("10") == nullptr);
      CHECK(sheet_integer(sheets[1], "11") == 0 && sheet_number(sheets[1], "10") == -25.0);
      CHECK(sheet_integer(sheets[1], "16") == 2);  // the underline beside it
      CHECK(sheet_integer(sheets[2], "11") == 2 && sheets[2]->find("10") == nullptr);
      CHECK(sheet_integer(sheets[3], "11") == 1 && sheets[3]->find("10") == nullptr);
    }
    // The TySh twin can only say manual: false with the number. Metrics and Optical read true,
    // and so does the normal style sheet (twice: the ResourceDict and its DocumentResources twin).
    CHECK(count_of(tysh_text(written, 0), "/AutoKerning true /Kerning 0 ") == 3U);
    CHECK(count_of(tysh_text(written, 0), "/AutoKerning false") == 0U);
    const auto tysh = tysh_text(written, 1);
    CHECK(count_of(tysh, "/AutoKerning false /Kerning 0 ") == 1U);
    CHECK(count_of(tysh, "/AutoKerning false /Kerning -25 ") == 1U);
    CHECK(count_of(tysh, "/AutoKerning true /Kerning 0 ") == 4U);
  };
  check_written(bytes);

  auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.layers().size() == 2U);
  if (read.layers().size() != 2U) {
    return;
  }
  CHECK(read.layers()[0].metadata().at(patchy::kLayerMetadataTextRuns).find("kern") == std::string::npos);
  CHECK(run_styles_of(read.layers()[1]) == "kern:0|underline,kern:-25|kern:optical|-");
  mark_text_layers_edited(read);
  check_written(patchy::psd::DocumentIo::write_layered_rgb8(read));

  // The column's tokens.
  patchy::psd::PsdTextStyleRun run;
  CHECK(!patchy::psd::text_run_has_kept_kerning(run) && !patchy::psd::text_run_has_character_styles(run));
  patchy::psd::parse_text_run_character_styles("kern:optical,future-token", run);
  CHECK(run.kerning_mode == patchy::psd::kTextKerningOptical && run.kerning == 0.0);
  CHECK(patchy::psd::text_run_has_character_styles(run));
  CHECK(patchy::psd::serialize_text_run_character_styles(run) == "kern:optical");
  patchy::psd::parse_text_run_character_styles("kern:50,justalt", run);
  CHECK(run.kerning_mode == patchy::psd::kTextKerningManual && run.kerning == 50.0);
  CHECK(patchy::psd::serialize_text_run_character_styles(run) == "justalt,kern:50");
  patchy::psd::parse_text_run_character_styles("kern:nope,kerning:3", run);  // not the token
  CHECK(run.kerning_mode == patchy::psd::kTextKerningMetrics && run.kerning == 0.0);
  CHECK(patchy::psd::serialize_text_run_character_styles(run) == "-");
}

// Where Photoshop takes a run's kerning from (Photoshop 2026 captures and the psd-tools files,
// docs/txt2-kept-settings.md "Kerning"): the Txt2 style run's own /11 (0 manual, 1 Metrics, 2 Optical), else the
// character style (/32) of its paragraph's parent sheet ("Basic Paragraph" says manual 0 in files
// Photoshop CC wrote, which is why a run that states nothing is unkerned there), else the
// document's default style sheet; the TySh /AutoKerning only for a layer with no Txt2 object.
// The committed Photoshop 2026 fixture (scripts/dev/txt2/ps-capture-point-anchors.ps1) has a
// manual and an Optical layer; the other cases edit its "right" layer in place.
void text_engine_kerning_reads_photoshop_sheets() {
  const auto original = read_all(patchy::test::committed_psd_fixture_path("photoshop-text-point-anchors.psd"));
  const auto as_photoshop_wrote = patchy::psd::DocumentIo::read(original);
  const auto right_layer = layer_index_named(as_photoshop_wrote, "right");
  const auto manual_layer = layer_index_named(as_photoshop_wrote, "kern-manual");
  const auto optical_layer = layer_index_named(as_photoshop_wrote, "kern-optical");
  CHECK(right_layer >= 0 && manual_layer >= 0 && optical_layer >= 0);
  if (right_layer < 0 || manual_layer < 0 || optical_layer < 0) {
    return;
  }
  CHECK(run_styles_of(as_photoshop_wrote.layers()[static_cast<std::size_t>(manual_layer)]) == "kern:0");
  CHECK(run_styles_of(as_photoshop_wrote.layers()[static_cast<std::size_t>(optical_layer)]) == "kern:optical");
  const auto right_object = text_index_of(original, right_layer);
  CHECK(right_object.has_value());
  if (!right_object.has_value()) {
    return;
  }
  const auto object_key = std::to_string(*right_object);

  static constexpr std::string_view kMarker = "8BIMTxt2";
  // The fixture with its Txt2 block edited in place: the edits only shorten it, and the
  // difference goes into the whitespace after the root.
  const auto with_txt2 = [&original](const std::function<void(patchy::psd::EngineNode&)>& edit) {
    auto bytes = original;
    const auto payload = text_engine_payload(bytes);
    auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    CHECK(block.has_value());
    if (!block.has_value()) {
      return bytes;
    }
    edit(block->root());
    const auto shorter = block->serialize();
    CHECK(shorter.size() <= payload->size());
    if (shorter.size() > payload->size()) {
      return bytes;
    }
    block->root().close_ws.append(payload->size() - shorter.size(), ' ');
    const auto edited = block->serialize();
    CHECK(edited.size() == payload->size());
    if (edited.size() != payload->size()) {
      return bytes;
    }
    const auto at = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
    std::copy(edited.begin(), edited.end(), at + static_cast<std::ptrdiff_t>(kMarker.size() + 4));
    return bytes;
  };
  const auto without_txt2 = [](std::vector<std::uint8_t> bytes) {
    const auto at = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
    CHECK(at != bytes.end());
    if (at != bytes.end()) {
      *(at + 7) = 'X';
    }
    return bytes;
  };
  const auto run_sheet = [&object_key](patchy::psd::EngineNode& root) {
    return root.at_path({"1", "1", object_key, "0", "6", "0", "0", "0", "0", "6"});
  };
  const auto paragraph_style = [](patchy::psd::EngineNode& root) {
    return root.at_path({"0", "6", "0", "1", "0", "5", "32"});  // "Basic Paragraph", every run's parent
  };
  const auto set_mode = [](patchy::psd::EngineNode* sheet, const char* mode) {
    auto* node = sheet != nullptr ? sheet->find("11") : nullptr;
    CHECK(node != nullptr);
    if (node != nullptr) {
      node->raw = mode;
    }
  };
  const auto erase = [](patchy::psd::EngineNode* sheet) { CHECK(sheet != nullptr && sheet->erase("11")); };
  // What a read finds for the "right" layer, and what an edit and a save then write for it.
  const auto read_and_saved = [&](const std::vector<std::uint8_t>& bytes) {
    auto document = patchy::psd::DocumentIo::read(bytes);
    const auto index = layer_index_named(document, "right");
    CHECK(index >= 0);
    if (index < 0) {
      return std::string("no layer");
    }
    auto found = run_styles_of(document.layers()[static_cast<std::size_t>(index)]);
    mark_text_layers_edited(document);
    const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    const auto object = text_index_of(written, index);
    if (block.has_value() && object.has_value()) {
      for (const auto* sheet : txt2_run_sheets(*block, *object, "6")) {
        found += " txt2=" + std::to_string(sheet_integer(sheet, "11"));
      }
    }
    found += count_of(tysh_text(written, index), "/AutoKerning false") > 0U ? " tysh=manual" : " tysh=auto";
    return found;
  };
  static constexpr const char* kMetrics = " txt2=1 tysh=auto";
  static constexpr const char* kManual = "kern:0 txt2=0 tysh=manual";
  static constexpr const char* kOptical = "kern:optical txt2=2 tysh=auto";

  // As Photoshop wrote it: Metrics, no token.
  CHECK(read_and_saved(original) == kMetrics);
  // The run's own /11.
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) { set_mode(run_sheet(root), "0"); })) == kManual);
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) { set_mode(run_sheet(root), "2"); })) == kOptical);
  // A run that leaves it out takes the character style of its paragraph's parent sheet.
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) { erase(run_sheet(root)); })) == kMetrics);
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) {
          erase(run_sheet(root));
          set_mode(paragraph_style(root), "0");
        })) == kManual);
  // Neither names it (the run's parent style sheet, "None", is empty): the document's default.
  CHECK(read_and_saved(with_txt2([&](patchy::psd::EngineNode& root) {
          erase(run_sheet(root));
          erase(paragraph_style(root));
          set_mode(root.at_path({"1", "2"}), "2");
        })) == kOptical);
  // The Txt2 object wins over a TySh run that says manual. "/AutoKerning true" and the
  // whitespace after it make room for "false"; the first one in the file is this layer's run.
  const auto with_manual_tysh = [](std::vector<std::uint8_t> bytes) {
    static constexpr std::string_view kKey = "/AutoKerning true";
    static constexpr std::string_view kManualKey = "/AutoKerning false";
    const auto at = std::search(bytes.begin(), bytes.end(), kKey.begin(), kKey.end());
    CHECK(at != bytes.end());
    if (at != bytes.end()) {
      std::copy(kManualKey.begin(), kManualKey.end(), at);
    }
    return bytes;
  };
  CHECK(read_and_saved(with_manual_tysh(original)) == kMetrics);
  // No Txt2 object: the TySh run.
  CHECK(read_and_saved(without_txt2(original)) == kMetrics);
  const auto from_tysh = read_and_saved(without_txt2(with_manual_tysh(original)));
  if (from_tysh != kManual) {
    std::printf("  TySh-only manual run read and saved as \"%s\"\n", from_tysh.c_str());
  }
  CHECK(from_tysh == kManual);
}

// The anti-aliasing method Photoshop shows and draws with is the TySh descriptor's 'AntA'. Files
// exist whose EngineData /AntiAlias number disagrees with it (Smooth beside 4, Crisp beside 2 in
// Testy's private corpus); Patchy read the number and saved such a layer as Sharp or Strong.
void text_engine_anti_alias_reads_the_descriptor_method() {
  auto bytes = read_all(patchy::test::committed_psd_fixture_path("photoshop-text-point-anchors.psd"));
  const auto anti_alias_of = [](const patchy::Document& document, std::string_view name) {
    const auto index = layer_index_named(document, name);
    if (index < 0) {
      return std::string("no layer");
    }
    const auto& metadata = document.layers()[static_cast<std::size_t>(index)].metadata();
    const auto found = metadata.find(patchy::kLayerMetadataTextAntiAlias);
    return found == metadata.end() ? std::string("unset") : found->second;
  };
  {
    const auto document = patchy::psd::DocumentIo::read(bytes);
    CHECK(anti_alias_of(document, "smooth") == std::to_string(patchy::kTextAntiAliasSmooth));
    CHECK(anti_alias_of(document, "right") == std::to_string(patchy::kTextAntiAliasCrisp));
  }
  // The Smooth layer's EngineData number (the only "/AntiAlias 3" of the file) becomes Sharp's.
  static constexpr std::string_view kKey = "/AntiAlias 3";
  const auto at = std::search(bytes.begin(), bytes.end(), kKey.begin(), kKey.end());
  CHECK(at != bytes.end());
  if (at == bytes.end()) {
    return;
  }
  CHECK(std::search(at + 1, bytes.end(), kKey.begin(), kKey.end()) == bytes.end());
  *(at + static_cast<std::ptrdiff_t>(kKey.size() - 1U)) = '4';
  auto document = patchy::psd::DocumentIo::read(bytes);
  CHECK(anti_alias_of(document, "smooth") == std::to_string(patchy::kTextAntiAliasSmooth));
  const auto smooth_index = layer_index_named(document, "smooth");
  mark_text_layers_edited(document);
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  write_artifact("text-engine-anti-alias-descriptor.psd", written);
  // Both of the save's records say Smooth: the descriptor enum and the EngineData number.
  const auto tysh = tysh_text(written, smooth_index);
  CHECK(count_of(tysh, "AnSm") == 1U && count_of(tysh, "antiAliasSharp") == 0U);
  CHECK(count_of(tysh, "/AntiAlias 3 ") == 1U);
  CHECK(anti_alias_of(patchy::psd::DocumentIo::read(written), "smooth") ==
        std::to_string(patchy::kTextAntiAliasSmooth));
}

// A text engine block Patchy cannot author into (the named-key engine of Photoshop CS-era files)
// stays verbatim while any layer still writes the type record it was read with, and a layer
// regenerated beside it takes an index no object has (Photoshop's old-text path). When every type
// record is regenerated (Type > Update All Text Layers) the block is replaced by an authored one
// and every layer addresses its own object, so Photoshop reads current text: the old-text path
// drew the psd-tools pen-text layer wider and lighter (Testy's text-save leg, November 2026).
void text_engine_old_engine_block_is_replaced_when_every_layer_is_regenerated() {
  // Eight point frames and eight objects, one per type layer of the fixture.
  const std::string kOldEngine = [] {
    std::string frames;
    std::string objects;
    for (int index = 0; index < 8; ++index) {
      frames += "<< /Resource << /Data << /TextOnPathTRange [ -1.0 -1.0 ] >> >> >> ";
      objects += "<< /View << /Frames [ << /Resource " + std::to_string(index) + " >> ] >> >> ";
    }
    return " /DocumentResources << /TextFrameSet << /Resources [ " + frames + "] >> >> /DocumentObjects << /TextObjects [ " +
           objects + "] >>";
  }();
  // The committed Photoshop fixture with its block's content replaced, in the file's bytes, by an
  // old-engine one padded with spaces to the same length.
  std::vector<std::uint8_t> old_engine_bytes;
  const auto file_bytes = [&old_engine_bytes, &kOldEngine] {
    auto bytes = read_all(patchy::test::committed_psd_fixture_path("photoshop-text-point-anchors.psd"));
    static constexpr std::string_view kMarker = "8BIMTxt2";
    const auto payload = text_engine_payload(bytes);
    const auto at = std::search(bytes.begin(), bytes.end(), kMarker.begin(), kMarker.end());
    CHECK(payload.has_value() && at != bytes.end() && payload->size() > kOldEngine.size());
    if (!payload.has_value() || at == bytes.end() || payload->size() <= kOldEngine.size()) {
      return bytes;
    }
    old_engine_bytes.assign(payload->size(), static_cast<std::uint8_t>(' '));
    std::copy(kOldEngine.begin(), kOldEngine.end(), old_engine_bytes.begin());
    std::copy(old_engine_bytes.begin(), old_engine_bytes.end(), at + static_cast<std::ptrdiff_t>(kMarker.size() + 4));
    return bytes;
  }();
  CHECK(patchy::psd::text_engine_block_is_named_key_engine(old_engine_bytes));
  const auto with_old_engine = [&file_bytes] { return patchy::psd::DocumentIo::read(file_bytes); };
  // The old engine has the Latin composer only, and Photoshop lays such a document's layers out
  // with it: every paragraph reads composer 0 (paragraph runs column 10), where the same layers
  // of the untouched fixture keep World-Ready (no column).
  {
    const auto document = with_old_engine();
    int text_layers = 0;
    for (const auto& layer : document.layers()) {
      if (!layer.metadata().contains(patchy::kLayerMetadataText)) {
        continue;
      }
      ++text_layers;
      for (const auto& composer : metadata_run_column(layer, patchy::kLayerMetadataTextParagraphRuns, 10)) {
        CHECK(composer == "0");
      }
    }
    CHECK(text_layers == 8);
    const auto untouched = patchy::psd::DocumentIo::read(
        read_all(patchy::test::committed_psd_fixture_path("photoshop-text-point-anchors.psd")));
    for (const auto& layer : untouched.layers()) {
      for (const auto& composer : metadata_run_column(layer, patchy::kLayerMetadataTextParagraphRuns, 10)) {
        CHECK(composer.empty() || composer == "1");
      }
    }
  }
  const auto text_layer_indices = [](const patchy::Document& document) {
    std::vector<int> indices;
    for (int index = 0; index < static_cast<int>(document.layers().size()); ++index) {
      if (document.layers()[static_cast<std::size_t>(index)].metadata().contains(patchy::kLayerMetadataText)) {
        indices.push_back(index);
      }
    }
    return indices;
  };

  // Nothing edited: the block and every type record are the file's.
  {
    const auto document = with_old_engine();
    const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
    CHECK(text_engine_payload(written) == old_engine_bytes);
  }
  // One layer edited: the block stays, and that layer alone leaves its objects.
  {
    auto document = with_old_engine();
    const auto indices = text_layer_indices(document);
    CHECK(indices.size() == 8U);
    if (indices.empty()) {
      return;
    }
    document.layers()[static_cast<std::size_t>(indices.front())].metadata()[patchy::kLayerMetadataTextRasterStatus] =
        "patchy_raster";
    const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
    CHECK(text_engine_payload(written) == old_engine_bytes);
    for (const auto index : indices) {
      const auto text_index = text_index_of(written, index);
      CHECK(text_index.has_value());
      if (text_index.has_value()) {
        CHECK((*text_index >= patchy::psd::kRegeneratedTextIndexBase) == (index == indices.front()));
      }
    }
  }
  // Every layer edited: an authored block, one object per layer, each at its own index.
  {
    auto document = with_old_engine();
    const auto indices = text_layer_indices(document);
    mark_text_layers_edited(document);
    const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
    write_artifact("text-engine-old-engine-replaced.psd", written);
    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    CHECK(block.has_value());
    if (!block.has_value()) {
      return;
    }
    CHECK(block->object_count() == indices.size());
    std::vector<std::int32_t> seen;
    for (const auto index : indices) {
      const auto text_index = text_index_of(written, index);
      CHECK(text_index.has_value());
      if (!text_index.has_value()) {
        continue;
      }
      CHECK(*text_index >= 0 && static_cast<std::size_t>(*text_index) < block->object_count());
      CHECK(std::find(seen.begin(), seen.end(), *text_index) == seen.end());
      seen.push_back(*text_index);
      const auto text = block->object_text(static_cast<std::size_t>(*text_index));
      const auto& layer_text =
          document.layers()[static_cast<std::size_t>(index)].metadata().at(patchy::kLayerMetadataText);
      CHECK(text.has_value());
      if (text.has_value()) {
        // The object's engine text is the layer's with '\r' breaks and a closing '\r'.
        auto expected = layer_text;
        std::replace(expected.begin(), expected.end(), '\n', '\r');
        CHECK(*text == expected + '\r');
      }
      // The Latin composer the old engine laid the layer out with.
      for (const auto* sheet : txt2_run_sheets(*block, *text_index, "5")) {
        CHECK(sheet_integer(sheet, "35") == 0);
      }
    }
    // The reopened file reads every layer back as text with its own object.
    const auto reread = patchy::psd::DocumentIo::read(written);
    CHECK(text_layer_indices(reread).size() == indices.size());
  }
}

// A box-text layer from a Photoshop file keeps the file's fractional box through a re-render
// that left the box alone (kLayerMetadataTextFileBox): the TySh /BoxBounds and the Txt2 frame
// carry it instead of Patchy's whole-pixel box. With whole pixels a gradient stroke aligned with
// the layer shifted a pixel in Photoshop on the psd-tools effect-stroke-gradient file. A box the
// user resized (more than a pixel from the file's) is written as it is.
void text_engine_box_keeps_the_files_fractional_size() {
  const auto written_with = [](const char* file_box) {
    patchy::Document document(600, 300, patchy::PixelFormat::rgb8());
    auto layer = patchy_text_layer(document, "Box", "Box text", true);
    layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "122";
    layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "121";
    layer.metadata()[patchy::kLayerMetadataTextParagraphRuns] = "v1\n0\t8\tcenter";
    layer.metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
    layer.metadata()[patchy::kLayerMetadataTextTransform] = "1 0 0 1 20 30";  // as every commit records
    if (file_box != nullptr) {
      layer.metadata()[patchy::kLayerMetadataTextFileBox] = file_box;
    }
    document.add_layer(std::move(layer));
    return patchy::psd::DocumentIo::write_layered_rgb8(document);
  };
  // The frame's contour ends its first segment at (width, 0) and its second at (width, height).
  const auto frame_size = [](const std::vector<std::uint8_t>& written) {
    const auto payload = text_engine_payload(written);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    const auto index = text_index_of(written, 0);
    std::pair<double, double> size{-1.0, -1.0};
    if (!block.has_value() || !index.has_value()) {
      return size;
    }
    const auto frame = block->object_frame_index(static_cast<std::size_t>(*index));
    const auto* contour = frame.has_value()
                              ? block->root().at_path({"0", "8", "0", std::to_string(*frame), "0", "1", "0"})
                              : nullptr;
    if (contour != nullptr && contour->is_list() && contour->list.size() == 32U) {
      size.first = contour->list[6].number().value_or(-1.0);
      size.second = contour->list[15].number().value_or(-1.0);
    }
    return size;
  };
  const auto kept = written_with("121.84547 120.04586");
  write_artifact("text-engine-file-box.psd", kept);
  CHECK(count_of(tysh_text(kept, 0), "/BoxBounds [ 0.000000 0.000000 121.845470 120.045860 ]") == 1U);
  const auto kept_frame = frame_size(kept);
  CHECK(std::abs(kept_frame.first - 121.84547) < 0.0001 && std::abs(kept_frame.second - 120.04586) < 0.0001);
  // A Photoshop-layout box has Photoshop's own first-baseline rule: no /10 in its frame.
  {
    const auto payload = text_engine_payload(kept);
    const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
    CHECK(block.has_value());
    if (block.has_value()) {
      CHECK(block->root().at_path({"0", "8", "0", "0", "0", "2", "10"}) == nullptr);
      CHECK(block->root().at_path({"0", "8", "0", "0", "0", "2", "6"}) != nullptr);
    }
  }
  // No file box, or one the box no longer matches: the whole-pixel box.
  for (const char* file_box : {static_cast<const char*>(nullptr), "125.5 120.04586", "121.84547 90", "nonsense"}) {
    const auto plain = written_with(file_box);
    CHECK(count_of(tysh_text(plain, 0), "/BoxBounds [ 0.000000 0.000000 122.000000 121.000000 ]") == 1U);
    const auto plain_frame = frame_size(plain);
    CHECK(plain_frame.first == 122.0 && plain_frame.second == 121.0);
  }
  // A reopened file reads the box back as whole pixels and carries no session key.
  const auto reread = patchy::psd::DocumentIo::read(kept);
  CHECK(reread.layers().size() == 1U);
  if (reread.layers().size() == 1U) {
    const auto& metadata = reread.layers()[0].metadata();
    CHECK(!metadata.contains(patchy::kLayerMetadataTextFileBox));
    CHECK(metadata.at(patchy::kLayerMetadataTextBoxWidth) == "122");
    CHECK(metadata.at(patchy::kLayerMetadataTextBoxHeight) == "120");
  }
}

// A Qt-natural box Patchy authors keeps the cap-height first-baseline rule in its frame, the one
// its baseline inset is measured against.
void text_engine_qt_natural_box_frame_keeps_the_cap_height_rule() {
  patchy::Document document(600, 300, patchy::PixelFormat::rgb8());
  document.add_layer(patchy_text_layer(document, "Box", "Box text here, second paragraph", true));
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto payload = text_engine_payload(written);
  const auto block = payload.has_value() ? patchy::psd::TextEngineBlock::parse(*payload) : std::nullopt;
  CHECK(block.has_value());
  if (!block.has_value()) {
    return;
  }
  const auto* rule = block->root().at_path({"0", "8", "0", "0", "0", "2", "10"});
  CHECK(rule != nullptr);
  CHECK(sheet_integer(rule, "0") == 2);
}

std::vector<patchy::test::TestCase> text_engine_block_tests() {
  return {
      {"text_engine_box_keeps_the_files_fractional_size", text_engine_box_keeps_the_files_fractional_size},
      {"text_engine_qt_natural_box_frame_keeps_the_cap_height_rule",
       text_engine_qt_natural_box_frame_keeps_the_cap_height_rule},
      {"text_engine_kerning_round_trips", text_engine_kerning_round_trips},
      {"text_engine_kerning_reads_photoshop_sheets", text_engine_kerning_reads_photoshop_sheets},
      {"text_engine_anti_alias_reads_the_descriptor_method", text_engine_anti_alias_reads_the_descriptor_method},
      {"text_engine_old_engine_block_is_replaced_when_every_layer_is_regenerated",
       text_engine_old_engine_block_is_replaced_when_every_layer_is_regenerated},
      {"text_engine_area_frames_preserve_native_contours", text_engine_area_frames_preserve_native_contours},
      {"text_engine_nul_padded_block_keeps_frames_editable", text_engine_nul_padded_block_keeps_frames_editable},
      {"text_engine_named_frames_keep_point_and_box_editable", text_engine_named_frames_keep_point_and_box_editable},
      {"text_engine_area_frames_reject_unrecognized_geometry", text_engine_area_frames_reject_unrecognized_geometry},
      {"text_engine_area_overflow_and_protected_rasters", text_engine_area_overflow_and_protected_rasters},
      {"text_engine_photoshop_blocks_round_trip_byte_exact", text_engine_photoshop_blocks_round_trip_byte_exact},
      {"text_engine_syntax_parses_and_authors_tokens", text_engine_syntax_parses_and_authors_tokens},
      {"text_engine_patchy_born_document_writes_a_block", text_engine_patchy_born_document_writes_a_block},
      {"text_engine_retyped_layer_replaces_its_object_in_place", text_engine_retyped_layer_replaces_its_object_in_place},
      {"text_engine_paragraph_composer_round_trips", text_engine_paragraph_composer_round_trips},
      {"text_engine_justify_modes_round_trip", text_engine_justify_modes_round_trip},
      {"text_engine_run_languages_round_trip", text_engine_run_languages_round_trip},
      {"text_engine_language_tables_match_photoshop", text_engine_language_tables_match_photoshop},
      {"text_engine_photoshop_languages_survive_an_edit", text_engine_photoshop_languages_survive_an_edit},
      {"text_engine_hyphenation_round_trips", text_engine_hyphenation_round_trips},
      {"text_engine_middle_eastern_settings_round_trip", text_engine_middle_eastern_settings_round_trip},
      {"text_engine_line_composer_round_trips", text_engine_line_composer_round_trips},
      {"text_engine_line_composer_reads_photoshop_sheets", text_engine_line_composer_reads_photoshop_sheets},
      {"text_engine_photoshop_line_composer_survives_an_edit_if_available",
       text_engine_photoshop_line_composer_survives_an_edit_if_available},
      {"text_engine_issue_683_reporter_settings_survive_an_edit_if_available",
       text_engine_issue_683_reporter_settings_survive_an_edit_if_available},
      {"text_engine_issue341_every_object_keeps_a_cache_key_if_available",
       text_engine_issue341_every_object_keeps_a_cache_key_if_available},
  };
}
