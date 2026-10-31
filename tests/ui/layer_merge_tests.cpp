#include <QPushButton>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/smart_filter.hpp"
#include "psd/psd_layer_effects.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_compound.hpp"
#include "core/pixel_tools.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_merge.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_preview_renderer.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/background_workers.hpp"
#include "ui/main_window_shared.hpp"
#include "formats/svg_document_io.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <array>
#include <cstdio>
#include <utility>

using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

namespace {

// A drop shadow that is only on or off; clang warns on {true} (-Wmissing-field-initializers).
LayerDropShadow drop_shadow(bool enabled) {
  LayerDropShadow shadow;
  shadow.enabled = enabled;
  return shadow;
}

VectorShapeContent rectangle(double x, double y, double w, double h, RgbColor color = {20, 80, 140}) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Rectangle;
  params.left = x; params.top = y; params.right = x + w; params.bottom = y + h;
  VectorShapeContent shape;
  shape.path.subpaths = generate_live_shape_subpaths(params);
  shape.fill.color = color;
  return shape;
}

Layer vector_layer(Document& doc, VectorShapeContent shape) {
  Layer result(doc.allocate_layer_id(), "Shape", LayerKind::Pixel);
  result.metadata()[kLayerMetadataVectorShape] = "1";
  result.set_vector_shape(std::move(shape));
  update_vector_shape_raster(result, Rect::from_size(doc.width(), doc.height()), &std::as_const(doc).metadata().patterns);
  return result;
}

Layer pixel_layer(Document& doc, int x, int y, RgbColor color) {
  PixelBuffer pixels(16, 16, PixelFormat::rgba8());
  for (int py = 0; py < 16; ++py) {
    for (int px = 0; px < 16; ++px) {
      auto* p = pixels.pixel(px, py);
      p[0] = color.red; p[1] = color.green; p[2] = color.blue; p[3] = 150;
    }
  }
  Layer layer(doc.allocate_layer_id(), "Bitmap", std::move(pixels));
  layer.set_bounds({x, y, 16, 16});
  return layer;
}

std::vector<LayerId> roots(const Document& doc) {
  std::vector<LayerId> ids;
  for (const auto& layer : doc.layers()) { ids.push_back(layer.id()); }
  return ids;
}

void check_close_images(const QImage& before, const QImage& after, int tolerance = 1) {
  CHECK(!before.isNull() && before.size() == after.size());
  for (int y = 0; y < before.height(); ++y) {
    for (int x = 0; x < before.width(); ++x) {
      const auto a = before.pixelColor(x, y);
      const auto b = after.pixelColor(x, y);
      CHECK(std::abs(a.alpha() - b.alpha()) <= tolerance);
      if (a.alpha() > 1 && b.alpha() > 1) {
        CHECK(std::abs(a.red() - b.red()) <= tolerance);
        CHECK(std::abs(a.green() - b.green()) <= tolerance);
        CHECK(std::abs(a.blue() - b.blue()) <= tolerance);
      }
    }
  }
}

Document grouped_sample() {
  Document doc(128, 96, PixelFormat::rgba8());
  Layer group(doc.allocate_layer_id(), "Mixed group", LayerKind::Group);
  group.set_blend_mode(BlendMode::PassThrough);
  group.set_opacity(0.7F);
  group.add_child(pixel_layer(doc, 3, 4, {180, 20, 30}));
  group.add_child(pixel_layer(doc, 10, 8, {20, 180, 30}));
  group.add_child(vector_layer(doc, rectangle(8, 8, 24, 20)));
  group.add_child(vector_layer(doc, rectangle(20, 18, 24, 20)));
  group.add_child(pixel_layer(doc, 26, 25, {30, 20, 180}));
  group.add_child(vector_layer(doc, rectangle(30, 30, 20, 20, {140, 80, 20})));
  group.add_child(vector_layer(doc, rectangle(40, 40, 20, 20, {140, 80, 20})));
  doc.add_layer(std::move(group));
  return doc;
}

void ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd() {
  const auto doc = grouped_sample();
  const auto original = psd::DocumentIo::write_layered_rgb8(doc);
  const auto revision = doc.layers()[0].content_revision();
  const auto plan = plan_layer_merge(doc, roots(doc));
  CHECK(plan.changed && plan.removed_layers == 3);
  CHECK(plan.vector_layers == 2 && plan.bitmap_layers == 2);
  CHECK(doc.layers()[0].content_revision() == revision);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
  const auto result = render_layer_merge(doc, plan);
  CHECK(result.layers().size() == 1 && result.layers()[0].kind() == LayerKind::Group);
  CHECK(result.layers()[0].opacity() == doc.layers()[0].opacity());
  const auto& children = result.layers()[0].children();
  CHECK(children.size() == 4);
  CHECK(!layer_is_vector_shape(children[0]) && layer_is_vector_shape(children[1]));
  CHECK(!layer_is_vector_shape(children[2]) && layer_is_vector_shape(children[3]));
  CHECK(children[1].vector_shape()->path.subpaths.size() == 2);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(result, true));
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(result));
  CHECK(layer_is_vector_shape(reread.layers()[0].children()[1]));
  CHECK(reread.layers()[0].children()[1].vector_shape()->path.subpaths.size() == 2);
  check_close_images(qimage_from_document(result, true), qimage_from_document(reread, true));
}

void ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations() {
  Document doc(180, 96, PixelFormat::rgba8());
  for (const int x : {8, 94}) {
    auto shape = rectangle(x, 8, 50, 60);
    auto hole = rectangle(x + 12, 20, 20, 28);
    hole.path.subpaths[0].shape_group = 1;
    hole.path.subpaths[0].op = PathCombineOp::Subtract;
    shape.path.subpaths.push_back(hole.path.subpaths[0]);
    shape.stroke.enabled = true;
    shape.stroke.width = 1.25;
    shape.stroke.miter_limit = 2;
    shape.stroke.cap = VectorStrokeCap::Round;
    shape.stroke.join = VectorStrokeJoin::Round;
    shape.stroke.dashes = {2, 1};
    auto layer = vector_layer(doc, shape);
    layer.set_opacity(0.6F);
    doc.add_layer(std::move(layer));
  }
  const auto plan = plan_layer_merge(doc, roots(doc));
  CHECK(plan.changed && plan.vector_layers == 1);
  const auto result = render_layer_merge(doc, plan);
  const auto& shape = *result.layers()[0].vector_shape();
  CHECK(shape.path.subpaths.size() == 4);
  CHECK(shape.path.subpaths[3].op == PathCombineOp::Subtract);
  CHECK(shape.path.subpaths[2].shape_group != shape.path.subpaths[0].shape_group);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(result, true));
  // An independent intersection must not intersect the preceding layer too.
  auto intersect = *std::as_const(doc).layers()[1].vector_shape();
  intersect.path.subpaths[1].op = PathCombineOp::Intersect;
  doc.layers()[1].set_vector_shape(intersect);
  update_vector_shape_raster(doc.layers()[1], Rect::from_size(doc.width(), doc.height()), nullptr);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(render_layer_merge(doc, plan_layer_merge(doc, roots(doc))), true));
  intersect.path_inverted = true;
  doc.layers()[1].set_vector_shape(intersect);
  update_vector_shape_raster(doc.layers()[1], Rect::from_size(doc.width(), doc.height()), nullptr);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(render_layer_merge(doc, plan_layer_merge(doc, roots(doc))), true));
}

void ui_layer_merge_gradients_patterns_and_paint_alignment() {
  for (const auto kind : {VectorFillKind::Gradient, VectorFillKind::Pattern}) {
    Document doc(180, 96, PixelFormat::rgba8());
    PatternResource tile;
    tile.id = "7a5b670b-78fa-4cb8-a617-e13c701588d4";
    tile.name = "Merge texture";
    const auto tile_layer = pixel_layer(doc, 0, 0, {180, 140, 30});
    tile.tile = tile_layer.pixels();
    doc.metadata().patterns.adopt(tile);
    for (const int x : {8, 100}) {
      auto shape = rectangle(x, 8, 50, 60);
      shape.fill.kind = kind;
      shape.fill.gradient.align_with_layer = false;
      shape.fill.gradient.color_stops = {{0, {230, 40, 20}}, {1, {20, 40, 230}}};
      shape.fill.gradient.alpha_stops = {{0, 0.3F}, {1, 0.9F}};
      shape.fill.pattern_id = tile.id;
      shape.fill.pattern_name = tile.name;
      shape.fill.pattern_scale = 0.75;
      shape.fill.pattern_angle_degrees = 25;
      doc.add_layer(vector_layer(doc, shape));
    }
    const auto plan = plan_layer_merge(doc, roots(doc));
    CHECK(plan.changed && plan.vector_layers == 1);
    const auto result = render_layer_merge(doc, plan);
    CHECK(result.layers()[0].vector_shape()->fill.kind == kind);
    check_close_images(qimage_from_document(doc, true), qimage_from_document(result, true));
    const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(result));
    CHECK(reread.layers()[0].vector_shape()->fill.kind == kind);
    check_close_images(qimage_from_document(result, true), qimage_from_document(reread, true), 2);
    if (kind == VectorFillKind::Gradient) {
      for (auto& layer : doc.layers()) {
        auto shape = *std::as_const(layer).vector_shape();
        shape.fill.gradient.align_with_layer = true;
        layer.set_vector_shape(shape);
      }
    } else {
      set_layer_effects_reference_point(doc.layers()[1], 10, 4);
    }
    for (auto& layer : doc.layers()) { update_vector_shape_raster(layer, Rect::from_size(doc.width(), doc.height()), &std::as_const(doc).metadata().patterns); }
    const auto aligned = render_layer_merge(doc, plan_layer_merge(doc, roots(doc)));
    CHECK(aligned.layers().size() == 1);
    check_close_images(qimage_from_document(doc, true), qimage_from_document(aligned, true));
    check_close_images(qimage_from_document(aligned, true), qimage_from_document(psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(aligned)), true), 2);
  }
}

void ui_layer_merge_group_and_vector_type_choices() {
  Document doc(128, 96, PixelFormat::rgba8());
  for (const int x : {4, 64}) {
    Layer group(doc.allocate_layer_id(), "Folder", LayerKind::Group);
    group.set_blend_mode(BlendMode::PassThrough);
    group.add_child(vector_layer(doc, rectangle(x, 8, 24, 24)));
    group.add_child(vector_layer(doc, rectangle(x + 10, 18, 24, 24)));
    doc.add_layer(std::move(group));
  }
  const auto per_group = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), {true, true, true}));
  CHECK(per_group.layers().size() == 2 && per_group.layers()[0].children().size() == 1);
  const auto across = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), {true, false, true}));
  CHECK(across.layers().size() == 1 && across.layers()[0].vector_shape() != nullptr);
  CHECK(across.layers()[0].vector_shape()->path.subpaths.size() == 4);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(across, true));
  doc.layers()[1].set_opacity(0.5F);
  const auto isolated = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), {true, false, true}));
  CHECK(isolated.layers().size() == 2 && isolated.layers()[1].kind() == LayerKind::Group);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(isolated, true));
  Document colors(96, 80, PixelFormat::rgba8());
  colors.add_layer(vector_layer(colors, rectangle(8, 8, 40, 40, {255, 0, 0})));
  colors.add_layer(vector_layer(colors, rectangle(28, 28, 40, 40, {0, 0, 255})));
  CHECK(plan_layer_merge(colors, roots(colors)).vector_layers == 1);
  const auto bottom_paint = render_layer_merge(colors, plan_layer_merge(colors, roots(colors), {true, true, false}));
  CHECK(bottom_paint.layers().size() == 1);
  CHECK(bottom_paint.layers()[0].vector_shape()->parts.size() == 2);
  check_close_images(qimage_from_document(colors, true), qimage_from_document(bottom_paint, true));
  auto gradient = rectangle(18, 18, 30, 30);
  gradient.fill.kind = VectorFillKind::Gradient;
  colors.add_layer(vector_layer(colors, gradient));
  CHECK(plan_layer_merge(colors, roots(colors)).vector_layers == 2);
  CHECK(plan_layer_merge(colors, roots(colors), {true, true, false}).vector_layers == 1);
  const auto bitmap = render_layer_merge(colors, plan_layer_merge(colors, roots(colors), {false, false, true}));
  CHECK(bitmap.layers().size() == 1 && !layer_is_vector_shape(bitmap.layers()[0]));
  check_close_images(qimage_from_document(colors, true), qimage_from_document(bitmap, true));
}

void ui_layer_merge_protected_layers_and_unselected_order_are_barriers() {
  Document doc(96, 80, PixelFormat::rgba8());
  for (int i = 0; i < 8; ++i) {
    doc.add_layer(vector_layer(doc, rectangle(5 + i * 2, 5 + i * 2, 40, 40)));
  }
  doc.layers()[0].set_visible(false);
  doc.layers()[1].set_lock_flags(kLayerLockImagePixels);
  LayerMask mask;
  mask.default_color = 200;
  mask.pixels = PixelBuffer(1, 1, PixelFormat::gray8());
  mask.bounds = {0, 0, 1, 1};
  doc.layers()[2].set_mask(mask);
  doc.layers()[3].layer_style().drop_shadows.push_back(drop_shadow(true));
  doc.layers()[4].set_blend_mode(BlendMode::Multiply);
  doc.layers()[6].set_clipped(true);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  CHECK(!plan_layer_merge(doc, roots(doc)).changed);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  Document ordered(96, 80, PixelFormat::rgba8());
  ordered.add_layer(vector_layer(ordered, rectangle(8, 8, 40, 40)));
  ordered.add_layer(pixel_layer(ordered, 24, 24, {230, 20, 20}));
  ordered.add_layer(vector_layer(ordered, rectangle(28, 28, 40, 40)));
  const auto ids = roots(ordered);
  CHECK(!plan_layer_merge(ordered, {ids[0], ids[2]}).changed);
  Document separated(180, 80, PixelFormat::rgba8());
  separated.add_layer(vector_layer(separated, rectangle(8, 8, 20, 20)));
  separated.add_layer(vector_layer(separated, rectangle(78, 8, 20, 20, {230, 20, 20})));
  separated.add_layer(vector_layer(separated, rectangle(140, 8, 20, 20)));
  const auto collected = plan_layer_merge(separated, roots(separated));
  CHECK(collected.changed && collected.vector_layers == 1);
  const auto collected_doc = render_layer_merge(separated, collected);
  check_close_images(qimage_from_document(separated, true), qimage_from_document(collected_doc, true));
  Layer protected_group(doc.allocate_layer_id(), "Locked contents", LayerKind::Group);
  protected_group.set_blend_mode(BlendMode::PassThrough);
  protected_group.add_child(doc.layers()[1]);
  Document protected_doc(96, 80, PixelFormat::rgba8());
  protected_doc.add_layer(std::move(protected_group));
  CHECK(!plan_layer_merge(protected_doc, roots(protected_doc), {false, false, true}).changed);
  Document unparsed(96, 80, PixelFormat::rgba8());
  auto unknown_vector = pixel_layer(unparsed, 0, 0, {});
  unknown_vector.metadata()[kLayerMetadataVectorLock] = "unparsed";
  unparsed.add_layer(std::move(unknown_vector));
  unparsed.add_layer(pixel_layer(unparsed, 8, 8, {}));
  CHECK(merge_selection_contains_vectors(unparsed, roots(unparsed)));
  CHECK(!plan_layer_merge(unparsed, roots(unparsed)).changed);
  Document custom(96, 80, PixelFormat::rgba8());
  custom.add_layer(vector_layer(custom, rectangle(8, 8, 40, 40)));
  auto opaque = rectangle(20, 20, 40, 40);
  LiveShapeParams annotation;
  annotation.kind = LiveShapeKind::Custom;
  annotation.raw_descriptor = {1, 2, 3};
  opaque.origination.push_back(annotation);
  custom.add_layer(vector_layer(custom, opaque));
  // The curves merge; an unmodeled live annotation cannot follow new ids.
  const auto custom_merged = render_layer_merge(custom, plan_layer_merge(custom, roots(custom)));
  CHECK(custom_merged.layers().size() == 1);
  CHECK(custom_merged.layers()[0].vector_shape()->origination.empty());
  CHECK(std::as_const(custom).layers()[1].vector_shape()->origination[0].raw_descriptor == annotation.raw_descriptor);
}

void run_merge_script(MainWindow& window, const QString& script) {
  ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("merge-layers-test");
  (void)window.script_engine_host().run_source(script, options);
  CHECK(process_events_until([&] { return !window.script_engine_host().run_active(); }));
  CHECK(!window.script_engine_host().last_run_had_error());
}

Document single_vector_sample() {
  Document doc(160, 120, PixelFormat::rgba8());
  PatternResource tile;
  tile.id = "81ec3610-d086-4e1e-96b3-49b9c079b3bb";
  tile.name = "Merge pattern";
  tile.tile = pixel_layer(doc, 0, 0, {60, 170, 90}).pixels();
  doc.metadata().patterns.adopt(tile);
  for (int i = 0; i < 3; ++i) {
    LiveShapeParams params;
    params.kind = LiveShapeKind::RoundedRectangle;
    params.left = 10 + i * 22; params.top = 10 + i * 20;
    params.right = params.left + 65; params.bottom = params.top + 50;
    params.corner_radii = {3, 8, 12, 5};
    params.index = 7; // Every source deliberately starts with the same group id.
    populate_live_shape_box_corners(params);
    VectorShapeContent shape;
    shape.path.subpaths = generate_live_shape_subpaths(params);
    shape.origination.push_back(params);
    shape.fill.color = {220, 65, 30};
    if (i == 1) {
      shape.fill.kind = VectorFillKind::Gradient;
      shape.fill.gradient.color_stops = {{0, {10, 50, 210}}, {1, {250, 190, 20}}};
      shape.fill.gradient.alpha_stops = {{0, 1.0F}, {1, 0.6F}};
      shape.fill.gradient.angle_degrees = 23;
    } else if (i == 2) {
      shape.fill.kind = VectorFillKind::Pattern;
      shape.fill.pattern_id = tile.id; shape.fill.pattern_name = tile.name;
      shape.fill.pattern_scale = 0.8; shape.fill.pattern_angle_degrees = 15;
    }
    shape.stroke.enabled = true;
    shape.stroke.width = 2 + i;
    shape.stroke.content.color = {40, 30, 90};
    auto layer = vector_layer(doc, shape);
    layer.set_name("Styled " + std::to_string(i + 1));
    layer.set_opacity(0.8F); layer.set_fill_opacity(0.9F);
    layer.layer_style().drop_shadows.push_back(drop_shadow(true));
    layer.layer_style().drop_shadows[0].distance = static_cast<float>(4 + i * 3);
    layer.layer_style().drop_shadows.push_back(drop_shadow(false));
    layer.layer_style().blend_interior_elements = i == 2;
    set_layer_effects_reference_point(layer, i * 3, i * 5);
    doc.add_layer(std::move(layer));
    if (i == 0) { doc.add_layer(pixel_layer(doc, 30, 30, {10, 210, 80})); }
  }
  return doc;
}

std::vector<LayerId> styled_ids(const Document& doc) {
  return {doc.layers()[0].id(), doc.layers()[2].id(), doc.layers()[3].id()};
}

LayerMergeOptions single_vector_options(std::optional<LayerId> effects = {}) {
  LayerMergeOptions result;
  result.single_vector = true;
  result.effects_source = effects;
  return result;
}

void ui_layer_merge_single_vector_paints_effects_order_and_psd() {
  // Start with imported native descriptors as well as modeled effects.
  const auto doc = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(single_vector_sample()));
  const auto ids = styled_ids(doc);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  CHECK(!plan_layer_merge(doc, ids).changed);
  for (const auto source : {std::optional<LayerId>{}, std::optional<LayerId>{ids.back()}}) {
    const auto plan = plan_layer_merge(doc, ids, single_vector_options(source));
    CHECK(plan.changed && plan.blockers.empty() && plan.changes_stacking_order);
    CHECK(plan.vector_layers == 1 && plan.result_ids == std::vector<LayerId>{ids[0]});
    const auto result = render_layer_merge(doc, plan);
    CHECK(result.layers().size() == 2 && result.layers()[0].id() == ids[0]);
    CHECK(result.layers()[1].id() == doc.layers()[1].id());
    CHECK(result.layers()[0].name() == doc.layers()[0].name());
    const auto& output = result.layers()[0];
    const auto& shape = *output.vector_shape();
    CHECK(shape.parts.size() == 3 && shape.origination.size() == 3);
    for (std::size_t i = 0; i < ids.size(); ++i) {
      const auto& original = *doc.find_layer(ids[i]);
      const auto& content = *original.vector_shape();
      CHECK(shape.parts[i].fill == content.fill && shape.parts[i].stroke == content.stroke);
      CHECK(shape.parts[i].opacity == original.opacity());
      CHECK(shape.parts[i].fill_opacity == original.fill_opacity());
      CHECK(shape.parts[i].pattern_anchor == layer_effects_reference_point(original));
      auto path = shape.path.subpaths[i];
      path.shape_group = content.path.subpaths[0].shape_group;
      CHECK(path == content.path.subpaths[0]);
      auto origin = shape.origination[i];
      origin.index = content.origination[0].index;
      CHECK(origin == content.origination[0]);
      if (i > 0) { CHECK(shape.parts[i].groups != shape.parts[i - 1].groups); }
    }
    CHECK(output.opacity() == 1 && output.fill_opacity() == 1);
    if (source) {
      CHECK(psd::photoshop_lfx2_layer_style_payload(output.layer_style()) ==
            psd::photoshop_lfx2_layer_style_payload(doc.find_layer(*source)->layer_style()));
      CHECK(output.layer_style().blend_interior_elements);
      CHECK(layer_effects_reference_point(output) == layer_effects_reference_point(*doc.find_layer(*source)));
      CHECK(std::any_of(output.unknown_psd_blocks().begin(), output.unknown_psd_blocks().end(),
                        [](const auto& block) { return block.key == "lfx2"; }));
    } else {
      CHECK(output.layer_style().drop_shadows.empty());
      CHECK(std::none_of(output.unknown_psd_blocks().begin(), output.unknown_psd_blocks().end(),
                        [](const auto& block) { return block.key == "lfx2" || block.key == "lrFX" ||
                                                      block.key == "plFX" || block.key == "lmfx"; }));
      Document expected = doc;
      expected.layers().clear();
      for (const auto id : ids) {
        Layer layer = *doc.find_layer(id);
        layer.layer_style() = {};
        clear_layer_psd_style_source(layer);
        expected.add_layer(std::move(layer));
      }
      expected.add_layer(doc.layers()[1]);
      check_close_images(qimage_from_document(expected, true), qimage_from_document(result, true));
    }
    for (const bool psb : {false, true}) {
      const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(result, {psb}));
      CHECK(reread.layers().size() == 2 && layer_is_compound_vector(reread.layers()[0]));
      CHECK(reread.layers()[0].vector_shape()->parts.size() == 3);
      CHECK(reread.layers()[0].layer_style().drop_shadows.size() ==
            (source ? doc.find_layer(*source)->layer_style().drop_shadows.size() : 0U));
      if (source) {
        // PSD import models enabled shadows; the preserved descriptor also
        // carries disabled instances and must survive this transfer verbatim.
        const auto& blocks = reread.layers()[0].unknown_psd_blocks();
        for (const auto& block : doc.find_layer(*source)->unknown_psd_blocks()) {
          if (block.key == "lfx2") {
            CHECK(std::any_of(blocks.begin(), blocks.end(), [&](const auto& candidate) {
              return candidate.key == block.key && candidate.payload == block.payload;
            }));
          }
        }
      }
      check_close_images(qimage_from_document(result, true), qimage_from_document(reread, true), 2);
      std::filesystem::create_directories("test-artifacts");
      const auto path = std::filesystem::path("test-artifacts") /
          (std::string("single-vector-") + (source ? "source-effects" : "remove-effects") + (psb ? ".psb" : ".psd"));
      psd::DocumentIo::write_layered_rgb8_file(result, path, {psb});
    }
  }
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
}

void ui_layer_merge_single_vector_protections_and_group_boundaries() {
  Document basic(96, 80, PixelFormat::rgba8());
  basic.add_layer(vector_layer(basic, rectangle(5, 5, 40, 40)));
  basic.add_layer(vector_layer(basic, rectangle(30, 20, 40, 40)));
  const auto check_blocked = [&](Document doc, LayerMergeBlocker reason) {
    const auto plan = plan_layer_merge(doc, roots(doc), single_vector_options());
    CHECK(!plan.changed && std::any_of(plan.blockers.begin(), plan.blockers.end(),
                                     [&](const auto& issue) { return issue.reason == reason; }));
    CHECK(!layer_merge_blocker_messages(doc, plan).isEmpty());
    bool threw = false;
    try { (void)render_layer_merge(doc, plan); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
  };
  for (const auto flag : {kLayerLockPosition, kLayerLockImagePixels, kLayerLockTransparentPixels}) {
    auto doc = basic; doc.layers()[0].set_lock_flags(flag); check_blocked(doc, LayerMergeBlocker::Locked);
  }
  auto doc = basic; doc.layers()[0].set_visible(false); check_blocked(doc, LayerMergeBlocker::Hidden);
  doc = basic; doc.layers()[1].set_clipped(true); check_blocked(doc, LayerMergeBlocker::Clipping);
  LayerMask mask;
  mask.bounds = {0, 0, 1, 1}; mask.pixels = PixelBuffer(1, 1, PixelFormat::gray8());
  doc = basic; doc.layers()[0].set_mask(mask); check_blocked(doc, LayerMergeBlocker::Mask);
  doc = basic; doc.layers()[0].set_vector_mask(LayerVectorMask{}); check_blocked(doc, LayerMergeBlocker::Mask);
  doc = basic; doc.layers()[0].set_smart_filter_stack(SmartFilterStack{}); check_blocked(doc, LayerMergeBlocker::Filters);
  doc = basic; doc.layers()[0].set_blend_mode(BlendMode::Multiply); check_blocked(doc, LayerMergeBlocker::Blending);
  doc = basic; doc.layers()[0].set_blend_if_payload({1}); check_blocked(doc, LayerMergeBlocker::Blending);
  doc = basic; doc.layers()[0].set_restricted_channels(1); check_blocked(doc, LayerMergeBlocker::Blending);
  doc = basic; doc.layers()[0].metadata()[kLayerMetadataVectorLock] = "unparsed";
  check_blocked(doc, LayerMergeBlocker::UnsupportedVector);
  doc = basic; doc.add_layer(pixel_layer(doc, 0, 0, {})); check_blocked(doc, LayerMergeBlocker::NotVector);
  for (const bool feather : {false, true}) {
    doc = basic;
    auto shape = *std::as_const(doc).layers()[0].vector_shape();
    if (feather) { shape.feather = 1; } else { shape.density = 128; }
    doc.layers()[0].set_vector_shape(shape); check_blocked(doc, LayerMergeBlocker::VectorEdges);
  }
  doc = render_layer_merge(basic, plan_layer_merge(basic, roots(basic), single_vector_options()));
  doc.layers()[0].set_opacity(0.7F);
  doc.add_layer(vector_layer(doc, rectangle(50, 40, 20, 20)));
  check_blocked(doc, LayerMergeBlocker::OpacityBoundary);
  CHECK(!plan_layer_merge(basic, roots(basic), single_vector_options(99999)).changed);
  CHECK(!plan_layer_merge(basic, {basic.layers()[0].id()}, single_vector_options()).changed);

  doc = basic;
  Layer group(doc.allocate_layer_id(), "Enclosing style", LayerKind::Group);
  group.set_blend_mode(BlendMode::Normal);
  group.set_opacity(0.6F);
  group.layer_style().drop_shadows.push_back(drop_shadow(true));
  group.children() = std::as_const(doc).layers();
  doc.layers().clear(); doc.add_layer(group);
  const auto plan = plan_layer_merge(doc, roots(doc), single_vector_options());
  CHECK(plan.changed && plan.blockers.empty());
  const auto inside = render_layer_merge(doc, plan);
  CHECK(inside.layers()[0].children().size() == 1 && inside.layers()[0].opacity() == 0.6F);
  CHECK(inside.layers()[0].layer_style().drop_shadows.size() == 1);
  doc.layers()[0].set_lock_flags(kLayerLockPosition); check_blocked(doc, LayerMergeBlocker::Locked);
  doc.layers()[0].set_lock_flags(kLayerLockNone);
  doc.add_layer(vector_layer(doc, rectangle(45, 35, 25, 25)));
  check_blocked(doc, LayerMergeBlocker::GroupBoundary);
  doc.layers()[0].set_opacity(1); doc.layers()[0].layer_style() = {};
  doc.layers()[0].set_blend_mode(BlendMode::PassThrough);
  const auto across = render_layer_merge(doc, plan_layer_merge(doc, roots(doc), single_vector_options()));
  CHECK(across.layers().size() == 1 && across.layers()[0].vector_shape()->parts.size() == 3);
}

void ui_vector_preview_compound_effect_halos_match_cropped_view() {
  const auto sample = single_vector_sample();
  const auto result = render_layer_merge(sample, plan_layer_merge(sample, styled_ids(sample),
                                         single_vector_options(styled_ids(sample).back())));
  const auto scene = build_vector_preview_scene(result);
  const auto native = render_vector_preview(scene, {{160, 120}, 1.0, {}});
  CHECK(native.fallback == VectorPreviewFallback::None);
  check_close_images(native.image, qimage_from_document(result, true), 2);
  const VectorPreviewView full{{520, 420}, 3.0, {23, 17}};
  const auto large = render_vector_preview(scene, full);
  const auto crop = render_vector_preview(scene, {{180, 160}, full.scale, full.offset - QPointF(110, 90)});
  CHECK(large.fallback == VectorPreviewFallback::None && crop.fallback == VectorPreviewFallback::None);
  check_close_images(crop.image, large.image.copy(110, 90, 180, 160));
}

void ui_layer_merge_single_vector_dialog_preview_cancel_and_history() {
  MainWindow window;
  show_window(window);
  window.add_document_session(single_vector_sample(), QStringLiteral("Single vector merge"));
  auto& doc = MainWindowTestAccess::document(window);
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list);
  list->clearSelection();
  for (const auto id : styled_ids(doc)) {
    require_layer_item(*list, QString::fromStdString(std::as_const(doc).find_layer(id)->name()))->setSelected(true);
  }
  const auto original = psd::DocumentIo::write_layered_rgb8(doc);
  const auto undo = MainWindowTestAccess::active_session_undo_depth(window);
  const auto dirty = MainWindowTestAccess::active_session_is_modified(window);
  for (const bool accept : {false, true}) {
    QTimer::singleShot(0, [&] {
      try {
        auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog")); CHECK(dialog);
        auto* single = dialog->findChild<QCheckBox*>(QStringLiteral("mergeSingleVectorCheck"));
        auto* effects = dialog->findChild<QComboBox*>(QStringLiteral("mergeVectorEffectsCombo"));
        auto* source = dialog->findChild<QComboBox*>(QStringLiteral("mergeEffectsSourceCombo"));
        auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("mergePreviewCheck"));
        auto* preview = static_cast<ZoomableImagePreview*>(dialog->findChild<QWidget*>(QStringLiteral("mergeVectorPreview")));
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        CHECK(single && effects && source && check && preview && !ok->isEnabled());
        single->setChecked(true);
        CHECK(!dialog->findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->isEnabled());
        CHECK(!dialog->findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"))->isEnabled());
        CHECK(dialog->findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"))->text().contains("Stacking"));
        // Change the policy while the first render is in flight. Only the final
        // source is eligible for display or acceptance.
        effects->setCurrentIndex(1); source->setCurrentIndex(0); source->setCurrentIndex(2);
        CHECK(!ok->isEnabled());
        CHECK(process_events_until([&] { return ok->isEnabled(); }, 15000));
        auto* summary = dialog->findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"));
        CHECK(preview->mapTo(dialog, QPoint()).x() >= summary->mapTo(dialog, QPoint(summary->width(), 0)).x());
        CHECK(preview->mapTo(dialog, QPoint(preview->width(), 0)).x() <= dialog->width());
        const auto merged_preview = preview->image(); CHECK(!merged_preview.isNull());
        check->setChecked(false);
        CHECK(!preview->image().isNull() && preview->image() != merged_preview);
        check->setChecked(true); CHECK(preview->image() == merged_preview);
        CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
        CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo);
        save_widget_artifact("ui_layer_merge_single_vector", *dialog);
        if (accept) { ok->click(); } else { dialog->reject(); }
      } catch (...) { (void)unwind_non_modal_dialog_loop(std::current_exception()); }
    });
    require_action(window, "layerMergeDownAction")->trigger();
    if (!accept) {
      CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo);
      CHECK(MainWindowTestAccess::active_session_is_modified(window) == dirty);
    }
  }
  CHECK(std::as_const(doc).layers().size() == 2);
  CHECK(std::as_const(doc).layers()[0].layer_style().drop_shadows[0].distance == 10);
  CHECK(list->selectedItems().size() == 1);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo + 1);
  const auto merged = psd::DocumentIo::write_layered_rgb8(doc);
  MainWindowTestAccess::undo(window); CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
  MainWindowTestAccess::redo(window); CHECK(psd::DocumentIo::write_layered_rgb8(doc) == merged);

  const auto sample = single_vector_sample();
  std::optional<Document> prepared;
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog")); CHECK(dialog);
      dialog->findChild<QCheckBox*>(QStringLiteral("mergeSingleVectorCheck"))->setChecked(true);
      dialog->reject(); // Close before the worker's queued completion.
    } catch (...) { (void)unwind_non_modal_dialog_loop(std::current_exception()); }
  });
  CHECK(!show_layer_merge_dialog(nullptr, sample, styled_ids(sample), false, &prepared));
  CHECK(!prepared);
  CHECK(process_events_until([] { return tracked_background_worker_count() == 0; }, 15000));
  QApplication::processEvents();
}

void ui_layer_merge_single_vector_script_validation_and_undo() {
  MainWindow window; show_window(window);
  window.add_document_session(single_vector_sample(), QStringLiteral("Script vector merge"));
  auto& doc = MainWindowTestAccess::document(window);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto undo = MainWindowTestAccess::active_session_undo_depth(window);
  run_merge_script(window, QStringLiteral(R"JS(
    const doc = app.activeDocument;
    const vectors = [doc.layers[0], doc.layers[2], doc.layers[3]];
    for (const options of [{singleVector:1}, {singleVector:true, effectsFrom:4},
                           {effectsFrom:vectors[0]}, {singleVector:true, effectsFrom:doc.layers[1]}]) {
      let threw = false;
      try { doc.mergeLayers(vectors, options); } catch (e) { threw = true; }
      if (!threw) throw new Error('invalid vector policy accepted');
    }
    let threw = false;
    try { doc.mergeLayers(doc.layers, {singleVector:true}); } catch (e) { threw = true; }
    if (!threw) throw new Error('bitmap silently discarded');
  )JS"));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo);
  for (const bool keep_effects : {false, true}) {
    run_merge_script(window, QStringLiteral(R"JS(
      const doc = app.activeDocument;
      const vectors = [doc.layers[0], doc.layers[2], doc.layers[3]];
      const options = {singleVector:true};
      if (%1) options.effectsFrom = vectors[2];
      const output = doc.mergeLayers(vectors, options);
      if (output.length !== 1 || output[0].getShape().parts.length !== 3 || doc.layers.length !== 2)
        throw new Error('wrong vector merge result');
    )JS").arg(keep_effects ? QStringLiteral("true") : QStringLiteral("false")));
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo + 1);
    CHECK(std::as_const(doc).layers()[0].layer_style().drop_shadows.size() == (keep_effects ? 2U : 0U));
    MainWindowTestAccess::undo(window);
    CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  }
}

void ui_merge_visible_copy_explicit_vector_mode_keeps_originals() {
  auto sample = single_vector_sample();
  sample.layers().erase(sample.layers().begin() + 1);
  const auto source = sample;
  const auto ids = roots(source);
  MainWindow window; show_window(window);
  window.add_document_session(std::move(sample), QStringLiteral("Copy vectors"));
  auto& doc = MainWindowTestAccess::document(window);
  const auto original = psd::DocumentIo::write_layered_rgb8(doc);
  const auto undo = MainWindowTestAccess::active_session_undo_depth(window);
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog")); CHECK(dialog);
      dialog->findChild<QCheckBox*>(QStringLiteral("mergeSingleVectorCheck"))->setChecked(true);
      auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
      CHECK(process_events_until([&] { return ok->isEnabled(); }, 15000));
      ok->click();
    } catch (...) { (void)unwind_non_modal_dialog_loop(std::current_exception()); }
  });
  require_action(window, "layerMergeVisibleAction")->trigger();
  CHECK(std::as_const(doc).layers().size() == 4);
  for (const auto id : ids) {
    const auto& layer = *std::as_const(doc).find_layer(id);
    CHECK(!layer.visible() && layer.vector_shape() == source.find_layer(id)->vector_shape());
    CHECK(layer.layer_style().drop_shadows.size() == 2);
  }
  const auto& copy = std::as_const(doc).layers().back();
  CHECK(copy.visible() && layer_is_compound_vector(copy));
  CHECK(copy.vector_shape()->parts.size() == 3 && copy.layer_style().drop_shadows.empty());
  CHECK(std::find(ids.begin(), ids.end(), copy.id()) == ids.end());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo + 1);
  MainWindowTestAccess::undo(window);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == original);
}

void ui_layer_merge_dialog_explains_separate_effects() {
  Document doc(160, 80, PixelFormat::rgba8());
  const std::array<std::string, 4> names{"Styled rectangle", "Styled star", "Styled <triangle>",
                                        "Unselected shape"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    auto layer = vector_layer(doc, rectangle(10 + 30 * static_cast<int>(i), 10, 20, 20));
    layer.set_name(names[i]);
    layer.layer_style().drop_shadows.push_back(drop_shadow(true));
    doc.add_layer(std::move(layer));
  }
  auto ids = roots(doc);
  ids.pop_back();
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog"));
      CHECK(dialog != nullptr);
      auto* keep = dialog->findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"));
      auto* types = dialog->findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"));
      auto* details = dialog->findChild<QPlainTextEdit*>(QStringLiteral("mergeLayersEffectsDetails"));
      auto* buttons = dialog->findChild<QDialogButtonBox*>();
      CHECK(keep && types && details && buttons);
      CHECK(keep->text() == QStringLiteral("Keep vector layers editable"));
      types->setChecked(false);
      CHECK(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
      CHECK(details->isVisible() && details->isReadOnly());
      CHECK(details->toPlainText().contains(QStringLiteral("stay separate in a vector merge")));
      for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(details->toPlainText().contains(QString::fromStdString(names[i])));
      }
      CHECK(!details->toPlainText().contains(QString::fromStdString(names.back())));
      save_widget_artifact("ui_layer_merge_separate_effects", *dialog);
      keep->setChecked(false);
      CHECK(!details->isVisible());
      CHECK(buttons->button(QDialogButtonBox::Ok)->isEnabled());
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"))->text().contains(
          QStringLiteral("0 vector layers, 1 bitmap layers")));
      keep->setChecked(true);
      CHECK(details->isVisible());
      CHECK(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
      dialog->reject();
    } catch (...) { (void)unwind_non_modal_dialog_loop(std::current_exception()); }
  });
  CHECK(!show_layer_merge_dialog(nullptr, doc, ids).has_value());
}

void ui_layer_merge_dialog_cancel_accept_and_history() {
  MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = grouped_sample();
  doc.set_active_layer(std::as_const(doc).layers()[0].id());
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  canvas->set_zoom_centered(4.0);
  canvas->set_vector_preview_enabled(true);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  for (const bool accept : {false, true}) {
    bool saw = false;
    QTimer::singleShot(0, [&] {
      try {
        auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog"));
        CHECK(dialog != nullptr);
        CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"))->isChecked());
        CHECK(!dialog->findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->isChecked());
        dialog->findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->setChecked(true);
        CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"))->isChecked());
        CHECK(dialog->findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"))->text().contains(QStringLiteral("2 vector layers")));
        // The vector-types choice only applies while vectors are kept, so it
        // greys out (and must look greyed out: the theme paints a disabled
        // checkbox with the disabled field tokens, otherwise it reads as a
        // checkbox that refuses to toggle) and comes back with the option.
        auto* keep = dialog->findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"));
        auto* types = dialog->findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"));
        CHECK(types->isEnabled());
        const auto enabled_look = types->grab().toImage();
        keep->setChecked(false);
        CHECK(!types->isEnabled());
        CHECK(types->isChecked());
        CHECK(types->grab().toImage() != enabled_look);
        keep->setChecked(true);
        CHECK(types->isEnabled());
        CHECK(types->grab().toImage() == enabled_look);
        CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
        CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
        save_widget_artifact("ui_layer_merge_dialog", *dialog);
        saw = true;
        if (accept) { dialog->accept(); } else { dialog->reject(); }
      } catch (...) {
        (void)unwind_non_modal_dialog_loop(std::current_exception());
      }
    });
    require_action(window, "layerMergeDownAction")->trigger();
    CHECK(saw);
    if (!accept) {
      CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
      CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
    }
  }
  CHECK(std::as_const(doc).layers()[0].children().size() == 4);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  CHECK(canvas->vector_preview_status().contains(QStringLiteral("sharp vector view")));
  const auto merged = psd::DocumentIo::write_layered_rgb8(doc);
  MainWindowTestAccess::undo(window);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  MainWindowTestAccess::redo(window);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == merged);
}

void ui_layer_merge_script_validation_noop_and_undo() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = grouped_sample();
  require_canvas(window)->set_document(&doc);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  run_merge_script(window, QStringLiteral(R"JS(
    const doc = app.activeDocument;
    const group = doc.layers[0];
    for (const options of [{bogus:true}, {keepVectors:1}, null, [], false]) {
      let threw = false;
      try { doc.mergeLayers([group], options); } catch (e) { threw = true; }
      if (!threw) throw new Error('invalid options accepted');
    }
    for (const layers of [[], [null], [group, null]]) {
      let threw = false;
      try { doc.mergeLayers(layers); } catch (e) { threw = true; }
      if (!threw) throw new Error('invalid layers accepted');
    }
    const result = doc.mergeLayers([group.children[2]]);
    if (result.length !== 1) throw new Error('single layer');
  )JS"));
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
  run_merge_script(window, QStringLiteral(R"JS(
    const doc = app.activeDocument;
    const result = doc.mergeLayers([doc.layers[0]], {keepVectors:true, withinGroups:true, separateVectorTypes:true});
    if (result.length !== 4) throw new Error('wrong output count');
    if (doc.layers[0].children.length !== 4) throw new Error('folder was flattened');
  )JS"));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  MainWindowTestAccess::undo(window);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
}

void ui_layer_merge_little_everywhere_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  patchy::test::skip_unless_exists(path);
  const auto doc = psd::DocumentIo::read_file(path);
  QElapsedTimer timer;
  timer.start();
  const auto plan = plan_layer_merge(doc, roots(doc), {true, true, true});
  const auto planning_ms = timer.elapsed();
  std::printf("Little-Everywhere plan: %zu removals, %zu vectors, %zu bitmaps, %zu other layers\n",
              plan.removed_layers, plan.vector_layers, plan.bitmap_layers, plan.kept_layers);
  CHECK(plan.changed && plan.removed_layers > 100);
  const auto merged = render_layer_merge(doc, plan);
  CHECK(plan.bitmap_layers == 0 && plan.vector_layers > 0 && plan.vector_layers < 200);
  const auto verify_groups = [&](const auto& self, const std::vector<Layer>& originals) -> void {
    for (const auto& group : originals) {
      if (group.kind() != LayerKind::Group) { continue; }
      const auto* output = merged.find_layer(group.id());
      CHECK(output != nullptr && output->kind() == LayerKind::Group);
      if (!group.children().empty() && std::all_of(group.children().begin(), group.children().end(), layer_is_vector_shape)) {
        CHECK(output->children().size() == 1);
        CHECK(layer_is_vector_shape(output->children()[0]));
      }
      self(self, group.children());
    }
  };
  verify_groups(verify_groups, doc.layers());
  CHECK(layer_tree_count(merged.layers()) + plan.removed_layers == layer_tree_count(doc.layers()));
  std::printf("Little-Everywhere merge: %zu -> %zu layers; %zu vectors; planning %lld ms; total %lld ms\n",
              layer_tree_count(doc.layers()), layer_tree_count(merged.layers()), plan.vector_layers,
              static_cast<long long>(planning_ms), static_cast<long long>(timer.elapsed()));
  check_close_images(qimage_from_document(doc, true), qimage_from_document(merged, true), 2);
  auto mixed = doc;
  mixed.add_layer(pixel_layer(mixed, 0, 0, {80, 100, 120}));
  const auto mixed_plan = plan_layer_merge(mixed, roots(mixed), {true, true, true});
  CHECK(mixed_plan.changed && mixed_plan.vector_layers == 153 && mixed_plan.bitmap_layers == 1);
  const auto before_scene = build_vector_preview_scene(doc);
  const auto after_scene = build_vector_preview_scene(merged);
  const auto find_bench = [&](const auto& self, const std::vector<Layer>& layers) -> const Layer* {
    for (const auto& layer : layers) {
      if (layer.name() == "Slatted bench") { return &layer; }
      if (const auto* child = self(self, layer.children())) { return child; }
    }
    return nullptr;
  };
  const auto* bench = find_bench(find_bench, doc.layers());
  CHECK(bench != nullptr);
  const auto bounds = layer_render_bounds(*bench);
  const QPointF center(bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5);
  for (const auto& view : {VectorPreviewView{{doc.width(), doc.height()}, 1.0, {}},
                           VectorPreviewView{{1200, 900}, 4.0, QPointF(600, 450) - center * 4.0}}) {
    const auto before = render_vector_preview(before_scene, view);
    const auto after = render_vector_preview(after_scene, view);
    CHECK(before.fallback == VectorPreviewFallback::None && after.fallback == VectorPreviewFallback::None);
    std::printf("Little-Everywhere merged preview %.2fx: %.1f ms, peak buffers %llu bytes\n",
                view.scale, after.elapsed_ms, static_cast<unsigned long long>(after.peak_raster_bytes));
    const auto prefix = QStringLiteral("test-artifacts/layer-merge-little-%1").arg(view.scale);
    CHECK(before.image.save(prefix + QStringLiteral("-before.png")));
    CHECK(after.image.save(prefix + QStringLiteral("-after.png")));
    check_close_images(before.image, after.image, 2);
  }
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(merged));
  const auto restored = plan_layer_merge(reread, roots(reread), {true, true, true});
  CHECK(restored.vector_layers == plan.vector_layers && restored.bitmap_layers == 0);
}

void ui_layer_merge_compound_geometry_and_processing() {
  MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(160, 128, PixelFormat::rgba8());
  auto a = rectangle(16, 16, 55, 55, {220, 40, 20});
  a.stroke.enabled = true;
  a.stroke.width = 0.7;
  a.stroke.content.color = {25, 50, 80};
  auto b = rectangle(35, 25, 55, 55, {20, 150, 210});
  b.stroke.enabled = true;
  b.stroke.width = 3.2;
  b.stroke.join = VectorStrokeJoin::Round;
  b.stroke.dashes = {2, 1};
  b.stroke.content.color = {150, 80, 20};
  doc.add_layer(vector_layer(doc, a));
  auto front = vector_layer(doc, b);
  front.set_opacity(0.6F);
  doc.add_layer(std::move(front));
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(process_events_until([&] { return canvas->render_settled(); }));
  const auto before = qimage_from_document(doc, true);
  const auto before_zoom = render_vector_preview(build_vector_preview_scene(doc), {{800, 600}, 4.5, {-20, -10}});
  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", "0");
  const auto overlays = canvas->render_cache_diagnostics().processing_overlays_shown;
  const auto plan = plan_layer_merge(doc, roots(doc), {true, true, true});
  // A zero display delay exercises feedback without slowing ordinary runs.
  auto merged = render_layer_merge_with_processing(canvas, doc, plan);
  CHECK(merged.layers().size() == 1 && layer_is_compound_vector(merged.layers()[0]));
  CHECK(!canvas->processing_operation_active());
  CHECK(canvas->render_cache_diagnostics().processing_overlays_shown > overlays);
  check_close_images(before, qimage_from_document(merged, true), 2);
  const auto after_zoom = render_vector_preview(build_vector_preview_scene(merged), {{800, 600}, 4.5, {-20, -10}});
  check_close_images(before_zoom.image, after_zoom.image, 2);
  CHECK(after_zoom.peak_raster_bytes <= kVectorPreviewRasterBudget);
  auto faded = merged;
  faded.layers()[0].set_fill_opacity(0.4F);
  const auto faded_native = qimage_from_document(faded, true);
  const auto faded_preview = render_vector_preview(build_vector_preview_scene(faded), {{160, 128}, 1.0, {}});
  CHECK(faded_preview.fallback == VectorPreviewFallback::None);
  check_close_images(faded_native, faded_preview.image, 2);
  const auto faded_read = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(faded));
  CHECK(faded_read.layers().size() == 1 && layer_is_compound_vector(faded_read.layers()[0]));
  CHECK(std::abs(faded_read.layers()[0].fill_opacity() - 0.4F) < 0.005F);
  check_close_images(faded_native, qimage_from_document(faded_read, true), 2);
  const std::array<double, 6> matrix{1.25, 0, 0, 1.25, 5, 8};
  for (auto& layer : doc.layers()) {
    transform_layer_vector_data(doc, layer, matrix, Rect::from_size(160, 128), 1.25);
  }
  transform_layer_vector_data(merged, merged.layers()[0], matrix, Rect::from_size(160, 128), 1.25);
  check_close_images(qimage_from_document(doc, true), qimage_from_document(merged, true), 2);
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(merged));
  CHECK(reread.layers().size() == 1 && layer_is_compound_vector(reread.layers()[0]));
  CHECK(reread.layers()[0].vector_shape()->parts.size() == 2);
  check_close_images(qimage_from_document(merged, true), qimage_from_document(reread, true), 2);
  // SVG retains both native paints when its normal representability checks
  // permit them. Use plain solid geometry to isolate the compound expansion.
  Document svg_doc(100, 80, PixelFormat::rgba8());
  svg_doc.add_layer(vector_layer(svg_doc, rectangle(4, 4, 35, 35, {255, 0, 0})));
  svg_doc.add_layer(vector_layer(svg_doc, rectangle(14, 14, 35, 35, {0, 0, 255})));
  const auto svg_merged = render_layer_merge(svg_doc, plan_layer_merge(svg_doc, roots(svg_doc)));
  const auto svg_bytes = svg::DocumentIo::write(svg_merged);
  const std::string svg_text(svg_bytes.begin(), svg_bytes.end());
  CHECK(svg_text.find("<image") == std::string::npos);
  const auto svg_read = svg::DocumentIo::read(svg_bytes);
  check_close_images(qimage_from_document(svg_doc, true), qimage_from_document(svg_read, true), 2);
  // A direct point edit only moves the selected part, leaving other paints.
  auto edited = *std::as_const(merged).layers()[0].vector_shape();
  const auto group = edited.parts[0].groups[0];
  std::erase_if(edited.path.subpaths, [group](const auto& path) { return path.shape_group == group; });
  merged.layers()[0].set_vector_shape(std::move(edited));
  update_vector_shape_raster(merged.layers()[0], Rect::from_size(160, 128), nullptr);
  CHECK(qimage_from_document(merged, true).pixelColor(26, 30).alpha() == 0);
}

void ui_layer_selection_count_includes_collapsed_descendants() {
  Document doc(32, 32, PixelFormat::rgba8());
  Layer group(doc.allocate_layer_id(), "Group", LayerKind::Group);
  const auto group_id = group.id();
  group.add_child(Layer(doc.allocate_layer_id(), "Visible", PixelBuffer(8, 8, PixelFormat::rgba8())));
  Layer nested(doc.allocate_layer_id(), "Nested", LayerKind::Group);
  Layer hidden(doc.allocate_layer_id(), "Hidden", PixelBuffer(8, 8, PixelFormat::rgba8()));
  hidden.set_visible(false);
  hidden.set_lock_flags(kLayerLockAll);
  nested.add_child(std::move(hidden));
  group.add_child(std::move(nested));
  doc.add_layer(std::move(group));
  doc.add_layer(Layer(doc.allocate_layer_id(), "Empty", LayerKind::Group));
  doc.add_layer(Layer(doc.allocate_layer_id(), "Outside", PixelBuffer(8, 8, PixelFormat::rgba8())));
  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(doc), QStringLiteral("Recursive selection count"));
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list && list->count() == 6);
  const auto undo = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  const auto select = [&](const QString& name) {
    list->setCurrentItem(require_layer_item(*list, name), QItemSelectionModel::ClearAndSelect);
  };
  const auto toggle_group = [&] {
    auto* row = list->itemWidget(require_layer_item(*list, QStringLiteral("Group")));
    auto* disclosure = row->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
    CHECK(disclosure);
    disclosure->click();
    QApplication::processEvents();
  };
  select(QStringLiteral("Group"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("4 layers selected"));
  require_layer_item(*list, QStringLiteral("Visible"))->setSelected(true);
  require_layer_item(*list, QStringLiteral("Nested"))->setSelected(true);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("4 layers selected"));
  list->selectAll();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("6 layers selected"));
  toggle_group();
  CHECK(list->count() == 3);
  select(QStringLiteral("Outside"));
  list->selectAll();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("6 layers selected"));
  select(QStringLiteral("Group"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("4 layers selected"));
  toggle_group();
  CHECK(list->count() == 6);
  select(QStringLiteral("Nested"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("2 layers selected"));
  select(QStringLiteral("Empty"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("1 layer selected"));
  select(QStringLiteral("Outside"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("1 layer selected"));
  auto* filter = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(filter);
  filter->setText(QStringLiteral("Visible"));
  CHECK(list->count() == 2);
  select(QStringLiteral("Visible"));
  select(QStringLiteral("Group"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("4 layers selected"));
  // The API's active-layer reveal has a separate status refresh, including when
  // the row was already selected and Qt emits no selection-change signal.
  ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("selection-count");
  window.statusBar()->showMessage(QStringLiteral("Before reveal"));
  (void)window.script_engine_host().run_source(QStringLiteral(
      "app.activeDocument.activeLayer = app.activeDocument.getLayer('%1');").arg(group_id), options);
  CHECK(process_events_until([&] { return !window.script_engine_host().run_active(); }));
  CHECK(!window.script_engine_host().last_run_had_error());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("4 layers selected"));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo);
  CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
}

void ui_layer_selection_count_little_everywhere_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  patchy::test::skip_unless_exists(path);
  auto doc = psd::DocumentIo::read_file(path);
  const auto total = layer_tree_count(std::as_const(doc).layers());
  CHECK(total == 2056);
  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(doc), QStringLiteral("Little-Everywhere count"));
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list);
  const auto select_all = [&] {
    list->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
    list->selectAll();
    CHECK(window.statusBar()->currentMessage() == QStringLiteral("2056 layers selected"));
  };
  const auto toggle_all = [&] {
    QToolButton* disclosure = nullptr;
    for (int row = 0; row < list->count() && disclosure == nullptr; ++row) {
      disclosure = list->itemWidget(list->item(row))->findChild<QToolButton*>(
          QStringLiteral("layerFolderDisclosureButton"));
    }
    CHECK(disclosure);
    const auto point = disclosure->rect().center();
    send_mouse(*disclosure, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton,
               Qt::ControlModifier | Qt::AltModifier);
    send_mouse(*disclosure, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton,
               Qt::ControlModifier | Qt::AltModifier);
    QApplication::processEvents();
  };
  select_all();
  toggle_all();
  select_all();
  const auto rows = list->count();
  toggle_all();
  select_all();
  CHECK(list->count() != rows);
  std::printf("Little-Everywhere selected count: %zu layers, expanded or collapsed\n", total);
}

void ui_layer_selection_little_everywhere_shift_range_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  patchy::test::skip_unless_exists(path);
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = psd::DocumentIo::read_file(path);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr && list->count() == 2056);
  list->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
  list->scrollToBottom();
  CHECK(process_events_until([&] { return canvas->render_settled(); }, 30000));
  const auto undo = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  auto* last = list->itemWidget(list->item(list->count() - 1))->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(last != nullptr);
  QElapsedTimer timer;
  timer.start();
  send_mouse(*last, QEvent::MouseButtonPress, last->rect().center(), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*last, QEvent::MouseButtonRelease, last->rect().center(), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  const auto elapsed = timer.elapsed();
  std::printf("Little-Everywhere Shift-select: %d rows, %lld ms\n", list->count(), static_cast<long long>(elapsed));
  CHECK(list->selectedItems().size() == list->count());
  CHECK(elapsed < 4000);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo);
  CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
  CHECK(!canvas->processing_operation_active());
  // The actual three-checkbox dialog must offer a useful merge on this file.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog"));
      CHECK(dialog != nullptr);
      dialog->findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->setChecked(true);
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"))->isChecked());
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"))->isChecked());
      CHECK(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"))->text().contains(QStringLiteral("153 vector layers")));
      save_widget_artifact("ui_layer_merge_little_all_options", *dialog);
      saw_dialog = true;
      dialog->accept();
    } catch (...) { (void)unwind_non_modal_dialog_loop(std::current_exception()); }
  });
  require_action(window, "layerMergeDownAction")->trigger();
  CHECK(saw_dialog);
  CHECK(layer_tree_count(std::as_const(doc).layers()) == 305);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo + 1);
  MainWindowTestAccess::undo(window);
  CHECK(layer_tree_count(std::as_const(doc).layers()) == 2056);
  MainWindowTestAccess::redo(window);
  CHECK(layer_tree_count(std::as_const(doc).layers()) == 305);
}

void drive_visible_copy_dialog(MainWindow& window, const std::function<void(QDialog&)>& driver) {
  bool saw = false;
  std::exception_ptr failure;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &window, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("mergeLayersDialog"));
    try {
      CHECK(dialog != nullptr);
      saw = true;
      CHECK(dialog->windowTitle().contains(QStringLiteral("(Copy)")));
      driver(*dialog);
    } catch (...) {
      failure = std::current_exception();
      if (dialog != nullptr) { dialog->reject(); }
    }
  });
  timer.start(0);
  require_action(window, "layerMergeVisibleAction")->trigger();
  if (failure) { std::rethrow_exception(failure); }
  CHECK(saw);
}

void ui_merge_visible_copy_bitmap_preserves_alpha_and_history() {
  for (const bool opaque_background : {false, true}) {
    MainWindow window;
    show_window(window);
    auto& doc = MainWindowTestAccess::document(window);
    // RGB document metadata does not imply that its visible content is opaque.
    doc = Document(48, 40, PixelFormat::rgb8());
    PixelBuffer background(48, 40, PixelFormat::rgb8());
    background.clear(255);
    doc.add_pixel_layer("Background", std::move(background)).set_visible(opaque_background);
    Layer group(doc.allocate_layer_id(), "Translucent group", LayerKind::Group);
    group.set_blend_mode(BlendMode::Normal);
    group.set_opacity(0.5F);
    group.add_child(pixel_layer(doc, 4, 4, {180, 30, 60}));
    auto masked = pixel_layer(doc, 12, 8, {20, 150, 90});
    LayerMask mask;
    mask.default_color = 128;
    mask.bounds = {12, 8, 1, 1};
    mask.pixels = PixelBuffer(1, 1, PixelFormat::gray8());
    mask.pixels.clear(0);
    masked.set_mask(std::move(mask));
    group.add_child(std::move(masked));
    doc.add_layer(std::move(group));
    const Document original = doc;
    const auto expected = qimage_from_document(original, true);
    CHECK(expected.pixelColor(0, 0).alpha() == (opaque_background ? 255 : 0));
    if (!opaque_background) {
      const auto pixel = expected.pixelColor(5, 5);
      CHECK(pixel.alpha() > 0 && pixel.alpha() < 255);
      CHECK(pixel.red() == 180 && pixel.green() == 30 && pixel.blue() == 60);
    }
    auto* canvas = require_canvas(window);
    canvas->set_document(&doc);
    MainWindowTestAccess::refresh_layer_ui(window);
    const auto history = MainWindowTestAccess::active_session_undo_depth(window);
    require_action(window, "layerMergeVisibleAction")->trigger();
    CHECK(std::as_const(doc).layers().size() == original.layers().size() + 1);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
    const auto& copy = std::as_const(doc).layers().back();
    const auto copy_id = copy.id();
    CHECK(copy.pixels().format() == PixelFormat::rgba8());
    CHECK(copy.bounds().x == 0 && copy.bounds().y == 0);
    CHECK(copy.bounds().width == doc.width() && copy.bounds().height == doc.height());
    CHECK(doc.active_layer_id() == copy_id);
    check_close_images(expected, render_layer_isolated(doc, copy), 0);
    for (std::size_t i = 0; i < original.layers().size(); ++i) {
      const auto& source = std::as_const(doc).layers()[i];
      CHECK(source.id() == original.layers()[i].id());
      CHECK(source.visible() == original.layers()[i].visible());
      CHECK(source.content_revision() == original.layers()[i].content_revision());
    }
    CHECK(!canvas->processing_operation_active());
    MainWindowTestAccess::undo(window);
    CHECK(std::as_const(doc).layers().size() == original.layers().size());
    check_close_images(expected, qimage_from_document(doc, true), 0);
    MainWindowTestAccess::redo(window);
    CHECK(std::as_const(doc).layers().back().id() == copy_id);
    check_close_images(expected, render_layer_isolated(doc, std::as_const(doc).layers().back()), 0);
  }
}

void ui_merge_visible_copy_bitmap_preserves_fully_transparent_canvas() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(24, 20, PixelFormat::rgba8());
  PixelBuffer empty(24, 20, PixelFormat::rgba8());
  empty.clear(0);
  doc.add_pixel_layer("Empty", std::move(empty));
  require_canvas(window)->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  require_action(window, "layerMergeVisibleAction")->trigger();
  CHECK(std::as_const(doc).layers().size() == 2);
  const auto& copy = std::as_const(doc).layers().back();
  CHECK(copy.pixels().format() == PixelFormat::rgba8());
  for (int y = 0; y < doc.height(); ++y) {
    for (int x = 0; x < doc.width(); ++x) { CHECK(copy.pixels().pixel(x, y)[3] == 0); }
  }
}

void ui_merge_visible_copy_dialog_preserves_sources_and_history() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = grouped_sample();
  Layer hidden(doc.allocate_layer_id(), "Hidden folder", LayerKind::Group);
  hidden.set_visible(false);
  hidden.add_child(vector_layer(doc, rectangle(0, 0, 120, 90, {240, 0, 0})));
  doc.add_layer(std::move(hidden));
  auto base = pixel_layer(doc, 5, 5, {0, 255, 0});
  base.set_visible(false);
  doc.add_layer(std::move(base));
  auto clipped = vector_layer(doc, rectangle(0, 0, 120, 90, {0, 255, 0}));
  clipped.set_clipped(true);
  doc.add_layer(std::move(clipped));
  const Document original = doc;
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  const auto modified = MainWindowTestAccess::active_session_is_modified(window);
  drive_visible_copy_dialog(window, [&](QDialog& dialog) { dialog.reject(); });
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
  CHECK(MainWindowTestAccess::active_session_is_modified(window) == modified);
  drive_visible_copy_dialog(window, [&](QDialog& dialog) {
    CHECK(dialog.findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"))->isChecked());
    CHECK(dialog.findChild<QCheckBox*>(QStringLiteral("mergeSeparateVectorTypesCheck"))->isChecked());
    CHECK(dialog.findChild<QCheckBox*>(QStringLiteral("mergeHideOriginalsCheck"))->isChecked());
    dialog.findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->setChecked(true);
    CHECK(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
    save_widget_artifact("ui_merge_visible_copy_dialog", dialog);
    dialog.accept();
  });
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(std::as_const(doc).layers().size() == original.layers().size() + 1);
  const auto& copy = std::as_const(doc).layers().back();
  CHECK(copy.kind() == LayerKind::Group && copy.children().size() == 4);
  CHECK(layer_is_compound_vector(copy.children()[1]) && layer_is_compound_vector(copy.children()[3]));
  CHECK(doc.active_layer_id() == copy.id());
  for (std::size_t i = 0; i < original.layers().size(); ++i) { CHECK(!std::as_const(doc).layers()[i].visible()); }
  auto originals = doc;
  originals.layers().resize(original.layers().size());
  for (std::size_t i = 0; i < original.layers().size(); ++i) {
    originals.layers()[i].set_visible(original.layers()[i].visible());
  }
  CHECK(psd::DocumentIo::write_layered_rgb8(originals) == bytes);
  check_close_images(qimage_from_document(original, true), qimage_from_document(doc, true), 2);
  CHECK(!canvas->processing_operation_active());
  MainWindowTestAccess::undo(window);
  CHECK(psd::DocumentIo::write_layered_rgb8(doc) == bytes);
  MainWindowTestAccess::redo(window);
  check_close_images(qimage_from_document(original, true), qimage_from_document(doc, true), 2);
}

void ui_merge_visible_copy_single_vector_raster_and_visibility_choices() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(96, 72, PixelFormat::rgba8());
  auto shape = vector_layer(doc, rectangle(4, 8, 55, 50));
  shape.set_opacity(0.5F);
  doc.add_layer(std::move(shape));
  require_canvas(window)->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  const Document original = doc;
  drive_visible_copy_dialog(window, [&](QDialog& dialog) {
    CHECK(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
    dialog.findChild<QCheckBox*>(QStringLiteral("mergeHideOriginalsCheck"))->setChecked(false);
    dialog.accept();
  });
  CHECK(std::as_const(doc).layers().size() == 2);
  CHECK(std::as_const(doc).layers()[0].visible() && layer_is_vector_shape(std::as_const(doc).layers()[1]));
  CHECK(std::as_const(doc).layers()[0].id() != std::as_const(doc).layers()[1].id());
  MainWindowTestAccess::undo(window);
  for (const bool within : {false, true}) {
    drive_visible_copy_dialog(window, [&](QDialog& dialog) {
      dialog.findChild<QCheckBox*>(QStringLiteral("mergeKeepVectorsCheck"))->setChecked(false);
      dialog.findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->setChecked(within);
      dialog.accept();
    });
    CHECK(std::as_const(doc).layers().size() == 2);
    CHECK(layer_is_vector_shape(std::as_const(doc).layers()[0]) && !std::as_const(doc).layers()[0].visible());
    CHECK(!layer_is_vector_shape(std::as_const(doc).layers()[1]));
    check_close_images(qimage_from_document(original, true), qimage_from_document(doc, true), 2);
    MainWindowTestAccess::undo(window);
  }
}

void ui_merge_visible_copy_little_everywhere_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  patchy::test::skip_unless_exists(path);
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = psd::DocumentIo::read_file(path);
  const Document original = doc;
  require_canvas(window)->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  drive_visible_copy_dialog(window, [&](QDialog& dialog) {
    dialog.findChild<QCheckBox*>(QStringLiteral("mergeWithinGroupsCheck"))->setChecked(true);
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("mergeLayersSummaryLabel"))->text().contains(QStringLiteral("153 vector layers")));
    dialog.accept();
  });
  CHECK(layer_tree_count(std::as_const(doc).layers()) == 2362);
  CHECK(layer_tree_count(std::as_const(doc).layers().back().children()) == 305);
  check_close_images(qimage_from_document(original, true), qimage_from_document(doc, true), 2);
  // Both complete vector copies exceed Photoshop's 8000 native records after
  // open-stroke expansion. Refuse the invalid export without changing history.
  bool rejected = false;
  try { (void)psd::DocumentIo::write_layered_rgb8(doc); }
  catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("8000") != std::string::npos; }
  CHECK(rejected);
  auto copy_only = doc;
  copy_only.layers().clear();
  copy_only.add_layer(std::as_const(doc).layers().back());
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(copy_only));
  CHECK(layer_tree_count(reread.layers()) == 306);
  check_close_images(qimage_from_document(original, true), qimage_from_document(reread, true), 2);
  MainWindowTestAccess::undo(window);
  CHECK(layer_tree_count(std::as_const(doc).layers()) == 2056);
}

// Image > Flatten Image (GitHub issue 97, Photoshop semantics): hidden layers need a
// confirmation and are discarded, everything visible (vectors included) composites onto
// white into one opaque, locked "Background", and one Undo restores the layers.
void ui_flatten_image_composites_onto_white_discards_hidden_and_undoes() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(48, 40, PixelFormat::rgba8());
  PixelBuffer empty(48, 40, PixelFormat::rgba8());
  empty.clear(0);
  doc.add_pixel_layer("Transparent", std::move(empty));
  doc.add_layer(pixel_layer(doc, 4, 4, {180, 30, 60}));
  doc.add_layer(vector_layer(doc, rectangle(24, 20, 16, 12)));
  auto hidden = pixel_layer(doc, 30, 4, {0, 255, 0});
  hidden.set_visible(false);
  doc.add_layer(std::move(hidden));
  const Document original = doc;
  const auto composite = qimage_from_document(original, true).convertToFormat(QImage::Format_RGBA8888);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);

  bool cancel_seen = false;
  QTimer::singleShot(0, [&cancel_seen] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("flattenDiscardHiddenMessageBox")));
    CHECK(box != nullptr);
    cancel_seen = true;
    box->button(QMessageBox::Cancel)->click();
  });
  require_action(window, "imageFlattenAction")->trigger();
  CHECK(cancel_seen);
  CHECK(std::as_const(doc).layers().size() == original.layers().size());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);

  bool ok_seen = false;
  QTimer::singleShot(0, [&ok_seen] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("flattenDiscardHiddenMessageBox")));
    CHECK(box != nullptr);
    ok_seen = true;
    box->button(QMessageBox::Ok)->click();
  });
  require_action(window, "imageFlattenAction")->trigger();
  CHECK(ok_seen);
  CHECK(!canvas->processing_operation_active());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(std::as_const(doc).layers().size() == 1);
  const auto& flat = std::as_const(doc).layers().front();
  CHECK(flat.name() == "Background");
  CHECK(flat.pixels().format() == PixelFormat::rgb8());
  CHECK(flat.bounds().x == 0 && flat.bounds().y == 0);
  CHECK(flat.bounds().width == doc.width() && flat.bounds().height == doc.height());
  CHECK(layer_locks_position(flat) && layer_locks_transparent_pixels(flat) && !layer_locks_image_pixels(flat));
  CHECK(doc.active_layer_id() == flat.id());
  for (int y = 0; y < doc.height(); ++y) {
    for (int x = 0; x < doc.width(); ++x) {
      const auto source = composite.pixelColor(x, y);
      const auto alpha = source.alphaF();
      const auto* px = flat.pixels().pixel(x, y);
      const std::array<int, 3> expected{source.red(), source.green(), source.blue()};
      for (std::size_t c = 0; c < 3U; ++c) {
        const auto want = static_cast<int>(std::lround(expected[c] * alpha + 255.0 * (1.0 - alpha)));
        CHECK(std::abs(static_cast<int>(px[c]) - want) <= 1);
      }
    }
  }
  CHECK(flat.pixels().pixel(0, 0)[0] == 255 && flat.pixels().pixel(0, 0)[2] == 255);  // transparent -> white
  CHECK(flat.pixels().pixel(34, 8)[1] == 255 && flat.pixels().pixel(34, 8)[0] == 255);  // hidden green discarded
  CHECK(flat.pixels().pixel(30, 26)[2] > 100);  // the vector rectangle rasterized

  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).layers().size() == original.layers().size());
  check_close_images(qimage_from_document(original, true), qimage_from_document(doc, true), 0);
  MainWindowTestAccess::redo(window);
  CHECK(std::as_const(doc).layers().size() == 1);

  // Already flat: no prompt, no history entry.
  require_action(window, "imageFlattenAction")->trigger();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("The image is already flat"));
}

std::vector<std::string> layer_names(const std::vector<Layer>& layers) {
  std::vector<std::string> names;
  for (const auto& layer : layers) { names.push_back(layer.name()); }
  return names;
}

Layer named(Layer layer, const char* name) {
  layer.set_name(name);
  return layer;
}

// GitHub issue 475: Merge Selected to New Layer (Copy) (Ctrl+Alt+E) renders what the selected
// layers draw into one new pixel layer above the topmost selected one, named after it. The
// originals stay, hidden selected layers are skipped, and an unselected group around a selected
// layer applies once (the copy lands inside it). One undo step; one layer is copied merged.
void ui_merge_selected_to_new_layer_keeps_originals_and_undoes() {
  {
    // A clipped layer keeps its clip only when its base is merged too.
    Document clip_doc(32, 32, PixelFormat::rgba8());
    const auto base_id = clip_doc.add_layer(named(pixel_layer(clip_doc, 2, 2, {200, 0, 0}), "Base")).id();
    auto clip = named(pixel_layer(clip_doc, 6, 6, {0, 0, 200}), "Clip");
    clip.set_clipped(true);
    const auto clip_id = clip_doc.add_layer(std::move(clip)).id();
    const auto alone = selected_document_for_merge_copy(clip_doc, {clip_id});
    CHECK(alone.layers().size() == 1U && !alone.layers().front().clipped());
    const auto both = selected_document_for_merge_copy(clip_doc, {clip_id, base_id});
    CHECK(both.layers().size() == 2U && both.layers().back().clipped());
  }
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(48, 40, PixelFormat::rgba8());
  const auto bottom_id = doc.add_layer(named(pixel_layer(doc, 2, 2, {180, 30, 60}), "Bottom")).id();
  auto multiply = named(pixel_layer(doc, 8, 6, {20, 150, 90}), "Multiply");
  multiply.set_blend_mode(BlendMode::Multiply);
  multiply.set_opacity(0.5F);
  const auto multiply_id = doc.add_layer(std::move(multiply)).id();
  auto hidden = named(pixel_layer(doc, 20, 4, {0, 255, 0}), "Hidden");
  hidden.set_visible(false);
  const auto hidden_id = doc.add_layer(std::move(hidden)).id();
  Layer group(doc.allocate_layer_id(), "Group", LayerKind::Group);
  group.set_blend_mode(BlendMode::Normal);
  group.set_opacity(0.75F);
  const auto group_id = group.id();
  auto inside = named(pixel_layer(doc, 12, 12, {30, 60, 220}), "Inside");
  const auto inside_id = inside.id();
  set_layer_locks_all(inside, true);  // locks never block a copy
  group.add_child(std::move(inside));
  group.add_child(named(pixel_layer(doc, 26, 20, {250, 200, 40}), "Other"));
  doc.add_layer(std::move(group));
  doc.add_layer(named(pixel_layer(doc, 6, 14, {90, 90, 90}), "Top"));
  doc.set_active_layer(inside_id);
  const Document original = doc;
  // The merge draws the selected visible layers alone, the unselected group dissolved.
  Document expected_doc = original;
  expected_doc.layers() = {*original.find_layer(multiply_id), *original.find_layer(inside_id)};
  const auto expected = qimage_from_document(expected_doc, true);
  CHECK(expected.pixelColor(13, 13).alpha() > 0 && expected.pixelColor(3, 3).alpha() == 0);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  MainWindowTestAccess::select_layers(window, {multiply_id, hidden_id, inside_id}, inside_id);
  auto* action = require_hotkey_action(window, QStringLiteral("layer.merge_selected_to_new"));
  CHECK(action == require_action(window, "layerMergeSelectedToNewAction"));
  CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E));
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);

  action->trigger();
  CHECK(!canvas->processing_operation_active());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  const auto* group_after = std::as_const(doc).find_layer(group_id);
  CHECK(group_after != nullptr);
  CHECK((layer_names(group_after->children()) == std::vector<std::string>{"Inside", "Inside (merged)", "Other"}));
  const auto& copy = group_after->children()[1];
  CHECK(doc.active_layer_id() == copy.id());
  CHECK(copy.kind() == LayerKind::Pixel && copy.lock_flags() == kLayerLockNone);
  CHECK(copy.opacity() == 1.0F && copy.blend_mode() == BlendMode::Normal && copy.visible());
  check_close_images(expected, render_layer_isolated(doc, copy), 0);
  CHECK(layer_names(std::as_const(doc).layers()) == layer_names(original.layers()));
  for (const auto id : {bottom_id, multiply_id, hidden_id, inside_id}) {
    const auto* now = std::as_const(doc).find_layer(id);
    CHECK(now != nullptr && now->content_revision() == original.find_layer(id)->content_revision());
    CHECK(now != nullptr && now->visible() == original.find_layer(id)->visible());
  }
  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).find_layer(group_id)->children().size() == 2U);
  MainWindowTestAccess::redo(window);
  CHECK(std::as_const(doc).find_layer(group_id)->children().size() == 3U);

  // One selected layer is copied merged, directly above it (Photoshop).
  MainWindowTestAccess::undo(window);
  MainWindowTestAccess::select_layers(window, {bottom_id}, bottom_id);
  action->trigger();
  CHECK((layer_names(std::as_const(doc).layers()) ==
         std::vector<std::string>{"Bottom", "Bottom (merged)", "Multiply", "Hidden", "Group", "Top"}));
  Document bottom_doc = original;
  bottom_doc.layers() = {*original.find_layer(bottom_id)};
  check_close_images(qimage_from_document(bottom_doc, true),
                     render_layer_isolated(doc, std::as_const(doc).layers()[1]), 0);

  // Only hidden layers selected: nothing to merge, no history entry.
  MainWindowTestAccess::undo(window);
  MainWindowTestAccess::select_layers(window, {hidden_id}, hidden_id);
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  action->trigger();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("No visible selected layers to merge"));
  CHECK(layer_names(std::as_const(doc).layers()) == layer_names(original.layers()));
}

// GitHub issue 294: Merge Visible merges every layer that draws into one pixel layer in the
// active layer's slot, as one undo step. Hidden layers stay, a visible group's hidden members
// come out of it, and the composite does not change (Photoshop 2026 over COM, October 2026).
void ui_merge_visible_merges_in_place_keeps_hidden_layers_and_undoes() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(48, 40, PixelFormat::rgba8());
  doc.add_layer(named(pixel_layer(doc, 2, 2, {180, 30, 60}), "Bottom"));
  auto hidden = named(pixel_layer(doc, 20, 4, {0, 255, 0}), "Hidden");
  hidden.set_visible(false);
  const auto hidden_id = hidden.id();
  doc.add_layer(std::move(hidden));
  Layer group(doc.allocate_layer_id(), "Group", LayerKind::Group);
  group.set_blend_mode(BlendMode::Normal);
  group.set_opacity(0.75F);
  auto inside = named(pixel_layer(doc, 10, 10, {20, 150, 90}), "Inside");
  const auto inside_id = inside.id();
  inside.set_color_label(kLayerColorBlue);
  group.add_child(std::move(inside));
  auto hidden_inside = named(pixel_layer(doc, 26, 20, {0, 0, 255}), "Hidden inside");
  hidden_inside.set_visible(false);
  group.add_child(std::move(hidden_inside));
  doc.add_layer(std::move(group));
  doc.add_layer(named(vector_layer(doc, rectangle(24, 20, 16, 12)), "Shape"));
  auto top = named(pixel_layer(doc, 6, 14, {250, 200, 40}), "Top");
  top.set_opacity(0.5F);
  top.set_blend_mode(BlendMode::Multiply);
  set_layer_locks_all(top, true);  // locks do not block Merge Visible (Photoshop)
  doc.add_layer(std::move(top));
  doc.set_active_layer(inside_id);
  const Document original = doc;
  const auto expected = qimage_from_document(original, true);
  CHECK(merge_visible_layer_count(original) == 4U);
  CHECK(merge_visible_target(original) == inside_id);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  auto* action = require_action(window, "layerMergeVisibleLayersAction");
  CHECK(action->isEnabled());
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);

  action->trigger();
  CHECK(!canvas->processing_operation_active());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Merged the visible layers"));
  const auto& layers = std::as_const(doc).layers();
  CHECK((layer_names(layers) == std::vector<std::string>{"Hidden", "Inside", "Hidden inside"}));
  CHECK(layers[0].id() == hidden_id && !layers[0].visible());
  CHECK(!layers[2].visible());
  const auto& merged = layers[1];
  CHECK(merged.id() == inside_id && doc.active_layer_id() == inside_id);
  CHECK(merged.kind() == LayerKind::Pixel && merged.visible() && !merged.clipped());
  CHECK(merged.opacity() == 1.0F && merged.blend_mode() == BlendMode::Normal);
  CHECK(merged.lock_flags() == kLayerLockNone && merged.color_label() == kLayerColorBlue);
  CHECK(merged.pixels().format() == PixelFormat::rgba8());
  CHECK(merged.bounds().x == 0 && merged.bounds().y == 0);
  CHECK(merged.bounds().width == doc.width() && merged.bounds().height == doc.height());
  check_close_images(expected, render_layer_isolated(doc, merged), 0);
  check_close_images(expected, qimage_from_document(doc, true), 0);
  // One visible layer is left: nothing more to merge.
  CHECK(!action->isEnabled());

  MainWindowTestAccess::undo(window);
  CHECK(layer_names(std::as_const(doc).layers()) == layer_names(original.layers()));
  check_close_images(expected, qimage_from_document(doc, true), 0);
  CHECK(action->isEnabled());
  MainWindowTestAccess::redo(window);
  CHECK((layer_names(std::as_const(doc).layers()) == std::vector<std::string>{"Hidden", "Inside", "Hidden inside"}));

  // A hidden active layer (Photoshop refuses the command) hands the slot to the topmost
  // layer that draws.
  MainWindowTestAccess::undo(window);
  doc.set_active_layer(hidden_id);
  CHECK(merge_visible_target(std::as_const(doc)) == std::as_const(doc).layers().back().id());
}

// GitHub issue 294: a visible Background takes the merge and stays the Background; a clipped
// layer over a hidden base stays clipped to it, and one left with nothing below is released.
// Fewer than two visible layers disable the command (Photoshop: "not currently available").
void ui_merge_visible_background_clipping_and_disabled_state() {
  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(40, 32, PixelFormat::rgb8());
  PixelBuffer white(40, 32, PixelFormat::rgb8());
  white.clear(255);
  auto& background = doc.add_pixel_layer("Background", std::move(white));
  set_layer_locks_position(background, true);
  set_layer_locks_transparent_pixels(background, true);
  const auto background_id = background.id();
  doc.add_layer(named(pixel_layer(doc, 2, 2, {180, 30, 60}), "Paint"));
  auto base = named(pixel_layer(doc, 12, 8, {0, 255, 0}), "Hidden base");
  base.set_visible(false);
  doc.add_layer(std::move(base));
  auto clip = named(pixel_layer(doc, 14, 10, {0, 0, 255}), "Clipped");
  clip.set_clipped(true);
  doc.add_layer(std::move(clip));
  doc.add_layer(named(pixel_layer(doc, 20, 16, {250, 200, 40}), "Top"));
  doc.set_active_layer(std::as_const(doc).layers().back().id());
  const Document original = doc;
  const auto expected = qimage_from_document(original, true);
  CHECK(merge_visible_layer_count(original) == 3U);  // the clipped layer over a hidden base draws nothing
  CHECK(merge_visible_target(original) == background_id);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  require_action(window, "layerMergeVisibleLayersAction")->trigger();
  const auto& layers = std::as_const(doc).layers();
  CHECK((layer_names(layers) == std::vector<std::string>{"Background", "Hidden base", "Clipped"}));
  CHECK(layers[0].id() == background_id && doc.active_layer_id() == background_id);
  CHECK(background_layer(layers, doc.width(), doc.height()) == &layers[0]);
  CHECK(layers[0].pixels().format() == PixelFormat::rgb8());
  CHECK(layer_locks_position(layers[0]) && layer_locks_transparent_pixels(layers[0]));
  CHECK(layers[2].clipped());
  check_close_images(expected, qimage_from_document(doc, true), 0);

  // Background plus one hidden layer: off, and the shortcut path refuses too.
  auto* action = require_action(window, "layerMergeVisibleLayersAction");
  CHECK(!action->isEnabled());
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);
  action->trigger();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history);
  doc.find_layer(std::as_const(doc).layers()[1].id())->set_visible(true);
  MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(action->isEnabled());

  // Hidden clipped layer over a visible base: the base merges into Top and the clipped layer,
  // now at the bottom, is released.
  Document clipping(24, 20, PixelFormat::rgba8());
  clipping.add_layer(named(pixel_layer(clipping, 2, 2, {180, 30, 60}), "Base"));
  auto hidden_clip = named(pixel_layer(clipping, 4, 4, {0, 0, 255}), "Clip");
  hidden_clip.set_clipped(true);
  hidden_clip.set_visible(false);
  clipping.add_layer(std::move(hidden_clip));
  clipping.add_layer(named(pixel_layer(clipping, 6, 6, {250, 200, 40}), "Top"));
  clipping.set_active_layer(clipping.layers().back().id());
  const auto target = merge_visible_target(clipping);
  CHECK(target == clipping.layers().back().id());
  const auto stack = merge_visible_layer_stack(clipping, target, Layer(target, "Top", PixelBuffer(24, 20, PixelFormat::rgba8())));
  CHECK((layer_names(stack) == std::vector<std::string>{"Clip", "Top"}));
  CHECK(!stack[0].clipped() && !stack[0].visible());
}

Layer square_layer(Document& doc, const char* name, QRect rect, RgbColor color) {
  PixelBuffer pixels(rect.width(), rect.height(), PixelFormat::rgba8());
  for (int y = 0; y < rect.height(); ++y) {
    for (int x = 0; x < rect.width(); ++x) {
      auto* p = pixels.pixel(x, y);
      p[0] = color.red; p[1] = color.green; p[2] = color.blue; p[3] = 255;
    }
  }
  Layer layer(doc.allocate_layer_id(), name, std::move(pixels));
  layer.set_bounds({rect.x(), rect.y(), rect.width(), rect.height()});
  return layer;
}

// GitHub issue 294: Layer > Merge > Merge Visible sits right below Merge Down and above the
// copying Merge Visible, and the issue's before/after captures in both schemes.
void ui_merge_visible_menu_placement_and_captures() {
  MainWindow window;
  show_window(window);
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  CHECK(layer_menu != nullptr);
  auto* merge_menu = window.findChild<QMenu*>(QStringLiteral("layerMergeMenu"));
  CHECK(merge_menu != nullptr && layer_menu->actions().contains(merge_menu->menuAction()));
  if (merge_menu != nullptr) {
    CHECK((merge_menu->actions() == QList<QAction*>{require_action(window, "layerMergeDownAction"),
                                                    require_action(window, "layerMergeVisibleLayersAction"),
                                                    require_action(window, "layerMergeVisibleAction")}));
  }

  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(240, 160, PixelFormat::rgb8());
  PixelBuffer white(240, 160, PixelFormat::rgb8());
  white.clear(255);
  auto& background = doc.add_pixel_layer("Background", std::move(white));
  set_layer_locks_position(background, true);
  set_layer_locks_transparent_pixels(background, true);
  doc.add_layer(square_layer(doc, "Red", QRect(30, 30, 80, 70), {220, 40, 50}));
  auto hidden = square_layer(doc, "Green (hidden)", QRect(80, 50, 80, 70), {40, 180, 80});
  hidden.set_visible(false);
  doc.add_layer(std::move(hidden));
  auto blue = square_layer(doc, "Blue", QRect(130, 60, 80, 70), {40, 90, 220});
  blue.set_opacity(0.7F);
  doc.add_layer(std::move(blue));
  doc.set_active_layer(std::as_const(doc).layers().back().id());
  require_canvas(window)->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  const auto capture_window = [&](const char* dark, const char* light) {
    for (const auto scheme : {ColorSchemePreference::Dark, ColorSchemePreference::Light}) {
      ColorSchemeRestorer restore(scheme);
      QApplication::processEvents();
      save_widget_artifact(scheme == ColorSchemePreference::Dark ? dark : light, window);
    }
  };
  for (const auto scheme : {ColorSchemePreference::Dark, ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    save_widget_artifact(scheme == ColorSchemePreference::Dark ? "issue294_layer_menu_dark" : "issue294_layer_menu_light",
                         *layer_menu);
    layer_menu->hide();
    QApplication::processEvents();
  }
  capture_window("issue294_before_dark", "issue294_before_light");
  require_action(window, "layerMergeVisibleLayersAction")->trigger();
  QApplication::processEvents();
  CHECK((layer_names(std::as_const(doc).layers()) == std::vector<std::string>{"Background", "Green (hidden)"}));
  capture_window("issue294_after_dark", "issue294_after_light");
}

// GitHub issue 519: Merge Visible works with one layer that draws when the merge applies
// something (a mask, style, text, shape, opacity, blending or a visible group); a single plain
// pixel layer keeps it off. The masked layer becomes one plain layer with the mask in its
// alpha, the same id and name, in one undo step that brings the mask back.
void ui_merge_visible_single_layer_applies_mask_and_undoes() {
  const auto opaque_layer = [](Document& doc, const char* name, RgbColor color) {
    PixelBuffer pixels(doc.width(), doc.height(), PixelFormat::rgba8());
    for (int y = 0; y < doc.height(); ++y) {
      for (int x = 0; x < doc.width(); ++x) {
        auto* p = pixels.pixel(x, y);
        p[0] = color.red; p[1] = color.green; p[2] = color.blue; p[3] = 255;
      }
    }
    return Layer(doc.allocate_layer_id(), name, std::move(pixels));
  };
  // The enable rule, one layer at a time.
  {
    Document plain(24, 20, PixelFormat::rgba8());
    plain.add_layer(opaque_layer(plain, "Plain", {200, 40, 40}));
    CHECK(merge_visible_layer_count(plain) == 1U);
    CHECK(!merge_visible_available(plain));
    auto hidden = pixel_layer(plain, 2, 2, {0, 0, 255});
    hidden.set_visible(false);
    plain.add_layer(std::move(hidden));
    CHECK(!merge_visible_available(plain));  // a hidden layer adds nothing to merge
    const auto variant = [&](const std::function<void(Layer&)>& change) {
      Document doc = plain;
      change(*doc.find_layer(std::as_const(doc).layers().front().id()));
      return merge_visible_available(doc);
    };
    CHECK(variant([](Layer& layer) {
      LayerMask mask;
      mask.bounds = layer.bounds();
      mask.pixels = PixelBuffer(layer.bounds().width, layer.bounds().height, PixelFormat::gray8());
      layer.mask() = std::move(mask);
    }));
    CHECK(variant([](Layer& layer) {
      LayerDropShadow shadow;
      shadow.enabled = true;
      layer.layer_style().drop_shadows = {shadow};
    }));
    CHECK(variant([](Layer& layer) { layer.set_opacity(0.5F); }));
    CHECK(variant([](Layer& layer) { layer.set_fill_opacity(0.5F); }));
    CHECK(variant([](Layer& layer) { layer.set_blend_mode(BlendMode::Multiply); }));
    Document text = plain;
    text.layers().front() = Layer(std::as_const(text).layers().front().id(), "Text", LayerKind::Text);
    CHECK(merge_visible_available(text));
    Document shape = plain;
    shape.layers().front() = named(vector_layer(shape, rectangle(2, 2, 10, 8)), "Shape");
    CHECK(merge_visible_available(shape));
    Document grouped(24, 20, PixelFormat::rgba8());
    Layer group(grouped.allocate_layer_id(), "Group", LayerKind::Group);
    group.add_child(opaque_layer(grouped, "Inside", {20, 150, 90}));
    grouped.add_layer(std::move(group));
    CHECK(merge_visible_available(grouped));  // the visible group dissolves
    Document background(24, 20, PixelFormat::rgb8());
    PixelBuffer white(24, 20, PixelFormat::rgb8());
    white.clear(255);
    auto& back = background.add_pixel_layer("Background", std::move(white));
    set_layer_locks_position(back, true);
    set_layer_locks_transparent_pixels(back, true);
    CHECK(!merge_visible_available(background));
  }

  MainWindow window;
  show_window(window);
  auto& doc = MainWindowTestAccess::document(window);
  doc = Document(160, 120, PixelFormat::rgba8());
  auto photo = opaque_layer(doc, "Photo", {220, 90, 40});
  // A transparent PNG opened with its alpha as a mask: a soft ellipse, clear outside.
  LayerMask mask;
  mask.bounds = photo.bounds();
  mask.pixels = PixelBuffer(160, 120, PixelFormat::gray8());
  mask.default_color = 0;
  for (int y = 0; y < 120; ++y) {
    for (int x = 0; x < 160; ++x) {
      const double dx = (x - 80) / 60.0;
      const double dy = (y - 60) / 42.0;
      const double d = std::sqrt(dx * dx + dy * dy);
      mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>(std::clamp((1.0 - d) * 4.0, 0.0, 1.0) * 255.0);
    }
  }
  photo.mask() = std::move(mask);
  photo.set_color_label(kLayerColorBlue);
  const auto photo_id = photo.id();
  doc.add_layer(std::move(photo));
  doc.set_active_layer(photo_id);
  const Document original = doc;
  const auto expected = qimage_from_document(original, true);
  auto* canvas = require_canvas(window);
  canvas->set_document(&doc);
  MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  auto* action = require_action(window, "layerMergeVisibleLayersAction");
  CHECK(action->isEnabled());
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  CHECK(layer_menu != nullptr);
  for (const auto scheme : {ColorSchemePreference::Dark, ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    CHECK(action->isEnabled());  // the menu's aboutToShow refresh keeps it on
    save_widget_artifact(scheme == ColorSchemePreference::Dark ? "issue519_layer_menu_dark" : "issue519_layer_menu_light",
                         *layer_menu);
    layer_menu->hide();
    QApplication::processEvents();
    save_widget_artifact(scheme == ColorSchemePreference::Dark ? "issue519_before_dark" : "issue519_before_light", window);
  }
  const auto history = MainWindowTestAccess::active_session_undo_depth(window);

  action->trigger();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == history + 1);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Merged the visible layers"));
  const auto& layers = std::as_const(doc).layers();
  CHECK(layers.size() == 1U);
  const auto& merged = layers.front();
  CHECK(merged.id() == photo_id && merged.name() == "Photo" && doc.active_layer_id() == photo_id);
  CHECK(merged.kind() == LayerKind::Pixel && !merged.mask().has_value());
  CHECK(merged.color_label() == kLayerColorBlue);
  CHECK(merged.pixels().format() == PixelFormat::rgba8());
  CHECK(merged.pixels().pixel(80, 60)[3] == 255);  // the mask is in the alpha now
  CHECK(merged.pixels().pixel(2, 2)[3] == 0);
  check_close_images(expected, qimage_from_document(doc, true), 0);
  CHECK(!action->isEnabled());  // a plain layer is left
  for (const auto scheme : {ColorSchemePreference::Dark, ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    QApplication::processEvents();
    save_widget_artifact(scheme == ColorSchemePreference::Dark ? "issue519_after_dark" : "issue519_after_light", window);
  }

  MainWindowTestAccess::undo(window);
  CHECK(std::as_const(doc).layers().size() == 1U);
  CHECK(std::as_const(doc).layers().front().mask().has_value());
  check_close_images(expected, qimage_from_document(doc, true), 0);
  CHECK(action->isEnabled());
}

// GitHub issue 389: Duplicate on a folder, then Merge Down on a child of the
// copy (and of the original), driven through Layers panel clicks.
std::string dump_tree(const std::vector<Layer>& layers, int depth = 0) {
  std::string out;
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    out += std::string(static_cast<std::size_t>(depth) * 2U, ' ') + it->name() + " #" + std::to_string(it->id()) + "\n";
    out += dump_tree(it->children(), depth + 1);
  }
  return out;
}

QListWidgetItem* layer_item_by_id(QListWidget& list, LayerId id) {
  for (int row = 0; row < list.count(); ++row) {
    auto* item = list.item(row);
    if (static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()) == id) { return item; }
  }
  return nullptr;
}

void click_layer_row_by_id(QListWidget& list, LayerId id) {
  const auto fetch = [&]() -> QLabel* {
    auto* item = layer_item_by_id(list, id);
    CHECK(item != nullptr);
    list.scrollToItem(item);
    auto* label = list.itemWidget(item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
    CHECK(label != nullptr);
    return label;
  };
  auto* press = fetch();
  send_mouse(*press, QEvent::MouseButtonPress, press->rect().center(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  auto* release = fetch();
  send_mouse(*release, QEvent::MouseButtonRelease, release->rect().center(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
}

void issue389_scenario(bool shapes, bool hide_original) {
  Document doc(96, 96, PixelFormat::rgba8());
  Layer folder(doc.allocate_layer_id(), "Folder 1", LayerKind::Group);
  const auto folder_id = folder.id();
  const std::array<RgbColor, 3> colors{{{220, 30, 30}, {30, 200, 40}, {40, 60, 230}}};
  for (int i = 0; i < 3; ++i) {
    auto child = shapes ? vector_layer(doc, rectangle(8 + i * 14, 8 + i * 14, 30, 30, colors[static_cast<std::size_t>(i)]))
                        : pixel_layer(doc, 8 + i * 14, 8 + i * 14, colors[static_cast<std::size_t>(i)]);
    child.set_name((shapes ? "Shape " : "Layer ") + std::to_string(i + 1));
    folder.add_child(std::move(child));
  }
  doc.add_layer(std::move(folder));
  MainWindow window;
  show_window(window);
  window.add_document_session(std::move(doc), QStringLiteral("Issue 389"));
  auto& live = MainWindowTestAccess::document(window);
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  const auto children_of = [&](LayerId id) {
    std::vector<std::string> names;
    const auto* group = std::as_const(live).find_layer(id);
    CHECK(group != nullptr);
    for (const auto& child : group->children()) { names.push_back(child.name()); }
    return names;
  };
  const auto merge_down = [&](LayerId upper) {
    click_layer_row_by_id(*list, upper);
    CHECK((MainWindowTestAccess::selected_layer_ids(window) == std::vector<LayerId>{upper}));
    require_action(window, "layerMergeDownAction")->trigger();
    QApplication::processEvents();
    std::printf("[389] merge down #%llu (status '%s')\n%s", static_cast<unsigned long long>(upper),
                window.statusBar()->currentMessage().toUtf8().constData(),
                dump_tree(std::as_const(live).layers()).c_str());
  };

  // Duplicate Layer on the folder: only the folder is renamed, like Photoshop's.
  click_layer_row_by_id(*list, folder_id);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  const auto& top = std::as_const(live).layers();
  const auto copy_it = std::find_if(top.begin(), top.end(), [](const Layer& l) { return l.name() == "Folder 1 copy"; });
  CHECK(copy_it != top.end());
  const auto copy_id = copy_it->id();
  const auto original_names = children_of(folder_id);
  CHECK(children_of(copy_id) == original_names);
  for (const auto& child : copy_it->children()) {
    std::vector<LayerId> ancestors;
    CHECK(collect_layer_ancestor_groups(top, child.id(), ancestors));
    CHECK((ancestors == std::vector<LayerId>{copy_id}));
  }

  if (hide_original) {
    // The reporter's panel: the original folder hidden after duplicating it.
    auto* eye = list->itemWidget(layer_item_by_id(*list, folder_id))->findChild<QToolButton*>(
        QStringLiteral("layerVisibilityCheck"));
    CHECK(eye != nullptr);
    eye->click();
    QApplication::processEvents();
    CHECK(!std::as_const(live).find_layer(folder_id)->visible());
  } else {
    merge_down(std::as_const(live).find_layer(copy_id)->children().back().id());
    CHECK((children_of(copy_id) == std::vector<std::string>{original_names[0], original_names[1]}));
    CHECK(children_of(folder_id) == original_names);
  }
  const auto* original = std::as_const(live).find_layer(folder_id);
  const auto upper = original->children()[2].id();
  const auto lower = original->children()[1].id();
  const auto bottom = original->children()[0].id();
  const auto undo_depth = MainWindowTestAccess::active_session_undo_depth(window);
  merge_down(upper);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1);
  CHECK((children_of(folder_id) == std::vector<std::string>{original_names[0], original_names[1]}));
  original = std::as_const(live).find_layer(folder_id);
  CHECK(original->visible() == !hide_original);
  CHECK(original->children()[0].id() == bottom);
  const auto& merged = original->children()[1];
  CHECK(merged.visible());
  // The merged layer holds both sources' artwork (the lower one keeps its id).
  if (!shapes) {
    CHECK(merged.id() == lower);
    CHECK(merged.bounds().width > 16 && merged.bounds().height > 16);
  } else {
    CHECK(std::as_const(live).find_layer(upper) == nullptr || std::as_const(live).find_layer(lower) == nullptr);
    CHECK(merged.vector_shape() != nullptr);
  }
  CHECK(children_of(copy_id).size() == (hide_original ? 3U : 2U));
  if (hide_original) {
    save_widget_artifact(shapes ? "issue389_hidden_folder_merged_shapes" : "issue389_hidden_folder_merged_pixels",
                         *layers_dock);
  }
}

void ui_issue389_duplicate_folder_then_merge_down_pixels() { issue389_scenario(false, false); }
void ui_issue389_duplicate_folder_then_merge_down_shapes() { issue389_scenario(true, false); }
void ui_issue389_merge_down_inside_hidden_folder_pixels() { issue389_scenario(false, true); }
void ui_issue389_merge_down_inside_hidden_folder_shapes() { issue389_scenario(true, true); }

}  // namespace

std::vector<patchy::test::TestCase> layer_merge_tests() {
  return {
      {"ui_issue389_duplicate_folder_then_merge_down_pixels", ui_issue389_duplicate_folder_then_merge_down_pixels},
      {"ui_issue389_duplicate_folder_then_merge_down_shapes", ui_issue389_duplicate_folder_then_merge_down_shapes},
      {"ui_issue389_merge_down_inside_hidden_folder_pixels", ui_issue389_merge_down_inside_hidden_folder_pixels},
      {"ui_issue389_merge_down_inside_hidden_folder_shapes", ui_issue389_merge_down_inside_hidden_folder_shapes},
      {"ui_layer_selection_count_includes_collapsed_descendants", ui_layer_selection_count_includes_collapsed_descendants},
      {"ui_layer_selection_count_little_everywhere_if_available", ui_layer_selection_count_little_everywhere_if_available},
      {"ui_merge_visible_copy_bitmap_preserves_alpha_and_history", ui_merge_visible_copy_bitmap_preserves_alpha_and_history},
      {"ui_merge_selected_to_new_layer_keeps_originals_and_undoes", ui_merge_selected_to_new_layer_keeps_originals_and_undoes},
      {"ui_merge_visible_copy_explicit_vector_mode_keeps_originals", ui_merge_visible_copy_explicit_vector_mode_keeps_originals},
      {"ui_merge_visible_copy_bitmap_preserves_fully_transparent_canvas", ui_merge_visible_copy_bitmap_preserves_fully_transparent_canvas},
      {"ui_merge_visible_copy_dialog_preserves_sources_and_history", ui_merge_visible_copy_dialog_preserves_sources_and_history},
      {"ui_merge_visible_copy_single_vector_raster_and_visibility_choices", ui_merge_visible_copy_single_vector_raster_and_visibility_choices},
      {"ui_merge_visible_copy_little_everywhere_if_available", ui_merge_visible_copy_little_everywhere_if_available},
      {"ui_flatten_image_composites_onto_white_discards_hidden_and_undoes",
       ui_flatten_image_composites_onto_white_discards_hidden_and_undoes},
      {"ui_merge_visible_merges_in_place_keeps_hidden_layers_and_undoes",
       ui_merge_visible_merges_in_place_keeps_hidden_layers_and_undoes},
      {"ui_merge_visible_background_clipping_and_disabled_state", ui_merge_visible_background_clipping_and_disabled_state},
      {"ui_merge_visible_menu_placement_and_captures", ui_merge_visible_menu_placement_and_captures},
      {"ui_merge_visible_single_layer_applies_mask_and_undoes", ui_merge_visible_single_layer_applies_mask_and_undoes},
      {"ui_layer_merge_compound_geometry_and_processing", ui_layer_merge_compound_geometry_and_processing},
      {"ui_layer_selection_little_everywhere_shift_range_if_available", ui_layer_selection_little_everywhere_shift_range_if_available},
      {"ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd", ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd},
      {"ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations", ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations},
      {"ui_layer_merge_gradients_patterns_and_paint_alignment", ui_layer_merge_gradients_patterns_and_paint_alignment},
      {"ui_layer_merge_group_and_vector_type_choices", ui_layer_merge_group_and_vector_type_choices},
      {"ui_layer_merge_protected_layers_and_unselected_order_are_barriers", ui_layer_merge_protected_layers_and_unselected_order_are_barriers},
      {"ui_layer_merge_single_vector_paints_effects_order_and_psd", ui_layer_merge_single_vector_paints_effects_order_and_psd},
      {"ui_layer_merge_single_vector_protections_and_group_boundaries", ui_layer_merge_single_vector_protections_and_group_boundaries},
      {"ui_vector_preview_compound_effect_halos_match_cropped_view", ui_vector_preview_compound_effect_halos_match_cropped_view},
      {"ui_layer_merge_single_vector_dialog_preview_cancel_and_history", ui_layer_merge_single_vector_dialog_preview_cancel_and_history},
      {"ui_layer_merge_single_vector_script_validation_and_undo", ui_layer_merge_single_vector_script_validation_and_undo},
      {"ui_layer_merge_dialog_explains_separate_effects", ui_layer_merge_dialog_explains_separate_effects},
      {"ui_layer_merge_dialog_cancel_accept_and_history", ui_layer_merge_dialog_cancel_accept_and_history},
      {"ui_layer_merge_script_validation_noop_and_undo", ui_layer_merge_script_validation_noop_and_undo},
      {"ui_layer_merge_little_everywhere_if_available", ui_layer_merge_little_everywhere_if_available},
  };
}
