#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_shape.hpp"
#include "formats/bmp_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_patterns.hpp"
#include "render/compositor.hpp"

#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "psd_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using patchy::Document;
using patchy::Layer;
using patchy::LiveShapeKind;
using patchy::PathCombineOp;
using patchy::VectorFillKind;
using patchy::test::committed_psd_fixture_path;
using patchy::test::rgb_diff_metrics;
using patchy::test::write_rgb8_bmp_artifact;

Document read_fixture(const char* name) {
  return patchy::psd::DocumentIo::read_file(committed_psd_fixture_path(name));
}

const Layer& layer_at(const Document& document, std::size_t index) {
  CHECK(index < document.layers().size());
  return document.layers()[index];
}

// Parity policy for Photoshop-rendered references: Patchy's rasterizer and
// Photoshop both compute near-exact area coverage, so interiors match exactly
// and differences concentrate in one-pixel AA bands. Never byte-pin PS pixels.
void check_flatten_matches_reference(const Document& document, const char* bmp_name,
                                     const char* artifact_stem, double max_mean = 1.0,
                                     int max_delta = 96, double max_differing_fraction = 0.12) {
  const auto reference_doc = patchy::bmp::DocumentIo::read_file(committed_psd_fixture_path(bmp_name));
  const auto& reference = std::as_const(reference_doc).layers().front().pixels();
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto metrics = rgb_diff_metrics(flattened, reference);
  if (metrics.mean_abs_channel_delta > max_mean || metrics.max_channel_delta > max_delta ||
      (metrics.pixels > 0 &&
       static_cast<double>(metrics.differing_pixels) / static_cast<double>(metrics.pixels) >
           max_differing_fraction)) {
    write_rgb8_bmp_artifact(std::string(artifact_stem) + "_patchy", flattened);
    write_rgb8_bmp_artifact(std::string(artifact_stem) + "_photoshop", reference);
    std::fprintf(stderr, "%s: mean %.3f max %d differing %llu/%llu\n", artifact_stem,
                 metrics.mean_abs_channel_delta, metrics.max_channel_delta,
                 static_cast<unsigned long long>(metrics.differing_pixels),
                 static_cast<unsigned long long>(metrics.pixels));
  }
  CHECK(metrics.mean_abs_channel_delta <= max_mean);
  CHECK(metrics.max_channel_delta <= max_delta);
  if (metrics.pixels > 0) {
    CHECK(static_cast<double>(metrics.differing_pixels) / static_cast<double>(metrics.pixels) <=
          max_differing_fraction);
  }
}

void psd_shape_solid_fixture_parses_and_renders() {
  const auto document = read_fixture("photoshop-shape-solid.psd");
  CHECK(document.layers().size() == 2);
  const auto& shape = layer_at(document, 1);
  const auto* content = shape.vector_shape();
  CHECK(content != nullptr);
  CHECK(patchy::layer_has_vector_shape_marker(shape));
  CHECK(content->fill.kind == VectorFillKind::Solid);
  CHECK(content->fill.color.red == 214 && content->fill.color.green == 40 && content->fill.color.blue == 40);
  CHECK(content->path.subpaths.size() == 1);
  const auto& anchors = content->path.subpaths[0].anchors;
  CHECK(anchors.size() == 5);
  CHECK(content->path.subpaths[0].closed);
  // Photoshop wrote empty channels: Patchy rasterized at import.
  CHECK(shape.metadata().at(patchy::kLayerMetadataVectorRasterStatus) ==
        patchy::kVectorRasterStatusPatchy);
  CHECK(!std::as_const(shape).pixels().empty());
  check_flatten_matches_reference(document, "photoshop-shape-solid.bmp", "psd_vector_solid");
}

void psd_shape_gradient_fixture_parses_and_renders() {
  const auto document = read_fixture("photoshop-shape-gradient.psd");
  const auto& shape = layer_at(document, 1);
  const auto* content = shape.vector_shape();
  CHECK(content != nullptr);
  CHECK(content->fill.kind == VectorFillKind::Gradient);
  const auto& gradient = content->fill.gradient;
  CHECK(gradient.color_stops.size() == 3);
  CHECK(gradient.alpha_stops.size() == 3);
  CHECK(std::fabs(gradient.angle_degrees - 37.0F) < 0.01F);
  CHECK(gradient.type == patchy::LayerStyleGradientType::Linear);
  CHECK(std::fabs(gradient.color_stops[0].midpoint - 0.30F) < 0.005F);
  CHECK(std::fabs(gradient.alpha_stops[1].opacity - 0.42F) < 0.005F);
  // Calibrated geometry (docs/vector-tools.md): center-chord span + the
  // catmull-rom smoothness ease on color AND opacity. The remaining ~1.2 mean
  // is Photoshop's non-uniform parametrization of unevenly spaced stops,
  // which stays a documented residual.
  check_flatten_matches_reference(document, "photoshop-shape-gradient.bmp", "psd_vector_gradient", 1.5, 12,
                                  0.75);
}

void psd_shape_pattern_fixture_parses_and_renders() {
  const auto document = read_fixture("photoshop-shape-pattern.psd");
  const auto& shape = layer_at(document, 1);
  const auto* content = shape.vector_shape();
  CHECK(content != nullptr);
  CHECK(content->fill.kind == VectorFillKind::Pattern);
  CHECK(!content->fill.pattern_id.empty());
  CHECK(document.metadata().patterns.find(content->fill.pattern_id) != nullptr);
  check_flatten_matches_reference(document, "photoshop-shape-pattern.bmp", "psd_vector_pattern");
}

void psd_shape_strokes_fixture_parses_and_renders() {
  const auto document = read_fixture("photoshop-shape-strokes.psd");
  CHECK(document.layers().size() == 7);
  const auto stroke_of = [&](std::size_t index) {
    const auto* content = layer_at(document, index).vector_shape();
    CHECK(content != nullptr);
    return content->stroke;
  };
  const auto center = stroke_of(1);
  CHECK(center.enabled && center.fill_enabled);
  CHECK(std::fabs(center.width - 6.0) < 1e-9);
  CHECK(center.alignment == patchy::VectorStrokeAlignment::Center);
  CHECK(center.cap == patchy::VectorStrokeCap::Butt);
  CHECK(center.join == patchy::VectorStrokeJoin::Miter);
  CHECK(stroke_of(2).alignment == patchy::VectorStrokeAlignment::Inside);
  CHECK(stroke_of(3).alignment == patchy::VectorStrokeAlignment::Outside);
  const auto dashed = stroke_of(4);
  CHECK(!dashed.fill_enabled);
  CHECK(dashed.cap == patchy::VectorStrokeCap::Round);
  CHECK(dashed.join == patchy::VectorStrokeJoin::Round);
  CHECK(dashed.dashes.size() == 2);
  CHECK(std::fabs(dashed.dashes[0] - 2.0) < 1e-9 && std::fabs(dashed.dashes[1] - 1.0) < 1e-9);
  const auto beveled = stroke_of(5);
  CHECK(beveled.cap == patchy::VectorStrokeCap::Square);
  CHECK(beveled.join == patchy::VectorStrokeJoin::Bevel);
  const auto hollow = stroke_of(6);
  CHECK(hollow.enabled && !hollow.fill_enabled);
  // Dash boundaries land where arc-length integration says; Photoshop's and
  // Patchy's flattenings disagree by a fraction of a pixel, so a handful of
  // dash-edge pixels flip fully while everything else matches (mean 0.3).
  check_flatten_matches_reference(document, "photoshop-shape-strokes.bmp", "psd_vector_strokes", 0.6, 255,
                                  0.06);
}

void psd_shape_boolean_fixture_combines_and_renders() {
  const auto document = read_fixture("photoshop-shape-boolean.psd");
  const auto& shape = layer_at(document, 1);
  const auto* content = shape.vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 4);
  CHECK(content->path.subpaths[0].op == PathCombineOp::Add);
  CHECK(content->path.subpaths[1].op == PathCombineOp::Subtract);
  CHECK(content->path.subpaths[2].op == PathCombineOp::Intersect);
  CHECK(content->path.subpaths[3].op == PathCombineOp::Xor);
  CHECK(content->path.subpaths[3].shape_group == 3);
  check_flatten_matches_reference(document, "photoshop-shape-boolean.bmp", "psd_vector_boolean");
}

void psd_shape_first_ops_fixture_renders() {
  const auto document = read_fixture("photoshop-shape-first-ops.psd");
  CHECK(document.layers().size() == 4);
  CHECK(layer_at(document, 1).vector_shape() != nullptr);
  CHECK(layer_at(document, 1).vector_shape()->path.subpaths[0].op == PathCombineOp::Subtract);
  check_flatten_matches_reference(document, "photoshop-shape-first-ops.bmp", "psd_vector_first_ops");
}

void psd_shape_live_fixture_parses_origination() {
  const auto document = read_fixture("photoshop-shape-live-rect.psd");
  CHECK(document.layers().size() == 4);
  const auto* rect = layer_at(document, 1).vector_shape();
  CHECK(rect != nullptr);
  CHECK(rect->origination.size() == 1);
  CHECK(rect->origination[0].kind == LiveShapeKind::RoundedRectangle);
  // Authored radii: topLeft 4, topRight 8, bottomLeft 12, bottomRight 16
  // (model order TL, TR, BR, BL).
  CHECK(std::fabs(rect->origination[0].corner_radii[0] - 4.0) < 1e-9);
  CHECK(std::fabs(rect->origination[0].corner_radii[1] - 8.0) < 1e-9);
  CHECK(std::fabs(rect->origination[0].corner_radii[2] - 16.0) < 1e-9);
  CHECK(std::fabs(rect->origination[0].corner_radii[3] - 12.0) < 1e-9);
  CHECK(std::fabs(rect->origination[0].left - 6.0) < 1e-9);
  CHECK(std::fabs(rect->origination[0].bottom - 40.0) < 1e-9);
  const auto* ellipse = layer_at(document, 2).vector_shape();
  CHECK(ellipse != nullptr);
  CHECK(ellipse->origination.size() == 1);
  CHECK(ellipse->origination[0].kind == LiveShapeKind::Ellipse);
  const auto* line = layer_at(document, 3).vector_shape();
  CHECK(line != nullptr);
  CHECK(line->origination.size() == 1);
  CHECK(line->origination[0].kind == LiveShapeKind::Line);
  CHECK(std::fabs(line->origination[0].line_weight - 4.0) < 1e-9);
  CHECK(!line->origination[0].arrow_start && !line->origination[0].arrow_end);
  check_flatten_matches_reference(document, "photoshop-shape-live-rect.bmp", "psd_vector_live");
}

void psd_vector_mask_fixture_masks_pixels() {
  const auto document = read_fixture("photoshop-vector-mask-on-pixel.psd");
  const auto& layer = layer_at(document, 1);
  CHECK(layer.vector_shape() == nullptr);
  const auto* mask = layer.vector_mask();
  CHECK(mask != nullptr);
  CHECK(!mask->disabled);
  CHECK(mask->path.subpaths.size() == 1);
  CHECK(mask->path.subpaths[0].anchors.size() == 4);
  CHECK(!mask->cache.empty());  // baked by finalize (no derived plane in the file)
  CHECK(!layer.mask().has_value());
  check_flatten_matches_reference(document, "photoshop-vector-mask-on-pixel.bmp", "psd_vector_vmask");
}

void psd_both_masks_fixture_parses_parameters() {
  const auto document = read_fixture("photoshop-both-masks.psd");
  CHECK(document.layers().size() == 3);
  const auto& both = layer_at(document, 1);
  CHECK(both.mask().has_value());
  CHECK(both.vector_mask() != nullptr);
  CHECK(both.vector_mask()->density == 255);
  const auto& parameterized = layer_at(document, 2);
  const auto* mask = parameterized.vector_mask();
  CHECK(mask != nullptr);
  CHECK(mask->density == 153);
  CHECK(std::fabs(mask->feather - 1.5) < 1e-9);
  // Photoshop's baked bit-3 plane holds unfeathered coverage and never
  // becomes a raster user mask; Patchy re-derives a feathered cache.
  CHECK(!parameterized.mask().has_value());
  CHECK(!mask->cache.empty());
  // The feather softens the boundary: a pixel just inside the triangle edge
  // reads partial coverage.
  const auto edge_alpha = patchy::vector_mask_alpha_at(parameterized, 88, 51);
  CHECK(edge_alpha > 0.45F && edge_alpha < 0.95F);
  check_flatten_matches_reference(document, "photoshop-both-masks.bmp", "psd_vector_both_masks", 1.6, 128,
                                  0.15);
}

void psd_saved_paths_fixture_populates_document_paths() {
  const auto document = read_fixture("photoshop-saved-paths.psd");
  CHECK(document.paths().size() == 3);
  const auto find_named = [&](const std::string& name) -> const patchy::DocumentPath* {
    for (const auto& path : document.paths()) {
      if (path.name() == name) {
        return &path;
      }
    }
    return nullptr;
  };
  const auto* alpha = find_named("Alpha Path");
  CHECK(alpha != nullptr);
  CHECK(alpha->kind() == patchy::DocumentPathKind::Saved);
  CHECK(alpha->is_clipping_path());
  CHECK(alpha->resource_id().has_value() && *alpha->resource_id() == 2000);
  CHECK(!alpha->dirty());
  CHECK(alpha->path().subpaths.size() == 1);
  const auto* beta = find_named("Beta Path");
  CHECK(beta != nullptr);
  CHECK(!beta->is_clipping_path());
  CHECK(beta->path().subpaths.size() == 2);
  CHECK(beta->path().subpaths[1].op == PathCombineOp::Subtract);
  const auto* work = find_named("Work Path");
  CHECK(work != nullptr);
  CHECK(work->kind() == patchy::DocumentPathKind::Work);
  CHECK(work->resource_id().has_value() && *work->resource_id() == 1025);
}

void psd_shape_psb_fixture_parses_and_renders() {
  const auto document = read_fixture("photoshop-shape.psb");
  CHECK(document.metadata().values.at("psd.version") == "PSB");
  const auto& shape = layer_at(document, 1);
  CHECK(shape.vector_shape() != nullptr);
  CHECK(shape.vector_shape()->fill.kind == VectorFillKind::Solid);
  check_flatten_matches_reference(document, "photoshop-shape-psb.bmp", "psd_vector_psb");
}

std::vector<std::uint8_t> read_fixture_bytes(const char* name) {
  const auto path = committed_psd_fixture_path(name);
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream.good());
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
}

// Scans a layer's extra-data bytes for an '8BIM'+key tagged block payload
// (test-local; keys are unique within one layer record).
std::optional<std::vector<std::uint8_t>> find_tagged_block(std::span<const std::uint8_t> extra,
                                                           std::string_view key) {
  std::string pattern = "8BIM";
  pattern += key;
  for (std::size_t i = 0; i + pattern.size() + 4 <= extra.size(); ++i) {
    if (std::memcmp(extra.data() + i, pattern.data(), pattern.size()) != 0) {
      continue;
    }
    const auto length = patchy::test::read_u32_be_at(extra, i + pattern.size());
    const auto start = i + pattern.size() + 4;
    if (start + length > extra.size()) {
      return std::nullopt;
    }
    return std::vector<std::uint8_t>(extra.begin() + static_cast<std::ptrdiff_t>(start),
                                     extra.begin() + static_cast<std::ptrdiff_t>(start + length));
  }
  return std::nullopt;
}

void check_vector_blocks_byte_equal(std::span<const std::uint8_t> original,
                                    std::span<const std::uint8_t> written, std::int16_t layer_index,
                                    std::initializer_list<const char*> keys) {
  const auto original_extra = patchy::test::psd_layer_extra_data(original, layer_index);
  const auto written_extra = patchy::test::psd_layer_extra_data(written, layer_index);
  for (const auto* key : keys) {
    const auto original_block = find_tagged_block(original_extra, key);
    const auto written_block = find_tagged_block(written_extra, key);
    CHECK(original_block.has_value());
    CHECK(written_block.has_value());
    if (*original_block != *written_block) {
      std::fprintf(stderr, "block %s differs: %zu vs %zu bytes\n", key, original_block->size(),
                   written_block->size());
    }
    CHECK(*original_block == *written_block);
  }
}

void psd_vector_untouched_blocks_round_trip_bytes() {
  const auto original = read_fixture_bytes("photoshop-shape-solid.psd");
  const auto document = patchy::psd::DocumentIo::read(original, {});
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  check_vector_blocks_byte_equal(original, written, 1, {"vmsk", "SoCo"});
  // Shape layers keep Photoshop's empty-channel convention on write.
  const auto channels = patchy::test::psd_layer_channel_records(written);
  const auto empty_channels = std::count_if(channels.begin(), channels.end(),
                                            [](const auto& record) { return record.length == 2; });
  CHECK(empty_channels >= 4);  // the shape layer's -1/R/G/B markers

  const auto strokes_original = read_fixture_bytes("photoshop-shape-strokes.psd");
  const auto strokes_document = patchy::psd::DocumentIo::read(strokes_original, {});
  const auto strokes_written = patchy::psd::DocumentIo::write_layered_rgb8(strokes_document);
  for (std::int16_t index = 1; index <= 6; ++index) {
    check_vector_blocks_byte_equal(strokes_original, strokes_written, index, {"vmsk", "SoCo", "vstk"});
  }
  const auto live_original = read_fixture_bytes("photoshop-shape-live-rect.psd");
  const auto live_document = patchy::psd::DocumentIo::read(live_original, {});
  const auto live_written = patchy::psd::DocumentIo::write_layered_rgb8(live_document);
  check_vector_blocks_byte_equal(live_original, live_written, 1, {"vmsk", "SoCo", "vogk", "vowv"});
}

void psd_vector_dirty_regeneration_reproduces_unchanged_bytes() {
  // Marking dirty WITHOUT changing values must regenerate byte-identical
  // blocks: vmsk from exact fixed-point round-trips, SoCo via descriptor
  // patch-in-place. The strongest writer canary available.
  const auto original = read_fixture_bytes("photoshop-shape-solid.psd");
  auto document = patchy::psd::DocumentIo::read(original, {});
  auto* shape = document.find_layer(document.layers()[1].id());
  CHECK(shape != nullptr);
  patchy::mark_layer_vector_block_dirty(*shape);
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  check_vector_blocks_byte_equal(original, written, 1, {"vmsk", "SoCo"});

  const auto live_original = read_fixture_bytes("photoshop-shape-live-rect.psd");
  auto live_document = patchy::psd::DocumentIo::read(live_original, {});
  auto* live = live_document.find_layer(live_document.layers()[1].id());
  patchy::mark_layer_vector_block_dirty(*live);
  const auto live_written = patchy::psd::DocumentIo::write_layered_rgb8(live_document);
  check_vector_blocks_byte_equal(live_original, live_written, 1, {"vmsk"});
}

void psd_vector_move_translates_model_and_round_trips() {
  auto document = read_fixture("photoshop-shape-solid.psd");
  auto* shape = document.find_layer(document.layers()[1].id());
  CHECK(shape != nullptr);
  const auto original_anchor = shape->vector_shape()->path.subpaths[0].anchors[0];
  auto bounds = shape->bounds();
  bounds.x += 5;
  bounds.y += 3;
  shape->set_bounds(bounds);
  patchy::translate_moved_layer_metadata(*shape, 5, 3, document.width(), document.height());
  CHECK(patchy::layer_vector_block_dirty(*shape));

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(written, {});
  const auto* moved = reread.layers()[1].vector_shape();
  CHECK(moved != nullptr);
  const auto& anchor = moved->path.subpaths[0].anchors[0];
  CHECK(std::fabs(anchor.anchor_x - (original_anchor.anchor_x + 5.0)) < 1e-5);
  CHECK(std::fabs(anchor.anchor_y - (original_anchor.anchor_y + 3.0)) < 1e-5);
  CHECK(std::fabs(anchor.in_x - (original_anchor.in_x + 5.0)) < 1e-5);
}

void psd_vector_mask_and_params_write_round_trip() {
  const auto document = read_fixture("photoshop-both-masks.psd");
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(written, {});
  const auto& both = reread.layers()[1];
  CHECK(both.mask().has_value());
  CHECK(both.vector_mask() != nullptr);
  const auto& parameterized = reread.layers()[2];
  const auto* mask = parameterized.vector_mask();
  CHECK(mask != nullptr);
  CHECK(mask->density == 153);
  CHECK(std::fabs(mask->feather - 1.5) < 1e-9);
  CHECK(!parameterized.mask().has_value());

  // A dirtied vector mask regenerates its vmsk with identical bytes when the
  // model is unchanged.
  const auto original_bytes = read_fixture_bytes("photoshop-vector-mask-on-pixel.psd");
  auto vmask_document = patchy::psd::DocumentIo::read(original_bytes, {});
  auto* layer = vmask_document.find_layer(vmask_document.layers()[1].id());
  patchy::mark_layer_vector_block_dirty(*layer);
  const auto vmask_written = patchy::psd::DocumentIo::write_layered_rgb8(vmask_document);
  check_vector_blocks_byte_equal(original_bytes, vmask_written, 1, {"vmsk"});
}

void psd_saved_paths_write_round_trips_and_edits() {
  auto document = read_fixture("photoshop-saved-paths.psd");
  // Untouched: resource payloads re-emit verbatim.
  const auto original_bytes = read_fixture_bytes("photoshop-saved-paths.psd");
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(written, {});
  CHECK(reread.paths().size() == 3);

  // Rename Beta: the resource regenerates under its id with the new name.
  for (auto& path : document.paths()) {
    if (path.name() == "Beta Path") {
      path.set_name("Renamed Path");
    }
  }
  const auto renamed_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto renamed = patchy::psd::DocumentIo::read(renamed_bytes, {});
  bool found_renamed = false;
  for (const auto& path : renamed.paths()) {
    if (path.name() == "Renamed Path") {
      found_renamed = true;
      CHECK(path.resource_id().has_value() && *path.resource_id() == 2001);
      CHECK(path.path().subpaths.size() == 2);
    }
    CHECK(path.name() != "Beta Path");
  }
  CHECK(found_renamed);

  // Delete the clipping path: its resource and the 2999 selector disappear.
  std::optional<patchy::DocumentPathId> alpha_id;
  for (const auto& path : document.paths()) {
    if (path.name() == "Alpha Path") {
      alpha_id = path.id();
    }
  }
  CHECK(alpha_id.has_value());
  CHECK(document.remove_path(*alpha_id));
  const auto deleted_bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto deleted = patchy::psd::DocumentIo::read(deleted_bytes, {});
  CHECK(deleted.paths().size() == 2);
  for (const auto& path : deleted.paths()) {
    CHECK(path.name() != "Alpha Path");
    CHECK(!path.is_clipping_path());
  }
  (void)original_bytes;
}

void psd_saved_paths_reorder_round_trips() {
  // Panel drag-reorder swaps the document order of the saved paths; the
  // writer renumbers the moved paths onto the sorted id set (verbatim payload
  // bytes move with them) so the order survives write -> read.
  auto document = read_fixture("photoshop-saved-paths.psd");
  std::vector<std::string> saved_names;
  for (const auto& path : document.paths()) {
    if (path.kind() == patchy::DocumentPathKind::Saved) {
      saved_names.push_back(path.name());
    }
  }
  CHECK(saved_names == (std::vector<std::string>{"Alpha Path", "Beta Path"}));

  // Reorder exactly like MainWindow::reorder_paths_from_panel: saved paths in
  // the new order, the work path after the block.
  auto& paths = document.paths();
  std::vector<patchy::DocumentPath> reordered;
  for (auto& path : paths) {
    if (path.name() == "Beta Path") {
      reordered.push_back(std::move(path));
    }
  }
  for (auto& path : paths) {
    if (!path.name().empty() && path.name() == "Alpha Path") {
      reordered.push_back(std::move(path));
    }
  }
  for (auto& path : paths) {
    if (path.kind() == patchy::DocumentPathKind::Work) {
      reordered.push_back(std::move(path));
    }
  }
  CHECK(reordered.size() == paths.size());
  paths = std::move(reordered);

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(written, {});
  std::vector<std::string> reread_names;
  std::vector<std::uint16_t> reread_ids;
  for (const auto& path : reread.paths()) {
    if (path.kind() == patchy::DocumentPathKind::Saved) {
      reread_names.push_back(path.name());
      CHECK(path.resource_id().has_value());
      reread_ids.push_back(*path.resource_id());
    }
  }
  CHECK(reread_names == (std::vector<std::string>{"Beta Path", "Alpha Path"}));
  CHECK(reread_ids == (std::vector<std::uint16_t>{2000, 2001}));
  // The moved payloads stayed verbatim: Beta keeps its two subpaths (the
  // donut) and Alpha its clipping-path flag under the new ids.
  for (const auto& path : reread.paths()) {
    if (path.name() == "Beta Path") {
      CHECK(path.path().subpaths.size() == 2);
    }
    if (path.name() == "Alpha Path") {
      CHECK(path.is_clipping_path());
    }
  }
  bool has_work_path = false;
  for (const auto& path : reread.paths()) {
    has_work_path = has_work_path || path.kind() == patchy::DocumentPathKind::Work;
  }
  CHECK(has_work_path);
}

void psd_work_path_saved_as_named_round_trips() {
  // Save Path (Work -> Saved) must drop the stale 1025 resource source so the
  // writer allocates a saved-range id; the old 1025 entry disappears (no
  // phantom work path) and clean siblings keep their ids.
  auto document = read_fixture("photoshop-saved-paths.psd");
  auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->resource_id().has_value() && *work->resource_id() == patchy::kPsdWorkPathResourceId);
  const auto promoted_id = work->id();
  work->set_name("Promoted Path");
  work->set_kind(patchy::DocumentPathKind::Saved);
  CHECK(!work->resource_id().has_value());
  // The UI additionally moves the promoted path to the end (PS placement);
  // mirror that so sibling ids stay put.
  auto& paths = document.paths();
  const auto it = std::find_if(paths.begin(), paths.end(), [promoted_id](const patchy::DocumentPath& path) {
    return path.id() == promoted_id;
  });
  CHECK(it != paths.end());
  std::rotate(it, it + 1, paths.end());

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto reread = patchy::psd::DocumentIo::read(written, {});
  CHECK(reread.paths().size() == 3);
  bool found_promoted = false;
  for (const auto& path : reread.paths()) {
    CHECK(path.kind() == patchy::DocumentPathKind::Saved);  // no phantom work path
    CHECK(path.resource_id().has_value());
    if (path.name() == "Promoted Path") {
      found_promoted = true;
      CHECK(*path.resource_id() >= patchy::kPsdSavedPathResourceFirst);
      CHECK(*path.resource_id() <= patchy::kPsdSavedPathResourceLast);
      CHECK(path.path().subpaths.size() >= 1);
    }
    if (path.name() == "Alpha Path") {
      CHECK(*path.resource_id() == 2000);  // clean siblings keep their ids
    }
    if (path.name() == "Beta Path") {
      CHECK(*path.resource_id() == 2001);
    }
  }
  CHECK(found_promoted);
}

void psd_authored_shape_layer_writes_native_blocks() {
  // A shape layer authored from scratch (no preserved originals) writes the
  // native block set and reopens with an identical model.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Shape 1", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::RoundedRectangle;
  params.left = 8;
  params.top = 10;
  params.right = 40;
  params.bottom = 30;
  params.corner_radii = {2.0, 4.0, 6.0, 8.0};
  patchy::populate_live_shape_box_corners(params);
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination = {params};
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{20, 120, 220};
  content.stroke.enabled = true;
  content.stroke.width = 3.0;
  content.stroke.content.kind = patchy::VectorFillKind::Solid;
  content.stroke.content.color = patchy::RgbColor{200, 40, 40};
  shape.set_vector_shape(content);
  shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(64, 64), nullptr);
  document.add_layer(std::move(shape));

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  // Photoshop acceptance probe: dump the authored bytes for a manual COM check.
  if (const char* dump_path = std::getenv("PATCHY_DUMP_AUTHORED_PSD")) {
    std::ofstream dump(dump_path, std::ios::binary);
    dump.write(reinterpret_cast<const char*>(written.data()),
               static_cast<std::streamsize>(written.size()));
  }
  const auto reread = patchy::psd::DocumentIo::read(written, {});
  const auto* roundtrip = reread.layers()[1].vector_shape();
  CHECK(roundtrip != nullptr);
  CHECK(roundtrip->fill.kind == patchy::VectorFillKind::Solid);
  CHECK(roundtrip->fill.color.blue == 220);
  CHECK(roundtrip->stroke.enabled);
  CHECK(std::fabs(roundtrip->stroke.width - 3.0) < 1e-9);
  CHECK(roundtrip->origination.size() == 1);
  CHECK(roundtrip->origination[0].kind == patchy::LiveShapeKind::RoundedRectangle);
  CHECK(std::fabs(roundtrip->origination[0].corner_radii[3] - 8.0) < 1e-9);
  CHECK(roundtrip->path.subpaths.size() == content.path.subpaths.size());
  const auto flat_original = patchy::Compositor{}.flatten_rgb8(document);
  const auto flat_reread = patchy::Compositor{}.flatten_rgb8(reread);
  const auto metrics = rgb_diff_metrics(flat_original, flat_reread);
  CHECK(metrics.max_channel_delta == 0);
}

patchy::PixelBuffer checker_tile(std::int32_t size, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  patchy::PixelBuffer tile(size, size, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < size; ++y) {
    for (std::int32_t x = 0; x < size; ++x) {
      auto* px = tile.pixel(x, y);
      const bool on = ((x / 2) + (y / 2)) % 2 == 0;
      px[0] = on ? r : 255;
      px[1] = on ? g : 255;
      px[2] = on ? b : 255;
      px[3] = 255;
    }
  }
  return tile;
}

patchy::Layer make_pattern_shape_layer(patchy::Document& document, const char* fill_pattern_id,
                                       const char* stroke_pattern_id) {
  patchy::Layer shape(document.allocate_layer_id(), "Pattern Shape", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  patchy::PathSubpath rect;
  for (const auto& [x, y] : {std::pair{8.0, 8.0}, {56.0, 8.0}, {56.0, 56.0}, {8.0, 56.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    rect.anchors.push_back(anchor);
  }
  content.path.subpaths.push_back(rect);
  content.fill.kind = patchy::VectorFillKind::Pattern;
  content.fill.pattern_id = fill_pattern_id;
  content.fill.pattern_name = "patchy fill pattern";
  // Non-default placement params ride PtFl's Algn/phase/Scl/Angl keys (PS
  // 27.8 order pinned by probe-pattern-params) and must round-trip.
  content.fill.pattern_scale = 1.5;
  content.fill.pattern_angle_degrees = 30.0;
  content.fill.pattern_linked = false;
  content.fill.pattern_phase_x = 10.0;
  content.fill.pattern_phase_y = 20.0;
  if (stroke_pattern_id != nullptr) {
    content.stroke.enabled = true;
    content.stroke.width = 3.0;
    content.stroke.content.kind = patchy::VectorFillKind::Pattern;
    content.stroke.content.pattern_id = stroke_pattern_id;
    content.stroke.content.pattern_name = "patchy stroke pattern";
    content.stroke.content.pattern_scale = 0.5;
    content.stroke.content.pattern_angle_degrees = -45.0;
    content.stroke.content.pattern_linked = false;
    content.stroke.content.pattern_phase_x = -3.0;
    content.stroke.content.pattern_phase_y = 7.0;
  }
  shape.set_vector_shape(content);
  shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  return shape;
}

void psd_pattern_fill_shape_embeds_patt_block() {
  // The bug behind "Could not open ... because of a program error" (July
  // 2026): a shape layer's PtFl (and a vstk pattern stroke paint) referenced
  // a pattern id, but the referenced-pattern collection only looked at layer
  // STYLES, so no global Patt block was written. Photoshop hard-refuses any
  // file whose pattern reference resolves neither in the file nor in its own
  // presets.
  constexpr const char* kFillId = "aaaaaaaa-1111-2222-3333-444444444444";
  constexpr const char* kStrokeId = "bbbbbbbb-5555-6666-7777-888888888888";
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
  patchy::PatternResource fill_pattern;
  fill_pattern.id = kFillId;
  fill_pattern.name = "patchy fill pattern";
  fill_pattern.tile = checker_tile(8, 200, 30, 30);
  document.metadata().patterns.adopt(fill_pattern);
  patchy::PatternResource stroke_pattern;
  stroke_pattern.id = kStrokeId;
  stroke_pattern.name = "patchy stroke pattern";
  stroke_pattern.tile = checker_tile(4, 30, 30, 200);
  document.metadata().patterns.adopt(stroke_pattern);
  document.add_layer(make_pattern_shape_layer(document, kFillId, kStrokeId));
  // A GRADIENT stroke paint (now UI-authorable) rides the same
  // strokeStyleContent writer; round-trip it beside the pattern layers.
  {
    patchy::Layer gradient_stroke(document.allocate_layer_id(), "Gradient Stroke",
                                  patchy::PixelBuffer());
    patchy::VectorShapeContent content;
    patchy::PathSubpath rect;
    for (const auto& [x, y] : {std::pair{20.0, 20.0}, {44.0, 20.0}, {44.0, 44.0}, {20.0, 44.0}}) {
      patchy::PathAnchor anchor;
      anchor.anchor_x = anchor.in_x = anchor.out_x = x;
      anchor.anchor_y = anchor.in_y = anchor.out_y = y;
      rect.anchors.push_back(anchor);
    }
    content.path.subpaths.push_back(rect);
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{200, 60, 40};
    content.stroke.enabled = true;
    content.stroke.width = 4.0;
    content.stroke.content.kind = patchy::VectorFillKind::Gradient;
    content.stroke.content.gradient.type = patchy::LayerStyleGradientType::Linear;
    content.stroke.content.gradient.angle_degrees = 45.0F;
    content.stroke.content.gradient.color_stops = {
        patchy::GradientColorStop{0.0F, patchy::RgbColor{20, 40, 220}, 0.5F},
        patchy::GradientColorStop{1.0F, patchy::RgbColor{240, 240, 40}, 0.5F}};
    content.stroke.content.gradient.alpha_stops = {patchy::GradientAlphaStop{0.0F, 1.0F, 0.5F},
                                                   patchy::GradientAlphaStop{1.0F, 1.0F, 0.5F}};
    gradient_stroke.set_vector_shape(content);
    gradient_stroke.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::update_vector_shape_raster(
        gradient_stroke, patchy::Rect::from_size(document.width(), document.height()),
        &document.metadata().patterns);
    document.add_layer(std::move(gradient_stroke));
  }

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  // Photoshop acceptance probe: dump the authored bytes for a manual COM check.
  if (const char* dump_path = std::getenv("PATCHY_DUMP_PATTERN_PSD")) {
    std::ofstream dump(dump_path, std::ios::binary);
    dump.write(reinterpret_cast<const char*>(written.data()),
               static_cast<std::streamsize>(written.size()));
  }
  const auto patt = find_tagged_block(written, "Patt");
  CHECK(patt.has_value());
  const auto ids = patchy::psd::pattern_ids_in_block(*patt);
  CHECK(std::find(ids.begin(), ids.end(), kFillId) != ids.end());
  CHECK(std::find(ids.begin(), ids.end(), kStrokeId) != ids.end());

  const auto reread = patchy::psd::DocumentIo::read(written, {});
  const auto* fill_resource = reread.metadata().patterns.find(kFillId);
  CHECK(fill_resource != nullptr);
  CHECK(fill_resource->tile.width() == 8);
  const auto* stroke_resource = reread.metadata().patterns.find(kStrokeId);
  CHECK(stroke_resource != nullptr);
  CHECK(stroke_resource->tile.width() == 4);
  const auto* roundtrip = reread.layers()[1].vector_shape();
  CHECK(roundtrip != nullptr);
  CHECK(roundtrip->fill.kind == patchy::VectorFillKind::Pattern);
  CHECK(roundtrip->fill.pattern_id == kFillId);
  CHECK(roundtrip->stroke.content.kind == patchy::VectorFillKind::Pattern);
  CHECK(roundtrip->stroke.content.pattern_id == kStrokeId);
  // Placement params survive on both the fill and the stroke paint.
  CHECK(std::fabs(roundtrip->fill.pattern_scale - 1.5) < 1e-9);
  CHECK(std::fabs(roundtrip->fill.pattern_angle_degrees - 30.0) < 1e-9);
  CHECK(!roundtrip->fill.pattern_linked);
  CHECK(std::fabs(roundtrip->fill.pattern_phase_x - 10.0) < 1e-9);
  CHECK(std::fabs(roundtrip->fill.pattern_phase_y - 20.0) < 1e-9);
  CHECK(std::fabs(roundtrip->stroke.content.pattern_scale - 0.5) < 1e-9);
  CHECK(std::fabs(roundtrip->stroke.content.pattern_angle_degrees + 45.0) < 1e-9);
  CHECK(!roundtrip->stroke.content.pattern_linked);
  CHECK(std::fabs(roundtrip->stroke.content.pattern_phase_x + 3.0) < 1e-9);
  CHECK(std::fabs(roundtrip->stroke.content.pattern_phase_y - 7.0) < 1e-9);
  const auto* gradient_roundtrip = reread.layers()[2].vector_shape();
  CHECK(gradient_roundtrip != nullptr);
  CHECK(gradient_roundtrip->stroke.enabled);
  CHECK(gradient_roundtrip->stroke.content.kind == patchy::VectorFillKind::Gradient);
  CHECK(std::fabs(gradient_roundtrip->stroke.content.gradient.angle_degrees - 45.0F) < 0.01F);
  CHECK(gradient_roundtrip->stroke.content.gradient.color_stops.size() == 2);
  // The rendered pattern pixels survive the round trip exactly.
  const auto flat_original = patchy::Compositor{}.flatten_rgb8(document);
  const auto flat_reread = patchy::Compositor{}.flatten_rgb8(reread);
  CHECK(rgb_diff_metrics(flat_original, flat_reread).max_channel_delta == 0);
}

void psd_pattern_params_probe_render_parity_if_available() {
  // PS 27.8's own render of non-default pattern placement on a full-canvas
  // pattern fill layer (angle 30 + scale 150% + phase (10,20), single-op Mk
  // authoring; local-test-fixtures/vector-probe/probe-pattern-params2.jsx).
  // Pins the reader's param parsing against a real PS file and keeps the
  // PatternTileSampler's calibrated rotation/scale/phase mapping
  // (R(angle) @ (p - anchor) / scale) honest.
  const auto path =
      patchy::test::local_format_fixture_path("vector-probe", "probe-pat-full-combo.psd");
  const auto reference_path =
      patchy::test::local_format_fixture_path("vector-probe", "probe-pat-full-combo.bmp");
  if (!std::filesystem::exists(path) || !std::filesystem::exists(reference_path)) {
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  CHECK(document.layers().size() == 2);
  const auto* fill_content = layer_at(document, 1).vector_shape();
  CHECK(fill_content != nullptr);
  CHECK(fill_content->fill.kind == VectorFillKind::Pattern);
  CHECK(std::fabs(fill_content->fill.pattern_scale - 1.5) < 1e-6);
  CHECK(std::fabs(fill_content->fill.pattern_angle_degrees - 30.0) < 1e-6);
  CHECK(fill_content->fill.pattern_linked);  // Algn omitted = linked default
  CHECK(std::fabs(fill_content->fill.pattern_phase_x - 10.0) < 1e-6);
  CHECK(std::fabs(fill_content->fill.pattern_phase_y - 20.0) < 1e-6);
  // Photoshop resamples ROTATED patterns with its own soft per-cell filter
  // (shrunken cells with light gutters), so pixel-mean parity is the wrong
  // gauge here. Instead pin the placement STRUCTURE: at every reference pixel
  // that is confidently dark or light (4-neighborhood agrees), Patchy's
  // render must classify the same way. A transposed rotation or a wrong
  // anchor scores ~50% on this metric; the calibrated mapping scores ~100%.
  const auto reference_doc = patchy::bmp::DocumentIo::read_file(reference_path);
  const auto& reference = std::as_const(reference_doc).layers().front().pixels();
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  CHECK(flattened.width() == reference.width());
  CHECK(flattened.height() == reference.height());
  const auto classify = [](const std::uint8_t* px) {
    if (px[0] < 70 && px[1] < 70 && px[2] < 70) {
      return 1;  // dark cell
    }
    if (px[0] > 210 && px[1] > 210 && px[2] > 210) {
      return 0;  // light cell
    }
    return -1;  // edge/filtered
  };
  std::int64_t agreed = 0;
  std::int64_t confident = 0;
  for (std::int32_t y = 1; y < reference.height() - 1; ++y) {
    for (std::int32_t x = 1; x < reference.width() - 1; ++x) {
      const auto want = classify(reference.pixel(x, y));
      if (want < 0 || classify(reference.pixel(x - 1, y)) != want ||
          classify(reference.pixel(x + 1, y)) != want ||
          classify(reference.pixel(x, y - 1)) != want ||
          classify(reference.pixel(x, y + 1)) != want) {
        continue;
      }
      const auto* got = flattened.pixel(x, y);
      const auto luminance = (static_cast<int>(got[0]) + got[1] + got[2]) / 3;
      ++confident;
      agreed += (luminance < 128) == (want == 1) ? 1 : 0;
    }
  }
  const auto agreement = confident > 0 ? static_cast<double>(agreed) / static_cast<double>(confident) : 0.0;
  if (agreement < 0.97) {
    write_rgb8_bmp_artifact("psd_pattern_params_probe_patchy", flattened);
    write_rgb8_bmp_artifact("psd_pattern_params_probe_photoshop", reference);
    std::fprintf(stderr, "pattern-params probe: agreement %.4f (%lld/%lld)\n", agreement,
                 static_cast<long long>(agreed), static_cast<long long>(confident));
  }
  CHECK(confident > 1000);
  CHECK(agreement >= 0.97);
}

void psd_pattern_fill_missing_tile_writes_placeholder() {
  // A referenced pattern with no usable tile anywhere (poisoned store entry,
  // or no entry at all) must never leave a dangling reference: the writer
  // embeds a 1x1 fully transparent placeholder instead, which renders as no
  // paint — matching Patchy's missing-pattern render — and adopt() later
  // replaces with the real pattern (pattern_tile_is_unrenderable).
  constexpr const char* kPoisonedId = "cccccccc-1111-2222-3333-444444444444";
  constexpr const char* kAbsentId = "dddddddd-5555-6666-7777-888888888888";
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
  patchy::PatternResource poisoned;
  poisoned.id = kPoisonedId;
  poisoned.name = "poisoned pattern";
  document.metadata().patterns.adopt(poisoned);  // empty tile
  document.add_layer(make_pattern_shape_layer(document, kPoisonedId, kAbsentId));

  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto patt = find_tagged_block(written, "Patt");
  CHECK(patt.has_value());
  const auto ids = patchy::psd::pattern_ids_in_block(*patt);
  CHECK(std::find(ids.begin(), ids.end(), kPoisonedId) != ids.end());
  CHECK(std::find(ids.begin(), ids.end(), kAbsentId) != ids.end());
  const auto decoded = patchy::psd::parse_patterns_block(*patt, nullptr);
  CHECK(decoded.size() == 2);
  for (const auto& resource : decoded) {
    CHECK(resource.tile.width() == 1 && resource.tile.height() == 1);
    CHECK(patchy::pattern_tile_is_unrenderable(resource.tile));
  }

  // Reopening and re-applying the real pattern heals the placeholder entry.
  auto reread = patchy::psd::DocumentIo::read(written, {});
  const auto* placeholder = reread.metadata().patterns.find(kPoisonedId);
  CHECK(placeholder != nullptr);
  CHECK(patchy::pattern_tile_is_unrenderable(placeholder->tile));
  patchy::PatternResource healed;
  healed.id = kPoisonedId;
  healed.name = "healed pattern";
  healed.tile = checker_tile(8, 10, 200, 10);
  reread.metadata().patterns.adopt(healed);
  const auto* after = reread.metadata().patterns.find(kPoisonedId);
  CHECK(after != nullptr);
  CHECK(after->tile.width() == 8);
}

void psd_partial_vogk_is_omitted_full_vogk_kept() {
  // Photoshop refuses to OPEN a file whose vogk keyDescriptorList covers only
  // some vmsk subpath groups (July 2026 byte bisection of a polygon + live
  // ellipse layer). A partially-live layer writes NO vogk/vowv; a fully-live
  // one keeps them.
  const auto make_document = [](bool add_plain_subpath) {
    patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
    patchy::Layer shape(document.allocate_layer_id(), "Mixed Shape", patchy::PixelBuffer());
    patchy::VectorShapeContent content;
    if (add_plain_subpath) {
      patchy::PathSubpath triangle;
      for (const auto& [x, y] : {std::pair{6.0, 6.0}, {30.0, 6.0}, {18.0, 26.0}}) {
        patchy::PathAnchor anchor;
        anchor.anchor_x = anchor.in_x = anchor.out_x = x;
        anchor.anchor_y = anchor.in_y = anchor.out_y = y;
        triangle.anchors.push_back(anchor);
      }
      triangle.shape_group = 0;
      content.path.subpaths.push_back(triangle);
    }
    patchy::LiveShapeParams ellipse;
    ellipse.kind = patchy::LiveShapeKind::Ellipse;
    ellipse.left = 32;
    ellipse.top = 32;
    ellipse.right = 56;
    ellipse.bottom = 52;
    ellipse.index = add_plain_subpath ? 1 : 0;
    for (auto& subpath : patchy::generate_live_shape_subpaths(ellipse)) {
      content.path.subpaths.push_back(std::move(subpath));
    }
    content.origination = {ellipse};
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{40, 180, 90};
    shape.set_vector_shape(content);
    shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(64, 64), nullptr);
    document.add_layer(std::move(shape));
    return document;
  };

  const auto mixed_written = patchy::psd::DocumentIo::write_layered_rgb8(make_document(true));
  // Photoshop acceptance probe: dump the mixed-liveness bytes for a COM check.
  if (const char* dump_path = std::getenv("PATCHY_DUMP_MIXED_PSD")) {
    std::ofstream dump(dump_path, std::ios::binary);
    dump.write(reinterpret_cast<const char*>(mixed_written.data()),
               static_cast<std::streamsize>(mixed_written.size()));
  }
  const auto mixed_extra = patchy::test::psd_layer_extra_data(mixed_written, 1);
  CHECK(!find_tagged_block(mixed_extra, "vogk").has_value());
  CHECK(!find_tagged_block(mixed_extra, "vowv").has_value());
  CHECK(find_tagged_block(mixed_extra, "vmsk").has_value());
  const auto mixed_reread = patchy::psd::DocumentIo::read(mixed_written, {});
  const auto* mixed_shape = mixed_reread.layers()[1].vector_shape();
  CHECK(mixed_shape != nullptr);
  CHECK(mixed_shape->origination.empty());
  CHECK(mixed_shape->path.subpaths.size() == 2);

  const auto live_written = patchy::psd::DocumentIo::write_layered_rgb8(make_document(false));
  const auto live_extra = patchy::test::psd_layer_extra_data(live_written, 1);
  CHECK(find_tagged_block(live_extra, "vogk").has_value());
  CHECK(find_tagged_block(live_extra, "vowv").has_value());
  const auto live_reread = patchy::psd::DocumentIo::read(live_written, {});
  const auto* live_shape = live_reread.layers()[1].vector_shape();
  CHECK(live_shape != nullptr);
  CHECK(live_shape->origination.size() == 1);
  CHECK(live_shape->origination[0].kind == patchy::LiveShapeKind::Ellipse);
}

void psd_damaged_partial_vogk_import_heals_on_resave() {
  // A file that already carries a partial vogk (only pre-fix Patchy could
  // write one; Photoshop refuses to open them) must not survive a Patchy
  // round trip: the reader keeps the raw vogk/vowv out of the preserved
  // blocks and the writer's coverage gate keeps regeneration out too.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("bg", patchy::test::solid_rgba(64, 64, 255, 255, 255, 255));
  patchy::Layer shape(document.allocate_layer_id(), "Live Ellipse", patchy::PixelBuffer());
  patchy::VectorShapeContent content;
  patchy::LiveShapeParams ellipse;
  ellipse.kind = patchy::LiveShapeKind::Ellipse;
  ellipse.left = 10;
  ellipse.top = 10;
  ellipse.right = 50;
  ellipse.bottom = 40;
  content.path.subpaths = patchy::generate_live_shape_subpaths(ellipse);
  content.origination = {ellipse};
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{90, 40, 180};
  shape.set_vector_shape(content);
  shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(64, 64), nullptr);
  document.add_layer(std::move(shape));
  auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);

  // Byte-patch the vmsk subpath group index (0 -> 7) so the vogk entry
  // (keyOriginIndex 0) no longer covers it — the damaged-file shape.
  const std::array<std::uint8_t, 8> vmsk_magic{'8', 'B', 'I', 'M', 'v', 'm', 's', 'k'};
  const auto it = std::search(written.begin(), written.end(), vmsk_magic.begin(), vmsk_magic.end());
  CHECK(it != written.end());
  const auto payload_at = static_cast<std::size_t>(std::distance(written.begin(), it)) + 12U;
  // Payload: u32 version, u32 flags, then 26-byte records (sel 6, sel 8, then
  // the length record); the group index is at record offset 12.
  const auto length_record_at = payload_at + 8U + 2U * 26U;
  const auto index_at = length_record_at + 12U;
  CHECK(patchy::test::read_u32_be_at(written, index_at) == 0U);
  written[index_at + 3U] = 7U;

  const auto damaged = patchy::psd::DocumentIo::read(written, {});
  const auto& healed_layer = damaged.layers()[1];
  CHECK(healed_layer.vector_shape() != nullptr);
  for (const auto& block : healed_layer.unknown_psd_blocks()) {
    CHECK(block.key != "vogk");
    CHECK(block.key != "vowv");
  }
  const auto resaved = patchy::psd::DocumentIo::write_layered_rgb8(damaged);
  const auto resaved_extra = patchy::test::psd_layer_extra_data(resaved, 1);
  CHECK(!find_tagged_block(resaved_extra, "vogk").has_value());
  CHECK(!find_tagged_block(resaved_extra, "vowv").has_value());
  CHECK(find_tagged_block(resaved_extra, "vmsk").has_value());
}

void psd_damaged_pattern_file_resave_is_photoshop_safe_if_available() {
  // The July 2026 user file that Photoshop refused with "program error": a
  // polygon + custom arrow + live ellipse shape layer whose PtFl referenced a
  // pattern with NO global Patt block, plus a partial vogk (ellipse entry
  // only). Resaving through Patchy must produce a file with neither defect.
  const auto path = patchy::test::local_psd_fixture_path("vectors_from_patchy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto written = patchy::psd::DocumentIo::write_layered_rgb8(document);

  const auto extra = patchy::test::psd_layer_extra_data(written, 1);
  CHECK(!find_tagged_block(extra, "vogk").has_value());
  CHECK(!find_tagged_block(extra, "vowv").has_value());
  const auto ptfl = find_tagged_block(extra, "PtFl");
  CHECK(ptfl.has_value());

  const auto patt = find_tagged_block(written, "Patt");
  CHECK(patt.has_value());
  const auto ids = patchy::psd::pattern_ids_in_block(*patt);
  CHECK(std::find(ids.begin(), ids.end(), "1076f0b5-6d90-e4f2-8896-d4f3093299f8") != ids.end());

  if (const char* dump_path = std::getenv("PATCHY_DUMP_RESAVED_USER_PSD")) {
    std::ofstream dump(dump_path, std::ios::binary);
    dump.write(reinterpret_cast<const char*>(written.data()),
               static_cast<std::streamsize>(written.size()));
  }
}

void collect_referenced_pattern_resources_covers_vector_content() {
  // Cross-document layer copies resolve their pattern resources through
  // collect_referenced_pattern_resources; vector fill and stroke paints must
  // ride along like style overlays always did.
  constexpr const char* kFillId = "eeeeeeee-1111-2222-3333-444444444444";
  constexpr const char* kStrokeId = "ffffffff-5555-6666-7777-888888888888";
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  patchy::PatternResource fill_pattern;
  fill_pattern.id = kFillId;
  fill_pattern.tile = checker_tile(4, 1, 2, 3);
  document.metadata().patterns.adopt(fill_pattern);
  patchy::PatternResource stroke_pattern;
  stroke_pattern.id = kStrokeId;
  stroke_pattern.tile = checker_tile(4, 4, 5, 6);
  document.metadata().patterns.adopt(stroke_pattern);
  const auto shape = make_pattern_shape_layer(document, kFillId, kStrokeId);
  std::vector<patchy::PatternResource> resources;
  patchy::collect_referenced_pattern_resources(shape, document.metadata().patterns, resources);
  CHECK(resources.size() == 2);
  const auto has_id = [&resources](const char* id) {
    return std::any_of(resources.begin(), resources.end(),
                       [id](const patchy::PatternResource& resource) { return resource.id == id; });
  };
  CHECK(has_id(kFillId));
  CHECK(has_id(kStrokeId));
}

}  // namespace

std::vector<patchy::test::TestCase> psd_vector_fixtures_tests() {
  return {
      {"psd_shape_solid_fixture_parses_and_renders", psd_shape_solid_fixture_parses_and_renders},
      {"psd_shape_gradient_fixture_parses_and_renders", psd_shape_gradient_fixture_parses_and_renders},
      {"psd_shape_pattern_fixture_parses_and_renders", psd_shape_pattern_fixture_parses_and_renders},
      {"psd_shape_strokes_fixture_parses_and_renders", psd_shape_strokes_fixture_parses_and_renders},
      {"psd_shape_boolean_fixture_combines_and_renders", psd_shape_boolean_fixture_combines_and_renders},
      {"psd_shape_first_ops_fixture_renders", psd_shape_first_ops_fixture_renders},
      {"psd_shape_live_fixture_parses_origination", psd_shape_live_fixture_parses_origination},
      {"psd_vector_mask_fixture_masks_pixels", psd_vector_mask_fixture_masks_pixels},
      {"psd_both_masks_fixture_parses_parameters", psd_both_masks_fixture_parses_parameters},
      {"psd_saved_paths_fixture_populates_document_paths", psd_saved_paths_fixture_populates_document_paths},
      {"psd_shape_psb_fixture_parses_and_renders", psd_shape_psb_fixture_parses_and_renders},
      {"psd_vector_untouched_blocks_round_trip_bytes", psd_vector_untouched_blocks_round_trip_bytes},
      {"psd_vector_dirty_regeneration_reproduces_unchanged_bytes",
       psd_vector_dirty_regeneration_reproduces_unchanged_bytes},
      {"psd_vector_move_translates_model_and_round_trips", psd_vector_move_translates_model_and_round_trips},
      {"psd_vector_mask_and_params_write_round_trip", psd_vector_mask_and_params_write_round_trip},
      {"psd_saved_paths_write_round_trips_and_edits", psd_saved_paths_write_round_trips_and_edits},
      {"psd_saved_paths_reorder_round_trips", psd_saved_paths_reorder_round_trips},
      {"psd_work_path_saved_as_named_round_trips", psd_work_path_saved_as_named_round_trips},
      {"psd_authored_shape_layer_writes_native_blocks", psd_authored_shape_layer_writes_native_blocks},
      {"psd_pattern_fill_shape_embeds_patt_block", psd_pattern_fill_shape_embeds_patt_block},
      {"psd_pattern_fill_missing_tile_writes_placeholder", psd_pattern_fill_missing_tile_writes_placeholder},
      {"psd_partial_vogk_is_omitted_full_vogk_kept", psd_partial_vogk_is_omitted_full_vogk_kept},
      {"psd_damaged_partial_vogk_import_heals_on_resave", psd_damaged_partial_vogk_import_heals_on_resave},
      {"psd_damaged_pattern_file_resave_is_photoshop_safe_if_available",
       psd_damaged_pattern_file_resave_is_photoshop_safe_if_available},
      {"psd_pattern_params_probe_render_parity_if_available",
       psd_pattern_params_probe_render_parity_if_available},
      {"collect_referenced_pattern_resources_covers_vector_content",
       collect_referenced_pattern_resources_covers_vector_content},
  };
}
