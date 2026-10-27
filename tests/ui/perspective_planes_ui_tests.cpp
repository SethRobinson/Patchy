#include "core/document.hpp"
#include "core/perspective_planes.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui/perspective_planes_dialog.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

constexpr int kViewWidth = 760;
constexpr int kViewHeight = 520;

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

// A flat brick texture: running bond, mortar lines, a little color variation per brick.
patchy::PixelBuffer brick_texture(int width, int height) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  const int brick_w = 48;
  const int brick_h = 20;
  for (int y = 0; y < height; ++y) {
    const int row = y / brick_h;
    for (int x = 0; x < width; ++x) {
      const int shifted = x + ((row % 2) != 0 ? brick_w / 2 : 0);
      const int column = shifted / brick_w;
      const bool mortar = (y % brick_h) < 3 || (shifted % brick_w) < 3;
      const auto noise = mix(static_cast<std::uint64_t>(row) * 1000003ULL + static_cast<std::uint64_t>(column));
      const auto grain = mix(static_cast<std::uint64_t>(y) * 7919ULL + static_cast<std::uint64_t>(x)) % 18U;
      auto* p = pixels.pixel(x, y);
      if (mortar) {
        p[0] = static_cast<std::uint8_t>(196 + grain);
        p[1] = static_cast<std::uint8_t>(188 + grain);
        p[2] = static_cast<std::uint8_t>(172 + grain);
      } else {
        const int shade = static_cast<int>(noise % 40U);
        p[0] = static_cast<std::uint8_t>(std::min(255, 150 + shade + static_cast<int>(grain)));
        p[1] = static_cast<std::uint8_t>(64 + shade / 2 + static_cast<int>(grain) / 2);
        p[2] = static_cast<std::uint8_t>(48 + shade / 3);
      }
      p[3] = 255;
    }
  }
  return pixels;
}

// A street scene drawn in code: sky, a paved floor and a brick wall seen at an angle.
const patchy::PlaneQuad kWall{{{130.0, 50.0}, {560.0, 150.0}, {560.0, 370.0}, {130.0, 480.0}}};

// The floor joined to the wall's bottom edge, as the workspace's + tab makes it.
patchy::PerspectivePlane floor_plane() {
  const auto camera = patchy::perspective_camera_for({{1, kWall, 0.0, std::nullopt}}, 800, 600);
  const auto quad = patchy::connected_plane_quad(kWall, 2, 90.0, 0.7, camera);
  CHECK(quad.has_value());
  return {2, *quad, 0.0, patchy::PlaneLink{1, 2, 90.0, 0.7}};
}

patchy::PixelBuffer wall_scene(int width = 800, int height = 600) {
  patchy::PixelBuffer scene(width, height, patchy::PixelFormat::rgba8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* p = scene.pixel(x, y);
      const double t = static_cast<double>(y) / height;
      p[0] = static_cast<std::uint8_t>(110 + 80 * t);
      p[1] = static_cast<std::uint8_t>(150 + 60 * t);
      p[2] = static_cast<std::uint8_t>(205 - 20 * t);
      p[3] = 255;
    }
  }
  patchy::PerspectiveCanvas canvas(scene, patchy::Rect{0, 0, width, height}, width, height);
  const auto camera = patchy::perspective_camera_for({{1, kWall, 0.0, std::nullopt}}, width, height);
  const auto wall = patchy::plane_geometry(kWall, camera);
  const auto floor = patchy::plane_geometry(floor_plane().corners, camera);
  CHECK(wall.has_value() && floor.has_value());
  const auto bricks = brick_texture(480, 300);
  canvas.paste(*wall, bricks, patchy::fill_plane_from_image(*wall, bricks.width(), bricks.height()));
  patchy::PixelBuffer paving(320, 320, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 320; ++y) {
    for (int x = 0; x < 320; ++x) {
      auto* p = paving.pixel(x, y);
      const bool joint = (x % 40) < 2 || (y % 40) < 2;
      const auto grain = static_cast<int>(mix(static_cast<std::uint64_t>(y) * 4099ULL + static_cast<std::uint64_t>(x)) % 22U);
      const int tile = static_cast<int>(mix(static_cast<std::uint64_t>(y / 40) * 31ULL + static_cast<std::uint64_t>(x / 40)) % 30U);
      const int value = joint ? 70 : 128 + tile + grain;
      p[0] = static_cast<std::uint8_t>(value);
      p[1] = static_cast<std::uint8_t>(value - 4);
      p[2] = static_cast<std::uint8_t>(value - 10);
      p[3] = 255;
    }
  }
  canvas.paste(*floor, paving, patchy::fill_plane_from_image(*floor, paving.width(), paving.height()));
  return canvas.composite();
}

// The picture to paste: a framed poster with a few shapes and a word.
patchy::PixelBuffer poster() {
  QImage image(300, 200, QImage::Format_RGBA8888);
  image.fill(QColor(250, 244, 226));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(40, 40, 60), 10));
  painter.drawRect(5, 5, 290, 190);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(230, 90, 40));
  painter.drawEllipse(QPointF(80, 85), 42, 42);
  painter.setBrush(QColor(40, 130, 200));
  painter.drawRect(150, 50, 110, 70);
  painter.setPen(QColor(40, 40, 60));
  QFont font = painter.font();
  font.setPixelSize(38);
  font.setBold(true);
  painter.setFont(font);
  painter.drawText(QRect(10, 128, 280, 56), Qt::AlignCenter, QStringLiteral("PATCHY"));
  painter.end();
  patchy::PixelBuffer pixels(image.width(), image.height(), patchy::PixelFormat::rgba8());
  for (int y = 0; y < image.height(); ++y) {
    std::copy_n(image.constScanLine(y), static_cast<std::size_t>(image.width()) * 4, pixels.pixel(0, y));
  }
  return pixels;
}

// Where a document point lands in the workspace view once it fits a fixed-size view.
QPoint view_point(QSize document, patchy::PlanePoint point) {
  const double margin = 24.0;
  const double zoom = std::min((kViewWidth - 2.0 * margin) / document.width(),
                               (kViewHeight - 2.0 * margin) / document.height());
  const double ox = (kViewWidth - document.width() * zoom) * 0.5;
  const double oy = (kViewHeight - document.height() * zoom) * 0.5;
  return QPoint(static_cast<int>(std::lround(ox + point.x * zoom)), static_cast<int>(std::lround(oy + point.y * zoom)));
}

using WorkspaceDriver = std::function<void(QDialog&, QWidget&)>;

// Runs the driver once the workspace is up (the view fixed at kViewWidth x kViewHeight).
void schedule_workspace_driver(const WorkspaceDriver& driver, std::exception_ptr& error, bool& ran) {
  QTimer::singleShot(0, [&error, &ran, driver] {
    auto* dialog = find_top_level_dialog(QStringLiteral("perspectivePlanesDialog"));
    try {
      CHECK(dialog != nullptr);
      auto* view = dialog->findChild<QWidget*>(QStringLiteral("perspectivePlanesView"));
      CHECK(view != nullptr);
      view->setFixedSize(kViewWidth, kViewHeight);
      dialog->activateWindow();
      view->setFocus();
      QApplication::processEvents();
      driver(*dialog, *view);
      ran = true;
    } catch (...) {
      error = std::current_exception();
      if (dialog != nullptr) {
        dialog->reject();
      }
    }
  });
}

QToolButton* tool(QDialog& dialog, const char* name) {
  auto* button = dialog.findChild<QToolButton*>(QLatin1String(name));
  CHECK(button != nullptr);
  return button;
}

QString hint_text(QDialog& dialog) {
  auto* hint = dialog.findChild<QLabel*>(QStringLiteral("perspectivePlanesHint"));
  CHECK(hint != nullptr);
  return hint->text();
}

void click_corners(QWidget& view, QSize document, const patchy::PlaneQuad& quad) {
  for (const auto& corner : quad) {
    const auto point = view_point(document, corner);
    send_mouse(view, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, point);
  }
  QApplication::processEvents();
}

void add_scene_document(patchy::ui::MainWindow& window, const QString& name) {
  patchy::Document document(800, 600, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Street", wall_scene());
  const auto id = layer.id();
  layer.set_bounds(patchy::Rect{0, 0, 800, 600});
  document.add_layer(std::move(layer));
  document.set_active_layer(id);
  window.add_document_session(std::move(document), name);
}

std::size_t layer_count(patchy::ui::MainWindow& window) {
  return std::as_const(MainWindowTestAccess::document(window)).layers().size();
}

void ui_perspective_planes_cancel_leaves_the_document_untouched() {
  patchy::ui::MainWindow window;
  add_scene_document(window, QStringLiteral("Perspective Cancel"));
  show_window(window);
  (void)require_canvas(window);
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto layers_before = layer_count(window);
  const auto pixels_before = std::as_const(MainWindowTestAccess::document(window)).layers().front().pixels();
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        CHECK(hint_text(dialog).contains(QStringLiteral("first corner")));
        click_corners(view, QSize(800, 600), kWall);
        // A new plane switches to Edit Plane and says how to adjust it.
        CHECK(tool(dialog, "perspectiveEditPlaneTool")->isChecked());
        CHECK(hint_text(dialog).contains(QStringLiteral("Drag a corner")));
        QTest::mouseClick(tool(dialog, "perspectivePaintTool"), Qt::LeftButton);
        drag(view, view_point(QSize(800, 600), {250.0, 250.0}), view_point(QSize(800, 600), {400.0, 280.0}));
        dialog.reject();
      },
      error, ran);
  require_action(window, "filterPerspectivePlanesAction")->trigger();
  QApplication::processEvents();
  if (error) {
    std::rethrow_exception(error);
  }
  CHECK(ran);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
  CHECK(layer_count(window) == layers_before);
  CHECK(patchy::ui::pixel_buffers_equal(std::as_const(MainWindowTestAccess::document(window)).layers().front().pixels(), pixels_before));
  // The plane stays with the document for the next visit.
  CHECK(patchy::test::ui::scripting_support::run_script(
      window, QStringLiteral("console.log('planes=' + patchy.perspective.getPlanes(app.activeDocument).length);")));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("planes=1")));
}

void ui_perspective_planes_ok_writes_one_undo_step() {
  patchy::ui::MainWindow window;
  add_scene_document(window, QStringLiteral("Perspective Apply"));
  show_window(window);
  (void)require_canvas(window);
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto layers_before = layer_count(window);
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        click_corners(view, QSize(800, 600), kWall);
        QTest::mouseClick(tool(dialog, "perspectivePaintTool"), Qt::LeftButton);
        drag(view, view_point(QSize(800, 600), {250.0, 250.0}), view_point(QSize(800, 600), {400.0, 280.0}));
        auto* undo = dialog.findChild<QPushButton*>(QStringLiteral("perspectiveUndoButton"));
        CHECK(undo != nullptr && undo->isEnabled());
        dialog.accept();
      },
      error, ran);
  require_action(window, "filterPerspectivePlanesAction")->trigger();
  QApplication::processEvents();
  if (error) {
    std::rethrow_exception(error);
  }
  CHECK(ran);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(layer_count(window) == layers_before + 1);
  // The new layer sits above the street and holds only the paint, inside the wall.
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto& painted = document.layers().back();
  CHECK(painted.name() == "Perspective");
  const auto bounds = painted.bounds();
  CHECK(bounds.x >= 118 && bounds.x + bounds.width <= 562 && bounds.y >= 58);
  CHECK(patchy::test::ui::scripting_support::run_script(window, QStringLiteral("app.activeDocument.undo();")));
  QApplication::processEvents();
  CHECK(layer_count(window) == layers_before);
}

void ui_perspective_planes_explains_impossible_corners() {
  patchy::ui::PerspectiveWorkspaceInput input;
  input.layer = wall_scene(400, 300);
  input.layer_bounds = patchy::Rect{0, 0, 400, 300};
  input.document_width = 400;
  input.document_height = 300;
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        const QSize document(400, 300);
        // Clicked across the diagonal: the fourth corner would make two sides cross.
        const patchy::PlaneQuad crossing{{{50.0, 50.0}, {300.0, 250.0}, {300.0, 50.0}, {50.0, 250.0}}};
        for (std::size_t i = 0; i < 3; ++i) {
          QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, view_point(document, crossing[i]));
        }
        send_mouse(view, QEvent::MouseMove, view_point(document, crossing[3]), Qt::NoButton, Qt::NoButton);
        QApplication::processEvents();
        CHECK(hint_text(dialog).contains(QStringLiteral("cannot be a plane")));
        CHECK(hint_text(dialog).contains(QStringLiteral("cross")));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, view_point(document, crossing[3]));
        // Refused: still placing the fourth corner.
        CHECK(tool(dialog, "perspectiveCreatePlaneTool")->isChecked());
        dialog.reject();
      },
      error, ran);
  const auto result = patchy::ui::run_perspective_planes_workspace(nullptr, std::move(input));
  if (error) {
    std::rethrow_exception(error);
  }
  CHECK(ran);
  CHECK(!result.accepted);
  CHECK(result.planes.empty());
}

// The card's pictures: the workspace with two joined planes on the street scene and a
// poster being placed on the wall, then the result in the document.
void ui_perspective_planes_workspace_captures() {
  std::filesystem::create_directories("test-artifacts/perspective-planes");
  const auto scene = wall_scene();
  {
    QImage image(scene.width(), scene.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < scene.height(); ++y) {
      std::copy_n(scene.pixel(0, y), static_cast<std::size_t>(scene.width()) * 4, image.scanLine(y));
    }
    CHECK(image.save(QStringLiteral("test-artifacts/perspective-planes/brick-wall.png")));
    const auto art = poster();
    QImage poster_image(art.width(), art.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < art.height(); ++y) {
      std::copy_n(art.pixel(0, y), static_cast<std::size_t>(art.width()) * 4, poster_image.scanLine(y));
    }
    CHECK(poster_image.save(QStringLiteral("test-artifacts/perspective-planes/poster.png")));
  }
  patchy::ui::PerspectiveWorkspaceInput input;
  input.layer = scene;
  input.layer_bounds = patchy::Rect{0, 0, 800, 600};
  input.document_width = 800;
  input.document_height = 600;
  input.planes.push_back({1, kWall, 0.0, std::nullopt});
  input.planes.push_back(floor_plane());
  input.paste_sources.push_back({QStringLiteral("Clipboard Image"), poster()});
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        const QSize document(800, 600);
        // The wall selected, with the floor joined to its bottom edge.
        QTest::mouseClick(tool(dialog, "perspectiveEditPlaneTool"), Qt::LeftButton);
        save_widget_artifact("perspective-planes/before-plane-only", dialog);
        auto* paste = dialog.findChild<QToolButton*>(QStringLiteral("perspectivePasteButton"));
        CHECK(paste != nullptr && paste->isEnabled());
        auto* source = paste->menu()->findChild<QAction*>(QStringLiteral("perspectivePasteSource0"));
        CHECK(source != nullptr);
        source->trigger();
        QApplication::processEvents();
        CHECK(hint_text(dialog).contains(QStringLiteral("Enter places it")));
        // Drag the poster toward the far end of the wall: it shrinks with distance.
        const auto camera = patchy::perspective_camera_for({{1, kWall, 0.0, std::nullopt}}, 800, 600);
        const auto wall = patchy::plane_geometry(kWall, camera);
        CHECK(wall.has_value());
        const auto middle = wall->to_image.map({wall->width * 0.5, wall->height * 0.5});
        const auto far = wall->to_image.map({wall->width * 0.78, wall->height * 0.42});
        CHECK(middle.has_value() && far.has_value());
        drag(view, view_point(document, *middle), view_point(document, *far));
        send_mouse(view, QEvent::MouseMove, view_point(document, {700.0, 120.0}), Qt::NoButton, Qt::NoButton);
        QApplication::processEvents();
        save_widget_artifact("perspective-planes/after-workspace-paste", dialog);
        QTest::keyClick(&view, Qt::Key_Return);
        QApplication::processEvents();
        QTest::mouseClick(tool(dialog, "perspectiveCloneTool"), Qt::LeftButton);
        dialog.accept();
      },
      error, ran);
  const auto result = patchy::ui::run_perspective_planes_workspace(nullptr, std::move(input));
  if (error) {
    std::rethrow_exception(error);
  }
  CHECK(ran);
  CHECK(result.accepted && result.edits.has_value());
  // Dragging the image moved the image, not the wall; the floor is still joined to it.
  CHECK(result.planes.size() == 2 && result.planes.front().corners == kWall);
  CHECK(result.planes.back().link.has_value());
  // The result composited into the document.
  QImage document_image(scene.width(), scene.height(), QImage::Format_RGBA8888);
  patchy::Rect bounds{};
  const auto merged = patchy::composite_edits_onto_layer(scene, patchy::Rect{0, 0, 800, 600}, result.edits->first,
                                                         result.edits->second, bounds);
  for (int y = 0; y < merged.height(); ++y) {
    std::copy_n(merged.pixel(0, y), static_cast<std::size_t>(merged.width()) * 4, document_image.scanLine(y));
  }
  CHECK(document_image.save(QStringLiteral("test-artifacts/perspective-planes/after-result.png")));
  // The poster landed on the wall only.
  const auto& edits = result.edits->first;
  const auto& rect = result.edits->second;
  CHECK(rect.x >= 118 && rect.x + rect.width <= 562);
  CHECK(edits.width() > 40 && edits.height() > 30);
}

void ui_script_perspective_paste_paint_and_clone() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto source = QStringLiteral(R"JS(
    var doc = app.newDocument(240, 200, {name: "Perspective Script"});
    var base = doc.activeLayer;
    base.fillRect(0, 0, 240, 200, "#204060");
    var poster = doc.addLayer("Poster");
    poster.fillRect(0, 0, 32, 32, "#ff0000");
    doc.activeLayer = base;
    var plane = [[70, 40], [170, 40], [220, 180], [20, 180]];
    console.log("check=" + patchy.perspective.checkPlane(plane) + "|" +
                patchy.perspective.checkPlane([[0, 0], [100, 100], [100, 0], [0, 100]]));
    var count = doc.layers.length;
    var pasted = patchy.perspective.paste(base, {plane: plane, source: poster});
    var block = pasted.getPixels();
    var bytes = new Uint8Array(block.data);
    var at = function (x, y) { var i = ((y - block.y) * block.width + (x - block.x)) * 4; return [bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]]; };
    var inside = at(120, 110);
    console.log("paste=" + inside.join(",") + " outside=" + (block.x > 10 ? "clear" : "covered") +
                " layers=" + (doc.layers.length - count));
    var painted = patchy.perspective.paint(base, {plane: plane, points: [[120, 100], [140, 120]], color: "#00ff00",
                                                  size: 20, hardness: 100, target: "layer"});
    console.log("paintSame=" + (painted.id === base.id));
    patchy.perspective.setPlanes(doc, [plane]);
    console.log("planes=" + patchy.perspective.getPlanes(doc).length);
    var cloned = patchy.perspective.clone(base, {plane: 0, from: [60, 170], points: [[150, 150], [160, 150]], size: 16});
    console.log("clone=" + (cloned !== null));
    var thrown = "none";
    try { patchy.perspective.paint(base, {plane: [[0, 0], [1, 0], [1, 1], [0, 1]], points: [[0, 0]]}); }
    catch (e) { thrown = "refused"; }
    console.log("tiny=" + thrown);
  )JS");
  CHECK(patchy::test::ui::scripting_support::run_script(window, source));
  using patchy::test::ui::scripting_support::backlog_contains;
  CHECK(backlog_contains(window, QStringLiteral("check=|crossing")));
  CHECK(backlog_contains(window, QStringLiteral("paste=255,0,0,255 outside=clear layers=1")));
  CHECK(backlog_contains(window, QStringLiteral("paintSame=true")));
  CHECK(backlog_contains(window, QStringLiteral("planes=1")));
  CHECK(backlog_contains(window, QStringLiteral("clone=true")));
  CHECK(backlog_contains(window, QStringLiteral("tiny=refused")));
}

}  // namespace

std::vector<patchy::test::TestCase> perspective_planes_ui_tests() {
  return {
      {"ui_perspective_planes_cancel_leaves_the_document_untouched",
       ui_perspective_planes_cancel_leaves_the_document_untouched},
      {"ui_perspective_planes_ok_writes_one_undo_step", ui_perspective_planes_ok_writes_one_undo_step},
      {"ui_perspective_planes_explains_impossible_corners", ui_perspective_planes_explains_impossible_corners},
      {"ui_perspective_planes_workspace_captures", ui_perspective_planes_workspace_captures},
      {"ui_script_perspective_paste_paint_and_clone", ui_script_perspective_paste_paint_and_clone},
  };
}
