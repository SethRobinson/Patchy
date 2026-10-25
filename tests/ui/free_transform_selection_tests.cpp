// Free Transform of the selected pixels (GitHub issue 399): with a selection on a
// pixel layer, Free Transform lifts only the selected pixels, the box fits the
// selection, Enter composites the transformed pixels back onto the layer (one undo
// step, the selection follows), Esc leaves layer and selection untouched, and with
// no selection the whole layer transforms as before. docs/free-transform.md.
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QRegion>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::scripting_support::run_script;
using patchy::ui::MainWindowTestAccess;

constexpr int kWidth = 400;
constexpr int kHeight = 300;
const QRect kSelection(100, 80, 120, 90);

// Pixel (x, y) of the "Photo" layer: a ramp that differs everywhere, so moved
// pixels and holes are both visible.
std::array<std::uint8_t, 3> photo_color(int x, int y) {
  return {static_cast<std::uint8_t>(40 + (x * 200) / kWidth), static_cast<std::uint8_t>(30 + (y * 200) / kHeight),
          static_cast<std::uint8_t>(((x / 20) + (y / 20)) % 2 == 0 ? 60 : 180)};
}

patchy::PixelBuffer photo_pixels(patchy::BitDepth depth) {
  patchy::PixelBuffer pixels(kWidth, kHeight, patchy::with_bit_depth(patchy::PixelFormat::rgba8(), depth));
  const auto domain = patchy::deep_domain_for(depth);
  std::vector<float> row(static_cast<std::size_t>(kWidth) * 4U);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto color = photo_color(x, y);
      for (std::size_t c = 0; c < 3U; ++c) {
        const auto value = static_cast<float>(color[c]);
        row[static_cast<std::size_t>(x) * 4U + c] =
            domain == patchy::DeepDomain::Linear
                ? static_cast<float>(patchy::srgb_decode(static_cast<double>(value) / 255.0) * 255.0)
                : value;
      }
      row[static_cast<std::size_t>(x) * 4U + 3U] = 255.0F;
    }
    patchy::store_rgba_row(pixels, y, 0, kWidth, domain, row);
  }
  return pixels;
}

// A white position-locked Background plus the opaque "Photo" layer covering the
// canvas, Move tool, 100% zoom, snapping off.
patchy::LayerId prepare_photo_scene(patchy::ui::MainWindow& window, patchy::BitDepth depth = patchy::BitDepth::UInt8) {
  const auto format = patchy::with_bit_depth(patchy::PixelFormat::rgba8(), depth);
  patchy::Document document(kWidth, kHeight, format);
  document.color_state().bit_depth = depth;
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  if (depth != patchy::BitDepth::UInt8) {
    background.set_pixels(patchy::convert_pixel_buffer_depth(background.pixels(), depth, patchy::SampleKind::Color));
  }
  patchy::set_layer_locks_position(background, true);
  auto& photo = document.add_pixel_layer("Photo", photo_pixels(depth));
  const auto photo_id = photo.id();
  window.add_document_session(std::move(document), QStringLiteral("Selection transform"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_show_transform_controls(false);
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

// Straight RGBA of a layer pixel in document coordinates on the 0..255 scale (16-bit
// samples / 257; 32-bit linear values sRGB-encoded), alpha 0 outside the layer.
std::array<float, 4> layer_rgba(const patchy::Layer& layer, int x, int y) {
  const auto bounds = layer.bounds();
  if (!bounds.contains(x, y)) {
    return {0.0F, 0.0F, 0.0F, 0.0F};
  }
  std::array<float, 4> value{};
  patchy::load_rgba_row(layer.pixels(), y - bounds.y, x - bounds.x, 1, patchy::DeepDomain::Encoded, value);
  return value;
}

bool rgba_is_photo(const std::array<float, 4>& value, int x, int y, float tolerance) {
  const auto expected = photo_color(x, y);
  return std::abs(value[3] - 255.0F) <= 0.5F && std::abs(value[0] - expected[0]) <= tolerance &&
         std::abs(value[1] - expected[1]) <= tolerance && std::abs(value[2] - expected[2]) <= tolerance;
}

bool rect_near(const std::optional<QRect>& actual, QRect expected, int slack = 1) {
  return actual.has_value() && std::abs(actual->left() - expected.left()) <= slack &&
         std::abs(actual->top() - expected.top()) <= slack && std::abs(actual->right() - expected.right()) <= slack &&
         std::abs(actual->bottom() - expected.bottom()) <= slack;
}

std::array<QPointF, 4> corners_of(QRect rect) {
  return {QPointF(rect.left(), rect.top()), QPointF(rect.left() + rect.width(), rect.top()),
          QPointF(rect.left() + rect.width(), rect.top() + rect.height()),
          QPointF(rect.left(), rect.top() + rect.height())};
}

bool quad_equals(const std::array<QPointF, 4>& a, const std::array<QPointF, 4>& b) {
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::abs(a[i].x() - b[i].x()) > 0.01 || std::abs(a[i].y() - b[i].y()) > 0.01) {
      return false;
    }
  }
  return true;
}

// Perspective on the selected rectangle: the top edge narrows symmetrically, so
// the selection's top corners leave a hole that the preview shows and the commit
// keeps; outside the selection the layer never changes.
const std::array<QPointF, 4> kPerspective{QPointF(130, 80), QPointF(190, 80), QPointF(220, 170),
                                          QPointF(100, 170)};

QImage canvas_crop(patchy::ui::CanvasWidget& canvas, QRect document_rect) {
  QApplication::processEvents();
  return canvas.grab().toImage().copy(
      QRect(canvas.widget_position_for_document_point(document_rect.topLeft()), document_rect.size()));
}

void ui_free_transform_selection_floats_selected_pixels() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_photo_scene(window);
  auto* canvas = require_canvas(window);
  select_rect(*canvas, kSelection);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);

  require_action(window, "editTransformPerspectiveAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_floats_selection());
  // The box fits the selection, not the layer.
  CHECK(quad_equals(canvas->free_transform_quad(), corners_of(kSelection)));

  CHECK(canvas->set_free_transform_quad(kPerspective));
  QApplication::processEvents();
  const QRect view(60, 50, 200, 150);
  QList<QImage> strips{canvas_crop(*canvas, view)};
  // Preview: the hole beside the narrowed top shows the white Background, the
  // layer outside the selection is unchanged, the document is not touched yet.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(105, 85)), QColor(Qt::white), 8));
  const auto outside = photo_color(60, 60);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(60, 60)), QColor(outside[0], outside[1], outside[2]), 6));
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));

  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_selection_perspective", strips);
  CHECK(!canvas->free_transform_active());
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);

  const auto& committed = layer_by_id(window, photo_id);
  CHECK(committed.bounds() == (patchy::Rect{0, 0, kWidth, kHeight}));
  CHECK(layer_rgba(committed, 105, 85)[3] == 0.0F);                  // the hole stays transparent
  CHECK(rgba_is_photo(layer_rgba(committed, 60, 60), 60, 60, 0.0F));  // outside: untouched
  CHECK(rgba_is_photo(layer_rgba(committed, 300, 250), 300, 250, 0.0F));
  CHECK(layer_rgba(committed, 160, 165)[3] > 250.0F);  // inside the trapezoid
  // The selection followed the pixels.
  CHECK(rect_near(canvas->selected_document_rect(), QRect(100, 80, 120, 90)));
  CHECK(canvas->selection_alpha_at(QPoint(105, 85)) == 0U);
  CHECK(canvas->selection_alpha_at(QPoint(160, 120)) == 255U);

  // One undo step restores the layer bytes and the rectangle selection.
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
  CHECK(canvas->selection_alpha_at(QPoint(105, 85)) == 255U);
  MainWindowTestAccess::redo(window);
  QApplication::processEvents();
  CHECK(layer_rgba(layer_by_id(window, photo_id), 105, 85)[3] == 0.0F);
  CHECK(canvas->selection_alpha_at(QPoint(105, 85)) == 0U);
}

// Esc leaves the layer and the selection exactly as they were and records nothing;
// session steps step back through the pending quad; Warp refuses the floating session.
void ui_free_transform_selection_cancel_and_session_steps() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_photo_scene(window);
  auto* canvas = require_canvas(window);
  select_rect(*canvas, kSelection);
  const auto original = layer_by_id(window, photo_id).pixels();
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_floats_selection());
  const std::array<QPointF, 4> moved{QPointF(140, 100), QPointF(260, 100), QPointF(260, 190), QPointF(140, 190)};
  CHECK(canvas->set_free_transform_quad(moved));
  CHECK(canvas->set_free_transform_quad(kPerspective));
  MainWindowTestAccess::undo(window);  // a session step, not document history
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(quad_equals(canvas->free_transform_quad(), moved));

  CHECK(!canvas->begin_warp_transform());
  CHECK(canvas->free_transform_active() && canvas->free_transform_floats_selection());

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(same_bytes(layer_by_id(window, photo_id).pixels(), original));
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_before);
}

// No selection: the session takes the whole layer, as before.
void ui_free_transform_without_selection_takes_whole_layer() {
  patchy::ui::MainWindow window;
  prepare_photo_scene(window);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(!canvas->free_transform_floats_selection());
  CHECK(quad_equals(canvas->free_transform_quad(), corners_of(QRect(0, 0, kWidth, kHeight))));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

// 16-bit: a whole-pixel move of the selection keeps the 16-bit samples exactly,
// leaves a transparent hole, and the selection moves along.
void ui_free_transform_selection_keeps_16_bit_samples() {
  patchy::ui::MainWindow window;
  const auto photo_id = prepare_photo_scene(window, patchy::BitDepth::UInt16);
  auto* canvas = require_canvas(window);
  select_rect(*canvas, kSelection);
  const auto original = layer_by_id(window, photo_id).pixels();
  CHECK(original.format().bit_depth == patchy::BitDepth::UInt16);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_floats_selection());
  CHECK(canvas->set_free_transform_quad(corners_of(kSelection.translated(30, 20))));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();

  const auto& committed = layer_by_id(window, photo_id);
  CHECK(committed.pixels().format() == original.format());
  CHECK(layer_rgba(committed, 105, 85)[3] == 0.0F);
  bool exact = true;
  for (const auto& point : {QPoint(130, 100), QPoint(200, 150), QPoint(249, 189)}) {
    const auto* moved = committed.pixels().pixel(point.x(), point.y());
    const auto* source = original.pixel(point.x() - 30, point.y() - 20);
    exact = exact && std::memcmp(moved, source, 8U) == 0;
  }
  CHECK(exact);
  CHECK(std::memcmp(committed.pixels().pixel(20, 20), original.pixel(20, 20), 8U) == 0);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(30, 20)));
}

// The Background: its position lock does not stop a selection transform, the hole
// fills with the background color and the layer stays a Background. A script's
// layer.transform honors the selection the same way.
void ui_free_transform_selection_on_background_and_script() {
  patchy::ui::MainWindow window;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer flat(kWidth, kHeight, patchy::PixelFormat::rgb8());
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto color = photo_color(x, y);
      std::memcpy(flat.pixel(x, y), color.data(), 3U);
    }
  }
  auto& background = document.add_pixel_layer("Background", std::move(flat));
  patchy::set_layer_locks_position(background, true);
  const auto background_id = background.id();
  window.add_document_session(std::move(document), QStringLiteral("Background transform"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_secondary_color(QColor(10, 200, 30));
  select_rect(*canvas, kSelection);
  CHECK(run_script(window, QStringLiteral("var layer = app.activeDocument.activeLayer;"
                                          "layer.transform({matrix: [1, 0, 0, 1, 40, 30]});")));
  QApplication::processEvents();
  const auto& doc = std::as_const(MainWindowTestAccess::document(window));
  const auto* layer = doc.find_layer(background_id);
  CHECK(layer != nullptr);
  CHECK(patchy::background_layer(doc.layers(), kWidth, kHeight) == layer);
  CHECK(layer->pixels().format() == patchy::PixelFormat::rgb8());
  const auto* hole = layer->pixels().pixel(105, 85);
  CHECK(hole[0] == 10 && hole[1] == 200 && hole[2] == 30);
  const auto* moved = layer->pixels().pixel(150, 120);
  const auto expected = photo_color(110, 90);
  CHECK(moved[0] == expected[0] && moved[1] == expected[1] && moved[2] == expected[2]);
  const auto* outside = layer->pixels().pixel(20, 20);
  const auto untouched = photo_color(20, 20);
  CHECK(outside[0] == untouched[0] && outside[2] == untouched[2]);
  CHECK(canvas->selected_document_rect() == std::optional<QRect>(kSelection.translated(40, 30)));
}

}  // namespace

std::vector<patchy::test::TestCase> free_transform_selection_tests() {
  return {
      {"ui_free_transform_selection_floats_selected_pixels", ui_free_transform_selection_floats_selected_pixels},
      {"ui_free_transform_selection_cancel_and_session_steps", ui_free_transform_selection_cancel_and_session_steps},
      {"ui_free_transform_without_selection_takes_whole_layer", ui_free_transform_without_selection_takes_whole_layer},
      {"ui_free_transform_selection_keeps_16_bit_samples", ui_free_transform_selection_keeps_16_bit_samples},
      {"ui_free_transform_selection_on_background_and_script", ui_free_transform_selection_on_background_and_script},
  };
}
