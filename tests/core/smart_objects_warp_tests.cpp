#include <string_view>
#include "core/adjustment_layer.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/pixel_tools.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "unicode_path_names.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include "core_test_support.hpp"
#include "psd_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::find_layer_named;
using patchy::test::psd_first_layer_extra_data;
using patchy::test::psd_layer_block_payload;
using patchy::test::require_layer_named;
using patchy::test::solid_rgb;
using patchy::test::solid_rgba;

void psd_global_link_blocks_round_trip_with_smart_object_layers() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Smart", solid_rgb(4, 2, 10, 20, 30));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"PlLd", {1, 2, 3, 4}});
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", {9, 9, 9, 9, 9, 9, 9, 9}});
  document.metadata().unknown_psd_resources.push_back(patchy::UnknownPsdBlock{"cinf", {1, 2, 3}});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  // With the global link data preserved, the smart object references stay valid
  // and must be written too.
  const auto extra = psd_first_layer_extra_data(bytes);
  CHECK(psd_layer_block_payload(extra, "PlLd").has_value());
  CHECK(psd_layer_block_payload(extra, "SoLd").has_value());

  const auto read = patchy::psd::DocumentIo::read(bytes);
  // 'lnk*' globals now parse into the smart-object store; this synthetic payload is
  // not a valid element list, so it must be preserved as an OPAQUE store block.
  const auto& globals = read.metadata().unknown_psd_resources;
  CHECK(globals.size() == 1);
  CHECK(globals[0].key == "cinf");  // odd-sized payload exercises 4-byte padding
  CHECK(globals[0].payload == (std::vector<std::uint8_t>{1, 2, 3}));
  const auto& store = read.metadata().smart_objects;
  CHECK(store.blocks.size() == 1);
  CHECK(store.blocks[0].key == "lnk2");
  CHECK(store.blocks[0].opaque);
  CHECK(store.blocks[0].original_payload != nullptr);
  CHECK(*store.blocks[0].original_payload == (std::vector<std::uint8_t>{9, 9, 9, 9, 9, 9, 9, 9}));
  CHECK(read.layers().size() == 1);
  const auto& read_blocks = read.layers().front().unknown_psd_blocks();
  CHECK(std::any_of(read_blocks.begin(), read_blocks.end(),
                    [](const patchy::UnknownPsdBlock& block) { return block.key == "PlLd"; }));

  // The opaque block re-emits verbatim (still 'lnk2' + the same payload) on resave.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto reread = patchy::psd::DocumentIo::read(resaved);
  CHECK(reread.metadata().smart_objects.blocks.size() == 1);
  CHECK(*reread.metadata().smart_objects.blocks[0].original_payload ==
        (std::vector<std::uint8_t>{9, 9, 9, 9, 9, 9, 9, 9}));
}

void psd_dangling_smart_object_blocks_are_stripped() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Smart", solid_rgb(4, 2, 10, 20, 30));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"PlLd", {1, 2, 3, 4}});
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  layer.unknown_psd_blocks().push_back(
      patchy::UnknownPsdBlock{"fxrp", std::vector<std::uint8_t>(16, 0)});

  // Without document-global 'lnk2' data the smart object references would dangle,
  // producing a file Photoshop can open but not save ("disk error (-1)").
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto extra = psd_first_layer_extra_data(bytes);
  CHECK(!psd_layer_block_payload(extra, "PlLd").has_value());
  CHECK(!psd_layer_block_payload(extra, "SoLd").has_value());
  CHECK(psd_layer_block_payload(extra, "fxrp").has_value());
}

void psd_smart_object_sources_survive_resave_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("eon_spider_original.psd");
  patchy::test::skip_unless_exists(path);
  const auto has_lnk2 = [](const patchy::Document& document) {
    const auto& store = document.metadata().smart_objects;
    return std::any_of(store.blocks.begin(), store.blocks.end(),
                       [](const patchy::SmartObjectLinkBlock& block) {
                         return block.key == "lnk2" && (!block.sources.empty() || block.opaque);
                       });
  };
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(has_lnk2(document));
  const auto resaved = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(has_lnk2(resaved));
}

void collect_smart_object_placements(
    const std::vector<patchy::Layer>& layers,
    std::vector<std::pair<std::string, patchy::SmartObjectPlacement>>& out) {
  for (const auto& layer : layers) {
    collect_smart_object_placements(layer.children(), out);
    if (!patchy::layer_is_smart_object(layer)) {
      continue;
    }
    if (const auto placement = patchy::smart_object_placement_from_layer(layer); placement.has_value()) {
      out.emplace_back(layer.name(), *placement);
    }
  }
}

// The reported case: this document's embedded smart objects (one of them perspective
// placed, so it carries a stashed nonAffineTransform) used to make Image Size refuse
// outright. Photoshop resizes it, and the scaled quads have to survive the resave or
// reopening puts the placed content back at its original rectangle.
void psd_smart_object_document_resize_scales_placements_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("eon_spider_original.psd");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto old_width = document.width();
  const auto old_height = document.height();
  CHECK(old_width > 1 && old_height > 1);
  std::vector<std::pair<std::string, patchy::SmartObjectPlacement>> before;
  collect_smart_object_placements(std::as_const(document).layers(), before);
  CHECK(!before.empty());

  const auto new_width = old_width / 2;
  const auto new_height = old_height / 2;
  const double sx = static_cast<double>(new_width) / static_cast<double>(old_width);
  const double sy = static_cast<double>(new_height) / static_cast<double>(old_height);
  patchy::resize_image_and_layers(document, new_width, new_height);
  CHECK(document.width() == new_width && document.height() == new_height);

  std::vector<std::pair<std::string, patchy::SmartObjectPlacement>> after;
  collect_smart_object_placements(std::as_const(document).layers(), after);
  CHECK(after.size() == before.size());
  for (std::size_t i = 0; i < after.size() && i < before.size(); ++i) {
    CHECK(after[i].first == before[i].first);
    for (std::size_t corner = 0; corner < 8U; corner += 2U) {
      CHECK(std::abs(after[i].second.transform[corner] - before[i].second.transform[corner] * sx) < 1e-9);
      CHECK(std::abs(after[i].second.transform[corner + 1U] -
                     before[i].second.transform[corner + 1U] * sy) < 1e-9);
    }
    // The placed CONTENT is untouched; the layer stays non-destructive.
    CHECK(after[i].second.uuid == before[i].second.uuid);
    CHECK(after[i].second.width == before[i].second.width);
    CHECK(after[i].second.height == before[i].second.height);
    CHECK(after[i].second.non_affine_transform.has_value() ==
          before[i].second.non_affine_transform.has_value());
  }

  const auto resaved = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  std::vector<std::pair<std::string, patchy::SmartObjectPlacement>> saved;
  collect_smart_object_placements(std::as_const(resaved).layers(), saved);
  CHECK(saved.size() == after.size());
  for (std::size_t i = 0; i < saved.size() && i < after.size(); ++i) {
    for (std::size_t corner = 0; corner < 8U; ++corner) {
      CHECK(std::abs(saved[i].second.transform[corner] - after[i].second.transform[corner]) < 1e-6);
    }
  }
}

// The first perspective-placed smart object (nonAffineTransform differing from
// Trnf). Since GitHub issue 225 these import editable (no "non_affine" lock).
patchy::Layer* first_non_affine_locked_layer(std::vector<patchy::Layer>& layers) {
  for (auto& layer : layers) {
    if (layer.kind() == patchy::LayerKind::Group) {
      if (auto* found = first_non_affine_locked_layer(layer.children()); found != nullptr) {
        return found;
      }
      continue;
    }
    if (const auto placement = patchy::smart_object_placement_from_layer(layer);
        placement.has_value() && placement->non_affine_transform.has_value()) {
      return &layer;
    }
  }
  return nullptr;
}

// A perspective-placed (preview-locked "non_affine") smart object's stored quads
// survive an affine transform round-trip: Free Transform maps Trnf AND the
// stashed nonAffineTransform per corner, store_smart_object_placement carries
// both, and the SoLd regeneration writes the mapped non-affine quad directly
// instead of the translation-only additive fallback.
void psd_smart_object_non_affine_quad_maps_through_affine_transform_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("pinball_retronight_poster_a3.psd");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::psd::DocumentIo::read_file(path);
  auto* locked = first_non_affine_locked_layer(document.layers());
  CHECK(locked != nullptr);
  // Perspective placements render through their quad now, so they import editable.
  CHECK(patchy::smart_object_lock_reason(*locked).empty());
  const auto layer_name = locked->name();
  const auto placement = patchy::smart_object_placement_from_layer(*locked);
  CHECK(placement.has_value());
  CHECK(placement->non_affine_transform.has_value());

  // Scale 1.5x about (100, 200): the per-corner mapping Free Transform applies.
  const auto map_x = [](double value) { return 100.0 + (value - 100.0) * 1.5; };
  const auto map_y = [](double value) { return 200.0 + (value - 200.0) * 1.5; };
  auto updated = *placement;
  auto mapped_non_affine = *placement->non_affine_transform;
  for (std::size_t i = 0; i < 8U; i += 2U) {
    updated.transform[i] = map_x(updated.transform[i]);
    updated.transform[i + 1U] = map_y(updated.transform[i + 1U]);
    mapped_non_affine[i] = map_x(mapped_non_affine[i]);
    mapped_non_affine[i + 1U] = map_y(mapped_non_affine[i + 1U]);
  }
  updated.non_affine_transform = mapped_non_affine;
  patchy::store_smart_object_placement(*locked, updated);
  patchy::mark_layer_smart_object_block_dirty(*locked);

  auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reread_layer = first_non_affine_locked_layer(reread.layers());
  CHECK(reread_layer != nullptr);
  CHECK(reread_layer->name() == layer_name);
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  CHECK(reread_placement->non_affine_transform.has_value());
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs(reread_placement->transform[i] - updated.transform[i]) < 1e-6);
    CHECK(std::abs((*reread_placement->non_affine_transform)[i] - mapped_non_affine[i]) < 1e-6);
  }
}

bool bytes_contain_sequence(std::span<const std::uint8_t> haystack, std::string_view needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

void psb_layered_round_trip_preserves_layers_and_blocks() {
  patchy::Document document(6, 4, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base", solid_rgb(6, 4, 200, 30, 40));
  patchy::Layer top(document.allocate_layer_id(), "Top", solid_rgba(3, 2, 10, 20, 30, 128));
  top.set_bounds(patchy::Rect{1, 1, 3, 2});
  top.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"fxrp", std::vector<std::uint8_t>(16, 7)});
  document.add_layer(std::move(top));
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", {9, 9, 9, 9, 9, 9, 9, 9}});
  document.metadata().unknown_psd_resources.push_back(patchy::UnknownPsdBlock{"cinf", {1, 2, 3}});

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  CHECK(bytes.size() > 6 && bytes[4] == 0 && bytes[5] == 2);  // header version 2 = PSB
  // 'lnk2' and 'cinf' are in the PSB 8-byte-length key set (cinf empirically: Photoshop
  // 2026 requires it wide in PSBs), so they carry the 8B64 signature + u64 length; keys
  // outside the set keep the 8BIM + u32 form ('fxrp' on the layer covers that path).
  CHECK(bytes_contain_sequence(bytes, "8B64lnk2"));
  CHECK(!bytes_contain_sequence(bytes, "8BIMlnk2"));
  CHECK(bytes_contain_sequence(bytes, "8B64cinf"));
  CHECK(bytes_contain_sequence(bytes, "8BIMfxrp"));

  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.metadata().values.at("psd.version") == "PSB");
  CHECK(read.layers().size() == 2);
  const auto* base = find_layer_named(read.layers(), "Base");
  const auto* top_read = find_layer_named(read.layers(), "Top");
  CHECK(base != nullptr && top_read != nullptr);
  const auto* base_px = base->pixels().pixel(3, 2);
  CHECK(base_px[0] == 200 && base_px[1] == 30 && base_px[2] == 40);
  CHECK(top_read->bounds().x == 1 && top_read->bounds().y == 1 && top_read->bounds().width == 3 &&
        top_read->bounds().height == 2);
  const auto* top_px = top_read->pixels().pixel(0, 0);
  CHECK(top_px[0] == 10 && top_px[1] == 20 && top_px[2] == 30 && top_px[3] == 128);
  const auto& top_blocks = top_read->unknown_psd_blocks();
  CHECK(std::any_of(top_blocks.begin(), top_blocks.end(), [](const patchy::UnknownPsdBlock& block) {
    return block.key == "fxrp" && block.payload == std::vector<std::uint8_t>(16, 7);
  }));
  const auto& globals = read.metadata().unknown_psd_resources;
  CHECK(globals.size() == 1);
  CHECK(globals[0].key == "cinf");
  CHECK(globals[0].payload == (std::vector<std::uint8_t>{1, 2, 3}));
  CHECK(globals[0].long_length);
  const auto& store = read.metadata().smart_objects;
  CHECK(store.blocks.size() == 1);
  CHECK(store.blocks[0].key == "lnk2");
  CHECK(store.blocks[0].long_length);  // signature-derived: 8B64 blocks re-emit wide
  CHECK(store.blocks[0].opaque);
  CHECK(*store.blocks[0].original_payload == (std::vector<std::uint8_t>{9, 9, 9, 9, 9, 9, 9, 9}));
}

void psb_flat_round_trip_reads_composite() {
  // A flat PSB exercises the merged-image path, whose RLE row byte counts widen
  // to u32 in PSB (a 64px-wide solid row compresses, so RLE wins).
  patchy::Document document(64, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_rgb(64, 8, 12, 200, 99));
  const auto bytes = patchy::psd::DocumentIo::write_flat_rgb8(document, patchy::psd::WriteOptions{true});
  CHECK(bytes.size() > 6 && bytes[5] == 2);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  CHECK(read.metadata().values.at("psd.version") == "PSB");
  CHECK(read.width() == 64 && read.height() == 8);
  CHECK(read.layers().size() == 1);
  const auto* px = read.layers().front().pixels().pixel(63, 7);
  CHECK(px[0] == 12 && px[1] == 200 && px[2] == 99);
}

void psb_photoshop_fixture_round_trips() {
  // Photoshop 2026-authored PSB (COM script; see docs/file-formats.md). Pins reading a real PS PSB —
  // including the 8B64-signature global blocks ('cinf' carries an 8-byte length there).
  const auto document =
      patchy::psd::DocumentIo::read_file(patchy::test::committed_psd_fixture_path("photoshop-basic.psb"));
  CHECK(document.metadata().values.at("psd.version") == "PSB");
  CHECK(document.width() == 40 && document.height() == 30);
  CHECK(document.layers().size() == 2);
  const auto* red = find_layer_named(document.layers(), "Red");
  CHECK(red != nullptr);
  CHECK(red->bounds().x == 4 && red->bounds().y == 4 && red->bounds().width == 16 && red->bounds().height == 12);
  const auto* px = red->pixels().pixel(2, 2);
  CHECK(px[0] == 210 && px[1] == 40 && px[2] == 50);
  const auto& globals = document.metadata().unknown_psd_resources;
  const auto cinf = std::find_if(globals.begin(), globals.end(),
                                 [](const patchy::UnknownPsdBlock& block) { return block.key == "cinf"; });
  CHECK(cinf != globals.end());
  CHECK(cinf->long_length);
  CHECK(cinf->payload.size() == 413);

  // The resave must keep Photoshop's 8B64 form for those blocks.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true});
  CHECK(bytes_contain_sequence(resaved, "8B64cinf"));
  CHECK(!bytes_contain_sequence(resaved, "8BIMcinf"));
}

// Parses a layer's 'SoLd' payload and re-serializes it through the generic descriptor
// writer; the result must be byte-identical to Photoshop's original (trailing bytes
// after the descriptor are 4-alignment padding and must be zero).
void check_sold_descriptor_round_trip(const patchy::Layer& layer) {
  const auto& blocks = layer.unknown_psd_blocks();
  const auto sold = std::find_if(blocks.begin(), blocks.end(),
                                 [](const patchy::UnknownPsdBlock& block) { return block.key == "SoLd"; });
  CHECK(sold != blocks.end());
  patchy::psd::BigEndianReader reader(sold->payload);
  CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) == "soLD");
  const auto version = reader.read_u32();
  const auto descriptor_version = reader.read_u32();
  CHECK(version == 4);
  CHECK(descriptor_version == 16);
  const auto descriptor = patchy::psd::read_descriptor(reader);
  const auto consumed = reader.position();

  patchy::psd::BigEndianWriter writer;
  for (const char ch : {'s', 'o', 'L', 'D'}) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
  writer.write_u32(version);
  writer.write_u32(descriptor_version);
  patchy::psd::write_descriptor(writer, descriptor);
  const auto& rewritten = writer.bytes();

  std::size_t first_mismatch = std::string::npos;
  const auto compare_count = std::min(rewritten.size(), consumed);
  for (std::size_t i = 0; i < compare_count; ++i) {
    if (rewritten[i] != sold->payload[i]) {
      first_mismatch = i;
      break;
    }
  }
  if (first_mismatch != std::string::npos || rewritten.size() != consumed) {
    std::cout << "descriptor rewrite diverges: original consumed " << consumed << " bytes, rewrote "
              << rewritten.size() << ", first mismatch at "
              << (first_mismatch == std::string::npos ? compare_count : first_mismatch) << "\n";
  }
  CHECK(rewritten.size() == consumed);
  CHECK(first_mismatch == std::string::npos);
  for (std::size_t i = consumed; i < sold->payload.size(); ++i) {
    CHECK(sold->payload[i] == 0);
  }
}

void psd_smart_object_fixture_parses_placement_and_source() {
  std::vector<std::string> notices;
  patchy::psd::ReadOptions options;
  options.notices = &notices;
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"), options);
  const auto* layer = find_layer_named(document.layers(), "small");
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*layer));
  CHECK(patchy::smart_object_lock_reason(*layer).empty());
  const auto placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(placement.has_value());
  // Pinned from the Photoshop 2026 capture: a 32x24 png placed 1:1 centered in 96x96.
  CHECK(placement->transform[0] == 32.0 && placement->transform[1] == 36.0);
  CHECK(placement->transform[4] == 64.0 && placement->transform[5] == 60.0);
  CHECK(placement->width == 32.0 && placement->height == 24.0);
  const auto* source = document.metadata().smart_objects.find(placement->uuid);
  CHECK(source != nullptr);
  CHECK(source->kind == patchy::SmartObjectSourceKind::Embedded);
  CHECK(source->filetype == "png ");
  CHECK(source->filename == "small.png");
  CHECK(source->file_bytes != nullptr);
  CHECK(source->file_bytes->size() == 8012);  // the original png, byte-for-byte
  CHECK(source->file_bytes->size() >= 8 && (*source->file_bytes)[1] == 'P' && (*source->file_bytes)[2] == 'N');
  CHECK(std::any_of(notices.begin(), notices.end(), [](const std::string& notice) {
    return notice.find("smart object") != std::string::npos;
  }));
}

void psd_smart_object_clean_resave_preserves_blocks_byte_identically() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  const auto resaved = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto sold_payload = [](const patchy::Document& doc) -> std::vector<std::uint8_t> {
    const auto* layer = find_layer_named(doc.layers(), "small");
    CHECK(layer != nullptr);
    for (const auto& block : layer->unknown_psd_blocks()) {
      if (block.key == "SoLd") {
        return block.payload;
      }
    }
    return {};
  };
  CHECK(!sold_payload(document).empty());
  CHECK(sold_payload(document) == sold_payload(resaved));  // untouched layers never regenerate
  const auto* original_source = document.metadata().smart_objects.find(
      patchy::smart_object_placement_from_layer(*find_layer_named(document.layers(), "small"))->uuid);
  const auto* resaved_source = resaved.metadata().smart_objects.find(
      patchy::smart_object_placement_from_layer(*find_layer_named(resaved.layers(), "small"))->uuid);
  CHECK(original_source != nullptr && resaved_source != nullptr);
  CHECK(*original_source->file_bytes == *resaved_source->file_bytes);
  CHECK(*original_source->original_element_bytes == *resaved_source->original_element_bytes);
}

void psd_smart_object_move_regenerates_placed_blocks() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  auto* layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "small"));
  CHECK(layer != nullptr);
  const auto original_placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(original_placement.has_value());

  patchy::translate_moved_layer_metadata(*layer, 5, 3, document.width(), document.height());
  auto bounds = layer->bounds();
  bounds.x += 5;
  bounds.y += 3;
  layer->set_bounds(bounds);
  CHECK(patchy::layer_smart_object_block_dirty(*layer));

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reread_layer = find_layer_named(reread.layers(), "small");
  CHECK(reread_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*reread_layer));
  CHECK(patchy::smart_object_lock_reason(*reread_layer).empty());
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  for (std::size_t i = 0; i < 8U; i += 2) {
    CHECK(reread_placement->transform[i] == original_placement->transform[i] + 5.0);
    CHECK(reread_placement->transform[i + 1] == original_placement->transform[i + 1] + 3.0);
  }
  // Patch-in-place: unmodeled descriptor keys (the ClMg OCIO conversion object) survive
  // regeneration, and the PlLd twin block was patched alongside SoLd.
  for (const auto& block : reread_layer->unknown_psd_blocks()) {
    if (block.key == "SoLd") {
      patchy::psd::BigEndianReader reader(block.payload);
      (void)patchy::psd::read_signature(reader);
      (void)reader.read_u32();
      (void)reader.read_u32();
      const auto descriptor = patchy::psd::read_descriptor(reader);
      CHECK(patchy::psd::descriptor_value(descriptor, "ClMg") != nullptr);
      CHECK(patchy::psd::descriptor_value(descriptor, "Crop") != nullptr);
    }
    if (block.key == "PlLd") {
      patchy::psd::BigEndianReader reader(block.payload);
      (void)patchy::psd::read_signature(reader);
      (void)reader.read_u32();
      const auto uuid_length = reader.read_u8();
      reader.skip(uuid_length);
      reader.skip(16);  // page, total pages, anti-alias, type
      CHECK(patchy::psd::read_f64(reader) == original_placement->transform[0] + 5.0);
      CHECK(patchy::psd::read_f64(reader) == original_placement->transform[1] + 3.0);
    }
  }
}

void smart_object_rescaled_placement_matches_photoshop_replace_rule() {
  // Pinned to the E5 COM captures (see docs/smart-objects.md): the content-inch map and the quad
  // center are preserved and applied to the new content's pixel size and density.
  patchy::SmartObjectPlacement placement;
  placement.uuid = "old";
  placement.transform = {80.0, 85.0, 120.0, 85.0, 120.0, 115.0, 80.0, 115.0};
  placement.width = 40.0;
  placement.height = 30.0;
  placement.resolution = 72.0;

  const auto grown = patchy::rescaled_smart_object_placement(placement, 80.0, 60.0, 72.0);
  CHECK((grown.transform == std::array<double, 8>{60.0, 70.0, 140.0, 70.0, 140.0, 130.0, 60.0, 130.0}));
  CHECK(grown.width == 80.0 && grown.height == 60.0 && grown.resolution == 72.0);

  // A 300 dpi replacement shrinks by 72/300 (physical size preserved; Photoshop
  // additionally rounds the corners to whole pixels, Patchy keeps exact doubles).
  const auto dense = patchy::rescaled_smart_object_placement(placement, 80.0, 60.0, 300.0);
  CHECK(std::abs(dense.transform[0] - 90.4) < 1e-9);
  CHECK(std::abs(dense.transform[1] - 92.8) < 1e-9);
  CHECK(std::abs(dense.transform[4] - 109.6) < 1e-9);
  CHECK(std::abs(dense.transform[5] - 107.2) < 1e-9);
  CHECK(dense.resolution == 300.0);

  // A 50%-scaled placement keeps its scale factor about its own center (E5 case 3).
  patchy::SmartObjectPlacement scaled = placement;
  scaled.transform = {90.0, 93.0, 110.0, 93.0, 110.0, 108.0, 90.0, 108.0};
  const auto replaced = patchy::rescaled_smart_object_placement(scaled, 80.0, 60.0, 72.0);
  CHECK(std::abs(replaced.transform[0] - 80.0) < 1e-9);
  CHECK(std::abs(replaced.transform[1] - 85.5) < 1e-9);
  CHECK(std::abs(replaced.transform[4] - 120.0) < 1e-9);
  CHECK(std::abs(replaced.transform[5] - 115.5) < 1e-9);
}

void smart_object_placements_follow_document_geometry() {
  // Photoshop keeps placed content non-destructive through Image Size, Canvas Size,
  // Crop, and canvas Rotate rather than demanding a rasterize first. Trnf is in
  // DOCUMENT coordinates, so a quad left behind by a document-space remap puts the
  // content back at its pre-operation rectangle on the next re-render or save.
  const auto make_document = [] {
    patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
    patchy::Layer placed(document.allocate_layer_id(), "Placed",
                         patchy::PixelBuffer(40, 30, patchy::PixelFormat::rgba8()));
    placed.set_bounds(patchy::Rect{10, 20, 40, 30});
    patchy::SmartObjectPlacement placement;
    placement.uuid = "source-uuid";
    placement.transform = {10.0, 20.0, 50.0, 20.0, 50.0, 50.0, 10.0, 50.0};
    // A stashed perspective quad has to map per corner alongside Trnf.
    placement.non_affine_transform = std::array<double, 8>{12.0, 20.0, 50.0, 22.0,
                                                           48.0, 50.0, 10.0, 48.0};
    placement.width = 40.0;
    placement.height = 30.0;
    placement.resolution = 72.0;
    patchy::set_layer_smart_object_metadata(placed, placement, "placed-uuid", "SoLd", "",
                                            patchy::kSmartObjectRasterStatusPhotoshop);
    document.add_layer(std::move(placed));
    return document;
  };
  const auto placement_of = [](const patchy::Document& document) {
    return patchy::smart_object_placement_from_layer(document.layers().front())
        .value_or(patchy::SmartObjectPlacement{});
  };
  const auto close_enough = [](const std::array<double, 8>& quad,
                               const std::array<double, 8>& expected) {
    for (std::size_t i = 0; i < quad.size(); ++i) {
      if (std::abs(quad[i] - expected[i]) > 1e-9) {
        return false;
      }
    }
    return true;
  };

  {
    auto document = make_document();
    patchy::resize_image_and_layers(document, 200, 160);
    const auto placement = placement_of(document);
    CHECK(close_enough(placement.transform,
                       {20.0, 40.0, 100.0, 40.0, 100.0, 100.0, 20.0, 100.0}));
    CHECK(placement.non_affine_transform.has_value());
    CHECK(close_enough(*placement.non_affine_transform,
                       {24.0, 40.0, 100.0, 44.0, 96.0, 100.0, 20.0, 96.0}));
    // The source size and density describe the CONTENT, which the document resize
    // never touched; only where the content lands moved.
    CHECK(placement.width == 40.0 && placement.height == 30.0);
    CHECK(placement.resolution == 72.0);
    const auto& layer = std::as_const(document).layers().front();
    CHECK(patchy::layer_smart_object_block_dirty(layer));
    const auto& metadata = layer.metadata();
    const auto status = metadata.find(patchy::kLayerMetadataSmartObjectRasterStatus);
    CHECK(status != metadata.end() &&
          status->second == patchy::kSmartObjectRasterStatusPatchy);
  }
  {
    auto document = make_document();
    patchy::resize_canvas_and_layers(document, 120, 100, patchy::CanvasAnchor::Center);
    CHECK(close_enough(placement_of(document).transform,
                       {20.0, 30.0, 60.0, 30.0, 60.0, 60.0, 20.0, 60.0}));
  }
  {
    auto document = make_document();
    CHECK(patchy::crop_document(document, patchy::Rect{10, 20, 40, 30}));
    CHECK(close_enough(placement_of(document).transform,
                       {0.0, 0.0, 40.0, 0.0, 40.0, 30.0, 0.0, 30.0}));
  }
  {
    // Clockwise maps (x, y) -> (H - y, x) with H the pre-rotation height.
    auto document = make_document();
    patchy::rotate_document_clockwise(document);
    CHECK(close_enough(placement_of(document).transform,
                       {60.0, 10.0, 60.0, 50.0, 30.0, 50.0, 30.0, 10.0}));
  }
  {
    // Counterclockwise maps (x, y) -> (y, W - x) with W the pre-rotation width.
    auto document = make_document();
    patchy::rotate_document_counterclockwise(document);
    CHECK(close_enough(placement_of(document).transform,
                       {20.0, 90.0, 20.0, 50.0, 50.0, 50.0, 50.0, 90.0}));
  }
}

void smart_object_store_remove_and_generated_uuid_shape() {
  patchy::SmartObjectStore store;
  const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3});
  store.add_embedded("aaa", "a.png", "png ", bytes);
  store.add_embedded("bbb", "b.png", "png ", bytes);
  CHECK(store.find("aaa") != nullptr);
  CHECK(store.remove("aaa"));
  CHECK(store.find("aaa") == nullptr);
  CHECK(store.find("bbb") != nullptr);
  CHECK(!store.remove("aaa"));

  const auto uuid = patchy::generate_smart_object_uuid();
  CHECK(uuid.size() == 36U);
  for (std::size_t i = 0; i < uuid.size(); ++i) {
    if (i == 8U || i == 13U || i == 18U || i == 23U) {
      CHECK(uuid[i] == '-');
    } else {
      const bool hex = (uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f');
      CHECK(hex);
    }
  }
  CHECK(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' ||
        uuid[19] == 'b');
  CHECK(uuid != patchy::generate_smart_object_uuid());
}

void psd_smart_object_committed_psb_contents_round_trip() {
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  auto* layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "small"));
  CHECK(layer != nullptr);
  const auto placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(placement.has_value());
  auto* source = document.metadata().smart_objects.find(placement->uuid);
  CHECK(source != nullptr);

  // Simulate an Edit Contents commit swapping the embedded png for PSB bytes (the
  // format Photoshop embeds for converted layers); the SoLd itself stays untouched.
  patchy::Document child(20, 10, patchy::PixelFormat::rgb8());
  child.add_pixel_layer("Contents", solid_rgb(20, 10, 10, 20, 30));
  const auto child_bytes = std::make_shared<const std::vector<std::uint8_t>>(
      patchy::psd::DocumentIo::write_layered_rgb8(child, patchy::psd::WriteOptions{true}));
  source->file_bytes = child_bytes;
  source->original_element_bytes = nullptr;
  source->dirty = true;

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reread_layer = find_layer_named(reread.layers(), "small");
  CHECK(reread_layer != nullptr);
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  const auto* reread_source = reread.metadata().smart_objects.find(reread_placement->uuid);
  CHECK(reread_source != nullptr && reread_source->file_bytes != nullptr);
  CHECK(*reread_source->file_bytes == *child_bytes);  // dirty element re-embedded byte-identically
  const auto reread_child = patchy::psd::DocumentIo::read(
      {reread_source->file_bytes->data(), reread_source->file_bytes->size()});
  CHECK(reread_child.width() == 20 && reread_child.height() == 10);  // still opens as a PSB
}

void smart_object_authored_sold_matches_photoshop_shape() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "11111111-2222-3333-4444-555555555555";
  placement.transform = {10.0, 20.0, 74.0, 20.0, 74.0, 68.0, 10.0, 68.0};
  placement.width = 64.0;
  placement.height = 48.0;
  placement.resolution = 72.0;
  const auto payload = patchy::psd::author_placed_layer_sold_payload(placement, "aaaa-bbbb");
  const auto parsed = patchy::psd::parse_placed_layer_block("SoLd", payload);
  CHECK(parsed.has_value());
  CHECK(parsed->placement.uuid == placement.uuid);
  CHECK(parsed->placement.transform == placement.transform);
  CHECK(parsed->placement.width == 64.0 && parsed->placement.height == 48.0);
  CHECK(parsed->placement.resolution == 72.0);
  CHECK(parsed->lock_reason.empty());
  CHECK(parsed->placed_uuid == "aaaa-bbbb");

  // The authored key order and id forms must mirror Photoshop's own placed SoLd
  // (E1 captures pinned the shape; the committed fixture carries a real one).
  const auto key_signature = [](std::span<const std::uint8_t> sold) {
    patchy::psd::BigEndianReader reader(sold);
    (void)patchy::psd::read_signature(reader);
    (void)reader.read_u32();
    (void)reader.read_u32();
    const auto descriptor = patchy::psd::read_descriptor(reader);
    std::string joined;
    for (const auto& entry : descriptor.key_order) {
      joined += entry.key;
      joined += entry.long_form ? "+" : "-";
      joined += '|';
    }
    return joined;
  };
  const auto fixture = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  const auto* layer = find_layer_named(fixture.layers(), "small");
  CHECK(layer != nullptr);
  std::vector<std::uint8_t> fixture_sold;
  for (const auto& block : layer->unknown_psd_blocks()) {
    if (block.key == "SoLd") {
      fixture_sold = block.payload;
    }
  }
  CHECK(!fixture_sold.empty());
  CHECK(key_signature(payload) == key_signature(fixture_sold));
}

void psd_unparsed_smart_object_locks_and_round_trips_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_before.psd");
  patchy::test::skip_unless_exists(path);
  // A real warped smart object: the ObAr mesh parses and the supported custom
  // envelope UNLOCKS the layer (lock empty, warp metadata present, Patchy
  // re-renders it); its blocks survive a clean resave byte-identically.
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "e5_a_40x30");
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*layer));
  CHECK(patchy::smart_object_lock_reason(*layer).empty());
  const auto warp = patchy::smart_object_warp_from_layer(*layer);
  CHECK(warp.has_value());
  CHECK(warp->style == "warpCustom");
  CHECK(warp->u_order == 4 && warp->v_order == 2);
  CHECK(warp->mesh_xs.size() == 8U && warp->mesh_ys.size() == 8U);
  const auto sold_payload = [](const patchy::Layer& target) -> std::vector<std::uint8_t> {
    for (const auto& block : target.unknown_psd_blocks()) {
      if (block.key == "SoLd") {
        return block.payload;
      }
    }
    return {};
  };
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* reread_layer = find_layer_named(reread.layers(), "e5_a_40x30");
  CHECK(reread_layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*reread_layer).empty());
  CHECK(!sold_payload(*layer).empty());
  CHECK(sold_payload(*layer) == sold_payload(*reread_layer));
}

void psb_linked_smart_objects_parse_lnke_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/10cm table tent.psb");
  patchy::test::skip_unless_exists(path);
  // PS 2023 writes 'lnk2' with '8BIM' + u64 in PSBs (width by KEY, not signature);
  // misreading it used to derail the walk and drop the 'lnkE' block holding the
  // linked-file (liFE) elements, orphaning every smart object in the file.
  const auto document = patchy::psd::DocumentIo::read_file(path);
  std::size_t external_sources = 0;
  bool saw_lnke = false;
  for (const auto& block : document.metadata().smart_objects.blocks) {
    if (block.key == "lnkE") {
      saw_lnke = true;
    }
    for (const auto& source : block.sources) {
      if (source.kind == patchy::SmartObjectSourceKind::ExternalFile) {
        ++external_sources;
      }
    }
  }
  CHECK(saw_lnke);
  CHECK(external_sources == 2U);  // Content.psb + Content B.psb
  std::size_t external_layers = 0;
  std::function<void(const std::vector<patchy::Layer>&)> walk = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (!layer.children().empty()) {
        walk(layer.children());
      }
      if (patchy::layer_is_smart_object(layer) && patchy::smart_object_lock_reason(layer) == "external") {
        ++external_layers;
      }
    }
  };
  walk(document.layers());
  CHECK(external_layers == 4U);  // recognized + preview-locked, not demoted to plain layers

  // A resave keeps the link data: re-read and count again.
  const auto reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{true}));
  std::size_t reread_external = 0;
  for (const auto& block : reread.metadata().smart_objects.blocks) {
    for (const auto& source : block.sources) {
      if (source.kind == patchy::SmartObjectSourceKind::ExternalFile) {
        ++reread_external;
      }
    }
  }
  CHECK(reread_external == 2U);
}

void warp_mesh_math_behaves() {
  // Identity mesh + homography == plain quad mapping at every parameter.
  const auto mesh = patchy::identity_warp_mesh(0.0, 0.0, 40.0, 30.0, 4, 4);
  const std::array<double, 8> quad{80.0, 85.0, 120.0, 85.0, 120.0, 115.0, 80.0, 115.0};
  const auto homography = patchy::homography_from_rect_to_quad(0.0, 0.0, 40.0, 30.0, quad);
  CHECK(homography.has_value());
  for (double v = 0.0; v <= 1.0; v += 0.25) {
    for (double u = 0.0; u <= 1.0; u += 0.25) {
      const auto surface = patchy::evaluate_warp_mesh(mesh, u, v);
      const auto mapped = patchy::apply_homography(*homography, surface[0], surface[1]);
      CHECK(std::abs(mapped[0] - (80.0 + 40.0 * u)) < 1e-9);
      CHECK(std::abs(mapped[1] - (85.0 + 30.0 * v)) < 1e-9);
    }
  }

  // Degree elevation preserves the surface exactly (4x2 style bakes lift to 4x4).
  auto low_order = patchy::identity_warp_mesh(0.0, 0.0, 40.0, 30.0, 4, 2);
  low_order.ys[1] = -6.0;  // bend the top edge
  low_order.ys[2] = -6.0;
  const auto elevated = patchy::elevate_warp_mesh_to_cubic(low_order);
  CHECK(elevated.u_order == 4 && elevated.v_order == 4);
  for (double v = 0.0; v <= 1.0; v += 0.2) {
    for (double u = 0.0; u <= 1.0; u += 0.2) {
      const auto a = patchy::evaluate_warp_mesh(low_order, u, v);
      const auto b = patchy::evaluate_warp_mesh(elevated, u, v);
      CHECK(std::abs(a[0] - b[0]) < 1e-9 && std::abs(a[1] - b[1]) < 1e-9);
    }
  }

  // The surface grid inverts: forward-map a point, find its cell, invert, and the
  // recovered source position matches. (The grid maps the mesh hull onto the quad,
  // matching how Photoshop stores warped placements.)
  const auto grid = patchy::build_warp_surface_grid(elevated, quad, 40.0, 30.0, 4.0, 128);
  CHECK(grid.has_value());
  CHECK(grid->columns >= 9 && grid->rows >= 9);
  bool inverted_any = false;
  for (int row = 0; row + 1 < grid->rows && !inverted_any; ++row) {
    const int column = grid->columns / 2;
    const auto i00 = static_cast<std::size_t>(row * grid->columns + column);
    const auto i10 = i00 + 1;
    const auto i01 = i00 + static_cast<std::size_t>(grid->columns);
    const auto i11 = i01 + 1;
    const double cx = (grid->doc_xs[i00] + grid->doc_xs[i10] + grid->doc_xs[i11] + grid->doc_xs[i01]) / 4.0;
    const double cy = (grid->doc_ys[i00] + grid->doc_ys[i10] + grid->doc_ys[i11] + grid->doc_ys[i01]) / 4.0;
    const auto st = patchy::invert_bilinear_cell(cx, cy, grid->doc_xs[i00], grid->doc_ys[i00],
                                                 grid->doc_xs[i10], grid->doc_ys[i10], grid->doc_xs[i11],
                                                 grid->doc_ys[i11], grid->doc_xs[i01], grid->doc_ys[i01]);
    if (st.has_value()) {
      CHECK(std::abs((*st)[0] - 0.5) < 0.05 && std::abs((*st)[1] - 0.5) < 0.05);
      inverted_any = true;
    }
  }
  CHECK(inverted_any);
}

void psd_warp_move_matches_photoshop_if_available() {
  const auto before_path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_before.psd");
  const auto after_path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_after.psd");
  patchy::test::skip_unless_exists(before_path);
  patchy::test::skip_unless_exists(after_path);
  // PS moved the warped smart object by (+21,+13) between the captures. Applying the
  // same translation in Patchy and regenerating must reproduce PS's own SoLd
  // byte-for-byte (quad translated, mesh and bounds untouched: mesh coordinates are
  // content-space).
  auto document = patchy::psd::DocumentIo::read_file(before_path);
  auto* layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "e5_a_40x30"));
  CHECK(layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*layer).empty());  // supported warp, re-renderable
  patchy::translate_moved_layer_metadata(*layer, 21, 13, document.width(), document.height());
  CHECK(patchy::layer_smart_object_block_dirty(*layer));

  const auto sold_payload = [](const patchy::Layer& target) -> std::vector<std::uint8_t> {
    for (const auto& block : target.unknown_psd_blocks()) {
      if (block.key == "SoLd") {
        return block.payload;
      }
    }
    return {};
  };
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto after = patchy::psd::DocumentIo::read_file(after_path);
  auto regenerated = sold_payload(*find_layer_named(reread.layers(), "e5_a_40x30"));
  const auto photoshop = sold_payload(*find_layer_named(after.layers(), "e5_a_40x30"));
  CHECK(!regenerated.empty());
  // Photoshop refreshes the per-layer 'placed' INSTANCE uuid on every save (E1), so
  // that one field can never byte-match; align it and require everything else to be
  // identical (the writer round-trip is separately pinned byte-exact).
  const auto parse_sold = [](std::span<const std::uint8_t> payload) {
    patchy::psd::BigEndianReader reader(payload);
    (void)patchy::psd::read_signature(reader);
    (void)reader.read_u32();
    (void)reader.read_u32();
    return patchy::psd::read_descriptor(reader);
  };
  const auto photoshop_descriptor = parse_sold(photoshop);
  auto aligned_descriptor = parse_sold(regenerated);
  const auto* photoshop_placed = patchy::psd::descriptor_value(photoshop_descriptor, "placed");
  CHECK(photoshop_placed != nullptr);
  auto* regenerated_placed =
      const_cast<patchy::psd::DescriptorValue*>(patchy::psd::descriptor_value(aligned_descriptor, "placed"));
  CHECK(regenerated_placed != nullptr);
  regenerated_placed->string_value = photoshop_placed->string_value;
  patchy::psd::BigEndianWriter writer;
  for (const char ch : {'s', 'o', 'L', 'D'}) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
  writer.write_u32(4);
  writer.write_u32(16);
  patchy::psd::write_descriptor(writer, aligned_descriptor);
  auto aligned = writer.bytes();
  while ((aligned.size() % 4U) != 0U) {
    aligned.push_back(0);
  }
  CHECK(aligned == photoshop);
}

void psd_descriptor_writer_round_trips_warp_sold_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_before.psd");
  patchy::test::skip_unless_exists(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto* layer = find_layer_named(document.layers(), "e5_a_40x30");
  CHECK(layer != nullptr);
  check_sold_descriptor_round_trip(*layer);  // ObAr/UnFl read -> write byte identity
}

void warp_style_meshes_match_photoshop_if_available() {
  // E9/E9b COM captures: Photoshop bakes each preset style to a warpCustom mesh;
  // Patchy's generate_style_warp_mesh must reproduce every control point.
  struct StyleCase {
    const char* file;
    const char* style;
    double value;
    const char* layer;
    double width;
    double height;
    bool vertical;
  };
  const StyleCase cases[] = {
      {"e9_arc_m100.psd", "warpArc", -100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arc_m50.psd", "warpArc", -50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arc_p25.psd", "warpArc", 25.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arc_p50.psd", "warpArc", 50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arc_p100.psd", "warpArc", 100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arc_p50_vrtc.psd", "warpArc", 50.0, "e5_a_40x30", 40.0, 30.0, true},
      {"e9_arch_m100.psd", "warpArch", -100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arch_m50.psd", "warpArch", -50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arch_p50.psd", "warpArch", 50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_arch_p100.psd", "warpArch", 100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_bulge_m100.psd", "warpBulge", -100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_bulge_m50.psd", "warpBulge", -50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_bulge_p50.psd", "warpBulge", 50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_bulge_p100.psd", "warpBulge", 100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_flag_m100.psd", "warpFlag", -100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_flag_m50.psd", "warpFlag", -50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_flag_p50.psd", "warpFlag", 50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_flag_p100.psd", "warpFlag", 100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_wave_m100.psd", "warpWave", -100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_wave_m50.psd", "warpWave", -50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_wave_p50.psd", "warpWave", 50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_wave_p100.psd", "warpWave", 100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_rise_m100.psd", "warpRise", -100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_rise_m50.psd", "warpRise", -50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_rise_p50.psd", "warpRise", 50.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9_rise_p100.psd", "warpRise", 100.0, "e5_a_40x30", 40.0, 30.0, false},
      {"e9b_arc_p50_60x20.psd", "warpArc", 50.0, "e9_src_60x20", 60.0, 20.0, false},
      {"e9b_arch_p50_60x20.psd", "warpArch", 50.0, "e9_src_60x20", 60.0, 20.0, false},
      {"e9b_bulge_p50_60x20.psd", "warpBulge", 50.0, "e9_src_60x20", 60.0, 20.0, false},
      {"e9b_flag_p50_60x20.psd", "warpFlag", 50.0, "e9_src_60x20", 60.0, 20.0, false},
      {"e9b_wave_p50_60x20.psd", "warpWave", 50.0, "e9_src_60x20", 60.0, 20.0, false},
      {"e9b_rise_p50_60x20.psd", "warpRise", 50.0, "e9_src_60x20", 60.0, 20.0, false},
  };
  int verified = 0;
  double max_delta = 0.0;
  for (const auto& style_case : cases) {
    const auto path =
        patchy::test::local_psd_fixture_path(std::string("ps2026_e9/") + style_case.file);
    if (!std::filesystem::exists(path)) {
      continue;
    }
    const auto document = patchy::psd::DocumentIo::read_file(path);
    const auto* layer = find_layer_named(document.layers(), style_case.layer);
    CHECK(layer != nullptr);
    CHECK(patchy::smart_object_lock_reason(*layer).empty());
    const auto warp = patchy::smart_object_warp_from_layer(*layer);
    CHECK(warp.has_value());
    CHECK(!warp->mesh_generated);  // baked captures carry a real mesh
    const auto generated = patchy::generate_style_warp_mesh(style_case.style, style_case.value,
                                                            style_case.vertical, style_case.width,
                                                            style_case.height);
    CHECK(generated.has_value());
    CHECK(generated->u_order == warp->u_order);
    CHECK(generated->v_order == warp->v_order);
    CHECK(generated->xs.size() == warp->mesh_xs.size());
    CHECK(generated->ys.size() == warp->mesh_ys.size());
    for (std::size_t i = 0; i < generated->xs.size(); ++i) {
      max_delta = std::max(max_delta, std::abs(generated->xs[i] - warp->mesh_xs[i]));
      max_delta = std::max(max_delta, std::abs(generated->ys[i] - warp->mesh_ys[i]));
    }
    ++verified;
  }
  if (verified == 0) {
    patchy::test::skip("no optional fixture present (ps2026_e9 style captures)");
  }
  std::cout << "  style meshes: " << verified << " captures, max control-point delta " << max_delta
            << '\n';
  // Photoshop's own float path lands ~2.3e-6 off the closed forms; a wrong
  // construction errs by tenths of a pixel, so 1e-4 separates the two cleanly.
  CHECK(max_delta < 1e-4);
  // Photoshop normalizes bend 0 to warpNone (e9_arc_p0 capture); the generator's
  // value-0 mesh is the identity, so rendering either representation matches.
  const auto identity = patchy::generate_style_warp_mesh("warpArc", 0.0, false, 40.0, 30.0);
  CHECK(identity.has_value());
  const auto expected_identity = patchy::identity_warp_mesh(0.0, 0.0, 40.0, 30.0, 4, 2);
  CHECK(identity->xs == expected_identity.xs && identity->ys == expected_identity.ys);
  const auto zero_path = patchy::test::local_psd_fixture_path("ps2026_e9/e9_arc_p0.psd");
  if (std::filesystem::exists(zero_path)) {
    const auto zero_document = patchy::psd::DocumentIo::read_file(zero_path);
    const auto* zero_layer = find_layer_named(zero_document.layers(), "e5_a_40x30");
    CHECK(zero_layer != nullptr);
    CHECK(patchy::smart_object_lock_reason(*zero_layer).empty());
    CHECK(!patchy::smart_object_warp_from_layer(*zero_layer).has_value());
  }
}

void psd_style_only_warp_unlocks_and_regenerates_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_e9/e9_arc_p50.psd");
  patchy::test::skip_unless_exists(path);
  // Synthesize the style-only SoLd shape (style + value, no customEnvelopeWarp) that
  // interactive Photoshop writes: Patchy must unlock it by baking the mesh itself,
  // render-identical to Photoshop's own bake, while the FILE stays style-only across
  // regenerating saves.
  auto document = patchy::psd::DocumentIo::read_file(path);
  auto* layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "e5_a_40x30"));
  CHECK(layer != nullptr);
  const auto baked = patchy::smart_object_warp_from_layer(*layer);
  CHECK(baked.has_value());
  bool rebuilt = false;
  for (auto& block : layer->unknown_psd_blocks()) {
    if (block.key != "SoLd") {
      continue;
    }
    patchy::psd::BigEndianReader reader(block.payload);
    (void)patchy::psd::read_signature(reader);
    const auto version = reader.read_u32();
    const auto descriptor_version = reader.read_u32();
    auto descriptor = patchy::psd::read_descriptor(reader);
    auto* warp_object =
        const_cast<patchy::psd::DescriptorObject*>(patchy::psd::descriptor_object(descriptor, "warp"));
    CHECK(warp_object != nullptr);
    if (auto* style = const_cast<patchy::psd::DescriptorValue*>(
            patchy::psd::descriptor_value(*warp_object, "warpStyle"));
        style != nullptr) {
      style->enum_value = "warpArc";
    }
    if (auto* value = const_cast<patchy::psd::DescriptorValue*>(
            patchy::psd::descriptor_value(*warp_object, "warpValue"));
        value != nullptr) {
      value->double_value = 50.0;
    }
    for (const char* order_key : {"uOrder", "vOrder"}) {
      if (auto* order = const_cast<patchy::psd::DescriptorValue*>(
              patchy::psd::descriptor_value(*warp_object, order_key));
          order != nullptr) {
        order->integer_value = 4;
      }
    }
    warp_object->key_order.erase(
        std::remove_if(warp_object->key_order.begin(), warp_object->key_order.end(),
                       [](const auto& entry) { return entry.key == "customEnvelopeWarp"; }),
        warp_object->key_order.end());
    warp_object->values.erase("customEnvelopeWarp");
    patchy::psd::BigEndianWriter writer;
    for (const char ch : {'s', 'o', 'L', 'D'}) {
      writer.write_u8(static_cast<std::uint8_t>(ch));
    }
    writer.write_u32(version);
    writer.write_u32(descriptor_version);
    patchy::psd::write_descriptor(writer, descriptor);
    auto payload = writer.bytes();
    while ((payload.size() % 4U) != 0U) {
      payload.push_back(0);
    }
    block.payload = std::move(payload);
    rebuilt = true;
  }
  CHECK(rebuilt);

  auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  auto* unlocked = const_cast<patchy::Layer*>(find_layer_named(reread.layers(), "e5_a_40x30"));
  CHECK(unlocked != nullptr);
  CHECK(patchy::smart_object_lock_reason(*unlocked).empty());
  const auto synthesized = patchy::smart_object_warp_from_layer(*unlocked);
  CHECK(synthesized.has_value());
  CHECK(synthesized->mesh_generated);
  CHECK(synthesized->style == "warpArc");
  CHECK(synthesized->value == 50.0);
  CHECK(synthesized->mesh_xs.size() == baked->mesh_xs.size());
  double max_delta = 0.0;
  for (std::size_t i = 0; i < synthesized->mesh_xs.size(); ++i) {
    max_delta = std::max(max_delta, std::abs(synthesized->mesh_xs[i] - baked->mesh_xs[i]));
    max_delta = std::max(max_delta, std::abs(synthesized->mesh_ys[i] - baked->mesh_ys[i]));
  }
  CHECK(max_delta < 1e-6);  // the synthesized bake IS Photoshop's bake

  // A move regenerates the SoLd: style/value/orders stay as the file had them and no
  // customEnvelopeWarp appears, but the quad translates.
  const auto placement_before = patchy::smart_object_placement_from_layer(*unlocked);
  CHECK(placement_before.has_value());
  patchy::translate_moved_layer_metadata(*unlocked, 7, 5, reread.width(), reread.height());
  CHECK(patchy::layer_smart_object_block_dirty(*unlocked));
  const auto final_document =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(reread));
  const auto* moved = find_layer_named(final_document.layers(), "e5_a_40x30");
  CHECK(moved != nullptr);
  CHECK(patchy::smart_object_lock_reason(*moved).empty());
  const auto moved_warp = patchy::smart_object_warp_from_layer(*moved);
  CHECK(moved_warp.has_value());
  CHECK(moved_warp->mesh_generated);
  CHECK(moved_warp->style == "warpArc");
  const auto placement_after = patchy::smart_object_placement_from_layer(*moved);
  CHECK(placement_after.has_value());
  CHECK(std::abs(placement_after->transform[0] - (placement_before->transform[0] + 7.0)) < 1e-6);
  CHECK(std::abs(placement_after->transform[1] - (placement_before->transform[1] + 5.0)) < 1e-6);
  for (const auto& block : moved->unknown_psd_blocks()) {
    if (block.key != "SoLd") {
      continue;
    }
    patchy::psd::BigEndianReader reader(block.payload);
    (void)patchy::psd::read_signature(reader);
    (void)reader.read_u32();
    (void)reader.read_u32();
    const auto descriptor = patchy::psd::read_descriptor(reader);
    const auto* warp_object = patchy::psd::descriptor_object(descriptor, "warp");
    CHECK(warp_object != nullptr);
    CHECK(patchy::psd::descriptor_object(*warp_object, "customEnvelopeWarp") == nullptr);
    const auto* style = patchy::psd::descriptor_value(*warp_object, "warpStyle");
    CHECK(style != nullptr && style->enum_value == "warpArc");
    const auto* u_order = patchy::psd::descriptor_value(*warp_object, "uOrder");
    CHECK(u_order != nullptr && u_order->integer_value == 4);
    const auto* v_order = patchy::psd::descriptor_value(*warp_object, "vOrder");
    CHECK(v_order != nullptr && v_order->integer_value == 4);  // regenerate never rewrote them
  }
}

void text_warp_serialization_round_trips() {
  patchy::TextWarp warp;
  warp.style = "warpSqueeze";
  warp.rotate = "Vrtc";
  warp.value = -70.0;
  warp.perspective = 25.0;
  warp.perspective_other = -10.0;
  warp.bounds_left = -0.5;
  warp.bounds_top = -20.59;
  warp.bounds_right = 41.25;
  warp.bounds_bottom = 7.79;
  warp.baseline = 41.19;
  const auto parsed = patchy::parse_text_warp(patchy::serialize_text_warp(warp));
  CHECK(parsed.has_value());
  CHECK(parsed->style == warp.style);
  CHECK(parsed->rotate == warp.rotate);
  CHECK(parsed->value == warp.value);
  CHECK(parsed->perspective == warp.perspective);
  CHECK(parsed->perspective_other == warp.perspective_other);
  CHECK(parsed->bounds_left == warp.bounds_left);
  CHECK(parsed->bounds_top == warp.bounds_top);
  CHECK(parsed->bounds_right == warp.bounds_right);
  CHECK(parsed->bounds_bottom == warp.bounds_bottom);
  CHECK(parsed->baseline == warp.baseline);
  CHECK(!patchy::text_warp_is_identity(*parsed));
  // Legacy strings without the trailing baseline parse with baseline 0 (keeps the
  // historical box-bottom anchoring on resave).
  const auto legacy = patchy::parse_text_warp("warpArc Hrzn 50 0 0 0 0 208 56");
  CHECK(legacy.has_value());
  CHECK(legacy->style == "warpArc");
  CHECK(legacy->bounds_bottom == 56.0);
  CHECK(legacy->baseline == 0.0);
  patchy::TextWarp identity;
  CHECK(patchy::text_warp_is_identity(identity));
  identity.style = "warpArc";  // style set but every value zero renders unwarped
  CHECK(patchy::text_warp_is_identity(identity));
  identity.value = 1.0;
  CHECK(!patchy::text_warp_is_identity(identity));
  // Distortion-only warps are active too (Photoshop renders them with bend 0).
  patchy::TextWarp distortion_only;
  distortion_only.style = "warpArc";
  distortion_only.perspective = 30.0;
  CHECK(!patchy::text_warp_is_identity(distortion_only));
  const auto mesh = patchy::generate_text_warp_mesh(warp);
  CHECK(mesh.has_value());
  CHECK(!patchy::generate_text_warp_mesh(identity).has_value());
}

void warp_style_meshes_match_photoshop_e9c_if_available() {
  // e9c/e9d COM captures (July 2026): the nine Warp Text styles beyond the E9 six,
  // plus the distortion probes that pin apply_warp_distortion's row/column scaling
  // (order AND edge-midpoint rule). Photoshop bakes each onto a smart object as a
  // warpCustom mesh; Patchy's constructions must reproduce every control point.
  struct StyleCase {
    const char* file;
    const char* style;
    double value;
    bool vertical;
    double perspective;
    double perspective_other;
  };
  const StyleCase cases[] = {
      {"ps2026_e9/e9c_arclower_m100.psd", "warpArcLower", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arclower_m50.psd", "warpArcLower", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arclower_p50.psd", "warpArcLower", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arclower_p100.psd", "warpArcLower", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arcupper_m100.psd", "warpArcUpper", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arcupper_m50.psd", "warpArcUpper", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arcupper_p50.psd", "warpArcUpper", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_arcupper_p100.psd", "warpArcUpper", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shelllower_m100.psd", "warpShellLower", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shelllower_m50.psd", "warpShellLower", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shelllower_p50.psd", "warpShellLower", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shelllower_p100.psd", "warpShellLower", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shellupper_m100.psd", "warpShellUpper", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shellupper_m50.psd", "warpShellUpper", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shellupper_p50.psd", "warpShellUpper", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_shellupper_p100.psd", "warpShellUpper", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fish_m100.psd", "warpFish", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fish_m50.psd", "warpFish", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fish_p50.psd", "warpFish", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fish_p100.psd", "warpFish", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fisheye_m100.psd", "warpFisheye", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fisheye_m50.psd", "warpFisheye", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fisheye_p50.psd", "warpFisheye", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fisheye_p100.psd", "warpFisheye", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_inflate_m100.psd", "warpInflate", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_inflate_m50.psd", "warpInflate", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_inflate_p50.psd", "warpInflate", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_inflate_p100.psd", "warpInflate", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_squeeze_m100.psd", "warpSqueeze", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_squeeze_m50.psd", "warpSqueeze", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_squeeze_p50.psd", "warpSqueeze", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_squeeze_p100.psd", "warpSqueeze", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_twist_m100.psd", "warpTwist", -100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_twist_m50.psd", "warpTwist", -50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_twist_p50.psd", "warpTwist", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_twist_p100.psd", "warpTwist", 100.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_arclower_p50_60x20.psd", "warpArcLower", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_arcupper_p50_60x20.psd", "warpArcUpper", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_shelllower_p50_60x20.psd", "warpShellLower", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_shellupper_p50_60x20.psd", "warpShellUpper", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_fish_p50_60x20.psd", "warpFish", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_fisheye_p50_60x20.psd", "warpFisheye", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_inflate_p50_60x20.psd", "warpInflate", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_squeeze_p50_60x20.psd", "warpSqueeze", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9d_twist_p50_60x20.psd", "warpTwist", 50.0, false, 0.0, 0.0},
      {"ps2026_e9/e9c_fish_p50_vrtc.psd", "warpFish", 50.0, true, 0.0, 0.0},
      // Twist is orientation-invariant: Photoshop's Vrtc bake equals the Hrzn one.
      {"ps2026_e9/e9c_twist_p50_vrtc.psd", "warpTwist", 50.0, true, 0.0, 0.0},
      {"ps2026_e9/e9c_arclower_p50_vrtc.psd", "warpArcLower", 50.0, true, 0.0, 0.0},
      {"ps2026_e9/e9c_shellupper_p50_vrtc.psd", "warpShellUpper", 50.0, true, 0.0, 0.0},
      {"ps2026_warptext/so_persp_arc_p0_h50.psd", "warpArc", 0.0, false, 50.0, 0.0},
      {"ps2026_warptext/so_persp_arc_p0_hm50.psd", "warpArc", 0.0, false, -50.0, 0.0},
      {"ps2026_warptext/so_persp_arc_p0_v50.psd", "warpArc", 0.0, false, 0.0, 50.0},
      {"ps2026_warptext/so_persp_arc_p50_h50.psd", "warpArc", 50.0, false, 50.0, 0.0},
      {"ps2026_warptext/so_persp_arc_p0_h50_v30.psd", "warpArc", 0.0, false, 50.0, 30.0},
      {"ps2026_warptext/so_persp_fisheye_p50_h50.psd", "warpFisheye", 50.0, false, 50.0, 0.0},
      {"ps2026_warptext/so_persp_wave_p50_v40.psd", "warpWave", 50.0, false, 0.0, 40.0},
      {"ps2026_warptext/so_persp_arc_p50_vrtc_h40.psd", "warpArc", 50.0, true, 40.0, 0.0},
      {"ps2026_warptext/so_persp_twist_p60_h30.psd", "warpTwist", 60.0, false, 30.0, 0.0},
  };
  int verified = 0;
  double max_delta = 0.0;
  for (const auto& style_case : cases) {
    const auto path = patchy::test::local_psd_fixture_path(style_case.file);
    if (!std::filesystem::exists(path)) {
      continue;
    }
    const auto document = patchy::psd::DocumentIo::read_file(path);
    const patchy::Layer* layer = nullptr;
    for (const auto& candidate : document.layers()) {
      if (patchy::layer_is_smart_object(candidate)) {
        layer = &candidate;
        break;
      }
    }
    CHECK(layer != nullptr);
    const auto warp = patchy::smart_object_warp_from_layer(*layer);
    CHECK(warp.has_value());
    CHECK(!warp->mesh_generated);
    auto generated = patchy::generate_style_warp_mesh(
        style_case.style, style_case.value, style_case.vertical,
        warp->bounds_right - warp->bounds_left, warp->bounds_bottom - warp->bounds_top);
    CHECK(generated.has_value());
    patchy::apply_warp_distortion(*generated, style_case.perspective, style_case.perspective_other);
    CHECK(generated->u_order == warp->u_order);
    CHECK(generated->v_order == warp->v_order);
    CHECK(generated->xs.size() == warp->mesh_xs.size());
    for (std::size_t i = 0; i < generated->xs.size(); ++i) {
      max_delta = std::max(max_delta,
                           std::abs(generated->xs[i] + warp->bounds_left - warp->mesh_xs[i]));
      max_delta = std::max(max_delta,
                           std::abs(generated->ys[i] + warp->bounds_top - warp->mesh_ys[i]));
    }
    ++verified;
  }
  if (verified == 0) {
    patchy::test::skip("no optional fixture present (e9c/warptext style captures)");
  }
  std::cout << "  e9c style meshes: " << verified << " captures, max control-point delta "
            << max_delta << '\n';
  CHECK(max_delta < 1e-4);
}

void psd_text_warp_round_trips_photoshop_fixture() {
  // Committed Photoshop 2026 fixture: two warped point-text layers ("Arc50" =
  // warpArc bend 50 horizontal; "Sqz" = warpSqueeze bend -70 vertical with 25/-10
  // distortions). Pins the TySh warp descriptor parse, the preserve-verbatim path,
  // and the regenerated descriptor + float32 tail once the text goes Patchy-owned.
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-warp-text.psd");
  auto document = patchy::psd::DocumentIo::read_file(path);
  // The fixture layers get mutated below to exercise the regeneration path.
  auto* arc_layer = const_cast<patchy::Layer*>(find_layer_named(document.layers(), "Arc50"));
  const auto* squeeze_layer = find_layer_named(document.layers(), "Sqz");
  CHECK(arc_layer != nullptr && squeeze_layer != nullptr);
  const auto arc_warp = patchy::text_warp_from_layer(*arc_layer);
  CHECK(arc_warp.has_value());
  CHECK(arc_warp->style == "warpArc");
  CHECK(arc_warp->rotate == "Hrzn");
  CHECK(std::abs(arc_warp->value - 50.0) < 1e-9);
  CHECK(arc_warp->perspective == 0.0 && arc_warp->perspective_other == 0.0);
  // The warp acts over the text 'bounds' layout box (COM captures, July 2026).
  CHECK(std::abs(arc_warp->bounds_top - -20.595703125) < 1e-9);
  CHECK(std::abs(arc_warp->bounds_bottom - 7.79296875) < 1e-9);
  const auto squeeze_warp = patchy::text_warp_from_layer(*squeeze_layer);
  CHECK(squeeze_warp.has_value());
  CHECK(squeeze_warp->style == "warpSqueeze");
  CHECK(squeeze_warp->rotate == "Vrtc");
  CHECK(std::abs(squeeze_warp->value - -70.0) < 1e-9);
  CHECK(std::abs(squeeze_warp->perspective - 25.0) < 1e-9);
  CHECK(std::abs(squeeze_warp->perspective_other - -10.0) < 1e-9);

  // Untouched import: the original TySh blob re-emits verbatim (warp included).
  const auto find_tysh = [](const patchy::Layer& layer) -> const patchy::UnknownPsdBlock* {
    for (const auto& block : layer.unknown_psd_blocks()) {
      if (block.key == "TySh" || block.key == "tySh") {
        return &block;
      }
    }
    return nullptr;
  };
  const auto* original_block = find_tysh(*arc_layer);
  CHECK(original_block != nullptr);
  const auto preserved_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto preserved = patchy::psd::DocumentIo::read(preserved_bytes);
  const auto* preserved_arc = find_layer_named(preserved.layers(), "Arc50");
  CHECK(preserved_arc != nullptr);
  const auto* preserved_block = find_tysh(*preserved_arc);
  CHECK(preserved_block != nullptr);
  CHECK(preserved_block->payload == original_block->payload);
  const auto preserved_warp = patchy::text_warp_from_layer(*preserved_arc);
  CHECK(preserved_warp.has_value());
  CHECK(preserved_warp->style == "warpArc");

  // A Patchy warp edit (raster goes patchy_raster) regenerates the TySh with the
  // new warp values and the float32 bounds tail.
  arc_layer->metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
  auto edited_warp = *arc_warp;
  edited_warp.style = "warpFlag";
  edited_warp.value = -30.0;
  edited_warp.perspective = 15.0;
  edited_warp.rotate = "Vrtc";
  arc_layer->metadata()[patchy::kLayerMetadataTextWarp] = patchy::serialize_text_warp(edited_warp);
  const auto regenerated_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto regenerated = patchy::psd::DocumentIo::read(regenerated_bytes);
  const auto* regenerated_arc = find_layer_named(regenerated.layers(), "Arc50");
  CHECK(regenerated_arc != nullptr);
  const auto* regenerated_block = find_tysh(*regenerated_arc);
  CHECK(regenerated_block != nullptr);
  CHECK(regenerated_block->payload != original_block->payload);
  const auto regenerated_warp = patchy::text_warp_from_layer(*regenerated_arc);
  CHECK(regenerated_warp.has_value());
  CHECK(regenerated_warp->style == "warpFlag");
  CHECK(regenerated_warp->rotate == "Vrtc");
  CHECK(std::abs(regenerated_warp->value - -30.0) < 1e-9);
  CHECK(std::abs(regenerated_warp->perspective - 15.0) < 1e-9);
  // The regenerated warp box is the metadata box (double precision in the
  // descriptor) and the tail carries it as four big-endian float32s.
  CHECK(std::abs(regenerated_warp->bounds_top - edited_warp.bounds_top) < 1e-9);
  CHECK(std::abs(regenerated_warp->bounds_bottom - edited_warp.bounds_bottom) < 1e-9);
  CHECK(regenerated_block->payload.size() >= 16U);
  const auto tail_offset = regenerated_block->payload.size() - 16U;
  const auto read_tail_f32 = [&](std::size_t index) {
    std::uint32_t bits = 0;
    for (int byte = 0; byte < 4; ++byte) {
      bits = (bits << 8U) | regenerated_block->payload[tail_offset + index * 4U + byte];
    }
    return std::bit_cast<float>(bits);
  };
  CHECK(std::abs(read_tail_f32(0) - static_cast<float>(edited_warp.bounds_left)) < 1e-3F);
  CHECK(std::abs(read_tail_f32(1) - static_cast<float>(edited_warp.bounds_top)) < 1e-3F);
  CHECK(std::abs(read_tail_f32(2) - static_cast<float>(edited_warp.bounds_right)) < 1e-3F);
  CHECK(std::abs(read_tail_f32(3) - static_cast<float>(edited_warp.bounds_bottom)) < 1e-3F);

  // Removing the warp writes a plain identity descriptor: no warp metadata on reopen.
  arc_layer->metadata().erase(patchy::kLayerMetadataTextWarp);
  const auto unwarped_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto unwarped = patchy::psd::DocumentIo::read(unwarped_bytes);
  const auto* unwarped_arc = find_layer_named(unwarped.layers(), "Arc50");
  CHECK(unwarped_arc != nullptr);
  CHECK(!patchy::text_warp_from_layer(*unwarped_arc).has_value());
}

void psb_life_trailer_fields_parse_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/10cm table tent.psb");
  patchy::test::skip_unless_exists(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const patchy::SmartObjectSource* content = nullptr;
  for (const auto& block : document.metadata().smart_objects.blocks) {
    for (const auto& source : block.sources) {
      if (source.filename == "Content.psb") {
        content = &source;
      }
    }
  }
  CHECK(content != nullptr);
  CHECK(content->kind == patchy::SmartObjectSourceKind::ExternalFile);
  // The ExternalFileLink descriptor + trailer, exactly as Photoshop 2023 stored them.
  CHECK(content->external_rel_path == "Content.psb");
  CHECK(content->external_original_path == "D:\\projects\\C2\\TableTents\\DrinkTable\\Content.psb");
  CHECK(content->external_full_path == "file:///D:/projects/C2/TableTents/DrinkTable/Content.psb");
  CHECK(content->external_link_desc_version == 2);
  CHECK(content->external_mod_year == 2023 && content->external_mod_month == 2 && content->external_mod_day == 2);
  CHECK(content->external_mod_hour == 10 && content->external_mod_minute == 32);
  CHECK(content->external_mod_seconds == 19.0);
  CHECK(content->external_file_size == 874996U);
  CHECK(content->child_doc_id.rfind("adobe:docid:photoshop:", 0) == 0);
}

void smart_object_external_element_round_trips_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("PSBtest/10cm table tent.psb");
  patchy::test::skip_unless_exists(path);
  // Marking an external element dirty re-serializes it through Patchy's liFE writer;
  // parsing that back must reproduce every modeled field (the writer mirrors PS's
  // pinned layout).
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::SmartObjectLinkBlock* link_block = nullptr;
  for (auto& block : document.metadata().smart_objects.blocks) {
    if (!block.sources.empty() && block.sources.front().kind == patchy::SmartObjectSourceKind::ExternalFile) {
      link_block = &block;
    }
  }
  CHECK(link_block != nullptr);
  const auto original = link_block->sources.front();
  link_block->sources.front().dirty = true;
  const auto payload = patchy::psd::serialize_linked_layer_block(*link_block);
  const auto reparsed = patchy::psd::parse_linked_layer_block(payload);
  CHECK(reparsed.has_value());
  CHECK(reparsed->size() == link_block->sources.size());
  const auto& round_tripped = reparsed->front();
  CHECK(round_tripped.kind == patchy::SmartObjectSourceKind::ExternalFile);
  CHECK(round_tripped.uuid == original.uuid);
  CHECK(round_tripped.filename == original.filename);
  CHECK(round_tripped.filetype == original.filetype);
  CHECK(round_tripped.external_full_path == original.external_full_path);
  CHECK(round_tripped.external_original_path == original.external_original_path);
  CHECK(round_tripped.external_rel_path == original.external_rel_path);
  CHECK(round_tripped.external_link_desc_version == original.external_link_desc_version);
  CHECK(round_tripped.external_mod_year == original.external_mod_year);
  CHECK(round_tripped.external_mod_month == original.external_mod_month);
  CHECK(round_tripped.external_mod_day == original.external_mod_day);
  CHECK(round_tripped.external_mod_hour == original.external_mod_hour);
  CHECK(round_tripped.external_mod_minute == original.external_mod_minute);
  CHECK(round_tripped.external_mod_seconds == original.external_mod_seconds);
  CHECK(round_tripped.external_file_size == original.external_file_size);
  CHECK(round_tripped.child_doc_id == original.child_doc_id);
  CHECK(round_tripped.asset_mod_time == original.asset_mod_time);
  CHECK(round_tripped.asset_lock_state == original.asset_lock_state);
}

// The placed-layer descriptor's keys in file order, each with its id form ('+' for a
// stringID, '-' for a charID), plus the warp bounds rectangle.
struct PlacedDescriptorShape {
  std::string keys;
  int type{0};
  std::array<double, 4> bounds{};  // left, top, right, bottom
};

PlacedDescriptorShape placed_descriptor_shape(std::span<const std::uint8_t> payload) {
  patchy::psd::BigEndianReader reader(payload);
  (void)patchy::psd::read_signature(reader);
  (void)reader.read_u32();
  (void)reader.read_u32();
  const auto descriptor = patchy::psd::read_descriptor(reader);
  PlacedDescriptorShape shape;
  for (const auto& entry : descriptor.key_order) {
    shape.keys += entry.key;
    shape.keys += entry.long_form ? "+" : "-";
    shape.keys += '|';
  }
  shape.type = static_cast<int>(patchy::psd::descriptor_number(descriptor, "Type", -1.0));
  const auto* warp = patchy::psd::descriptor_object(descriptor, "warp");
  const auto* bounds = warp != nullptr ? patchy::psd::descriptor_object(*warp, "bounds") : nullptr;
  if (bounds != nullptr) {
    shape.bounds = {patchy::psd::descriptor_number(*bounds, "Left"), patchy::psd::descriptor_number(*bounds, "Top "),
                    patchy::psd::descriptor_number(*bounds, "Rght"), patchy::psd::descriptor_number(*bounds, "Btom")};
  }
  return shape;
}

std::vector<std::uint8_t> placed_block_payload(const patchy::Layer& layer, std::string_view key) {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == key) {
      return block.payload;
    }
  }
  return {};
}

// Vector contents (Type 1: a placed SVG) follow Photoshop's vector shape: no
// compInfo, and warp bounds that hold the artwork's unscaled placement rectangle in
// document space, which no later move or scale rewrites (September 2026 captures).
void smart_object_authored_vector_sold_matches_photoshop_shape() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "11111111-2222-4333-8444-555555555555";
  placement.transform = {100.0, 100.0, 500.0, 100.0, 500.0, 500.0, 100.0, 500.0};
  placement.width = 1024.0;
  placement.height = 1024.0;
  placement.resolution = 72.0;
  placement.placed_type = 1;
  const std::array<double, 4> base_rect{200.0, 0.0, 1000.0, 800.0};
  const auto payload = patchy::psd::author_placed_layer_sold_payload(placement, "aaaa-bbbb", nullptr, &base_rect);
  const auto shape = placed_descriptor_shape(payload);
  CHECK(shape.keys ==
        "Idnt-|placed+|PgNm-|totalPages+|Crop-|frameStep+|duration+|frameCount+|Annt-|Type-|Trnf-|"
        "nonAffineTransform+|warp+|Sz  -|Rslt-|comp-|ClMg-|");
  CHECK(shape.type == 1);
  CHECK(shape.bounds == base_rect);
  const auto parsed = patchy::psd::parse_placed_layer_block("SoLE", payload);
  CHECK(parsed.has_value());
  CHECK(parsed->placement.placed_type == 1);
  CHECK(parsed->placement.transform == placement.transform);
  CHECK(parsed->lock_reason.empty());
  CHECK(!parsed->warp.has_value());

  // Without an explicit rectangle the quad's own bounding box stands in.
  const auto fallback = placed_descriptor_shape(patchy::psd::author_placed_layer_sold_payload(placement, "aaaa-bbbb"));
  CHECK((fallback.bounds == std::array<double, 4>{100.0, 100.0, 500.0, 500.0}));

  // A move or scale patches the quad and leaves the vector bounds alone.
  auto moved = placement;
  for (std::size_t i = 0; i < moved.transform.size(); ++i) {
    moved.transform[i] = moved.transform[i] * 0.5 + 30.0;
  }
  const auto regenerated = patchy::psd::regenerate_placed_layer_payload("SoLE", payload, moved);
  CHECK(regenerated.has_value());
  const auto moved_shape = placed_descriptor_shape(*regenerated);
  CHECK(moved_shape.bounds == base_rect);
  CHECK(moved_shape.type == 1);
  const auto reparsed = patchy::psd::parse_placed_layer_block("SoLE", *regenerated);
  CHECK(reparsed.has_value() && reparsed->placement.transform == moved.transform);

  // Raster contents keep the content rectangle, the compInfo object, and Type 2.
  auto raster = placement;
  raster.placed_type = 2;
  const auto raster_shape = placed_descriptor_shape(patchy::psd::author_placed_layer_sold_payload(raster, "aaaa-bbbb"));
  CHECK(raster_shape.type == 2);
  CHECK(raster_shape.keys.find("compInfo+|") != std::string::npos);
  CHECK((raster_shape.bounds == std::array<double, 4>{0.0, 0.0, 1024.0, 1024.0}));

  // Relink to File can swap the contents' kind: the regenerated block follows.
  const auto retyped = patchy::psd::regenerate_placed_layer_payload("SoLE", payload, raster);
  CHECK(retyped.has_value());
  CHECK(placed_descriptor_shape(*retyped).type == 2);
}

// Place Linked authors a 'lnkE' element and a 'SoLE' layer block from scratch. Both
// must come back from a save as written, a non-ASCII file name included, and a
// second save must keep them.
void psd_smart_object_authored_link_round_trips() {
  const auto name = patchy::test::utf8_string(u8"caf\u00E9 \u30ED\u30B4 #1.svg");
  patchy::SmartObjectSource link;
  link.kind = patchy::SmartObjectSourceKind::ExternalFile;
  link.uuid = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
  link.filename = name;
  link.filetype = "SVG ";
  link.creator = std::string(4, '\0');
  link.external_full_path = "file:///D:/art work/" + name;
  link.external_original_path = "D:\\art work\\" + name;
  link.external_rel_path = "../" + name;
  link.external_mod_year = 2026;
  link.external_mod_month = 9;
  link.external_mod_day = 30;
  link.external_mod_hour = 13;
  link.external_mod_minute = 3;
  link.external_mod_seconds = 5.0;
  link.external_file_size = 670U;

  patchy::SmartObjectPlacement placement;
  placement.uuid = link.uuid;
  placement.transform = {8.0, 4.0, 40.0, 4.0, 40.0, 36.0, 8.0, 36.0};
  placement.width = 1024.0;
  placement.height = 1024.0;
  placement.resolution = 72.0;
  placement.placed_type = 1;
  const std::array<double, 4> base_rect{8.0, 0.0, 56.0, 48.0};
  const std::string placed_uuid = "12345678-aaaa-4bbb-8ccc-dddddddddddd";

  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("base", solid_rgb(64, 48, 200, 200, 200));
  auto& stored = document.metadata().smart_objects.add_external(link);
  CHECK(stored.dirty && stored.kind == patchy::SmartObjectSourceKind::ExternalFile);
  CHECK(document.metadata().smart_objects.blocks.size() == 1U);
  CHECK(document.metadata().smart_objects.blocks.front().key == "lnkE");
  patchy::Layer logo(document.allocate_layer_id(), "logo", solid_rgb(32, 32, 250, 180, 30));
  logo.set_bounds(patchy::Rect{8, 4, 32, 32});
  patchy::set_layer_smart_object_metadata(logo, placement, placed_uuid, "SoLE", "external",
                                          patchy::kSmartObjectRasterStatusPatchy);
  logo.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{
      "SoLE", patchy::psd::author_placed_layer_sold_payload(placement, placed_uuid, nullptr, &base_rect)});
  document.add_layer(std::move(logo));

  const auto check_link = [&](const patchy::Document& read) {
    const auto* layer = find_layer_named(read.layers(), "logo");
    CHECK(layer != nullptr);
    CHECK(patchy::layer_is_smart_object(*layer));
    CHECK(patchy::smart_object_lock_reason(*layer) == "external");
    CHECK(patchy::smart_object_placed_uuid(*layer) == placed_uuid);
    CHECK(!placed_block_payload(*layer, "SoLE").empty());
    CHECK(placed_block_payload(*layer, "SoLd").empty());
    CHECK(placed_block_payload(*layer, "PlLd").empty());
    const auto read_placement = patchy::smart_object_placement_from_layer(*layer);
    CHECK(read_placement.has_value());
    CHECK(read_placement->transform == placement.transform);
    CHECK(read_placement->placed_type == 1);
    CHECK(read_placement->width == 1024.0 && read_placement->height == 1024.0);
    const auto& store = read.metadata().smart_objects;
    CHECK(store.blocks.size() == 1U);
    CHECK(store.blocks.front().key == "lnkE" && !store.blocks.front().opaque);
    const auto* source = store.find(link.uuid);
    CHECK(source != nullptr);
    CHECK(source->kind == patchy::SmartObjectSourceKind::ExternalFile);
    CHECK(source->file_bytes == nullptr);
    CHECK(source->filename == name);
    CHECK(source->filetype == "SVG ");
    CHECK(source->external_full_path == link.external_full_path);
    CHECK(source->external_original_path == link.external_original_path);
    CHECK(source->external_rel_path == link.external_rel_path);
    CHECK(source->external_link_desc_version == 2);
    CHECK(source->external_mod_year == 2026 && source->external_mod_month == 9 && source->external_mod_day == 30);
    CHECK(source->external_mod_hour == 13 && source->external_mod_minute == 3);
    CHECK(source->external_mod_seconds == 5.0);
    CHECK(source->external_file_size == 670U);
  };
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  check_link(reread);
  const auto* reread_layer = find_layer_named(reread.layers(), "logo");
  CHECK(placed_block_payload(*reread_layer, "SoLE") ==
        patchy::psd::author_placed_layer_sold_payload(placement, placed_uuid, nullptr, &base_rect));
  // Untouched, the parsed element and block re-emit verbatim.
  const auto again = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(reread));
  check_link(again);
  CHECK(placed_block_payload(*find_layer_named(again.layers(), "logo"), "SoLE") ==
        placed_block_payload(*reread_layer, "SoLE"));
  const auto& first_block = reread.metadata().smart_objects.blocks.front();
  const auto& second_block = again.metadata().smart_objects.blocks.front();
  CHECK(first_block.original_payload != nullptr && second_block.original_payload != nullptr);
  CHECK(*first_block.original_payload == *second_block.original_payload);

  // A raster link carries its own filetype and the layer-comp open descriptor; both
  // element shapes parse back through the shared reader.
  auto raster = link;
  raster.uuid = "bbbbbbbb-bbbb-4ccc-8ddd-eeeeeeeeeeee";
  raster.filename = "art.png";
  raster.filetype = "png ";
  patchy::SmartObjectLinkBlock block;
  block.key = "lnkE";
  block.sources = {link, raster};
  const auto parsed = patchy::psd::parse_linked_layer_block(patchy::psd::serialize_linked_layer_block(block));
  CHECK(parsed.has_value() && parsed->size() == 2U);
  CHECK((*parsed)[0].filetype == "SVG " && (*parsed)[0].filename == name);
  CHECK((*parsed)[1].filetype == "png " && (*parsed)[1].external_rel_path == link.external_rel_path);
}

// Photoshop 2026's own linked and embedded placements (scripts/dev/smart-objects/
// ps-capture-linked.ps1): the authored placed-layer block must equal Photoshop's
// byte for byte, for a linked SVG, an embedded SVG, and a linked PNG.
void psd_photoshop_linked_captures_match_authored_blocks_if_available() {
  struct Capture {
    const char* file;
    const char* key;
    bool vector;
  };
  int checked = 0;
  for (const auto& capture : {Capture{"ps2026_linked/linked_svg.psd", "SoLE", true},
                              Capture{"ps2026_linked/embedded_svg.psd", "SoLd", true},
                              Capture{"ps2026_linked/linked_png.psd", "SoLE", false},
                              Capture{"ps2026_linked/embedded_png.psd", "SoLd", false}}) {
    const auto path = patchy::test::local_psd_fixture_path(capture.file);
    if (!std::filesystem::exists(path)) {
      if (checked == 0) {
        patchy::test::skip("optional fixture missing: " + patchy::test::fixture_display_path(path));
      }
      std::cout << "  note: optional fixture missing: " << patchy::test::fixture_display_path(path) << '\n';
      return;
    }
    const auto document = patchy::psd::DocumentIo::read_file(path);
    const auto* layer = find_layer_named(document.layers(), "logo");
    CHECK(layer != nullptr);
    const auto payload = placed_block_payload(*layer, capture.key);
    CHECK(!payload.empty());
    const auto info = patchy::psd::parse_placed_layer_block(capture.key, payload);
    CHECK(info.has_value());
    const auto shape = placed_descriptor_shape(payload);
    CHECK(shape.type == (capture.vector ? 1 : 2));
    CHECK(info->placement.placed_type == shape.type);
    const auto authored = patchy::psd::author_placed_layer_sold_payload(
        info->placement, info->placed_uuid, nullptr, capture.vector ? &shape.bounds : nullptr);
    CHECK(authored == payload);
    // A linked layer reads back preview-locked as external; an embedded one editable.
    const bool linked = std::string_view(capture.key) == "SoLE";
    CHECK(patchy::smart_object_lock_reason(*layer) == (linked ? "external" : ""));
    ++checked;
  }
}

// What Photoshop 2026 writes into the link element itself: the SVG filetype, the
// paths (a file beside the document, one folder up, and a non-ASCII name stored
// without percent-encoding), whole-second stamps, and one shared element after
// Update All Modified Content.
void psd_photoshop_linked_elements_parse_if_available() {
  const auto beside_path = patchy::test::local_psd_fixture_path("ps2026_linked/linked_svg.psd");
  patchy::test::skip_unless_exists(beside_path);
  const auto only_link = [](const patchy::Document& document) -> const patchy::SmartObjectSource* {
    const patchy::SmartObjectSource* found = nullptr;
    std::size_t count = 0;
    for (const auto& block : document.metadata().smart_objects.blocks) {
      for (const auto& source : block.sources) {
        if (source.kind == patchy::SmartObjectSourceKind::ExternalFile) {
          CHECK(block.key == "lnkE");
          found = &source;
          ++count;
        }
      }
    }
    CHECK(count == 1U);
    return found;
  };
  const auto ends_with = [](const std::string& text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  const auto beside = patchy::psd::DocumentIo::read_file(beside_path);
  const auto* svg = only_link(beside);
  CHECK(svg->filename == "logo.svg");
  CHECK(svg->filetype == "SVG ");
  CHECK(svg->external_rel_path == "logo.svg");
  CHECK(svg->external_full_path.rfind("file:///", 0) == 0);
  CHECK(ends_with(svg->external_full_path, "/ps2026_linked/logo.svg"));
  CHECK(svg->external_link_desc_version == 2);
  CHECK(svg->external_file_size > 0U);
  CHECK(svg->external_mod_year >= 2026);
  CHECK(svg->external_mod_seconds == std::floor(svg->external_mod_seconds));

  const auto png = patchy::psd::DocumentIo::read_file(
      patchy::test::local_psd_fixture_path("ps2026_linked/linked_png.psd"));
  CHECK(only_link(png)->filetype == "png ");

  const auto parent = patchy::psd::DocumentIo::read_file(
      patchy::test::local_psd_fixture_path("ps2026_linked/sub/linked_svg_parent.psd"));
  CHECK(only_link(parent)->external_rel_path == "../logo.svg");

  const auto unicode = patchy::psd::DocumentIo::read_file(
      patchy::test::local_psd_fixture_path("ps2026_linked/linked_svg_unicode.psd"));
  const auto unicode_name = patchy::test::utf8_string(u8"logo \u00FCn\u00EF \u65E5\u672C.svg");
  const auto* unicode_link = only_link(unicode);
  CHECK(unicode_link->filename == unicode_name);
  CHECK(unicode_link->external_rel_path == unicode_name);
  CHECK(ends_with(unicode_link->external_full_path, "/ps2026_linked/" + unicode_name));
  CHECK(unicode_link->external_full_path.find('%') == std::string::npos);

  // Two placements of one file, then Update All Modified Content: one element, and
  // both layers point at it.
  const auto updated = patchy::psd::DocumentIo::read_file(
      patchy::test::local_psd_fixture_path("ps2026_linked/linked_svg_update.psd"));
  const auto* shared = only_link(updated);
  std::size_t sharing = 0;
  for (const auto& layer : updated.layers()) {
    if (patchy::layer_is_smart_object(layer)) {
      CHECK(patchy::smart_object_source_uuid(layer) == shared->uuid);
      ++sharing;
    }
  }
  CHECK(sharing == 2U);
}

void psd_descriptor_writer_round_trips_sold() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  const auto* layer = find_layer_named(document.layers(), "small");
  CHECK(layer != nullptr);
  check_sold_descriptor_round_trip(*layer);
}

void psd_descriptor_writer_round_trips_smart_filter_sold_if_available() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd"));
  const auto& layer = require_layer_named(document, "Gaussian radius 2.0");
  check_sold_descriptor_round_trip(layer);
}

// Walks a written PSD/PSB to its merged-composite section and returns the RLE
// row byte counts, or an empty vector for raw composites.
std::vector<std::uint32_t> composite_rle_row_lengths(std::span<const std::uint8_t> bytes) {
  patchy::psd::BigEndianReader reader(bytes);
  (void)patchy::psd::read_signature(reader);
  const auto version = reader.read_u16();
  reader.skip(6);
  const auto channels = reader.read_u16();
  const auto height = reader.read_u32();
  (void)reader.read_u32();  // width
  (void)reader.read_u16();  // depth
  (void)reader.read_u16();  // mode
  reader.skip(reader.read_u32());  // color mode data
  reader.skip(reader.read_u32());  // image resources
  const auto layer_length = version == 2 ? reader.read_u64() : reader.read_u32();
  reader.skip(static_cast<std::size_t>(layer_length));
  if (reader.read_u16() != 1) {
    return {};
  }
  std::vector<std::uint32_t> lengths(static_cast<std::size_t>(height) * channels);
  for (auto& length : lengths) {
    length = version == 2 ? reader.read_u32() : reader.read_u16();
  }
  return lengths;
}

// Pixels whose composite rows RLE-encode to odd byte counts without padding:
// each channel row is a 60-byte run plus a four-byte literal (2 + 5 = 7 bytes).
patchy::PixelBuffer odd_rle_row_pixels(std::int32_t width, std::int32_t height) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* pixel = pixels.pixel(x, y);
      const bool tail = x >= width - 4;
      pixel[0] = tail ? static_cast<std::uint8_t>(10 + x + y) : 0;
      pixel[1] = tail ? static_cast<std::uint8_t>(90 + x + y) : 0;
      pixel[2] = tail ? static_cast<std::uint8_t>(170 + x + y) : 0;
    }
  }
  return pixels;
}

// Photoshop's smart-object embed parser rejects documents whose embedded
// PSD/PSB composite has any odd-length RLE row (Photoshop 2026, pinned by
// byte-level bisection July 2026; docs/ps-compat.md). Every composite Patchy
// writes is even-rowed so a Patchy PSD/PSB placed or embedded anywhere stays
// openable.
void psd_composite_rle_rows_are_even_for_photoshop_embeds() {
  patchy::Document document(64, 3, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Odd", odd_rle_row_pixels(64, 3));

  for (const bool large_document : {false, true}) {
    const auto bytes =
        patchy::psd::DocumentIo::write_layered_rgb8(document, patchy::psd::WriteOptions{large_document});
    const auto lengths = composite_rle_row_lengths(bytes);
    CHECK(lengths.size() == 9U);
    for (const auto length : lengths) {
      CHECK((length % 2U) == 0U);
    }
    const auto reread = patchy::psd::DocumentIo::read(bytes);
    CHECK(reread.layers().size() == 1);
    const auto& pixels = reread.layers().front().pixels();
    const auto expected = odd_rle_row_pixels(64, 3);
    CHECK(pixels.width() == 64 && pixels.height() == 3);
    for (std::int32_t y = 0; y < 3; ++y) {
      for (std::int32_t x = 0; x < 64; ++x) {
        CHECK(std::memcmp(pixels.pixel(x, y), expected.pixel(x, y), 3) == 0);
      }
    }
  }
}

// A minimal PSB (v2, 3 channels, one 4-pixel row) whose composite rows are the
// odd five-byte literal [3, a, b, c, d] — the shape Patchy wrote before the
// even-row rule and the shape Photoshop rejects as an embed.
std::vector<std::uint8_t> odd_composite_mini_psb() {
  patchy::psd::BigEndianWriter writer;
  for (const char ch : {'8', 'B', 'P', 'S'}) {
    writer.write_u8(static_cast<std::uint8_t>(ch));
  }
  writer.write_u16(2);  // PSB
  for (int i = 0; i < 6; ++i) {
    writer.write_u8(0);
  }
  writer.write_u16(3);   // channels
  writer.write_u32(1);   // height
  writer.write_u32(4);   // width
  writer.write_u16(8);   // depth
  writer.write_u16(3);   // RGB
  writer.write_u32(0);   // color mode data
  writer.write_u32(0);   // image resources
  writer.write_u64(0);   // layer and mask section
  writer.write_u16(1);   // RLE composite
  for (int channel = 0; channel < 3; ++channel) {
    writer.write_u32(5);
  }
  for (int channel = 0; channel < 3; ++channel) {
    writer.write_u8(3);  // literal of four bytes
    for (int i = 0; i < 4; ++i) {
      writer.write_u8(static_cast<std::uint8_t>(50 * channel + i));
    }
  }
  return writer.bytes();
}

// Saving a document whose stored embed still has odd composite rows rewrites
// the embedded bytes (the repair path for files saved before the even-row
// rule); the decoded pixels stay identical.
void psd_smart_object_embed_odd_composite_normalized_on_save() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Smart", solid_rgb(4, 2, 10, 20, 30));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  const auto odd_psb = odd_composite_mini_psb();
  document.metadata().smart_objects.add_embedded(
      "11111111-2222-3333-4444-555555555555", "inner.psb", "8BPB",
      std::make_shared<const std::vector<std::uint8_t>>(odd_psb));

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* source = reread.metadata().smart_objects.find("11111111-2222-3333-4444-555555555555");
  CHECK(source != nullptr && source->file_bytes != nullptr);
  CHECK(source->file_bytes->size() == odd_psb.size() + 3U);  // one literal split per row
  const auto lengths = composite_rle_row_lengths(*source->file_bytes);
  CHECK(lengths.size() == 3U);
  for (const auto length : lengths) {
    CHECK(length == 6U);
  }
  const auto inner = patchy::psd::DocumentIo::read(
      {source->file_bytes->data(), source->file_bytes->size()});
  CHECK(inner.width() == 4 && inner.height() == 1);
  const auto flattened = patchy::Compositor{}.flatten_rgb8(inner);
  for (std::int32_t x = 0; x < 4; ++x) {
    CHECK(flattened.pixel(x, 0)[0] == static_cast<std::uint8_t>(x));
    CHECK(flattened.pixel(x, 0)[1] == static_cast<std::uint8_t>(50 + x));
    CHECK(flattened.pixel(x, 0)[2] == static_cast<std::uint8_t>(100 + x));
  }

  // Already-normalized embeds re-emit untouched: the next save is byte-stable.
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(reread);
  const auto resaved_reread = patchy::psd::DocumentIo::read(resaved);
  const auto* stable = resaved_reread.metadata().smart_objects.find("11111111-2222-3333-4444-555555555555");
  CHECK(stable != nullptr && stable->file_bytes != nullptr);
  CHECK(*stable->file_bytes == *source->file_bytes);
}

// Photoshop keys placed layers by their 'lyid' layer id when a Smart Filter
// cache (FEid) is present; a smart-object layer without one makes it reject
// the whole file (Photoshop 2026, pinned by byte-level bisection July 2026).
// Saving assigns fresh unique ids to id-less smart-object layers and keeps
// preserved ones untouched.
void psd_smart_object_layers_get_layer_ids_on_save() {
  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  const auto embed = std::make_shared<const std::vector<std::uint8_t>>(odd_composite_mini_psb());
  auto& first = document.add_pixel_layer("First", solid_rgb(4, 2, 10, 20, 30));
  first.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  first.metadata()[patchy::kLayerMetadataSmartObject] = "aaaa";
  auto& second = document.add_pixel_layer("Second", solid_rgb(4, 2, 40, 50, 60));
  second.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  second.metadata()[patchy::kLayerMetadataSmartObject] = "bbbb";
  patchy::set_photoshop_layer_id(second, 7);
  auto& plain = document.add_pixel_layer("Plain", solid_rgb(4, 2, 70, 80, 90));
  document.metadata().smart_objects.add_embedded("aaaa", "a.psb", "8BPB", embed);
  document.metadata().smart_objects.add_embedded("bbbb", "b.psb", "8BPB", embed);
  (void)plain;

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(reread.layers().size() == 3);
  const auto first_id = patchy::photoshop_layer_id(*find_layer_named(reread.layers(), "First"));
  const auto second_id = patchy::photoshop_layer_id(*find_layer_named(reread.layers(), "Second"));
  const auto plain_id = patchy::photoshop_layer_id(*find_layer_named(reread.layers(), "Plain"));
  CHECK(first_id.has_value());
  CHECK(second_id.has_value() && *second_id == 7U);  // preserved, not reassigned
  CHECK(*first_id != *second_id);
  CHECK(*first_id == 8U);  // continues above the largest preserved id
  CHECK(!plain_id.has_value());  // only smart-object layers need ids
}

// Photoshop 2026 refuses to open a PSD whose lnk2 holds an element no layer
// references, whoever wrote it ("program error"; September 2026 probes).
// The writer leaves such elements out while the store keeps them; referenced
// elements, and documents with a placed layer of unknown source, keep everything.
void psd_writer_omits_unreferenced_smart_object_sources() {
  const auto embed = std::make_shared<const std::vector<std::uint8_t>>(odd_composite_mini_psb());
  const auto contains_lnk2 = [](const std::vector<std::uint8_t>& bytes) {
    static constexpr std::array<std::uint8_t, 8> kTag{'8', 'B', 'I', 'M', 'l', 'n', 'k', '2'};
    return std::search(bytes.begin(), bytes.end(), kTag.begin(), kTag.end()) != bytes.end();
  };

  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& used = document.add_pixel_layer("Used", solid_rgb(4, 2, 10, 20, 30));
  used.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  used.metadata()[patchy::kLayerMetadataSmartObject] = "aaaa";
  document.metadata().smart_objects.add_embedded("aaaa", "a.psb", "8BPB", embed);
  document.metadata().smart_objects.add_embedded("orphan", "o.psb", "8BPB", embed);
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(reread.metadata().smart_objects.find("aaaa") != nullptr);
  CHECK(reread.metadata().smart_objects.find("orphan") == nullptr);
  CHECK(document.metadata().smart_objects.find("orphan") != nullptr);  // writing never mutates

  // Nothing referenced: the emptied lnk2 block is not written at all.
  patchy::Document lone(4, 2, patchy::PixelFormat::rgb8());
  lone.add_pixel_layer("Plain", solid_rgb(4, 2, 70, 80, 90));
  lone.metadata().smart_objects.add_embedded("orphan", "o.psb", "8BPB", embed);
  const auto lone_bytes = patchy::psd::DocumentIo::write_layered_rgb8(lone);
  CHECK(!contains_lnk2(lone_bytes));
  CHECK(patchy::psd::DocumentIo::read(lone_bytes).metadata().smart_objects.empty());

  // A placed block that never became metadata might reference any element.
  patchy::Document unknown(4, 2, patchy::PixelFormat::rgb8());
  auto& mystery = unknown.add_pixel_layer("Mystery", solid_rgb(4, 2, 40, 50, 60));
  mystery.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  unknown.metadata().smart_objects.add_embedded("orphan", "o.psb", "8BPB", embed);
  CHECK(contains_lnk2(patchy::psd::DocumentIo::write_layered_rgb8(unknown)));

  // A linked element (lnkE) is pruned the same way: Photoshop refuses an orphaned
  // liFE element too (September 30, 2026 probe).
  patchy::Document linked(4, 2, patchy::PixelFormat::rgb8());
  auto& linked_layer = linked.add_pixel_layer("Linked", solid_rgb(4, 2, 10, 20, 30));
  linked_layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLE", {5, 6, 7, 8}});
  linked_layer.metadata()[patchy::kLayerMetadataSmartObject] = "kept";
  patchy::SmartObjectSource kept_link;
  kept_link.uuid = "kept";
  kept_link.filename = "kept.svg";
  kept_link.filetype = "SVG ";
  kept_link.external_rel_path = "kept.svg";
  patchy::SmartObjectSource orphan_link = kept_link;
  orphan_link.uuid = "orphan-link";
  orphan_link.filename = "orphan.svg";
  linked.metadata().smart_objects.add_external(kept_link);
  linked.metadata().smart_objects.add_external(orphan_link);
  const auto linked_reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(linked));
  CHECK(linked_reread.metadata().smart_objects.find("kept") != nullptr);
  CHECK(linked_reread.metadata().smart_objects.find("orphan-link") == nullptr);
  CHECK(linked.metadata().smart_objects.find("orphan-link") != nullptr);

  // Photoshop's own element goes too once its only layer is rasterized: Photoshop
  // refuses that orphan as well, and drops it itself when it saves.
  auto placed = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd"));
  auto* placed_layer = const_cast<patchy::Layer*>(find_layer_named(placed.layers(), "small"));
  CHECK(placed_layer != nullptr);
  const auto uuid = patchy::smart_object_source_uuid(*placed_layer);
  patchy::strip_layer_smart_object_data(*placed_layer);
  const auto placed_bytes = patchy::psd::DocumentIo::write_layered_rgb8(placed);
  CHECK(!contains_lnk2(placed_bytes));
  CHECK(patchy::psd::DocumentIo::read(placed_bytes).metadata().smart_objects.find(uuid) == nullptr);
  CHECK(placed.metadata().smart_objects.find(uuid) != nullptr);
}

// The July 2026 field failure: a Patchy-authored smart-filter PSD Photoshop
// refused to open ("program error"), root causes odd embed-composite rows and
// a missing 'lyid'. Resaving through the fixed writer must repair both.
void psd_local_smart_filter_file_repairs_on_resave_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("akiko_cycling_okinawa_with_filters.psd");
  patchy::test::skip_unless_exists(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* layer = find_layer_named(reread.layers(), "akiko_cycling_okinawa");
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*layer));
  CHECK(patchy::photoshop_layer_id(*layer).has_value());
  const auto* source =
      reread.metadata().smart_objects.find(patchy::smart_object_source_uuid(*layer));
  CHECK(source != nullptr && source->file_bytes != nullptr);
  for (const auto length : composite_rle_row_lengths(*source->file_bytes)) {
    CHECK((length % 2U) == 0U);
  }
}

// A clean (undirtied) element from an existing file goes through the surgical
// rebuild: only the embedded bytes and the length fields change, the wrapper
// (version, uuid, name, unmodeled trailer bytes) stays verbatim.
void psd_smart_object_clean_element_normalization_keeps_wrapper() {
  const auto odd_psb = odd_composite_mini_psb();
  patchy::psd::BigEndianWriter body;
  for (const char ch : {'l', 'i', 'F', 'D'}) {
    body.write_u8(static_cast<std::uint8_t>(ch));
  }
  body.write_u32(7);
  body.write_u8(36);
  for (const char ch : std::string_view("22222222-3333-4444-5555-666666666666")) {
    body.write_u8(static_cast<std::uint8_t>(ch));
  }
  body.write_u32(6);  // unicode "in.psb"
  for (const char ch : std::string_view("in.psb")) {
    body.write_u16(static_cast<std::uint16_t>(ch));
  }
  for (const char ch : std::string_view("8BPB8BIM")) {
    body.write_u8(static_cast<std::uint8_t>(ch));
  }
  body.write_u64(odd_psb.size());
  body.write_u8(0);  // no file-open descriptor
  body.write_bytes(odd_psb);
  body.write_u32(0);        // child document id
  patchy::psd::write_f64(body, 0.0);  // asset mod time
  body.write_u8(0);         // asset locked state
  for (int i = 0; i < 5; ++i) {
    body.write_u8(0xAB);  // unmodeled trailer bytes (a version-8-style tail)
  }
  patchy::psd::BigEndianWriter element;
  element.write_u64(body.bytes().size());
  element.write_bytes(body.bytes());
  const auto padding = (4U - (body.bytes().size() % 4U)) % 4U;
  for (std::size_t i = 0; i < padding; ++i) {
    element.write_u8(0);
  }

  patchy::Document document(4, 2, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer("Smart", solid_rgb(4, 2, 10, 20, 30));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"SoLd", {5, 6, 7, 8}});
  document.metadata().unknown_psd_resources.push_back(
      patchy::UnknownPsdBlock{"lnk2", element.bytes()});

  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto* parsed = read.metadata().smart_objects.find("22222222-3333-4444-5555-666666666666");
  CHECK(parsed != nullptr && !parsed->dirty && parsed->original_element_bytes != nullptr);
  CHECK(parsed->file_bytes != nullptr && *parsed->file_bytes == odd_psb);  // read stays faithful

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(read));
  const auto* normalized = reread.metadata().smart_objects.find("22222222-3333-4444-5555-666666666666");
  CHECK(normalized != nullptr && normalized->file_bytes != nullptr);
  CHECK(normalized->file_bytes->size() == odd_psb.size() + 3U);
  const auto lengths = composite_rle_row_lengths(*normalized->file_bytes);
  CHECK(lengths.size() == 3U);
  for (const auto length : lengths) {
    CHECK(length == 6U);
  }
  CHECK(normalized->filename == "in.psb");
  CHECK(normalized->original_element_bytes != nullptr);
  const std::array<std::uint8_t, 5> marker{0xAB, 0xAB, 0xAB, 0xAB, 0xAB};
  CHECK(std::search(normalized->original_element_bytes->begin(), normalized->original_element_bytes->end(),
                    marker.begin(), marker.end()) != normalized->original_element_bytes->end());
}

}  // namespace

// Free Transform of a placement (GitHub issue 225), pinned to Photoshop's own
// perspective layers in the pinball poster: Trnf is the axis-aligned bounding
// box of nonAffineTransform, written TL, TR, BR, BL. The numbers are that file's.
void smart_object_placement_perspective_rule_matches_photoshop() {
  const std::array<double, 9> identity{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  patchy::SmartObjectPlacement photoshop;
  photoshop.transform = {-1546.0, -132.0, 2746.0719, -132.0, 2746.0719, 337.0, -1546.0, 337.0};
  photoshop.non_affine_transform =
      std::array<double, 8>{-1546.0, -132.0, 2746.0719, -132.0, 1255.0, 337.0, -54.9281, 337.0};
  const auto same = patchy::map_smart_object_placement(photoshop, identity);
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs(same.transform[i] - photoshop.transform[i]) < 1e-9);
    CHECK(std::abs((*same.non_affine_transform)[i] - (*photoshop.non_affine_transform)[i]) < 1e-9);
  }
  // The second capture is turned 180 degrees: its corners start at the bottom right.
  patchy::SmartObjectPlacement turned;
  turned.transform = {-1404.7288, 421.0, 2605.0, 421.0, 2605.0, 846.0, -1404.7288, 846.0};
  turned.non_affine_transform =
      std::array<double, 8>{2605.0, 846.0, -1404.7288, 846.0, -55.0, 421.0, 1255.2712, 421.0};
  const auto turned_same = patchy::map_smart_object_placement(turned, identity);
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs(turned_same.transform[i] - turned.transform[i]) < 1e-9);
  }
  CHECK(patchy::smart_object_render_quad(turned) == *turned.non_affine_transform);

  // A plain placement under an affine map keeps the per-corner Trnf map.
  patchy::SmartObjectPlacement plain;
  plain.transform = {10.0, 20.0, 110.0, 20.0, 110.0, 80.0, 10.0, 80.0};
  const auto moved = patchy::map_smart_object_placement(plain, {2.0, 0.0, 5.0, 0.0, 1.0, -3.0, 0.0, 0.0, 1.0});
  CHECK(!moved.non_affine_transform.has_value());
  CHECK(moved.transform == (std::array<double, 8>{25.0, 17.0, 225.0, 17.0, 225.0, 77.0, 25.0, 77.0}));
  // A perspective map: the corners go to nonAffineTransform, their bounds to Trnf.
  const std::array<double, 9> perspective{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.005, 0.0, 1.0};
  const auto distorted = patchy::map_smart_object_placement(plain, perspective);
  CHECK(distorted.non_affine_transform.has_value());
  const auto& quad = *distorted.non_affine_transform;
  CHECK(std::abs(quad[0] - 10.0 / 1.05) < 1e-9);
  CHECK(std::abs(quad[3] - 20.0 / 1.55) < 1e-9);
  const double left = std::min({quad[0], quad[2], quad[4], quad[6]});
  const double bottom = std::max({quad[1], quad[3], quad[5], quad[7]});
  CHECK(std::abs(distorted.transform[0] - left) < 1e-12);
  CHECK(std::abs(distorted.transform[6] - left) < 1e-12);
  CHECK(std::abs(distorted.transform[5] - bottom) < 1e-12);
  CHECK(distorted.transform[1] == distorted.transform[3]);
  // A plain placement under a skew (affine) keeps the Trnf-only map; an already
  // perspective placement keeps its quad under an identity map.
  const auto skewed = patchy::map_smart_object_placement(plain, {1.0, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
  CHECK(!skewed.non_affine_transform.has_value());
  const auto unchanged = patchy::map_smart_object_placement(distorted, identity);
  CHECK(unchanged.non_affine_transform == distorted.non_affine_transform);
  CHECK(unchanged.transform == distorted.transform);
  // A perspective placement mapped back to a parallelogram stores that quad in both.
  patchy::SmartObjectPlacement flat_again;
  flat_again.transform = plain.transform;
  flat_again.non_affine_transform = plain.transform;
  const auto parallelogram = patchy::map_smart_object_placement(flat_again, {1.0, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
  CHECK(parallelogram.non_affine_transform.has_value());
  CHECK(parallelogram.transform == *parallelogram.non_affine_transform);
}

std::vector<patchy::test::TestCase> smart_objects_warp_tests() {
  return {
      {"psd_global_link_blocks_round_trip_with_smart_object_layers",
       psd_global_link_blocks_round_trip_with_smart_object_layers},
      {"psd_dangling_smart_object_blocks_are_stripped", psd_dangling_smart_object_blocks_are_stripped},
      {"psd_smart_object_sources_survive_resave_if_available",
       psd_smart_object_sources_survive_resave_if_available},
      {"psd_smart_object_document_resize_scales_placements_if_available",
       psd_smart_object_document_resize_scales_placements_if_available},
      {"smart_object_placement_perspective_rule_matches_photoshop", smart_object_placement_perspective_rule_matches_photoshop},
      {"psd_smart_object_non_affine_quad_maps_through_affine_transform_if_available",
       psd_smart_object_non_affine_quad_maps_through_affine_transform_if_available},
      {"psb_layered_round_trip_preserves_layers_and_blocks",
       psb_layered_round_trip_preserves_layers_and_blocks},
      {"psb_flat_round_trip_reads_composite", psb_flat_round_trip_reads_composite},
      {"psb_photoshop_fixture_round_trips", psb_photoshop_fixture_round_trips},
      {"psd_smart_object_fixture_parses_placement_and_source",
       psd_smart_object_fixture_parses_placement_and_source},
      {"psd_smart_object_clean_resave_preserves_blocks_byte_identically",
       psd_smart_object_clean_resave_preserves_blocks_byte_identically},
      {"psd_smart_object_move_regenerates_placed_blocks", psd_smart_object_move_regenerates_placed_blocks},
      {"smart_object_rescaled_placement_matches_photoshop_replace_rule",
       smart_object_rescaled_placement_matches_photoshop_replace_rule},
      {"smart_object_placements_follow_document_geometry",
       smart_object_placements_follow_document_geometry},
      {"smart_object_store_remove_and_generated_uuid_shape",
       smart_object_store_remove_and_generated_uuid_shape},
      {"psd_smart_object_committed_psb_contents_round_trip",
       psd_smart_object_committed_psb_contents_round_trip},
      {"smart_object_authored_sold_matches_photoshop_shape",
       smart_object_authored_sold_matches_photoshop_shape},
      {"psd_unparsed_smart_object_locks_and_round_trips_if_available",
       psd_unparsed_smart_object_locks_and_round_trips_if_available},
      {"psb_linked_smart_objects_parse_lnke_if_available",
       psb_linked_smart_objects_parse_lnke_if_available},
      {"warp_mesh_math_behaves", warp_mesh_math_behaves},
      {"psd_warp_move_matches_photoshop_if_available", psd_warp_move_matches_photoshop_if_available},
      {"psd_descriptor_writer_round_trips_warp_sold_if_available",
       psd_descriptor_writer_round_trips_warp_sold_if_available},
      {"warp_style_meshes_match_photoshop_if_available", warp_style_meshes_match_photoshop_if_available},
      {"psd_style_only_warp_unlocks_and_regenerates_if_available",
       psd_style_only_warp_unlocks_and_regenerates_if_available},
      {"text_warp_serialization_round_trips", text_warp_serialization_round_trips},
      {"warp_style_meshes_match_photoshop_e9c_if_available",
       warp_style_meshes_match_photoshop_e9c_if_available},
      {"psd_text_warp_round_trips_photoshop_fixture", psd_text_warp_round_trips_photoshop_fixture},
      {"psb_life_trailer_fields_parse_if_available", psb_life_trailer_fields_parse_if_available},
      {"smart_object_external_element_round_trips_if_available",
       smart_object_external_element_round_trips_if_available},
      {"smart_object_authored_vector_sold_matches_photoshop_shape",
       smart_object_authored_vector_sold_matches_photoshop_shape},
      {"psd_smart_object_authored_link_round_trips", psd_smart_object_authored_link_round_trips},
      {"psd_photoshop_linked_captures_match_authored_blocks_if_available",
       psd_photoshop_linked_captures_match_authored_blocks_if_available},
      {"psd_photoshop_linked_elements_parse_if_available", psd_photoshop_linked_elements_parse_if_available},
      {"psd_descriptor_writer_round_trips_sold", psd_descriptor_writer_round_trips_sold},
      {"psd_descriptor_writer_round_trips_smart_filter_sold_if_available",
       psd_descriptor_writer_round_trips_smart_filter_sold_if_available},
      {"psd_composite_rle_rows_are_even_for_photoshop_embeds",
       psd_composite_rle_rows_are_even_for_photoshop_embeds},
      {"psd_smart_object_embed_odd_composite_normalized_on_save",
       psd_smart_object_embed_odd_composite_normalized_on_save},
      {"psd_writer_omits_unreferenced_smart_object_sources",
       psd_writer_omits_unreferenced_smart_object_sources},
      {"psd_smart_object_layers_get_layer_ids_on_save",
       psd_smart_object_layers_get_layer_ids_on_save},
      {"psd_local_smart_filter_file_repairs_on_resave_if_available",
       psd_local_smart_filter_file_repairs_on_resave_if_available},
      {"psd_smart_object_clean_element_normalization_keeps_wrapper",
       psd_smart_object_clean_element_normalization_keeps_wrapper},
  };
}
