// The Layers panel's effect rows (GitHub issue 256): a layer's effects listed under its
// row like Smart Filters, with an eye per effect, a master eye, and an Edit pencil or
// double-click that opens the Layer Style dialog on that effect.
#include "core/layer.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_manager.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPointF>
#include <QStringList>
#include <QPoint>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

// A white 64x48 document with a red 20x20 "Badge" square whose style is a blue 3 px
// outside Stroke plus a green Color Overlay (both easy to find in the composite).
patchy::Document styled_document(bool stroke_enabled) {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  auto pixels = solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  for (int y = 14; y < 34; ++y) {
    for (int x = 22; x < 42; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 220;
      px[1] = 40;
      px[2] = 40;
      px[3] = 255;
    }
  }
  auto& badge = document.add_pixel_layer("Badge", std::move(pixels));
  patchy::LayerStroke stroke;
  stroke.enabled = stroke_enabled;
  stroke.size = 3.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{20, 40, 220};
  stroke.opacity = 1.0F;
  badge.layer_style().strokes.push_back(stroke);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.color = patchy::RgbColor{20, 200, 40};
  overlay.opacity = 1.0F;
  badge.layer_style().color_overlays.push_back(overlay);
  document.set_active_layer(badge.id());
  return document;
}

QColor composite_pixel(patchy::ui::MainWindow& window, int x, int y) {
  auto* canvas = require_canvas(window);
  QImage composite;
  CHECK(process_events_until([&] {
    composite = canvas->current_composite();
    return !composite.isNull();
  }));
  CHECK(!composite.isNull());
  return composite.isNull() ? QColor() : composite.pixelColor(x, y);
}

QWidget* badge_row(QListWidget& list) {
  return list.itemWidget(require_layer_item(list, QStringLiteral("Badge")));
}

// The effect entry row whose label reads `name`, or null.
QWidget* effect_entry_row(QWidget& row, const QString& name) {
  for (auto* entry : row.findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"))) {
    auto* label = entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"));
    if (label != nullptr && label->text() == name) {
      return entry;
    }
  }
  return nullptr;
}

// A user-style click on `child` through the list's viewport (the row event filter sees it).
void click_row_child(QListWidget& list, QWidget& child) {
  const auto global = child.mapToGlobal(child.rect().center());
  click_widget_like_a_user(*list.viewport(), list.viewport()->mapFromGlobal(global));
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents();  // the handlers defer past the row rebuild
  }
}

void ui_layer_effect_rows_toggle_effects_and_render() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(true), QStringLiteral("Effect Rows"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto badge_id = *doc.active_layer_id();
  const auto style = [&]() -> const patchy::LayerStyle& { return std::as_const(doc).find_layer(badge_id)->layer_style(); };

  // Rows: the Effects header, then Stroke and Color Overlay in the dialog's order, and the
  // item is tall enough for them (the Smart Filter rule).
  auto* row = badge_row(*list);
  CHECK(row != nullptr);
  CHECK(row->findChildren<QWidget*>(QStringLiteral("layerEffectsRow")).size() == 1);
  const auto entries = row->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"));
  CHECK(entries.size() == 2);
  QStringList names;
  for (auto* entry : entries) {
    names.push_back(entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"))->text());
  }
  CHECK(names == (QStringList{QStringLiteral("Stroke"), QStringLiteral("Color Overlay")}));
  CHECK(require_layer_item(*list, QStringLiteral("Badge"))->sizeHint().height() == 44 + 26 * 3);
  CHECK(require_layer_item(*list, QStringLiteral("Background"))->sizeHint().height() == 44);
  CHECK(row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton")) != nullptr);

  // Rendered: the overlay greens the square, the stroke rings it in blue.
  const auto center_before = composite_pixel(window, 32, 24);
  const auto ring_before = composite_pixel(window, 20, 24);
  CHECK(center_before.green() > 150 && center_before.red() < 100);
  CHECK(ring_before.blue() > 150 && ring_before.red() < 100);
  save_widget_artifact("ui_layer_effect_rows_dark", *layers_dock);

  // The Stroke eye hides the stroke: one undo step, the ring goes, the row rebuilds hidden.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  auto* stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  CHECK(stroke_entry != nullptr);
  click_row_child(*list, *stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton")));
  CHECK(!style().strokes.front().enabled);
  CHECK(style().color_overlays.front().enabled);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Stroke hidden")));
  const auto ring_hidden = composite_pixel(window, 20, 24);
  CHECK(ring_hidden.red() > 240 && ring_hidden.green() > 240 && ring_hidden.blue() > 240);
  row = badge_row(*list);
  stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  CHECK(stroke_entry != nullptr);
  CHECK(!stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"))->isChecked());
  CHECK(!stroke_entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"))->isEnabled());
  CHECK(row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton")) != nullptr);  // hidden keeps the badge

  // The header eye hides every effect: the square shows its own red again.
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(!style().effects_visible);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 2);
  const auto center_hidden = composite_pixel(window, 32, 24);
  CHECK(center_hidden.red() > 150 && center_hidden.green() < 100);
  row = badge_row(*list);
  CHECK(!row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton"))->isChecked());
  CHECK(!row->findChild<QLabel*>(QStringLiteral("layerEffectsLabel"))->isEnabled());

  // Undo restores both switches and the render.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(style().effects_visible);
  CHECK(style().strokes.front().enabled);
  CHECK(composite_pixel(window, 20, 24).blue() > 150);
  CHECK(composite_pixel(window, 32, 24).green() > 150);

  // Light: the rows and their eyes render in the light palette too.
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();
  row = badge_row(*list);
  CHECK(row != nullptr);
  auto* light_eye = row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton"));
  CHECK(light_eye != nullptr);
  const auto light_icon = light_eye->icon().pixmap(QSize(15, 15)).toImage();
  int opaque = 0;
  for (int y = 0; y < light_icon.height(); ++y) {
    for (int x = 0; x < light_icon.width(); ++x) {
      opaque += light_icon.pixelColor(x, y).alpha() > 32 ? 1 : 0;
    }
  }
  CHECK(opaque > 10);
  save_widget_artifact("ui_layer_effect_rows_light", *layers_dock);
}

// A hidden Stroke row: double-clicking it (and the pencil on the Color Overlay row) opens
// the Layer Style dialog on that effect without turning it on; Cancel leaves it hidden.
void ui_layer_effect_row_double_click_and_pencil_open_dialog_on_effect() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(false), QStringLiteral("Effect Row Edit"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto badge_id = *doc.active_layer_id();

  struct Seen {
    bool found{false};
    QString current_label;
    bool checked{false};
  };
  // The row handlers defer the dialog past the row rebuild, so poll for it (and always
  // close it, so a failure cannot leave the modal dialog up).
  const std::function<void(Seen&, int)> watch_dialog = [&watch_dialog](Seen& seen, int attempts) {
    QTimer::singleShot(20, [&seen, &watch_dialog, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
      if (dialog == nullptr || !dialog->isVisible()) {
        if (attempts > 0) {
          watch_dialog(seen, attempts - 1);
        }
        return;
      }
      seen.found = true;
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      if (categories != nullptr && categories->currentItem() != nullptr) {
        seen.current_label = categories->currentItem()->text();
        seen.checked = categories->currentItem()->checkState() == Qt::Checked;
      }
      dialog->reject();
    });
  };

  auto* row = badge_row(*list);
  auto* stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  CHECK(stroke_entry != nullptr);
  CHECK(!stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"))->isChecked());
  Seen by_double_click;
  watch_dialog(by_double_click, 100);
  {
    auto* label = stroke_entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"));
    const auto viewport_pos = list->viewport()->mapFromGlobal(label->mapToGlobal(label->rect().center()));
    QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(viewport_pos),
                             list->viewport()->mapToGlobal(viewport_pos), Qt::LeftButton, Qt::LeftButton,
                             Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &double_click);
  }
  process_events_for(300);
  CHECK(by_double_click.found);
  CHECK(by_double_click.current_label == QStringLiteral("Stroke"));
  CHECK(!by_double_click.checked);
  CHECK(!std::as_const(doc).find_layer(badge_id)->layer_style().strokes.front().enabled);

  row = badge_row(*list);
  auto* overlay_entry = effect_entry_row(*row, QStringLiteral("Color Overlay"));
  CHECK(overlay_entry != nullptr);
  Seen by_pencil;
  watch_dialog(by_pencil, 100);
  click_row_child(*list, *overlay_entry->findChild<QToolButton*>(QStringLiteral("layerEffectEditButton")));
  process_events_for(300);
  CHECK(by_pencil.found);
  CHECK(by_pencil.current_label == QStringLiteral("Color Overlay"));
  CHECK(by_pencil.checked);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_effect_rows_tests() {
  return {
      {"ui_layer_effect_rows_toggle_effects_and_render", ui_layer_effect_rows_toggle_effects_and_render},
      {"ui_layer_effect_row_double_click_and_pencil_open_dialog_on_effect",
       ui_layer_effect_row_double_click_and_pencil_open_dialog_on_effect},
  };
}
