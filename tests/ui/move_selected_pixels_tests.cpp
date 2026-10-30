// Move tool drags inside a pixel selection (GitHub issue 532): a plain press inside
// the selection moves the selected pixels on the active layer, with Auto-Select on or
// off, as one "Move" undo step; no layer is added, the selection follows, a second drag
// keeps floating the same pixels (the first hole stays the only hole), Shift locks the
// axis, and a press outside the selection still moves the layer. docs/move-tool.md.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QRegion>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

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

}  // namespace

std::vector<patchy::test::TestCase> move_selected_pixels_tests() {
  return {
      {"ui_move_drag_inside_selection_moves_pixels", ui_move_drag_inside_selection_moves_pixels},
      {"ui_move_second_drag_keeps_floating_pixels", ui_move_second_drag_keeps_floating_pixels},
      {"ui_move_selection_drag_modifiers_and_outside_press", ui_move_selection_drag_modifiers_and_outside_press},
      {"ui_move_selection_drag_on_background", ui_move_selection_drag_on_background},
  };
}
