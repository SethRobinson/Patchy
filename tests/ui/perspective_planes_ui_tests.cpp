#include "core/document.hpp"
#include "core/perspective_planes.hpp"
#include "render/compositor.hpp"
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
#include <QClipboard>
#include <QCheckBox>
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

// Seth's path (#457 follow-up): the poster pasted into the wall document as its own layer,
// which is active when the workspace opens.
constexpr int kPosterX = 250;
constexpr int kPosterY = 200;

patchy::LayerId add_poster_document(patchy::ui::MainWindow& window, const QString& name) {
  patchy::Document document(800, 600, patchy::PixelFormat::rgba8());
  patchy::Layer wall(document.allocate_layer_id(), "Street", wall_scene());
  wall.set_bounds(patchy::Rect{0, 0, 800, 600});
  document.add_layer(std::move(wall));
  const auto art = poster();
  patchy::Layer layer(document.allocate_layer_id(), "Poster", art);
  layer.set_bounds(patchy::Rect{kPosterX, kPosterY, art.width(), art.height()});
  const auto id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(id);
  window.add_document_session(std::move(document), name);
  return id;
}

const patchy::Layer* find_layer(patchy::ui::MainWindow& window, patchy::LayerId id) {
  return std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
}

QPushButton* button(QDialog& dialog, const char* name) {
  auto* found = dialog.findChild<QPushButton*>(QLatin1String(name));
  CHECK(found != nullptr);
  return found;
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
        CHECK(hint_text(dialog).contains(QStringLiteral("Step 1 of 2")));
        click_corners(view, QSize(800, 600), kWall);
        // A new plane switches to Edit Plane and says how to adjust it.
        CHECK(tool(dialog, "perspectiveEditPlaneTool")->isChecked());
        CHECK(hint_text(dialog).contains(QStringLiteral("Step 2 of 2")));
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

// The workspace draws the document's other layers behind the layer it edits, so the wall a
// pasted poster layer belongs on is there to line up with; the opening hint suggests Place
// Layer. Cancel changes nothing.
void ui_perspective_planes_shows_the_other_layers_behind() {
  patchy::ui::MainWindow window;
  const auto poster_id = add_poster_document(window, QStringLiteral("Perspective Backdrop"));
  show_window(window);
  (void)require_canvas(window);
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto layers_before = layer_count(window);
  const auto poster_before = std::as_const(*find_layer(window, poster_id)).pixels();
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        const QSize document(800, 600);
        CHECK(hint_text(dialog).contains(QStringLiteral("where this layer should go")));
        auto* show_others = dialog.findChild<QCheckBox*>(QStringLiteral("perspectiveShowOtherLayersCheck"));
        CHECK(show_others != nullptr && show_others->isVisible() && show_others->isChecked());
        // On the wall, outside the poster: the poster layer is transparent there, the wall is not.
        const auto probe = view_point(document, {170.0, 300.0});
        const QColor shown = view.grab().toImage().pixelColor(probe);
        CHECK(shown.red() > shown.blue() + 40);
        show_others->setChecked(false);
        QApplication::processEvents();
        const QColor hidden = view.grab().toImage().pixelColor(probe);
        CHECK(std::abs(hidden.red() - hidden.blue()) < 12);
        std::filesystem::create_directories("test-artifacts/perspective-planes");
        show_others->setChecked(true);
        QApplication::processEvents();
        save_widget_artifact("perspective-planes/after-open-on-poster-layer", dialog);
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
  CHECK(patchy::ui::pixel_buffers_equal(std::as_const(*find_layer(window, poster_id)).pixels(), poster_before));
}

// Place Layer: a finished plane names its next steps; Place Layer moves the poster layer's
// own content onto the plane and OK replaces that layer, as one undo step.
void ui_perspective_planes_place_layer_replaces_the_layer() {
  patchy::ui::MainWindow window;
  const auto poster_id = add_poster_document(window, QStringLiteral("Perspective Place Layer"));
  show_window(window);
  (void)require_canvas(window);
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto layers_before = layer_count(window);
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        click_corners(view, QSize(800, 600), kWall);
        CHECK(hint_text(dialog).contains(QStringLiteral("Step 2 of 2")));
        auto* place = button(dialog, "perspectivePlaceLayerButton");
        CHECK(place->isVisible() && place->isEnabled());
        CHECK(button(dialog, "perspectivePasteOntoPlaneButton")->isVisible());
        CHECK(button(dialog, "perspectiveNextCloneButton")->isVisible());
        std::filesystem::create_directories("test-artifacts/perspective-planes");
        save_widget_artifact("perspective-planes/after-plane-next-steps", dialog);
        QTest::mouseClick(place, Qt::LeftButton);
        QApplication::processEvents();
        CHECK(hint_text(dialog).contains(QStringLiteral("The layer now fills the plane")));
        auto* output = dialog.findChild<QComboBox*>(QStringLiteral("perspectiveOutputCombo"));
        CHECK(output != nullptr && output->currentIndex() == 1);
        send_mouse(view, QEvent::MouseMove, view_point(QSize(800, 600), {700.0, 120.0}), Qt::NoButton, Qt::NoButton);
        QApplication::processEvents();
        save_widget_artifact("perspective-planes/after-place-layer", dialog);
        QTest::keyClick(&view, Qt::Key_Return);
        QApplication::processEvents();
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
  CHECK(layer_count(window) == layers_before);
  const auto* poster_layer = find_layer(window, poster_id);
  CHECK(poster_layer != nullptr);
  const auto& pixels = std::as_const(*poster_layer).pixels();
  const auto bounds = poster_layer->bounds();
  const auto alpha_at = [&](int x, int y) {
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
      return 0;
    }
    return static_cast<int>(pixels.pixel(x - bounds.x, y - bounds.y)[3]);
  };
  // The poster now covers the wall: near the wall's far top corner, and no longer just its old box.
  CHECK(alpha_at(540, 170) == 255);
  CHECK(alpha_at(140, 70) == 255);
  {
    const auto flattened = patchy::Compositor().flatten_rgb8(MainWindowTestAccess::document(window));
    QImage image(flattened.width(), flattened.height(), QImage::Format_RGB888);
    for (int y = 0; y < flattened.height(); ++y) {
      std::copy_n(flattened.pixel(0, y), static_cast<std::size_t>(flattened.width()) * 3, image.scanLine(y));
    }
    CHECK(image.save(QStringLiteral("test-artifacts/perspective-planes/after-place-layer-result.png")));
  }
  CHECK(patchy::test::ui::scripting_support::run_script(window, QStringLiteral("app.activeDocument.undo();")));
  QApplication::processEvents();
  const auto* restored = find_layer(window, poster_id);
  CHECK(restored != nullptr && restored->bounds().x == kPosterX && restored->bounds().y == kPosterY);
}

QString label_text(QDialog& dialog, const char* name) {
  auto* label = dialog.findChild<QLabel*>(QLatin1String(name));
  CHECK(label != nullptr);
  return label->text();
}

// Seth's second report (#457): setting the target plane comes first. Before any plane the
// tools that put something on a plane are disabled and the hint is a Step 1 banner; a
// finished plane is the target (Step 2); with two planes a click picks the target.
void ui_perspective_planes_needs_a_target_plane_first() {
  patchy::ui::MainWindow window;
  (void)add_poster_document(window, QStringLiteral("Perspective Target"));
  show_window(window);
  (void)require_canvas(window);
  std::exception_ptr error;
  bool ran = false;
  schedule_workspace_driver(
      [](QDialog& dialog, QWidget& view) {
        const QSize document(800, 600);
        CHECK(tool(dialog, "perspectiveCreatePlaneTool")->isChecked());
        CHECK(!tool(dialog, "perspectiveCloneTool")->isEnabled());
        CHECK(!tool(dialog, "perspectivePaintTool")->isEnabled());
        CHECK(!tool(dialog, "perspectivePasteButton")->isEnabled());
        CHECK(!tool(dialog, "perspectivePlaceLayerTool")->isEnabled());
        CHECK(tool(dialog, "perspectiveCloneTool")->toolTip().contains(QStringLiteral("Draw a plane first")));
        CHECK(hint_text(dialog).startsWith(QStringLiteral("Step 1 of 2: set the target plane")));
        auto* hint = dialog.findChild<QLabel*>(QStringLiteral("perspectivePlanesHint"));
        CHECK(hint->property("patchy.step").toBool() && hint->font().bold());
        CHECK(label_text(dialog, "perspectiveTargetLabel").contains(QStringLiteral("no plane yet")));
        CHECK(label_text(dialog, "perspectiveTargetLabel").contains(QStringLiteral("Editing: Poster")));
        std::filesystem::create_directories("test-artifacts/perspective-planes");
        save_widget_artifact("perspective-planes/after-step-one", dialog);
        click_corners(view, document, kWall);
        CHECK(tool(dialog, "perspectiveCloneTool")->isEnabled());
        CHECK(hint_text(dialog).startsWith(QStringLiteral("Step 2 of 2")));
        CHECK(label_text(dialog, "perspectiveTargetLabel").contains(QStringLiteral("plane 1 of 1")));
        // A second plane becomes the target; a click inside the first picks it again.
        QTest::mouseClick(tool(dialog, "perspectiveCreatePlaneTool"), Qt::LeftButton);
        click_corners(view, document, {{{600.0, 120.0}, {760.0, 100.0}, {760.0, 300.0}, {600.0, 290.0}}});
        CHECK(label_text(dialog, "perspectiveTargetLabel").contains(QStringLiteral("plane 2 of 2")));
        CHECK(hint_text(dialog).contains(QStringLiteral("make it the target")));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, view_point(document, {300.0, 260.0}));
        QApplication::processEvents();
        CHECK(label_text(dialog, "perspectiveTargetLabel").contains(QStringLiteral("plane 1 of 2")));
        send_mouse(view, QEvent::MouseMove, view_point(document, {700.0, 500.0}), Qt::NoButton, Qt::NoButton);
        QApplication::processEvents();
        save_widget_artifact("perspective-planes/after-target-plane", dialog);
        dialog.reject();
      },
      error, ran);
  require_action(window, "filterPerspectivePlanesAction")->trigger();
  QApplication::processEvents();
  if (error) {
    std::rethrow_exception(error);
  }
  CHECK(ran);
}

// Clone's Sample choice: on an empty layer over the wall, All Layers clones the wall's
// bricks onto the new layer, Current Layer clones nothing, and an image placed in the
// session is cloned even with Current Layer.
void ui_perspective_planes_clone_samples_the_layers_below() {
  const auto run = [](int sample_index, bool place_poster, bool capture) {
    patchy::ui::MainWindow window;
    patchy::Document document(800, 600, patchy::PixelFormat::rgba8());
    patchy::Layer wall(document.allocate_layer_id(), "Street", wall_scene());
    wall.set_bounds(patchy::Rect{0, 0, 800, 600});
    document.add_layer(std::move(wall));
    patchy::PixelBuffer empty(800, 600, patchy::PixelFormat::rgba8());
    empty.clear(0);
    patchy::Layer target(document.allocate_layer_id(), "Patch", std::move(empty));
    target.set_bounds(patchy::Rect{0, 0, 800, 600});
    const auto target_id = target.id();
    document.add_layer(std::move(target));
    document.set_active_layer(target_id);
    window.add_document_session(std::move(document), QStringLiteral("Perspective Clone Sample"));
    show_window(window);
    (void)require_canvas(window);
    if (place_poster) {
      const auto art = poster();
      QImage image(art.width(), art.height(), QImage::Format_RGBA8888);
      for (int row = 0; row < art.height(); ++row) {
        std::copy_n(art.pixel(0, row), static_cast<std::size_t>(art.width()) * 4, image.scanLine(row));
      }
      QApplication::clipboard()->setImage(image);
    }
    const auto layers_before = layer_count(window);
    std::exception_ptr error;
    bool ran = false;
    schedule_workspace_driver(
        [&](QDialog& dialog, QWidget& view) {
          const QSize document_size(800, 600);
          click_corners(view, document_size, kWall);
          if (place_poster) {
            auto* paste = dialog.findChild<QToolButton*>(QStringLiteral("perspectivePasteButton"));
            CHECK(paste != nullptr && paste->isEnabled());
            auto* source = paste->menu()->findChild<QAction*>(QStringLiteral("perspectivePasteSource0"));
            CHECK(source != nullptr);
            source->trigger();
            QTest::keyClick(&view, Qt::Key_Return);
            QApplication::processEvents();
          }
          QTest::mouseClick(tool(dialog, "perspectiveCloneTool"), Qt::LeftButton);
          auto* sample = dialog.findChild<QComboBox*>(QStringLiteral("perspectiveCloneSampleCombo"));
          CHECK(sample != nullptr && sample->isVisible());
          sample->setCurrentIndex(sample_index);
          QApplication::processEvents();
          // The poster sits at the wall's middle; the source is there, the copy goes near the wall's left end.
          const auto camera = patchy::perspective_camera_for({{1, kWall, 0.0, std::nullopt}}, 800, 600);
          const auto plane = patchy::plane_geometry(kWall, camera);
          CHECK(plane.has_value());
          const auto from = plane->to_image.map({plane->width * 0.5, plane->height * 0.5});
          const auto to = plane->to_image.map({plane->width * 0.12, plane->height * 0.5});
          CHECK(from.has_value() && to.has_value());
          QTest::mouseClick(&view, Qt::LeftButton, Qt::AltModifier, view_point(document_size, *from));
          drag(view, view_point(document_size, *to),
               view_point(document_size, {to->x, to->y + 40.0}));
          if (capture) {
            // A wider patch for the picture, then the other layers hidden: the cloned bricks sit
            // on the empty layer itself.
            const auto across = plane->to_image.map({plane->width * 0.3, plane->height * 0.62});
            CHECK(across.has_value());
            drag(view, view_point(document_size, {to->x, to->y + 40.0}), view_point(document_size, *across));

            auto* show_others = dialog.findChild<QCheckBox*>(QStringLiteral("perspectiveShowOtherLayersCheck"));
            CHECK(show_others != nullptr);
            show_others->setChecked(false);
            send_mouse(view, QEvent::MouseMove, view_point(document_size, {to->x + 20.0, to->y + 60.0}), Qt::NoButton,
                       Qt::NoButton);
            QApplication::processEvents();
            save_widget_artifact("perspective-planes/after-clone-sample", dialog);
          }
          dialog.accept();
        },
        error, ran);
    require_action(window, "filterPerspectivePlanesAction")->trigger();
    QApplication::processEvents();
    if (error) {
      std::rethrow_exception(error);
    }
    CHECK(ran);
    // The stroke's pixel, on whichever layer the result went to (the new layer when there is one).
    const auto& doc = std::as_const(MainWindowTestAccess::document(window));
    const auto camera = patchy::perspective_camera_for({{1, kWall, 0.0, std::nullopt}}, 800, 600);
    const auto plane = patchy::plane_geometry(kWall, camera);
    const auto to = plane->to_image.map({plane->width * 0.12, plane->height * 0.5});
    const int x = static_cast<int>(to->x);
    const int y = static_cast<int>(to->y) + 20;
    std::array<int, 4> color{0, 0, 0, 0};
    if (layer_count(window) > layers_before) {
      const auto& top = doc.layers().back();
      const auto bounds = top.bounds();
      if (x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width && y < bounds.y + bounds.height) {
        const auto* p = std::as_const(top).pixels().pixel(x - bounds.x, y - bounds.y);
        color = {p[0], p[1], p[2], p[3]};
      }
    }
    return color;
  };
  const auto bricks = run(2, false, true);
  CHECK(bricks[3] == 255 && bricks[0] > bricks[2]);
  CHECK(run(0, false, false)[3] == 0);
  const auto placed = run(0, true, false);
  CHECK(placed[3] == 255);
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
    var sticker = doc.addLayer("Sticker");
    sticker.fillRect(0, 0, 20, 20, "#0000ff");
    doc.activeLayer = sticker;
    var placed = patchy.perspective.placeLayer(sticker, {plane: plane});
    var sb = placed.getPixels();
    var sbytes = new Uint8Array(sb.data);
    var sat = function (x, y) {
      if (x < sb.x || y < sb.y || x >= sb.x + sb.width || y >= sb.y + sb.height) return 0;
      return sbytes[((y - sb.y) * sb.width + (x - sb.x)) * 4 + 3];
    };
    console.log("placeSame=" + (placed.id === sticker.id) + " filled=" + sat(120, 110) + " old=" + sat(10, 10));
    var patch = doc.addLayer("Patch");
    doc.activeLayer = patch;
    var none = patchy.perspective.clone(patch, {plane: plane, from: [60, 170], points: [[150, 150]], size: 16});
    var all = patchy.perspective.clone(patch, {plane: plane, from: [60, 170], points: [[150, 150]], size: 16,
                                               sample: "all"});
    var ab = all.getPixels();
    var abytes = new Uint8Array(ab.data);
    console.log("cloneCurrent=" + (none === null) + " cloneAll=" +
                abytes[((150 - ab.y) * ab.width + (150 - ab.x)) * 4 + 3]);
    var badSample = "none";
    try { patchy.perspective.clone(patch, {plane: plane, from: [60, 170], points: [[150, 150]], sample: "below"}); }
    catch (e) { badSample = "refused"; }
    console.log("badSample=" + badSample);
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
  CHECK(backlog_contains(window, QStringLiteral("placeSame=true filled=255 old=0")));
  CHECK(backlog_contains(window, QStringLiteral("cloneCurrent=true cloneAll=255")));
  CHECK(backlog_contains(window, QStringLiteral("badSample=refused")));
}

}  // namespace

std::vector<patchy::test::TestCase> perspective_planes_ui_tests() {
  return {
      {"ui_perspective_planes_cancel_leaves_the_document_untouched",
       ui_perspective_planes_cancel_leaves_the_document_untouched},
      {"ui_perspective_planes_ok_writes_one_undo_step", ui_perspective_planes_ok_writes_one_undo_step},
      {"ui_perspective_planes_shows_the_other_layers_behind", ui_perspective_planes_shows_the_other_layers_behind},
      {"ui_perspective_planes_place_layer_replaces_the_layer", ui_perspective_planes_place_layer_replaces_the_layer},
      {"ui_perspective_planes_needs_a_target_plane_first", ui_perspective_planes_needs_a_target_plane_first},
      {"ui_perspective_planes_clone_samples_the_layers_below", ui_perspective_planes_clone_samples_the_layers_below},
      {"ui_perspective_planes_explains_impossible_corners", ui_perspective_planes_explains_impossible_corners},
      {"ui_perspective_planes_workspace_captures", ui_perspective_planes_workspace_captures},
      {"ui_script_perspective_paste_paint_and_clone", ui_script_perspective_paste_paint_and_clone},
  };
}
