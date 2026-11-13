// Text along a path (docs/path-text.md): glyph placement against Photoshop 2026 captures, the
// scripting API, the Type tool's targeting of paths and shape edges, and the on-canvas marks
// that move the range and flip the text.
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_document.hpp"
#include "ui/text_path_layout.hpp"
#include "ui/text_render.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_area.hpp"
#include "core/text_path.hpp"
#include "psd/psd_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QTextCursor>
#include <QTextEdit>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

PathAnchor knot(double x, double y, double in_x, double in_y, double out_x, double out_y) {
  return PathAnchor{x, y, in_x, in_y, out_x, out_y, false};
}

VectorPath contour(std::vector<PathAnchor> anchors, bool closed) {
  VectorPath path;
  auto& sub = path.subpaths.emplace_back();
  sub.anchors = std::move(anchors);
  sub.closed = closed;
  sub.op = PathCombineOp::Xor;
  return path;
}

// The capture matrix's arc, in text-local coordinates (the captures put the origin at 80, 60).
VectorPath arc_path() {
  return contour({knot(0, 300, 0, 300, 120, 0), knot(500, 300, 380, 0, 500, 300)}, false);
}

QJsonValue run(MainWindow& window, const QString& code) {
  auto& host = window.script_engine_host();
  ScriptEngineHost::RunOptions options;
  options.name = "path-text-test";
  options.unattended = true;
  const bool started = host.run_source(
      QStringLiteral("function check(v,m){if(!v)throw Error(m||'check failed');}"
                     "function reject(f){var bad=false;try{f();}catch(e){bad=true;}check(bad,'expected refusal');}\n") +
          code,
      options);
  if (!started) {
    for (const auto& message : host.message_backlog()) std::fprintf(stderr, "%s\n", message.toUtf8().constData());
  }
  CHECK(started);
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 30000) QApplication::processEvents(QEventLoop::AllEvents, 10);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& message : host.message_backlog()) std::fprintf(stderr, "%s\n", message.toUtf8().constData());
  }
  CHECK(!host.last_run_had_error());
  return host.last_result();
}

// Ink (0 white to 255 black) of an image on white.
int ink_at(const QImage& image, int x, int y) {
  return 255 - qGray(image.pixel(x, y));
}

struct Blob {
  double x{0.0};
  double y{0.0};
  double weight{0.0};
};

// Ink-weighted centroids of the connected marks of an image, sorted left to right then down.
std::vector<Blob> ink_blobs(const QImage& image) {
  constexpr int kThreshold = 40;
  std::vector<int> labels(static_cast<std::size_t>(image.width()) * image.height(), 0);
  std::vector<Blob> blobs;
  std::vector<QPoint> stack;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto index = static_cast<std::size_t>(y) * image.width() + x;
      if (labels[index] != 0 || ink_at(image, x, y) <= kThreshold) continue;
      Blob blob;
      double sum_x = 0.0;
      double sum_y = 0.0;
      stack.push_back(QPoint(x, y));
      labels[index] = 1;
      while (!stack.empty()) {
        const auto point = stack.back();
        stack.pop_back();
        const double weight = ink_at(image, point.x(), point.y());
        blob.weight += weight;
        sum_x += weight * (point.x() + 0.5);
        sum_y += weight * (point.y() + 0.5);
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = point.x() + dx;
            const int ny = point.y() + dy;
            if (nx < 0 || ny < 0 || nx >= image.width() || ny >= image.height()) continue;
            const auto next = static_cast<std::size_t>(ny) * image.width() + nx;
            if (labels[next] != 0 || ink_at(image, nx, ny) <= kThreshold) continue;
            labels[next] = 1;
            stack.push_back(QPoint(nx, ny));
          }
        }
      }
      if (blob.weight < 255.0 * 20.0) continue;  // specks
      blob.x = sum_x / blob.weight;
      blob.y = sum_y / blob.weight;
      blobs.push_back(blob);
    }
  }
  std::sort(blobs.begin(), blobs.end(), [](const Blob& a, const Blob& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  return blobs;
}

QRect ink_bounds(const QImage& image) {
  QRect bounds;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (ink_at(image, x, y) > 96) bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
    }
  }
  return bounds;
}

// How much of each image's ink lies within a pixel of the other's: 1 for the same marks.
double ink_agreement(const QImage& a, const QImage& b) {
  const auto near_ink = [](const QImage& image, int x, int y) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < image.width() && ny < image.height() && ink_at(image, nx, ny) > 64) return true;
      }
    }
    return false;
  };
  double total = 0.0;
  double matched = 0.0;
  for (int y = 0; y < a.height() && y < b.height(); ++y) {
    for (int x = 0; x < a.width() && x < b.width(); ++x) {
      if (ink_at(a, x, y) > 128) {
        total += 1.0;
        if (near_ink(b, x, y)) matched += 1.0;
      }
      if (ink_at(b, x, y) > 128) {
        total += 1.0;
        if (near_ink(a, x, y)) matched += 1.0;
      }
    }
  }
  return total > 0.0 ? matched / total : 0.0;
}

QImage photoshop_render(std::string_view name) {
  const auto path = test::committed_psd_fixture_path(std::string(name) + ".png");
  QImage image(to_qstring(path));
  return image.convertToFormat(QImage::Format_RGB32);
}

// A text layer's pixels over white at the capture's canvas size.
QImage layer_on_white(const Layer& layer, QSize size) {
  QImage canvas(size, QImage::Format_RGB32);
  canvas.fill(Qt::white);
  QPainter painter(&canvas);
  painter.drawImage(QPoint(layer.bounds().x, layer.bounds().y), qimage_from_pixel_buffer(layer.pixels()));
  return canvas;
}

QImage render_on_white(const TextToolSettings& settings, const QString& paragraph_runs) {
  const auto rendered = render_text_pixels_with_local_rect(settings, QColor(Qt::black), 660, paragraph_runs, QString(),
                                                           std::nullopt, 1.0, QTransform::fromTranslate(80.0, 60.0));
  QImage canvas(660, 520, QImage::Format_RGB32);
  canvas.fill(Qt::white);
  QPainter painter(&canvas);
  painter.drawImage(rendered.local_rect.topLeft(), qimage_from_pixel_buffer(rendered.pixels));
  return canvas;
}

bool arial_missing() {
  if (QFontDatabase::hasFamily(QStringLiteral("Arial"))) return false;
  std::printf("  [SKIP] Arial is not installed\n");
  return true;
}

// The straight line bent onto the contour: points, glyph placements, strips and the way back.
void ui_text_path_line_plan_maps_onto_the_contour() {
  TextToolSettings settings;
  settings.text = QStringLiteral("IIIII IIIII IIIII IIIII IIIII");
  settings.family = QStringLiteral("Arial");
  settings.size = 48;
  settings.boxed = true;
  settings.path = arc_path();
  settings.path_range = {0.0, 0.5};
  CHECK(text_settings_on_path(settings));
  const auto built = build_text_render_document(settings, QColor(Qt::black), 660, QString(), QString(), 1.0, 1.0, 1.0);
  const TextPathCurve curve(arc_path());
  const auto span = text_path_span(curve, settings.path_range);
  CHECK(std::abs(text_path_layout_width(arc_path(), settings.path_range) - span.length) < 1e-9);
  CHECK(std::abs(built.document->textWidth() - span.length) < 1e-6);
  const auto plan = text_path_line_plan(*built.document, arc_path(), settings.path_range);
  CHECK(plan.valid);
  // Photoshop hides whole words that do not fit: four of the five groups show.
  CHECK(plan.hidden_lines);
  CHECK(plan.line.line.textLength() >= 23 && plan.line.line.textLength() <= 24);
  CHECK(std::abs(plan.start_distance) < 1e-9);
  CHECK(std::abs(plan.span_length - span.length) < 1e-9);

  // A straight point: x along the range, y from the baseline (down positive).
  const auto on_line = plan.map(QPointF(100.0, 0.0));
  const auto sample = curve.sample(100.0);
  CHECK(std::abs(on_line.x() - sample.x) < 1e-6 && std::abs(on_line.y() - sample.y) < 1e-6);
  const auto above = plan.map(QPointF(100.0, -20.0));
  CHECK(std::abs(std::hypot(above.x() - sample.x, above.y() - sample.y) - 20.0) < 1e-6);
  CHECK(curve.nearest(above.x(), above.y()).left);
  // A glyph is placed rigidly about the middle of its advance.
  const auto placement = plan.glyph_transform(100.0);
  const auto centre = placement.map(QPointF(100.0, 0.0));
  CHECK(std::abs(centre.x() - sample.x) < 1e-6 && std::abs(centre.y() - sample.y) < 1e-6);
  const auto ahead = placement.map(QPointF(110.0, 0.0));
  CHECK(std::abs(ahead.x() - (sample.x + 10.0 * sample.tangent_x)) < 1e-6);
  CHECK(std::abs(ahead.y() - (sample.y + 10.0 * sample.tangent_y)) < 1e-6);
  // And back: a point beside the line resolves to its place along it.
  const auto back = plan.unmap(above);
  CHECK(std::abs(back.x() - 100.0) < 0.5 && std::abs(back.y() + 20.0) < 0.1);
  CHECK(plan.unmap(plan.map(QPointF(-50.0, 0.0))).x() == 0.0);  // clamped to the range
  // A strip follows the line.
  const auto strip = plan.strip(QRectF(50.0, -30.0, 100.0, 40.0));
  CHECK(strip.size() >= 8);
  CHECK(std::abs(strip.front().x() - plan.map(QPointF(50.0, -30.0)).x()) < 1e-6);

  // Alignment works inside the range: a short centred line sits around the range's middle.
  settings.text = QStringLiteral("III");
  const auto centred = build_text_render_document(settings, QColor(Qt::black), 660, QStringLiteral("v1\n0\t3\tcenter"),
                                                  QString(), 1.0, 1.0, 1.0);
  const auto centred_plan = text_path_line_plan(*centred.document, arc_path(), settings.path_range);
  CHECK(centred_plan.valid && !centred_plan.hidden_lines);
  const auto natural = centred_plan.line.line.naturalTextRect();
  CHECK(std::abs(natural.center().x() - span.length / 2.0) < 1.0);
}

// Photoshop 2026 captures of capital I rows (each a separate bar): every glyph's centroid
// lands within a pixel of Photoshop's, for a left-aligned and a right-aligned range.
void ui_text_path_glyphs_match_photoshop_placement_if_available() {
  if (arial_missing()) return;
  ensure_artifact_dir();
  struct Case {
    const char* fixture;
    const char* text;
    const char* align;
    std::size_t bars;
  };
  const Case cases[] = {
      {"photoshop-path-cal-arc-range", "IIIIIIIIIIIIIIIIIIII", "left", 20},
      {"photoshop-path-cal-right-range", "IIIII", "right", 5},
  };
  for (const auto& item : cases) {
    TextToolSettings settings;
    settings.text = QString::fromLatin1(item.text);
    settings.family = QStringLiteral("Arial");
    settings.size = 48;
    settings.boxed = true;
    settings.photoshop_layout = true;
    settings.path = arc_path();
    settings.path_range = {0.3, 0.9};
    const auto runs = QStringLiteral("v1\n0\t%1\t%2").arg(settings.text.size()).arg(QString::fromLatin1(item.align));
    const auto ours = render_on_white(settings, runs);
    const auto theirs = photoshop_render(item.fixture);
    CHECK(!theirs.isNull());
    if (theirs.isNull()) continue;
    save_image_artifact(std::string("ui_text_path_") + item.fixture + "_patchy", ours);
    const auto our_blobs = ink_blobs(ours);
    const auto their_blobs = ink_blobs(theirs);
    CHECK(their_blobs.size() == item.bars);
    CHECK(our_blobs.size() == their_blobs.size());
    if (our_blobs.size() != their_blobs.size()) continue;
    double worst = 0.0;
    for (std::size_t i = 0; i < our_blobs.size(); ++i) {
      worst = std::max(worst, std::hypot(our_blobs[i].x - their_blobs[i].x, our_blobs[i].y - their_blobs[i].y));
    }
    std::printf("  %s: %zu bars, worst centroid distance %.2f px\n", item.fixture, our_blobs.size(), worst);
    CHECK(worst <= 1.0);
  }
}

// Photoshop's own path-text files, opened and drawn again by Patchy from the layer's text
// (the saved raster replaced): the same ink in the same place.
void ui_text_path_photoshop_files_redraw_where_photoshop_drew_them_if_available() {
  if (arial_missing()) return;
  ensure_artifact_dir();
  struct Case {
    const char* fixture;
    double agreement;  // least share of ink within a pixel of the other render's
    int bounds_slack;  // most each side of the ink bounds may differ, in pixels
  };
  const Case cases[] = {
      {"photoshop-path-open-curve", 0.90, 2},   {"photoshop-path-wave-range", 0.90, 2},
      {"photoshop-path-ellipse-range", 0.90, 2}, {"photoshop-path-center", 0.90, 2},
      {"photoshop-path-mixed", 0.90, 2},        {"photoshop-path-baseline-shift", 0.90, 2},
      {"photoshop-path-rotated", 0.90, 2},      {"photoshop-path-overflow", 0.90, 2},
      // Made in Photoshop's window and saved after its flip gesture, which Photoshop 2026 neither
      // draws nor stores (docs/path-text.md, "Flip"): plain path text, 50 px, behind a frame affine.
      {"photoshop-path-flip-drag", 0.90, 2},
  };
  for (const auto& item : cases) {
    auto document = psd::DocumentIo::read_file(test::committed_psd_fixture_path(std::string(item.fixture) + ".psd"));
    auto text = std::find_if(document.layers().begin(), document.layers().end(), [](const Layer& layer) {
      return layer_is_text(layer);
    });
    CHECK(text != document.layers().end());
    if (text == document.layers().end()) continue;
    CHECK(text_path_for_layer(std::as_const(*text)).has_value());
    CHECK(!text_geometry_is_protected(std::as_const(*text)));
    const auto theirs = photoshop_render(item.fixture);
    CHECK(!theirs.isNull());
    if (theirs.isNull()) continue;
    // Untouched, the layer shows Photoshop's saved raster.
    const auto saved = layer_on_white(std::as_const(*text), theirs.size());
    CHECK(ink_agreement(saved, theirs) > 0.99);
    CHECK(rerender_text_layer_through_stored_transform(*text));
    const auto ours = layer_on_white(std::as_const(*text), theirs.size());
    CHECK(ours != saved);  // Patchy's own glyphs now, not Photoshop's raster
    save_image_artifact(std::string("ui_text_path_") + item.fixture + "_patchy", ours);
    const auto agreement = ink_agreement(ours, theirs);
    const auto our_bounds = ink_bounds(ours);
    const auto their_bounds = ink_bounds(theirs);
    const int slack = std::max({std::abs(our_bounds.left() - their_bounds.left()),
                                std::abs(our_bounds.top() - their_bounds.top()),
                                std::abs(our_bounds.right() - their_bounds.right()),
                                std::abs(our_bounds.bottom() - their_bounds.bottom())});
    std::printf("  %s: ink agreement %.3f, ink bounds differ by at most %d px\n", item.fixture, agreement, slack);
    CHECK(agreement >= item.agreement);
    CHECK(slack <= item.bounds_slack);
  }
}

// Photoshop's own file through the app: the layer opens as path text, an edit keeps the contour
// and the range, the text still follows the arc, and the save is a path frame again.
void ui_text_path_photoshop_file_edits_in_place() {
  MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);  // Match --run-script's missing-font substitution on Linux.
  const auto fixture = to_qstring(test::committed_psd_fixture_path("photoshop-path-open-curve.psd"));
  const auto quoted = QString::fromUtf8(QJsonDocument(QJsonArray{fixture}).toJson(QJsonDocument::Compact));
  run(window, QStringLiteral(R"JS(
    var d=app.open(%1[0]);
    var t=d.layers.filter(function(l){return l.isText;})[0];
    var story=t.text,path=JSON.stringify(t.textPath),range=JSON.stringify(t.textPathRange);
    check(story==='One two three four five six seven eight nine ten.','story read');
    check(t.textPath.subpaths[0].anchors.length===2 && t.textPath.subpaths[0].closed===false,'contour read');
    check(Math.abs(t.textPath.subpaths[0].anchors[0].x-80)<1e-6 && Math.abs(t.textPath.subpaths[0].anchors[0].y-360)<1e-6,'in the document');
    check(t.textPathRange.start===0 && t.textPathRange.end===1,'range read');
    var before=t.bounds;
    t.text=story+' More.';
    check(t.text===story+' More.','edited');
    check(JSON.stringify(t.textPath)===path && JSON.stringify(t.textPathRange)===range,'the edit kept the path');
    // The longer line reaches further along the arc; it starts where it did.
    check(Math.abs(t.bounds.x-before.x)<=6 && Math.abs(t.bounds.y-before.y)<=6,'the text did not move');
    check(t.bounds.width>before.width+20,'the new words follow the arc');
    t.textPathRange={start:0.5,end:1};
    check(t.bounds.x>before.x+150,'the range moved the text');
    patchy.setResult(t.id);
  )JS").arg(quoted));
  const auto id = window.script_engine_host().last_result().toString().toULongLong();
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  const auto* layer = doc.find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) return;
  const auto reopened = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(doc));
  bool kept = false;
  for (const auto& candidate : reopened.layers()) {
    if (!layer_is_text(candidate)) continue;
    const auto frame = text_path_frame_for_layer(candidate);
    kept = frame.has_value() && frame->path == *text_path_for_layer(*layer) &&
           std::abs(frame->range.start - 0.5) < 1e-5 && frame->range.end == 1.0 &&
           !text_geometry_is_protected(candidate);
  }
  CHECK(kept);
}

void ui_text_path_script_creation_range_flip_and_undo() {
  MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);  // Match --run-script's missing-font substitution on Linux.
  run(window, R"JS(
    var d=app.newDocument(660,520);
    var p={subpaths:[{closed:false,anchors:[{x:80,y:360,outX:200,outY:60},{x:580,y:360,inX:460,inY:60}]}]};
    var t=d.addTextLayer('One two three four five six seven eight nine ten.',{font:'Arial',size:24,path:p});
    t.name='Path';
    check(t.textPath.subpaths.length===1 && t.textPath.subpaths[0].anchors.length===2,'path read back');
    check(t.textPath.subpaths[0].closed===false,'open contour');
    check(t.textPathRange.start===0 && t.textPathRange.end===1,'whole contour by default');
    check(t.textArea===null,'not area text');
    var original=JSON.stringify(t.textPath);
    p.subpaths[0].anchors[0].x+=30;
    check(JSON.stringify(t.textPath)===original,'the path is a snapshot');
    reject(function(){d.addTextLayer('bad',{path:p,box:{width:100,height:100}});});
    reject(function(){d.addTextLayer('bad',{path:p,area:{subpaths:[{anchors:[{x:0,y:0},{x:50,y:0},{x:50,y:50}]}]}});});
    reject(function(){d.addTextLayer('bad',{path:{subpaths:[]}});});
    reject(function(){d.addTextLayer('bad',{path:{subpaths:[p.subpaths[0],p.subpaths[0]]}});});
    reject(function(){d.addTextLayer('bad',{path:p,pathRange:{start:0.8,end:0.2}});});
    reject(function(){d.addTextLayer('bad',{path:p,pathRange:{start:0,end:3}});});
    var ranged=d.addTextLayer('Ranged',{font:'Arial',size:24,path:p,pathRange:{start:0.25,end:0.75}});
    check(Math.abs(ranged.textPathRange.start-0.25)<1e-9 && Math.abs(ranged.textPathRange.end-0.75)<1e-9,'range option');
    ranged.remove();
    d.activeLayer=t;
    patchy.setResult({id:t.id,path:t.textPath,text:t.text,width:t.width,height:t.height});
  )JS");
  const auto original = window.script_engine_host().last_result().toObject();
  const auto id = original["id"].toString().toULongLong();
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  const auto* layer = doc.find_layer(id);
  CHECK(layer != nullptr && text_path_for_layer(*layer).has_value());
  if (layer == nullptr) return;
  CHECK(!layer->pixels().empty());
  CHECK(layer->metadata().at(kLayerMetadataTextFlow) == "box");
  // The text follows the arc: its raster is far taller than a straight line of 24 px type.
  CHECK(layer->bounds().height > 120);
  const auto full_bounds = layer->bounds();

  // The range moves the text along the contour, in one undoable step.
  run(window, R"JS(
    var t=app.activeDocument.findLayer('Path');
    t.textPathRange={start:0.5,end:1};
    check(Math.abs(t.textPathRange.start-0.5)<1e-9,'range set');
    reject(function(){t.textPathRange={start:0.9,end:0.1};});
    reject(function(){t.textPathRange={start:-1,end:0.5};});
    patchy.setResult(t.x);
  )JS");
  CHECK(doc.find_layer(id)->bounds().x > full_bounds.x + 100);
  run(window, "check(app.activeDocument.undo());patchy.setResult(app.activeDocument.findLayer('Path').textPathRange);");
  CHECK(window.script_engine_host().last_result().toObject()["start"].toDouble() == 0.0);
  CHECK(doc.find_layer(id)->bounds() == full_bounds);
  run(window, "check(app.activeDocument.redo());check(app.activeDocument.undo());");

  // A flip walks the contour the other way: the first anchor becomes the last, the text sits
  // below the arc and reads from the other end.
  run(window, R"JS(
    var t=app.activeDocument.findLayer('Path'),before=t.textPath;
    t.flipTextPath();
    var after=t.textPath;
    check(after.subpaths[0].anchors[0].x===before.subpaths[0].anchors[1].x,'contour reversed');
    check(after.subpaths[0].anchors[0].outX===before.subpaths[0].anchors[1].inX,'handles swapped');
    check(t.textPathRange.start===0 && t.textPathRange.end===1,'whole range stays whole');
    patchy.setResult(t.y);
  )JS");
  CHECK(doc.find_layer(id)->bounds().y > full_bounds.y);
  run(window, "check(app.activeDocument.undo());");
  CHECK(doc.find_layer(id)->bounds() == full_bounds);

  // Editing the text keeps the path; a duplicate, a copy into another document and a resize
  // carry it; null turns the layer into a plain text box.
  run(window, R"JS(
    var source=app.activeDocument,t=source.findLayer('Path'),path=JSON.stringify(t.textPath);
    t.text+=' More.';
    check(JSON.stringify(t.textPath)===path,'an edit keeps the path');
    var duplicate=t.duplicate();check(JSON.stringify(duplicate.textPath)===path,'duplicate');
    duplicate.remove();
    var target=app.newDocument(660,520),copy=t.duplicate(target);
    check(copy.text===t.text && JSON.stringify(copy.textPath)===path,'cross-document copy');
    var before=copy.textPath.subpaths[0].anchors;
    target.resizeImage(330,260);
    var after=copy.textPath.subpaths[0].anchors;
    for(var i=0;i<before.length;i++) {
      check(Math.abs(after[i].x-before[i].x/2)<1.01 && Math.abs(after[i].y-before[i].y/2)<1.01,'document resize');
    }
    copy.textPath=null;
    check(copy.textPath===null && copy.textPathRange===null,'null clears the path');
    check(copy.textBox!==null,'and leaves box text');
    source.activate();source.activeLayer=t;
  )JS");
  CHECK(text_path_for_layer(*doc.find_layer(id)).has_value());

  // PSD and PSB round trips keep the contour, the range and the story.
  const auto saved = psd::DocumentIo::write_layered_rgb8(doc);
  const auto reopened = psd::DocumentIo::read(saved);
  const Layer* again = nullptr;
  for (const auto& candidate : reopened.layers()) {
    if (candidate.name() == "Path") again = &candidate;
  }
  CHECK(again != nullptr);
  if (again != nullptr) {
    CHECK(text_path_for_layer(*again) == text_path_for_layer(*doc.find_layer(id)));
    CHECK(text_path_range_for_layer(*again, *text_path_for_layer(*again)) ==
          text_path_range_for_layer(*doc.find_layer(id), *text_path_for_layer(*doc.find_layer(id))));
    CHECK(again->metadata().at(kLayerMetadataText) == doc.find_layer(id)->metadata().at(kLayerMetadataText));
    CHECK(!text_geometry_is_protected(*again));
  }
  const auto large = psd::DocumentIo::read(psd::DocumentIo::write_layered_rgb8(doc, {.large_document = true}));
  bool found_large = false;
  for (const auto& candidate : large.layers()) {
    if (candidate.name() == "Path") {
      found_large = text_path_for_layer(candidate) == text_path_for_layer(*doc.find_layer(id));
    }
  }
  CHECK(found_large);
  // The owned contour is the Direct Selection tool's path target, like an area boundary:
  // reshaping it moves the text, and undo puts it back.
  auto* canvas = require_canvas(window);
  const auto* target = canvas->text_area_edit_target_path();
  CHECK(target != nullptr);
  if (target == nullptr) return;
  const auto before_edit = *doc.find_layer(id);
  auto reshaped = *target;
  reshaped.subpaths.front().anchors.front().anchor_y += 40.0;
  canvas->replace_path_edit_target(reshaped, {});
  CHECK(text_path_for_layer(*doc.find_layer(id)) != text_path_for_layer(before_edit));
  CHECK(text_path_range_for_layer(*doc.find_layer(id), *text_path_for_layer(*doc.find_layer(id))) ==
        text_path_range_for_layer(before_edit, *text_path_for_layer(before_edit)));
  CHECK(doc.find_layer(id)->bounds() != before_edit.bounds());
  MainWindowTestAccess::undo(window);
  CHECK(text_path_for_layer(*doc.find_layer(id)) == text_path_for_layer(before_edit));

  // Free Transform turns the layer; the contour is text-local, so a later edit keeps it.
  CHECK(canvas->begin_free_transform());
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  if (state) CHECK(canvas->set_transform_controls_state(state->reference_position, 100.0, 100.0, 20.0));
  canvas->finish_free_transform();
  run(window, "var t=app.activeDocument.findLayer('Path'),before=JSON.stringify(t.textPath);t.text+=' Turned.';"
              "check(JSON.stringify(t.textPath)===before,'re-entry moved the path');");
  const auto turned = text_area_transform(*doc.find_layer(id));
  CHECK(std::abs(turned[1]) > 0.1);  // the rotation is in the text transform

  // A file for the Photoshop acceptance check (docs/path-text.md): an open curve, a ranged
  // two-segment wave, a closed ellipse whose range passes the seam, and a flipped arc.
  ensure_artifact_dir();
  run(window, R"JS(
    var d=app.newDocument(660,520),story='One two three four five six seven eight nine ten.';
    function knot(x,y,ix,iy,ox,oy){return {x:x+80,y:y+60,inX:ix+80,inY:iy+60,outX:ox+80,outY:oy+60};}
    var arc={subpaths:[{closed:false,anchors:[knot(0,300,0,300,120,0),knot(500,300,380,0,500,300)]}]};
    var wave={subpaths:[{closed:false,anchors:[knot(0,250,0,250,40,120),knot(150,150,110,150,260,150),knot(500,250,400,380,500,250)]}]};
    var ellipse={subpaths:[{closed:true,anchors:[knot(250,0,111.9288,0,388.0712,0),knot(500,200,500,89.543,500,310.457),
                                               knot(250,400,388.0712,400,111.9288,400),knot(0,200,0,310.457,0,89.543)]}]};
    d.addTextLayer(story,{font:'Arial',size:24,color:'#000000',path:arc}).name='Arc';
    d.addTextLayer(story,{font:'Arial',size:24,color:'#000000',path:wave,pathRange:{start:0.5,end:1.5}}).name='Wave';
    d.addTextLayer(story,{font:'Arial',size:24,color:'#000000',path:ellipse,pathRange:{start:3.5,end:5.5}}).name='Ellipse';
    var flipped=d.addTextLayer(story,{font:'Arial',size:24,color:'#000000',path:arc});
    flipped.name='Flipped';flipped.flipTextPath();
  )JS");
  const auto artifact = to_filesystem_path(QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/path-text-photoshop-check.psd")));
  psd::DocumentIo::write_layered_rgb8_file(std::as_const(MainWindowTestAccess::document(window)), artifact);
  const auto check = psd::DocumentIo::read_file(artifact);
  int path_layers = 0;
  for (const auto& candidate : check.layers()) {
    if (text_path_for_layer(candidate).has_value()) ++path_layers;
  }
  CHECK(path_layers == 4);
}

QTextEdit* session_editor(CanvasWidget& canvas) {
  return canvas.findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
}

QWidget* session_overlay(CanvasWidget& canvas) {
  return canvas.findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
}

// The session's overlay once its first preview has landed and its path marks are published. A
// preview refresh can replace the overlay widget, so callers look it up again after each wait.
QWidget* wait_for_path_overlay(CanvasWidget& canvas) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 10000) {
    if (auto* overlay = session_overlay(canvas);
        overlay != nullptr && overlay->isVisible() &&
        overlay->property("patchy.transformedTextPathHandleCenters").toList().size() == 3) {
      return overlay;
    }
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return session_overlay(canvas);
}

void click_canvas(CanvasWidget& canvas, QPoint document_point) {
  const auto click = canvas.widget_position_for_document_point(document_point);
  send_mouse(canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// The Type tool on a shape: its edge starts text along the outline, its inside fills the shape,
// and a click on the text (or the bare contour) opens the layer again.
void ui_text_path_type_tool_targets_shape_edges_and_reopens() {
  MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, R"JS(
    var d=app.newDocument(660,520);
    var s=d.addShape('Source',{type:'ellipse',x:130,y:90,width:400,height:320});
    patchy.setResult(s.id);
  )JS");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  const auto source_id = window.script_engine_host().last_result().toString().toULongLong();
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  const auto source_path = doc.find_layer(source_id)->vector_shape()->path;
  require_action_by_text(window, QStringLiteral("Type"))->trigger();

  // The top of the ellipse is on its edge; the centre is inside; far outside is neither.
  const QPoint edge(330, 90);
  const QPoint centre(330, 250);
  CHECK(canvas->text_path_at(QPointF(edge)).has_value());
  CHECK(canvas->text_path_at(QPointF(edge + QPoint(0, 4))).has_value());
  CHECK(!canvas->text_path_at(QPointF(centre)).has_value());
  CHECK(canvas->text_area_at(centre).has_value());
  CHECK(!canvas->text_path_at(QPointF(20, 20)).has_value());
  const auto target = canvas->text_path_at(QPointF(edge));
  if (!target) return;
  CHECK(target->path.subpaths.size() == 1 && target->path.subpaths.front().closed);
  // Once around from the click.
  CHECK(std::abs((target->range.end - target->range.start) - text_path_segment_count(target->path)) < 1e-9);

  click_canvas(*canvas, edge);
  auto* editor = session_editor(*canvas);
  CHECK(editor != nullptr);
  if (!editor) return;
  CHECK(!editor->property("patchy.documentTextPath").toString().isEmpty());
  CHECK(editor->property("patchy.documentTextArea").toString().isEmpty());
  auto* overlay = session_overlay(*canvas);
  CHECK(overlay != nullptr && overlay->isVisible());
  if (!overlay) return;
  // No box handles; three marks on the contour.
  for (const auto& corner : overlay->property("patchy.transformedTextResizeHandleCenters").toList()) {
    CHECK(!corner.isValid());
  }
  CHECK(overlay->property("patchy.transformedTextPathHandleCenters").toList().size() == 3);
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Around the edge"));
  process_events_for(150);
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  save_widget_artifact("ui_text_path_editing", *canvas);
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(session_editor(*canvas) == nullptr);

  const auto id = doc.active_layer_id();
  CHECK(id.has_value());
  if (!id) return;
  const auto* layer = doc.find_layer(*id);
  CHECK(layer != nullptr && layer_is_text(*layer) && text_path_for_layer(*layer).has_value());
  if (!layer) return;
  CHECK(QString::fromStdString(layer->metadata().at(kLayerMetadataText)) == QStringLiteral("Around the edge"));
  CHECK(!text_area_for_layer(*layer).has_value());
  // The text stands on the outside of the ellipse's top, so its raster starts above the edge.
  CHECK(layer->bounds().y < 90);
  // The source shape is untouched: the text owns a copy.
  CHECK(doc.find_layer(source_id)->vector_shape()->path == source_path);
  const auto committed = *layer;

  // Clicking the text opens it again; Escape leaves it as it was.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint on_text(layer->bounds().x + layer->bounds().width / 2, layer->bounds().y + layer->bounds().height / 2);
  click_canvas(*canvas, on_text);
  editor = session_editor(*canvas);
  CHECK(editor != nullptr);
  if (!editor) return;
  CHECK(editor->property("patchy.editingLayerId").toULongLong() == *id);
  CHECK(editor->toPlainText() == QStringLiteral("Around the edge"));
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Cancelled edit"));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(session_editor(*canvas) == nullptr);
  CHECK(doc.find_layer(*id)->metadata() == committed.metadata());

  // A long story overflows the range: the hidden text stays in the story and the end mark
  // carries the overflow plus.
  click_canvas(*canvas, on_text);
  editor = session_editor(*canvas);
  CHECK(editor != nullptr);
  if (!editor) return;
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Round and round the outline goes. ").repeated(12));
  process_events_for(150);
  CHECK(editor->property("patchy.textHiddenLines").toBool());
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(QString::fromStdString(doc.find_layer(*id)->metadata().at(kLayerMetadataText)).size() > 300);
  MainWindowTestAccess::undo(window);
  CHECK(QString::fromStdString(doc.find_layer(*id)->metadata().at(kLayerMetadataText)) ==
        QStringLiteral("Around the edge"));
}

// The Type tool on an open pen path: the click point starts the range, which runs to the end
// of the path, and the path itself stays as it was.
void ui_text_path_type_tool_types_along_an_open_work_path() {
  MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, R"JS(
    var d=app.newDocument(660,520);
    d.setWorkPath({subpaths:[{closed:false,anchors:[{x:80,y:360,outX:200,outY:60},{x:580,y:360,inX:460,inY:60}]}]});
    patchy.setResult(JSON.stringify(d.workPath.getPath()));
  )JS");
  const auto work_before = window.script_engine_host().last_result().toString();
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto source = contour({knot(80, 360, 80, 360, 200, 60), knot(580, 360, 460, 60, 580, 360)}, false);
  const TextPathCurve curve(source);
  const auto quarter = curve.sample(curve.distance_at_parameter(0.25));
  const QPoint on_path(static_cast<int>(std::lround(quarter.x)), static_cast<int>(std::lround(quarter.y)));
  const auto target = canvas->text_path_at(QPointF(on_path));
  CHECK(target.has_value());
  if (!target) return;
  CHECK(!target->path.subpaths.front().closed);
  CHECK(std::abs(target->range.start - 0.25) < 0.01);
  CHECK(target->range.end == 1.0);
  CHECK(!canvas->text_path_at(QPointF(on_path + QPoint(0, 30))).has_value());
  // Over the line the cursor is the I-beam standing on a wavy stroke (hot spot 12, 18).
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(on_path), Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().hotSpot() == QPoint(12, 18));
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(on_path + QPoint(0, 60)), Qt::NoButton,
             Qt::NoButton);
  CHECK(canvas->cursor().hotSpot() != QPoint(12, 18));
  // The last pixels of the path leave no room for text: the whole path is taken.
  const auto at_end = canvas->text_path_at(QPointF(579, 359));
  CHECK(at_end.has_value() && at_end->range.start == 0.0 && at_end->range.end == 1.0);

  click_canvas(*canvas, on_path);
  auto* editor = session_editor(*canvas);
  CHECK(editor != nullptr);
  if (!editor) return;
  CHECK(!editor->property("patchy.documentTextPath").toString().isEmpty());
  editor->selectAll();
  editor->insertPlainText(QStringLiteral("Along the path"));
  process_events_for(150);
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  const auto id = doc.active_layer_id();
  CHECK(id.has_value());
  if (!id) return;
  const auto* layer = doc.find_layer(*id);
  CHECK(layer != nullptr && layer_is_text(*layer));
  if (!layer) return;
  const auto frame = text_path_frame_for_layer(*layer);
  CHECK(frame.has_value());
  if (!frame) return;
  CHECK(std::abs(frame->range.start - 0.25) < 0.01 && frame->range.end == 1.0);
  // The layer's contour is the work path, in the document.
  const auto in_document = text_path_in_document(*layer);
  CHECK(in_document.has_value());
  if (in_document) {
    const auto& anchors = in_document->subpaths.front().anchors;
    CHECK(anchors.size() == 2);
    CHECK(std::abs(anchors[0].anchor_x - 80.0) < 1e-6 && std::abs(anchors[0].anchor_y - 360.0) < 1e-6);
    CHECK(std::abs(anchors[1].in_x - 460.0) < 1e-6 && std::abs(anchors[1].in_y - 60.0) < 1e-6);
  }
  // The text starts at the click: its ink begins within the type's height of that point.
  CHECK(std::abs(layer->bounds().x - on_path.x()) < 30);
  CHECK(layer->bounds().x + layer->bounds().width > on_path.x() + 100);
  run(window, "patchy.setResult(JSON.stringify(app.activeDocument.workPath.getPath()));");
  CHECK(window.script_engine_host().last_result().toString() == work_before);
}

QPoint overlay_point(const QWidget& overlay, const QVariant& canvas_point) {
  return canvas_point.toPointF().toPoint() - overlay.pos();
}

// The marks on the contour: the end mark shortens the range, the middle mark slides it, and
// dragging the middle mark across the line flips the text to the other side. Only the marks
// move during a drag; the text is laid out on release.
void ui_text_path_handles_move_the_range_and_flip() {
  MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, R"JS(
    var d=app.newDocument(660,520);
    var p={subpaths:[{closed:false,anchors:[{x:80,y:260},{x:580,y:260}]}]};
    var t=d.addTextLayer('Along',{font:'Arial',size:24,path:p});
    t.name='Path';
    patchy.setResult(t.id);
  )JS");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  const auto id = window.script_engine_host().last_result().toString().toULongLong();
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  const auto committed = *doc.find_layer(id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto bounds = doc.find_layer(id)->bounds();
  click_canvas(*canvas, QPoint(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2));
  auto* editor = session_editor(*canvas);
  CHECK(editor != nullptr);
  if (!editor) return;
  auto* overlay = wait_for_path_overlay(*canvas);
  CHECK(overlay != nullptr && overlay->isVisible());
  if (!overlay) return;
  auto handles = overlay->property("patchy.transformedTextPathHandleCenters").toList();
  CHECK(handles.size() == 3);
  if (handles.size() != 3) return;
  // Start at the line's left end, End at its right end, Middle halfway.
  const auto left_end = canvas->widget_position_for_document_point(QPoint(80, 260));
  const auto right_end = canvas->widget_position_for_document_point(QPoint(580, 260));
  CHECK(std::abs(handles[0].toPointF().x() - left_end.x()) <= 1.5);
  CHECK(std::abs(handles[1].toPointF().x() - right_end.x()) <= 1.5);
  CHECK(std::abs(handles[2].toPointF().x() - (left_end.x() + right_end.x()) / 2.0) <= 1.5);

  const auto range_of = [&]() {
    return parse_text_path_range(editor->property("patchy.documentTextPathRange").toString().toStdString())
        .value_or(TextPathRange{});
  };
  CHECK(range_of().start == 0.0 && range_of().end == 1.0);

  // Drag the end mark 200 px back along the line.
  const auto end_point = overlay_point(*overlay, handles[1]);
  send_mouse(*overlay, QEvent::MouseButtonPress, end_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*overlay, QEvent::MouseMove, end_point - QPoint(100, 3), Qt::NoButton, Qt::LeftButton);
  // Mid-drag only the overlay moved: the session's range is still the old one.
  CHECK(range_of().end == 1.0);
  const auto dragged = overlay->property("patchy.transformedTextPathHandleCenters").toList();
  CHECK(dragged.size() == 3 && std::abs(dragged[1].toPointF().x() - (right_end.x() - 100)) <= 1.5);
  send_mouse(*overlay, QEvent::MouseMove, end_point - QPoint(200, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*overlay, QEvent::MouseButtonRelease, end_point - QPoint(200, 0), Qt::LeftButton, Qt::NoButton);
  process_events_for(150);
  const TextPathCurve line(*text_path_for_layer(committed));
  CHECK(std::abs(line.distance_at_parameter(range_of().end) - 300.0) <= 1.5);
  CHECK(range_of().start == 0.0);
  CHECK(editor->toPlainText() == QStringLiteral("Along"));  // the story is untouched

  // Slide the whole range 80 px to the right with the middle mark.
  overlay = wait_for_path_overlay(*canvas);
  CHECK(overlay != nullptr);
  if (!overlay) return;
  handles = overlay->property("patchy.transformedTextPathHandleCenters").toList();
  CHECK(handles.size() == 3);
  if (handles.size() != 3) return;
  const auto middle_point = overlay_point(*overlay, handles[2]);
  send_mouse(*overlay, QEvent::MouseButtonPress, middle_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*overlay, QEvent::MouseMove, middle_point + QPoint(80, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*overlay, QEvent::MouseButtonRelease, middle_point + QPoint(80, 0), Qt::LeftButton, Qt::NoButton);
  process_events_for(150);
  CHECK(std::abs(line.distance_at_parameter(range_of().start) - 80.0) <= 1.5);
  CHECK(std::abs(line.distance_at_parameter(range_of().end) - 380.0) <= 1.5);
  const auto path_before_flip = parse_vector_path(editor->property("patchy.documentTextPath").toString().toStdString());

  // Drag the middle mark across the line: the contour reverses and the text hangs below.
  overlay = wait_for_path_overlay(*canvas);
  CHECK(overlay != nullptr);
  if (!overlay) return;
  handles = overlay->property("patchy.transformedTextPathHandleCenters").toList();
  CHECK(handles.size() == 3);
  if (handles.size() != 3) return;
  const auto flip_point = overlay_point(*overlay, handles[2]);
  send_mouse(*overlay, QEvent::MouseButtonPress, flip_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*overlay, QEvent::MouseMove, flip_point + QPoint(0, 40), Qt::NoButton, Qt::LeftButton);
  send_mouse(*overlay, QEvent::MouseButtonRelease, flip_point + QPoint(0, 40), Qt::LeftButton, Qt::NoButton);
  process_events_for(150);
  const auto flipped_path = parse_vector_path(editor->property("patchy.documentTextPath").toString().toStdString());
  CHECK(path_before_flip.has_value() && flipped_path.has_value());
  if (!path_before_flip || !flipped_path) return;
  CHECK(*flipped_path == reversed_text_path(*path_before_flip));
  // The same stretch of the line, measured from its other end.
  const TextPathCurve reversed_line(*flipped_path);
  CHECK(std::abs(reversed_line.distance_at_parameter(range_of().start) - 120.0) <= 1.5);
  CHECK(std::abs(reversed_line.distance_at_parameter(range_of().end) - 420.0) <= 1.5);
  save_widget_artifact("ui_text_path_flipped", *canvas);

  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  const auto* layer = doc.find_layer(id);
  CHECK(layer != nullptr);
  if (!layer) return;
  // Below the line now (the text stood above it before), and one undo puts everything back.
  CHECK(layer->bounds().y + layer->bounds().height / 2 > 262);
  CHECK(committed.bounds().y + committed.bounds().height / 2 < 258);
  MainWindowTestAccess::undo(window);
  CHECK(doc.find_layer(id)->metadata() == committed.metadata());
  CHECK(doc.find_layer(id)->bounds() == committed.bounds());
}

// The caret is a bar across the line where the text bends, and a click on it puts the cursor
// back where it was (the hit test resolves from the nearest point of the contour).
void ui_text_path_caret_and_clicks_follow_the_contour() {
  MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, R"JS(
    var d=app.newDocument(660,520);
    var p={subpaths:[{closed:false,anchors:[{x:80,y:360,outX:200,outY:60},{x:580,y:360,inX:460,inY:60}]}]};
    var t=d.addTextLayer('One two three four five six seven',{font:'Arial',size:32,path:p});
    patchy.setResult(t.id);
  )JS");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  const auto id = window.script_engine_host().last_result().toString().toULongLong();
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto bounds = doc.find_layer(id)->bounds();
  // A press on the ink of the first word.
  click_canvas(*canvas, QPoint(bounds.x + 14, bounds.y + bounds.height - 30));
  auto* editor = session_editor(*canvas);
  CHECK(editor != nullptr);
  if (!editor) return;
  auto* overlay = wait_for_path_overlay(*canvas);
  CHECK(overlay != nullptr && overlay->isVisible());
  if (!overlay) return;
  const TextPathCurve curve(*text_path_in_document(*doc.find_layer(id)));
  const auto text_length = editor->toPlainText().size();
  for (const int position : {0, 5, 14, static_cast<int>(text_length)}) {
    auto cursor = editor->textCursor();
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
    overlay->repaint();
    const auto polygon = overlay->property("patchy.transformedTextCaretPolygon").toList();
    CHECK(polygon.size() == 4);
    if (polygon.size() != 4) continue;
    QPointF centre;
    for (const auto& point : polygon) centre += point.toPointF() / 4.0;
    const auto document_point = canvas->document_point_for_widget_position(centre);
    const auto nearest = curve.nearest(document_point.x(), document_point.y());
    // The bar stands on the text's side of the line, within the type's height of it.
    CHECK(nearest.left);
    CHECK(nearest.gap > 2.0 && nearest.gap < 32.0);
    // Its long axis lies along the contour's normal there.
    const auto top = polygon[0].toPointF();
    const auto bottom = polygon[3].toPointF();
    const auto sample = curve.sample(nearest.distance);
    const auto along = bottom - top;
    const auto length = std::hypot(along.x(), along.y());
    CHECK(length > 20.0);
    CHECK(std::abs((along.x() * sample.tangent_x + along.y() * sample.tangent_y) / length) < 0.05);
    // A click there lands on the same position.
    click_widget_like_a_user(*canvas, centre.toPoint());
    QApplication::processEvents();
    CHECK(session_editor(*canvas) == editor);
    if (session_editor(*canvas) != editor) return;
    CHECK(editor->textCursor().position() == position);
    overlay = session_overlay(*canvas);
    CHECK(overlay != nullptr);
    if (!overlay) return;
  }
  // A selection highlights along the contour without ending the session.
  auto cursor = editor->textCursor();
  cursor.setPosition(4);
  cursor.setPosition(13, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  overlay->repaint();
  save_widget_artifact("ui_text_path_selection", *canvas);
  save_widget_artifact("ui_text_path_window", window);
  // A click well off the line, though inside the contour's bounds, is not a caret click: it
  // clicks off, which commits this session (the Type tool may open a new one at the click).
  const QPointer<QTextEdit> session(editor);
  cursor.insertText(QStringLiteral("XYZ"));
  // Sent to the canvas, whose filter offers every press to the overlay first: the overlay
  // declines this one, and the canvas handles it as a click off the text.
  click_canvas(*canvas, QPoint(330, 330));
  process_events_for(50);
  CHECK(session.isNull() || session_editor(*canvas) != session.data());
  if (auto* next = session_editor(*canvas); next != nullptr && next != session.data()) {
    send_key(*next, Qt::Key_Escape);
    QApplication::processEvents();
  }
  const auto* committed = doc.find_layer(id);
  CHECK(committed != nullptr && text_path_for_layer(*committed).has_value());
  if (committed != nullptr) {
    CHECK(QString::fromStdString(committed->metadata().at(kLayerMetadataText)).contains(QStringLiteral("XYZ")));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> text_path_tests() {
  return {
      {"ui_text_path_line_plan_maps_onto_the_contour", ui_text_path_line_plan_maps_onto_the_contour},
      {"ui_text_path_glyphs_match_photoshop_placement_if_available",
       ui_text_path_glyphs_match_photoshop_placement_if_available},
      {"ui_text_path_photoshop_files_redraw_where_photoshop_drew_them_if_available",
       ui_text_path_photoshop_files_redraw_where_photoshop_drew_them_if_available},
      {"ui_text_path_photoshop_file_edits_in_place", ui_text_path_photoshop_file_edits_in_place},
      {"ui_text_path_script_creation_range_flip_and_undo", ui_text_path_script_creation_range_flip_and_undo},
      {"ui_text_path_type_tool_targets_shape_edges_and_reopens", ui_text_path_type_tool_targets_shape_edges_and_reopens},
      {"ui_text_path_type_tool_types_along_an_open_work_path", ui_text_path_type_tool_types_along_an_open_work_path},
      {"ui_text_path_handles_move_the_range_and_flip", ui_text_path_handles_move_the_range_and_flip},
      {"ui_text_path_caret_and_clicks_follow_the_contour", ui_text_path_caret_and_clicks_follow_the_contour},
  };
}
