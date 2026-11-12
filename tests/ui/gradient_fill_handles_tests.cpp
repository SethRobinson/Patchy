// The Gradient tool's handles on an existing Gradient Fill (GitHub issue 663, docs/gradients-fill-handles.md,
// "Gradient Fill handles"): where the line, its ends, the stops and the midpoints are drawn,
// and each gesture (end, line, stop, midpoint, add, delete, color, Esc) as one undo step,
// while a drag away from them still draws a new gradient.
#include "core/blend_math.hpp"
#include "core/gradient_presets.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
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
#include <QToolTip>
#include <QHelpEvent>
#include <QElapsedTimer>
#include <QFrame>
#include <QAction>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineF>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
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
  SettingsValueRestorer definition{QStringLiteral("tools/gradientDefinition")};
};

// A click the way the window system delivers one: a hover move, then a press and a release
// through the top-level QWindow, so Qt routes them to whatever widget is under the point.
void window_click(QWidget& widget, QPoint position) {
  auto* top = widget.window();
  auto* handle = top != nullptr ? top->windowHandle() : nullptr;
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  const auto in_window = widget.mapTo(top, position);
  QTest::mouseMove(handle, in_window);
  QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, in_window);
  QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, in_window);
  QApplication::processEvents();
}

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

  // A click on the line adds a color stop there, delivered like a real mouse click; a click
  // a few pixels beside the drawn line still counts as on it.
  depth = fill.depth();
  const auto on_line = widget_at(*canvas, QPointF(200, 150)) + QPoint(0, 5);
  CHECK(canvas->gradient_fill_handle_part_at(QPointF(on_line)).kind == Kind::Line);
  window_click(*canvas, on_line);
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
  depth = fill.depth();
  const auto last_stop = part_position(*canvas, Kind::ColorStop, 0);
  drag(*canvas, rounded(last_stop), rounded(last_stop + QPointF(0.0, 90.0)));
  CHECK(fill.gradient().color_stops.size() == 2U);
  // Rounding the pointer to whole widget pixels can nudge its location: at most a move.
  CHECK(fill.depth() == depth || fill.label() == QStringLiteral("Move Color Stop"));

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

// Seth's case (GitHub issue 663 follow-up): a many-stop preset, then Black to White from the
// options bar's Presets popup. The tool keeps the preset's own two stops (they were 33
// samples), so a Fill Layer drag shows two color stops on the line and Edit Gradient Stops
// lists two rows.
void ui_gradient_fill_handles_show_a_presets_own_stops() {
  const GradientSettingsGuard settings_guard;
  auto&& [window, first_canvas] = ShownWindowFixture{};
  (void)first_canvas;
  MainWindowTestAccess::create_document(window, 400, 300);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolGradientAction")->trigger();
  QApplication::processEvents();
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("gradientModeCombo"));
  CHECK(mode != nullptr);
  if (mode != nullptr) {
    mode->setCurrentIndex(1);
  }
  const auto pick_preset = [&](const QString& name) {
    auto* presets_button = window.findChild<QPushButton*>(QStringLiteral("gradientPresetsButton"));
    CHECK(presets_button != nullptr);
    if (presets_button == nullptr) {
      return;
    }
    presets_button->click();
    QApplication::processEvents();
    QPointer<QFrame> popup = presets_button->findChild<QFrame*>(QStringLiteral("gradientPresetsButtonPopup"));
    CHECK(popup != nullptr);
    auto* tree = popup != nullptr ? popup->findChild<QTreeWidget*>(QStringLiteral("gradientPresetsButtonTree"))
                                  : nullptr;
    CHECK(tree != nullptr);
    if (tree == nullptr) {
      return;
    }
    QTreeWidgetItem* found = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it != nullptr && found == nullptr; ++it) {
      if ((*it)->text(0) == name && !(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
        found = *it;
      }
    }
    CHECK(found != nullptr);
    if (found == nullptr) {
      popup->close();
      return;
    }
    tree->scrollToItem(found);
    QApplication::processEvents();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(found).center());
    QApplication::processEvents();
  };
  pick_preset(QStringLiteral("Neon Spectrum"));
  CHECK(canvas->gradient_stops().has_value() && canvas->gradient_stops()->size() == 5U);
  pick_preset(QStringLiteral("Black to White"));
  CHECK(canvas->gradient_stops().has_value());
  if (!canvas->gradient_stops().has_value()) {
    return;
  }
  const auto stops = *canvas->gradient_stops();
  CHECK(stops.size() == 2U);
  CHECK(stops.front().location == 0.0F && stops.back().location == 1.0F);
  CHECK(stops.front().color.r == 0 && stops.back().color.r == 255);

  drag(*canvas, widget_at(*canvas, QPointF(60, 240)), widget_at(*canvas, QPointF(340, 60)));
  QApplication::processEvents();
  const auto handles = canvas->gradient_fill_handles();
  CHECK(handles.has_value());
  if (!handles.has_value()) {
    return;
  }
  CHECK(handles->gradient.color_stops.size() == 2U);
  CHECK(handles->gradient.alpha_stops.size() == 2U);
  CHECK(!canvas->gradient_fill_handle_widget_position(Part{Kind::ColorStop, 2}).has_value());

  // Edit Gradient Stops lists the same two stops; the card's shot stacks it under the window.
  bool checked = false;
  when_dialog_visible(QStringLiteral("gradientStopsDialog"), [&](QDialog& dialog) {
    // The two-track editor shows the preset's own stops (two color, two opacity).
    CHECK(dialog.findChild<QWidget*>(QStringLiteral("gradientStopsEditor")) != nullptr);
    CHECK(canvas->gradient_definition().has_value() && canvas->gradient_definition()->color_stops.size() == 2U &&
          canvas->gradient_definition()->alpha_stops.size() == 2U);
    canvas->set_zoom_centered(1.5);
    QApplication::processEvents();
    save_stacked_artifact("ui_gradient_fill_handles_black_to_white",
                          {capture_widget_artifact("ui_gradient_fill_handles_black_to_white_window", window),
                           capture_widget_artifact("ui_gradient_fill_handles_black_to_white_dialog", dialog)});
    checked = true;
    dialog.reject();
  });
  auto* edit_stops = window.findChild<QPushButton*>(QStringLiteral("gradientEditStopsButton"));
  CHECK(edit_stops != nullptr);
  if (edit_stops != nullptr) {
    edit_stops->click();
  }
  QApplication::processEvents();
  CHECK(checked);
}

namespace {

// Fill frames that reached the screen during a drag: view-resolution previews, and
// background composites installed.
int fill_frames_shown(const CanvasWidget& canvas) {
  const auto diagnostics = canvas.render_cache_diagnostics();
  return diagnostics.gradient_view_previews + diagnostics.intermediate_refreshes + diagnostics.full_refreshes;
}

// Waits (at most 30 s) for the canvas to show the document as it is.
void wait_until_settled(CanvasWidget& canvas) {
  QElapsedTimer timer;
  timer.start();
  while (!canvas.render_settled() && timer.elapsed() < 30000) {
    QTest::qWait(2);
  }
}

}  // namespace

// Per-move cost of the handle drags on large documents (GitHub issue 663 follow-up: the
// handles felt sluggish; GitHub #714: a 3580 x 5370 photo). Prints each gesture's mean
// milliseconds per pointer move, including the repaint, how many fill frames reached the
// screen during the drag and how long after the release the canvas settled, and checks the
// drag still lands as one undo step. The larger sizes run only with
// PATCHY_GRADIENT_HANDLE_TIMING=1 (they take seconds each; 8000 x 8000 needs about 1 GB).
void ui_gradient_fill_handles_drag_timing() {
  const GradientSettingsGuard settings_guard;
  std::vector<QSize> sizes{QSize(1600, 1200)};
  if (qEnvironmentVariableIntValue("PATCHY_GRADIENT_HANDLE_TIMING") != 0) {
    sizes = {QSize(3580, 5370), QSize(4000, 3000), QSize(8000, 8000)};
  }
  for (const auto size : sizes) {
    auto&& [window, first_canvas] = ShownWindowFixture{};
    (void)first_canvas;
    MainWindowTestAccess::create_document(window, size.width(), size.height());
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    canvas->fit_to_view();
    require_action(window, "toolGradientAction")->trigger();
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("gradientModeCombo"));
    if (mode != nullptr) {
      mode->setCurrentIndex(1);
    }
    QApplication::processEvents();
    const QPointF from(size.width() * 0.2, size.height() * 0.5);
    const QPointF to(size.width() * 0.8, size.height() * 0.5);
    drag(*canvas, widget_at(*canvas, from), widget_at(*canvas, to));
    QApplication::processEvents();
    CHECK(canvas->gradient_fill_handles().has_value());
    // A third stop so there is a midpoint to drag.
    window_click(*canvas, widget_at(*canvas, (from + to) / 2.0));
    CHECK(canvas->gradient_fill_handles().has_value() &&
          canvas->gradient_fill_handles()->gradient.color_stops.size() == 3U);
    const auto time_drag = [&](const char* name, Part part, QPointF step) {
      const bool view_preview = canvas->gradient_fill_view_preview_enabled_for_testing();
      const auto start = canvas->gradient_fill_handle_widget_position(part);
      CHECK(start.has_value());
      if (!start.has_value()) {
        return;
      }
      const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
      constexpr int kMoves = 12;
      wait_until_settled(*canvas);
      const auto frames_before = fill_frames_shown(*canvas);
      const auto diagnostics_before = canvas->render_cache_diagnostics();
      double move_ms = 0.0;
      double repaint_ms = 0.0;
      send_mouse(*canvas, QEvent::MouseButtonPress, rounded(*start), Qt::LeftButton, Qt::LeftButton);
      // Moves arrive every 20 ms like a pointer; the cost is the wall time the event loop
      // was busy (handler, throttled previews, repaints), the waits subtracted.
      constexpr int kGapMs = 20;
      QElapsedTimer timer;
      timer.start();
      for (int move = 1; move <= kMoves; ++move) {
        QElapsedTimer part_timer;
        part_timer.start();
        send_mouse(*canvas, QEvent::MouseMove, rounded(*start + step * move), Qt::NoButton, Qt::LeftButton);
        move_ms += static_cast<double>(part_timer.nsecsElapsed()) / 1e6;
        part_timer.start();
        canvas->repaint();
        repaint_ms += static_cast<double>(part_timer.nsecsElapsed()) / 1e6;
        QTest::qWait(kGapMs);
      }
      const auto per_move =
          std::max(0.0, static_cast<double>(timer.nsecsElapsed()) / 1e6 - kMoves * kGapMs) / kMoves;
      const auto frames = fill_frames_shown(*canvas) - frames_before;
      QElapsedTimer release_timer;
      release_timer.start();
      send_mouse(*canvas, QEvent::MouseButtonRelease, rounded(*start + step * kMoves), Qt::LeftButton,
                 Qt::NoButton);
      canvas->repaint();
      const auto release = static_cast<double>(release_timer.nsecsElapsed()) / 1e6;
      wait_until_settled(*canvas);
      const auto settled = static_cast<double>(release_timer.nsecsElapsed()) / 1e6;
      std::fprintf(stderr,
                   "  %dx%d %s, %s: %.1f ms per move, %d fill frames in %d moves, %.1f ms release, settled "
                   "after %.1f ms\n",
                   size.width(), size.height(), name, view_preview ? "view previews" : "document previews", per_move,
                   frames, kMoves, release, settled);
      // Where the time went: the move handlers and the repaints the loop made itself (the
      // rest is throttled previews and paints the event loop ran), and for view previews the
      // stages of each.
      const auto after = canvas->render_cache_diagnostics();
      const auto previews = after.gradient_view_previews - diagnostics_before.gradient_view_previews;
      std::fprintf(stderr, "    move handlers %.1f ms, repaints %.1f ms a move", move_ms / kMoves, repaint_ms / kMoves);
      if (previews > 0) {
        std::fprintf(stderr,
                     "; %d view previews of %llu px: source %.1f ms in %d builds, raster %.2f ms each, composite "
                     "%.2f ms each",
                     previews, static_cast<unsigned long long>(after.gradient_view_preview_last_pixels),
                     after.gradient_view_preview_source_ms - diagnostics_before.gradient_view_preview_source_ms,
                     after.gradient_view_preview_source_builds - diagnostics_before.gradient_view_preview_source_builds,
                     (after.gradient_view_preview_raster_ms - diagnostics_before.gradient_view_preview_raster_ms) /
                         previews,
                     (after.gradient_view_preview_composite_ms - diagnostics_before.gradient_view_preview_composite_ms) /
                         previews);
      }
      std::fprintf(stderr, "\n");
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
    };
    // Each gesture twice: through the document, as every preview went before GitHub #714
    // (and still goes for a fill the view preview does not take), then at view resolution.
    for (const bool view_preview : {false, true}) {
      canvas->set_gradient_fill_view_preview_enabled_for_testing(view_preview);
      time_drag("end handle", Part{Kind::End, -1}, QPointF(0.0, view_preview ? 3.0 : -3.0));
      time_drag("line", Part{Kind::Line, -1}, QPointF(2.0, 2.0) * (view_preview ? -1.0 : 1.0));
      time_drag("color stop", Part{Kind::ColorStop, 1}, QPointF(view_preview ? -3.0 : 3.0, 0.0));
      time_drag("midpoint", Part{Kind::ColorMidpoint, 1}, QPointF(view_preview ? -2.0 : 2.0, 0.0));
    }
  }
}

// GitHub #714: on a 3580 x 5370 photo the end handle trailed the pointer by up to a second,
// "Processing..." stayed up through the drag and the fill moved in jumps, because every
// preview baked a full-size raster into the layer and recomposited the whole document. A
// drag now previews at the view's resolution and leaves the document alone until the
// release. Pinned by what the drag does, not by how long it takes: no bake into the layer,
// no full composite and no background work while dragging, a preview no larger than the
// view, the exact bake on release, and Esc putting everything back.
void ui_gradient_fill_handle_drag_previews_at_view_resolution() {
  const GradientSettingsGuard settings_guard;
  auto&& [window, first_canvas] = ShownWindowFixture{};
  (void)first_canvas;
  constexpr int kWidth = 3580;
  constexpr int kHeight = 5370;
  MainWindowTestAccess::create_document(window, kWidth, kHeight);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();
  require_action(window, "toolGradientAction")->trigger();
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("gradientModeCombo"));
  CHECK(mode != nullptr);
  if (mode != nullptr) {
    mode->setCurrentIndex(1);
  }
  QApplication::processEvents();
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{200, 30, 30, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{30, 40, 210, 255}}});
  const QPointF from(kWidth * 0.2, kHeight * 0.5);
  const QPointF to(kWidth * 0.8, kHeight * 0.5);
  drag(*canvas, widget_at(*canvas, from), widget_at(*canvas, to));
  QApplication::processEvents();
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value() && canvas->gradient_fill_handles().has_value());
  if (!active.has_value() || !canvas->gradient_fill_handles().has_value()) {
    return;
  }
  const FillProbe fill{window, *active};
  wait_until_settled(*canvas);
  QApplication::processEvents();
  CHECK(canvas->render_settled());

  // A point well away from the line and its stops, halfway along the horizontal ramp.
  const auto sample = widget_at(*canvas, QPointF(kWidth * 0.5, kHeight * 0.9));
  CHECK(canvas_content_rect(*canvas).contains(sample));
  const auto shown_at_sample = [&] { return render_widget_image(*canvas).pixelColor(sample); };
  const QColor start_color(200, 30, 30);
  CHECK(!color_close(shown_at_sample(), start_color, 40));

  const auto layer_pixels = [&] {
    const auto* layer = std::as_const(document).find_layer(fill.id);
    return layer != nullptr ? layer->pixels().data().data() : nullptr;
  };
  const auto pixels_before = layer_pixels();
  const auto gradient_before = fill.gradient();
  const auto depth = fill.depth();
  const auto before = canvas->render_cache_diagnostics();
  const auto view_pixels = static_cast<std::uint64_t>(
      static_cast<double>(canvas->width()) * canvas->height() * canvas->devicePixelRatioF() *
      canvas->devicePixelRatioF());
  const auto full_composites = [](const CanvasWidget::RenderCacheDiagnostics& diagnostics) {
    return diagnostics.full_refreshes + diagnostics.intermediate_refreshes + diagnostics.region_preview_refreshes;
  };

  // Drag the end handle up until the ramp runs at 45 degrees: the sample point falls before
  // the start handle, so it turns the start color.
  const auto press = part_position(*canvas, Kind::End);
  const QPointF target(widget_at(*canvas, QPointF(kWidth * 0.8, kHeight * 0.1)));
  send_mouse(*canvas, QEvent::MouseButtonPress, rounded(press), Qt::LeftButton, Qt::LeftButton);
  constexpr int kMoves = 6;
  for (int move = 1; move <= kMoves; ++move) {
    send_mouse(*canvas, QEvent::MouseMove, rounded(press + (target - press) * move / kMoves), Qt::NoButton,
               Qt::LeftButton);
    QTest::qWait(45);  // past the preview interval, so every move previews
  }
  const auto during = canvas->render_cache_diagnostics();
  CHECK(canvas->gradient_fill_view_preview_shown());
  CHECK(during.gradient_view_previews >= before.gradient_view_previews + 3);
  // The handle is at the pointer, and the picture under it follows.
  CHECK(near(part_position(*canvas, Kind::End), target, 1.0));
  CHECK(color_close(shown_at_sample(), start_color, 12));
  // The document was left alone: the layer's raster and gradient are the ones from before.
  CHECK(layer_pixels() == pixels_before);
  CHECK(fill.gradient() == gradient_before);
  CHECK(fill.depth() == depth);
  // Nothing composited the whole document and nothing ran in the background, so no
  // "Processing..." either.
  CHECK(full_composites(during) == full_composites(before));
  CHECK(during.processing_overlays_shown == before.processing_overlays_shown);
  CHECK(canvas->render_settled());
  // The preview rendered the view's pixels, not the document's.
  CHECK(during.gradient_view_preview_last_pixels > 0);
  CHECK(during.gradient_view_preview_last_pixels <= view_pixels * 4);
  CHECK(during.gradient_view_preview_last_pixels * 8 <= static_cast<std::uint64_t>(kWidth) * kHeight);

  // The release bakes exactly, once, as one undo step, and the preview stays up until the
  // exact composite is on screen.
  send_mouse(*canvas, QEvent::MouseButtonRelease, rounded(target), Qt::LeftButton, Qt::NoButton);
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Gradient End Point"));
  // (A document small enough to recomposite at once is settled already.)
  CHECK(canvas->gradient_fill_view_preview_shown() || canvas->render_settled());
  CHECK(color_close(shown_at_sample(), start_color, 12));
  CHECK(fill.gradient() != gradient_before);
  {
    const auto* layer = std::as_const(document).find_layer(fill.id);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      auto exact = *layer;
      patchy::update_vector_shape_raster(exact, patchy::Rect::from_size(kWidth, kHeight),
                                         &std::as_const(document).metadata().patterns);
      CHECK(std::as_const(exact).bounds() == layer->bounds());
      CHECK(std::ranges::equal(std::as_const(exact).pixels().data(), layer->pixels().data()));
    }
  }
  wait_until_settled(*canvas);
  QApplication::processEvents();
  CHECK(color_close(shown_at_sample(), start_color, 12));
  CHECK(!canvas->gradient_fill_view_preview_shown());

  // Esc during a drag: the preview goes, the picture and the layer are as committed.
  const auto committed = fill.gradient();
  const auto committed_pixels = layer_pixels();
  const auto second_press = part_position(*canvas, Kind::End);
  const QPointF second_target(widget_at(*canvas, QPointF(kWidth * 0.8, kHeight * 0.9)));
  send_mouse(*canvas, QEvent::MouseButtonPress, rounded(second_press), Qt::LeftButton, Qt::LeftButton);
  for (int move = 1; move <= 3; ++move) {
    send_mouse(*canvas, QEvent::MouseMove, rounded(second_press + (second_target - second_press) * move / 3),
               Qt::NoButton, Qt::LeftButton);
    QTest::qWait(45);
  }
  CHECK(canvas->gradient_fill_view_preview_shown());
  CHECK(!color_close(shown_at_sample(), start_color, 40));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->gradient_fill_view_preview_shown());
  CHECK(color_close(shown_at_sample(), start_color, 12));
  CHECK(fill.gradient() == committed);
  CHECK(layer_pixels() == committed_pixels);
  CHECK(fill.depth() == depth + 1);
  CHECK(canvas->render_settled());
  send_mouse(*canvas, QEvent::MouseButtonRelease, rounded(second_target), Qt::LeftButton, Qt::NoButton);
  CHECK(fill.depth() == depth + 1);

  // The opacity popup previews the same way: the picture and the handles show the value
  // while the layer keeps its gradient. Esc puts it back, Enter commits one step.
  auto* handle = window.windowHandle();
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  const auto in_window = [&](QPointF widget_point) { return canvas->mapTo(&window, rounded(widget_point)); };
  const auto open_popup = [&]() -> QSpinBox* {
    QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, in_window(part_position(*canvas, Kind::AlphaStop, 0)));
    QApplication::processEvents();
    auto* popup = canvas->gradient_opacity_popup();
    CHECK(popup != nullptr && popup->isVisible());
    return popup != nullptr ? popup->findChild<QSpinBox*>(QStringLiteral("gradientOpacityStopSpin")) : nullptr;
  };
  const auto popup_key = [&](Qt::Key key) {
    auto* popup = canvas->gradient_opacity_popup();
    if (popup != nullptr && popup->windowHandle() != nullptr) {
      QTest::keyClick(popup->windowHandle(), key);
    }
    QApplication::processEvents();
  };
  const auto shown_opacity = [&] {
    const auto handles = canvas->gradient_fill_handles();
    return handles.has_value() && !handles->gradient.alpha_stops.empty() ? handles->gradient.alpha_stops[0].opacity
                                                                         : -1.0F;
  };
  if (auto* spin = open_popup(); spin != nullptr) {
    spin->setValue(0);
    QApplication::processEvents();
  }
  CHECK(canvas->gradient_fill_view_preview_shown());
  CHECK(std::abs(shown_opacity()) < 0.001F);
  CHECK(!color_close(shown_at_sample(), start_color, 40));  // the start color is see-through there now
  CHECK(fill.gradient() == committed);
  CHECK(layer_pixels() == committed_pixels);
  popup_key(Qt::Key_Escape);
  CHECK(canvas->gradient_opacity_popup() == nullptr);
  CHECK(!canvas->gradient_fill_view_preview_shown());
  CHECK(std::abs(shown_opacity() - 1.0F) < 0.001F);
  CHECK(color_close(shown_at_sample(), start_color, 12));
  CHECK(fill.depth() == depth + 1);
  if (auto* spin = open_popup(); spin != nullptr) {
    spin->setValue(0);
    QApplication::processEvents();
  }
  popup_key(Qt::Key_Return);
  CHECK(canvas->gradient_opacity_popup() == nullptr);
  CHECK(fill.depth() == depth + 2);
  CHECK(fill.label() == QStringLiteral("Change Opacity Stop"));
  CHECK(std::abs(fill.gradient().alpha_stops[0].opacity) < 0.001F);
  CHECK(!color_close(shown_at_sample(), start_color, 40));
  wait_until_settled(*canvas);
  QApplication::processEvents();
  CHECK(!color_close(shown_at_sample(), start_color, 40));
  CHECK(!canvas->gradient_fill_view_preview_shown());
}

// The view preview at the document's own resolution and under a rotated view (GitHub #714):
// at 100% it renders the part of the fill on screen, a rotated view paints it through the
// view transform, and either way the drag shows what the release then bakes.
void ui_gradient_fill_handle_view_preview_follows_zoom_and_rotation() {
  const GradientSettingsGuard settings_guard;
  auto&& [window, first_canvas] = ShownWindowFixture{};
  (void)first_canvas;
  constexpr int kWidth = 1400;
  constexpr int kHeight = 1000;
  MainWindowTestAccess::create_document(window, kWidth, kHeight);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolGradientAction")->trigger();
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("gradientModeCombo"));
  CHECK(mode != nullptr);
  if (mode != nullptr) {
    mode->setCurrentIndex(1);
  }
  QApplication::processEvents();
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{200, 30, 30, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{30, 40, 210, 255}}});
  canvas->set_zoom_centered(1.0);
  QApplication::processEvents();
  // Every point is placed in the part of the canvas that shows the document, as a fraction
  // of its half size from its center, so the test does not depend on the window's size.
  const auto content = canvas_content_rect(*canvas);
  const QPointF center(content.center());
  const double reach = std::min(content.width(), content.height()) * 0.5;
  CHECK(reach > 120.0);
  const auto at = [&](double x, double y) { return QPointF(center.x() + x * reach, center.y() + y * reach); };
  drag(*canvas, rounded(at(-0.6, 0.0)), rounded(at(0.6, 0.0)));
  QApplication::processEvents();
  const auto active = MainWindowTestAccess::document(window).active_layer_id();
  CHECK(active.has_value() && canvas->gradient_fill_handles().has_value());
  if (!active.has_value() || !canvas->gradient_fill_handles().has_value()) {
    return;
  }
  const FillProbe fill{window, *active};
  const auto full_composites = [](const CanvasWidget::RenderCacheDiagnostics& diagnostics) {
    return diagnostics.full_refreshes + diagnostics.intermediate_refreshes + diagnostics.region_preview_refreshes;
  };
  // Below the line and its stops before and after either drag.
  const auto sample = rounded(at(-0.2, 0.55));
  // Drags the end handle to the widget point `target`, checks the picture mid-drag against
  // the committed one, and returns the pixels the last preview rendered.
  const auto drag_end_to = [&](QPointF target) -> std::uint64_t {
    wait_until_settled(*canvas);
    QApplication::processEvents();
    const auto color_before = render_widget_image(*canvas).pixelColor(sample);
    const auto before = canvas->render_cache_diagnostics();
    const auto depth = fill.depth();
    const auto press = part_position(*canvas, Kind::End);
    send_mouse(*canvas, QEvent::MouseButtonPress, rounded(press), Qt::LeftButton, Qt::LeftButton);
    for (int move = 1; move <= 4; ++move) {
      send_mouse(*canvas, QEvent::MouseMove, rounded(press + (target - press) * move / 4), Qt::NoButton,
                 Qt::LeftButton);
      QTest::qWait(45);
    }
    const auto during = canvas->render_cache_diagnostics();
    CHECK(canvas->gradient_fill_view_preview_shown());
    CHECK(during.gradient_view_previews >= before.gradient_view_previews + 2);
    CHECK(full_composites(during) == full_composites(before));
    CHECK(fill.depth() == depth);
    const auto color_previewed = render_widget_image(*canvas).pixelColor(sample);
    CHECK(!color_close(color_previewed, color_before, 16));
    send_mouse(*canvas, QEvent::MouseButtonRelease, rounded(target), Qt::LeftButton, Qt::NoButton);
    CHECK(fill.depth() == depth + 1);
    wait_until_settled(*canvas);
    QApplication::processEvents();
    const auto color_committed = render_widget_image(*canvas).pixelColor(sample);
    CHECK(!canvas->gradient_fill_view_preview_shown());
    CHECK(color_close(color_previewed, color_committed, 10));
    return during.gradient_view_preview_last_pixels;
  };

  // 100%: the preview is the document's own pixels, for the area on screen only.
  const auto document_pixels = static_cast<std::uint64_t>(kWidth) * kHeight;
  const auto view_pixels =
      static_cast<std::uint64_t>(canvas->width() + 16) * static_cast<std::uint64_t>(canvas->height() + 16);
  const auto upright_pixels = drag_end_to(at(0.6, -0.6));
  CHECK(upright_pixels > 0);
  CHECK(upright_pixels <= std::min(document_pixels, view_pixels));

  // A rotated view paints the same preview through the view transform.
  canvas->set_view_rotation(30.0);
  QApplication::processEvents();
  const auto rotated_pixels = drag_end_to(at(0.5, 0.3));
  CHECK(rotated_pixels > 0 && rotated_pixels <= document_pixels);
}

// GitHub #705: Wayland delivers every pointer motion, and a Gradient Fill handle drag placed
// the gradient for each one (and, whenever a move took longer than the preview interval, a
// preview too), so on Linux the handle stood still for seconds with "Processing..." up and
// then jumped. Moves queued in one event-loop pass now apply once, at the latest position,
// and a move recorded right before the release still lands.
void ui_gradient_fill_handle_drag_coalesces_queued_moves() {
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
  const auto depth = fill.depth();
  canvas->set_drag_move_coalescing_for_testing(CanvasWidget::DragMoveCoalescing::AllMoves);
  const auto coalesced = [canvas] { return canvas->render_cache_diagnostics().drag_moves_coalesced; };
  const auto start = rounded(part_position(*canvas, Kind::End));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  const auto before = coalesced();
  for (int move = 1; move <= 30; ++move) {
    const auto position = start + QPoint(0, move);
    QApplication::postEvent(canvas, new QMouseEvent(QEvent::MouseMove, QPointF(position),
                                                    QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                                                    Qt::LeftButton, Qt::NoModifier));
  }
  // The first queued move applies at once, the other 29 wait for the pass end.
  QApplication::processEvents();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(coalesced() - before == 29);
  CHECK(near(part_position(*canvas, Kind::End), QPointF(start + QPoint(0, 30)), 1.0));
  // A move recorded right before the release still lands: the release applies it.
  const auto end = start + QPoint(-20, 45);
  const auto deliver = [canvas](QPoint position) {
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
  };
  deliver(end - QPoint(0, 3));  // applied, opens a pass
  deliver(end);                 // recorded
  CHECK(coalesced() - before == 30);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(near(part_position(*canvas, Kind::End), QPointF(end), 1.0));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Gradient End Point"));
}

// GitHub issue 663, second follow-up: ways to delete a stop that people find, and a cursor
// for every handle part. All input goes through the window like a real mouse and keyboard.
void ui_gradient_fill_handles_delete_stops_and_show_cursors() {
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
  auto* handle = window.windowHandle();
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  const auto in_window = [&](QPointF widget_point) { return canvas->mapTo(&window, rounded(widget_point)); };
  const auto hover = [&](QPointF widget_point) {
    QTest::mouseMove(handle, in_window(widget_point));
    QApplication::processEvents();
  };
  const auto press_key = [&](Qt::Key key) {
    QTest::keyClick(handle, key);
    QApplication::processEvents();
  };

  // Cursors: the tool's own away from the handles, add-stop over a blank part of the line,
  // move over an end, along-the-line over a stop.
  hover(QPointF(widget_at(*canvas, QPointF(60, 40))));
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::None);
  const auto tool_cursor_shape = canvas->cursor().shape();
  hover(QPointF(widget_at(*canvas, QPointF(200, 150)) + QPoint(0, 5)));
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::AddStop);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  hover(part_position(*canvas, Kind::End));
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::Move);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  hover(part_position(*canvas, Kind::ColorStop, 0));
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::AlongLine);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  hover(QPointF(widget_at(*canvas, QPointF(60, 40))));
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::None);
  CHECK(canvas->cursor().shape() == tool_cursor_shape);

  // A click on the line adds a stop and selects it; Delete removes it as one undo step.
  window_click(*canvas, widget_at(*canvas, QPointF(200, 150)));
  CHECK(fill.gradient().color_stops.size() == 3U);
  CHECK(canvas->gradient_fill_selected_stop() == (std::optional<Part>(Part{Kind::ColorStop, 1})));
  auto depth = fill.depth();
  press_key(Qt::Key_Delete);
  CHECK(fill.gradient().color_stops.size() == 2U);
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Delete Color Stop"));
  CHECK(!canvas->gradient_fill_selected_stop().has_value());

  // At two stops a selected stop stays: Delete is refused and goes nowhere else.
  QAction* delete_action = nullptr;
  for (auto* action : window.findChildren<QAction*>()) {
    if (action->shortcuts().contains(QKeySequence(Qt::Key_Delete))) {
      delete_action = action;
      break;
    }
  }
  CHECK(delete_action != nullptr);
  int delete_triggers = 0;
  if (delete_action != nullptr) {
    QObject::connect(delete_action, &QAction::triggered, &window, [&delete_triggers] { ++delete_triggers; });
  }
  window_click(*canvas, rounded(part_position(*canvas, Kind::ColorStop, 0)));
  CHECK(canvas->gradient_fill_selected_stop() == (std::optional<Part>(Part{Kind::ColorStop, 0})));
  depth = fill.depth();
  press_key(Qt::Key_Delete);
  CHECK(fill.gradient().color_stops.size() == 2U);
  CHECK(fill.depth() == depth);
  CHECK(delete_triggers == 0);

  // Right-click a stop: Delete Stop removes it; at two stops it is disabled.
  window_click(*canvas, widget_at(*canvas, QPointF(250, 150)));
  CHECK(fill.gradient().color_stops.size() == 3U);
  const auto open_stop_menu = [&](int index) -> QMenu* {
    QTest::mouseClick(handle, Qt::RightButton, Qt::NoModifier,
                      in_window(part_position(*canvas, Kind::ColorStop, index)));
    QApplication::processEvents();
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    CHECK(menu != nullptr && menu->objectName() == QStringLiteral("gradientStopContextMenu"));
    return menu;
  };
  auto* menu = open_stop_menu(1);
  if (menu != nullptr) {
    auto* remove = menu->findChild<QAction*>(QStringLiteral("gradientStopDeleteAction"));
    CHECK(remove != nullptr && remove->isEnabled());
    CHECK(menu->findChild<QAction*>(QStringLiteral("gradientStopColorAction")) != nullptr);
    CHECK(canvas->gradient_fill_selected_stop() == (std::optional<Part>(Part{Kind::ColorStop, 1})));
    canvas->set_zoom_centered(1.5);
    QApplication::processEvents();
    menu->move(canvas->mapToGlobal(rounded(part_position(*canvas, Kind::ColorStop, 1)) + QPoint(4, 4)));
    QApplication::processEvents();
    save_window_popup_artifact("ui_gradient_fill_handles_stop_menu", window, *canvas, *menu);
    depth = fill.depth();
    if (remove != nullptr) {
      remove->trigger();
    }
    menu->close();
    QApplication::processEvents();
    CHECK(fill.gradient().color_stops.size() == 2U);
    CHECK(fill.depth() == depth + 1);
    CHECK(fill.label() == QStringLiteral("Delete Color Stop"));
    canvas->set_zoom_centered(1.0);
    QApplication::processEvents();
  }
  menu = open_stop_menu(0);
  if (menu != nullptr) {
    auto* remove = menu->findChild<QAction*>(QStringLiteral("gradientStopDeleteAction"));
    CHECK(remove != nullptr && !remove->isEnabled());
    CHECK(remove != nullptr && !remove->toolTip().isEmpty());
    menu->close();
    QApplication::processEvents();
  }

  // Dragged off the line a stop shows it will go (delete cursor, gone from the preview);
  // back on the line it stays; off again and released, it is deleted.
  window_click(*canvas, widget_at(*canvas, QPointF(200, 150)));
  CHECK(fill.gradient().color_stops.size() == 3U);
  const auto stop = part_position(*canvas, Kind::ColorStop, 1);
  depth = fill.depth();
  QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, in_window(stop));
  QTest::mouseMove(handle, in_window(stop + QPointF(0.0, 70.0)));
  QApplication::processEvents();
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::DeleteStop);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->gradient_fill_handles()->gradient.color_stops.size() == 2U);
  QTest::mouseMove(handle, in_window(stop + QPointF(0.0, 4.0)));
  QApplication::processEvents();
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::AlongLine);
  CHECK(canvas->gradient_fill_handles()->gradient.color_stops.size() == 3U);
  QTest::mouseMove(handle, in_window(stop + QPointF(0.0, 70.0)));
  QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, in_window(stop + QPointF(0.0, 70.0)));
  QApplication::processEvents();
  CHECK(fill.gradient().color_stops.size() == 2U);
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Delete Color Stop"));

  // With no stop selected the canvas leaves Delete to the shortcut (Edit > Clear does its
  // usual job); with one selected it takes the key first.
  const auto canvas_takes_delete = [&] {
    QKeyEvent override_event(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
    override_event.ignore();
    QApplication::sendEvent(canvas, &override_event);
    return override_event.isAccepted();
  };
  window_click(*canvas, widget_at(*canvas, QPointF(60, 40)));
  CHECK(!canvas->gradient_fill_selected_stop().has_value());
  CHECK(!canvas_takes_delete());
  window_click(*canvas, rounded(part_position(*canvas, Kind::ColorStop, 0)));
  CHECK(canvas_takes_delete());
  CHECK(delete_triggers == 0);
}

// GitHub issue 663, third follow-up: the two kinds of midpoint are named apart, and an opacity
// midpoint between equal opacities (which changes nothing yet) is drawn dimmed but still drags.
void ui_gradient_fill_handles_name_and_dim_the_opacity_midpoint() {
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
  const Part color_midpoint{Kind::ColorMidpoint, 1};
  const Part opacity_midpoint{Kind::AlphaMidpoint, 1};
  CHECK(canvas->gradient_fill_handle_widget_position(color_midpoint).has_value());
  CHECK(canvas->gradient_fill_handle_widget_position(opacity_midpoint).has_value());
  // Both opacity stops are 100%: the opacity midpoint is dimmed, the color one never is.
  CHECK(canvas->gradient_fill_midpoint_dimmed(opacity_midpoint));
  CHECK(!canvas->gradient_fill_midpoint_dimmed(color_midpoint));
  const auto color_tip = canvas->gradient_fill_handle_tooltip(color_midpoint);
  const auto opacity_tip = canvas->gradient_fill_handle_tooltip(opacity_midpoint);
  CHECK(color_tip.startsWith(QStringLiteral("Color midpoint: 50%")));
  CHECK(opacity_tip.startsWith(QStringLiteral("Opacity midpoint: 50%")));
  CHECK(opacity_tip.contains(QStringLiteral("no effect yet")));
  CHECK(canvas->gradient_fill_handle_hint(color_midpoint) != canvas->gradient_fill_handle_hint(opacity_midpoint));
  CHECK(canvas->gradient_fill_handle_hint(opacity_midpoint).contains(QStringLiteral("opacity midpoint")));
  CHECK(canvas->gradient_fill_handle_tooltip(Part{Kind::ColorStop, 0}).startsWith(QStringLiteral("Color stop")));
  CHECK(canvas->gradient_fill_handle_tooltip(Part{Kind::AlphaStop, 0}).startsWith(QStringLiteral("Opacity stop")));
  // The pixels: the dimmed diamond's center is fainter than the color midpoint's.
  {
    const auto image = render_widget_image(*canvas);
    const auto color_center = image.pixelColor(rounded(*canvas->gradient_fill_handle_widget_position(color_midpoint)));
    const auto opacity_center =
        image.pixelColor(rounded(*canvas->gradient_fill_handle_widget_position(opacity_midpoint)));
    const auto& palette = patchy::ui::theme();
    CHECK(color_near(color_center, palette.canvas_gradient_handle_fill, 24));
    CHECK(!color_near(opacity_center, palette.canvas_gradient_handle_fill, 24));
  }

  // The card's shot: the tooltip over the dimmed opacity midpoint, the color midpoint beside it.
  canvas->set_zoom_centered(1.5);
  QApplication::processEvents();
  {
    const auto at = rounded(*canvas->gradient_fill_handle_widget_position(opacity_midpoint));
    // The tip shows above the diamond, clear of the color midpoint below the line.
    QHelpEvent help(QEvent::ToolTip, at, canvas->mapToGlobal(at + QPoint(12, -52)));
    QApplication::sendEvent(canvas, &help);
    QApplication::processEvents();
    QWidget* tip = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("qtooltip_label") && widget->isVisible()) {
        tip = widget;
      }
    }
    if (tip != nullptr) {
      save_window_popup_artifact("ui_gradient_fill_handles_opacity_midpoint", window, *canvas, *tip);
    } else {
      save_widget_artifact("ui_gradient_fill_handles_opacity_midpoint", window);
    }
    QToolTip::hideText();
  }
  canvas->set_zoom_centered(1.0);
  QApplication::processEvents();

  // Still draggable, one undo step.
  const auto depth = fill.depth();
  const auto from = rounded(*canvas->gradient_fill_handle_widget_position(opacity_midpoint));
  const auto shift = canvas->widget_position_f(QPointF(150, 150)) - canvas->widget_position_f(QPointF(200, 150));
  drag(*canvas, from, rounded(QPointF(from) + shift));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Move Gradient Midpoint"));
  CHECK(std::abs(fill.gradient().alpha_stops[1].midpoint - 0.25F) < 0.02F);
  CHECK(canvas->gradient_fill_midpoint_dimmed(opacity_midpoint));

  // Once one stop's opacity differs it is a normal midpoint again.
  {
    auto& document = MainWindowTestAccess::document(window);
    auto* layer = document.find_layer(fill.id);
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      auto content = *std::as_const(*layer).vector_shape();
      content.fill.gradient.alpha_stops[1].opacity = 0.3F;
      layer->set_vector_shape(std::move(content));
      patchy::update_vector_shape_raster(*layer, patchy::Rect::from_size(document.width(), document.height()),
                                         &document.metadata().patterns);
      canvas->document_changed();
      QApplication::processEvents();
    }
  }
  CHECK(!canvas->gradient_fill_midpoint_dimmed(opacity_midpoint));
  CHECK(!canvas->gradient_fill_handle_tooltip(opacity_midpoint).contains(QStringLiteral("no effect yet")));
}

// GitHub issue 663, fourth follow-up: opacity on the canvas. The opacity popup from a
// double-click and from the menu (live preview, Enter commits one step, Esc restores, closing
// commits), a click on the opacity side adds an opacity stop under the plus cursor, and
// Opacity Here... on a color stop edits or inserts the opacity stop at its location.
void ui_gradient_fill_handles_set_opacity_on_the_canvas() {
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
  auto* handle = window.windowHandle();
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  const auto in_window = [&](QPointF widget_point) { return canvas->mapTo(&window, rounded(widget_point)); };
  const auto popup_spin = [&]() -> QSpinBox* {
    auto* popup = canvas->gradient_opacity_popup();
    CHECK(popup != nullptr && popup->isVisible());
    return popup != nullptr ? popup->findChild<QSpinBox*>(QStringLiteral("gradientOpacityStopSpin")) : nullptr;
  };
  const auto popup_key = [&](Qt::Key key) {
    auto* popup = canvas->gradient_opacity_popup();
    if (popup != nullptr && popup->windowHandle() != nullptr) {
      QTest::keyClick(popup->windowHandle(), key);
    }
    QApplication::processEvents();
  };

  // Double-click an opacity circle: the popup, live preview, Enter keeps it as one step.
  canvas->set_zoom_centered(1.5);
  QApplication::processEvents();
  auto depth = fill.depth();
  QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, in_window(part_position(*canvas, Kind::AlphaStop, 1)));
  QApplication::processEvents();
  auto* spin = popup_spin();
  CHECK(spin != nullptr && spin->value() == 100);
  if (spin != nullptr) {
    spin->setValue(40);
    QApplication::processEvents();
  }
  CHECK(std::abs(fill.gradient().alpha_stops[1].opacity - 0.4F) < 0.001F);  // previews live
  CHECK(fill.depth() == depth);
  // The preview recomposites in the background: let it land before the shot.
  process_events_until([&] { return canvas->render_settled(); }, 3000);
  if (auto* popup = canvas->gradient_opacity_popup(); popup != nullptr) {
    save_window_popup_artifact("ui_gradient_fill_handles_opacity_popup", window, *canvas, *popup);
  }
  popup_key(Qt::Key_Return);
  CHECK(canvas->gradient_opacity_popup() == nullptr);
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Change Opacity Stop"));
  CHECK(std::abs(fill.gradient().alpha_stops[1].opacity - 0.4F) < 0.001F);
  canvas->set_zoom_centered(1.0);
  QApplication::processEvents();

  // Right-click > Opacity...: Esc puts it back with no step.
  QTest::mouseClick(handle, Qt::RightButton, Qt::NoModifier, in_window(part_position(*canvas, Kind::AlphaStop, 1)));
  QApplication::processEvents();
  auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
  CHECK(menu != nullptr && menu->objectName() == QStringLiteral("gradientStopContextMenu"));
  if (menu != nullptr) {
    CHECK(!menu->actions().isEmpty() &&
          menu->actions().front()->objectName() == QStringLiteral("gradientStopOpacityAction"));
    CHECK(menu->findChild<QAction*>(QStringLiteral("gradientStopFillSettingsAction")) != nullptr);
    auto* opacity = menu->findChild<QAction*>(QStringLiteral("gradientStopOpacityAction"));
    menu->close();
    QApplication::processEvents();
    if (opacity != nullptr) {
      opacity->trigger();
    }
    QApplication::processEvents();
  }
  depth = fill.depth();
  spin = popup_spin();
  CHECK(spin != nullptr && spin->value() == 40);
  if (spin != nullptr) {
    spin->setValue(75);
    QApplication::processEvents();
  }
  CHECK(std::abs(fill.gradient().alpha_stops[1].opacity - 0.75F) < 0.001F);
  popup_key(Qt::Key_Escape);
  CHECK(canvas->gradient_opacity_popup() == nullptr);
  CHECK(std::abs(fill.gradient().alpha_stops[1].opacity - 0.4F) < 0.001F);
  CHECK(fill.depth() == depth);

  // Closing it any other way (a click away) keeps the value as one step.
  QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, in_window(part_position(*canvas, Kind::AlphaStop, 0)));
  QApplication::processEvents();
  spin = popup_spin();
  if (spin != nullptr) {
    spin->setValue(60);
    QApplication::processEvents();
  }
  if (auto* popup = canvas->gradient_opacity_popup(); popup != nullptr) {
    popup->close();
  }
  QApplication::processEvents();
  CHECK(fill.depth() == depth + 1);
  CHECK(std::abs(fill.gradient().alpha_stops[0].opacity - 0.6F) < 0.001F);

  // The opacity side of the line adds an opacity stop under the plus cursor; the line adds color.
  // At 35%, clear of the opacity midpoint (50%) that sits in the same band.
  const auto opacity_side = QPointF(widget_at(*canvas, QPointF(170, 150))) + QPointF(0.0, -12.0);
  CHECK(canvas->gradient_fill_handle_part_at(opacity_side).kind == Kind::AlphaLine);
  QTest::mouseMove(handle, in_window(opacity_side));
  QApplication::processEvents();
  CHECK(canvas->gradient_handle_cursor() == CanvasWidget::GradientHandleCursor::AddStop);
  depth = fill.depth();
  const auto color_count = fill.gradient().color_stops.size();
  window_click(*canvas, rounded(opacity_side));
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Add Opacity Stop"));
  CHECK(fill.gradient().alpha_stops.size() == 3U);
  CHECK(fill.gradient().color_stops.size() == color_count);
  CHECK(std::abs(fill.gradient().alpha_stops[1].location - 0.35F) < 0.02F);
  CHECK(canvas->gradient_fill_selected_stop() == (std::optional<Part>(Part{Kind::AlphaStop, 1})));
  window_click(*canvas, widget_at(*canvas, QPointF(150, 150)));
  CHECK(fill.label() == QStringLiteral("Add Color Stop"));
  CHECK(fill.gradient().alpha_stops.size() == 3U);

  // Opacity Here... on that new color stop (0.25, no opacity stop there) inserts one on commit.
  const auto opacity_here = [&](int color_index) {
    QTest::mouseClick(handle, Qt::RightButton, Qt::NoModifier,
                      in_window(part_position(*canvas, Kind::ColorStop, color_index)));
    QApplication::processEvents();
    auto* stop_menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    CHECK(stop_menu != nullptr);
    auto* action = stop_menu != nullptr
                       ? stop_menu->findChild<QAction*>(QStringLiteral("gradientStopOpacityHereAction"))
                       : nullptr;
    CHECK(action != nullptr);
    if (stop_menu != nullptr) {
      stop_menu->close();
      QApplication::processEvents();
    }
    if (action != nullptr) {
      action->trigger();
    }
    QApplication::processEvents();
  };
  depth = fill.depth();
  opacity_here(1);
  spin = popup_spin();
  if (spin != nullptr) {
    spin->setValue(30);
    QApplication::processEvents();
  }
  popup_key(Qt::Key_Return);
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Add Opacity Stop"));
  CHECK(fill.gradient().alpha_stops.size() == 4U);
  CHECK(std::abs(fill.gradient().alpha_stops[1].location - 0.25F) < 0.01F);
  CHECK(std::abs(fill.gradient().alpha_stops[1].opacity - 0.3F) < 0.001F);
  // On the first color stop an opacity stop already sits at 0: it is edited, nothing added.
  depth = fill.depth();
  opacity_here(0);
  spin = popup_spin();
  CHECK(spin != nullptr && spin->value() == 60);
  if (spin != nullptr) {
    spin->setValue(90);
    QApplication::processEvents();
  }
  popup_key(Qt::Key_Return);
  CHECK(fill.depth() == depth + 1);
  CHECK(fill.label() == QStringLiteral("Change Opacity Stop"));
  CHECK(fill.gradient().alpha_stops.size() == 4U);
  CHECK(std::abs(fill.gradient().alpha_stops[0].opacity - 0.9F) < 0.001F);
  // Cancelled, it adds nothing.
  window_click(*canvas, widget_at(*canvas, QPointF(270, 150)));
  const auto alpha_count = fill.gradient().alpha_stops.size();
  depth = fill.depth();
  opacity_here(2);
  spin = popup_spin();
  if (spin != nullptr) {
    spin->setValue(10);
    QApplication::processEvents();
  }
  popup_key(Qt::Key_Escape);
  CHECK(fill.depth() == depth);
  CHECK(fill.gradient().alpha_stops.size() == alpha_count);
}

// GitHub issue 663, one gradient editor: the tool's two-track gradient persists under
// tools/gradientDefinition beside the flat tools/gradientStops (kept for older builds). Old
// settings with only flat stops still load; flat stops changed by an older build win.
void ui_gradient_tool_definition_settings_round_trip() {
  const GradientSettingsGuard settings_guard;
  patchy::GradientDefinition preset;
  for (const auto& builtin : patchy::builtin_gradient_presets()) {
    if (std::string_view(builtin.english_name) == "Sepia Wash") {
      preset = builtin.definition;
    }
  }
  CHECK(preset.color_stops.size() == 3U);
  preset.smoothness = 3000;
  preset.alpha_stops = {{0.0F, 1.0F, 0.5F}, {0.6F, 0.4F, 0.3F}, {1.0F, 1.0F, 0.5F}};
  preset.color_stops[1].kind = patchy::GradientColorStop::Kind::Foreground;
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    canvas->set_gradient_definition(preset);
    MainWindowTestAccess::save_tool_settings(window);
  }
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("tools/gradientUseCustomStops")).toBool());
  CHECK(!settings.value(QStringLiteral("tools/gradientDefinition")).toByteArray().isEmpty());
  const auto flat = settings.value(QStringLiteral("tools/gradientStops")).toByteArray();
  CHECK(!flat.isEmpty());
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    (void)window;
    CHECK(canvas->gradient_definition().has_value() && *canvas->gradient_definition() == preset);
  }
  // An older build rewrote only the flat stops: they win.
  settings.setValue(QStringLiteral("tools/gradientStops"),
                    QByteArray("[{\"location\":0,\"r\":10,\"g\":20,\"b\":30,\"a\":255},"
                               "{\"location\":1,\"r\":40,\"g\":50,\"b\":60,\"a\":128}]"));
  settings.sync();
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    (void)window;
    CHECK(canvas->gradient_definition().has_value());
    if (canvas->gradient_definition().has_value()) {
      const auto& loaded = *canvas->gradient_definition();
      CHECK(loaded.smoothness == 0);
      CHECK(loaded.color_stops.size() == 2U && loaded.alpha_stops.size() == 2U);
      CHECK(loaded.color_stops[0].color == (patchy::RgbColor{10, 20, 30}));
      CHECK(std::abs(loaded.alpha_stops[1].opacity - 128.0F / 255.0F) < 1e-4F);
    }
  }
  // Settings from before the new key (flat stops only) load the same way.
  settings.remove(QStringLiteral("tools/gradientDefinition"));
  settings.sync();
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    (void)window;
    CHECK(canvas->gradient_definition().has_value() && canvas->gradient_definition()->color_stops.size() == 2U);
  }
}

std::vector<patchy::test::TestCase> gradient_fill_handles_tests() {
  return {
      {"ui_gradient_tool_definition_settings_round_trip", ui_gradient_tool_definition_settings_round_trip},
      {"ui_gradient_fill_handles_set_opacity_on_the_canvas", ui_gradient_fill_handles_set_opacity_on_the_canvas},
      {"ui_gradient_fill_handles_name_and_dim_the_opacity_midpoint",
       ui_gradient_fill_handles_name_and_dim_the_opacity_midpoint},
      {"ui_gradient_fill_handles_delete_stops_and_show_cursors", ui_gradient_fill_handles_delete_stops_and_show_cursors},
      {"ui_gradient_fill_handles_edit_a_linear_fill", ui_gradient_fill_handles_edit_a_linear_fill},
      {"ui_gradient_fill_handles_follow_a_radial_fill_in_a_rotated_view",
       ui_gradient_fill_handles_follow_a_radial_fill_in_a_rotated_view},
      {"ui_gradient_fill_handles_show_a_presets_own_stops", ui_gradient_fill_handles_show_a_presets_own_stops},
      {"ui_gradient_fill_handles_drag_timing", ui_gradient_fill_handles_drag_timing},
      {"ui_gradient_fill_handle_drag_previews_at_view_resolution",
       ui_gradient_fill_handle_drag_previews_at_view_resolution},
      {"ui_gradient_fill_handle_view_preview_follows_zoom_and_rotation",
       ui_gradient_fill_handle_view_preview_follows_zoom_and_rotation},
      {"ui_gradient_fill_handle_drag_coalesces_queued_moves", ui_gradient_fill_handle_drag_coalesces_queued_moves},
  };
}
