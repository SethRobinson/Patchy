#include "ui_test_support.hpp"
#include "local_psd_fixtures.hpp"

#include "core/layer_metadata.hpp"
#include "core/vector_compound.hpp"
#include "core/pixel_tools.hpp"
#include "ui/layer_list_widget.hpp"
#include <QScrollBar>
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/layer_merge.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_preview_renderer.hpp"
#include "formats/svg_document_io.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <cstdio>
#include <utility>

using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

namespace {

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
  doc.layers()[3].layer_style().drop_shadows.push_back({true});
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
  if (!std::filesystem::exists(path)) { return; }
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

void ui_layer_selection_little_everywhere_shift_range_if_available() {
  const auto path = patchy::test::local_format_fixture_path("vector-preview", "Little-Everywhere.psd");
  if (!std::filesystem::exists(path)) { return; }
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
  if (!std::filesystem::exists(path)) { std::printf("[SKIP] Little-Everywhere fixture missing\n"); return; }
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
  const auto reread = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(doc));
  CHECK(layer_tree_count(reread.layers()) == 2362);
  MainWindowTestAccess::undo(window);
  CHECK(layer_tree_count(std::as_const(doc).layers()) == 2056);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_merge_tests() {
  return {
      {"ui_merge_visible_copy_dialog_preserves_sources_and_history", ui_merge_visible_copy_dialog_preserves_sources_and_history},
      {"ui_merge_visible_copy_single_vector_raster_and_visibility_choices", ui_merge_visible_copy_single_vector_raster_and_visibility_choices},
      {"ui_merge_visible_copy_little_everywhere_if_available", ui_merge_visible_copy_little_everywhere_if_available},
      {"ui_layer_merge_compound_geometry_and_processing", ui_layer_merge_compound_geometry_and_processing},
      {"ui_layer_selection_little_everywhere_shift_range_if_available", ui_layer_selection_little_everywhere_shift_range_if_available},
      {"ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd", ui_layer_merge_mixed_group_keeps_vectors_order_opacity_and_psd},
      {"ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations", ui_layer_merge_holes_alpha_and_strokes_keep_independent_operations},
      {"ui_layer_merge_gradients_patterns_and_paint_alignment", ui_layer_merge_gradients_patterns_and_paint_alignment},
      {"ui_layer_merge_group_and_vector_type_choices", ui_layer_merge_group_and_vector_type_choices},
      {"ui_layer_merge_protected_layers_and_unselected_order_are_barriers", ui_layer_merge_protected_layers_and_unselected_order_are_barriers},
      {"ui_layer_merge_dialog_cancel_accept_and_history", ui_layer_merge_dialog_cancel_accept_and_history},
      {"ui_layer_merge_script_validation_noop_and_undo", ui_layer_merge_script_validation_noop_and_undo},
      {"ui_layer_merge_little_everywhere_if_available", ui_layer_merge_little_everywhere_if_available},
  };
}
