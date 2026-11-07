// The Gradient tool's handles on an existing Gradient Fill (GitHub issue 663, docs/gradients.md,
// "Gradient Fill handles"): where the line, its ends, the stops and the midpoints are drawn,
// and each gesture (end, line, stop, midpoint, add, delete, color, Esc) as one undo step,
// while a drag away from them still draws a new gradient.
#include "core/blend_math.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_panel.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_palette.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QLineF>

#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

using patchy::ui::CanvasTool;
using patchy::ui::CanvasWidget;
using patchy::ui::MainWindowTestAccess;
using Kind = CanvasWidget::GradientHandlePartKind;
using Part = CanvasWidget::GradientHandlePart;

QPoint rounded(QPointF point) {
  return {static_cast<int>(std::lround(point.x())), static_cast<int>(std::lround(point.y()))};
}

QPoint widget_at(CanvasWidget& canvas, QPointF document_point) {
  return rounded(canvas.widget_position_f(document_point));
}

bool near(QPointF actual, QPointF expected, double tolerance) {
  const bool close = QLineF(actual, expected).length() <= tolerance;
  if (!close) {
    std::fprintf(stderr, "  (%.2f, %.2f) is not within %.2f of (%.2f, %.2f)\n", actual.x(), actual.y(), tolerance,
                 expected.x(), expected.y());
  }
  return close;
}

bool color_near(QColor actual, QColor expected, int tolerance) {
  const bool close = std::abs(actual.red() - expected.red()) <= tolerance &&
                     std::abs(actual.green() - expected.green()) <= tolerance &&
                     std::abs(actual.blue() - expected.blue()) <= tolerance;
  if (!close) {
    std::fprintf(stderr, "  color %d,%d,%d is not within %d of %d,%d,%d\n", actual.red(), actual.green(),
                 actual.blue(), tolerance, expected.red(), expected.green(), expected.blue());
  }
  return close;
}

// A 400 x 300 document with a Gradient Fill layer from a Fill Layer mode drag (GitHub issue
// 580) in red to blue, the Gradient tool left active on it.
CanvasWidget* open_gradient_fill(patchy::ui::MainWindow& window, patchy::GradientMethod method, QPoint from,
                                 QPoint to) {
  MainWindowTestAccess::create_document(window, 400, 300);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolGradientAction")->trigger();
  QApplication::processEvents();
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("gradientModeCombo"));
  auto* style = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  CHECK(mode != nullptr && style != nullptr);
  if (mode != nullptr && style != nullptr) {
    mode->setCurrentIndex(1);
    style->setCurrentIndex(style->findData(static_cast<int>(method)));
  }
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Gradient);
  CHECK(canvas->gradient_tool_mode() == patchy::ui::GradientToolMode::FillLayer);
  CHECK(canvas->gradient_method() == method);
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{200, 30, 30, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{30, 40, 210, 255}}});
  // The background layer is not a fill: no handles.
  CHECK(!canvas->gradient_fill_handles().has_value());
  drag(*canvas, widget_at(*canvas, from), widget_at(*canvas, to));
  QApplication::processEvents();
  return canvas;
}

// The Gradient tool settings these tests change through the options bar.
struct GradientSettingsGuard {
  SettingsValueRestorer active_tool{QStringLiteral("tools/activeTool")};
  SettingsValueRestorer method{QStringLiteral("tools/gradientMethod")};
  SettingsValueRestorer mode{QStringLiteral("tools/gradientMode")};
  SettingsValueRestorer reverse{QStringLiteral("tools/gradientReverse")};
  SettingsValueRestorer use_custom{QStringLiteral("tools/gradientUseCustomStops")};
  SettingsValueRestorer stops{QStringLiteral("tools/gradientStops")};
};

struct FillProbe {
  patchy::ui::MainWindow& window;
  patchy::LayerId id;

  [[nodiscard]] patchy::LayerStyleGradient gradient() const {
    const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
    CHECK(layer != nullptr && layer->vector_shape() != nullptr);
    return layer != nullptr && layer->vector_shape() != nullptr ? layer->vector_shape()->fill.gradient
                                                                 : patchy::LayerStyleGradient{};
  }
  [[nodiscard]] std::size_t depth() const { return MainWindowTestAccess::active_session_undo_depth(window); }
  [[nodiscard]] QString label() const { return MainWindowTestAccess::current_history_label(window); }
};

QPointF part_position(CanvasWidget& canvas, Kind kind, int index = -1) {
  const auto position = canvas.gradient_fill_handle_widget_position(Part{kind, index});
  CHECK(position.has_value());
  return position.value_or(QPointF());
}

void ui_gradient_fill_handles_edit_a_linear_fill() {
  const GradientSettingsGuard settings_guard;
  auto&& [window, first_canvas] = ShownWindowFixture{};
  (void)first_canvas;
  auto* canvas = open_gradient_fill(window, patchy::GradientMethod::Linear, QPoint(100, 150), QPoint(300, 150));
  const auto active = MainWindowTestAccess::document(window).active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const FillProbe fill{window, *active};
  CHECK(patchy::layer_is_fill_layer(*std::as_const(MainWindowTestAccess::document(window)).find_layer(fill.id)));
  const auto line = [&] {
    const auto handles = canvas->gradient_fill_handles();
    CHECK(handles.has_value());
    return handles.has_value() ? handles->line : patchy::GradientFillLine{};
  };
  const auto start_of = [&] { return QPointF(line().start_x, line().start_y); };
  const auto end_of = [&] { return QPointF(line().end_x, line().end_y); };
  CHECK(canvas->gradient_fill_handles()->layer_id == fill.id);
  CHECK(near(start_of(), QPointF(100, 150), 1.5));
  CHECK(near(end_of(), QPointF(300, 150), 1.5));

  // Drawn where the placement puts them, through the view, and hit there.
  const auto start = part_position(*canvas, Kind::Start);
  const auto end = part_position(*canvas, Kind::End);
  CHECK(near(start, canvas->widget_position_f(start_of()), 0.01));
  CHECK(near(end, canvas->widget_position_f(end_of()), 0.01));
  CHECK(canvas->gradient_fill_handle_part_at(start).kind == Kind::Start);
  CHECK(canvas->gradient_fill_handle_part_at(end).kind == Kind::End);
  CHECK(canvas->gradient_fill_handle_part_at((start + end) / 2.0).kind == Kind::Line);
  const auto color_stop_0 = part_position(*canvas, Kind::ColorStop, 0);
  const auto color_stop_1 = part_position(*canvas, Kind::ColorStop, 1);
  const auto alpha_stop_1 = part_position(*canvas, Kind::AlphaStop, 1);
  CHECK(canvas->gradient_fill_handle_part_at(color_stop_0) == (Part{Kind::ColorStop, 0}));
  CHECK(canvas->gradient_fill_handle_part_at(color_stop_1) == (Part{Kind::ColorStop, 1}));
  CHECK(canvas->gradient_fill_handle_part_at(alpha_stop_1) == (Part{Kind::AlphaStop, 1}));
  // Color stops sit on one side of the line, opacity stops on the other.
  CHECK((color_stop_1.y() - end.y()) * (alpha_stop_1.y() - end.y()) < 0.0);
  const auto midpoint = part_position(*canvas, Kind::ColorMidpoint, 1);
  CHECK(canvas->gradient_fill_handle_part_at(midpoint) == (Part{Kind::ColorMidpoint, 1}));
  CHECK(near(midpoint, (color_stop_0 + color_stop_1) / 2.0, 0.5));
  // The pixels: a light start disc, the end's dark center dot, stops in their colors.
  const auto image = render_widget_image(*canvas);
  const auto& palette = patchy::ui::theme();
  CHECK(color_near(image.pixelColor(rounded(start)), palette.canvas_gradient_handle_fill, 24));
  CHECK(color_near(image.pixelColor(rounded(end)), palette.canvas_gradient_handle_ink, 40));
  CHECK(color_near(image.pixelColor(rounded(color_stop_0)), QColor(200, 30, 30), 24));
  CHECK(color_near(image.pixelColor(rounded(color_stop_1)), QColor(30, 40, 210), 24));
  canvas->set_zoom_centered(1.5);
  QApplication::processEvents();
  save_widget_artifact("ui_gradient_fill_handles_linear", window);
  canvas->set_zoom_centered(1.0);
  QApplication::processEvents();

  // The end handle: angle and length change, the start stays; one undo step.
  auto depth = fill.depth();
  drag(*canvas, rounded(part_position(*canvas, Kind::End)), widget_at(*canvas, QPointF(300, 230)));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Gradient End Point"));
  CHECK(near(end_of(), QPointF(300, 230), 1.5));
  CHECK(near(start_of(), QPointF(100, 150), 1.5));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(near(end_of(), QPointF(300, 150), 1.5));

  // Shift keeps the angle in 15-degree steps.
  drag(*canvas, rounded(part_position(*canvas, Kind::End)), widget_at(*canvas, QPointF(290, 245)),
       Qt::ShiftModifier);
  const auto snapped = static_cast<double>(fill.gradient().angle_degrees);
  CHECK(std::abs(snapped / 15.0 - std::round(snapped / 15.0)) < 1e-3);
  CHECK(std::abs(snapped) > 1.0);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  // The line moves the gradient: offsets change, angle and scale do not.
  const auto placed = fill.gradient();
  depth = fill.depth();
  drag(*canvas, widget_at(*canvas, QPointF(200, 150)), widget_at(*canvas, QPointF(220, 160)));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Gradient"));
  CHECK(near(start_of(), QPointF(120, 160), 1.5));
  CHECK(near(end_of(), QPointF(320, 160), 1.5));
  CHECK(std::abs(fill.gradient().angle_degrees - placed.angle_degrees) < 1e-3F);
  CHECK(std::abs(fill.gradient().scale - placed.scale) < 1e-4F);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  // A color stop slides along the line.
  depth = fill.depth();
  const auto stop_from = part_position(*canvas, Kind::ColorStop, 1);
  const auto along = canvas->widget_position_f(QPointF(250, 150)) - canvas->widget_position_f(QPointF(300, 150));
  drag(*canvas, rounded(stop_from), rounded(stop_from + along));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Color Stop"));
  CHECK(fill.gradient().color_stops.size() == 2U);
  CHECK(std::abs(fill.gradient().color_stops[1].location - 0.75F) < 0.01F);

  // Its midpoint slides between the stops (0 and 0.75): to 0.25 is a third of the way.
  depth = fill.depth();
  const auto midpoint_from = part_position(*canvas, Kind::ColorMidpoint, 1);
  const auto to_quarter = canvas->widget_position_f(QPointF(150, 150)) - canvas->widget_position_f(QPointF(175, 150));
  drag(*canvas, rounded(midpoint_from), rounded(midpoint_from + to_quarter));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Gradient Midpoint"));
  CHECK(std::abs(fill.gradient().color_stops[1].midpoint - 1.0F / 3.0F) < 0.02F);

  // A click on the line adds a color stop there.
  depth = fill.depth();
  const auto on_line = widget_at(*canvas, QPointF(200, 150));
  CHECK(canvas->gradient_fill_handle_part_at(QPointF(on_line)).kind == Kind::Line);
  send_mouse(*canvas, QEvent::MouseButtonPress, on_line, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, on_line, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Add Color Stop"));
  CHECK(fill.gradient().color_stops.size() == 3U);
  CHECK(std::abs(fill.gradient().color_stops[1].location - 0.5F) < 0.01F);

  // Dragged well off the line, it goes again.
  depth = fill.depth();
  const auto added = part_position(*canvas, Kind::ColorStop, 1);
  drag(*canvas, rounded(added), rounded(added + QPointF(0.0, 90.0)));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Delete Color Stop"));
  CHECK(fill.gradient().color_stops.size() == 2U);
  // Two stops are the least: the same drag on one of them only moves it.
  const auto last_stop = part_position(*canvas, Kind::ColorStop, 0);
  drag(*canvas, rounded(last_stop), rounded(last_stop + QPointF(0.0, 90.0)));
  CHECK(fill.gradient().color_stops.size() == 2U);
  CHECK(fill.label() == QStringLiteral("Delete Color Stop"));

  // Esc during a drag puts the gradient back, with no history.
  const auto before_escape = fill.gradient();
  depth = fill.depth();
  const auto end_now = rounded(part_position(*canvas, Kind::End));
  send_mouse(*canvas, QEvent::MouseButtonPress, end_now, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, QPointF(350, 40)), Qt::NoButton, Qt::LeftButton);
  CHECK(!(fill.gradient() == before_escape));  // the drag previews live
  send_key(*canvas, Qt::Key_Escape);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_at(*canvas, QPointF(350, 40)), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(fill.gradient() == before_escape);
  CHECK(fill.depth() == depth);

  // A drag away from the handles still draws a new gradient on the same layer.
  depth = fill.depth();
  CHECK(canvas->gradient_fill_handle_part_at(QPointF(widget_at(*canvas, QPointF(40, 40)))).kind == Kind::None);
  drag(*canvas, widget_at(*canvas, QPointF(40, 40)), widget_at(*canvas, QPointF(40, 260)));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Edit Gradient Fill"));
  CHECK(MainWindowTestAccess::document(window).active_layer_id() == fill.id);
  CHECK(near(start_of(), QPointF(40, 40), 1.5));
  CHECK(near(end_of(), QPointF(40, 260), 1.5));

  // Another tool hides them.
  canvas->set_tool(CanvasTool::Brush);
  CHECK(!canvas->gradient_fill_handles().has_value());
}

void ui_gradient_fill_handles_follow_a_radial_fill_in_a_rotated_view() {
  const GradientSettingsGuard settings_guard;
  auto&& [window, first_canvas] = ShownWindowFixture{};
  (void)first_canvas;
  auto* canvas = open_gradient_fill(window, patchy::GradientMethod::Radial, QPoint(200, 150), QPoint(320, 150));
  const auto active = MainWindowTestAccess::document(window).active_layer_id();
  CHECK(active.has_value());
  if (!active.has_value()) {
    return;
  }
  const FillProbe fill{window, *active};
  CHECK(fill.gradient().type == patchy::LayerStyleGradientType::Radial);
  // A third stop so the shot shows midpoints on both sides of it.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_at(*canvas, QPointF(260, 150)), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_at(*canvas, QPointF(260, 150)), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(fill.gradient().color_stops.size() == 3U);
  canvas->set_zoom_centered(1.5);
  QApplication::processEvents();
  save_widget_artifact("ui_gradient_fill_handles_radial", window);
  canvas->set_zoom_centered(1.0);
  QApplication::processEvents();

  canvas->set_view_rotation(30.0);
  QApplication::processEvents();
  const auto handles = canvas->gradient_fill_handles();
  CHECK(handles.has_value());
  if (!handles.has_value()) {
    return;
  }
  CHECK(near(QPointF(handles->line.start_x, handles->line.start_y), QPointF(200, 150), 1.5));
  CHECK(near(QPointF(handles->line.end_x, handles->line.end_y), QPointF(320, 150), 1.5));
  const auto end = part_position(*canvas, Kind::End);
  CHECK(near(end, canvas->widget_position_f(QPointF(handles->line.end_x, handles->line.end_y)), 0.01));
  CHECK(canvas->gradient_fill_handle_part_at(end).kind == Kind::End);
  const auto depth = fill.depth();
  drag(*canvas, rounded(end), widget_at(*canvas, QPointF(200, 60)));
  CHECK(fill.depth() == depth + 1);
  const auto moved = canvas->gradient_fill_handles();
  CHECK(moved.has_value() && near(QPointF(moved->line.end_x, moved->line.end_y), QPointF(200, 60), 1.5));
  CHECK(moved.has_value() && near(QPointF(moved->line.start_x, moved->line.start_y), QPointF(200, 150), 1.5));
  canvas->set_view_rotation(0.0);
  QApplication::processEvents();

  // A double-click on a color stop opens the picker; its choice is one undo step.
  const auto stop = rounded(part_position(*canvas, Kind::ColorStop, 2));
  bool picked = false;
  when_dialog_visible(QStringLiteral("patchyColorDialog"), [&](QDialog& dialog) {
    CHECK(patchy::ui::apply_color_to_open_color_picker(QColor(20, 160, 60)));
    picked = true;
    dialog.accept();
  });
  const auto before_pick = fill.depth();
  send_double_click(*canvas, stop);
  QApplication::processEvents();
  CHECK(picked);
  const auto picked_color = fill.gradient().color_stops[2].color;
  CHECK(picked_color.red == 20 && picked_color.green == 160 && picked_color.blue == 60);
  CHECK(fill.depth() == before_pick + 1);
  CHECK(fill.label() == QStringLiteral("Change Color Stop"));
}

}  // namespace

std::vector<patchy::test::TestCase> gradient_fill_handles_tests() {
  return {
      {"ui_gradient_fill_handles_edit_a_linear_fill", ui_gradient_fill_handles_edit_a_linear_fill},
      {"ui_gradient_fill_handles_follow_a_radial_fill_in_a_rotated_view",
       ui_gradient_fill_handles_follow_a_radial_fill_in_a_rotated_view},
  };
}
