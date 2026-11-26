// Move tool drags inside a pixel selection (GitHub issue 532): a plain press inside
// the selection moves the selected pixels on the active layer, with Auto-Select on or
// off, as one "Move" undo step; no layer is added, the selection follows, a second drag
// keeps floating the same pixels (the first hole stays the only hole), Shift locks the
// axis, and a press outside the selection still moves the layer. docs/move-tool.md.
// The selection tools run the same drag while their temporary Move key is held (GitHub
// issue 753): Ctrl, or Alt in the GIMP-style tool modifiers. docs/selection-tools.md.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_modifier_style.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace patchy::test::ui;
using patchy::ui::CanvasTool;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::ToolModifierRole;
using patchy::ui::ToolModifierStyle;

constexpr int kWidth = 400;
constexpr int kHeight = 300;
const QRect kSelection(100, 80, 120, 90);

// A ramp that differs everywhere, so moved pixels and holes are both visible.
std::array<std::uint8_t, 3> photo_color(int x, int y) {
  return {static_cast<std::uint8_t>(40 + (x * 200) / kWidth), static_cast<std::uint8_t>(30 + (y * 200) / kHeight),
          static_cast<std::uint8_t>(((x / 20) + (y / 20)) % 2 == 0 ? 60 : 180)};
}

patchy::PixelBuffer photo_pixels(patchy::PixelFormat format) {
  patchy::PixelBuffer pixels(kWidth, kHeight, format);
  const auto channels = static_cast<std::size_t>(format.channels);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto color = photo_color(x, y);
      auto* pixel = pixels.pixel(x, y);
      std::memcpy(pixel, color.data(), 3U);
      if (channels == 4U) {
        pixel[3] = 255U;
      }
    }
  }
  return pixels;
}

// A white position-locked Background plus the opaque "Photo" layer, Move tool, 100%
// zoom, snapping off, Photo active.
patchy::LayerId prepare_photo_scene(patchy::ui::MainWindow& window, bool auto_select, bool transform_controls) {
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  auto& photo = document.add_pixel_layer("Photo", photo_pixels(patchy::PixelFormat::rgba8()));
  const auto photo_id = photo.id();
  window.add_document_session(std::move(document), QStringLiteral("Move selected pixels"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_show_transform_controls(transform_controls);
  canvas->set_auto_select_layer(auto_select);
  QApplication::processEvents();
  return photo_id;
}

void select_rect(patchy::ui::CanvasWidget& canvas, QRect rect) {
  patchy::ui::CanvasWidget::SelectionSnapshot snapshot;
  snapshot.selection = QRegion(rect);
  snapshot.display_region = QRegion(rect);
  canvas.apply_selection_snapshot(snapshot);
  QApplication::processEvents();
}

const patchy::Layer& layer_by_id(patchy::ui::MainWindow& window, patchy::LayerId id) {
  const auto* layer = std::as_const(MainWindowTestAccess::document(window)).find_layer(id);
  CHECK(layer != nullptr);
  return *layer;
}

std::size_t layer_count(patchy::ui::MainWindow& window) {
  return std::as_const(MainWindowTestAccess::document(window)).layers().size();
}

// Straight RGBA8 of a layer pixel in document coordinates, alpha 0 outside the layer.
std::array<std::uint8_t, 4> layer_rgba(const patchy::Layer& layer, int x, int y) {
  const auto bounds = layer.bounds();
  if (!bounds.contains(x, y)) {
    return {0U, 0U, 0U, 0U};
  }
  const auto* pixel = layer.pixels().pixel(x - bounds.x, y - bounds.y);
  return {pixel[0], pixel[1], pixel[2], layer.pixels().format().channels == 4 ? pixel[3] : std::uint8_t{255U}};
}

bool is_photo_at(const patchy::Layer& layer, QPoint at, QPoint source) {
  const auto value = layer_rgba(layer, at.x(), at.y());
  const auto expected = photo_color(source.x(), source.y());
  return value[3] == 255U && value[0] == expected[0] && value[1] == expected[1] && value[2] == expected[2];
}

bool same_bytes(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) {
    return false;
  }
  for (int y = 0; y < a.height(); ++y) {
    const auto row_a = a.row(y);
    const auto row_b = b.row(y);
    if (std::memcmp(row_a.data(), row_b.data(), row_a.size()) != 0) {
      return false;
    }
  }
  return true;
}

void drag_document(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to,
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  drag(canvas, widget_center_of_document_pixel(canvas, from), widget_center_of_document_pixel(canvas, to), modifiers);
  QApplication::processEvents();
}

QImage canvas_crop(patchy::ui::CanvasWidget& canvas, QRect document_rect) {
  QApplication::processEvents();
  return canvas.grab().toImage().copy(
      QRect(canvas.widget_position_for_document_point(document_rect.topLeft()), document_rect.size()));
}

void check_drag_moves_selected_pixels(bool auto_select, bool transform_controls) {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_photo_scene(window, auto_select, transform_controls);
  auto* canvas = require_canvas(window);
  select_rect(*canvas, kSelection);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  const auto layers_before = layer_count(window);
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);

  const auto view = QRect(60, 50, 260, 200);
  const auto before_image = canvas_crop(*canvas, view);
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150));
  if (auto_select && transform_controls) {
    save_stacked_artifact("ui_move_drag_inside_selection_moves_pixels", {before_image, canvas_crop(*canvas, view)});
  }

  CHECK(!canvas->free_transform_active());
  CHECK(layer_count(window) == layers_before);
  const auto& moved = layer_by_id(window, photo_id);
  CHECK(moved.bounds() == original_bounds);
  // The selected pixels land 40 px right and 30 px down; the hole is transparent; the
  // rest of the layer is untouched.
  CHECK(is_photo_at(moved, QPoint(150, 120), QPoint(110, 90)));
  CHECK(is_photo_at(moved, QPoint(259, 199), QPoint(219, 169)));
  CHECK(layer_rgba(moved, 105, 85)[3] == 0U);
  CHECK(is_photo_at(moved, QPoint(20, 20), QPoint(20, 20)));
  CHECK(is_photo_at(moved, QPoint(300, 250), QPoint(300, 250)));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 30)));
  CHECK(MainWindowTestAccess::document(window).active_layer_id() == std::optional<patchy::LayerId>(photo_id));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);

  // One undo step restores the layer bytes and the selection.
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
}

void ui_move_drag_inside_selection_moves_pixels() {
  check_drag_moves_selected_pixels(/*auto_select=*/true, /*transform_controls=*/true);
  check_drag_moves_selected_pixels(/*auto_select=*/true, /*transform_controls=*/false);
  check_drag_moves_selected_pixels(/*auto_select=*/false, /*transform_controls=*/true);
  check_drag_moves_selected_pixels(/*auto_select=*/false, /*transform_controls=*/false);
}

// A second drag moves the same floating pixels: what the first drop covered outside
// the original selection comes back, and only the first hole stays. Two undo steps.
void ui_move_second_drag_keeps_floating_pixels() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_photo_scene(window, /*auto_select=*/true, /*transform_controls=*/true);
  auto* canvas = require_canvas(window);
  select_rect(*canvas, kSelection);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150));
  drag_document(*canvas, QPoint(190, 150), QPoint(230, 150));

  const auto& moved = layer_by_id(window, photo_id);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(80, 30)));
  CHECK(is_photo_at(moved, QPoint(190, 120), QPoint(110, 90)));
  // Covered by the first drop, outside the original selection and the second drop:
  // the original pixels again, not a second hole.
  CHECK(is_photo_at(moved, QPoint(150, 190), QPoint(150, 190)));
  // Inside the original selection and outside the second drop: the hole.
  CHECK(layer_rgba(moved, 105, 85)[3] == 0U);
  CHECK(layer_rgba(moved, 170, 100)[3] == 0U);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 2);

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 30)));
  CHECK(is_photo_at(layer_by_id(window, photo_id), QPoint(150, 120), QPoint(110, 90)));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));

  // A selection changed in between ends the float: the next drag lifts what is there.
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150));
  select_rect(*canvas, kSelection.translated(40, 30).adjusted(0, 0, -1, 0));
  drag_document(*canvas, QPoint(190, 150), QPoint(230, 150));
  CHECK(layer_rgba(layer_by_id(window, photo_id), 150, 190)[3] == 0U);
}

// Shift locks the drag to an axis; a press outside the selection moves the whole
// layer as before; Alt still duplicates the layer.
void ui_move_selection_drag_modifiers_and_outside_press() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_photo_scene(window, /*auto_select=*/false, /*transform_controls=*/false);
  auto* canvas = require_canvas(window);
  select_rect(*canvas, kSelection);
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 130), Qt::ShiftModifier);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 0)));
  CHECK(is_photo_at(layer_by_id(window, photo_id), QPoint(150, 120), QPoint(110, 120)));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  const auto bounds = layer_by_id(window, photo_id).bounds();
  drag_document(*canvas, QPoint(20, 20), QPoint(30, 25));
  const auto moved_bounds = layer_by_id(window, photo_id).bounds();
  CHECK(moved_bounds.x == bounds.x + 10 && moved_bounds.y == bounds.y + 5);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  const auto layers_before = layer_count(window);
  drag_document(*canvas, QPoint(150, 120), QPoint(170, 130), Qt::AltModifier);
  CHECK(layer_count(window) == layers_before + 1U);
}

// A flat image's Background: the hole fills with the background color, the layer stays
// the only layer and a Background, with Auto-Select on.
void ui_move_selection_drag_on_background() {
  patchy::ui::MainWindow window;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer("Background", photo_pixels(patchy::PixelFormat::rgb8()));
  patchy::set_layer_locks_position(background, true);
  const auto background_id = background.id();
  window.add_document_session(std::move(document), QStringLiteral("Background move"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_auto_select_layer(true);
  canvas->set_secondary_color(QColor(10, 200, 30));
  select_rect(*canvas, kSelection);
  // The position-locked Background shows the Move cursor inside the selection.
  send_mouse(*canvas, QEvent::MouseMove, widget_center_of_document_pixel(*canvas, QPoint(150, 120)), Qt::NoButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150));

  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  CHECK(doc.layers().size() == 1U);
  const auto* layer = doc.find_layer(background_id);
  CHECK(layer != nullptr);
  CHECK(patchy::background_layer(doc.layers(), kWidth, kHeight) == layer);
  const auto hole = layer_rgba(*layer, 105, 85);
  CHECK(hole[0] == 10U && hole[1] == 200U && hole[2] == 30U && hole[3] == 255U);
  CHECK(is_photo_at(*layer, QPoint(150, 120), QPoint(110, 90)));
  CHECK(is_photo_at(*layer, QPoint(20, 20), QPoint(20, 20)));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 30)));
}

// --- The selection tools' temporary Move (GitHub issue 753) ---

// Sets the canvas's tool modifiers for one test and puts the previous ones back. Made after
// the MainWindow, which loads the stored style at startup.
class ToolModifierStyleScope {
public:
  explicit ToolModifierStyleScope(ToolModifierStyle style) : saved_(patchy::ui::active_tool_modifier_style()) {
    patchy::ui::set_active_tool_modifier_style(style);
  }
  ~ToolModifierStyleScope() { patchy::ui::set_active_tool_modifier_style(saved_); }
  ToolModifierStyleScope(const ToolModifierStyleScope&) = delete;
  ToolModifierStyleScope& operator=(const ToolModifierStyleScope&) = delete;

private:
  ToolModifierStyle saved_;
};

// What a drag left behind, for comparing two ways of making it.
struct DragOutcome {
  patchy::PixelBuffer pixels;
  patchy::Rect bounds{};
  std::optional<QRect> selection;
  std::size_t undo_steps{0};
};

DragOutcome drag_outcome(patchy::ui::MainWindow& window, patchy::LayerId id, std::size_t depth_before) {
  const auto& layer = layer_by_id(window, id);
  return {layer.pixels(), layer.bounds(), require_canvas(window)->selected_document_rect(),
          MainWindowTestAccess::active_session_undo_depth(window) - depth_before};
}

bool same_outcome(const DragOutcome& a, const DragOutcome& b) {
  return same_bytes(a.pixels, b.pixels) && a.bounds == b.bounds && a.selection == b.selection &&
         a.undo_steps == b.undo_steps;
}

// A selection tool with the photo scene, the rectangle selected.
patchy::LayerId prepare_selection_tool_scene(patchy::ui::MainWindow& window, CanvasTool tool) {
  const auto photo_id = prepare_photo_scene(window, /*auto_select=*/false, /*transform_controls=*/false);
  auto* canvas = require_canvas(window);
  if (tool == CanvasTool::Marquee) {
    require_action(window, "toolMarqueeAction")->trigger();  // the palette's own route
  } else {
    canvas->set_tool(tool);
  }
  QApplication::processEvents();
  CHECK(canvas->tool() == tool);
  select_rect(*canvas, kSelection);
  return photo_id;
}

void check_selection_tool_moves_selected_pixels(CanvasTool tool) {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, tool);
  auto* canvas = require_canvas(window);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  const auto layers_before = layer_count(window);
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);

  const auto view = QRect(60, 50, 260, 200);
  const auto before_image = canvas_crop(*canvas, view);
  const auto from = widget_center_of_document_pixel(*canvas, QPoint(150, 120));
  const auto to = widget_center_of_document_pixel(*canvas, QPoint(190, 150));
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  const auto dragging_image = canvas_crop(*canvas, view);  // the pixels in hand, before the drop
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  if (tool == CanvasTool::Marquee) {
    save_stacked_artifact("ui_issue_753_marquee_ctrl_drag_moves_pixels",
                          {before_image, dragging_image, canvas_crop(*canvas, view)});
  }

  // The Move tool's drag (check_drag_moves_selected_pixels), and the tool is still the tool.
  CHECK(canvas->tool() == tool);
  CHECK(!canvas->free_transform_active());
  CHECK(layer_count(window) == layers_before);
  const auto& moved = layer_by_id(window, photo_id);
  CHECK(moved.bounds() == original_bounds);
  CHECK(is_photo_at(moved, QPoint(150, 120), QPoint(110, 90)));
  CHECK(is_photo_at(moved, QPoint(259, 199), QPoint(219, 169)));
  CHECK(layer_rgba(moved, 105, 85)[3] == 0U);
  CHECK(is_photo_at(moved, QPoint(20, 20), QPoint(20, 20)));
  CHECK(is_photo_at(moved, QPoint(300, 250), QPoint(300, 250)));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 30)));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Move"));

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
}

// Ctrl+drag inside the selection with every selection tool: the selected pixels move with
// the outline, the hole is transparent, one "Move" undo step, the tool stays.
void ui_selection_tools_ctrl_drag_inside_selection_moves_pixels() {
  for (const auto tool :
       {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
        CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect, CanvasTool::ObjectSelect,
        CanvasTool::PatchTool, CanvasTool::ContentAwareMove}) {
    check_selection_tool_moves_selected_pixels(tool);
  }
}

// Esc mid-drag restores everything, Shift locks the axis, a press outside the selection or
// with no selection moves the layer, a click moves nothing, Ctrl+Alt is not the Move, and
// the four-way cursor follows the key.
void ui_selection_tool_ctrl_drag_escape_shift_outside_and_cursor() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, CanvasTool::Marquee);
  auto* canvas = require_canvas(window);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto unchanged = [&] {
    return same_bytes(layer_by_id(window, photo_id).pixels(), original) &&
           layer_by_id(window, photo_id).bounds() == original_bounds &&
           MainWindowTestAccess::active_session_undo_depth(window) == depth_before;
  };
  const auto at = [canvas](QPoint document_point) { return widget_center_of_document_pixel(*canvas, document_point); };

  // Esc while the button is still down. The key is let go first, as a user must on Windows
  // (Ctrl+Esc is the Start menu): the drag goes on without it.
  send_mouse(*canvas, QEvent::MouseButtonPress, at(QPoint(150, 120)), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(180, 140)), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  CHECK(canvas->free_transform_active());
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(190, 150)), Qt::NoButton, Qt::LeftButton);
  CHECK(canvas->free_transform_active());
  CHECK(std::abs(canvas->free_transform_preview_delta_for_testing().map(QPointF(kSelection.topLeft())).x() -
                 (kSelection.x() + 40)) < 0.6);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(QPoint(190, 150)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(unchanged());
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
  CHECK(canvas->tool() == CanvasTool::Marquee);

  // Shift locks the axis.
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 130), Qt::ControlModifier | Qt::ShiftModifier);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 0)));
  CHECK(is_photo_at(layer_by_id(window, photo_id), QPoint(150, 120), QPoint(110, 120)));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(unchanged());

  // A click inside that never drags changes nothing and keeps the selection (a plain
  // Marquee click there deselects).
  send_mouse(*canvas, QEvent::MouseButtonPress, at(QPoint(150, 120)), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(QPoint(150, 120)), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(unchanged());
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));

  // Outside the selection the whole layer moves, the outline stays, the tool stays.
  drag_document(*canvas, QPoint(20, 20), QPoint(30, 25), Qt::ControlModifier);
  auto moved_bounds = layer_by_id(window, photo_id).bounds();
  CHECK(moved_bounds.x == original_bounds.x + 10 && moved_bounds.y == original_bounds.y + 5);
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  CHECK(canvas->tool() == CanvasTool::Marquee);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(unchanged());

  // Ctrl+Alt is not the Move: Alt subtracts, as before (a selection history step only).
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150), Qt::ControlModifier | Qt::AltModifier);
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(layer_by_id(window, photo_id).bounds() == original_bounds);
  CHECK(!canvas->free_transform_active());
  CHECK(!canvas->selection_contains(QPoint(170, 135)));
  CHECK(canvas->selection_contains(QPoint(110, 90)));

  // With nothing selected the layer moves and no selection is drawn.
  canvas->clear_selection();
  QApplication::processEvents();
  drag_document(*canvas, QPoint(150, 120), QPoint(162, 120), Qt::ControlModifier);
  moved_bounds = layer_by_id(window, photo_id).bounds();
  CHECK(moved_bounds.x == original_bounds.x + 12 && moved_bounds.y == original_bounds.y);
  CHECK(!canvas->has_selection());
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();

  // The cursor: four-way while the key is held, on the key itself and on hover.
  select_rect(*canvas, kSelection);
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(20, 20)), Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() != Qt::SizeAllCursor);
  send_key_press(*canvas, Qt::Key_Control);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  CHECK(canvas->cursor().shape() != Qt::SizeAllCursor);
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(24, 20)), Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_mouse(*canvas, QEvent::MouseMove, at(QPoint(24, 22)), Qt::NoButton, Qt::NoButton,
             Qt::ControlModifier | Qt::AltModifier);
  CHECK(canvas->cursor().shape() != Qt::SizeAllCursor);
}

// Plain, add, subtract and intersect drags still make selections in every tool modifier
// style and never move a pixel; the style's Move key drags the selected pixels. GIMP-style:
// Ctrl subtracts, so the Move is Alt.
void check_selection_gestures_in_style(ToolModifierStyle style, Qt::KeyboardModifier expected_move_key) {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, CanvasTool::Marquee);
  const ToolModifierStyleScope scope(style);
  auto* canvas = require_canvas(window);
  const auto subtract = patchy::ui::tool_modifier(ToolModifierRole::SelectionSubtract, CanvasTool::Marquee);
  const auto move_key = patchy::ui::tool_modifier(ToolModifierRole::TemporaryMove, CanvasTool::Marquee);
  CHECK(move_key == expected_move_key);
  CHECK(move_key != subtract);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);

  canvas->clear_selection();
  QApplication::processEvents();
  drag_document(*canvas, QPoint(100, 80), QPoint(220, 170));
  CHECK(canvas->selection_contains(QPoint(150, 120)));
  CHECK(!canvas->selection_contains(QPoint(50, 50)));
  drag_document(*canvas, QPoint(250, 80), QPoint(300, 120), Qt::ShiftModifier);
  CHECK(canvas->selection_contains(QPoint(270, 100)));
  CHECK(canvas->selection_contains(QPoint(150, 120)));
  drag_document(*canvas, QPoint(70, 50), QPoint(160, 200), subtract);
  CHECK(!canvas->selection_contains(QPoint(120, 100)));
  CHECK(canvas->selection_contains(QPoint(200, 120)));
  CHECK(canvas->selection_contains(QPoint(270, 100)));
  drag_document(*canvas, QPoint(180, 50), QPoint(330, 100), Qt::ShiftModifier | subtract);
  CHECK(canvas->selection_contains(QPoint(200, 90)));
  CHECK(!canvas->selection_contains(QPoint(200, 150)));
  CHECK(canvas->selection_contains(QPoint(270, 90)));
  CHECK(!canvas->selection_contains(QPoint(270, 110)));
  // Selections only: the layer never changed and nothing but selection history was made.
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(layer_by_id(window, photo_id).bounds() == original_bounds);

  select_rect(*canvas, kSelection);
  const auto depth_before_move = MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(depth_before_move >= depth_before);
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150), move_key);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 30)));
  CHECK(is_photo_at(layer_by_id(window, photo_id), QPoint(150, 120), QPoint(110, 90)));
  CHECK(layer_rgba(layer_by_id(window, photo_id), 105, 85)[3] == 0U);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before_move + 1);
  CHECK(canvas->tool() == CanvasTool::Marquee);
}

void ui_selection_tool_modifiers_still_select_in_every_style() {
  check_selection_gestures_in_style(ToolModifierStyle::Patchy, Qt::ControlModifier);
  check_selection_gestures_in_style(ToolModifierStyle::Krita, Qt::ControlModifier);
  check_selection_gestures_in_style(ToolModifierStyle::Gimp, Qt::AltModifier);

  // GIMP-style: Ctrl+drag inside the selection still subtracts and moves nothing; the Alt
  // key shows the four-way cursor.
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, CanvasTool::Lasso);
  const ToolModifierStyleScope scope(ToolModifierStyle::Gimp);
  auto* canvas = require_canvas(window);
  const auto original = layer_by_id(window, photo_id).pixels();
  drag_document_path(*canvas, {QPoint(140, 100), QPoint(200, 100), QPoint(200, 150), QPoint(140, 150)}, 4,
                     Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(!canvas->selection_contains(QPoint(170, 125)));
  CHECK(canvas->selection_contains(QPoint(110, 90)));
  send_mouse(*canvas, QEvent::MouseMove, widget_center_of_document_pixel(*canvas, QPoint(110, 90)), Qt::NoButton,
             Qt::NoButton);
  send_key_press(*canvas, Qt::Key_Alt);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_key_release(*canvas, Qt::Key_Alt, Qt::AltModifier);
  drag_document(*canvas, QPoint(110, 90), QPoint(110, 60), Qt::AltModifier);
  CHECK(is_photo_at(layer_by_id(window, photo_id), QPoint(110, 60), QPoint(110, 90)));
  CHECK(canvas->tool() == CanvasTool::Lasso);
}

// What the Move tool refuses or redirects inside a selection, a selection tool's Ctrl drag
// treats the same way: locked pixels, a locked position, locked transparency. A channel
// view refuses the Move tool, so it refuses this too.
void ui_selection_tool_ctrl_drag_follows_move_tool_rules() {
  enum class Lock { Pixels, Position, Transparency };
  for (const auto lock : {Lock::Pixels, Lock::Position, Lock::Transparency}) {
    const auto run = [lock](CanvasTool tool, Qt::KeyboardModifiers modifiers) {
      patchy::ui::MainWindow window;
      const auto photo_id = prepare_selection_tool_scene(window, tool);
      auto* layer = MainWindowTestAccess::document(window).find_layer(photo_id);
      CHECK(layer != nullptr);
      switch (lock) {
        case Lock::Pixels:
          patchy::set_layer_locks_image_pixels(*layer, true);
          break;
        case Lock::Position:
          patchy::set_layer_locks_position(*layer, true);
          break;
        case Lock::Transparency:
          patchy::set_layer_locks_transparent_pixels(*layer, true);
          break;
      }
      QApplication::processEvents();
      auto* canvas = require_canvas(window);
      const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
      drag_document(*canvas, QPoint(150, 120), QPoint(190, 150), modifiers);
      CHECK(!canvas->free_transform_active());
      CHECK(canvas->tool() == tool);
      return drag_outcome(window, photo_id, depth_before);
    };
    const auto with_move_tool = run(CanvasTool::Move, Qt::NoModifier);
    const auto with_marquee = run(CanvasTool::Marquee, Qt::ControlModifier);
    CHECK(same_outcome(with_move_tool, with_marquee));
  }

  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, CanvasTool::Marquee);
  auto* canvas = require_canvas(window);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  canvas->set_component_channel_preview(patchy::ui::CanvasWidget::LayerEditTarget::ComponentRed);
  QApplication::processEvents();
  for (const auto from : {QPoint(150, 120), QPoint(20, 20)}) {
    drag_document(*canvas, from, from + QPoint(40, 30), Qt::ControlModifier);
    CHECK(!canvas->free_transform_active());
    CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
    CHECK(layer_by_id(window, photo_id).bounds() == original_bounds);
    CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before);
  }
}

// Gestures that own the press keep it: a committed marquee's resize handle resizes with
// Ctrl held, and a Polygonal Lasso trace in progress takes a Ctrl+click as its next point.
void ui_selection_tool_ctrl_press_yields_to_handles_and_traces() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, CanvasTool::Marquee);
  auto* canvas = require_canvas(window);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  const auto untouched = [&] {
    return same_bytes(layer_by_id(window, photo_id).pixels(), original) &&
           layer_by_id(window, photo_id).bounds() == original_bounds;
  };
  canvas->clear_selection();
  QApplication::processEvents();
  drag_document(*canvas, QPoint(100, 80), QPoint(220, 170));
  const auto drawn = canvas->selected_document_rect();
  CHECK(drawn.has_value());
  if (!drawn.has_value()) {
    return;
  }
  // The right edge's middle handle: the resize cursor stays under Ctrl and the drag resizes.
  const auto handle = canvas->widget_position_for_document_point(
      QPoint(drawn->x() + drawn->width(), drawn->y() + drawn->height() / 2));
  send_mouse(*canvas, QEvent::MouseMove, handle, Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  drag(*canvas, handle, handle + QPoint(30, 0), Qt::ControlModifier);
  QApplication::processEvents();
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value() && resized->x() == drawn->x() && std::abs(resized->width() - (drawn->width() + 30)) <= 1);
  CHECK(untouched());
  // Inside that same marquee the key moves the pixels.
  drag_document(*canvas, QPoint(150, 120), QPoint(150, 140), Qt::ControlModifier);
  CHECK(is_photo_at(layer_by_id(window, photo_id), QPoint(150, 140), QPoint(150, 120)));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(untouched());

  canvas->set_tool(CanvasTool::PolygonLasso);
  canvas->clear_selection();
  QApplication::processEvents();
  const auto click = [canvas](QPoint document_point, Qt::KeyboardModifiers modifiers) {
    const auto at = widget_center_of_document_pixel(*canvas, document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };
  click(QPoint(100, 80), Qt::NoModifier);
  CHECK(canvas->polygon_lasso_active());
  const auto vertices = canvas->polygon_lasso_vertex_count();
  click(QPoint(200, 80), Qt::ControlModifier);
  CHECK(canvas->polygon_lasso_active());
  CHECK(canvas->polygon_lasso_vertex_count() == vertices + 1);
  CHECK(untouched());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

// The painting tools' Ctrl drag is unchanged: inside a selection it still moves the whole
// layer, not the selected pixels.
void ui_paint_tool_ctrl_drag_inside_selection_still_moves_layer() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_selection_tool_scene(window, CanvasTool::Brush);
  auto* canvas = require_canvas(window);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto original_bounds = layer_by_id(window, photo_id).bounds();
  drag_document(*canvas, QPoint(150, 120), QPoint(190, 150), Qt::ControlModifier);
  CHECK(!canvas->free_transform_active());
  const auto& moved = layer_by_id(window, photo_id);
  CHECK(moved.bounds().x == original_bounds.x + 40 && moved.bounds().y == original_bounds.y + 30);
  CHECK(same_bytes(moved.pixels(), original));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
  CHECK(canvas->tool() == CanvasTool::Brush);
}

// The Wayland drag rule (docs/interactive-previews-drag-coalescing.md): the moves of a
// selection tool's Ctrl drag queued in one event-loop pass apply once, at the latest
// position, and the release lands on a move recorded right before it, as one undo step.
void ui_selection_tool_ctrl_drag_coalesces_queued_moves() {
  patchy::ui::MainWindow window;
  prepare_selection_tool_scene(window, CanvasTool::Marquee);
  auto* canvas = require_canvas(window);
  canvas->set_drag_move_coalescing_for_testing(patchy::ui::CanvasWidget::DragMoveCoalescing::AllMoves);
  const auto coalesced = [canvas] { return canvas->render_cache_diagnostics().drag_moves_coalesced; };
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  const auto start = widget_center_of_document_pixel(*canvas, QPoint(150, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto before = coalesced();
  for (int move = 1; move <= 30; ++move) {
    const auto position = start + QPoint(move, move);
    QApplication::postEvent(canvas, new QMouseEvent(QEvent::MouseMove, QPointF(position),
                                                    QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                                                    Qt::LeftButton, Qt::ControlModifier));
  }
  // The first queued move applies at once, the other 29 wait for the pass end.
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(coalesced() - before == 29);
  QApplication::processEvents();  // the pass the pass-end apply opened ends too
  const auto corner = [canvas] {
    return canvas->free_transform_preview_delta_for_testing().map(QPointF(kSelection.topLeft()));
  };
  CHECK(std::abs(corner().x() - (kSelection.x() + 30)) < 0.6);
  CHECK(std::abs(corner().y() - (kSelection.y() + 30)) < 0.6);
  const auto deliver = [canvas](QPoint position) {
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(canvas->mapToGlobal(position)), Qt::NoButton,
                      Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &event);
  };
  const auto end = start + QPoint(55, 35);
  deliver(end - QPoint(5, 0));  // applied, opens a pass
  deliver(end);                 // recorded
  CHECK(coalesced() - before == 30);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(55, 35)));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
}

}  // namespace

std::vector<patchy::test::TestCase> move_selected_pixels_tests() {
  return {
      {"ui_move_drag_inside_selection_moves_pixels", ui_move_drag_inside_selection_moves_pixels},
      {"ui_move_second_drag_keeps_floating_pixels", ui_move_second_drag_keeps_floating_pixels},
      {"ui_move_selection_drag_modifiers_and_outside_press", ui_move_selection_drag_modifiers_and_outside_press},
      {"ui_move_selection_drag_on_background", ui_move_selection_drag_on_background},
      {"ui_selection_tools_ctrl_drag_inside_selection_moves_pixels",
       ui_selection_tools_ctrl_drag_inside_selection_moves_pixels},
      {"ui_selection_tool_ctrl_drag_escape_shift_outside_and_cursor",
       ui_selection_tool_ctrl_drag_escape_shift_outside_and_cursor},
      {"ui_selection_tool_modifiers_still_select_in_every_style",
       ui_selection_tool_modifiers_still_select_in_every_style},
      {"ui_selection_tool_ctrl_drag_follows_move_tool_rules", ui_selection_tool_ctrl_drag_follows_move_tool_rules},
      {"ui_selection_tool_ctrl_press_yields_to_handles_and_traces",
       ui_selection_tool_ctrl_press_yields_to_handles_and_traces},
      {"ui_paint_tool_ctrl_drag_inside_selection_still_moves_layer",
       ui_paint_tool_ctrl_drag_inside_selection_still_moves_layer},
      {"ui_selection_tool_ctrl_drag_coalesces_queued_moves", ui_selection_tool_ctrl_drag_coalesces_queued_moves},
  };
}
