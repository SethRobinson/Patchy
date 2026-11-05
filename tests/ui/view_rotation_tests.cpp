// View rotation (docs/view-navigation-rotation.md): the CanvasViewTransform mapping, the
// canvas pivoting on the view center, navigation (Fit, scroll bars) on the rotated
// document's bounding box, rotated drawing, input, overlays and text editing.
#include "ui/canvas_view_transform.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "scripting_test_support.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QScrollBar>
#include <QTest>
#include <QTextEdit>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;

namespace {

bool near(double a, double b, double tolerance = 1e-6) {
  return std::abs(a - b) <= tolerance;
}

bool near(QPointF a, QPointF b, double tolerance = 1e-6) {
  return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}

void ui_view_transform_round_trips_at_any_angle() {
  using patchy::ui::CanvasViewTransform;
  const QSizeF document(300.0, 200.0);
  for (const double angle : {0.0, 37.0, 90.0, 180.0, -135.0, -90.0, 359.0}) {
    const CanvasViewTransform view(1.75, QPointF(12.5, -40.25), angle, document);
    for (const QPointF point : {QPointF(0.0, 0.0), QPointF(300.0, 200.0), QPointF(17.25, 133.5)}) {
      CHECK(near(view.map_to_document(view.map_to_widget(point)), point, 1e-9));
      CHECK(near(view.to_widget().map(point), view.map_to_widget(point), 1e-9));
    }
    // The bounding box starts at pan + bounds_offset and spans extent().
    const auto bounds = view.widget_bounds_of_document_rect(QRectF(QPointF(0.0, 0.0), document));
    CHECK(near(bounds.topLeft(), view.pan() + view.bounds_offset(), 1e-9));
    CHECK(near(bounds.width(), view.extent().width(), 1e-9));
    CHECK(near(bounds.height(), view.extent().height(), 1e-9));
  }
  // Angle 0 is the plain per-axis arithmetic, bit for bit.
  const CanvasViewTransform upright(1.3, QPointF(7.1, 9.3), 0.0, document);
  CHECK(upright.axis_aligned());
  CHECK(upright.map_to_widget(QPointF(5.5, 6.25)) == QPointF(7.1 + 5.5 * 1.3, 9.3 + 6.25 * 1.3));
  CHECK(upright.map_to_document(QPointF(40.0, 50.0)) == QPointF((40.0 - 7.1) / 1.3, (50.0 - 9.3) / 1.3));
  CHECK(upright.bounds_offset() == QPointF(0.0, 0.0));
  // Right angles are exact: whole pixels stay whole.
  const CanvasViewTransform quarter(2.0, QPointF(100.0, 10.0), 90.0, document);
  CHECK(quarter.map_to_widget(QPointF(3.0, 4.0)) == QPointF(100.0 - 8.0, 10.0 + 6.0));
  CHECK(near(quarter.extent().width(), 400.0) && near(quarter.extent().height(), 600.0));
  // Wrapping into (-180, 180].
  CHECK(CanvasViewTransform::normalized_angle(540.0) == 180.0);
  CHECK(CanvasViewTransform::normalized_angle(-180.0) == 180.0);
  CHECK(CanvasViewTransform::normalized_angle(-370.0) == -10.0);
  CHECK(CanvasViewTransform::normalized_angle(360.0) == 0.0);
}

void ui_view_rotation_pivots_on_the_view_center() {
  patchy::Document document(300, 200, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(300, 200, patchy::PixelFormat::rgba8(), QColor(90, 120, 200)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(1.0);

  const auto pivot = canvas.view_content_rect().center();
  const auto before = canvas.document_point_for_widget_position(pivot);
  canvas.set_view_rotation(30.0);
  CHECK(canvas.view_rotation() == 30.0);
  CHECK(!canvas.view_axis_aligned());
  CHECK(near(canvas.document_point_for_widget_position(pivot), before, 1e-6));
  // Funnel round trip under rotation.
  const QPointF document_point(123.25, 45.5);
  CHECK(near(canvas.document_point_for_widget_position(canvas.widget_position_f(document_point)), document_point,
             1e-9));
  // The document's x axis points 30 degrees clockwise on screen.
  const auto along_x = canvas.widget_position_f(QPointF(100.0, 0.0)) - canvas.widget_position_f(QPointF(0.0, 0.0));
  CHECK(near(std::atan2(along_x.y(), along_x.x()) * 180.0 / 3.14159265358979323846, 30.0, 1e-9));

  canvas.set_view_rotation(390.0);  // wraps to 30: no change
  CHECK(canvas.view_rotation() == 30.0);
  canvas.set_view_rotation(0.0);
  CHECK(canvas.view_axis_aligned());
  CHECK(near(canvas.document_point_for_widget_position(pivot), before, 1e-6));
}

void ui_view_rotation_fit_and_scroll_use_the_rotated_bounds() {
  patchy::Document document(400, 100, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(400, 100, patchy::PixelFormat::rgba8(), QColor(200, 60, 40)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 500);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  canvas.set_view_rotation(90.0);
  canvas.fit_to_view();
  // Turned a quarter, the wide strip stands upright: its 400 px side fits the height.
  const auto area = canvas.view_content_rect();
  const auto bounds = canvas.view_transform().widget_bounds_of_document_rect(QRectF(0.0, 0.0, 400.0, 100.0));
  CHECK(bounds.top() >= area.top() - 0.5);
  CHECK(bounds.bottom() <= area.bottom() + 0.5);
  CHECK(bounds.height() > bounds.width());
  CHECK(near(bounds.center(), area.center(), 0.5));

  canvas.set_view_rotation(45.0);
  canvas.fit_to_view();
  const auto diagonal = canvas.view_transform().widget_bounds_of_document_rect(QRectF(0.0, 0.0, 400.0, 100.0));
  CHECK(area.adjusted(-0.5, -0.5, 0.5, 0.5).contains(diagonal));

  // Zoomed in, the scroll bars span the rotated bounding box: moving a bar pans it.
  canvas.set_zoom_centered(4.0);
  const auto extent = canvas.view_transform().extent();
  QScrollBar* wide_bar = nullptr;
  for (auto* bar : canvas.findChildren<QScrollBar*>()) {
    if (bar->orientation() == Qt::Horizontal) {
      wide_bar = bar;
    }
  }
  CHECK(wide_bar != nullptr);
  if (wide_bar != nullptr) {
    CHECK(wide_bar->maximum() > 0);
    CHECK(wide_bar->maximum() + wide_bar->pageStep() >= static_cast<int>(extent.width() * 0.8));
    const auto before = canvas.widget_position_f(QPointF(200.0, 50.0));
    wide_bar->setValue(wide_bar->value() + 50);
    const auto after = canvas.widget_position_f(QPointF(200.0, 50.0));
    CHECK(near(after.x() - before.x(), -50.0, 1.0));
    CHECK(near(after.y(), before.y(), 1e-6));
  }
}

// A document whose every pixel has its own color, so a misplaced pixel shows.
patchy::PixelBuffer gradient_pixels(int width, int height) {
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(Qt::black));
  const auto data = pixels.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* pixel =
          data.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
      pixel[0] = static_cast<std::uint8_t>(20 + x * 9);
      pixel[1] = static_cast<std::uint8_t>(20 + y * 13);
      pixel[2] = static_cast<std::uint8_t>(((x + y) % 2) * 200 + 30);
      pixel[3] = 255;
    }
  }
  return pixels;
}

// Every document pixel shows, at its mapped center, in its own color: the rotated draw
// places the image exactly where the view transform says, and the upright view still does.
void ui_view_rotation_renders_each_pixel_under_its_mapped_center() {
  constexpr int kWidth = 20;
  constexpr int kHeight = 14;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", gradient_pixels(kWidth, kHeight));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 420);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(8.0);

  for (const double angle : {0.0, 90.0, 180.0, 30.0, -63.0}) {
    canvas.set_view_rotation(angle);
    canvas.center_document_in_view();
    const auto frame = canvas.grab().toImage();
    const auto ratio = frame.devicePixelRatio();
    const auto area = canvas.view_content_rect().adjusted(2.0, 2.0, -2.0, -2.0);
    int checked = 0;
    int wrong = 0;
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        const auto center = canvas.widget_position_f(QPointF(x + 0.5, y + 0.5));
        if (!area.contains(center)) {
          continue;
        }
        ++checked;
        const auto shown = frame.pixelColor(QPoint(static_cast<int>(center.x() * ratio),
                                                   static_cast<int>(center.y() * ratio)));
        const QColor expected(20 + x * 9, 20 + y * 13, ((x + y) % 2) * 200 + 30);
        if (!color_close(shown, expected, 3)) {
          ++wrong;
        }
      }
    }
    CHECK(checked > kWidth * kHeight / 2);
    CHECK(wrong == 0);
  }
}

// A brush stroke under a rotated view paints where the pointer is: the press lands on
// the document point under it, and a drag along a document row stays on that row.
void ui_view_rotation_brush_paints_under_the_pointer() {
  patchy::Document document(120, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 360);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(5);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(2.0);
  canvas.set_view_rotation(30.0);

  const auto widget_point = [&canvas](QPoint document_point) {
    return canvas.widget_position_f(QPointF(document_point) + QPointF(0.5, 0.5)).toPoint();
  };
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point(QPoint(20, 30)), Qt::LeftButton, Qt::LeftButton);
  for (int x = 24; x <= 80; x += 4) {
    send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(x, 30)), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(QPoint(80, 30)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas_pixel(canvas, QPoint(20, 30)).red() < 80);
  CHECK(canvas_pixel(canvas, QPoint(50, 30)).red() < 80);
  CHECK(canvas_pixel(canvas, QPoint(80, 30)).red() < 80);
  // Off the row stays white: the stroke followed the document row, not a screen line.
  CHECK(color_close(canvas_pixel(canvas, QPoint(50, 40)), QColor(Qt::white), 2));
  CHECK(color_close(canvas_pixel(canvas, QPoint(80, 20)), QColor(Qt::white), 2));
}

// A Rectangular Marquee dragged under a rotated view selects a rectangle on the
// document's axes, from the press point to the release point (as in other editors).
void ui_view_rotation_marquee_selects_a_document_rectangle() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 360);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(2.0);
  canvas.set_view_rotation(-40.0);

  const auto widget_point = [&canvas](QPoint document_point) {
    return canvas.widget_position_f(QPointF(document_point)).toPoint();
  };
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point(QPoint(20, 15)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(50, 40)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(90, 70)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(QPoint(90, 70)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas.has_selection());
  const auto bounds = canvas.selected_document_region().boundingRect();
  CHECK(std::abs(bounds.left() - 20) <= 1 && std::abs(bounds.top() - 15) <= 1);
  CHECK(std::abs(bounds.right() + 1 - 90) <= 1 && std::abs(bounds.bottom() + 1 - 70) <= 1);
  // A full rectangle: the corners of the bounds are selected too.
  CHECK(canvas.selection_contains(QPoint(22, 68)));
  CHECK(canvas.selection_contains(QPoint(88, 17)));
}

// Free Transform under a quarter turn: the box's handles sit on the turned corners, the
// resize cursor points along the corner's on-screen direction, and dragging a corner
// resizes the box in document space.
void ui_view_rotation_free_transform_handles_follow_the_view() {
  patchy::Document document(160, 110, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", gradient_pixels(160, 110));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(480, 480);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(1.5);
  canvas.set_view_rotation(90.0);
  CHECK(canvas.begin_free_transform());
  QApplication::processEvents();

  const auto corner = canvas.widget_position_f(QPointF(160.0, 110.0)).toPoint();
  send_mouse(canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
  // The bottom-right corner points down-right on an upright box; turned a quarter
  // clockwise it points down-left.
  CHECK(canvas.cursor().shape() == Qt::SizeBDiagCursor);
  const auto middle = canvas.widget_position_f(QPointF(80.0, 55.0)).toPoint();
  send_mouse(canvas, QEvent::MouseMove, middle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas.cursor().shape() == Qt::SizeAllCursor);

  const auto target = canvas.widget_position_f(QPointF(190.0, 130.0)).toPoint();
  send_mouse(canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, (corner + target) / 2, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto quad = canvas.free_transform_quad();
  CHECK(near(quad[0], QPointF(0.0, 0.0), 1.0));
  CHECK(near(quad[2], QPointF(190.0, 130.0), 1.5));
  save_widget_artifact("view_rotation_free_transform", canvas);
  canvas.cancel_free_transform();
}

// Every overlay family draws under a rotated view without leaving the frame: rulers,
// grid, guides, the selection's ants and the crop box. The capture is the visual check.
void ui_view_rotation_overlays_draw_under_rotation() {
  patchy::Document document(160, 110, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", gradient_pixels(160, 110));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 440);
  canvas.set_document(&document);
  canvas.set_rulers_visible(true);
  canvas.set_grid_visible(true);
  canvas.add_guide(patchy::GuideOrientation::Vertical, 40 * 32);
  canvas.add_guide(patchy::GuideOrientation::Horizontal, 30 * 32);
  canvas.set_tool(patchy::ui::CanvasTool::Marquee);
  canvas.show();
  QApplication::processEvents();
  canvas.set_zoom_centered(2.0);
  canvas.set_view_rotation(25.0);

  const auto widget_point = [&canvas](QPoint document_point) {
    return canvas.widget_position_f(QPointF(document_point)).toPoint();
  };
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point(QPoint(30, 20)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(100, 80)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(QPoint(100, 80)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas.has_selection());
  const auto overlays = canvas.grab().toImage();
  CHECK(!overlays.isNull());
  save_widget_artifact("view_rotation_overlays", canvas);

  canvas.set_tool(patchy::ui::CanvasTool::Crop);
  QApplication::processEvents();
  if (canvas.crop_session_active()) {
    save_widget_artifact("view_rotation_crop", canvas);
    canvas.cancel_crop_session();
  }
  // Back upright, the same document draws exactly as before the turn.
  canvas.set_view_rotation(0.0);
  CHECK(canvas.view_axis_aligned());
}


// Typing under a rotated view edits through the transformed overlay, which turns with the
// canvas: the plain inline editor is an upright widget and could not.
void ui_view_rotation_text_editing_turns_with_the_view() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) {
    return;
  }
  canvas->set_view_rotation(30.0);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto click = canvas->widget_position_f(QPointF(120.0, 120.0)).toPoint();
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  QTest::keyClicks(editor, QStringLiteral("Turned"));
  process_events_for(200);
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
  CHECK(overlay != nullptr && overlay->isVisible());
  if (overlay == nullptr) {
    return;
  }
  // The editor outline's top edge runs along the document's x axis: 30 degrees on screen.
  const auto polygon = overlay->property("patchy.transformedTextEditorPolygon").toList();
  CHECK(polygon.size() == 4);
  if (polygon.size() == 4) {
    const auto edge = polygon[1].toPointF() - polygon[0].toPointF();
    CHECK(near(std::atan2(edge.y(), edge.x()) * 180.0 / 3.14159265358979323846, 30.0, 0.5));
  }
  save_widget_artifact("view_rotation_text", *canvas);
}


// The Rotate View tool (R): the palette groups it with the Hand, a drag turns the view
// about the view center, Shift snaps to 15 degrees, Escape turns it upright, and the View
// menu steps 15 degrees. The options bar's angle follows the view.
void ui_view_rotation_tool_and_commands() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) {
    return;
  }
  canvas->setFocus();
  QTest::keyClick(canvas, Qt::Key_R);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::RotateView);
  CHECK(require_action(window, "toolRotateViewAction")->isChecked());

  const auto pivot = canvas->view_content_rect().center();
  const auto start = (pivot + QPointF(120.0, 0.0)).toPoint();
  // A quarter turn clockwise around the pivot, held to 15-degree steps by Shift.
  const auto end = (pivot + QPointF(3.0, 120.0)).toPoint();
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, (pivot + QPointF(85.0, 85.0)).toPoint(), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
  CHECK(canvas->rotating_view());
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(!canvas->rotating_view());
  CHECK(near(canvas->view_rotation(), 90.0, 1e-9));
  auto* angle_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("rotateViewAngleSpin"));
  CHECK(angle_spin != nullptr && near(angle_spin->value(), 90.0, 0.05));

  require_action(window, "viewRotateLeftAction")->trigger();
  CHECK(near(canvas->view_rotation(), 75.0, 1e-9));
  require_action(window, "viewRotateRightAction")->trigger();
  require_action(window, "viewRotateRightAction")->trigger();
  CHECK(near(canvas->view_rotation(), 105.0, 1e-9));
  if (angle_spin != nullptr) {
    angle_spin->setValue(-30.0);
    CHECK(near(canvas->view_rotation(), -30.0, 1e-9));
  }
  QApplication::processEvents();
  save_widget_artifact("view_rotation_window", window);

  QTest::keyClick(canvas, Qt::Key_Escape);
  CHECK(canvas->view_rotation() == 0.0);
  // An unused Escape turns the view upright with any tool; a Free Transform's Escape
  // cancels the transform first and leaves the view turned.
  require_action(window, "toolBrushAction")->trigger();
  canvas->set_view_rotation(30.0);
  QTest::keyClick(canvas, Qt::Key_Escape);
  CHECK(canvas->view_rotation() == 0.0);
  require_action(window, "toolMoveAction")->trigger();
  canvas->set_view_rotation(30.0);
  if (canvas->begin_free_transform()) {
    QTest::keyClick(canvas, Qt::Key_Escape);
    CHECK(!canvas->free_transform_active());
    CHECK(canvas->view_rotation() == 30.0);
  }
  QTest::keyClick(canvas, Qt::Key_Escape);
  CHECK(canvas->view_rotation() == 0.0);
  canvas->set_view_rotation(45.0);
  require_action(window, "viewRotateResetAction")->trigger();
  CHECK(canvas->view_rotation() == 0.0);
  // Smudge gave R to Rotate View; the detail group's Shift+R still reaches it.
  CHECK(require_action(window, "toolSmudgeAction")->shortcut().isEmpty());
}

void ui_script_ui_view_rotation() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    console.log('start=' + patchy.ui.rotation);
    patchy.ui.rotation = 30;
    console.log('thirty=' + patchy.ui.rotation);
    patchy.ui.rotation = 370;
    console.log('wrapped=' + patchy.ui.rotation);
    patchy.ui.rotation = -180;
    console.log('half=' + patchy.ui.rotation);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("start=0")));
  CHECK(backlog_contains(window, QStringLiteral("thirty=30")));
  CHECK(backlog_contains(window, QStringLiteral("wrapped=10")));
  CHECK(backlog_contains(window, QStringLiteral("half=180")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.rotation = NaN;")));
  CHECK(run_script(window, QStringLiteral("console.log('kept=' + patchy.ui.rotation);")));
  CHECK(backlog_contains(window, QStringLiteral("kept=180")));
}

}  // namespace

std::vector<patchy::test::TestCase> view_rotation_tests() {
  return {
      {"ui_view_transform_round_trips_at_any_angle", ui_view_transform_round_trips_at_any_angle},
      {"ui_view_rotation_pivots_on_the_view_center", ui_view_rotation_pivots_on_the_view_center},
      {"ui_view_rotation_fit_and_scroll_use_the_rotated_bounds",
       ui_view_rotation_fit_and_scroll_use_the_rotated_bounds},
      {"ui_view_rotation_renders_each_pixel_under_its_mapped_center",
       ui_view_rotation_renders_each_pixel_under_its_mapped_center},
      {"ui_view_rotation_brush_paints_under_the_pointer", ui_view_rotation_brush_paints_under_the_pointer},
      {"ui_view_rotation_marquee_selects_a_document_rectangle",
       ui_view_rotation_marquee_selects_a_document_rectangle},
      {"ui_view_rotation_free_transform_handles_follow_the_view",
       ui_view_rotation_free_transform_handles_follow_the_view},
      {"ui_view_rotation_overlays_draw_under_rotation", ui_view_rotation_overlays_draw_under_rotation},
      {"ui_view_rotation_text_editing_turns_with_the_view", ui_view_rotation_text_editing_turns_with_the_view},
      {"ui_view_rotation_tool_and_commands", ui_view_rotation_tool_and_commands},
      {"ui_script_ui_view_rotation", ui_script_ui_view_rotation},
  };
}
