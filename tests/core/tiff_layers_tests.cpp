// Layered TIFF (docs/tiff.md): Photoshop layer data in tag 37724, read through the PSD reader.
// Synthetic files come from tests/layered_tiff_fixture.hpp.

#include "core/document.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_tiff.hpp"
#include "support/path_utils.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include "layered_tiff_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using patchy::BlendMode;
using patchy::Document;
using patchy::Layer;
using patchy::LayerKind;

using namespace patchy::test::layered_tiff;

patchy::psd::ReadOptions eight_bit_options() {
  patchy::psd::ReadOptions options;
  options.keep_bit_depth = false;
  return options;
}

const Layer* find_layer(const std::vector<Layer>& layers, std::string_view name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* child = find_layer(layer.children(), name)) {
      return child;
    }
  }
  return nullptr;
}

std::string utf8(std::u16string_view text) {
  std::string result;
  for (const auto unit : text) {
    const auto code = static_cast<std::uint32_t>(unit);
    if (code < 0x80U) {
      result.push_back(static_cast<char>(code));
    } else if (code < 0x800U) {
      result.push_back(static_cast<char>(0xC0U | (code >> 6U)));
      result.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
      result.push_back(static_cast<char>(0xE0U | (code >> 12U)));
      result.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
  }
  return result;
}

void check_sample_document(const Document& document) {
  CHECK(document.width() == 8);
  CHECK(document.height() == 6);
  CHECK(document.metadata().values.at("tiff.layers") == "photoshop");
  CHECK(std::abs(document.print_settings().horizontal_ppi - 72.0) < 0.01);
  const auto& top = document.layers();
  CHECK(top.size() == 3U);
  const auto* background = find_layer(top, "Background");
  const auto* masked = find_layer(top, utf8(u"Rot ü テ"));
  const auto* group = find_layer(top, "Group");
  const auto* inner = find_layer(top, "Inside");
  CHECK(background != nullptr && masked != nullptr && group != nullptr && inner != nullptr);
  if (background == nullptr || masked == nullptr || group == nullptr || inner == nullptr) {
    return;
  }
  CHECK(group->kind() == LayerKind::Group);
  CHECK(group->children().size() == 1U && group->children().front().name() == "Inside");
  CHECK(masked->blend_mode() == BlendMode::Multiply);
  CHECK(std::abs(masked->opacity() - 200.0F / 255.0F) < 0.01F);
  CHECK(masked->mask().has_value());
  if (masked->mask().has_value()) {
    CHECK(masked->mask()->bounds.x == 2 && masked->mask()->bounds.y == 1);
    CHECK(masked->mask()->bounds.width == 5 && masked->mask()->bounds.height == 4);
  }
  CHECK(!inner->visible());
  CHECK(!inner->layer_style().color_overlays.empty());
  if (!inner->layer_style().color_overlays.empty()) {
    const auto& overlay = inner->layer_style().color_overlays.front();
    CHECK(overlay.enabled);
    CHECK(overlay.color.red == 255 && overlay.color.green == 0 && overlay.color.blue == 0);
  }
  // Pixels: raw samples on the masked layer, RLE on the others.
  LayerSpec masked_spec;
  masked_spec.seed = 40;
  const auto& pixels = masked->pixels();
  CHECK(pixels.width() == 5 && pixels.height() == 4);
  bool pixels_match = true;
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* pixel = pixels.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        pixels_match = pixels_match && pixel[channel] == sample(masked_spec, x, y, channel);
      }
      pixels_match = pixels_match && pixel[3] == static_cast<std::uint8_t>(255 - x * 9);
    }
  }
  CHECK(pixels_match);
  LayerSpec background_spec;
  background_spec.seed = 3;
  CHECK(background->pixels().pixel(7, 5)[1] == sample(background_spec, 7, 5, 1));
}

void tiff_layers_little_and_big_endian_convert_identically() {
  for (const int depth : {8, 16, 32}) {
    std::vector<std::string> little_notices;
    std::vector<std::string> big_notices;
    const auto from_little = patchy::psd::photoshop_document_from_tiff(sample_tiff(true, depth), &little_notices);
    const auto from_big = patchy::psd::photoshop_document_from_tiff(sample_tiff(false, depth), &big_notices);
    CHECK(from_little == from_big);
    CHECK(little_notices.empty());
    CHECK(big_notices.empty());
  }
}

void tiff_layers_open_with_groups_masks_effects_and_names() {
  for (const bool little : {true, false}) {
    const auto tiff = sample_tiff(little);
    CHECK(patchy::psd::tiff_has_photoshop_layers(tiff));
    const auto document = patchy::psd::read_layered_tiff(tiff, eight_bit_options());
    check_sample_document(document);
  }
}

void tiff_layers_deep_files_keep_their_depth() {
  for (const int depth : {16, 32}) {
    patchy::psd::ReadOptions options;
    options.keep_bit_depth = true;
    const auto document = patchy::psd::read_layered_tiff(sample_tiff(true, depth), options);
    CHECK(document.color_state().bit_depth ==
          (depth == 16 ? patchy::BitDepth::UInt16 : patchy::BitDepth::Float32));
    CHECK(find_layer(document.layers(), "Inside") != nullptr);
    if (depth == 16) {
      // value * 257 converts back to the 8-bit value exactly.
      const auto converted = patchy::psd::read_layered_tiff(sample_tiff(true, depth), eight_bit_options());
      check_sample_document(converted);
    }
  }
}

void tiff_layers_merged_transparency_opens() {
  // A negative layer count marks merged transparency; the composite then has four samples.
  const auto data = source_data(true, sample_layers(), 8, true);
  const auto tiff = tiff_file(true, 8, 6, 8, true, data);
  const auto document = patchy::psd::read_layered_tiff(tiff, eight_bit_options());
  CHECK(document.layers().size() == 3U);
}

void tiff_layers_absent_or_unsupported_layouts_are_refused() {
  const auto flat = tiff_file(true, 8, 6, 8, false, {});
  CHECK(!patchy::psd::tiff_has_photoshop_layers(flat));
  bool refused = false;
  try {
    (void)patchy::psd::read_layered_tiff(flat);
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);

  // Palette color (Photoshop's Indexed mode has no layers).
  refused = false;
  try {
    (void)patchy::psd::read_layered_tiff(
        tiff_file(true, 8, 6, 8, false, source_data(true, sample_layers(), 8, false), 3));
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);

  // No layers in the block.
  refused = false;
  try {
    (void)patchy::psd::read_layered_tiff(tiff_file(false, 8, 6, 8, false, source_data(false, {}, 8, false)));
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);

  // BigTIFF and non-TIFF bytes never claim layers.
  auto big_tiff = sample_tiff(true);
  big_tiff[2] = 43;
  CHECK(!patchy::psd::tiff_has_photoshop_layers(big_tiff));
  const std::vector<std::uint8_t> junk{'8', 'B', 'P', 'S', 0, 1};
  CHECK(!patchy::psd::tiff_has_photoshop_layers(junk));
  CHECK(patchy::psd::tiff_page_count(junk) == 0U);
}

void tiff_layers_unknown_little_endian_blocks_are_dropped_with_a_notice() {
  // A per-layer block whose layout is unknown and that is no descriptor: dropped, noted,
  // and the layers still open.
  auto layers = sample_layers();
  auto data = source_data(true, layers, 8, false);
  // Rename the first 'fxrp' ("prxf" on the wire) to an unknown key; its 16 bytes of
  // doubles do not walk as a descriptor.
  const std::array<std::uint8_t, 4> needle{'p', 'r', 'x', 'f'};
  const auto found = std::search(data.begin(), data.end(), needle.begin(), needle.end());
  CHECK(found != data.end());
  if (found == data.end()) {
    return;
  }
  std::copy_n(std::array<std::uint8_t, 4>{'z', 'z', 'z', 'z'}.begin(), 4, found);
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options = eight_bit_options();
  options.notices = &notices;
  const auto document = patchy::psd::read_layered_tiff(tiff_file(true, 8, 6, 8, false, data), options);
  check_sample_document(document);
  CHECK(std::any_of(notices.begin(), notices.end(),
                    [](const std::string& notice) { return notice.find("IBM PC byte order") != std::string::npos; }));
}

void tiff_layers_page_count_skips_reduced_resolution_directories() {
  CHECK(patchy::psd::tiff_page_count(sample_tiff(true)) == 1U);
  const auto pages = tiff_file(false, 8, 6, 8, false, {}, 2, 3);
  CHECK(patchy::psd::tiff_page_count(pages) == 3U);  // page 1, a preview, pages 2 and 3
}

// Damaged and hostile files: every truncation and a deterministic set of byte flips inside
// the layer data either opens or throws; nothing crashes, hangs or allocates wildly.
void tiff_layers_damaged_data_throws_or_opens() {
  std::uint64_t state = 0x7469666661ULL;
  const auto next = [&state] {
    state += 0x9E3779B97F4A7C15ULL;
    auto z = state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  };
  std::size_t attempts = 0;
  for (const bool little : {true, false}) {
    const auto tiff = sample_tiff(little);
    for (std::size_t cut = 0; cut < tiff.size(); cut += 5) {
      const std::vector<std::uint8_t> truncated(tiff.begin(), tiff.begin() + static_cast<std::ptrdiff_t>(cut));
      try {
        (void)patchy::psd::read_layered_tiff(truncated, eight_bit_options());
      } catch (const std::exception&) {
      }
      ++attempts;
    }
    for (int round = 0; round < 1500; ++round) {
      auto mutated = tiff;
      const auto flips = 1 + static_cast<int>(next() % 4U);
      for (int flip = 0; flip < flips; ++flip) {
        const auto at = static_cast<std::size_t>(next() % mutated.size());
        mutated[at] = static_cast<std::uint8_t>(next() & 0xFFU);
      }
      try {
        (void)patchy::psd::read_layered_tiff(mutated, eight_bit_options());
      } catch (const std::exception&) {
      }
      ++attempts;
    }
  }
  CHECK(attempts > 3000U);
}

void tiff_layers_unicode_path_opens() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"layered-tiff");
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto path = dir / patchy::test::unicode_path_piece(std::u8string(stem) + u8".tif");
    {
      std::ofstream file(path, std::ios::binary);
      const auto tiff = sample_tiff(true);
      file.write(reinterpret_cast<const char*>(tiff.data()), static_cast<std::streamsize>(tiff.size()));
    }
    const auto document = patchy::psd::read_layered_tiff_file(path, eight_bit_options());
    check_sample_document(document);
  }
}

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void collect_layers(const std::vector<Layer>& layers, std::vector<const Layer*>& out) {
  for (const auto& layer : layers) {
    out.push_back(&layer);
    collect_layers(layer.children(), out);
  }
}

// GIMP issue 7549's samples: Photoshop CC 2014 and 23.5 files in both byte orders
// (gitlab.gnome.org/GNOME/gimp merge request 910), copied into local-test-fixtures.
void tiff_layers_gimp_photoshop_samples_open_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "layered-tiff" / "gimp-mr910";
  patchy::test::skip_unless_exists(dir / "ret-IBMFormat.tif");
  for (const auto* stem : {"RedBlue", "ret", "test"}) {
    std::vector<std::string> notices;
    auto options = eight_bit_options();
    options.notices = &notices;
    const auto little =
        patchy::psd::read_layered_tiff(file_bytes(dir / (std::string(stem) + "-IBMFormat.tif")), options);
    // Every block these files carry (luni, lnsr, lyid, clbl, infx, knko, lspf, lclr, shmd,
    // fxrp, LMsk, Patt, FMsk) converts: nothing was dropped.
    CHECK(std::none_of(notices.begin(), notices.end(), [](const std::string& notice) {
      return notice.find("IBM PC byte order") != std::string::npos;
    }));
    const auto big = patchy::psd::read_layered_tiff(file_bytes(dir / (std::string(stem) + "-MacFormat.tif")),
                                                    eight_bit_options());
    std::vector<const Layer*> little_layers;
    std::vector<const Layer*> big_layers;
    collect_layers(little.layers(), little_layers);
    collect_layers(big.layers(), big_layers);
    CHECK(!little_layers.empty());
    CHECK(little_layers.size() == big_layers.size());
    for (std::size_t index = 0; index < std::min(little_layers.size(), big_layers.size()); ++index) {
      CHECK(little_layers[index]->name() == big_layers[index]->name());
      CHECK(little_layers[index]->blend_mode() == big_layers[index]->blend_mode());
      CHECK(little_layers[index]->bounds().width == big_layers[index]->bounds().width);
    }
  }
  // The 16-bit pair was saved from one document: its layers match pixel for pixel.
  const auto little = patchy::psd::read_layered_tiff(file_bytes(dir / "ret-IBMFormat.tif"), eight_bit_options());
  const auto big = patchy::psd::read_layered_tiff(file_bytes(dir / "ret-MacFormat.tif"), eight_bit_options());
  CHECK(little.layers().size() == 4U);
  for (std::size_t index = 0; index < std::min(little.layers().size(), big.layers().size()); ++index) {
    const auto& a = little.layers()[index].pixels();
    const auto& b = big.layers()[index].pixels();
    CHECK(a.width() == b.width() && a.height() == b.height());
    CHECK(std::equal(a.data().begin(), a.data().end(), b.data().begin(), b.data().end()));
  }
}

// The section of a PSD stream that holds the layers (after the image resources).
std::vector<std::uint8_t> layer_section(const std::vector<std::uint8_t>& psd) {
  std::size_t at = 26;
  const auto skip_block = [&psd, &at] {
    const auto length = (static_cast<std::uint32_t>(psd[at]) << 24U) | (static_cast<std::uint32_t>(psd[at + 1]) << 16U) |
                        (static_cast<std::uint32_t>(psd[at + 2]) << 8U) | psd[at + 3];
    at += 4U + length;
  };
  skip_block();  // color mode data
  skip_block();  // image resources
  return std::vector<std::uint8_t>(psd.begin() + static_cast<std::ptrdiff_t>(at), psd.end());
}

// Photoshop's own IBM PC and Macintosh saves of one document
// (scripts/dev/tiff/ps-save-layered-tiffs.ps1): the little-endian layer data must convert
// to exactly the big-endian bytes, with nothing dropped, and both must open.
void tiff_layers_photoshop_byte_order_pairs_match_if_available() {
  const auto dir = patchy::test::source_root_path() / "local-test-fixtures" / "layered-tiff" / "photoshop";
  if (!std::filesystem::exists(dir)) {
    patchy::test::skip("optional fixture missing: local-test-fixtures/layered-tiff/photoshop");
  }
  std::size_t pairs = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const auto name = patchy::path_to_utf8(entry.path().filename());
    if (name.size() < 8U || name.substr(name.size() - 8U) != ".ibm.tif") {
      continue;
    }
    const auto stem = name.substr(0, name.size() - 8U);
    const auto mac = dir / (stem + ".mac.tif");
    if (!std::filesystem::exists(mac)) {
      continue;
    }
    std::vector<std::string> notices;
    const auto little = patchy::psd::photoshop_document_from_tiff(file_bytes(entry.path()), &notices);
    const auto big = patchy::psd::photoshop_document_from_tiff(file_bytes(mac));
    const bool same = layer_section(little) == layer_section(big);
    if (!same || !notices.empty()) {
      std::cerr << "  layer data differs or was dropped: " << stem << "\n";
    }
    CHECK(same);
    CHECK(notices.empty());
    for (const auto& path : {entry.path(), mac, dir / (stem + ".ibm-zip.tif")}) {
      if (std::filesystem::exists(path)) {
        CHECK(!patchy::psd::read_layered_tiff(file_bytes(path), eight_bit_options()).layers().empty());
      }
    }
    if (const auto flat = dir / (stem + ".flat.tif"); std::filesystem::exists(flat)) {
      CHECK(!patchy::psd::tiff_has_photoshop_layers(file_bytes(flat)));
    }
    ++pairs;
  }
  if (pairs == 0U) {
    patchy::test::skip("no .ibm.tif/.mac.tif pairs in local-test-fixtures/layered-tiff/photoshop");
  }
}

}  // namespace

std::vector<patchy::test::TestCase> tiff_layers_tests() {
  return {
      {"tiff_layers_little_and_big_endian_convert_identically", tiff_layers_little_and_big_endian_convert_identically},
      {"tiff_layers_open_with_groups_masks_effects_and_names", tiff_layers_open_with_groups_masks_effects_and_names},
      {"tiff_layers_deep_files_keep_their_depth", tiff_layers_deep_files_keep_their_depth},
      {"tiff_layers_merged_transparency_opens", tiff_layers_merged_transparency_opens},
      {"tiff_layers_absent_or_unsupported_layouts_are_refused", tiff_layers_absent_or_unsupported_layouts_are_refused},
      {"tiff_layers_unknown_little_endian_blocks_are_dropped_with_a_notice",
       tiff_layers_unknown_little_endian_blocks_are_dropped_with_a_notice},
      {"tiff_layers_page_count_skips_reduced_resolution_directories",
       tiff_layers_page_count_skips_reduced_resolution_directories},
      {"tiff_layers_damaged_data_throws_or_opens", tiff_layers_damaged_data_throws_or_opens},
      {"tiff_layers_unicode_path_opens", tiff_layers_unicode_path_opens},
      {"tiff_layers_gimp_photoshop_samples_open_if_available", tiff_layers_gimp_photoshop_samples_open_if_available},
      {"tiff_layers_photoshop_byte_order_pairs_match_if_available",
       tiff_layers_photoshop_byte_order_pairs_match_if_available},
  };
}
