// The stay-open Shape Appearance window (GitHub issue 694, docs/vector-tools-appearance-window.md):
// it opens without a dialog loop or the edit lock, applies each change at once as one undo
// step per gesture, follows the selected shape layers, and stays safe while the selection,
// the history and the documents change under it. Its Show gradient handles box puts the
// Gradient tool's handles on the canvas under the other tools (docs/gradients-fill-handles.md).
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_panel.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineF>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

using Access = patchy::ui::MainWindowTestAccess;
using patchy::VectorFillKind;

patchy::LayerId make_rect(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  drag(canvas, canvas.widget_position_for_document_point(from), canvas.widget_position_for_document_point(to));
  QApplication::processEvents();
  const auto active = Access::document(window).active_layer_id();
  CHECK(active.has_value());
  return *active;
}

const patchy::VectorShapeContent& shape_of(const patchy::Document& document, patchy::LayerId id) {
  const auto* layer = document.find_layer(id);
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  return *layer->vector_shape();
}

QDialog* panel_window(patchy::ui::MainWindow& window) {
  return window.findChild<QDialog*>(QStringLiteral("shapeAppearanceWindow"));
}

// The window's current page; a replaced page leaves the window at once.
QWidget* page_of(QDialog& panel) {
  return panel.findChild<QWidget*>(QStringLiteral("shapeAppearanceBody"));
}

template <typename Widget>
Widget* field(QDialog& panel, const char* name) {
  auto* widget = panel.findChild<Widget*>(QLatin1String(name));
  CHECK(widget != nullptr);
  return widget;
}

QDialog* open_panel(patchy::ui::MainWindow& window) {
  Access::open_shape_appearance_window(window);
  QApplication::processEvents();
  auto* panel = panel_window(window);
  CHECK(panel != nullptr && panel->isVisible());
  return panel;
}

bool panel_usable(QDialog& panel) {
  auto* page = page_of(panel);
  auto* reason = panel.findChild<QLabel*>(QStringLiteral("brushToolWindowUnavailableLabel"));
  return page != nullptr && page->isEnabled() && reason != nullptr && reason->isHidden();
}

QString panel_reason(QDialog& panel) {
  auto* reason = panel.findChild<QLabel*>(QStringLiteral("brushToolWindowUnavailableLabel"));
  return reason != nullptr && !reason->isHidden() ? reason->text() : QString();
}

void select_only(patchy::ui::MainWindow& window, patchy::LayerId id) {
  Access::select_layers(window, {id}, id);
  QApplication::processEvents();
}

// Every entry point opens the window and returns: the document is not locked, a second
// shape can be drawn with it open, and the window then edits that one.
void ui_shape_appearance_window_stays_open_while_editing() {
  VectorSettingsGuard settings_guard;
  // With nothing saved, Show gradient handles starts ticked (checked below).
  SettingsValueRestorer handles_setting{QStringLiteral("shapeAppearance/showGradientHandles")};
  patchy::ui::app_settings().remove(QStringLiteral("shapeAppearance/showGradientHandles"));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = Access::document(window);
  const auto first = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));

  require_action(window, "layerShapeAppearanceAction")->trigger();
  QApplication::processEvents();
  auto* panel = panel_window(window);
  CHECK(panel != nullptr && panel->isVisible());
  if (panel == nullptr) return;
  // No modal dialog and no edit lock.
  CHECK(find_top_level_dialog(QStringLiteral("shapeAppearanceDialog")) == nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("Undo"))->isEnabled());
  CHECK(panel_usable(*panel));
  CHECK(panel->windowTitle() == QStringLiteral("Shape Appearance"));
  // Close and Reset; no OK, Cancel or Preview.
  CHECK(panel->findChild<QCheckBox*>(QStringLiteral("shapeAppearancePreviewCheck")) == nullptr);
  auto* buttons = page_of(*panel)->findChild<QDialogButtonBox*>();
  CHECK(buttons != nullptr);
  CHECK(buttons->button(QDialogButtonBox::Ok) == nullptr && buttons->button(QDialogButtonBox::Cancel) == nullptr);
  CHECK(buttons->button(QDialogButtonBox::Close) != nullptr);
  CHECK(panel->findChild<QPushButton*>(QStringLiteral("shapeAppearanceResetButton")) != nullptr);
  // Show gradient handles: ticked by default, greyed out on this solid shape, and the canvas
  // is told at once (it shows nothing until the fill is a gradient).
  auto* handles_box = panel->findChild<QCheckBox*>(QStringLiteral("shapeAppearanceGradientHandlesCheck"));
  CHECK(handles_box != nullptr && handles_box->isChecked() && !handles_box->isEnabled());
  CHECK(canvas->gradient_handles_forced());
  CHECK(!canvas->gradient_fill_handles().has_value());

  // Draw a second shape with the window open: it becomes the window's target.
  const auto second = make_rect(window, *canvas, QPoint(320, 240), QPoint(420, 300));
  CHECK(second != first);
  QApplication::processEvents();
  CHECK(panel_usable(*panel));
  field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(4.0);
  CHECK(shape_of(document, second).stroke.enabled);
  CHECK(!shape_of(document, first).stroke.enabled);

  // The other entry points bring the same window forward.
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  auto* appearance_button = window.findChild<QToolButton*>(QStringLiteral("vectorAppearanceButton"));
  CHECK(appearance_button != nullptr && appearance_button->isEnabled());
  appearance_button->click();
  QApplication::processEvents();
  CHECK(panel_window(window) == panel && panel->isVisible());
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  send_double_click(*canvas, canvas->widget_position_for_document_point(QPoint(370, 270)));
  QApplication::processEvents();
  CHECK(panel_window(window) == panel && panel->isVisible());

  // Close hides it; the command shows it again.
  page_of(*panel)->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close)->click();
  QApplication::processEvents();
  CHECK(!panel->isVisible());
  require_action(window, "layerShapeAppearanceAction")->trigger();
  QApplication::processEvents();
  CHECK(panel->isVisible() && panel_usable(*panel));
  save_widget_artifact("issue694_shape_appearance_window", *panel);
}

// A change applies at once. The values of one gesture share one undo step; another
// property starts the next; Undo steps back and the fields follow.
void ui_shape_appearance_window_records_one_undo_step_per_gesture() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = Access::document(window);
  const auto id = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  auto* panel = open_panel(window);
  if (panel == nullptr) return;
  const auto depth = Access::active_session_undo_depth(window);

  auto* width = field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin");
  CHECK(width->value() == 0.0);
  width->setValue(4.0);
  CHECK(shape_of(document, id).stroke.enabled);
  CHECK(std::abs(shape_of(document, id).stroke.width - 4.0) < 1e-9);
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  CHECK(Access::current_history_label(window) == QStringLiteral("Shape appearance"));
  // More values of the same field (a scrub, a held stepper): the same step.
  width->setValue(5.0);
  width->setValue(9.0);
  CHECK(std::abs(shape_of(document, id).stroke.width - 9.0) < 1e-9);
  CHECK(Access::active_session_undo_depth(window) == depth + 1);

  // Another property is another step.
  auto* align = field<QComboBox>(*panel, "shapeStrokeAlignCombo");
  align->setCurrentIndex(align->findData(static_cast<int>(patchy::VectorStrokeAlignment::Center)));
  CHECK(shape_of(document, id).stroke.alignment == patchy::VectorStrokeAlignment::Center);
  CHECK(Access::active_session_undo_depth(window) == depth + 2);
  CHECK(panel_usable(*panel));

  // An edit made elsewhere ends the gesture: the next value of the same field is a new step.
  Access::push_undo_snapshot(window, QStringLiteral("Something else"));
  align->setCurrentIndex(align->findData(static_cast<int>(patchy::VectorStrokeAlignment::Outside)));
  CHECK(shape_of(document, id).stroke.alignment == patchy::VectorStrokeAlignment::Outside);
  CHECK(Access::active_session_undo_depth(window) == depth + 4);

  // Undo, through the window's steps; the page is rebuilt from the layer each time.
  Access::undo(window);
  QApplication::processEvents();
  CHECK(shape_of(document, id).stroke.alignment == patchy::VectorStrokeAlignment::Center);
  Access::undo(window);
  Access::undo(window);
  QApplication::processEvents();
  CHECK(shape_of(document, id).stroke.alignment != patchy::VectorStrokeAlignment::Center);
  CHECK(std::abs(shape_of(document, id).stroke.width - 9.0) < 1e-9);
  CHECK(field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->value() == 9.0);
  Access::undo(window);
  QApplication::processEvents();
  CHECK(!shape_of(document, id).stroke.enabled);
  CHECK(Access::active_session_undo_depth(window) == depth);
  CHECK(panel_usable(*panel));
  CHECK(field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->value() == 0.0);
}

// Selecting another shape rebuilds the page for it (a gradient fill gets the gradient rows,
// several layers get the summary); a layer the window cannot edit greys it out with the
// reason, and nothing a stale field sends is applied.
void ui_shape_appearance_window_reconfigures_for_the_selected_shape() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = Access::document(window);
  const auto background = document.active_layer_id();
  CHECK(background.has_value());
  const auto solid = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  const auto gradient = make_rect(window, *canvas, QPoint(320, 240), QPoint(420, 300));
  auto* panel = open_panel(window);
  if (panel == nullptr || !background.has_value()) return;

  // Make the second shape's fill a gradient, through the window.
  auto* kind = field<QComboBox>(*panel, "shapeFillKindCombo");
  CHECK(kind->currentData().toInt() == static_cast<int>(VectorFillKind::Solid));
  CHECK(!field<QComboBox>(*panel, "shapeFillGradientCombo")->isVisibleTo(panel));
  kind->setCurrentIndex(kind->findData(static_cast<int>(VectorFillKind::Gradient)));
  CHECK(shape_of(document, gradient).fill.kind == VectorFillKind::Gradient);
  CHECK(field<QComboBox>(*panel, "shapeFillGradientCombo")->isVisibleTo(panel));
  field<QSpinBox>(*panel, "shapeGradientAngleSpin")->setValue(35);
  CHECK(std::abs(shape_of(document, gradient).fill.gradient.angle_degrees - 35.0) < 1e-6);
  const QPointer<QWidget> gradient_page(page_of(*panel));

  // The solid shape: a new page, solid rows.
  select_only(window, solid);
  CHECK(panel_usable(*panel));
  CHECK(page_of(*panel) != gradient_page.data());
  CHECK(field<QComboBox>(*panel, "shapeFillKindCombo")->currentData().toInt() ==
        static_cast<int>(VectorFillKind::Solid));
  CHECK(!field<QComboBox>(*panel, "shapeFillGradientCombo")->isVisibleTo(panel));
  CHECK(panel->findChild<QLabel*>(QStringLiteral("shapeAppearanceSelectionSummary")) == nullptr);

  // Back to the gradient shape: its rows and its angle.
  select_only(window, gradient);
  CHECK(field<QComboBox>(*panel, "shapeFillKindCombo")->currentData().toInt() ==
        static_cast<int>(VectorFillKind::Gradient));
  CHECK(field<QComboBox>(*panel, "shapeFillGradientCombo")->isVisibleTo(panel));
  CHECK(field<QSpinBox>(*panel, "shapeGradientAngleSpin")->value() == 35);
  // Once the new page's layout has settled its columns fit the window: nothing is cut off
  // at the right, with or without a scroll bar.
  auto* scroll = field<QScrollArea>(*panel, "shapeAppearanceScroll");
  CHECK(scroll->widget() != nullptr);
  for (int pass = 0; pass < 5; ++pass) QApplication::processEvents();
  CHECK(scroll->widget()->width() <= scroll->viewport()->width());
  CHECK(scroll->viewport()->width() >= scroll->widget()->minimumSizeHint().width());
  save_widget_artifact("issue694_shape_appearance_window_gradient", *panel);

  // Both shapes: the batch summary, and an edit reaches both.
  Access::select_layers(window, {solid, gradient}, gradient);
  QApplication::processEvents();
  CHECK(panel_usable(*panel));
  CHECK(panel->findChild<QLabel*>(QStringLiteral("shapeAppearanceSelectionSummary")) != nullptr);
  field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(3.0);
  CHECK(shape_of(document, solid).stroke.enabled && shape_of(document, gradient).stroke.enabled);

  // A pixel layer: greyed out with the reason, and a stale field applies nothing.
  auto* stale_width = field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin");
  select_only(window, *background);
  CHECK(!panel_usable(*panel));
  CHECK(panel_reason(*panel).contains(QStringLiteral("shape layer")));
  const auto depth = Access::active_session_undo_depth(window);
  if (page_of(*panel) != nullptr && stale_width == panel->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"))) {
    stale_width->setValue(12.0);
    QApplication::processEvents();
  }
  CHECK(Access::active_session_undo_depth(window) == depth);
  CHECK(std::abs(shape_of(document, solid).stroke.width - 3.0) < 1e-9);

  select_only(window, solid);
  CHECK(panel_usable(*panel));
  CHECK(field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->value() == 3.0);
}

// Another document, a closed document and a deleted layer under the open window, each with
// a gesture still open: nothing is written to the wrong place and nothing dangles.
void ui_shape_appearance_window_survives_document_switch_close_and_layer_delete() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& first_document = Access::document(window);
  const auto id = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  auto* panel = open_panel(window);
  if (panel == nullptr) return;

  // A gesture, then a second document (no shape there).
  field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(3.0);
  Access::create_document(window, 320, 240);
  QApplication::processEvents();
  auto* second_canvas = require_canvas(window);
  CHECK(second_canvas != canvas);
  CHECK(Access::session_count(window) == 2U);
  CHECK(!panel_usable(*panel));
  const auto second_depth = Access::active_session_undo_depth(window);
  CHECK(std::abs(shape_of(first_document, id).stroke.width - 3.0) < 1e-9);

  // Back on the first document the window shows its shape again.
  Access::activate_canvas(window, canvas);
  QApplication::processEvents();
  CHECK(panel_usable(*panel));
  CHECK(field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->value() == 3.0);

  // Delete the target with a gesture open, then bring it back.
  field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(6.0);
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(first_document).find_layer(id) == nullptr);
  CHECK(!panel_usable(*panel));
  Access::undo(window);
  QApplication::processEvents();
  CHECK(std::abs(shape_of(first_document, id).stroke.width - 6.0) < 1e-9);
  select_only(window, id);
  CHECK(panel_usable(*panel));

  // Close the document with a gesture open.
  field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(8.0);
  Access::mark_active_session_saved(window);
  CHECK(Access::close_document_tab(window, 0));
  QApplication::processEvents();
  CHECK(Access::session_count(window) == 1U);
  CHECK(!panel_usable(*panel));
  CHECK(Access::active_session_undo_depth(window) == second_depth);

  // No document at all.
  Access::mark_active_session_saved(window);
  CHECK(Access::close_document_tab(window, 0));
  QApplication::processEvents();
  CHECK(Access::session_count(window) == 0U);
  CHECK(!panel_usable(*panel));
  CHECK(panel_reason(*panel).contains(QStringLiteral("document")));
}

// Another dialog's edit lock greys the window out for as long as it is held, and a field
// changed meanwhile applies nothing.
void ui_shape_appearance_window_is_disabled_under_another_dialogs_lock() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = Access::document(window);
  const auto id = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  auto* panel = open_panel(window);
  if (panel == nullptr) return;
  const auto depth = Access::active_session_undo_depth(window);

  bool checked = false;
  when_dialog_visible(QStringLiteral("shapeAppearanceDialog"), [&](QDialog& modal) {
    checked = true;
    CHECK(!panel_usable(*panel));
    CHECK(panel_reason(*panel).contains(QStringLiteral("dialog")));
    field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(7.0);
    QApplication::processEvents();
    CHECK(!shape_of(document, id).stroke.enabled);
    modal.reject();
  });
  Access::edit_active_shape_appearance(window);  // the modal dialog, on the same shape
  QApplication::processEvents();
  CHECK(checked);
  CHECK(panel_usable(*panel));
  CHECK(!shape_of(document, id).stroke.enabled);
  CHECK(Access::active_session_undo_depth(window) == depth);
}

// A color picker opened from the window locks the document while it is up and keeps the
// page it was opened from: Cancel puts the color back with no undo step, OK is one step.
void ui_shape_appearance_window_color_picker_commits_one_step() {
  VectorSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = Access::document(window);
  const auto id = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  auto* panel = open_panel(window);
  if (panel == nullptr) return;
  const auto original = shape_of(document, id).fill.color;
  const auto depth = Access::active_session_undo_depth(window);
  const QPointer<QWidget> page(page_of(*panel));

  const auto pick = [&](bool accept) {
    bool previewed = false;
    QTimer::singleShot(0, guarded([&, accept] {
      auto* picker_dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
      CHECK(picker_dialog != nullptr);
      if (picker_dialog == nullptr) return;
      auto* picker = picker_dialog->findChild<patchy::ui::PatchyColorPicker*>(
          QStringLiteral("patchyAdvancedColorPicker"));
      CHECK(picker != nullptr);
      // Locked while the picker is up, and the page stays.
      CHECK(!require_action_by_text(window, QStringLiteral("Undo"))->isEnabled());
      picker->setCurrentColor(QColor(30, 40, 230));
      previewed = process_events_until([&] { return shape_of(document, id).fill.color.blue == 230; });
      CHECK(page != nullptr && page.data() == page_of(*panel));
      if (accept) {
        picker_dialog->accept();
      } else {
        picker_dialog->reject();
      }
    }));
    field<QPushButton>(*panel, "shapeFillColorButton")->click();
    QApplication::processEvents();
    return previewed;
  };

  CHECK(pick(false));
  CHECK(shape_of(document, id).fill.color == original);
  CHECK(Access::active_session_undo_depth(window) == depth);
  CHECK(panel_usable(*panel));
  CHECK(page != nullptr && page.data() == page_of(*panel));

  CHECK(pick(true));
  CHECK(shape_of(document, id).fill.color.blue == 230);
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  CHECK(panel_usable(*panel));
  CHECK(page != nullptr && page.data() == page_of(*panel));
  // A value after the picker is its own step, on the committed color.
  field<QDoubleSpinBox>(*panel, "shapeStrokeWidthSpin")->setValue(2.0);
  CHECK(shape_of(document, id).fill.color.blue == 230);
  CHECK(shape_of(document, id).stroke.enabled);
  CHECK(Access::active_session_undo_depth(window) == depth + 2);
}

// What one value of a gesture costs on a large gradient shape (each is baked on the UI
// thread). Prints; the only check is that the values land as one step.
void ui_shape_appearance_window_step_timing() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer gradient_mode{QStringLiteral("tools/gradientMode")};
  SettingsValueRestorer active_tool{QStringLiteral("tools/activeTool")};
  auto&& [window, first_canvas] = ShownWindowFixture{};
  static_cast<void>(first_canvas);
  Access::create_document(window, 4000, 3000);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.1);
  QApplication::processEvents();
  auto& document = Access::document(window);
  const auto background = document.active_layer_id();
  CHECK(background.has_value());
  const auto id = make_rect(window, *canvas, QPoint(200, 200), QPoint(3800, 2800));
  auto* panel = open_panel(window);
  if (panel == nullptr || !background.has_value()) return;
  auto* kind = field<QComboBox>(*panel, "shapeFillKindCombo");
  kind->setCurrentIndex(kind->findData(static_cast<int>(VectorFillKind::Gradient)));
  CHECK(shape_of(document, id).fill.kind == VectorFillKind::Gradient);
  const auto depth = Access::active_session_undo_depth(window);
  auto* angle = field<QSpinBox>(*panel, "shapeGradientAngleSpin");
  constexpr int kSteps = 8;
  QElapsedTimer timer;
  timer.start();
  for (int step = 1; step <= kSteps; ++step) {
    angle->setValue(step * 10);
  }
  const auto elapsed = timer.elapsed();
  std::fprintf(stderr, "[shape-appearance-window] 4000x3000 gradient shape: %.1f ms per value (%d values)\n",
               static_cast<double>(elapsed) / kSteps, kSteps);
  CHECK(std::abs(shape_of(document, id).fill.gradient.angle_degrees - kSteps * 10.0) < 1e-6);
  CHECK(Access::active_session_undo_depth(window) == depth + 1);

  // A Gradient Fill layer (no path) takes the draft bake while the gesture lasts, and the
  // window is titled Fill Layer Settings for it. Drawn with the background active: on the
  // gradient shape the drag would set that shape's gradient.
  select_only(window, *background);
  require_action(window, "toolGradientAction")->trigger();
  QApplication::processEvents();
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("gradientModeCombo"));
  CHECK(mode != nullptr);
  if (mode == nullptr) return;
  mode->setCurrentIndex(1);  // Fill Layer
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(400, 400)),
       canvas->widget_position_for_document_point(QPoint(3400, 2400)));
  QApplication::processEvents();
  const auto fill_id = document.active_layer_id();
  CHECK(fill_id.has_value() && *fill_id != id);
  if (!fill_id.has_value()) return;
  CHECK(panel_usable(*panel));
  CHECK(panel->windowTitle() == QStringLiteral("Fill Layer Settings"));
  auto* title = panel->findChild<QLabel*>(QStringLiteral("dialogChromeTitleLabel"));
  CHECK(title != nullptr && title->text() == QStringLiteral("Fill Layer Settings"));
  const auto fill_depth = Access::active_session_undo_depth(window);
  auto* fill_angle = field<QSpinBox>(*panel, "shapeGradientAngleSpin");
  const int start = fill_angle->value();
  timer.restart();
  for (int step = 1; step <= kSteps; ++step) {
    fill_angle->setValue(start + step * 7);
  }
  const auto fill_elapsed = timer.elapsed();
  std::fprintf(stderr, "[shape-appearance-window] 4000x3000 Gradient Fill layer: %.1f ms per value (%d values)\n",
               static_cast<double>(fill_elapsed) / kSteps, kSteps);
  CHECK(Access::active_session_undo_depth(window) == fill_depth + 1);
  // The gesture's end (here another edit's undo snapshot) bakes the last value exactly.
  Access::push_undo_snapshot(window, QStringLiteral("Something else"));
  const auto* fill_layer = std::as_const(document).find_layer(*fill_id);
  CHECK(fill_layer != nullptr);
  if (fill_layer == nullptr) return;
  auto exact = *fill_layer;
  patchy::update_vector_shape_raster(exact, patchy::Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  CHECK(exact.pixels().width() == fill_layer->pixels().width());
  bool same = exact.pixels().width() == fill_layer->pixels().width() &&
              exact.pixels().height() == fill_layer->pixels().height();
  for (int y = 0; same && y < exact.pixels().height(); y += 97) {
    for (int x = 0; same && x < exact.pixels().width(); x += 89) {
      const auto a = exact.pixels().pixel(x, y);
      const auto b = fill_layer->pixels().pixel(x, y);
      same = a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
    }
  }
  CHECK(same);
}

// The window's Show gradient handles box: the Gradient tool's handles on the active gradient
// shape under the shape tools, Path Select and the Move tool. A press on a handle part edits
// the gradient as one undo step; any other press, and every key the handles do not own, is
// the tool's. Unticking the box or closing the window takes them away again.
void ui_shape_appearance_window_shows_gradient_handles_under_other_tools() {
  using Kind = patchy::ui::CanvasWidget::GradientHandlePartKind;
  using Part = patchy::ui::CanvasWidget::GradientHandlePart;
  using Cursor = patchy::ui::CanvasWidget::GradientHandleCursor;
  using patchy::ui::CanvasTool;
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer active_tool{QStringLiteral("tools/activeTool")};
  SettingsValueRestorer handles_setting{QStringLiteral("shapeAppearance/showGradientHandles")};
  patchy::ui::app_settings().setValue(QStringLiteral("shapeAppearance/showGradientHandles"), false);
  auto&& [window, canvas] = ShownWindowFixture{};
  auto& document = Access::document(window);
  const auto id = make_rect(window, *canvas, QPoint(100, 100), QPoint(300, 220));
  auto* panel = open_panel(window);
  if (panel == nullptr) return;
  // A page is rebuilt after each handle edit, so the box is looked up each time.
  const auto box = [&] { return field<QCheckBox>(*panel, "shapeAppearanceGradientHandlesCheck"); };
  const auto rounded = [](QPointF point) {
    return QPoint(static_cast<int>(std::lround(point.x())), static_cast<int>(std::lround(point.y())));
  };
  const auto part_at = [&](Kind kind, int index = -1) {
    const auto position = canvas->gradient_fill_handle_widget_position(Part{kind, index});
    CHECK(position.has_value());
    return rounded(position.value_or(QPointF()));
  };
  const auto line = [&] {
    const auto handles = canvas->gradient_fill_handles();
    CHECK(handles.has_value());
    return handles.has_value() ? handles->line : patchy::GradientFillLine{};
  };
  const auto start_of = [&] { return QPointF(line().start_x, line().start_y); };
  const auto end_of = [&] { return QPointF(line().end_x, line().end_y); };
  const auto depth = [&] { return Access::active_session_undo_depth(window); };
  const auto gradient = [&] { return shape_of(document, id).fill.gradient; };
  // A click and a key the way the window system delivers them (through the top-level window).
  auto* top = canvas->window()->windowHandle();
  CHECK(top != nullptr);
  if (top == nullptr) return;
  const auto window_click = [&](QPoint position) {
    const auto in_window = canvas->mapTo(canvas->window(), position);
    QTest::mouseMove(top, in_window);
    QTest::mousePress(top, Qt::LeftButton, Qt::NoModifier, in_window);
    QTest::mouseRelease(top, Qt::LeftButton, Qt::NoModifier, in_window);
    QApplication::processEvents();
  };

  // A solid fill has no handles: the box is there, greyed out.
  CHECK(!box()->isEnabled() && !box()->isChecked());
  auto* kind = field<QComboBox>(*panel, "shapeFillKindCombo");
  kind->setCurrentIndex(kind->findData(static_cast<int>(VectorFillKind::Gradient)));
  CHECK(shape_of(document, id).fill.kind == VectorFillKind::Gradient);
  CHECK(box()->isEnabled());
  // Unticked: a shape tool shows no gradient handles, as before.
  CHECK(canvas->tool() == CanvasTool::Rectangle);
  CHECK(!canvas->gradient_handles_forced());
  CHECK(!canvas->gradient_fill_handles().has_value());

  box()->click();
  QApplication::processEvents();
  CHECK(canvas->gradient_handles_forced());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("shapeAppearance/showGradientHandles")).toBool());
  CHECK(canvas->gradient_fill_handles().has_value());
  if (!canvas->gradient_fill_handles().has_value()) return;
  CHECK(canvas->gradient_fill_handles()->layer_id == id);

  // The Rectangle tool: a drag of the end handle moves the end, as one undo step, and draws
  // no shape.
  auto before = depth();
  const auto start_before = start_of();
  const auto end_before = end_of();
  const auto end_widget = part_at(Kind::End);
  drag(*canvas, end_widget, end_widget + QPoint(34, 22));
  QApplication::processEvents();
  CHECK(depth() == before + 1);
  CHECK(Access::current_history_label(window) == QStringLiteral("Move Gradient End Point"));
  CHECK(document.active_layer_id() == std::optional(id));
  CHECK(QLineF(end_of(), end_before).length() > 10.0);
  CHECK(QLineF(start_of(), start_before).length() < 1.5);
  // The window follows: a new page with the new angle, the box still ticked.
  for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
  CHECK(panel_usable(*panel));
  CHECK(box()->isChecked() && box()->isEnabled());
  const auto shown_angle = field<QSpinBox>(*panel, "shapeGradientAngleSpin")->value();
  const auto angle_off = std::fmod(std::abs(shown_angle - static_cast<double>(gradient().angle_degrees)), 360.0);
  CHECK(angle_off <= 1.0 || angle_off >= 359.0);
  // Undo puts the end back and the handles follow.
  Access::undo(window);
  QApplication::processEvents();
  CHECK(QLineF(end_of(), end_before).length() < 1.5);

  // A press away from every handle part is the tool's: it draws a shape.
  const auto drawn = make_rect(window, *canvas, QPoint(330, 240), QPoint(400, 290));
  CHECK(drawn != id);
  // That shape is solid: no handles, and the box greys out with the new page.
  for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
  CHECK(!canvas->gradient_fill_handles().has_value());
  CHECK(!box()->isEnabled() && box()->isChecked());
  select_only(window, id);
  CHECK(canvas->gradient_fill_handles().has_value());

  // Path Select: a click on the shape away from the handles selects its path; a click on
  // the gradient line then adds a color stop and selects it, and Delete removes that stop,
  // not the path's anchor points.
  require_action(window, "toolPathSelectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::PathSelect);
  CHECK(canvas->gradient_fill_handles().has_value());
  const auto corner = canvas->widget_position_for_document_point(QPoint(108, 108));
  CHECK(canvas->gradient_fill_handle_part_at(QPointF(corner)).kind == Kind::None);
  window_click(corner);
  CHECK(canvas->path_edit_has_selection());
  const auto path_before = shape_of(document, id).path;
  const auto middle = rounded((QPointF(part_at(Kind::Start)) + QPointF(part_at(Kind::End))) / 2.0);
  CHECK(canvas->gradient_fill_handle_part_at(QPointF(middle)).kind == Kind::Line);
  before = depth();
  window_click(middle);
  CHECK(depth() == before + 1);
  CHECK(Access::current_history_label(window) == QStringLiteral("Add Color Stop"));
  CHECK(gradient().color_stops.size() == 3U);
  CHECK(canvas->gradient_fill_selected_stop() == (std::optional<Part>(Part{Kind::ColorStop, 1})));
  CHECK(canvas->path_edit_has_selection());  // the path is still selected: Delete has two takers
  QTest::keyClick(top, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(depth() == before + 2);
  CHECK(Access::current_history_label(window) == QStringLiteral("Delete Color Stop"));
  CHECK(gradient().color_stops.size() == 2U);
  CHECK(std::as_const(document).find_layer(id) != nullptr);
  CHECK(shape_of(document, id).path == path_before);

  // The Move tool: the start handle moves the gradient's start, not the layer. Over a
  // handle the cursor is the handle's; with Ctrl held (the tool's own press) it is not.
  require_action(window, "toolMoveAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Move);
  CHECK(canvas->gradient_fill_handles().has_value());
  send_mouse(*canvas, QEvent::MouseMove, part_at(Kind::End), Qt::NoButton, Qt::NoButton);
  CHECK(canvas->gradient_handle_cursor() == Cursor::Move);
  send_mouse(*canvas, QEvent::MouseMove, part_at(Kind::End), Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
  CHECK(canvas->gradient_handle_cursor() == Cursor::None);
  before = depth();
  const auto start_widget = part_at(Kind::Start);
  const auto start_then = start_of();
  drag(*canvas, start_widget, start_widget + QPoint(-18, 12));
  QApplication::processEvents();
  CHECK(depth() == before + 1);
  CHECK(Access::current_history_label(window) == QStringLiteral("Move Gradient Start Point"));
  CHECK(QLineF(start_of(), start_then).length() > 8.0);
  CHECK(shape_of(document, id).path == path_before);
  save_widget_artifact("issue694_gradient_handles_move_tool", window);

  // The view tools own every press, so no handles show under them; the Gradient tool keeps
  // its own.
  canvas->set_tool(CanvasTool::Zoom);
  CHECK(!canvas->gradient_fill_handles().has_value());
  canvas->set_tool(CanvasTool::Gradient);
  CHECK(canvas->gradient_fill_handles().has_value());
  canvas->set_tool(CanvasTool::Move);
  CHECK(canvas->gradient_fill_handles().has_value());

  // Unticking mid-drag puts the gradient back and records nothing; the release is inert.
  for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
  before = depth();
  const auto resting = gradient();
  const auto grab = part_at(Kind::End);
  send_mouse(*canvas, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, grab + QPoint(25, 25), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!(gradient() == resting));  // the drag previews live
  box()->click();
  QApplication::processEvents();
  CHECK(!canvas->gradient_handles_forced());
  CHECK(!canvas->gradient_fill_handles().has_value());
  CHECK(gradient() == resting);
  send_mouse(*canvas, QEvent::MouseButtonRelease, grab + QPoint(25, 25), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(gradient() == resting);
  CHECK(depth() == before);
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("shapeAppearance/showGradientHandles")).toBool());

  // Ticked again, the handles are back; closing the window takes them away and opening it
  // brings them back, the box still ticked.
  box()->click();
  QApplication::processEvents();
  CHECK(canvas->gradient_fill_handles().has_value());
  page_of(*panel)->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close)->click();
  for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
  CHECK(!panel->isVisible());
  CHECK(!canvas->gradient_handles_forced());
  CHECK(!canvas->gradient_fill_handles().has_value());
  require_action(window, "layerShapeAppearanceAction")->trigger();
  for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
  CHECK(panel->isVisible() && box()->isChecked());
  CHECK(canvas->gradient_handles_forced());
  CHECK(canvas->gradient_fill_handles().has_value());

  // A second document's canvas follows the box too, and shows nothing without a gradient.
  Access::create_document(window, 320, 240);
  QApplication::processEvents();
  auto* second_canvas = require_canvas(window);
  CHECK(second_canvas != canvas);
  CHECK(second_canvas->gradient_handles_forced());
  CHECK(!second_canvas->gradient_fill_handles().has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> shape_appearance_panel_tests() {
  return {
      {"ui_shape_appearance_window_stays_open_while_editing", ui_shape_appearance_window_stays_open_while_editing},
      {"ui_shape_appearance_window_records_one_undo_step_per_gesture",
       ui_shape_appearance_window_records_one_undo_step_per_gesture},
      {"ui_shape_appearance_window_reconfigures_for_the_selected_shape",
       ui_shape_appearance_window_reconfigures_for_the_selected_shape},
      {"ui_shape_appearance_window_survives_document_switch_close_and_layer_delete",
       ui_shape_appearance_window_survives_document_switch_close_and_layer_delete},
      {"ui_shape_appearance_window_is_disabled_under_another_dialogs_lock",
       ui_shape_appearance_window_is_disabled_under_another_dialogs_lock},
      {"ui_shape_appearance_window_color_picker_commits_one_step",
       ui_shape_appearance_window_color_picker_commits_one_step},
      {"ui_shape_appearance_window_step_timing", ui_shape_appearance_window_step_timing},
      {"ui_shape_appearance_window_shows_gradient_handles_under_other_tools",
       ui_shape_appearance_window_shows_gradient_handles_under_other_tools},
  };
}
