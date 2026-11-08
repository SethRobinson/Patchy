// Type > Convert to Shape and Type > Create Work Path (GitHub issue 598;
// docs/text-outlines.md): text layers become shape layers and work paths from their glyph
// outlines. The shape must cover the pixels the text drew (point, area, vertical and warped
// text), keep the layer's own properties, undo in one step, survive a PSD round trip, and the
// outline grouping must fill like the nonzero glyph outlines it came from.
// Also the Type menu's rows and submenus (GitHub issue 650).
#include "core/document.hpp"
#include "core/document_path.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_warp.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_outline.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonValue>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::ScriptEngineHost;

QJsonValue run_script(MainWindow& window, const QString& code) {
  auto& host = window.script_engine_host();
  ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("text-outline-test");
  options.unattended = true;
  const bool started = host.run_source(
      QStringLiteral("function check(v,m){if(!v)throw Error(m||'check failed');}"
                     "function reject(f){var bad=false;try{f();}catch(e){bad=true;}check(bad,'expected refusal');}\n") +
          code,
      std::move(options));
  CHECK(started);
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& message : host.message_backlog()) {
      std::fprintf(stderr, "%s\n", message.toUtf8().constData());
    }
  }
  CHECK(!host.last_run_had_error());
  return host.last_result();
}

const patchy::Layer* find_named(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* found = find_named(layer.children(), name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

std::uint8_t alpha_at(const patchy::Layer& layer, int x, int y) {
  const auto bounds = layer.bounds();
  const auto& pixels = layer.pixels();
  const int local_x = x - bounds.x;
  const int local_y = y - bounds.y;
  if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
    return 0;
  }
  return pixels.pixel(local_x, local_y)[3];
}

struct CoverageMatch {
  double iou{0.0};
  double mean_alpha_difference{255.0};
};

// How well two layers' rasters agree over the union of their bounds: the intersection over union
// of their half-covered pixels and the mean alpha difference over the pixels either one inks.
CoverageMatch compare_coverage(const patchy::Layer& a, const patchy::Layer& b) {
  const auto ab = a.bounds();
  const auto bb = b.bounds();
  const int left = std::min(ab.x, bb.x);
  const int top = std::min(ab.y, bb.y);
  const int right = std::max(ab.x + ab.width, bb.x + bb.width);
  const int bottom = std::max(ab.y + ab.height, bb.y + bb.height);
  std::int64_t both = 0;
  std::int64_t either = 0;
  std::int64_t inked = 0;
  double difference = 0.0;
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const int alpha_a = alpha_at(a, x, y);
      const int alpha_b = alpha_at(b, x, y);
      both += (alpha_a >= 128 && alpha_b >= 128) ? 1 : 0;
      either += (alpha_a >= 128 || alpha_b >= 128) ? 1 : 0;
      if (alpha_a > 0 || alpha_b > 0) {
        ++inked;
        difference += std::abs(alpha_a - alpha_b);
      }
    }
  }
  CoverageMatch match;
  match.iou = either > 0 ? static_cast<double>(both) / static_cast<double>(either) : 0.0;
  match.mean_alpha_difference = inked > 0 ? difference / static_cast<double>(inked) : 255.0;
  return match;
}

// The visible (alpha > 0) pixel box of a layer, in document pixels.
QRectF ink_rect(const patchy::Layer& layer) {
  const auto bounds = layer.bounds();
  int min_x = bounds.x + bounds.width;
  int min_y = bounds.y + bounds.height;
  int max_x = bounds.x - 1;
  int max_y = bounds.y - 1;
  for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
    for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
      if (alpha_at(layer, x, y) > 0) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  return max_x < min_x ? QRectF() : QRectF(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

// The alpha-weighted centre of a layer's ink, in document pixels.
QPointF ink_centroid(const patchy::Layer& layer) {
  const auto bounds = layer.bounds();
  double mass = 0.0;
  double sum_x = 0.0;
  double sum_y = 0.0;
  for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
    for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
      const double alpha = alpha_at(layer, x, y);
      mass += alpha;
      sum_x += alpha * (x + 0.5);
      sum_y += alpha * (y + 0.5);
    }
  }
  return mass > 0.0 ? QPointF(sum_x / mass, sum_y / mass) : QPointF();
}

// The exact (curve-tight) bounds of a path's outlines.
QRectF path_rect(const patchy::VectorPath& path) {
  QPainterPath painter_path;
  for (const auto& subpath : path.subpaths) {
    if (subpath.anchors.empty()) {
      continue;
    }
    const auto& first = subpath.anchors.front();
    painter_path.moveTo(first.anchor_x, first.anchor_y);
    for (std::size_t index = 0; index < subpath.anchors.size(); ++index) {
      const auto& from = subpath.anchors[index];
      const auto& to = subpath.anchors[(index + 1) % subpath.anchors.size()];
      painter_path.cubicTo(from.out_x, from.out_y, to.in_x, to.in_y, to.anchor_x, to.anchor_y);
    }
  }
  return painter_path.boundingRect();
}

bool rects_close(const QRectF& a, const QRectF& b, double tolerance) {
  return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance &&
         std::abs(a.right() - b.right()) <= tolerance && std::abs(a.bottom() - b.bottom()) <= tolerance;
}

// A copy of a layer's raster for comparing after the layer changes.
patchy::Layer raster_copy(const patchy::Layer& layer) {
  patchy::Layer copy(0, "copy", layer.pixels());
  copy.set_bounds(layer.bounds());
  return copy;
}

void select_only(MainWindow& window, patchy::LayerId id) { MainWindowTestAccess::select_layers(window, {id}, id); }

// Builds the test document: a white background and the point text "Shape 598" in Arial.
patchy::LayerId add_point_text(MainWindow& window) {
  run_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(520, 220);
    d.activeLayer.fill('#ffffff');
    var t = d.addTextLayer('Shape 598', {font: 'Arial', size: 72, x: 30, y: 50, color: '#c03020'});
    t.name = 'Title';
  )JS"));
  const auto* layer = find_named(MainWindowTestAccess::document(window).layers(), "Title");
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  return layer != nullptr ? layer->id() : patchy::LayerId{};
}

void ui_text_convert_to_shape_matches_text_and_undoes() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  const auto id = add_point_text(window);
  auto& document = MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(id);
  if (layer == nullptr) {
    return;
  }
  layer->set_opacity(0.75F);
  layer->set_blend_mode(patchy::BlendMode::Multiply);
  layer->layer_style().drop_shadows.emplace_back();
  const auto text_raster = raster_copy(*layer);
  const auto text_ink = ink_rect(*layer);
  select_only(window, id);
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);

  auto* convert = patchy::test::ui::require_action(window, "typeConvertToShapeAction");
  CHECK(convert != nullptr);
  convert->trigger();
  QApplication::processEvents();

  const auto* shape = MainWindowTestAccess::document(window).find_layer(id);
  CHECK(shape != nullptr);
  if (shape == nullptr) {
    return;
  }
  CHECK(!patchy::layer_is_text(*shape));
  CHECK(patchy::layer_is_vector_shape(*shape));
  CHECK(shape->name() == "Title");
  CHECK(std::abs(shape->opacity() - 0.75F) < 1e-6F);
  CHECK(shape->blend_mode() == patchy::BlendMode::Multiply);
  CHECK(shape->layer_style().drop_shadows.size() == 1U);
  const auto& content = *shape->vector_shape();
  CHECK(content.parts.empty());
  CHECK(content.fill.kind == patchy::VectorFillKind::Solid);
  CHECK((content.fill.color == patchy::RgbColor{0xc0, 0x30, 0x20}));
  CHECK(!content.stroke.enabled);
  // Real outlines, not a trace: "S", "h", "a", "p", "e", "5", "9", "8" at least, holes grouped
  // with their glyph (the "a" bowl, the two counters of the "8").
  CHECK(content.path.subpaths.size() >= 12U);
  bool shared_group = false;
  for (std::size_t index = 1; index < content.path.subpaths.size(); ++index) {
    shared_group = shared_group ||
                   content.path.subpaths[index].shape_group == content.path.subpaths[index - 1].shape_group;
  }
  CHECK(shared_group);
  const auto outline = path_rect(content.path);
  std::printf("[INFO] text ink (%.1f, %.1f, %.1f x %.1f), outline (%.2f, %.2f, %.2f x %.2f)\n", text_ink.x(),
              text_ink.y(), text_ink.width(), text_ink.height(), outline.x(), outline.y(), outline.width(),
              outline.height());
  CHECK(rects_close(outline, text_ink, 1.5));
  const auto match = compare_coverage(text_raster, *shape);
  std::printf("[INFO] point text coverage IoU %.4f, mean alpha difference %.2f\n", match.iou,
              match.mean_alpha_difference);
  CHECK(match.iou > 0.97);
  CHECK(match.mean_alpha_difference < 12.0);

  // One undo step brings the text layer back with its pixels.
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  const auto* restored = MainWindowTestAccess::document(window).find_layer(id);
  CHECK(restored != nullptr && patchy::layer_is_text(*restored) && !patchy::layer_is_vector_shape(*restored));
  if (restored != nullptr) {
    CHECK(compare_coverage(text_raster, *restored).iou > 0.999);
  }
}

void ui_text_create_work_path_keeps_text() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  const auto id = add_point_text(window);
  select_only(window, id);
  const auto* text = MainWindowTestAccess::document(window).find_layer(id);
  CHECK(text != nullptr);
  if (text == nullptr) {
    return;
  }
  const auto text_ink = ink_rect(*text);
  auto* create = patchy::test::ui::require_action(window, "typeCreateWorkPathAction");
  CHECK(create != nullptr);
  create->trigger();
  QApplication::processEvents();
  auto& document = MainWindowTestAccess::document(window);
  const auto* kept = document.find_layer(id);
  CHECK(kept != nullptr && patchy::layer_is_text(*kept));
  const auto* work = std::as_const(document).work_path();
  CHECK(work != nullptr);
  if (work == nullptr) {
    return;
  }
  CHECK(work->path().subpaths.size() >= 12U);
  CHECK(rects_close(path_rect(work->path()), text_ink, 1.5));
  // A second Create Work Path replaces the work path rather than adding a second one.
  create->trigger();
  QApplication::processEvents();
  int work_paths = 0;
  for (const auto& path : std::as_const(document).paths()) {
    work_paths += path.kind() == patchy::DocumentPathKind::Work ? 1 : 0;
  }
  CHECK(work_paths == 1);
}

// Area text, vertical type and warped text convert too: the shape covers the text's pixels.
void ui_text_convert_to_shape_handles_area_vertical_and_warp() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  run_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(900, 700);
    d.activeLayer.fill('#ffffff');
    var ring = d.addShape('Area', {type: 'ellipse', x: 20, y: 20, width: 380, height: 300});
    var area = d.addTextLayer('Text inside a shape wraps to the outline of the ellipse here.',
                              {font: 'Arial', size: 30, area: ring.getShape().path, color: '#204080'});
    area.name = 'AreaText';
    ring.remove();
    var vertical = d.addTextLayer('Tall', {font: 'Arial', size: 64, x: 460, y: 30, orientation: 'vertical',
                                           color: '#106030'});
    vertical.name = 'VerticalText';
    var warped = d.addTextLayer('Arched', {font: 'Arial', size: 80, x: 420, y: 420, color: '#802080'});
    warped.name = 'WarpedText';
  )JS"));
  auto& document = MainWindowTestAccess::document(window);
  auto* warped = const_cast<patchy::Layer*>(find_named(document.layers(), "WarpedText"));
  CHECK(warped != nullptr);
  if (warped == nullptr) {
    return;
  }
  patchy::TextWarp warp;
  warp.style = "warpArc";
  warp.value = 45.0;
  CHECK(MainWindowTestAccess::apply_text_warp(window, *warped, warp));
  CHECK(patchy::text_warp_from_layer(*warped).has_value());
  // The unconverted text, for trying the commands by hand (the desk card's test file).
  std::filesystem::create_directories("test-artifacts");
  patchy::psd::DocumentIo::write_layered_rgb8_file(MainWindowTestAccess::document(window),
                                                   std::filesystem::path("test-artifacts/text-to-shape-sources.psd"));
  for (const char* name : {"AreaText", "VerticalText", "WarpedText"}) {
    const auto* text = find_named(MainWindowTestAccess::document(window).layers(), name);
    CHECK(text != nullptr && patchy::layer_is_text(*text));
    if (text == nullptr) {
      continue;
    }
    const auto id = text->id();
    const auto text_raster = raster_copy(*text);
    select_only(window, id);
    patchy::test::ui::require_action(window, "typeConvertToShapeAction")->trigger();
    QApplication::processEvents();
    const auto* shape = MainWindowTestAccess::document(window).find_layer(id);
    CHECK(shape != nullptr && patchy::layer_is_vector_shape(*shape) && !patchy::layer_is_text(*shape));
    if (shape == nullptr) {
      continue;
    }
    const auto match = compare_coverage(text_raster, *shape);
    const auto text_ink = ink_rect(text_raster);
    const auto shape_ink = ink_rect(*shape);
    std::printf("[INFO] %s coverage IoU %.4f, mean alpha difference %.2f, %zu subpaths, text ink (%.0f, %.0f, %.0f x "
                "%.0f), shape ink (%.0f, %.0f, %.0f x %.0f)\n",
                name, match.iou, match.mean_alpha_difference, shape->vector_shape()->path.subpaths.size(), text_ink.x(),
                text_ink.y(), text_ink.width(), text_ink.height(), shape_ink.x(), shape_ink.y(), shape_ink.width(),
                shape_ink.height());
    // The text raster and the shape raster side by side, for review.
    {
      const auto box = text_ink.united(shape_ink).toAlignedRect().adjusted(-4, -4, 4, 4);
      QImage pair(box.width() * 2 + 8, box.height(), QImage::Format_ARGB32);
      pair.fill(Qt::white);
      for (int y = 0; y < box.height(); ++y) {
        for (int x = 0; x < box.width(); ++x) {
          const int left = 255 - alpha_at(text_raster, box.x() + x, box.y() + y);
          const int right = 255 - alpha_at(*shape, box.x() + x, box.y() + y);
          pair.setPixel(x, y, qRgb(left, left, left));
          pair.setPixel(box.width() + 8 + x, y, qRgb(right, right, right));
        }
      }
      patchy::test::ui::save_image_artifact(std::string("ui_text_outline_") + name, pair);
    }
    // Vertical cells draw as glyph runs, which the raster engine places on whole pixels (about
    // 0.2 px from the exact outline here), and the warped raster is a bilinear resample of a
    // supersampled render: both lose some overlap on thin stems. The ink centres must still agree.
    // macOS fills the outlines themselves (draw_document_glyph_run): area 0.986, vertical 0.994.
    CHECK(match.iou > (std::string(name) == "AreaText" ? 0.97 : 0.9));
    const auto text_centre = ink_centroid(text_raster);
    const auto shape_centre = ink_centroid(*shape);
    std::printf("[INFO] %s ink centre moved by (%.3f, %.3f)\n", name, shape_centre.x() - text_centre.x(),
                shape_centre.y() - text_centre.y());
    CHECK(std::abs(shape_centre.x() - text_centre.x()) < 0.5 && std::abs(shape_centre.y() - text_centre.y()) < 0.5);
  }
  // The converted area, vertical and warped shapes, for a Photoshop open check.
  patchy::psd::DocumentIo::write_layered_rgb8_file(MainWindowTestAccess::document(window),
                                                   std::filesystem::path("test-artifacts/ui_text_outline_converted.psd"));
}

// Point text at the sizes Qt draws from cached glyph images (up to 64 px). macOS fills the glyph
// outlines instead (draw_document_glyph_run): CoreText's smoothed images covered only 0.80 of
// their own outlines at 30 and 64 px, against 0.99 above 64 px. FreeType's unhinted images (the
// offscreen suite on Windows and Linux) cover 0.90 at 30 px and 0.96 at 64 px.
void ui_text_convert_to_shape_matches_small_point_text() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  run_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(760, 220);
    d.activeLayer.fill('#ffffff');
    var small = d.addTextLayer('Point thirty px', {font: 'Arial', size: 30, x: 20, y: 20, color: '#204080'});
    small.name = 'Point30';
    var cached = d.addTextLayer('Point sixty-four', {font: 'Arial', size: 64, x: 20, y: 90, color: '#204080'});
    cached.name = 'Point64';
  )JS"));
  for (const char* name : {"Point30", "Point64"}) {
    const auto* text = find_named(MainWindowTestAccess::document(window).layers(), name);
    CHECK(text != nullptr && patchy::layer_is_text(*text));
    if (text == nullptr) {
      continue;
    }
    const auto id = text->id();
    const auto text_raster = raster_copy(*text);
    select_only(window, id);
    patchy::test::ui::require_action(window, "typeConvertToShapeAction")->trigger();
    QApplication::processEvents();
    const auto* shape = MainWindowTestAccess::document(window).find_layer(id);
    CHECK(shape != nullptr && patchy::layer_is_vector_shape(*shape));
    if (shape == nullptr) {
      continue;
    }
    const auto match = compare_coverage(text_raster, *shape);
    std::printf("[INFO] %s coverage IoU %.4f, mean alpha difference %.2f\n", name, match.iou,
                match.mean_alpha_difference);
#ifdef Q_OS_MACOS
    CHECK(match.iou > 0.97);
#else
    CHECK(match.iou > 0.88);
#endif
  }
}

// Several text colours keep their colours as paint parts, and the converted layers survive a PSD
// round trip with their outlines intact.
void ui_text_convert_to_shape_keeps_colors_and_round_trips_psd() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  run_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(520, 220);
    d.activeLayer.fill('#ffffff');
    var t = d.addTextLayer([{text: 'Red', font: 'Arial', size: 64, color: '#d02020'},
                            {text: 'Blue', font: 'Arial', size: 64, color: '#2030d0'}], {x: 30, y: 60});
    t.name = 'TwoColors';
    var s = d.addTextLayer('Solo', {font: 'Arial', size: 48, x: 30, y: 140, color: '#101010'});
    s.name = 'Solo';
  )JS"));
  std::vector<patchy::LayerId> ids;
  for (const char* name : {"TwoColors", "Solo"}) {
    const auto* text = find_named(MainWindowTestAccess::document(window).layers(), name);
    CHECK(text != nullptr);
    if (text != nullptr) {
      ids.push_back(text->id());
    }
  }
  if (ids.size() != 2U) {
    return;
  }
  // Both at once: one undo step for the pair.
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  MainWindowTestAccess::select_layers(window, ids, ids.back());
  patchy::test::ui::require_action(window, "typeConvertToShapeAction")->trigger();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  auto& document = MainWindowTestAccess::document(window);
  const auto* two = document.find_layer(ids.front());
  const auto* solo = document.find_layer(ids.back());
  CHECK(two != nullptr && solo != nullptr && patchy::layer_is_vector_shape(*two) &&
        patchy::layer_is_vector_shape(*solo));
  if (two == nullptr || solo == nullptr) {
    return;
  }
  const auto& parts = two->vector_shape()->parts;
  CHECK(parts.size() == 2U);
  if (parts.size() == 2U) {
    CHECK((parts[0].fill.color == patchy::RgbColor{0xd0, 0x20, 0x20}));
    CHECK((parts[1].fill.color == patchy::RgbColor{0x20, 0x30, 0xd0}));
  }
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::filesystem::create_directories("test-artifacts");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document,
                                                   std::filesystem::path("test-artifacts/ui_text_convert_to_shape.psd"));
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const auto* solo_back = find_named(reread.layers(), "Solo");
  const auto* two_back = find_named(reread.layers(), "TwoColors");
  CHECK(solo_back != nullptr && patchy::layer_is_vector_shape(*solo_back));
  CHECK(two_back != nullptr && patchy::layer_is_vector_shape(*two_back));
  if (solo_back != nullptr && solo_back->vector_shape() != nullptr) {
    CHECK(solo_back->vector_shape()->path.subpaths.size() == solo->vector_shape()->path.subpaths.size());
    CHECK(rects_close(path_rect(solo_back->vector_shape()->path), path_rect(solo->vector_shape()->path), 0.01));
    CHECK(compare_coverage(*solo, *solo_back).iou > 0.99);
  }
  if (two_back != nullptr && two_back->vector_shape() != nullptr) {
    CHECK(two_back->vector_shape()->parts.size() == 2U);
    CHECK(compare_coverage(*two, *two_back).iou > 0.99);
  }
}

void ui_script_text_convert_to_shape_and_work_path() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  run_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(400, 160);
    var box = d.addShape('Box', {type: 'ellipse', x: 300, y: 10, width: 40, height: 40});
    var t = d.addTextLayer('Path', {font: 'Arial', size: 64, x: 20, y: 30});
    var work = t.createWorkPath();
    check(work.kind === 'work', 'work path kind');
    check(t.isText, 'text kept');
    check(work.getPath().subpaths.length >= 4, 'outlines');
    var same = t.convertToShape();
    check(same.id === t.id && t.isShape && !t.isText, 'converted in place');
    check(t.getShape().path.subpaths.length >= 4, 'shape outlines');
    reject(function () { t.convertToShape(); });
    reject(function () { box.createWorkPath(); });
  )JS"));
}

// The Type menu and the Layers panel's text-layer menu offer both commands.
void ui_text_outline_commands_in_menus() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  add_point_text(window);
  auto* convert = patchy::test::ui::require_action(window, "typeConvertToShapeAction");
  auto* create = patchy::test::ui::require_action(window, "typeCreateWorkPathAction");
  CHECK(convert != nullptr && create != nullptr);
  bool in_type_menu = false;
  for (auto* menu : window.findChildren<QMenu*>()) {
    if (menu->menuAction() != nullptr && menu->menuAction()->text() == QStringLiteral("&Type")) {
      in_type_menu = menu->actions().contains(convert) && menu->actions().contains(create);
    }
  }
  CHECK(in_type_menu);
}

// Review captures for the desk card: the window with a text layer before and after Convert to
// Shape (Direct Select shows the new outlines), and the Type menu.
void ui_text_outline_review_captures() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  window.resize(1280, 760);
  run_script(window, QStringLiteral(R"JS(
    var d = app.newDocument(760, 300);
    d.activeLayer.fill('#ffffff');
    var t = d.addTextLayer('Patchy', {font: 'Arial', size: 150, bold: true, x: 70, y: 50, color: '#2f6fd0'});
    t.name = 'Patchy';
  )JS"));
  const auto* text = find_named(MainWindowTestAccess::document(window).layers(), "Patchy");
  CHECK(text != nullptr);
  if (text == nullptr) {
    return;
  }
  const auto id = text->id();
  select_only(window, id);
  QApplication::processEvents();
  patchy::test::ui::save_widget_artifact("ui_text_outline_before", window);
  patchy::test::ui::require_action(window, "typeConvertToShapeAction")->trigger();
  QApplication::processEvents();
  if (auto* direct = patchy::test::ui::require_hotkey_action(window, QStringLiteral("tools.direct_select"));
      direct != nullptr) {
    direct->trigger();
  }
  QApplication::processEvents();
  CHECK(patchy::layer_is_vector_shape(*MainWindowTestAccess::document(window).find_layer(id)));
  patchy::test::ui::save_widget_artifact("ui_text_outline_after", window);
  for (auto* menu : window.findChildren<QMenu*>()) {
    if (menu->menuAction() != nullptr && menu->menuAction()->text() == QStringLiteral("&Type")) {
      menu->popup(window.mapToGlobal(QPoint(200, 40)));
      QApplication::processEvents();
      patchy::test::ui::save_widget_artifact("ui_text_outline_type_menu", *menu);
      menu->close();
    }
  }
}

// The Type menu (GitHub issue 650): its rows top to bottom; a separator is "-", a submenu its
// menu's objectName.
QMenu* type_menu(MainWindow& window) {
  auto* menu = window.findChild<QMenu*>(QStringLiteral("typeMenu"));
  CHECK(menu != nullptr);
  return menu;
}

QStringList menu_rows(const QMenu& menu) {
  QStringList rows;
  for (const auto* action : menu.actions()) {
    rows << (action->isSeparator() ? QStringLiteral("-")
             : action->menu() != nullptr ? action->menu()->objectName()
                                         : action->objectName());
  }
  return rows;
}

// Opens and closes the menu, so its aboutToShow refresh runs as it does for a user.
void open_type_menu(MainWindow& window) {
  if (auto* menu = type_menu(window); menu != nullptr) {
    menu->popup(window.mapToGlobal(QPoint(200, 40)));
    QApplication::processEvents();
    menu->close();
    QApplication::processEvents();
  }
}

bool action_checked(MainWindow& window, const char* object_name) {
  auto* action = patchy::test::ui::require_action(window, object_name);
  return action != nullptr && action->isChecked();
}

bool submenu_enabled(MainWindow& window, const char* object_name) {
  auto* menu = window.findChild<QMenu*>(QLatin1String(object_name));
  CHECK(menu != nullptr);
  return menu != nullptr && menu->menuAction()->isEnabled();
}

QString metadata_value(MainWindow& window, patchy::LayerId id, const char* key) {
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
  if (layer == nullptr) {
    return {};
  }
  const auto found = layer->metadata().find(key);
  return found == layer->metadata().end() ? QString() : QString::fromStdString(found->second);
}

// Column `column` of the layer's first paragraph run (2: alignment, 9: direction).
QString first_paragraph_field(MainWindow& window, patchy::LayerId id, int column) {
  const auto runs = metadata_value(window, id, patchy::kLayerMetadataTextParagraphRuns);
  for (const auto& line : runs.split(QLatin1Char('\n'))) {
    const auto fields = line.trimmed().split(QLatin1Char('\t'));
    if (fields.size() >= 3) {
      return fields.size() > column ? fields[column] : QString();
    }
  }
  return {};
}

// The rows, their order, macOS roles, and which submenus are on: a selected text layer turns
// every one on; a pixel layer alone turns them off, except Orientation and Anti-Alias while the
// Type tool is active (they set the next new layer). The Type tool is not a row; T still picks it.
void ui_type_menu_rows_and_enabled_states() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  const auto id = add_point_text(window);
  auto* menu = type_menu(window);
  if (menu == nullptr) {
    return;
  }
  const QStringList expected{QStringLiteral("typeCharacterPanelAction"), QStringLiteral("typeParagraphPanelAction"),
                             QStringLiteral("-"),
                             QStringLiteral("typeAlignmentMenu"), QStringLiteral("typeDirectionMenu"),
                             QStringLiteral("typeOrientationMenu"), QStringLiteral("-"),
                             QStringLiteral("typeAntiAliasMenu"), QStringLiteral("typeOpenTypeMenu"),
                             QStringLiteral("-"),
                             QStringLiteral("typeCreateWorkPathAction"), QStringLiteral("typeConvertToShapeAction"),
                             QStringLiteral("-"),
                             QStringLiteral("typeRasterizeAction"), QStringLiteral("typeWarpTextAction"),
                             QStringLiteral("-"),
                             QStringLiteral("typeResolveMissingFontsAction")};
  const auto rows = menu_rows(*menu);
  if (rows != expected) {
    std::fprintf(stderr, "Type menu rows: %s\n", rows.join(QLatin1Char(',')).toUtf8().constData());
  }
  CHECK(rows == expected);
  auto* type_tool = patchy::test::ui::require_hotkey_action(window, QStringLiteral("tools.type"));
  CHECK(type_tool != nullptr && !menu->actions().contains(type_tool));
  CHECK(type_tool != nullptr && type_tool->shortcut() == QKeySequence(Qt::Key_T));
  const std::vector<std::pair<const char*, int>> submenus{{"typeAlignmentMenu", 7}, {"typeDirectionMenu", 3},
                                                          {"typeOrientationMenu", 2}, {"typeAntiAliasMenu", 7},
                                                          {"typeOpenTypeMenu", 8}};
  for (const auto& [name, count] : submenus) {
    auto* submenu = window.findChild<QMenu*>(QLatin1String(name));
    CHECK(submenu != nullptr);
    if (submenu == nullptr) {
      continue;
    }
    CHECK(submenu->actions().size() == count);
    CHECK(submenu->menuAction()->menuRole() == QAction::NoRole);
    for (const auto* action : submenu->actions()) {
      CHECK(action->isCheckable());
      CHECK(action->menuRole() == QAction::NoRole);
      CHECK(!action->objectName().isEmpty());
    }
  }
  for (const auto* action : menu->actions()) {
    CHECK(action->isSeparator() || action->menuRole() == QAction::NoRole);
  }

  // A selected text layer: everything on, and the checks read the layer.
  select_only(window, id);
  open_type_menu(window);
  for (const auto& submenu : submenus) {
    CHECK(submenu_enabled(window, submenu.first));
  }
  CHECK(action_checked(window, "typeAlignLeftAction"));
  CHECK(action_checked(window, "typeDirectionAutoAction"));
  CHECK(action_checked(window, "typeOrientationHorizontalAction"));
  CHECK(action_checked(window, "typeOpenTypeLigaturesAction"));  // on in new text, as in Photoshop
  CHECK(!action_checked(window, "typeOpenTypeSwashAction"));
  const auto anti_alias = metadata_value(window, id, patchy::kLayerMetadataTextAntiAlias);
  if (auto* anti_alias_menu = window.findChild<QMenu*>(QStringLiteral("typeAntiAliasMenu"));
      anti_alias_menu != nullptr && !anti_alias.isEmpty()) {
    for (const auto* action : anti_alias_menu->actions()) {
      CHECK(action->isChecked() == (action->data().toInt() == anti_alias.toInt()));
    }
  }

  // Only the pixel background selected, with the Move tool: nothing to act on.
  std::optional<patchy::LayerId> pixel_layer;
  for (const auto& layer : MainWindowTestAccess::document(window).layers()) {
    if (!patchy::layer_is_text(layer)) {
      pixel_layer = layer.id();
    }
  }
  CHECK(pixel_layer.has_value());
  if (!pixel_layer.has_value()) {
    return;
  }
  select_only(window, *pixel_layer);
  if (auto* move = patchy::test::ui::require_hotkey_action(window, QStringLiteral("tools.move")); move != nullptr) {
    move->trigger();
  }
  open_type_menu(window);
  for (const auto& submenu : submenus) {
    CHECK(!submenu_enabled(window, submenu.first));
  }
  // The entries themselves stay on, so a key bound to one still fires.
  CHECK(patchy::test::ui::require_action(window, "typeAlignCenterAction")->isEnabled());
  // The Type tool: orientation and smoothing seed the next new layer.
  if (type_tool != nullptr) {
    type_tool->trigger();
  }
  open_type_menu(window);
  CHECK(submenu_enabled(window, "typeOrientationMenu"));
  CHECK(submenu_enabled(window, "typeAntiAliasMenu"));
  CHECK(!submenu_enabled(window, "typeAlignmentMenu"));
  CHECK(!submenu_enabled(window, "typeDirectionMenu"));
  CHECK(!submenu_enabled(window, "typeOpenTypeMenu"));
}

// Each submenu applies to the selected text layer through its control's path: one undo step per
// pick, and the menu's checks follow.
void ui_type_menu_applies_to_selected_text_layer() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  const auto id = add_point_text(window);
  select_only(window, id);
  const auto trigger = [&window](const char* object_name) {
    auto* action = patchy::test::ui::require_action(window, object_name);
    const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
    if (action != nullptr) {
      action->trigger();
    }
    QApplication::processEvents();
    const auto added = MainWindowTestAccess::active_session_undo_depth(window) - depth;
    if (added != 1U) {
      std::fprintf(stderr, "%s added %zu undo steps\n", object_name, added);
    }
    CHECK(added == 1U);
  };

  trigger("typeAlignCenterAction");
  CHECK(first_paragraph_field(window, id, 2) == QStringLiteral("center"));
  trigger("typeAlignJustifyAction");
  CHECK(first_paragraph_field(window, id, 2) == QStringLiteral("justify"));
  open_type_menu(window);
  CHECK(action_checked(window, "typeAlignJustifyAction"));
  CHECK(!action_checked(window, "typeAlignCenterAction"));
  // Photoshop's other justify modes (#672): each its own name, checked on its own.
  for (const auto& [action, name] : {std::pair{"typeAlignJustifyCenterAction", "justifyCenter"},
                                     std::pair{"typeAlignJustifyRightAction", "justifyRight"},
                                     std::pair{"typeAlignJustifyAllAction", "justifyAll"}}) {
    trigger(action);
    CHECK(first_paragraph_field(window, id, 2) == QLatin1String(name));
    open_type_menu(window);
    CHECK(action_checked(window, action));
    CHECK(!action_checked(window, "typeAlignJustifyAction"));
  }

  trigger("typeDirectionRightToLeftAction");
  CHECK(first_paragraph_field(window, id, 9) == QStringLiteral("rtl"));

  // New text takes the persisted Smoothing choice, which an earlier test may have left at Crisp:
  // pick a mode the layer does not have, so the choice is a real change (one undo step).
  const bool starts_crisp =
      metadata_value(window, id, patchy::kLayerMetadataTextAntiAlias).toInt() == patchy::kTextAntiAliasCrisp;
  const char* anti_alias_action = starts_crisp ? "typeAntiAliasStrongAction" : "typeAntiAliasCrispAction";
  const int anti_alias = starts_crisp ? patchy::kTextAntiAliasStrong : patchy::kTextAntiAliasCrisp;
  trigger(anti_alias_action);
  CHECK(metadata_value(window, id, patchy::kLayerMetadataTextAntiAlias).toInt() == anti_alias);
  open_type_menu(window);
  CHECK(action_checked(window, anti_alias_action));

  // A toggle flips what the text has, whatever check the action showed last.
  trigger("typeOpenTypeSwashAction");
  open_type_menu(window);
  CHECK(action_checked(window, "typeOpenTypeSwashAction"));
  trigger("typeOpenTypeSwashAction");
  open_type_menu(window);
  CHECK(!action_checked(window, "typeOpenTypeSwashAction"));

  trigger("typeOrientationVerticalAction");
  CHECK(metadata_value(window, id, patchy::kLayerMetadataTextOrientation) == QStringLiteral("vertical"));
  open_type_menu(window);
  CHECK(action_checked(window, "typeOrientationVerticalAction"));

  // One undo puts the orientation back alone.
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(metadata_value(window, id, patchy::kLayerMetadataTextOrientation) != QStringLiteral("vertical"));
  CHECK(metadata_value(window, id, patchy::kLayerMetadataTextAntiAlias).toInt() == anti_alias);

  // Rasterize Type Layer: the text layer becomes pixels, one step.
  trigger("typeRasterizeAction");
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr && !patchy::layer_is_text(*layer));
}

// Review capture for the desk card: the Type menu with its OpenType submenu beside it.
void ui_type_menu_review_capture() {
  MainWindow window;
  patchy::test::ui::show_window_empty(window);
  const auto id = add_point_text(window);
  select_only(window, id);
  auto* menu = type_menu(window);
  auto* opentype = window.findChild<QMenu*>(QStringLiteral("typeOpenTypeMenu"));
  if (menu == nullptr || opentype == nullptr) {
    return;
  }
  menu->popup(window.mapToGlobal(QPoint(200, 40)));
  QApplication::processEvents();
  const auto menu_image = patchy::test::ui::capture_widget_artifact("ui_type_menu", *menu);
  const auto row = menu->actionGeometry(opentype->menuAction());
  opentype->popup(menu->mapToGlobal(QPoint(menu->width() - 4, row.top())));
  QApplication::processEvents();
  const auto opentype_image = patchy::test::ui::capture_widget_artifact("ui_type_menu_opentype", *opentype);
  opentype->close();
  menu->close();
  QApplication::processEvents();
  if (menu_image.isNull() || opentype_image.isNull()) {
    return;
  }
  // Side by side in device pixels, the submenu level with its row.
  const auto ratio = menu_image.devicePixelRatio();
  const int offset_x = menu_image.width() - static_cast<int>(4 * ratio);
  const int offset_y = static_cast<int>(row.top() * ratio);
  QImage sheet(std::max(menu_image.width(), offset_x + opentype_image.width()),
               std::max(menu_image.height(), offset_y + opentype_image.height()), QImage::Format_ARGB32_Premultiplied);
  sheet.fill(Qt::transparent);
  {
    // At 1:1, so a high-DPI grab is not drawn at half size.
    auto left = menu_image;
    auto right = opentype_image;
    left.setDevicePixelRatio(1.0);
    right.setDevicePixelRatio(1.0);
    QPainter painter(&sheet);
    painter.drawImage(QPoint(0, 0), left);
    painter.drawImage(QPoint(offset_x, offset_y), right);
  }
  patchy::test::ui::save_image_artifact("ui_type_menu_with_opentype", sheet);
}

// The outline grouping fills like nonzero winding: nested contours share a group (a hole), a
// solid contour inside another is dropped, crossing contours become separate united groups.
void ui_text_outline_groups_follow_nonzero_fill() {
  // A donut: outer clockwise, inner counterclockwise -> one group, two subpaths.
  QPainterPath donut;
  donut.addRect(QRectF(0, 0, 100, 100));
  QPainterPath hole;
  hole.addRect(QRectF(30, 30, 40, 40));
  donut.addPath(hole.toReversed());
  patchy::VectorPath path;
  patchy::ui::append_outline_subpaths(donut, path, nullptr);
  CHECK(path.subpaths.size() == 2U);
  CHECK(path.subpaths.size() == 2U && path.subpaths[0].shape_group == path.subpaths[1].shape_group);

  // Same direction inside: nonzero fills it solid, so the inner contour goes.
  QPainterPath doubled;
  doubled.addRect(QRectF(0, 0, 100, 100));
  doubled.addRect(QRectF(30, 30, 40, 40));
  patchy::VectorPath solid;
  patchy::ui::append_outline_subpaths(doubled, solid, nullptr);
  CHECK(solid.subpaths.size() == 1U);

  // Two crossing strokes (a variable font's overlapping contours): two united groups.
  QPainterPath cross;
  cross.addRect(QRectF(0, 40, 100, 20));
  cross.addRect(QRectF(40, 0, 20, 100));
  patchy::VectorPath crossing;
  std::vector<std::int32_t> groups;
  patchy::ui::append_outline_subpaths(cross, crossing, &groups);
  CHECK(crossing.subpaths.size() == 2U);
  CHECK(groups.size() == 2U);
  for (const auto& subpath : crossing.subpaths) {
    CHECK(subpath.op == patchy::PathCombineOp::Add && subpath.closed);
  }

  // A curved outline keeps its curves, with smooth knots.
  QPainterPath circle;
  circle.addEllipse(QRectF(0, 0, 50, 50));
  patchy::VectorPath round;
  patchy::ui::append_outline_subpaths(circle, round, nullptr);
  CHECK(round.subpaths.size() == 1U);
  if (round.subpaths.size() == 1U) {
    CHECK(round.subpaths[0].anchors.size() == 4U);
    for (const auto& anchor : round.subpaths[0].anchors) {
      CHECK(anchor.smooth);
    }
  }

  // The recording device keeps a filled text item as outlines in device coordinates.
  patchy::ui::OutlineRecordingDevice device;
  {
    QPainter painter(&device);
    painter.translate(10, 20);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(40);
    painter.setFont(font);
    painter.setPen(QColor(10, 20, 30));
    painter.drawText(QPointF(0, 40), QStringLiteral("Ag"));
  }
  CHECK(!device.paints().empty());
  if (!device.paints().empty()) {
    const auto bounds = device.paints().front().path.boundingRect();
    CHECK(bounds.left() > 9.0 && bounds.top() > 20.0 && bounds.width() > 30.0);
    CHECK(device.paints().front().color == QColor(10, 20, 30));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> text_outline_tests() {
  return {
      {"ui_text_convert_to_shape_matches_text_and_undoes", ui_text_convert_to_shape_matches_text_and_undoes},
      {"ui_text_create_work_path_keeps_text", ui_text_create_work_path_keeps_text},
      {"ui_text_convert_to_shape_handles_area_vertical_and_warp",
       ui_text_convert_to_shape_handles_area_vertical_and_warp},
      {"ui_text_convert_to_shape_matches_small_point_text", ui_text_convert_to_shape_matches_small_point_text},
      {"ui_text_convert_to_shape_keeps_colors_and_round_trips_psd",
       ui_text_convert_to_shape_keeps_colors_and_round_trips_psd},
      {"ui_script_text_convert_to_shape_and_work_path", ui_script_text_convert_to_shape_and_work_path},
      {"ui_text_outline_commands_in_menus", ui_text_outline_commands_in_menus},
      {"ui_text_outline_groups_follow_nonzero_fill", ui_text_outline_groups_follow_nonzero_fill},
      {"ui_text_outline_review_captures", ui_text_outline_review_captures},
      {"ui_type_menu_rows_and_enabled_states", ui_type_menu_rows_and_enabled_states},
      {"ui_type_menu_applies_to_selected_text_layer", ui_type_menu_applies_to_selected_text_layer},
      {"ui_type_menu_review_capture", ui_type_menu_review_capture},
  };
}
