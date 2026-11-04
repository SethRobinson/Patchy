// Selection edits while a filter's settings dialog is open (local-028; "Selection edits
// under a filter dialog" in docs/selection-tools.md): the selection tools and Select
// commands stay live on the canvas, both previews follow the new selection, OK applies
// inside the selection as it is then, and everything else stays locked.

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QRegion>
#include <QTimer>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::CanvasTool;
using patchy::ui::MainWindowTestAccess;

constexpr int kWidth = 360;
constexpr int kHeight = 240;
const QRect kFirstSelection(20, 20, 100, 80);
const QRect kSecondSelection(200, 120, 120, 90);

patchy::Layer* active_layer(patchy::ui::MainWindow& window) {
  auto& document = MainWindowTestAccess::document(window);
  const auto id = std::as_const(document).active_layer_id();
  return id.has_value() ? document.find_layer(*id) : nullptr;
}

const patchy::PixelBuffer& active_pixels(patchy::ui::MainWindow& window) {
  static const patchy::PixelBuffer empty;
  const auto* layer = active_layer(window);
  return layer != nullptr ? std::as_const(*layer).pixels() : empty;
}

// True when `rect` (document = layer coordinates here) holds the same bytes in both.
bool same_in(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b, QRect rect) {
  if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) {
    return false;
  }
  rect &= QRect(0, 0, a.width(), a.height());
  const auto bytes = static_cast<std::size_t>(rect.width()) * 4U;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    if (std::memcmp(a.pixel(rect.left(), y), b.pixel(rect.left(), y), bytes) != 0) {
      return false;
    }
  }
  return true;
}

bool same_in(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b, const QRegion& region) {
  for (const auto& rect : region) {
    if (!same_in(a, b, rect)) {
      return false;
    }
  }
  return true;
}

// The marquee a drag over `rect` made: its bounds within a pixel of `rect` (the
// widget mapping may round an edge).
bool selection_near(const QRegion& selection, QRect rect) {
  const auto bounds = selection.boundingRect();
  return std::abs(bounds.left() - rect.left()) <= 1 && std::abs(bounds.top() - rect.top()) <= 1 &&
         std::abs(bounds.right() - rect.right()) <= 1 && std::abs(bounds.bottom() - rect.bottom()) <= 1;
}

// Every pixel outside `inside` matches.
bool same_outside(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b, const QRegion& inside) {
  const QRegion outside = QRegion(0, 0, a.width(), a.height()).subtracted(inside);
  for (const auto& rect : outside) {
    if (!same_in(a, b, rect)) {
      return false;
    }
  }
  return true;
}

// The photo, scaled to the test document, on its only layer.
patchy::PixelBuffer open_photo_document(patchy::ui::MainWindow& window) {
  MainWindowTestAccess::create_document(window, kWidth, kHeight);
  QImage photo(patchy::ui::to_qstring(
      patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg")));
  CHECK(!photo.isNull());
  photo = photo.scaled(kWidth, kHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);
  patchy::PixelBuffer pixels(kWidth, kHeight, patchy::PixelFormat::rgba8());
  for (int y = 0; y < kHeight && !photo.isNull(); ++y) {
    std::memcpy(pixels.pixel(0, y), photo.constScanLine(y), static_cast<std::size_t>(kWidth) * 4U);
  }
  auto* layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    layer->set_pixels(pixels);
    layer->set_bounds(patchy::Rect::from_size(kWidth, kHeight));
  }
  auto* canvas = MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  if (canvas != nullptr) {
    canvas->document_changed();
  }
  QApplication::processEvents();
  return pixels;
}

QAction* tool_action(patchy::ui::MainWindow& window, CanvasTool tool) {
  for (auto* action : window.findChildren<QAction*>()) {
    if (action->isCheckable() && action->data().isValid() && action->data().toInt() == static_cast<int>(tool)) {
      return action;
    }
  }
  return nullptr;
}

void draw_marquee(patchy::ui::CanvasWidget& canvas, QRect rect) {
  drag(canvas, canvas.widget_position_for_document_point(rect.topLeft()),
       canvas.widget_position_for_document_point(rect.topLeft() + QPoint(rect.width(), rect.height())));
  QApplication::processEvents();
}

QDialog* visible_filter_dialog() {
  auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
  return dialog != nullptr && dialog->isVisible() ? dialog : nullptr;
}

// Runs `drive` once the filter dialog shows (polling, as the dialog runs its own loop).
void when_filter_dialog_shows(std::function<void(QDialog&)> drive, int attempts = 400) {
  QTimer::singleShot(25, guarded([drive = std::move(drive), attempts]() mutable {
    auto* dialog = visible_filter_dialog();
    if (dialog == nullptr) {
      CHECK(attempts > 0);
      if (attempts > 0) {
        when_filter_dialog_shows(std::move(drive), attempts - 1);
      }
      return;
    }
    drive(*dialog);
  }));
}

patchy::ui::ZoomableImagePreview* proxy_preview(QDialog& dialog) {
  // ZoomableImagePreview has no Q_OBJECT macro, so findChild cannot cast to it.
  return static_cast<patchy::ui::ZoomableImagePreview*>(
      dialog.findChild<QWidget*>(QStringLiteral("filterDialogPreview")));
}

// The canvas preview has caught up with a selection: the filter shows inside it and
// nowhere else.
bool canvas_preview_follows(patchy::ui::MainWindow& window, const patchy::PixelBuffer& original,
                            const QRegion& selected) {
  const auto& pixels = active_pixels(window);
  return !same_in(pixels, original, selected) && same_outside(pixels, original, selected);
}

void save_review_shot(const std::string& name, patchy::ui::MainWindow& window, QDialog& dialog) {
  save_widget_artifact(name + "_window", window);
  save_widget_artifact(name + "_dialog", dialog);
}

void ui_filter_dialog_selection_edits_follow_preview_and_apply() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto original = open_photo_document(window);
  auto* canvas = MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) {
    return;
  }
  auto* marquee_tool = tool_action(window, CanvasTool::Marquee);
  CHECK(marquee_tool != nullptr);
  if (marquee_tool == nullptr) {
    return;
  }
  marquee_tool->trigger();
  draw_marquee(*canvas, kFirstSelection);
  const auto first = canvas->selected_document_region();
  CHECK(selection_near(first, kFirstSelection));
  QRegion second;
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);

  bool drove = false;
  when_filter_dialog_shows([&](QDialog& dialog) {
    auto* preview = proxy_preview(dialog);
    CHECK(preview != nullptr);
    CHECK(process_events_until([&] { return canvas_preview_follows(window, original, first); }, 8000));
    CHECK(process_events_until(
        [&] {
          return preview != nullptr &&
                 preview->property("filterDialogRenderedFilterId").toString() == QStringLiteral("patchy.filters.pixelate");
        },
        8000));
    CHECK(canvas->edit_locked());
    CHECK(canvas->selection_edits_allowed_while_locked());

    // The selection tools and Select commands are live; painting, other filters and undo are not.
    auto* marquee = tool_action(window, CanvasTool::Marquee);
    auto* lasso = tool_action(window, CanvasTool::Lasso);
    auto* brush = tool_action(window, CanvasTool::Brush);
    auto* move = tool_action(window, CanvasTool::Move);
    CHECK(marquee != nullptr && marquee->isEnabled());
    CHECK(lasso != nullptr && lasso->isEnabled());
    CHECK(brush != nullptr && !brush->isEnabled());
    CHECK(move != nullptr && !move->isEnabled());
    CHECK(require_action(window, "editSelectAllAction")->isEnabled());
    CHECK(require_action(window, "editDeselectAction")->isEnabled());
    CHECK(require_action(window, "selectInverseAction")->isEnabled());
    CHECK(!require_action(window, "filterAction_patchy_filters_pixelate")->isEnabled());
    CHECK(!require_action(window, "editFillAction")->isEnabled());

    // A paint tool forced onto the canvas still cannot touch the layer.
    const auto before_blocked = active_pixels(window);
    canvas->set_tool(CanvasTool::Brush);
    drag(*canvas, canvas->widget_position_for_document_point(QPoint(150, 30)),
         canvas->widget_position_for_document_point(QPoint(330, 220)));
    QApplication::processEvents();
    CHECK(same_in(active_pixels(window), before_blocked, QRect(0, 0, kWidth, kHeight)));
    CHECK(canvas->selected_document_region() == first);
    canvas->set_tool(CanvasTool::Marquee);

    // A new marquee elsewhere: both previews follow it.
    const auto proxy_before = preview != nullptr ? preview->image() : QImage();
    if (marquee != nullptr) {
      marquee->trigger();
    }
    draw_marquee(*canvas, kSecondSelection);
    second = canvas->selected_document_region();
    CHECK(selection_near(second, kSecondSelection));
    CHECK(process_events_until([&] { return canvas_preview_follows(window, original, second); }, 8000));
    CHECK(process_events_until([&] { return preview != nullptr && preview->image() != proxy_before; }, 8000));
    static_cast<void>(process_events_until([] { return false; }, 300));
    save_review_shot("local_028_selection_moved", window, dialog);
    drove = true;
    dialog.accept();
  });
  MainWindowTestAccess::apply_filter_with_dialog(window, QStringLiteral("patchy.filters.pixelate"));
  QApplication::processEvents();
  CHECK(drove);
  CHECK(!canvas->edit_locked());
  CHECK(!canvas->selection_edits_allowed_while_locked());

  // OK filtered the second selection only.
  const auto applied = active_pixels(window);
  CHECK(!same_in(applied, original, second));
  CHECK(same_outside(applied, original, second));
  // The marquee is its own undo step before the filter's.
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 2U);
  MainWindowTestAccess::undo(window);
  CHECK(same_in(active_pixels(window), original, QRect(0, 0, kWidth, kHeight)));
  CHECK(canvas->selected_document_region() == second);
  // The selection step holds the unfiltered layer, never the dialog's preview pixels.
  MainWindowTestAccess::undo(window);
  CHECK(same_in(active_pixels(window), original, QRect(0, 0, kWidth, kHeight)));
  CHECK(canvas->selected_document_region() == first);
}

void ui_filter_dialog_select_commands_preview_and_cancel_keeps_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto original = open_photo_document(window);
  auto* canvas = MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  if (canvas == nullptr) {
    return;
  }
  canvas->set_tool(CanvasTool::Marquee);
  draw_marquee(*canvas, kFirstSelection);
  const auto first = canvas->selected_document_region();
  CHECK(selection_near(first, kFirstSelection));
  QRegion second;
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  const QRect whole(0, 0, kWidth, kHeight);

  bool drove = false;
  when_filter_dialog_shows([&](QDialog& dialog) {
    CHECK(process_events_until([&] { return canvas_preview_follows(window, original, first); }, 8000));
    // Select All from the menu: the whole layer previews.
    require_action(window, "editSelectAllAction")->trigger();
    CHECK(process_events_until(
        [&] {
          const auto& pixels = active_pixels(window);
          return !same_in(pixels, original, kFirstSelection) && !same_in(pixels, original, kSecondSelection);
        },
        8000));
    // Deselect: no selection filters the whole layer too; the preview stays whole.
    require_action(window, "editDeselectAction")->trigger();
    static_cast<void>(process_events_until([] { return false; }, 300));
    CHECK(!same_in(active_pixels(window), original, kSecondSelection));
    // A new marquee from nothing.
    canvas->set_tool(CanvasTool::Marquee);
    draw_marquee(*canvas, kSecondSelection);
    second = canvas->selected_document_region();
    CHECK(selection_near(second, kSecondSelection));
    CHECK(process_events_until([&] { return canvas_preview_follows(window, original, second); }, 8000));
    drove = true;
    dialog.reject();
  });
  MainWindowTestAccess::apply_filter_with_dialog(window, QStringLiteral("patchy.filters.pixelate"));
  QApplication::processEvents();
  CHECK(drove);
  // Cancel discards the filter and keeps the selection edits, each its own step.
  CHECK(same_in(active_pixels(window), original, whole));
  CHECK(canvas->selected_document_region() == second);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 3U);
  for (int step = 0; step < 3; ++step) {
    MainWindowTestAccess::undo(window);
    CHECK(same_in(active_pixels(window), original, whole));
  }
  CHECK(canvas->selected_document_region() == first);
}

}  // namespace

std::vector<patchy::test::TestCase> filter_dialog_selection_tests() {
  return {
      {"ui_filter_dialog_selection_edits_follow_preview_and_apply",
       ui_filter_dialog_selection_edits_follow_preview_and_apply},
      {"ui_filter_dialog_select_commands_preview_and_cancel_keeps_selection",
       ui_filter_dialog_select_commands_preview_and_cancel_keeps_selection},
  };
}
