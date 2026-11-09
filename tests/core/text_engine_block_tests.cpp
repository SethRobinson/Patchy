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

std::vector<patchy::test::TestCase> text_engine_block_tests() {
  return {
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
      {"text_engine_issue341_every_object_keeps_a_cache_key_if_available",
       text_engine_issue341_every_object_keeps_a_cache_key_if_available},
  };
}
