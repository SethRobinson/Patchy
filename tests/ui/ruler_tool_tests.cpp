// Ruler tool (GitHub issue 331, docs/pick-tool.md): the Pick flyout and its Shift+I
// cycle, drawing and adjusting the measuring line, the options-bar readout in the ruler
// unit, Clear, the protractor, and Straighten Layer / Straighten Image with their undo.
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/ruler_tool.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

using patchy::ui::CanvasTool;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::MeasurementUnit;
using patchy::ui::RulerLine;

QPoint widget_point(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  return canvas.widget_position_for_document_point(document_point);
}

QString readout(patchy::ui::MainWindow& window, const char* name) {
  const auto* label = window.findChild<QLabel*>(QString::fromLatin1(name));
  CHECK(label != nullptr);
  return label != nullptr ? label->text() : QString();
}

QPushButton* require_button(patchy::ui::MainWindow& window, const char* name) {
  auto* button = window.findChild<QPushButton*>(QString::fromLatin1(name));
  CHECK(button != nullptr);
  return button;
}

bool line_is(const std::optional<RulerLine>& line, QPointF start, QPointF end) {
  return line.has_value() && line->start == start && line->end == end;
}

// A 400 x 300 document whose active layer "Bar" holds an opaque bar 8 px thick and
// 240 px long through the canvas center, tilted `degrees` counterclockwise.
patchy::LayerId add_tilted_bar(patchy::ui::MainWindow& window, double degrees) {
  MainWindowTestAccess::create_document(window, 400, 300);
  QApplication::processEvents();
  auto& document = MainWindowTestAccess::document(window);
  auto pixels = solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  const double radians = degrees * std::numbers::pi / 180.0;
  const QPointF along(std::cos(radians), -std::sin(radians));
  const QPointF center(200.0, 150.0);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const QPointF offset = QPointF(x + 0.5, y + 0.5) - center;
      const double t = offset.x() * along.x() + offset.y() * along.y();
      const double d = std::abs(offset.x() * along.y() - offset.y() * along.x());
      if (std::abs(t) <= 120.0 && d <= 4.0) {
        auto* pixel = pixels.pixel(x, y);
        pixel[0] = 200;
        pixel[1] = 30;
        pixel[2] = 30;
        pixel[3] = 255;
      }
    }
  }
  const auto id = document.add_pixel_layer("Bar", std::move(pixels)).id();
  document.set_active_layer(id);
  MainWindowTestAccess::refresh_layer_ui(window);
  require_canvas(window)->document_changed();
  QApplication::processEvents();
  return id;
}

// The mean row of the opaque pixels in the leftmost and rightmost 20 opaque columns of a
// layer, in document space.
std::pair<double, double> bar_end_rows(const patchy::Document& document, patchy::LayerId id) {
  const auto* layer = document.find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return {0.0, 0.0};
  }
  const auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  std::int32_t first = pixels.width();
  std::int32_t last = -1;
  for (std::int32_t x = 0; x < pixels.width(); ++x) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      if (pixels.pixel(x, y)[3] > 128) {
        first = std::min(first, x);
        last = std::max(last, x);
      }
    }
  }
  const auto mean_row = [&](std::int32_t from, std::int32_t to) {
    double sum = 0.0;
    double count = 0.0;
    for (std::int32_t x = std::max(from, 0); x <= std::min(to, pixels.width() - 1); ++x) {
      for (std::int32_t y = 0; y < pixels.height(); ++y) {
        if (pixels.pixel(x, y)[3] > 128) {
          sum += y;
          count += 1.0;
        }
      }
    }
    return count > 0.0 ? bounds.y + sum / count : 0.0;
  };
  return {mean_row(first, first + 19), mean_row(last - 19, last)};
}

// The geometry helpers: angle sign, protractor angle, Shift steps, the straighten turn
// to the nearest axis and the Straighten Image crop box.
void ui_ruler_measurement_math() {
  const auto up_right = patchy::ui::measure_ruler(RulerLine{QPointF(100, 200), QPointF(200, 100), std::nullopt});
  CHECK(up_right.x == 100.0 && up_right.y == 200.0);
  CHECK(up_right.width == 100.0 && up_right.height == -100.0);
  CHECK(std::abs(up_right.angle_degrees - 45.0) < 1e-9);
  CHECK(std::abs(up_right.length1 - std::sqrt(2.0) * 100.0) < 1e-9);
  CHECK(!up_right.length2.has_value());
  const auto down_right = patchy::ui::measure_ruler(RulerLine{QPointF(0, 0), QPointF(10, 10), std::nullopt});
  CHECK(std::abs(down_right.angle_degrees + 45.0) < 1e-9);
  const auto left = patchy::ui::measure_ruler(RulerLine{QPointF(10, 0), QPointF(0, 0), std::nullopt});
  CHECK(left.angle_degrees == 180.0);
  const auto protractor =
      patchy::ui::measure_ruler(RulerLine{QPointF(0, 0), QPointF(100, 0), QPointF(0, -50)});
  CHECK(std::abs(protractor.angle_degrees - 90.0) < 1e-9);
  CHECK(protractor.length2.has_value() && *protractor.length2 == 50.0);

  CHECK(patchy::ui::constrain_ruler_point(QPointF(50, 50), QPointF(150, 60)) == QPointF(150, 50));
  CHECK(patchy::ui::constrain_ruler_point(QPointF(50, 50), QPointF(58, 140)) == QPointF(50, 140));
  CHECK(patchy::ui::constrain_ruler_point(QPointF(50, 50), QPointF(90, 10)) == QPointF(90, 10));
  CHECK(patchy::ui::constrain_ruler_point(QPointF(50, 50), QPointF(88, 14)) == QPointF(87, 13));

  // A line rising 10 degrees turns 10 clockwise; one leaning 80 degrees stands upright.
  CHECK(std::abs(patchy::ui::ruler_straighten_degrees(
                     RulerLine{QPointF(0, 0), QPointF(std::cos(0.1745329), -std::sin(0.1745329)) * 100.0,
                               std::nullopt}) +
                 10.0) < 0.01);
  CHECK(std::abs(patchy::ui::ruler_straighten_degrees(
                     RulerLine{QPointF(0, 0), QPointF(std::cos(1.3962634), -std::sin(1.3962634)) * 100.0,
                               std::nullopt}) -
                 10.0) < 0.01);
  CHECK(patchy::ui::ruler_straighten_degrees(RulerLine{QPointF(0, 0), QPointF(50, 0), std::nullopt}) == 0.0);

  const auto none = patchy::ui::ruler_straighten_crop(400, 300, 0.0);
  CHECK(none.x == 0 && none.y == 0 && none.width == 400 && none.height == 300);
  const auto crop = patchy::ui::ruler_straighten_crop(400, 300, 10.0);
  const double radians = 10.0 * std::numbers::pi / 180.0;
  CHECK(crop.width * std::cos(radians) + crop.height * std::sin(radians) <= 400.0 + 1e-6);
  CHECK(crop.width * std::sin(radians) + crop.height * std::cos(radians) <= 300.0 + 1e-6);
  CHECK(std::abs(static_cast<double>(crop.width) / crop.height - 4.0 / 3.0) < 0.02);
  CHECK(crop.width > 300 && crop.x == (400 - crop.width) / 2);
}

// Ruler is the second member of the Pick flyout with its own permanent command id and no
// default key; Shift+I cycles the two and I stays on Pick.
void ui_ruler_tool_joins_pick_flyout() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* pick = require_action(window, "toolPickAction");
  auto* ruler = require_action(window, "toolRulerAction");
  CHECK(ruler->text() == QStringLiteral("Ruler"));
  CHECK(!ruler->icon().isNull());
  CHECK(ruler->shortcut().isEmpty());
  CHECK(pick->shortcut() == QKeySequence(Qt::Key_I));
  auto* button = window.findChild<QToolButton*>(QStringLiteral("pickToolButton"));
  CHECK(button != nullptr);
  if (button == nullptr) {
    return;
  }
  CHECK(button->menu() != nullptr);
  CHECK(button->menu() != nullptr &&
        button->menu()->actions().indexOf(ruler) == button->menu()->actions().indexOf(pick) + 1);
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("tools.ruler"));
  CHECK(command != nullptr && command->action == ruler);
  CHECK(command != nullptr && command->category == QStringLiteral("tools"));
  CHECK(window.hotkey_registry().find_command(QStringLiteral("tools.cycle.pick")) != nullptr);

  pick->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_I, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Ruler);
  CHECK(button->defaultAction() == ruler);
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Ruler: drag to measure")));
  QTest::keyClick(canvas, Qt::Key_I, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Eyedropper);
  QTest::keyClick(canvas, Qt::Key_I, Qt::ShiftModifier);
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_I);
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Eyedropper);
}

// A drag draws the line and the bar reads it out (angle counterclockwise positive, H
// down positive); Shift keeps 45-degree steps; an end and the body adjust it; a press
// away starts a new line and a click clears it. The readout follows the ruler unit, the
// line is no undo step and survives a tool switch, and Clear removes it.
void ui_ruler_draws_line_and_reads_out() {
  patchy::ui::MainWindow window;
  show_window(window);
  SettingsValueRestorer saved_units(QStringLiteral("view/rulerUnits"));
  MainWindowTestAccess::create_document(window, 400, 300);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_snap_enabled(false);
  canvas->set_view_zoom_centered(1.0);  // presses land on the pixel they name
  auto* undo = require_hotkey_action(window, QStringLiteral("edit.undo"));
  const bool undo_enabled_before = undo->isEnabled();
  require_action(window, "toolRulerAction")->trigger();
  QApplication::processEvents();
  auto* clear = require_button(window, "rulerClearButton");
  auto* straighten_layer = require_button(window, "rulerStraightenLayerButton");
  auto* straighten_image = require_button(window, "rulerStraightenImageButton");
  if (clear == nullptr || straighten_layer == nullptr || straighten_image == nullptr) {
    return;
  }
  CHECK(!canvas->ruler_line().has_value());
  CHECK(!clear->isEnabled() && !straighten_image->isEnabled());
  CHECK(readout(window, "rulerReadoutX").isEmpty());

  drag(*canvas, widget_point(*canvas, QPoint(100, 200)), widget_point(*canvas, QPoint(200, 100)));
  QApplication::processEvents();
  CHECK(line_is(canvas->ruler_line(), QPointF(100, 200), QPointF(200, 100)));
  CHECK(readout(window, "rulerReadoutX") == QStringLiteral("100 px"));
  CHECK(readout(window, "rulerReadoutY") == QStringLiteral("200 px"));
  CHECK(readout(window, "rulerReadoutW") == QStringLiteral("100 px"));
  CHECK(readout(window, "rulerReadoutH") == QStringLiteral("-100 px"));
  CHECK(readout(window, "rulerReadoutA") == QStringLiteral("45.0°"));
  CHECK(readout(window, "rulerReadoutL1") == QStringLiteral("141.4 px"));
  CHECK(readout(window, "rulerReadoutL2").isEmpty());
  CHECK(clear->isEnabled() && straighten_image->isEnabled());
  CHECK(undo->isEnabled() == undo_enabled_before);  // a view overlay, never an undo step

  // Down and to the right reads as a negative angle.
  drag(*canvas, widget_point(*canvas, QPoint(50, 50)), widget_point(*canvas, QPoint(80, 80)));
  QApplication::processEvents();
  CHECK(line_is(canvas->ruler_line(), QPointF(50, 50), QPointF(80, 80)));
  CHECK(readout(window, "rulerReadoutA") == QStringLiteral("-45.0°"));

  // Shift keeps the new line horizontal.
  drag(*canvas, widget_point(*canvas, QPoint(50, 150)), widget_point(*canvas, QPoint(250, 160)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(line_is(canvas->ruler_line(), QPointF(50, 150), QPointF(250, 150)));
  CHECK(readout(window, "rulerReadoutA") == QStringLiteral("0.0°"));
  CHECK(readout(window, "rulerReadoutL1") == QStringLiteral("200.0 px"));

  // The end moves alone, then the body moves both ends.
  drag(*canvas, widget_point(*canvas, QPoint(250, 150)), widget_point(*canvas, QPoint(250, 50)));
  QApplication::processEvents();
  CHECK(line_is(canvas->ruler_line(), QPointF(50, 150), QPointF(250, 50)));
  drag(*canvas, widget_point(*canvas, QPoint(150, 100)), widget_point(*canvas, QPoint(160, 120)));
  QApplication::processEvents();
  CHECK(line_is(canvas->ruler_line(), QPointF(60, 170), QPointF(260, 70)));

  // Inches at 100 PPI.
  auto& document = MainWindowTestAccess::document(window);
  document.print_settings().horizontal_ppi = 100.0;
  document.print_settings().vertical_ppi = 100.0;
  MainWindowTestAccess::set_ruler_unit_preference(window, MeasurementUnit::Inches);
  QApplication::processEvents();
  CHECK(readout(window, "rulerReadoutX") == QStringLiteral("0.600 in"));
  CHECK(readout(window, "rulerReadoutW") == QStringLiteral("2.000 in"));
  CHECK(readout(window, "rulerReadoutH") == QStringLiteral("-1.000 in"));
  CHECK(readout(window, "rulerReadoutL1") == QStringLiteral("2.236 in"));
  MainWindowTestAccess::set_ruler_unit_preference(window, MeasurementUnit::Pixels);
  QApplication::processEvents();

  // Alt+drag from the start adds the protractor's second leg there.
  drag(*canvas, widget_point(*canvas, QPoint(60, 170)), widget_point(*canvas, QPoint(60, 70)), Qt::AltModifier);
  QApplication::processEvents();
  auto line = canvas->ruler_line();
  CHECK(line.has_value() && line->second.has_value() && *line->second == QPointF(60, 70));
  CHECK(readout(window, "rulerReadoutL2") == QStringLiteral("100.0 px"));
  CHECK(readout(window, "rulerReadoutA") == QStringLiteral("63.4°"));

  // Another tool hides the line but keeps it; coming back shows it again.
  require_action(window, "toolPickAction")->trigger();
  QApplication::processEvents();
  require_action(window, "toolRulerAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->ruler_line().has_value());
  CHECK(readout(window, "rulerReadoutL2") == QStringLiteral("100.0 px"));

  // A press away from the line starts a new one; a plain click clears it.
  drag(*canvas, widget_point(*canvas, QPoint(300, 250)), widget_point(*canvas, QPoint(340, 250)));
  QApplication::processEvents();
  CHECK(line_is(canvas->ruler_line(), QPointF(300, 250), QPointF(340, 250)));
  CHECK(canvas->ruler_line().has_value() && !canvas->ruler_line()->second.has_value());
  const auto away = widget_point(*canvas, QPoint(100, 30));
  send_mouse(*canvas, QEvent::MouseButtonPress, away, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, away, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->ruler_line().has_value());

  canvas->set_ruler_line(RulerLine{QPointF(10, 10), QPointF(20, 20), std::nullopt});
  QApplication::processEvents();
  CHECK(clear->isEnabled());
  clear->click();
  QApplication::processEvents();
  CHECK(!canvas->ruler_line().has_value());
  CHECK(readout(window, "rulerReadoutL1").isEmpty());
  CHECK(!clear->isEnabled());
  CHECK(undo->isEnabled() == undo_enabled_before);
}

// Straighten Layer turns the active layer so the measured bar is level, in one undo step;
// it is off without a line and for a position-locked layer.
void ui_ruler_straighten_layer_levels_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto bar = add_tilted_bar(window, 10.0);
  auto* canvas = require_canvas(window);
  require_action(window, "toolRulerAction")->trigger();
  QApplication::processEvents();
  auto* straighten = require_button(window, "rulerStraightenLayerButton");
  if (straighten == nullptr) {
    return;
  }
  CHECK(!straighten->isEnabled());
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto before_rows = bar_end_rows(document, bar);
  CHECK(before_rows.first - before_rows.second > 30.0);  // tilted: the right end is higher
  const auto before_bounds = document.find_layer(bar)->bounds();

  const double radians = 10.0 * std::numbers::pi / 180.0;
  canvas->set_ruler_line(RulerLine{QPointF(200, 150) - QPointF(std::cos(radians), -std::sin(radians)) * 100.0,
                                   QPointF(200, 150) + QPointF(std::cos(radians), -std::sin(radians)) * 100.0,
                                   std::nullopt});
  QApplication::processEvents();
  CHECK(straighten->isEnabled());
  CHECK(readout(window, "rulerReadoutA").startsWith(QStringLiteral("9.8")));  // whole-pixel ends

  // A position lock turns the button off.
  auto& editable = MainWindowTestAccess::document(window);
  editable.find_layer(bar)->set_lock_flags(patchy::kLayerLockPosition);
  MainWindowTestAccess::refresh_layer_ui(window);
  MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  CHECK(!straighten->isEnabled());
  editable.find_layer(bar)->set_lock_flags(patchy::kLayerLockNone);
  MainWindowTestAccess::refresh_layer_ui(window);
  MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  CHECK(straighten->isEnabled());

  straighten->click();
  QApplication::processEvents();
  const auto& straightened = std::as_const(MainWindowTestAccess::document(window));
  const auto after_rows = bar_end_rows(straightened, bar);
  CHECK(std::abs(after_rows.first - after_rows.second) <= 1.5);
  CHECK(!canvas->ruler_line().has_value());
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Straightened layer 9.8")));
  CHECK(window.statusBar()->currentMessage().endsWith(QStringLiteral(" degrees clockwise")));
  CHECK(straightened.width() == 400 && straightened.height() == 300);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto& restored = std::as_const(MainWindowTestAccess::document(window));
  const auto restored_rows = bar_end_rows(restored, bar);
  CHECK(std::abs(restored_rows.first - before_rows.first) < 0.01);
  CHECK(std::abs(restored_rows.second - before_rows.second) < 0.01);
  const auto restored_bounds = restored.find_layer(bar)->bounds();
  CHECK(restored_bounds.x == before_bounds.x && restored_bounds.y == before_bounds.y &&
        restored_bounds.width == before_bounds.width && restored_bounds.height == before_bounds.height);
}

// Straighten Image turns the whole document so the bar is level and crops it to the
// largest box of the canvas's shape; one undo puts the canvas back. Also writes the
// screenshot of the tool with a line and its readout.
void ui_ruler_straighten_image_rotates_and_crops() {
  patchy::ui::MainWindow window;
  window.resize(1200, 760);
  show_window(window);
  const auto bar = add_tilted_bar(window, -8.0);
  auto* canvas = require_canvas(window);
  require_action(window, "toolRulerAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_point(*canvas, QPoint(90, 135)), widget_point(*canvas, QPoint(310, 166)));
  QApplication::processEvents();
  // Fine placement for an exact angle: the drag lands on whole pixels near the bar.
  const double radians = -8.0 * std::numbers::pi / 180.0;
  const QPointF along(std::cos(radians), -std::sin(radians));
  canvas->set_ruler_line(RulerLine{QPointF(200, 150) - along * 110.0, QPointF(200, 150) + along * 110.0, std::nullopt});
  QApplication::processEvents();
  save_widget_artifact("ui_ruler_tool_line_and_readout", window);
  auto* straighten = require_button(window, "rulerStraightenImageButton");
  if (straighten == nullptr) {
    return;
  }
  const auto expected = patchy::ui::ruler_straighten_crop(
      400, 300, patchy::ui::ruler_straighten_degrees(*canvas->ruler_line()));
  straighten->click();
  QApplication::processEvents();
  const auto& straightened = std::as_const(MainWindowTestAccess::document(window));
  CHECK(straightened.width() == expected.width && straightened.height() == expected.height);
  CHECK(straightened.width() < 400 && straightened.height() < 300);
  const auto rows = bar_end_rows(straightened, bar);
  CHECK(std::abs(rows.first - rows.second) <= 1.5);
  CHECK(!canvas->ruler_line().has_value());
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Straightened image ")));
  CHECK(window.statusBar()->currentMessage().endsWith(QStringLiteral(" degrees counterclockwise")));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto& restored = std::as_const(MainWindowTestAccess::document(window));
  CHECK(restored.width() == 400 && restored.height() == 300);
}

}  // namespace

std::vector<patchy::test::TestCase> ruler_tool_tests() {
  return {
      {"ui_ruler_measurement_math", ui_ruler_measurement_math},
      {"ui_ruler_tool_joins_pick_flyout", ui_ruler_tool_joins_pick_flyout},
      {"ui_ruler_draws_line_and_reads_out", ui_ruler_draws_line_and_reads_out},
      {"ui_ruler_straighten_layer_levels_and_undoes", ui_ruler_straighten_layer_levels_and_undoes},
      {"ui_ruler_straighten_image_rotates_and_crops", ui_ruler_straighten_image_rotates_and_crops},
  };
}
