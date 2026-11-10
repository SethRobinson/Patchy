// Dynamic Vector Preview against the document render (local-051 follow-up 2). The sharp view
// must show what the document render shows: for a matrix of shape layers (every kind, paint,
// stroke, Edge value, mask, effect and combine op the shape tools make) the preview frame of
// a view, reduced to document pixels, is compared with the document composite, at 100%, at
// a whole zoom and at a fractional one, and a panned view must equal the same pixels of the
// full view (tiles and culling). A shape layer's Feather and Density were missing from the
// preview's copy of the shape for seven weeks before this test existed.
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_compound.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_merge.hpp"
#include "ui/main_window.hpp"
#include "ui/vector_preview_renderer.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

namespace {

struct PreviewPreferenceGuard {
  QVariant before = app_settings().value(QStringLiteral("view/vectorPreview"));
  PreviewPreferenceGuard() { app_settings().remove(QStringLiteral("view/vectorPreview")); }
  ~PreviewPreferenceGuard() {
    if (before.isValid()) { app_settings().setValue(QStringLiteral("view/vectorPreview"), before); }
    else { app_settings().remove(QStringLiteral("view/vectorPreview")); }
  }
};

constexpr int kWidth = 160;
constexpr int kHeight = 120;

Document paper_document() {
  Document doc(kWidth, kHeight, PixelFormat::rgba8());
  PixelBuffer paper(kWidth, kHeight, PixelFormat::rgba8());
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      auto* pixel = paper.pixel(x, y);
      pixel[0] = 232; pixel[1] = 236; pixel[2] = 240; pixel[3] = 255;
    }
  }
  doc.add_layer(Layer(doc.allocate_layer_id(), "Paper", std::move(paper)));
  return doc;
}

PathAnchor corner(double x, double y) {
  PathAnchor anchor;
  anchor.anchor_x = anchor.in_x = anchor.out_x = x;
  anchor.anchor_y = anchor.in_y = anchor.out_y = y;
  return anchor;
}

PathAnchor curve(double x, double y, double in_dx, double in_dy, double out_dx, double out_dy) {
  auto anchor = corner(x, y);
  anchor.in_x = x + in_dx; anchor.in_y = y + in_dy;
  anchor.out_x = x + out_dx; anchor.out_y = y + out_dy;
  anchor.smooth = true;
  return anchor;
}

VectorShapeContent live(LiveShapeParams params, RgbColor color) {
  VectorShapeContent shape;
  shape.path.subpaths = generate_live_shape_subpaths(params);
  shape.origination.push_back(params);
  shape.fill.kind = VectorFillKind::Solid;
  shape.fill.color = color;
  return shape;
}

VectorShapeContent rect_shape(double x, double y, double w, double h, double radius = 0.0,
                              RgbColor color = {40, 110, 200}) {
  LiveShapeParams params;
  params.kind = radius > 0.0 ? LiveShapeKind::RoundedRectangle : LiveShapeKind::Rectangle;
  params.left = x; params.top = y; params.right = x + w; params.bottom = y + h;
  params.corner_radii = {radius, radius, radius, radius};
  return live(params, color);
}

VectorShapeContent ellipse_shape(double x, double y, double w, double h, RgbColor color = {200, 80, 40}) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Ellipse;
  params.left = x; params.top = y; params.right = x + w; params.bottom = y + h;
  return live(params, color);
}

VectorShapeContent polygon_shape(double cx, double cy, double radius, int sides, int inset) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Polygon;
  params.left = cx - radius; params.top = cy - radius; params.right = cx + radius; params.bottom = cy + radius;
  params.polygon_sides = sides; params.polygon_star_inset = inset; params.polygon_angle = -1.2;
  return live(params, {60, 150, 90});
}

VectorShapeContent line_shape(double x1, double y1, double x2, double y2, double weight, bool start, bool end,
                              double head_width = 0.0, double head_length = 0.0, int concavity = 0) {
  LiveShapeParams params;
  params.kind = LiveShapeKind::Line;
  params.line_start_x = x1; params.line_start_y = y1; params.line_end_x = x2; params.line_end_y = y2;
  params.line_weight = weight; params.arrow_start = start; params.arrow_end = end;
  params.arrow_width = head_width; params.arrow_length = head_length; params.arrow_concavity = concavity;
  return live(params, {214, 160, 40});
}

// A custom-shape-like plain path: a curved outline with a hole in the same group.
VectorShapeContent custom_shape() {
  VectorShapeContent shape;
  PathSubpath outer;
  outer.closed = true;
  outer.anchors = {curve(80, 22, -26, 0, 26, 0), curve(128, 58, 0, -22, 0, 26), corner(80, 102),
                   curve(32, 58, 0, 26, 0, -22)};
  PathSubpath hole;
  hole.closed = true;
  hole.anchors = {corner(80, 44), corner(98, 70), corner(62, 70)};
  shape.path.subpaths = {outer, hole};
  shape.fill.kind = VectorFillKind::Solid;
  shape.fill.color = {150, 60, 160};
  return shape;
}

VectorShapeContent pen_shape(bool closed) {
  VectorShapeContent shape;
  PathSubpath path;
  path.closed = closed;
  path.anchors = {corner(26, 92), curve(60, 24, -18, 14, 18, -14), corner(96, 70), curve(134, 30, -14, 6, 10, -4)};
  shape.path.subpaths = {path};
  shape.fill.kind = VectorFillKind::Solid;
  shape.fill.color = {30, 140, 150};
  return shape;
}

void stroke(VectorShapeContent& shape, double width, VectorStrokeAlignment alignment,
            RgbColor color = {30, 34, 40}) {
  shape.stroke.enabled = true;
  shape.stroke.width = width;
  shape.stroke.alignment = alignment;
  shape.stroke.content.kind = VectorFillKind::Solid;
  shape.stroke.content.color = color;
}

LayerStyleGradient gradient(bool aligned) {
  LayerStyleGradient result;
  result.color_stops = {{0, {240, 60, 30}}, {0.55F, {250, 220, 60}}, {1, {20, 120, 230}}};
  result.alpha_stops = {{0, 1}, {1, 1}};
  result.angle_degrees = 28;
  result.align_with_layer = aligned;
  return result;
}

void add_pattern(Document& doc) {
  PixelBuffer tile(4, 4, PixelFormat::rgba8());
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      auto* pixel = tile.pixel(x, y);
      const bool on = (x < 2) != (y < 2);
      pixel[0] = on ? 206 : 190; pixel[1] = on ? 150 : 170; pixel[2] = on ? 120 : 140; pixel[3] = 255;
    }
  }
  doc.metadata().patterns.adopt({"parity-pattern", "Parity", std::move(tile)});
}

Layer& add_shape(Document& doc, VectorShapeContent shape, const char* name = "Shape") {
  Layer layer(doc.allocate_layer_id(), name, LayerKind::Pixel);
  layer.set_vector_shape(std::move(shape));
  layer.metadata()[kLayerMetadataVectorShape] = "1";
  update_vector_shape_raster(layer, Rect::from_size(doc.width(), doc.height()), &std::as_const(doc).metadata().patterns);
  return doc.add_layer(std::move(layer));
}

// How strict a case is. Flat fills and strokes must agree almost exactly. Blurs (Feather,
// effects) are built from box passes whose sizes round differently at each zoom. An Outside
// stroke leaves a one-pixel seam along the path's edge in the document render (its band is
// cut by the fill's antialiased coverage), which a view at a higher zoom draws thinner.
// A pattern is interpolated between its texels in the sharp view.
enum class Kind { Crisp, Soft, Seam, Pattern };

struct ParityCase {
  std::string name;
  Kind kind{Kind::Crisp};
  std::function<void(Document&)> build;
  // Paints sampled per pixel (patterns) reduce cleanly only at whole zooms.
  bool whole_zooms_only{false};
};

std::vector<ParityCase> parity_cases() {
  using Align = VectorStrokeAlignment;
  std::vector<ParityCase> cases;
  const auto crisp = [&](std::string name, std::function<void(Document&)> build) {
    cases.push_back({std::move(name), Kind::Crisp, std::move(build)});
  };
  const auto soft = [&](std::string name, std::function<void(Document&)> build) {
    cases.push_back({std::move(name), Kind::Soft, std::move(build)});
  };
  const auto seam = [&](std::string name, std::function<void(Document&)> build) {
    cases.push_back({std::move(name), Kind::Seam, std::move(build)});
  };

  // --- kinds ---
  crisp("line", [](Document& doc) { add_shape(doc, line_shape(20, 90, 140, 30, 6, false, false)); });
  crisp("line arrowheads", [](Document& doc) { add_shape(doc, line_shape(24, 88, 136, 34, 4, true, true, 20, 26, 20)); });
  crisp("line large head", [](Document& doc) { add_shape(doc, line_shape(16, 60, 140, 60, 2, false, true, 70, 60, 0)); });
  crisp("line head longer than line", [](Document& doc) {
    add_shape(doc, line_shape(90, 70, 130, 50, 3, false, true, 60, 110, 30));
  });
  crisp("line two heads meeting", [](Document& doc) {
    add_shape(doc, line_shape(50, 60, 110, 60, 4, true, true, 50, 80, -30));
  });
  for (const auto alignment : {Align::Center, Align::Inside, Align::Outside}) {
    const char* names[] = {"inside", "center", "outside"};
    cases.push_back({std::string("line stroke ") + names[static_cast<int>(alignment)],
                     alignment == Align::Outside ? Kind::Seam : Kind::Crisp, [alignment](Document& doc) {
      auto shape = line_shape(24, 84, 136, 40, 8, false, true, 28, 30, 0);
      stroke(shape, 5, alignment);
      add_shape(doc, std::move(shape));
    }});
  }
  crisp("rectangle", [](Document& doc) { add_shape(doc, rect_shape(22.5, 18.25, 110, 76)); });
  crisp("rounded rectangle", [](Document& doc) { add_shape(doc, rect_shape(24, 20, 108, 74, 18)); });
  crisp("ellipse", [](Document& doc) { add_shape(doc, ellipse_shape(20.5, 14, 118, 90)); });
  crisp("polygon", [](Document& doc) { add_shape(doc, polygon_shape(80, 60, 48, 6, 0)); });
  crisp("star", [](Document& doc) { add_shape(doc, polygon_shape(80, 60, 52, 5, 55)); });
  crisp("custom shape", [](Document& doc) { add_shape(doc, custom_shape()); });
  crisp("pen closed", [](Document& doc) { add_shape(doc, pen_shape(true)); });
  crisp("pen open stroked", [](Document& doc) {
    auto shape = pen_shape(false);
    stroke(shape, 4, Align::Center);
    add_shape(doc, std::move(shape));
  });
  // An open path has no inside: its Inside and Outside strokes run beside the line.
  for (const auto alignment : {Align::Inside, Align::Outside}) {
    crisp(alignment == Align::Inside ? "pen open stroke inside" : "pen open stroke outside", [alignment](Document& doc) {
      VectorShapeContent shape;
      PathSubpath path;
      path.closed = false;
      path.anchors = {corner(24, 84), curve(80, 40, -26, 10, 26, -10), corner(138, 78)};
      shape.path.subpaths = {path};
      shape.fill.kind = VectorFillKind::None;
      stroke(shape, 5, alignment);
      shape.stroke.cap = VectorStrokeCap::Round;
      add_shape(doc, std::move(shape));
    });
  }
  crisp("fill layer", [](Document& doc) {
    VectorShapeContent shape;
    shape.fill.kind = VectorFillKind::Solid;
    shape.fill.color = {120, 170, 210};
    add_shape(doc, std::move(shape)).set_opacity(0.6F);
  });

  // --- fill ---
  crisp("no fill stroke", [](Document& doc) {
    auto shape = ellipse_shape(24, 18, 112, 84);
    shape.fill.kind = VectorFillKind::None;
    stroke(shape, 6, Align::Center, {180, 40, 60});
    add_shape(doc, std::move(shape));
  });
  for (const bool aligned : {true, false}) {
    crisp(aligned ? "gradient fill aligned" : "gradient fill canvas", [aligned](Document& doc) {
      auto shape = rect_shape(30, 24, 96, 70, 10);
      shape.fill.kind = VectorFillKind::Gradient;
      shape.fill.gradient = gradient(aligned);
      add_shape(doc, std::move(shape));
    });
  }
  cases.push_back({"pattern fill", Kind::Pattern, [](Document& doc) {
    add_pattern(doc);
    auto shape = ellipse_shape(22, 16, 116, 88);
    shape.fill.kind = VectorFillKind::Pattern;
    shape.fill.pattern_id = "parity-pattern";
    shape.fill.pattern_scale = 2;
    add_shape(doc, std::move(shape));
  }, true});
  crisp("gradient fill layer", [](Document& doc) {
    VectorShapeContent shape;
    shape.fill.kind = VectorFillKind::Gradient;
    shape.fill.gradient = gradient(true);
    add_shape(doc, std::move(shape));
  });

  // --- stroke ---
  for (const auto alignment : {Align::Inside, Align::Center, Align::Outside}) {
    const char* names[] = {"inside", "center", "outside"};
    cases.push_back({std::string("rectangle stroke ") + names[static_cast<int>(alignment)],
                     alignment == Align::Outside ? Kind::Seam : Kind::Crisp, [alignment](Document& doc) {
      auto shape = rect_shape(30, 26, 100, 68);
      stroke(shape, 7, alignment);
      add_shape(doc, std::move(shape));
    }});
  }
  crisp("stroke dashes round", [](Document& doc) {
    auto shape = ellipse_shape(26, 20, 108, 80);
    stroke(shape, 4, Align::Center);
    shape.stroke.dashes = {3, 2};
    shape.stroke.cap = VectorStrokeCap::Round;
    shape.stroke.join = VectorStrokeJoin::Round;
    add_shape(doc, std::move(shape));
  });
  seam("stroke square caps bevel", [](Document& doc) {
    auto shape = polygon_shape(80, 60, 46, 5, 40);
    stroke(shape, 5, Align::Outside);
    shape.stroke.cap = VectorStrokeCap::Square;
    shape.stroke.join = VectorStrokeJoin::Bevel;
    shape.stroke.dashes = {4, 2, 1, 2};
    shape.stroke.dash_offset = 0.5;
    add_shape(doc, std::move(shape));
  });
  crisp("gradient stroke", [](Document& doc) {
    auto shape = rect_shape(34, 28, 92, 64, 14, {250, 250, 250});
    stroke(shape, 9, Align::Center);
    shape.stroke.content.kind = VectorFillKind::Gradient;
    shape.stroke.content.gradient = gradient(true);
    add_shape(doc, std::move(shape));
  });
  cases.push_back({"pattern stroke", Kind::Pattern, [](Document& doc) {
    add_pattern(doc);
    auto shape = ellipse_shape(30, 24, 100, 72, {245, 245, 245});
    stroke(shape, 10, Align::Center);
    shape.stroke.content.kind = VectorFillKind::Pattern;
    shape.stroke.content.pattern_id = "parity-pattern";
    shape.stroke.content.pattern_scale = 1;
    add_shape(doc, std::move(shape));
  }, true});
  crisp("stroke opacity", [](Document& doc) {
    auto shape = rect_shape(30, 26, 100, 68, 8);
    stroke(shape, 8, Align::Center, {200, 30, 30});
    shape.stroke.opacity = 0.45;
    add_shape(doc, std::move(shape));
  });

  // --- Edge: Feather and Density ---
  soft("feather", [](Document& doc) {
    auto shape = ellipse_shape(40, 30, 80, 60);
    shape.feather = 3.5;
    add_shape(doc, std::move(shape));
  });
  crisp("density", [](Document& doc) {
    auto shape = rect_shape(40, 30, 80, 60, 6);
    shape.density = 133;
    add_shape(doc, std::move(shape));
  });
  soft("feather and density", [](Document& doc) {
    auto shape = line_shape(40, 44, 112, 82, 4, false, true, 22, 20, 0);
    shape.feather = 4.5;
    shape.density = 133;
    add_shape(doc, std::move(shape));
  });
  soft("feather stroke at canvas edge", [](Document& doc) {
    auto shape = rect_shape(-12, 40, 70, 96, 0);
    stroke(shape, 6, Align::Center, {200, 30, 30});
    shape.feather = 2.5;
    add_shape(doc, std::move(shape));
  });
  soft("feather large", [](Document& doc) {
    auto shape = polygon_shape(80, 60, 40, 5, 50);
    shape.feather = 9.0;
    add_shape(doc, std::move(shape));
  });
  crisp("density gradient stroke", [](Document& doc) {
    auto shape = ellipse_shape(36, 26, 88, 68);
    shape.fill.kind = VectorFillKind::Gradient;
    shape.fill.gradient = gradient(false);
    stroke(shape, 5, Align::Inside);
    shape.density = 90;
    add_shape(doc, std::move(shape));
  });
  // A layer-aligned gradient spans the fill's coverage: all of the canvas once Density is
  // under 100%, and the canvas grown by the feather's reach when the shape is feathered too.
  crisp("density aligned gradient", [](Document& doc) {
    auto shape = rect_shape(40, 30, 80, 60, 10);
    shape.fill.kind = VectorFillKind::Gradient;
    shape.fill.gradient = gradient(true);
    stroke(shape, 4, Align::Inside);
    shape.density = 250;
    add_shape(doc, std::move(shape));
  });
  soft("feather density aligned gradients", [](Document& doc) {
    auto shape = ellipse_shape(36, 26, 88, 68);
    shape.fill.kind = VectorFillKind::Gradient;
    shape.fill.gradient = gradient(true);
    shape.fill.gradient.type = LayerStyleGradientType::Radial;
    stroke(shape, 6, Align::Center);
    shape.stroke.content.kind = VectorFillKind::Gradient;
    shape.stroke.content.gradient = gradient(true);
    shape.stroke.content.gradient.angle_degrees = 90;
    shape.feather = 3.0;
    shape.density = 200;
    add_shape(doc, std::move(shape));
  });
  soft("feather aligned gradient past the canvas edge", [](Document& doc) {
    auto shape = rect_shape(-30, 50, 90, 110, 0);
    shape.fill.kind = VectorFillKind::Gradient;
    shape.fill.gradient = gradient(true);
    shape.fill.gradient.angle_degrees = 0;
    stroke(shape, 6, Align::Center);
    shape.stroke.content.kind = VectorFillKind::Gradient;
    shape.stroke.content.gradient = gradient(true);
    shape.stroke.content.gradient.angle_degrees = 0;
    shape.feather = 3.0;
    add_shape(doc, std::move(shape));
  });
  crisp("aligned gradient past the canvas edge", [](Document& doc) {
    auto shape = ellipse_shape(96, -24, 100, 70);
    shape.fill.kind = VectorFillKind::Gradient;
    shape.fill.gradient = gradient(true);
    stroke(shape, 5, Align::Center);
    shape.stroke.content.kind = VectorFillKind::Gradient;
    shape.stroke.content.gradient = gradient(true);
    shape.stroke.content.gradient.angle_degrees = 0;
    add_shape(doc, std::move(shape));
  });
  crisp("density inverted", [](Document& doc) {
    auto shape = ellipse_shape(40, 30, 80, 60);
    shape.path_inverted = true;
    shape.density = 170;
    add_shape(doc, std::move(shape));
  });
  soft("feather inverted", [](Document& doc) {
    auto shape = rect_shape(46, 34, 68, 52);
    shape.path_inverted = true;
    shape.feather = 3.0;
    add_shape(doc, std::move(shape));
  });
  soft("feather density disabled path", [](Document& doc) {
    auto shape = rect_shape(46, 34, 68, 52);
    shape.path_disabled = true;
    shape.feather = 3.0;
    shape.density = 120;
    add_shape(doc, std::move(shape)).set_opacity(0.7F);
  });
  soft("feather density fill layer", [](Document& doc) {
    VectorShapeContent shape;
    shape.fill.kind = VectorFillKind::Solid;
    shape.fill.color = {214, 160, 40};
    shape.feather = 4.0;
    shape.density = 100;
    add_shape(doc, std::move(shape)).set_opacity(0.5F);
  });

  // --- layer values, masks, effects ---
  crisp("opacity and fill opacity", [](Document& doc) {
    auto shape = rect_shape(30, 24, 100, 72, 10);
    stroke(shape, 6, Align::Center);
    auto& layer = add_shape(doc, std::move(shape));
    layer.set_opacity(0.7F);
    layer.set_fill_opacity(0.4F);
  });
  crisp("path inverted", [](Document& doc) {
    auto shape = ellipse_shape(36, 26, 88, 68);
    shape.path_inverted = true;
    add_shape(doc, std::move(shape)).set_opacity(0.8F);
  });
  crisp("path disabled", [](Document& doc) {
    auto shape = ellipse_shape(36, 26, 88, 68);
    shape.path_disabled = true;
    add_shape(doc, std::move(shape)).set_opacity(0.5F);
  });
  soft("vector mask density feather", [](Document& doc) {
    auto& layer = add_shape(doc, rect_shape(24, 20, 112, 80));
    LayerVectorMask mask;
    mask.path = ellipse_shape(44, 30, 86, 70).path;
    mask.density = 200;
    mask.feather = 2.0;
    layer.set_vector_mask(std::move(mask));
    update_vector_mask_raster(layer, Rect::from_size(doc.width(), doc.height()));
  });
  crisp("raster mask", [](Document& doc) {
    auto& layer = add_shape(doc, ellipse_shape(24, 18, 112, 84));
    PixelBuffer mask(60, 50, PixelFormat::gray8());
    for (int y = 0; y < 50; ++y) {
      for (int x = 0; x < 60; ++x) { mask.pixel(x, y)[0] = static_cast<std::uint8_t>(255 - (x / 12) * 40); }
    }
    layer.set_mask(LayerMask{{50, 36, 60, 50}, std::move(mask), 60, false});
  });
  soft("drop shadow and effect stroke", [](Document& doc) {
    auto& layer = add_shape(doc, polygon_shape(78, 58, 40, 6, 0));
    LayerDropShadow shadow;
    shadow.enabled = true; shadow.size = 3; shadow.distance = 5;
    layer.layer_style().drop_shadows.push_back(shadow);
    LayerStroke outline;
    outline.enabled = true; outline.size = 3; outline.color = {250, 250, 250};
    layer.layer_style().strokes.push_back(outline);
  });
  soft("feather with drop shadow", [](Document& doc) {
    auto shape = ellipse_shape(44, 32, 72, 54);
    shape.feather = 2.0;
    auto& layer = add_shape(doc, std::move(shape));
    LayerDropShadow shadow;
    shadow.enabled = true; shadow.size = 2; shadow.distance = 6;
    layer.layer_style().drop_shadows.push_back(shadow);
  });
  soft("outer glow on stroked shape", [](Document& doc) {
    auto shape = rect_shape(46, 36, 68, 48, 8);
    stroke(shape, 4, Align::Inside);
    auto& layer = add_shape(doc, std::move(shape));
    LayerOuterGlow glow;
    glow.enabled = true; glow.size = 4;
    layer.layer_style().outer_glows.push_back(glow);
  });

  // --- canvas edge, combine ops, merged vectors ---
  seam("shapes past the canvas edge", [](Document& doc) {
    auto first = ellipse_shape(-30, -20, 90, 70);
    stroke(first, 6, Align::Outside);
    add_shape(doc, std::move(first));
    add_shape(doc, rect_shape(120, 84, 80, 60, 12));
    add_shape(doc, line_shape(100, -10, 175, 60, 5, true, true, 24, 28, 0));
  });
  for (const auto op : {PathCombineOp::Add, PathCombineOp::Subtract, PathCombineOp::Intersect, PathCombineOp::Xor}) {
    const char* names[] = {"exclude", "add", "subtract", "intersect"};
    crisp(std::string("combine ") + names[static_cast<int>(op)], [op](Document& doc) {
      auto shape = rect_shape(26, 22, 78, 60, 6);
      auto second = ellipse_shape(64, 44, 72, 56).path.subpaths.front();
      second.op = op;
      second.shape_group = 1;
      shape.path.subpaths.push_back(second);
      shape.origination.clear();
      stroke(shape, 3, Align::Center);
      add_shape(doc, std::move(shape));
    });
  }
  crisp("merged vector layer", [](Document& doc) {
    const auto first = add_shape(doc, rect_shape(26, 24, 70, 56, 6)).id();
    auto second_shape = ellipse_shape(70, 46, 66, 50);
    stroke(second_shape, 4, Align::Center);
    const auto second = add_shape(doc, std::move(second_shape)).id();
    LayerMergeOptions options;
    options.single_vector = true;
    const auto plan = plan_layer_merge(doc, {first, second}, options);
    CHECK(plan.blockers.empty());
    doc = render_layer_merge(doc, plan);
  });
  return cases;
}

struct Difference {
  int max{0};
  double mean{0.0};
  double over_8{0.0};   // share of pixels whose largest channel difference passes 8
  double over_32{0.0};
};

QImage rgba(const QImage& image) { return image.convertToFormat(QImage::Format_RGBA8888); }

// `frame` (a view of the whole document at `scale`, origin at the top left) averaged down to
// document pixels, premultiplied.
QImage reduce_to_document(const QImage& frame, double scale, QSize document) {
  QImage out(document, QImage::Format_RGBA8888);
  for (int y = 0; y < document.height(); ++y) {
    auto* row = out.scanLine(y);
    const double y0 = y * scale, y1 = (y + 1) * scale;
    for (int x = 0; x < document.width(); ++x) {
      const double x0 = x * scale, x1 = (x + 1) * scale;
      double sum[4] = {0, 0, 0, 0};
      double area = 0.0;
      for (int fy = static_cast<int>(std::floor(y0)); fy < std::min(frame.height(), static_cast<int>(std::ceil(y1))); ++fy) {
        const double wy = std::min(y1, fy + 1.0) - std::max(y0, static_cast<double>(fy));
        if (wy <= 0.0) continue;
        const auto* source = frame.constScanLine(fy);
        for (int fx = static_cast<int>(std::floor(x0)); fx < std::min(frame.width(), static_cast<int>(std::ceil(x1))); ++fx) {
          const double wx = std::min(x1, fx + 1.0) - std::max(x0, static_cast<double>(fx));
          if (wx <= 0.0) continue;
          const auto* pixel = source + fx * 4;
          const double weight = wx * wy;
          const double alpha = pixel[3] / 255.0;
          sum[0] += weight * pixel[0] * alpha;
          sum[1] += weight * pixel[1] * alpha;
          sum[2] += weight * pixel[2] * alpha;
          sum[3] += weight * pixel[3];
          area += weight;
        }
      }
      const double alpha = area > 0.0 ? sum[3] / area / 255.0 : 0.0;
      for (int channel = 0; channel < 3; ++channel) {
        row[x * 4 + channel] = alpha > 0.0
            ? static_cast<std::uint8_t>(std::clamp(std::lround(sum[channel] / area / alpha), 0L, 255L)) : 0;
      }
      row[x * 4 + 3] = static_cast<std::uint8_t>(std::clamp(std::lround(alpha * 255.0), 0L, 255L));
    }
  }
  return out;
}

// Per pixel, the largest difference of alpha and of the alpha-weighted colors. With
// `edges` (reduced views): how far the pixel lies outside the range the other image holds
// in the 3 x 3 pixels around it, each way. An antialiased edge that falls a fraction of a
// pixel differently then counts nothing, while a flat area that differs, a feature that is
// missing or an extra one counts in full.
Difference compare(const QImage& expected, const QImage& actual, bool edges) {
  const int width = expected.width(), height = expected.height();
  const auto premultiplied = [&](const QImage& image) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4U);
    for (int y = 0; y < height; ++y) {
      const auto* row = image.constScanLine(y);
      auto* target = out.data() + static_cast<std::size_t>(y) * width * 4U;
      for (int x = 0; x < width; ++x, row += 4, target += 4) {
        for (int channel = 0; channel < 3; ++channel) {
          target[channel] = static_cast<std::uint8_t>((row[channel] * row[3] + 127) / 255);
        }
        target[3] = row[3];
      }
    }
    return out;
  };
  const auto a = premultiplied(expected), b = premultiplied(actual);
  const auto outside = [&](const std::vector<std::uint8_t>& value, const std::vector<std::uint8_t>& around, int x, int y) {
    int worst = 0;
    const auto* pixel = value.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
    for (int channel = 0; channel < 4; ++channel) {
      int low = 255, high = 0;
      const int reach = edges ? 1 : 0;
      for (int ny = std::max(0, y - reach); ny <= std::min(height - 1, y + reach); ++ny) {
        for (int nx = std::max(0, x - reach); nx <= std::min(width - 1, x + reach); ++nx) {
          const int neighbor = around[(static_cast<std::size_t>(ny) * width + nx) * 4U + channel];
          low = std::min(low, neighbor);
          high = std::max(high, neighbor);
        }
      }
      worst = std::max({worst, low - pixel[channel], pixel[channel] - high});
    }
    return worst;
  };
  Difference result;
  double total = 0.0;
  int over_8 = 0, over_32 = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int worst = std::max(outside(a, b, x, y), outside(b, a, x, y));
      result.max = std::max(result.max, worst);
      total += worst;
      over_8 += worst > 8 ? 1 : 0;
      over_32 += worst > 32 ? 1 : 0;
    }
  }
  const double pixels = static_cast<double>(width) * height;
  result.mean = total / pixels;
  result.over_8 = over_8 / pixels;
  result.over_32 = over_32 / pixels;
  return result;
}

// At 100% the two renders run the same code on the same geometry. Reduced views differ by
// antialiasing and, for blurs, by box sizes that round differently at each zoom.
bool differs(const Difference& difference, double zoom, Kind kind) {
  if (zoom == 1.0) return difference.max > (kind == Kind::Soft ? 3 : 1);
  const bool whole = zoom == std::floor(zoom);
  switch (kind) {
    case Kind::Soft: return difference.mean > 0.6 || difference.over_32 > 0.0005 || difference.over_8 > 0.01;
    case Kind::Seam: return difference.mean > 0.5 || difference.over_32 > 0.008 || difference.over_8 > 0.015;
    case Kind::Pattern: return difference.mean > 0.8 || difference.over_32 > 0.0005 || difference.over_8 > 0.003;
    case Kind::Crisp: break;
  }
  // A thin shape falls between view pixels at a fractional zoom.
  return difference.mean > (whole ? 0.12 : 0.2) || difference.over_32 > 0.0005 ||
         difference.over_8 > (whole ? 0.003 : 0.01);
}

std::string file_name(const std::string& name) {
  std::string out = name;
  std::replace(out.begin(), out.end(), ' ', '_');
  return out;
}

void run_parity(const std::vector<ParityCase>& cases, const char* report_name) {
  std::filesystem::create_directories("test-artifacts");
  std::ofstream report(std::filesystem::path("test-artifacts") / report_name, std::ios::binary);
  std::vector<std::string> failures;
  const auto note = [&](const std::string& line, bool failed) {
    report << (failed ? "FAIL " : "ok   ") << line << "\n";
    if (failed) {
      failures.push_back(line);
      std::printf("[parity] FAIL %s\n", line.c_str());
    }
  };
  for (const auto& item : cases) {
    auto doc = paper_document();
    item.build(doc);
    const auto scene = build_vector_preview_scene(doc);
    if (scene.fallback != VectorPreviewFallback::None || !scene.has_vectors) {
      note(item.name + ": no sharp scene", true);
      continue;
    }
    const auto expected = rgba(qimage_from_document(doc, true));
    std::vector<double> zooms{1.0, 4.0};
    if (!item.whole_zooms_only) zooms.push_back(2.5);
    QImage full_4;
    for (const double zoom : zooms) {
      const QSize pixels(static_cast<int>(std::lround(kWidth * zoom)), static_cast<int>(std::lround(kHeight * zoom)));
      const auto result = render_vector_preview(scene, {pixels, zoom, {}});
      char view[32];
      std::snprintf(view, sizeof(view), "%g%%", zoom * 100.0);
      if (result.fallback != VectorPreviewFallback::None || result.image.isNull()) {
        note(item.name + " at " + view + ": the preview fell back", true);
        continue;
      }
      const auto frame = rgba(result.image);
      if (zoom == 4.0) full_4 = frame;
      const auto reduced = zoom == 1.0 ? frame : reduce_to_document(frame, zoom, expected.size());
      const auto difference = compare(expected, reduced, zoom != 1.0);
      const bool failed = differs(difference, zoom, item.kind);
      char line[256];
      std::snprintf(line, sizeof(line), "%s at %s: max %d, mean %.3f, over 8: %.2f%%, over 32: %.2f%%", item.name.c_str(),
                    view, difference.max, difference.mean, difference.over_8 * 100.0, difference.over_32 * 100.0);
      note(line, failed);
      if (failed) {
        const auto stem = QStringLiteral("test-artifacts/vector-preview-parity-%1-%2")
                              .arg(QString::fromStdString(file_name(item.name))).arg(static_cast<int>(zoom * 100));
        (void)frame.save(stem + QStringLiteral("-preview.png"));
        (void)reduced.save(stem + QStringLiteral("-preview-reduced.png"));
        (void)expected.save(stem + QStringLiteral("-document.png"));
      }
    }
    // A panned view draws the same pixels as the full one: nothing is culled or cut at a
    // tile edge (the tile grid starts at the view's origin, so the tiles differ).
    if (!full_4.isNull()) {
      const auto panned = render_vector_preview(scene, {{300, 260}, 4.0, {-173.0, -117.0}});
      const bool same = panned.fallback == VectorPreviewFallback::None &&
                        rgba(panned.image) == full_4.copy(173, 117, 300, 260);
      note(item.name + ": panned view equals the full view", !same);
      if (!same && !panned.image.isNull()) {
        const auto stem = QStringLiteral("test-artifacts/vector-preview-parity-%1")
                              .arg(QString::fromStdString(file_name(item.name)));
        (void)panned.image.save(stem + QStringLiteral("-panned.png"));
        (void)full_4.copy(173, 117, 300, 260).save(stem + QStringLiteral("-full-crop.png"));
      }
    }
  }
  report.close();
  if (!failures.empty()) {
    std::string message = std::to_string(failures.size()) + " preview views differ from the document render:";
    for (const auto& line : failures) message += "\n  " + line;
    throw std::runtime_error(message);
  }
}

// Every shape kind, paint, stroke, Edge value, mask, effect and combine op: the sharp view
// shows what the document render shows.
void ui_vector_preview_matches_document_render_for_shape_layers() {
  run_parity(parity_cases(), "vector-preview-parity.txt");
}

// --- on the canvas ---

void select_layer(MainWindow& window, LayerId id) {
  MainWindowTestAccess::refresh_layer_ui(window);
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  QListWidgetItem* current = nullptr;
  {
    const QSignalBlocker blocker(list);
    list->clearSelection();
    for (int row = 0; row < list->count(); ++row) {
      auto* item = list->item(row);
      if (static_cast<LayerId>(item->data(kLayerIdRole).toULongLong()) == id) {
        item->setSelected(true);
        current = item;
      }
    }
    list->setCurrentItem(current, QItemSelectionModel::NoUpdate);
  }
  QMetaObject::invokeMethod(list, "itemSelectionChanged", Qt::DirectConnection);
}

void settle(CanvasWidget& canvas) {
  canvas.update();
  CHECK(process_events_until([&] { return canvas.render_settled() && !canvas.processing_operation_active(); }, 60000));
  QApplication::processEvents();
}

// The canvas shows the document's own color at each probe that the view holds.
bool canvas_matches_document(CanvasWidget& canvas, const Document& doc, const std::vector<QPoint>& probes, int tolerance,
                             std::string* detail = nullptr) {
  const auto expected = qimage_from_document(doc, true);
  const auto grab = canvas.grab().toImage();
  bool same = true;
  int seen = 0;
  for (const auto probe : probes) {
    const auto top_left = canvas.widget_position_for_document_point(probe);
    const auto bottom_right = canvas.widget_position_for_document_point(probe + QPoint(1, 1));
    const QPoint center((top_left.x() + bottom_right.x()) / 2, (top_left.y() + bottom_right.y()) / 2);
    if (!QRect(24, 24, grab.width() - 60, grab.height() - 60).contains(center)) continue;
    ++seen;
    const QColor shown = grab.pixelColor(center);
    const QColor wanted = expected.pixelColor(probe);
    if (!color_close(shown, wanted, tolerance)) {
      same = false;
      if (detail != nullptr) {
        *detail += " (" + std::to_string(probe.x()) + "," + std::to_string(probe.y()) + ") shows " +
                   shown.name().toStdString() + ", the document has " + wanted.name().toStdString() + ";";
      }
    }
  }
  if (seen == 0) {
    same = false;
    if (detail != nullptr) *detail += " no probe in view;";
  }
  return same;
}

// Seth's picture (local-051): a gold arrow over a photo, Edge Feather 9 px and Density 52%.
// The photo is two flat tones so a probe reads one exact color.
LayerId add_arrow_over_photo(MainWindow& window, double feather, std::uint8_t density) {
  auto& doc = MainWindowTestAccess::document(window);
  PixelBuffer photo(doc.width(), doc.height(), PixelFormat::rgba8());
  for (int y = 0; y < doc.height(); ++y) {
    for (int x = 0; x < doc.width(); ++x) {
      auto* pixel = photo.pixel(x, y);
      const bool left = x < doc.width() / 2;
      pixel[0] = left ? 28 : 118; pixel[1] = left ? 52 : 150; pixel[2] = left ? 92 : 190; pixel[3] = 255;
    }
  }
  doc.add_layer(Layer(doc.allocate_layer_id(), "Photo", std::move(photo)));
  auto shape = line_shape(150, 120, 250, 190, 6, false, true, 30, 28, 0);
  shape.feather = feather;
  shape.density = density;
  const auto id = add_shape(doc, std::move(shape), "Line 1").id();
  doc.set_active_layer(id);
  require_canvas(window)->document_changed();
  select_layer(window, id);
  return id;
}

// local-051 follow-up 2: a shape layer's Edge values (Feather, Density) show the same with
// Dynamic Vector Preview on at every zoom, live in Shape Appearance and after OK, Undo and
// Cancel. The sharp view used to draw the shape without them, so above 100% the feather and
// the whole-canvas Density tint vanished whenever the view settled.
void ui_vector_preview_shows_shape_feather_and_density_at_every_zoom() {
  PreviewPreferenceGuard preference;
  MainWindow window; show_window(window);
  MainWindowTestAccess::create_document(window, 400, 300);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = add_arrow_over_photo(window, 9.0, 133);
  canvas->set_vector_preview_enabled(true);
  // Far from the arrow on each tone (the Density tint), on the shaft, and in the feather's
  // halo beside it.
  const std::vector<QPoint> tint{{90, 230}, {310, 80}, {120, 60}, {280, 240}};
  const std::vector<QPoint> edge{{200, 155}, {200, 146}, {212, 140}, {190, 163}, {236, 180}, {196, 171}};
  auto all = tint;
  all.insert(all.end(), edge.begin(), edge.end());
  {
    // The tint is there in the document: both tones moved toward gold.
    const auto composite = qimage_from_document(doc, true);
    CHECK(!color_close(composite.pixelColor(90, 230), QColor(28, 52, 92), 20));
    CHECK(!color_close(composite.pixelColor(310, 80), QColor(118, 150, 190), 20));
  }
  std::string detail;
  canvas->set_view_zoom_centered(0.5);
  settle(*canvas);
  save_widget_artifact("ui_local_051_edge_zoom_50", *canvas);
  const bool zoomed_out = canvas_matches_document(*canvas, doc, tint, 10, &detail);
  canvas->set_view_zoom_centered(3.0);
  settle(*canvas);
  save_widget_artifact("ui_local_051_edge_zoom_300", *canvas);
  const bool at_300 = canvas_matches_document(*canvas, doc, all, 14, &detail);
  canvas->set_view_zoom_centered(16.0);
  settle(*canvas);
  save_widget_artifact("ui_local_051_edge_zoom_1600", *canvas);
  const bool at_1600 = canvas_matches_document(*canvas, doc, edge, 14, &detail);
  canvas->set_view_zoom_centered(1.0);
  settle(*canvas);
  const bool at_100 = canvas_matches_document(*canvas, doc, all, 14, &detail);
  if (!(zoomed_out && at_300 && at_1600 && at_100)) {
    throw std::runtime_error("the sharp view differs from the document:" + detail);
  }
  // The same with the preview off (the pixel view).
  canvas->set_vector_preview_enabled(false);
  canvas->set_view_zoom_centered(3.0);
  settle(*canvas);
  CHECK(canvas_matches_document(*canvas, doc, all, 14));
  canvas->set_vector_preview_enabled(true);
  settle(*canvas);

  // Live in Shape Appearance at 300%: typed and stepped values show when their render
  // lands and stay once the view settles.
  const Layer opened = *std::as_const(doc).find_layer(id);
  const auto shape_now = [&]() -> const VectorShapeContent& { return *std::as_const(doc).find_layer(id)->vector_shape(); };
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  std::string live;
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    auto* feather = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"));
    auto* density = dialog.findChild<QSpinBox*>(QStringLiteral("shapeDensitySpin"));
    CHECK(feather != nullptr && density != nullptr);
    CHECK(std::abs(feather->value() - 9.0) < 1e-9 && density->value() == 52);
    // The note under Density says why the whole canvas is tinted, while it is.
    auto* note = dialog.findChild<QLabel*>(QStringLiteral("shapeDensityNote"));
    CHECK(note != nullptr && note->isVisible());
    CHECK(note->text() == QStringLiteral("Below 100% the fill also shows over the whole canvas, as in Photoshop."));
    CHECK(note->height() >= note->heightForWidth(note->width()));
    const auto dialog_size = dialog.size();
    const auto shown = [&](const char* what) {
      settle(*canvas);
      std::string differences;
      if (!canvas_matches_document(*canvas, doc, all, 14, &differences)) live += std::string(" [") + what + "]" + differences;
    };
    feather->setValue(0.0);
    density->setValue(100);
    (void)process_events_until([&] { return shape_now().feather < 0.05 && shape_now().density == 255; });
    shown("no feather, full density");
    CHECK(!note->isVisible() && dialog.size() == dialog_size);
    // The tint is gone from the canvas: the photo's own tones.
    CHECK(color_close(canvas_pixel_center(*canvas, QPoint(90, 230)), QColor(28, 52, 92), 6));
    for (const int value : {90, 80, 70, 60, 52}) density->setValue(value);  // a scrub
    (void)process_events_until([&] { return shape_now().density == 133; });
    shown("density 52");
    CHECK(note->isVisible() && dialog.size() == dialog_size);
    patchy::test::ui::save_widget_artifact("ui_local_051_density_note", dialog);
    CHECK(!color_close(canvas_pixel_center(*canvas, QPoint(90, 230)), QColor(28, 52, 92), 20));
    for (const double value : {1.5, 3.0, 4.5, 6.0, 7.5, 9.0}) feather->setValue(value);
    (void)process_events_until([&] { return std::abs(shape_now().feather - 9.0) < 1e-9; });
    shown("feather 9");
    density->setValue(80);
    (void)process_events_until([&] { return shape_now().density == 204; });
    shown("density 80");
    dialog.accept();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  settle(*canvas);
  if (!live.empty()) throw std::runtime_error("Shape Appearance's live view differs from the document:" + live);
  CHECK(shape_now().density == 204 && std::abs(shape_now().feather - 9.0) < 1e-9);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before + 1);
  CHECK(canvas_matches_document(*canvas, doc, all, 14));
  MainWindowTestAccess::undo(window);
  settle(*canvas);
  CHECK(shape_now().density == 133);
  CHECK(std::ranges::equal(std::as_const(doc).find_layer(id)->pixels().data(), opened.pixels().data()));
  CHECK(canvas_matches_document(*canvas, doc, all, 14));
  // Cancel restores the layer and the view.
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    dialog.findChild<QSpinBox*>(QStringLiteral("shapeDensitySpin"))->setValue(100);
    dialog.findChild<QDoubleSpinBox*>(QStringLiteral("shapeFeatherSpin"))->setValue(0.0);
    (void)process_events_until([&] { return shape_now().density == 255 && shape_now().feather < 0.05; });
    settle(*canvas);
    dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  settle(*canvas);
  CHECK(shape_now().density == 133 && std::abs(shape_now().feather - 9.0) < 1e-9);
  CHECK(std::ranges::equal(std::as_const(doc).find_layer(id)->pixels().data(), opened.pixels().data()));
  CHECK(canvas_matches_document(*canvas, doc, all, 14));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == before);
  canvas->set_vector_preview_enabled(false);
}

// --- every live field, for each kind of shape ---

// Whether the layer's pixels are what its own vector content draws: the same bounds and bytes.
bool raster_matches_vector(const Document& doc, LayerId id) {
  const auto* layer = std::as_const(doc).find_layer(id);
  if (layer == nullptr || layer->vector_shape() == nullptr) return false;
  Layer fresh = *layer;
  update_vector_shape_raster(fresh, Rect::from_size(doc.width(), doc.height()), &doc.metadata().patterns);
  const auto& drawn = std::as_const(fresh);
  return layer->bounds() == drawn.bounds() && std::ranges::equal(layer->pixels().data(), drawn.pixels().data());
}

bool same_layer_content(const Layer& a, const Layer& b) {
  return a.bounds() == b.bounds() && std::ranges::equal(a.pixels().data(), b.pixels().data()) &&
         a.vector_shape() != nullptr && b.vector_shape() != nullptr && a.vector_shape()->path == b.vector_shape()->path &&
         a.vector_shape()->fill == b.vector_shape()->fill && a.vector_shape()->stroke == b.vector_shape()->stroke &&
         a.vector_shape()->feather == b.vector_shape()->feather && a.vector_shape()->density == b.vector_shape()->density &&
         a.opacity() == b.opacity() && a.fill_opacity() == b.fill_opacity();
}

// What is wrong with the document and the canvas right now, or nothing:
// - the layer's pixels are what its outline and paints draw;
// - the sharp view of the document (200%, reduced) is its render;
// - the canvas shows this document: forcing a fresh frame changes no pixel.
std::string paint_text(const VectorFill& paint) {
  char text[200];
  if (paint.kind == VectorFillKind::Gradient) {
    const auto& g = paint.gradient;
    std::snprintf(text, sizeof(text), "gradient '%s' form %d type %d angle %g scale %g reverse %d dither %d method %d "
                  "aligned %d stops %zu/%zu smoothness %d", g.name.c_str(), static_cast<int>(g.form),
                  static_cast<int>(g.type), static_cast<double>(g.angle_degrees), static_cast<double>(g.scale),
                  g.reverse ? 1 : 0, g.dither ? 1 : 0, static_cast<int>(g.interpolation), g.align_with_layer ? 1 : 0,
                  g.color_stops.size(), g.alpha_stops.size(), static_cast<int>(g.smoothness));
  } else if (paint.kind == VectorFillKind::Pattern) {
    std::snprintf(text, sizeof(text), "pattern scale %g angle %g", static_cast<double>(paint.pattern_scale),
                  static_cast<double>(paint.pattern_angle_degrees));
  } else {
    std::snprintf(text, sizeof(text), "%s", paint.kind == VectorFillKind::Solid ? "solid" : "none");
  }
  return text;
}

std::string state_problems(CanvasWidget& canvas, const Document& doc, LayerId id, bool sharp, bool check_canvas,
                           const std::string& tag = {}) {
  std::string problems;
  if (!raster_matches_vector(doc, id)) problems += " the layer's pixels are not what its outline draws;";
  const auto scene = build_vector_preview_scene(doc);
  const QSize pixels(doc.width() * 2, doc.height() * 2);
  const auto frame = render_vector_preview(scene, {pixels, 2.0, {}});
  if (scene.fallback != VectorPreviewFallback::None || frame.fallback != VectorPreviewFallback::None) {
    problems += " the sharp view fell back;";
  } else {
    const auto expected = rgba(qimage_from_document(doc, true));
    const auto difference = compare(expected, reduce_to_document(rgba(frame.image), 2.0, expected.size()), true);
    // Loose enough for an Outside stroke's seam and an interpolated pattern (see Kind).
    if (difference.mean > 0.8 || difference.over_32 > 0.008 || difference.over_8 > 0.015) {
      char text[160];
      std::snprintf(text, sizeof(text), " the sharp view differs from the render (max %d, mean %.3f, over 8: %.2f%%, over 32: %.2f%%);",
                    difference.max, difference.mean, difference.over_8 * 100.0, difference.over_32 * 100.0);
      problems += text;
      if (const auto* layer = std::as_const(doc).find_layer(id); layer != nullptr && layer->vector_shape() != nullptr) {
        const auto& shape = *layer->vector_shape();
        char more[96];
        std::snprintf(more, sizeof(more), " width %g alignment %d dashes %zu feather %g density %d;",
                      shape.stroke.width, static_cast<int>(shape.stroke.alignment), shape.stroke.dashes.size(),
                      shape.feather, static_cast<int>(shape.density));
        problems += " fill " + paint_text(shape.fill) + "; stroke " +
                    (shape.stroke.enabled ? paint_text(shape.stroke.content) : std::string("off")) + more;
      }
      // The first differing states keep their pictures.
      static int saved = 0;
      if (!tag.empty() && saved < 24) {
        ++saved;
        const auto stem = QStringLiteral("test-artifacts/vector-preview-state-%1").arg(QString::fromStdString(file_name(tag)));
        (void)expected.save(stem + QStringLiteral("-document.png"));
        (void)reduce_to_document(rgba(frame.image), 2.0, expected.size()).save(stem + QStringLiteral("-preview-reduced.png"));
      }
    }
  }
  if (check_canvas) {
    settle(canvas);
    const auto shown = canvas.grab().toImage();
    if (sharp) {
      canvas.set_vector_preview_enabled(false);
      canvas.set_vector_preview_enabled(true);
    } else {
      canvas.document_changed();
    }
    settle(canvas);
    if (canvas.grab().toImage() != shown) problems += " the canvas showed an older picture;";
  }
  return problems;
}

// Changes one control to another value. False when it has nowhere to go.
bool change_control(QWidget* control) {
  if (auto* check = qobject_cast<QCheckBox*>(control)) {
    check->setChecked(!check->isChecked());
    return true;
  }
  if (auto* combo = qobject_cast<QComboBox*>(control)) {
    for (int step = 1; step < combo->count(); ++step) {
      const int index = (combo->currentIndex() + step) % combo->count();
      const auto text = combo->itemText(index);
      const bool enabled = (combo->model()->flags(combo->model()->index(index, combo->modelColumn(), combo->rootModelIndex())) & Qt::ItemIsEnabled) != 0;
      if (text.isEmpty() || text.endsWith(QStringLiteral("...")) || !enabled) continue;
      combo->setCurrentIndex(index);
      return true;
    }
    return false;
  }
  if (auto* spin = qobject_cast<QSpinBox*>(control)) {
    const int step = std::clamp((spin->maximum() - spin->minimum()) / 40, 1, 3);
    const int value = spin->value() + step <= spin->maximum() ? spin->value() + step : spin->value() - step;
    if (value == spin->value() || value < spin->minimum()) return false;
    spin->setValue(value);
    return true;
  }
  if (auto* spin = qobject_cast<QDoubleSpinBox*>(control)) {
    const double step = std::clamp((spin->maximum() - spin->minimum()) / 100.0, 0.5, 3.0);
    const double value = spin->value() + step <= spin->maximum() ? spin->value() + step : spin->value() - step;
    if (value == spin->value() || value < spin->minimum()) return false;
    spin->setValue(value);
    return true;
  }
  return false;
}

struct ShapeKind {
  const char* name;
  std::function<VectorShapeContent()> make;
};

std::vector<ShapeKind> dialog_kinds() {
  using Align = VectorStrokeAlignment;
  return {
      {"line", [] {
         auto shape = line_shape(40, 120, 190, 60, 6, true, true, 22, 26, 10);
         stroke(shape, 3, Align::Center);
         return shape;
       }},
      {"rounded rectangle", [] {
         auto shape = rect_shape(50, 40, 140, 96, 14);
         stroke(shape, 4, Align::Inside);
         return shape;
       }},
      {"ellipse", [] { return ellipse_shape(50, 36, 140, 104); }},
      {"star", [] { return polygon_shape(120, 90, 60, 5, 45); }},
      {"custom shape", [] {
         auto shape = custom_shape();
         stroke(shape, 3, Align::Outside);
         return shape;
       }},
      {"pen open path", [] {
         auto shape = pen_shape(false);
         stroke(shape, 4, Align::Center);
         return shape;
       }},
      {"fill layer", [] {
         VectorShapeContent shape;
         shape.fill.kind = VectorFillKind::Solid;
         shape.fill.color = {120, 170, 210};
         return shape;
       }},
  };
}

// One Shape Appearance session on one kind of shape: every spin box, list and checkbox the
// dialog shows is changed in turn (and the gradient and pattern rows once those paints are
// chosen); after each change lands the document and the canvas are checked. Then Preview
// off, Preview on, Reset, and OK (with Undo) or Cancel.
void drive_shape_appearance(const ShapeKind& kind, bool sharp, bool accept, std::vector<std::string>& failures) {
  PreviewPreferenceGuard preference;
  MainWindow window; show_window(window);
  MainWindowTestAccess::create_document(window, 240, 180);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto& doc = MainWindowTestAccess::document(window);
  const auto id = add_shape(doc, kind.make(), "Shape 1").id();
  doc.set_active_layer(id);
  canvas->document_changed();
  select_layer(window, id);
  canvas->set_vector_preview_enabled(sharp);
  canvas->set_view_zoom_centered(2.0);
  settle(*canvas);
  const Layer opened = *std::as_const(doc).find_layer(id);
  const auto layer_now = [&]() -> const Layer& { return *std::as_const(doc).find_layer(id); };
  const auto idle = [&] { return !canvas->processing_operation_active(); };
  const std::string label = std::string(kind.name) + (sharp ? ", sharp view" : ", pixel view");
  const auto note = [&](const std::string& where, const std::string& problems) {
    if (!problems.empty()) failures.push_back(label + ", " + where + ":" + problems);
  };
  note("before the dialog", state_problems(*canvas, doc, id, sharp, true));
  const auto before = MainWindowTestAccess::active_session_undo_depth(window);
  int changed = 0;
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& dialog) {
    std::vector<QString> visited{QStringLiteral("shapeAppearancePreviewCheck"),
                                 QStringLiteral("shapeGeometryCustomShapeCombo")};
    const auto landed = [&] {
      (void)process_events_until(idle);
      QApplication::processEvents();
      (void)process_events_until(idle);
    };
    const auto pass = [&] {
      for (int guard = 0; guard < 200; ++guard) {
        QWidget* next = nullptr;
        for (auto* widget : dialog.findChildren<QWidget*>()) {
          if (widget->objectName().isEmpty() || !widget->isVisibleTo(&dialog) || !widget->isEnabled()) continue;
          if (qobject_cast<QAbstractSpinBox*>(widget) == nullptr && qobject_cast<QComboBox*>(widget) == nullptr &&
              qobject_cast<QCheckBox*>(widget) == nullptr) continue;
          if (std::find(visited.begin(), visited.end(), widget->objectName()) != visited.end()) continue;
          next = widget;
          break;
        }
        if (next == nullptr) break;
        const auto name = next->objectName();
        visited.push_back(name);
        if (!change_control(next)) continue;
        ++changed;
        landed();
        // The canvas is compared after every change in the sharp view, every fourth otherwise.
        note(name.toStdString(), state_problems(*canvas, doc, id, sharp, sharp || changed % 4 == 0,
                                                sharp ? std::string(kind.name) + " " + name.toStdString() : std::string()));
      }
    };
    pass();
    // The rows of a gradient and of a pattern paint show once that paint is chosen.
    for (const char* list : {"shapeFillKindCombo", "shapeStrokePaintCombo"}) {
      auto* combo = dialog.findChild<QComboBox*>(QLatin1String(list));
      if (combo == nullptr || !combo->isVisibleTo(&dialog) || !combo->isEnabled()) continue;
      for (const auto paint : {VectorFillKind::Gradient, VectorFillKind::Pattern}) {
        const int index = combo->findData(static_cast<int>(paint));
        if (index < 0 || index == combo->currentIndex()) continue;
        combo->setCurrentIndex(index);
        landed();
        note(std::string(list) + " paint " + std::to_string(static_cast<int>(paint)),
             state_problems(*canvas, doc, id, sharp, true));
        pass();
      }
    }
    // Preview off shows the layer as it was; on again shows the edits.
    auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("shapeAppearancePreviewCheck"));
    if (preview != nullptr) {
      preview->setChecked(false);
      landed();
      if (!same_layer_content(layer_now(), opened)) note("Preview off", " the layer is not as it was;");
      note("Preview off", state_problems(*canvas, doc, id, sharp, true));
      preview->setChecked(true);
      landed();
      note("Preview on", state_problems(*canvas, doc, id, sharp, true));
    }
    if (auto* reset = dialog.findChild<QPushButton*>(QStringLiteral("shapeAppearanceResetButton"))) {
      reset->click();
      landed();
      note("Reset", state_problems(*canvas, doc, id, sharp, true));
    }
    if (accept) dialog.accept(); else dialog.reject();
  });
  MainWindowTestAccess::edit_active_shape_appearance(window);
  (void)process_events_until(idle);
  settle(*canvas);
  if (changed < 4) failures.push_back(label + ": the dialog offered fewer than four fields");
  if (accept) {
    note("after OK", state_problems(*canvas, doc, id, sharp, true));
    if (MainWindowTestAccess::active_session_undo_depth(window) != before + 1) {
      failures.push_back(label + ": OK was not one undo step");
    } else {
      MainWindowTestAccess::undo(window);
      settle(*canvas);
      if (!same_layer_content(layer_now(), opened)) failures.push_back(label + ": Undo did not restore the layer");
      note("after Undo", state_problems(*canvas, doc, id, sharp, true));
    }
  } else {
    if (!same_layer_content(layer_now(), opened)) failures.push_back(label + ": Cancel did not restore the layer");
    if (MainWindowTestAccess::active_session_undo_depth(window) != before) failures.push_back(label + ": Cancel left an undo step");
    note("after Cancel", state_problems(*canvas, doc, id, sharp, true));
  }
  canvas->set_vector_preview_enabled(false);
}

void throw_failures(const std::vector<std::string>& failures, const char* what) {
  if (failures.empty()) return;
  {
    std::filesystem::create_directories("test-artifacts");
    std::ofstream report(std::filesystem::path("test-artifacts") / "vector-preview-states.txt", std::ios::binary);
    for (const auto& line : failures) report << line << "\n";
  }
  std::string message = std::to_string(failures.size()) + std::string(" ") + what + ":";
  for (std::size_t index = 0; index < failures.size() && index < 40; ++index) message += "\n  " + failures[index];
  throw std::runtime_error(message);
}

// local-051 follow-up 2: for every kind of shape, each live field of Shape Appearance shows
// on the canvas when its render lands and still shows once the view settles, with Dynamic
// Vector Preview on (ending in OK and Undo) and off (ending in Cancel).
void ui_shape_appearance_fields_show_on_the_canvas_for_every_shape_kind() {
  std::vector<std::string> failures;
  for (const auto& kind : dialog_kinds()) {
    drive_shape_appearance(kind, true, true, failures);
    drive_shape_appearance(kind, false, false, failures);
  }
  throw_failures(failures, "Shape Appearance states are wrong");
}

// The options bar's fields edit a selected shape the same way: Stroke width, the dash list,
// Weight and the arrowhead toggles of a line, Radius of a rectangle, and W / H.
void ui_shape_options_bar_fields_show_on_the_canvas() {
  std::vector<std::string> failures;
  for (const bool sharp : {true, false}) {
    for (const bool line : {true, false}) {
      PreviewPreferenceGuard preference;
      SettingsValueRestorer arrow_start_setting(QStringLiteral("tools/lineArrowStart"));
      SettingsValueRestorer arrow_end_setting(QStringLiteral("tools/lineArrowEnd"));
      VectorSettingsGuard guard;
      MainWindow window; show_window(window);
      MainWindowTestAccess::create_document(window, 240, 180);
      QApplication::processEvents();
      auto* canvas = require_canvas(window);
      auto& doc = MainWindowTestAccess::document(window);
      auto shape = line ? line_shape(40, 120, 190, 60, 6, false, true, 22, 26, 0) : rect_shape(50, 40, 140, 96, 0);
      stroke(shape, 3, VectorStrokeAlignment::Center);
      const auto id = add_shape(doc, std::move(shape), "Shape 1").id();
      doc.set_active_layer(id);
      canvas->document_changed();
      select_layer(window, id);
      require_action(window, line ? "toolLineAction" : "toolRectAction")->trigger();
      QApplication::processEvents();
      select_layer(window, id);
      canvas->set_vector_preview_enabled(sharp);
      canvas->set_view_zoom_centered(2.0);
      settle(*canvas);
      const std::string label = std::string(line ? "line" : "rectangle") + (sharp ? ", sharp view" : ", pixel view");
      const auto shape_now = [&]() -> const VectorShapeContent& { return *std::as_const(doc).find_layer(id)->vector_shape(); };
      const auto check = [&](const char* field, bool applied) {
        if (!applied) failures.push_back(label + ", " + field + ": the edit did not reach the shape");
        const auto problems = state_problems(*canvas, doc, id, sharp, true);
        if (!problems.empty()) failures.push_back(label + ", " + field + ":" + problems);
      };
      const auto wait = [&](const std::function<bool()>& done) {
        const bool landed = process_events_until(done, 4000);
        (void)process_events_until([&] { return !canvas->processing_operation_active(); });
        return landed;
      };
      const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
      auto* stroke_width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorStrokeWidthSpin"));
      auto* dashes = window.findChild<QComboBox*>(QStringLiteral("vectorStrokeDashCombo"));
      CHECK(stroke_width != nullptr && dashes != nullptr);
      stroke_width->setValue(9.0);
      check("Stroke width", wait([&] { return shape_now().stroke.width == 9.0; }));
      stroke_width->setValue(0.0);
      check("Stroke off", wait([&] { return !shape_now().stroke.enabled; }));
      stroke_width->setValue(5.0);
      check("Stroke on", wait([&] { return shape_now().stroke.enabled && shape_now().stroke.width == 5.0; }));
      dashes->setCurrentIndex(dashes->findData(static_cast<int>(StrokeDashPreset::Dashed)));
      check("Dashes", wait([&] { return !shape_now().stroke.dashes.empty(); }));
      if (line) {
        auto* weight = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
        auto* arrow_start = window.findChild<QPushButton*>(QStringLiteral("lineArrowStartButton"));
        auto* arrow_end = window.findChild<QPushButton*>(QStringLiteral("lineArrowEndButton"));
        CHECK(weight != nullptr && arrow_start != nullptr && arrow_end != nullptr);
        weight->setValue(12.0);
        QMetaObject::invokeMethod(weight, "editingFinished", Qt::DirectConnection);
        check("Weight", wait([&] { return shape_now().origination.front().line_weight == 12.0; }));
        arrow_start->click();
        check("Start arrowhead", wait([&] { return shape_now().origination.front().arrow_start; }));
        arrow_end->click();
        check("End arrowhead", wait([&] { return !shape_now().origination.front().arrow_end; }));
      } else {
        auto* radius = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
        CHECK(radius != nullptr);
        radius->setValue(18);
        QMetaObject::invokeMethod(radius, "editingFinished", Qt::DirectConnection);
        check("Radius", wait([&] { return shape_now().origination.front().corner_radii[0] == 18.0; }));
      }
      auto* width = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeWidthSpin"));
      auto* height = window.findChild<QDoubleSpinBox*>(QStringLiteral("vectorShapeHeightSpin"));
      CHECK(width != nullptr && height != nullptr);
      const auto bounds_width = [&] {
        const auto bounds = shape_now().path.bounds();
        return bounds ? bounds->right - bounds->left : 0.0;
      };
      const double wanted = std::floor(bounds_width()) - 20.0;
      width->setValue(wanted);
      QMetaObject::invokeMethod(width, "editingFinished", Qt::DirectConnection);
      check("W", wait([&] { return std::abs(bounds_width() - wanted) < 0.6; }));
      // Every edit was its own undo step; undoing them all shows the first picture again.
      const auto steps = MainWindowTestAccess::active_session_undo_depth(window) - depth;
      if (steps < 5) failures.push_back(label + ": the edits left fewer undo steps than fields");
      for (std::size_t step = 0; step < steps; ++step) MainWindowTestAccess::undo(window);
      settle(*canvas);
      check("Undo", shape_now().stroke.width == 3.0 && shape_now().stroke.dashes.empty());
      canvas->set_vector_preview_enabled(false);
    }
  }
  throw_failures(failures, "options bar edits are wrong");
}

}  // namespace

std::vector<patchy::test::TestCase> vector_preview_parity_tests() {
  return {
      {"ui_vector_preview_matches_document_render_for_shape_layers",
       ui_vector_preview_matches_document_render_for_shape_layers},
      {"ui_vector_preview_shows_shape_feather_and_density_at_every_zoom",
       ui_vector_preview_shows_shape_feather_and_density_at_every_zoom},
      {"ui_shape_appearance_fields_show_on_the_canvas_for_every_shape_kind",
       ui_shape_appearance_fields_show_on_the_canvas_for_every_shape_kind},
      {"ui_shape_options_bar_fields_show_on_the_canvas", ui_shape_options_bar_fields_show_on_the_canvas},
  };
}
