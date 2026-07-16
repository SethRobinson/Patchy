#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_shape.hpp"
#include "formats/bmp_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"

#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "psd_test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
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
  // Known divergence (docs/vector-tools.md): PS's fill-layer gradient ramp is
  // S-eased with a span that differs slightly from the overlay-calibrated
  // geometry at non-axis angles; the ramp differs smoothly by a few percent.
  check_flatten_matches_reference(document, "photoshop-shape-gradient.bmp", "psd_vector_gradient", 7.0, 48,
                                  0.85);
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
      {"psd_authored_shape_layer_writes_native_blocks", psd_authored_shape_layer_writes_native_blocks},
  };
}
