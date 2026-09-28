// Photoshop 5.0/5.5 'tySh' type records (docs/psd-legacy-text.md): the synthetic record
// builder mirrors the layout decoded from Title02.psd byte for byte, and the local fixture
// test pins that file's six type layers against what Photoshop 2026 reads from them over COM.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "psd_test_support.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using patchy::test::find_layer_named;
using patchy::test::psd_layer_block_payload;
using patchy::test::psd_layer_extra_data;
using patchy::test::single_text_layer_psd;
using patchy::test::write_pascal_padded;

struct LegacyFaceSpec {
  std::uint16_t mark{1};
  std::string postscript_name;
  std::string family;
  std::string style;
};

struct LegacyStyleSpec {
  std::uint16_t mark{1};
  std::uint16_t face_mark{1};
  double size{32.0};
  double tracking_em{0.0};
  double leading{0.0};
};

struct LegacyLineSpec {
  std::int16_t alignment{0};  // 0 left, 1 center, -1 right
  std::vector<std::pair<std::uint16_t, std::uint16_t>> units;  // (UTF-16 unit, style mark)
};

struct LegacyRecordSpec {
  std::array<double, 6> transform{1.0, 0.0, 0.0, 1.0, 120.0, 60.0};
  std::vector<LegacyFaceSpec> faces;
  std::vector<LegacyStyleSpec> styles;
  std::vector<LegacyLineSpec> lines;
  std::uint16_t color_space{1};  // HSB
  std::array<std::uint16_t, 4> color{39835, 65535, 30583, 0};  // #002a77 in HSB
  std::uint8_t anti_alias{1};
  // The specification's u16 style-info version word, absent from real PS 5.x files.
  bool style_version_word{false};
};

std::uint32_t fixed_16_16(double value) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(value * 65536.0)));
}

std::vector<std::pair<std::uint16_t, std::uint16_t>> ascii_units(std::string_view text, std::uint16_t style_mark) {
  std::vector<std::pair<std::uint16_t, std::uint16_t>> units;
  for (const char ch : text) {
    units.emplace_back(static_cast<std::uint16_t>(static_cast<unsigned char>(ch)), style_mark);
  }
  return units;
}

std::vector<std::uint8_t> legacy_type_tool_payload(const LegacyRecordSpec& spec) {
  patchy::psd::BigEndianWriter writer;
  writer.write_u16(1);
  for (const double value : spec.transform) {
    patchy::psd::write_f64(writer, value);
  }
  writer.write_u16(6);
  writer.write_u16(static_cast<std::uint16_t>(spec.faces.size()));
  for (const auto& face : spec.faces) {
    writer.write_u16(face.mark);
    writer.write_u32(0);
    write_pascal_padded(writer, face.postscript_name, 1);
    write_pascal_padded(writer, face.family, 1);
    write_pascal_padded(writer, face.style, 1);
    writer.write_u16(0);
    writer.write_u32(0);
  }
  if (spec.style_version_word) {
    writer.write_u16(6);
  }
  writer.write_u16(static_cast<std::uint16_t>(spec.styles.size()));
  for (const auto& style : spec.styles) {
    writer.write_u16(style.mark);
    writer.write_u16(style.face_mark);
    writer.write_u32(fixed_16_16(style.size));
    writer.write_u32(fixed_16_16(style.tracking_em));
    writer.write_u32(fixed_16_16(0.02));  // kerning, not modeled
    writer.write_u32(fixed_16_16(style.leading));
    writer.write_u32(0);                  // base shift
    writer.write_u8(1);                   // auto kern
    writer.write_u8(0);                   // rotate
  }
  std::uint32_t character_count = 0;
  for (const auto& line : spec.lines) {
    character_count += static_cast<std::uint32_t>(line.units.size());
  }
  writer.write_u16(0);
  writer.write_u32(fixed_16_16(1.0));
  writer.write_u32(character_count);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(0);
  writer.write_u32(character_count);
  writer.write_u16(static_cast<std::uint16_t>(spec.lines.size()));
  for (const auto& line : spec.lines) {
    writer.write_u32(static_cast<std::uint32_t>(line.units.size()));
    writer.write_u16(0);
    writer.write_u16(static_cast<std::uint16_t>(line.alignment));
    for (const auto& [unit, style_mark] : line.units) {
      writer.write_u16(unit);
      writer.write_u16(style_mark);
    }
  }
  writer.write_u16(spec.color_space);
  for (const auto component : spec.color) {
    writer.write_u16(component);
  }
  writer.write_u8(spec.anti_alias);
  while ((writer.bytes().size() % 2U) != 0U) {
    writer.write_u8(0);
  }
  return writer.bytes();
}

// Two faces no machine has installed, so the reader's GDI-name fallback decides the family;
// two lines ("FLY!" then "QUIT") with the second style starting inside the first line.
LegacyRecordSpec two_line_spec() {
  LegacyRecordSpec spec;
  spec.faces = {{1, "ZzTestFace-Bold", "Zz Test Face", "Bold"}, {2, "ZzOther-Regular", "Zz Other", "Regular"}};
  spec.styles = {{4, 1, 32.0, 0.1, 34.0}, {2, 2, 24.0, 0.0, 0.0}};
  LegacyLineSpec first;
  first.alignment = 1;
  first.units = ascii_units("F", 4);
  const auto rest = ascii_units("LY!\r", 2);
  first.units.insert(first.units.end(), rest.begin(), rest.end());
  LegacyLineSpec second;
  second.alignment = -1;
  second.units = ascii_units("QUIT", 2);
  spec.lines = {first, second};
  return spec;
}

std::vector<std::vector<std::string>> parse_tab_rows(std::string_view serialized) {
  std::vector<std::vector<std::string>> rows;
  std::istringstream lines{std::string(serialized)};
  std::string line;
  bool first = true;
  while (std::getline(lines, line)) {
    if (first) {  // version token
      first = false;
      continue;
    }
    std::vector<std::string> columns;
    std::istringstream cells(line);
    std::string cell;
    while (std::getline(cells, cell, '\t')) {
      columns.push_back(cell);
    }
    rows.push_back(std::move(columns));
  }
  return rows;
}

std::string metadata_or_empty(const patchy::Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  return found == layer.metadata().end() ? std::string{} : found->second;
}

void psd_legacy_type_tool_block_imports_as_editable_text() {
  const auto payload = legacy_type_tool_payload(two_line_spec());
  const auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(payload, "tySh"));
  const auto* layer = find_layer_named(document.layers(), "Text Layer");
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(patchy::layer_is_text(*layer));
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataText) == "FLY!\nQUIT");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextSourceBlock) == "tySh");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextRasterStatus) == "placeholder");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextLayoutMode) == patchy::kTextLayoutModePhotoshop);
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextFlow) == "point");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextColor) == "#002a77");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextFont) == "Zz Test Face");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextSize) == "32");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextBold) == "true");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextItalic) == "false");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextAntiAlias) == "4");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataPsdTextTransform) == "1 0 0 1 120 60");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextTransform) == "1 0 0 1 120 60");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataPsdTextBounds) == "0 0 0 0");
  CHECK(!layer->metadata().contains(patchy::kLayerMetadataPsdTextIndex));

  const auto runs = parse_tab_rows(metadata_or_empty(*layer, patchy::kLayerMetadataTextRuns));
  CHECK(runs.size() == 2);
  if (runs.size() == 2) {
    // start, length, size, bold, italic, color, family, leading, tracking, hscale, vscale, ...
    CHECK(runs[0].size() >= 11 && runs[1].size() >= 11);
    if (runs[0].size() >= 11 && runs[1].size() >= 11) {
      CHECK(runs[0][0] == "0" && runs[0][1] == "1");
      CHECK(runs[0][2] == "32" && runs[0][3] == "1" && runs[0][4] == "0");
      CHECK(runs[0][5] == "#002a77" && runs[0][6] == "Zz%20Test%20Face");
      CHECK(runs[0][7] == "34" && runs[0][8] == "100");
      CHECK(runs[1][0] == "1" && runs[1][1] == "8");
      CHECK(runs[1][2] == "24" && runs[1][3] == "0" && runs[1][6] == "Zz%20Other");
      CHECK(runs[1][7] == "auto" && runs[1][8] == "0");
    }
  }
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextParagraphRuns) == "v1\n0\t5\tcenter\n5\t4\tright");
}

void psd_legacy_type_tool_untouched_save_keeps_the_block_verbatim() {
  const auto payload = legacy_type_tool_payload(two_line_spec());
  const auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(payload, "tySh"));
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra = psd_layer_extra_data(bytes, 0);
  const auto written = psd_layer_block_payload(extra, "tySh");
  CHECK(written.has_value());
  CHECK(written.has_value() && *written == payload);
  CHECK(!psd_layer_block_payload(extra, "TySh").has_value());

  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const auto* layer = find_layer_named(reread.layers(), "Text Layer");
  CHECK(layer != nullptr && metadata_or_empty(*layer, patchy::kLayerMetadataText) == "FLY!\nQUIT");
}

void psd_legacy_type_tool_edited_layer_writes_modern_type_block() {
  const auto payload = legacy_type_tool_payload(two_line_spec());
  auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(payload, "tySh"));
  patchy::Layer* layer = nullptr;
  for (auto& candidate : document.layers()) {
    if (candidate.name() == "Text Layer") {
      layer = &candidate;
    }
  }
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  layer->metadata()[patchy::kLayerMetadataText] = "FLY!\nEXIT";
  layer->metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  // A second edited legacy layer must get its own text object, not overwrite the first's.
  patchy::Layer second(document.allocate_layer_id(), "Second", patchy::test::solid_rgba(60, 30, 0, 0, 0, 0));
  second.set_bounds(patchy::Rect{20, 80, 60, 30});
  for (const auto& [key, value] : layer->metadata()) {
    second.metadata()[key] = value;
  }
  second.metadata()[patchy::kLayerMetadataText] = "MENU";
  second.unknown_psd_blocks() = layer->unknown_psd_blocks();
  document.add_layer(std::move(second));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  for (const std::int16_t index : {std::int16_t{0}, std::int16_t{1}}) {
    const auto extra = psd_layer_extra_data(bytes, index);
    CHECK(!psd_layer_block_payload(extra, "tySh").has_value());
    CHECK(psd_layer_block_payload(extra, "TySh").has_value());
  }
  const std::string marker = "8BIMTxt2";
  CHECK(std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) != bytes.end());

  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const auto* first = find_layer_named(reread.layers(), "Text Layer");
  const auto* second_read = find_layer_named(reread.layers(), "Second");
  CHECK(first != nullptr && metadata_or_empty(*first, patchy::kLayerMetadataText) == "FLY!\nEXIT");
  CHECK(second_read != nullptr && metadata_or_empty(*second_read, patchy::kLayerMetadataText) == "MENU");
  CHECK(first != nullptr && metadata_or_empty(*first, patchy::kLayerMetadataTextSourceBlock) == "TySh");
  if (first != nullptr && second_read != nullptr) {
    CHECK(metadata_or_empty(*first, patchy::kLayerMetadataPsdTextIndex) !=
          metadata_or_empty(*second_read, patchy::kLayerMetadataPsdTextIndex));
  }
}

// The presence of a document-level 'Txt2' block makes Photoshop drop every untouched tySh
// layer to a plain pixel layer (COM readback of a Patchy resave, September 28, 2026), so a
// document that still carries one gets no block; the edited layer's TySh stands on its own.
void psd_legacy_type_tool_kept_record_suppresses_the_text_engine_block() {
  const auto payload = legacy_type_tool_payload(two_line_spec());
  auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(payload, "tySh"));
  patchy::Layer* layer = nullptr;
  for (auto& candidate : document.layers()) {
    if (candidate.name() == "Text Layer") {
      layer = &candidate;
    }
  }
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  patchy::Layer edited(document.allocate_layer_id(), "Edited", patchy::test::solid_rgba(60, 30, 0, 0, 0, 0));
  edited.set_bounds(patchy::Rect{20, 80, 60, 30});
  for (const auto& [key, value] : layer->metadata()) {
    edited.metadata()[key] = value;
  }
  edited.metadata()[patchy::kLayerMetadataText] = "MENU";
  edited.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  edited.unknown_psd_blocks() = layer->unknown_psd_blocks();
  document.add_layer(std::move(edited));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string marker = "8BIMTxt2";
  CHECK(std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) == bytes.end());
  const auto untouched = psd_layer_block_payload(psd_layer_extra_data(bytes, 0), "tySh");
  CHECK(untouched.has_value() && *untouched == payload);
  CHECK(psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "TySh").has_value());
  CHECK(!psd_layer_block_payload(psd_layer_extra_data(bytes, 1), "tySh").has_value());

  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const auto* kept = find_layer_named(reread.layers(), "Text Layer");
  const auto* regenerated = find_layer_named(reread.layers(), "Edited");
  CHECK(kept != nullptr && metadata_or_empty(*kept, patchy::kLayerMetadataText) == "FLY!\nQUIT");
  CHECK(regenerated != nullptr && metadata_or_empty(*regenerated, patchy::kLayerMetadataText) == "MENU");
}

void psd_legacy_type_tool_truncated_payload_stays_a_pixel_layer() {
  const auto payload = legacy_type_tool_payload(two_line_spec());
  for (std::size_t length = 0; length < payload.size(); ++length) {
    const std::vector<std::uint8_t> truncated(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(length));
    const auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(truncated, "tySh"));
    const auto* layer = find_layer_named(document.layers(), "Text Layer");
    CHECK(layer != nullptr);
    if (layer != nullptr && patchy::layer_is_text(*layer)) {
      std::cout << "[legacy tySh] prefix of " << length << " bytes read as text\n";
      CHECK(false);
      return;
    }
  }
  // An absurd character count must not allocate or read past the block either.
  auto inflated = payload;
  const std::string family_marker = "Zz Other";
  const auto family_at = std::search(inflated.begin(), inflated.end(), family_marker.begin(), family_marker.end());
  CHECK(family_at != inflated.end());
  // The character count follows: style count (2) + 2 x 26 style bytes + type (2) + scaling (4).
  const auto count_offset = static_cast<std::size_t>(family_at - inflated.begin()) + family_marker.size() +
                            1U + std::string("Regular").size() + 2U + 4U + 2U + 2U * 26U + 2U + 4U;
  CHECK(count_offset + 4U <= inflated.size());
  if (count_offset + 4U <= inflated.size()) {
    inflated[count_offset] = 0xFF;
    inflated[count_offset + 1U] = 0xFF;
    inflated[count_offset + 2U] = 0xFF;
    inflated[count_offset + 3U] = 0xFF;
    const auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(inflated, "tySh"));
    const auto* layer = find_layer_named(document.layers(), "Text Layer");
    CHECK(layer != nullptr && !patchy::layer_is_text(*layer));
  }
}

void psd_legacy_type_tool_style_version_word_parses() {
  auto spec = two_line_spec();
  spec.style_version_word = true;
  const auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(legacy_type_tool_payload(spec), "tySh"));
  const auto* layer = find_layer_named(document.layers(), "Text Layer");
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataText) == "FLY!\nQUIT");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextSize) == "32");
  CHECK(metadata_or_empty(*layer, patchy::kLayerMetadataTextParagraphRuns) == "v1\n0\t5\tcenter\n5\t4\tright");
}

void psd_legacy_type_tool_rgb_and_gray_colors() {
  auto spec = two_line_spec();
  spec.color_space = 0;
  spec.color = {65535, 32896, 0, 0};
  auto document = patchy::psd::DocumentIo::read(single_text_layer_psd(legacy_type_tool_payload(spec), "tySh"));
  auto* layer = find_layer_named(document.layers(), "Text Layer");
  CHECK(layer != nullptr && metadata_or_empty(*layer, patchy::kLayerMetadataTextColor) == "#ff8000");
  spec.color_space = 8;
  spec.color = {5000, 0, 0, 0};
  document = patchy::psd::DocumentIo::read(single_text_layer_psd(legacy_type_tool_payload(spec), "tySh"));
  layer = find_layer_named(document.layers(), "Text Layer");
  CHECK(layer != nullptr && metadata_or_empty(*layer, patchy::kLayerMetadataTextColor) == "#808080");
  spec.anti_alias = 0;
  document = patchy::psd::DocumentIo::read(single_text_layer_psd(legacy_type_tool_payload(spec), "tySh"));
  layer = find_layer_named(document.layers(), "Text Layer");
  CHECK(layer != nullptr && metadata_or_empty(*layer, patchy::kLayerMetadataTextAntiAlias) == "0");
}

struct Title02Expectation {
  std::string text;
  std::string size;
  std::string color;
  std::string alignment;
  std::string family_fragment;
};

void psd_title02_legacy_text_layers_import_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Title02.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local Title02.psd fixture missing: " << path.string() << '\n';
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  // Photoshop 2026 over COM (September 2026): contents, size (px), color, justification.
  const std::vector<Title02Expectation> expected = {
      {"Copyright \xC2\xA9 2000 MachineWorks Northwest", "9", "#002a77", "right", "Arial"},
      {"WWW.COCKPITMASTER.COM", "23", "#004995", "left", "Futura"},
      {"Radio Control Flight Simulator", "18", "#000000", "center", "Futura"},
      {"COCKPIT MASTER", "72", "#000000", "center", "Anna"},
      {"COCKPIT MASTER", "72", "#ffffff", "center", "Anna"},
      {"FLY!\nPLAY ONLINE\naIRPLANE\nRUNWAY\nGRAPHICS\nCONTROLS\nMISC.\nHELP\nQUIT", "32", "#000000", "left", "Anna"},
  };
  std::vector<bool> matched(expected.size(), false);
  int text_layers = 0;
  const std::function<void(const std::vector<patchy::Layer>&)> visit = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      visit(layer.children());
      if (!patchy::layer_is_text(layer)) {
        continue;
      }
      ++text_layers;
      const auto text = metadata_or_empty(layer, patchy::kLayerMetadataText);
      const auto color = metadata_or_empty(layer, patchy::kLayerMetadataTextColor);
      for (std::size_t index = 0; index < expected.size(); ++index) {
        if (matched[index] || expected[index].text != text || expected[index].color != color) {
          continue;
        }
        matched[index] = true;
        CHECK(metadata_or_empty(layer, patchy::kLayerMetadataTextSourceBlock) == "tySh");
        CHECK(metadata_or_empty(layer, patchy::kLayerMetadataTextRasterStatus) == "psd_raster_preview");
        CHECK(metadata_or_empty(layer, patchy::kLayerMetadataTextLayoutMode) == patchy::kTextLayoutModePhotoshop);
        CHECK(metadata_or_empty(layer, patchy::kLayerMetadataTextSize) == expected[index].size);
        CHECK(metadata_or_empty(layer, patchy::kLayerMetadataTextAntiAlias) == "4");
        CHECK(metadata_or_empty(layer, patchy::kLayerMetadataTextFont).find(expected[index].family_fragment) !=
              std::string::npos);
        const auto paragraphs = parse_tab_rows(metadata_or_empty(layer, patchy::kLayerMetadataTextParagraphRuns));
        CHECK(!paragraphs.empty() && paragraphs.front().size() >= 3 &&
              paragraphs.front()[2] == expected[index].alignment);
        const auto runs = parse_tab_rows(metadata_or_empty(layer, patchy::kLayerMetadataTextRuns));
        CHECK(!runs.empty());
        if (expected[index].text.rfind("FLY!", 0) == 0) {
          CHECK(paragraphs.size() == 9);
          // Anna 32 with fixed leading 34 and tracking 100 on every run.
          CHECK(std::all_of(runs.begin(), runs.end(), [](const std::vector<std::string>& run) {
            return run.size() >= 9 && run[2] == "32" && run[7] == "34" && run[8] == "100";
          }));
        }
        if (expected[index].text == "WWW.COCKPITMASTER.COM") {
          // Scaled through the record's transform: size 23.31 x 0.7722 renders at 18 px.
          const auto transform = metadata_or_empty(layer, patchy::kLayerMetadataPsdTextTransform);
          std::istringstream values(transform);
          std::array<double, 6> parsed{};
          for (double& value : parsed) {
            values >> value;
          }
          CHECK(std::abs(parsed[0] - 0.7722) < 0.001 && std::abs(parsed[3] - 0.7722) < 0.001);
          CHECK(std::abs(parsed[4] - 264.0) < 0.001 && std::abs(parsed[5] - 475.0) < 0.001);
          CHECK(!runs.empty() && runs.front().size() >= 9 && runs.front()[8] == "400");
        }
      }
    }
  };
  visit(document.layers());
  CHECK(text_layers == 6);
  CHECK(std::all_of(matched.begin(), matched.end(), [](bool value) { return value; }));

  // Untouched, every legacy record saves back byte for byte.
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const std::string legacy_marker = "8BIMtySh";
  const std::string modern_marker = "8BIMTySh";
  std::size_t legacy_blocks = 0;
  for (auto it = std::search(bytes.begin(), bytes.end(), legacy_marker.begin(), legacy_marker.end());
       it != bytes.end(); it = std::search(it + 1, bytes.end(), legacy_marker.begin(), legacy_marker.end())) {
    ++legacy_blocks;
  }
  CHECK(legacy_blocks == 6);
  CHECK(std::search(bytes.begin(), bytes.end(), modern_marker.begin(), modern_marker.end()) == bytes.end());
  // No 'Txt2' either: with one present Photoshop opens every kept tySh layer as pixels.
  const std::string engine_marker = "8BIMTxt2";
  CHECK(std::search(bytes.begin(), bytes.end(), engine_marker.begin(), engine_marker.end()) == bytes.end());
}

}  // namespace

std::vector<patchy::test::TestCase> psd_legacy_text_tests() {
  return {
      {"psd_legacy_type_tool_block_imports_as_editable_text", psd_legacy_type_tool_block_imports_as_editable_text},
      {"psd_legacy_type_tool_untouched_save_keeps_the_block_verbatim",
       psd_legacy_type_tool_untouched_save_keeps_the_block_verbatim},
      {"psd_legacy_type_tool_edited_layer_writes_modern_type_block",
       psd_legacy_type_tool_edited_layer_writes_modern_type_block},
      {"psd_legacy_type_tool_kept_record_suppresses_the_text_engine_block",
       psd_legacy_type_tool_kept_record_suppresses_the_text_engine_block},
      {"psd_legacy_type_tool_truncated_payload_stays_a_pixel_layer",
       psd_legacy_type_tool_truncated_payload_stays_a_pixel_layer},
      {"psd_legacy_type_tool_style_version_word_parses", psd_legacy_type_tool_style_version_word_parses},
      {"psd_legacy_type_tool_rgb_and_gray_colors", psd_legacy_type_tool_rgb_and_gray_colors},
      {"psd_title02_legacy_text_layers_import_if_available", psd_title02_legacy_text_layers_import_if_available},
  };
}
